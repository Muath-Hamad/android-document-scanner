cmake_minimum_required(VERSION 3.18)
project(documentscanner)

# ---------------------------------------------------------------------------
# Obtain the opencv-mobile (nihui) prebuilt Android SDK.
#
# By default the SDK is downloaded + extracted into the build directory the first
# time CMake configures the project, so it does not need to be committed to the
# repository. To use a local copy instead, pass -DOPENCV_MOBILE_DIR=/path/to/sdk
# (the directory that contains sdk/native/jni).
#
# NDK COMPATIBILITY: the opencv-mobile prebuilt static libraries are compiled
# against the libc++ / libomp that shipped with the NDK used to build that
# release. A release built with a newer toolchain references libc++ symbols
# (e.g. std::__libcpp_verbose_abort) that an older NDK can't provide, which
# fails at link time with "undefined symbol" errors.
#
# 4.9.0 (v25) is built with clang 14 (NDK r25), so it links cleanly with NDK r25
# and - because newer libc++/libomp keep those symbols for ABI stability - with
# newer NDKs too. It is also the first release that lowered the OpenCV minimum
# Android API level back to 21, so it matches this module's minSdk 21 (the older
# clang-14 build 4.8.1 requires API 24). That combination makes it the most
# portable default. If you build with a very new NDK and want the latest OpenCV,
# override the version to a release built with a matching toolchain, e.g.:
#   externalNativeBuild { cmake { arguments "-DOPENCV_MOBILE_VERSION=4.13.0",
#                                            "-DOPENCV_MOBILE_RELEASE=v35" } }
# (v35/4.13.0 requires NDK r29+).
# ---------------------------------------------------------------------------
# Defaults are plain (non-cache) variables so changing them here always takes
# effect; pass -DOPENCV_MOBILE_VERSION=.../-DOPENCV_MOBILE_RELEASE=... to override.
if(NOT DEFINED OPENCV_MOBILE_VERSION)
    set(OPENCV_MOBILE_VERSION "4.9.0")
endif()
if(NOT DEFINED OPENCV_MOBILE_RELEASE)
    set(OPENCV_MOBILE_RELEASE "v25")
endif()

if(NOT DEFINED OPENCV_MOBILE_DIR)
    set(OPENCV_MOBILE_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/opencv-mobile-${OPENCV_MOBILE_VERSION}-android")
endif()

if(NOT EXISTS "${OPENCV_MOBILE_DIR}/sdk/native/jni/OpenCVConfig.cmake")
    set(_opencv_zip
        "${CMAKE_CURRENT_BINARY_DIR}/opencv-mobile-${OPENCV_MOBILE_VERSION}-android.zip")
    if(NOT EXISTS "${_opencv_zip}")
        set(_opencv_url
            "https://github.com/nihui/opencv-mobile/releases/download/${OPENCV_MOBILE_RELEASE}/opencv-mobile-${OPENCV_MOBILE_VERSION}-android.zip")
        message(STATUS "Downloading opencv-mobile from ${_opencv_url}")
        file(DOWNLOAD "${_opencv_url}" "${_opencv_zip}"
            SHOW_PROGRESS
            STATUS _opencv_dl_status
            TLS_VERIFY ON)
        list(GET _opencv_dl_status 0 _opencv_dl_code)
        if(NOT _opencv_dl_code EQUAL 0)
            file(REMOVE "${_opencv_zip}")
            list(GET _opencv_dl_status 1 _opencv_dl_msg)
            message(FATAL_ERROR "Failed to download opencv-mobile: ${_opencv_dl_msg}")
        endif()
    endif()
    message(STATUS "Extracting opencv-mobile to ${CMAKE_CURRENT_BINARY_DIR}")
    file(ARCHIVE_EXTRACT INPUT "${_opencv_zip}" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# Only request the modules we actually use, so the linker can drop the rest.
set(OpenCV_DIR "${OPENCV_MOBILE_DIR}/sdk/native/jni")
find_package(OpenCV REQUIRED core imgproc highgui)

add_library(documentscanner SHARED
    jni_bridge.cpp
    document_processor.cpp)

find_library(jnigraphics-lib jnigraphics)
find_library(log-lib log)

target_include_directories(documentscanner PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(documentscanner
    ${OpenCV_LIBS}
    ${jnigraphics-lib}
    ${log-lib})

# garbage collect unused sections and strip symbols to keep the .so small
target_compile_options(documentscanner PRIVATE -ffunction-sections -fdata-sections)
target_link_options(documentscanner PRIVATE
    -Wl,--gc-sections
    "$<$<CONFIG:Release>:-Wl,--strip-all>")
