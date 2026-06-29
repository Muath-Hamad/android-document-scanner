# Migration Plan: `com.websitebeaver:opencv` → `nihui/opencv-mobile`

## 1. Goal

Replace the current full OpenCV Android dependency
(`com.websitebeaver:opencv:4.1.0`, essentially the official OpenCV Java SDK with
`libopencv_java4.so`) with [`nihui/opencv-mobile`](https://github.com/nihui/opencv-mobile)
to **drastically reduce the APK size** of apps that consume this library, while
keeping the public Kotlin API of `documentscanner` unchanged and touching as little
library logic as possible.

### Expected size impact

`opencv-mobile` is a stripped, statically-linkable build of OpenCV. The official
Android pack is ~303 MB of SDK shipping a multi-MB `libopencv_java4.so` per ABI; the
opencv-mobile Android pack is ~18.8 MB and links **only the functions we use** into a
single small `.so` per ABI. In practice the bundled OpenCV native payload in a
consuming APK drops from "tens of MB per ABI" to typically **< 2 MB per ABI**.

---

## 2. The core constraint (read this first)

**`opencv-mobile` deliberately removes the Java/JNI bindings** (`opencv_java4`,
`org.opencv.*`). It ships **C++ only**. Its own docs say: *"the opencv_java module is
discarded — wrap your C++ code with JNI."*

This library currently calls the OpenCV **Java API directly** from Kotlin:

| File | OpenCV Java usage |
|------|-------------------|
| `documentscanner/.../DocumentDetector.kt` | `Utils`, `Core`, `CvType`, `Mat`, `MatOfPoint`, `MatOfPoint2f`, `Point`, `Size`, `Imgproc` — the full corner-detection algorithm |
| `documentscanner/.../utils/ImageUtil.kt` | `Utils`, `Mat`, `MatOfPoint2f`, `Point`, `Size`, `Imgcodecs`, `Imgproc` — image read + perspective crop/warp |
| `documentscanner/.../extensions/Point.kt` | `org.opencv.core.Point` (used as a plain `(x, y)` data holder) |
| `documentscanner/.../models/Quad.kt` | `org.opencv.core.Point` (plain data holder) |
| `documentscanner/.../DocumentScannerActivity.kt` | `import org.opencv.core.Point` + `System.loadLibrary("opencv_java4")` |

**Consequence:** There is no drop-in Maven artifact swap. The two pieces of real
image-processing logic (`DocumentDetector.findDocumentCorners` and `ImageUtil`'s
read/crop) must be **ported to C++** and exposed to Kotlin through a thin custom JNI
layer. Everything else (UI, activity flow, models) stays in Kotlin.

This is the unavoidable cost of moving to opencv-mobile. The plan below isolates that
cost into one small native module so the rest of the library is barely touched.

---

## 3. Target architecture

```
Kotlin (unchanged public API)
  DocumentDetector.findDocumentCorners(bitmap)  ─┐
  ImageUtil.getImageFromFilePath / crop(...)    ─┤── thin Kotlin wrappers
                                                  │
        JNI boundary (new):  NativeOpenCV.kt  ────┘
                                                  │
  C++ (new): documentscanner/src/main/cpp/
        document_detector.cpp   (port of findDocumentCorners + findCorners)
        image_util.cpp          (port of imread/cvtColor/warpPerspective crop)
        jni_bridge.cpp          (JNI entry points)
                                                  │
        opencv-mobile static libs (.a)  ──────────┘  → linked into ONE libdocumentscanner.so
```

Key decisions:

- **Keep the JNI surface tiny** — 2–3 native functions that take/return Android
  `Bitmap` + primitive arrays, not OpenCV `Mat`s. This avoids re-implementing the
  OpenCV Java type system and keeps marshalling simple.
- **Replace `org.opencv.core.Point`** with a 6-line in-repo `Point(x: Double, y: Double)`
  class so `Point.kt`, `Quad.kt`, `DocumentDetector`, and the activity import compile
  without OpenCV. This is a mechanical import swap, not a logic change.
- **Static linking** — link opencv-mobile's `.a` libraries so only used symbols end up
  in the final `.so`. This is what produces the size win.
- The library AAR will **bundle the built `.so`** per ABI; consuming apps need **no NDK
  setup** and no OpenCV dependency at all.

---

## 4. Prerequisites

- Android NDK (r25+ recommended; opencv-mobile v35 is built with r29 but is
  backward-compatible). Install via SDK Manager or `ndkVersion` in gradle.
- CMake 3.18+ (Android SDK CMake is fine).
- The opencv-mobile Android release zip, e.g.
  `opencv-mobile-4.x.y-android.zip` from
  https://github.com/nihui/opencv-mobile/releases (latest stable, v35 at time of
  writing). It contains `sdk/native/jni` with `OpenCVConfig.cmake` and prebuilt static
  libs for `arm64-v8a`, `armeabi-v7a`, `x86`, `x86_64`.

---

## 5. Step-by-step

### Step 1 — Vendor the opencv-mobile SDK

1. Download the opencv-mobile Android zip and extract it under the library module, e.g.
   `documentscanner/src/main/cpp/opencv-mobile-<ver>-android/`.
2. Decide whether to commit it to the repo (simplest, ~18 MB) or fetch it at build time
   via a Gradle download task / Git LFS. **Recommendation:** commit it (or use LFS) so
   CI and Maven Central publishing stay self-contained and reproducible.
3. Add a short `THIRD_PARTY/LICENSES` note — opencv-mobile is Apache-2.0; record
   attribution.

### Step 2 — Add NDK/CMake build to the library module

In `documentscanner/build.gradle`, inside `android { ... }`:

```gradle
android {
    // ...
    defaultConfig {
        // ...
        externalNativeBuild {
            cmake {
                cppFlags "-std=c++11 -fvisibility=hidden -ffunction-sections -fdata-sections"
                arguments "-DANDROID_STL=c++_static"
            }
        }
        ndk {
            // start with the two ABIs that matter most for size/coverage,
            // expand if needed
            abiFilters 'arm64-v8a', 'armeabi-v7a' // optionally 'x86', 'x86_64'
        }
    }
    externalNativeBuild {
        cmake {
            path "src/main/cpp/CMakeLists.txt"
            version "3.22.1"
        }
    }
    // ndkVersion "26.x.x"  // pin to match CI
}
```

Remove the OpenCV Java dependency from `dependencies`:

```diff
- implementation 'com.websitebeaver:opencv:4.1.0'
```

### Step 3 — Write `CMakeLists.txt`

`documentscanner/src/main/cpp/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.18)
project(documentscanner)

set(OpenCV_DIR ${CMAKE_SOURCE_DIR}/opencv-mobile-<ver>-android/sdk/native/jni)
find_package(OpenCV REQUIRED core imgproc)   # only the modules we use

add_library(documentscanner SHARED
    jni_bridge.cpp
    document_detector.cpp
    image_util.cpp)

find_library(jnigraphics-lib jnigraphics)  # for AndroidBitmap_* APIs
find_library(log-lib log)

target_link_libraries(documentscanner
    ${OpenCV_LIBS}
    ${jnigraphics-lib}
    ${log-lib})

# strip + GC sections to keep the .so small
target_link_options(documentscanner PRIVATE -Wl,--gc-sections -Wl,--strip-all)
```

> Note: `highgui`/`imgcodecs` may be needed if you keep `Imgcodecs.imread` (see Step 5
> decision). Prefer dropping it and decoding via Android `BitmapFactory` to keep the
> module set minimal (`core` + `imgproc` only).

### Step 4 — Implement the C++ / JNI layer

Port the existing Kotlin algorithms 1:1 into C++. The OpenCV C++ API mirrors the Java
API, so this is largely mechanical.

**`jni_bridge.cpp` — proposed JNI surface (keep it this small):**

```cpp
// Returns 8 doubles: [tlx,tly, trx,try, brx,bry, blx,bly], or empty if none found.
extern "C" JNIEXPORT jdoubleArray JNICALL
Java_com_websitebeaver_documentscanner_NativeOpenCV_findDocumentCorners(
    JNIEnv* env, jobject, jobject bitmap);

// Reads file at path, warps the 4 corners to a rectangle, returns a new ARGB_8888 Bitmap.
extern "C" JNIEXPORT jobject JNICALL
Java_com_websitebeaver_documentscanner_NativeOpenCV_cropAndWarp(
    JNIEnv* env, jobject, jstring filePath, jdoubleArray corners);

// Reads file at path (with EXIF rotation handled on the Kotlin side), returns a Bitmap.
extern "C" JNIEXPORT jobject JNICALL
Java_com_websitebeaver_documentscanner_NativeOpenCV_readImage(
    JNIEnv* env, jobject, jstring filePath);
```

Use `AndroidBitmap_getInfo` / `AndroidBitmap_lockPixels` (from `jnigraphics`) to convert
between `Bitmap` and `cv::Mat` — this replaces `org.opencv.android.Utils.bitmapToMat` /
`matToBitmap`.

**Algorithm port mapping (Java API → C++ API):**

| Kotlin (current) | C++ (opencv-mobile) |
|---|---|
| `Utils.bitmapToMat` / `matToBitmap` | `AndroidBitmap_*` + manual `cv::Mat` copy |
| `Imgproc.resize` | `cv::resize` |
| `Imgproc.cvtColor(..., COLOR_BGR2Luv)` | `cv::cvtColor(..., cv::COLOR_BGR2Luv)` |
| `Core.split` | `cv::split` |
| `Imgproc.GaussianBlur` | `cv::GaussianBlur` |
| `Imgproc.threshold(THRESH_BINARY+THRESH_OTSU)` | `cv::threshold` |
| `Imgproc.Canny` | `cv::Canny` |
| `Imgproc.morphologyEx(MORPH_CLOSE, ones)` | `cv::morphologyEx` + `cv::Mat::ones` |
| `Imgproc.findContours(RETR_LIST, CHAIN_APPROX_SIMPLE)` | `cv::findContours` |
| `Imgproc.approxPolyDP` / `arcLength` | `cv::approxPolyDP` / `cv::arcLength` |
| `Imgproc.contourArea` / `isContourConvex` | `cv::contourArea` / `cv::isContourConvex` |
| `Imgcodecs.imread` (in `ImageUtil`) | `cv::imread` **or** decode in Kotlin (preferred — drops the imgcodecs module) |
| `Imgproc.getPerspectiveTransform` / `warpPerspective` | `cv::getPerspectiveTransform` / `cv::warpPerspective` |

Keep the exact constants, thresholds, and sort/order logic from the current Kotlin so
detection behavior is unchanged.

### Step 5 — Add the Kotlin JNI wrapper + native loader

Create `documentscanner/.../NativeOpenCV.kt`:

```kotlin
internal object NativeOpenCV {
    init { System.loadLibrary("documentscanner") }
    external fun findDocumentCorners(bitmap: Bitmap): DoubleArray
    external fun cropAndWarp(filePath: String, corners: DoubleArray): Bitmap
    external fun readImage(filePath: String): Bitmap
}
```

In `DocumentScannerActivity.kt`, replace:

```diff
- System.loadLibrary("opencv_java4")
+ // native lib is loaded lazily by NativeOpenCV; nothing to do here
```

(or load `"documentscanner"` here if you want the same eager-load + error-toast behavior).

**Decision on `Imgcodecs.imread`:** the cleanest minimal-module path is to decode the
file to a `Bitmap` in Kotlin (the existing `ImageUtil` already has a `BitmapFactory` +
EXIF fallback path) and pass the `Bitmap` into native code, dropping `cv::imread` and the
`imgcodecs`/`highgui` modules entirely. If you keep `cv::imread`, add `imgcodecs` to
`find_package`.

### Step 6 — Rewire the call sites (minimal Kotlin edits)

1. **New `Point` type.** Add a tiny in-repo class, e.g.
   `documentscanner/.../models/Point.kt`:
   ```kotlin
   data class Point(val x: Double = 0.0, val y: Double = 0.0)
   ```
   Then change the imports only:
   - `extensions/Point.kt`: `import org.opencv.core.Point` → the new class.
   - `models/Quad.kt`: same import swap (constructor logic unchanged).
   - `DocumentScannerActivity.kt`: same import swap.
   - `DocumentDetector.kt`: return type stays `List<Point>?` but now the new `Point`.

2. **`DocumentDetector.kt`** becomes a thin wrapper:
   ```kotlin
   fun findDocumentCorners(image: Bitmap): List<Point>? {
       val c = NativeOpenCV.findDocumentCorners(image)
       if (c.isEmpty()) return null
       return listOf(
           Point(c[0], c[1]), Point(c[2], c[3]),
           Point(c[4], c[5]), Point(c[6], c[7])
       )
   }
   ```
   (Corner ordering / scaling now lives in C++; keep identical logic there.)

3. **`ImageUtil.kt`**: `getImageFromFilePath` → `NativeOpenCV.readImage(path)`;
   `crop` → build the `DoubleArray` of corners and call `NativeOpenCV.cropAndWarp`.
   `readBitmapFromFileUriString` is pure Android and stays as-is.

> These are the only logic files touched, and each shrinks to a few lines that delegate
> to native. The public method signatures of `DocumentDetector`, `ImageUtil`,
> `DocumentScanner`, and `DocumentScannerActivity` are preserved, so consumers see no API
> change.

### Step 7 — ProGuard / consumer rules

Add to `documentscanner/consumer-rules.pro` (so consumers don't strip the JNI entry
points):

```proguard
-keep class com.websitebeaver.documentscanner.NativeOpenCV { *; }
```

JNI `external` methods are resolved by name; R8 must not rename/remove them.

### Step 8 — Build, CI, and publishing changes

- **`deploy-to-maven-central.yml`**: the build now requires the NDK + CMake. On
  `ubuntu-latest` the NDK is usually preinstalled; otherwise add a step to install it
  (e.g. `sdkmanager "ndk;26.x.x" "cmake;3.22.1"`) and set `ndkVersion` in gradle. Verify
  the AAR contains `jni/<abi>/libdocumentscanner.so` for every configured ABI.
- **Version bump** in `documentscanner/build.gradle` (`version = "1.4.0"`), since the
  native packaging changes and the minimum integration story changes.
- Confirm `minSdk 21` still holds (opencv-mobile targets API 21 — compatible).

### Step 9 — Verification

1. **Builds:** `./gradlew :documentscanner:assembleRelease` produces an AAR with the
   `.so`(s); `./gradlew :documentscanner-demo:assembleRelease` builds the demo.
2. **Size check:** compare APK size of the demo before/after (use APK Analyzer or
   `unzip -l`); confirm `libopencv_java4.so` is gone and `libdocumentscanner.so` is small.
3. **Functional parity:** run the demo on real devices (at least one arm64 and one
   armeabi-v7a, or emulators for x86/x86_64 if those ABIs are kept). Verify:
   - corner auto-detection matches the old behavior on the same sample photos,
   - manual cropping + perspective warp output is visually identical,
   - multi-document scan and the "remove cropper" / `maxNumDocuments` paths still work.
4. **Regression:** test images that previously failed detection (so OpenCV falls back to
   the `BitmapFactory`/EXIF path) still behave the same.

### Step 10 — Docs

Update `README.md`: note the size reduction, the new native packaging, supported ABIs,
and remove any mention of the old OpenCV dependency. If consumers previously had to do
anything OpenCV-related, document that it's no longer needed.

---

## 6. Risks & mitigations

| Risk | Mitigation |
|------|------------|
| Porting the algorithm to C++ subtly changes detection results | Port constants/order 1:1; validate against a fixed set of sample images before/after |
| `Mat`↔`Bitmap` color/channel mismatch (RGBA vs BGR) | Be explicit about channel order in JNI; the current code already juggles BGR/RGB — replicate exactly |
| APK gets a `.so` per ABI → universal APK grows even if per-split shrinks | Recommend consumers use ABI splits / app bundles; ship only arm64-v8a + armeabi-v7a by default |
| Maven Central build needs NDK | Pin `ndkVersion`, add an explicit install step in CI |
| Committing ~18 MB SDK bloats the repo | Use Git LFS or a Gradle download-and-cache task instead of committing raw |
| opencv-mobile lacks a module you later need (dnn, calib3d, etc.) | Not used today; if needed later, prefer a dedicated lib (e.g. ncnn) per opencv-mobile guidance |

---

## 7. Files touched (summary)

**New:**
- `documentscanner/src/main/cpp/CMakeLists.txt`
- `documentscanner/src/main/cpp/jni_bridge.cpp`
- `documentscanner/src/main/cpp/document_detector.cpp`
- `documentscanner/src/main/cpp/image_util.cpp`
- `documentscanner/src/main/cpp/opencv-mobile-<ver>-android/` (vendored SDK or LFS)
- `documentscanner/.../NativeOpenCV.kt`
- `documentscanner/.../models/Point.kt`

**Modified (small/mechanical):**
- `documentscanner/build.gradle` (remove OpenCV dep, add NDK/CMake/abiFilters, version)
- `documentscanner/.../DocumentDetector.kt` (delegate to native)
- `documentscanner/.../utils/ImageUtil.kt` (delegate to native)
- `documentscanner/.../extensions/Point.kt` (import swap)
- `documentscanner/.../models/Quad.kt` (import swap)
- `documentscanner/.../DocumentScannerActivity.kt` (import swap + loader change)
- `documentscanner/consumer-rules.pro` (keep JNI class)
- `.github/workflows/deploy-to-maven-central.yml` (NDK/CMake availability)
- `README.md`

**Unchanged:** all UI (`ui/*`), `models/Document.kt`, `models/Line.kt`,
`enums/QuadCorner.kt`, `DocumentScanner.kt`, constants, resources, and the public API.

---

## 8. Effort estimate

- C++/JNI port + CMake wiring: the bulk of the work (~1–2 days incl. device testing).
- Kotlin rewiring + import swaps: a few hours (mechanical).
- CI/publishing + docs: a few hours.

The algorithm is small and well-contained, which is what makes this migration feasible
with minimal disruption to the library's Kotlin code.
