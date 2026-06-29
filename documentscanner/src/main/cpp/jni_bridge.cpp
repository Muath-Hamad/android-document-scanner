#include <jni.h>
#include <android/bitmap.h>
#include <android/log.h>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>

#include <vector>

#include "document_processor.hpp"

#define LOG_TAG "documentscanner-native"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

/**
 * Copy an ARGB_8888 Android Bitmap into an RGBA (CV_8UC4) cv::Mat. This replaces
 * org.opencv.android.Utils.bitmapToMat.
 */
bool bitmapToMatRgba(JNIEnv *env, jobject bitmap, cv::Mat &out) {
    AndroidBitmapInfo info;
    if (AndroidBitmap_getInfo(env, bitmap, &info) != ANDROID_BITMAP_RESULT_SUCCESS) {
        LOGE("AndroidBitmap_getInfo failed");
        return false;
    }
    if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
        LOGE("unsupported bitmap format %d (expected RGBA_8888)", info.format);
        return false;
    }

    void *pixels = nullptr;
    if (AndroidBitmap_lockPixels(env, bitmap, &pixels) != ANDROID_BITMAP_RESULT_SUCCESS) {
        LOGE("AndroidBitmap_lockPixels failed");
        return false;
    }

    // wrap the locked pixels (honouring the row stride), then copy into a tightly packed Mat
    cv::Mat wrapped(
        static_cast<int>(info.height),
        static_cast<int>(info.width),
        CV_8UC4,
        pixels,
        info.stride);
    wrapped.copyTo(out);

    AndroidBitmap_unlockPixels(env, bitmap);
    return true;
}

/**
 * Create a new ARGB_8888 Android Bitmap from a cv::Mat (RGB, RGBA or grayscale). This
 * replaces org.opencv.android.Utils.matToBitmap + Bitmap.createBitmap.
 */
jobject matToBitmap(JNIEnv *env, const cv::Mat &mat) {
    cv::Mat rgba;
    switch (mat.channels()) {
        case 1:
            cv::cvtColor(mat, rgba, cv::COLOR_GRAY2RGBA);
            break;
        case 3:
            cv::cvtColor(mat, rgba, cv::COLOR_RGB2RGBA);
            break;
        case 4:
            rgba = mat;
            break;
        default:
            LOGE("unsupported channel count %d", mat.channels());
            return nullptr;
    }

    jclass bitmapClass = env->FindClass("android/graphics/Bitmap");
    jclass configClass = env->FindClass("android/graphics/Bitmap$Config");
    jfieldID argb8888Field = env->GetStaticFieldID(
        configClass, "ARGB_8888", "Landroid/graphics/Bitmap$Config;");
    jobject argb8888 = env->GetStaticObjectField(configClass, argb8888Field);
    jmethodID createBitmap = env->GetStaticMethodID(
        bitmapClass,
        "createBitmap",
        "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;");
    jobject bitmap = env->CallStaticObjectMethod(
        bitmapClass, createBitmap, rgba.cols, rgba.rows, argb8888);
    if (bitmap == nullptr) {
        LOGE("Bitmap.createBitmap returned null");
        return nullptr;
    }

    AndroidBitmapInfo info;
    if (AndroidBitmap_getInfo(env, bitmap, &info) != ANDROID_BITMAP_RESULT_SUCCESS) {
        return nullptr;
    }
    void *pixels = nullptr;
    if (AndroidBitmap_lockPixels(env, bitmap, &pixels) != ANDROID_BITMAP_RESULT_SUCCESS) {
        return nullptr;
    }

    cv::Mat dst(
        static_cast<int>(info.height),
        static_cast<int>(info.width),
        CV_8UC4,
        pixels,
        info.stride);
    rgba.copyTo(dst);

    AndroidBitmap_unlockPixels(env, bitmap);
    return bitmap;
}

} // namespace

extern "C" {

JNIEXPORT jdoubleArray JNICALL
Java_com_websitebeaver_documentscanner_NativeOpenCV_findDocumentCorners(
    JNIEnv *env, jobject /* thiz */, jobject bitmap) {
    cv::Mat rgba;
    if (!bitmapToMatRgba(env, bitmap, rgba)) {
        return env->NewDoubleArray(0);
    }

    std::vector<cv::Point2d> corners = docscanner::findDocumentCorners(rgba);
    if (corners.size() != 4) {
        return env->NewDoubleArray(0);
    }

    jdouble buffer[8];
    for (int i = 0; i < 4; ++i) {
        buffer[i * 2] = corners[i].x;
        buffer[i * 2 + 1] = corners[i].y;
    }

    jdoubleArray result = env->NewDoubleArray(8);
    env->SetDoubleArrayRegion(result, 0, 8, buffer);
    return result;
}

JNIEXPORT jobject JNICALL
Java_com_websitebeaver_documentscanner_NativeOpenCV_readImageFromFile(
    JNIEnv *env, jobject /* thiz */, jstring filePath) {
    const char *path = env->GetStringUTFChars(filePath, nullptr);
    cv::Mat bgr = cv::imread(path);
    env->ReleaseStringUTFChars(filePath, path);

    // OpenCV failed to read the image (it's empty); let the caller fall back to
    // BitmapFactory by returning null
    if (bgr.empty()) {
        return nullptr;
    }

    // convert to RGB since OpenCV reads using BGR color space
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    return matToBitmap(env, rgb);
}

JNIEXPORT jobject JNICALL
Java_com_websitebeaver_documentscanner_NativeOpenCV_crop(
    JNIEnv *env, jobject /* thiz */, jobject bitmap, jdoubleArray cornersArray) {
    cv::Mat rgba;
    if (!bitmapToMatRgba(env, bitmap, rgba)) {
        return nullptr;
    }

    if (env->GetArrayLength(cornersArray) != 8) {
        LOGE("crop expects 8 corner values");
        return nullptr;
    }

    jdouble *c = env->GetDoubleArrayElements(cornersArray, nullptr);
    std::vector<cv::Point2d> corners{
        cv::Point2d(c[0], c[1]),
        cv::Point2d(c[2], c[3]),
        cv::Point2d(c[4], c[5]),
        cv::Point2d(c[6], c[7])};
    env->ReleaseDoubleArrayElements(cornersArray, c, JNI_ABORT);

    cv::Mat output = docscanner::cropAndWarp(rgba, corners);
    return matToBitmap(env, output);
}

} // extern "C"
