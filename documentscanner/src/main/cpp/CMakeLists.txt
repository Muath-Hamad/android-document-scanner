cmake_minimum_required(VERSION 3.18)
project(documentscanner)

# ---------------------------------------------------------------------------
# Obtain the opencv-mobile (nihui) prebuilt Android SDK.
#
# By default the SDK is downloaded + extracted into the build directory the first
# time CMake configures the project, so it does not need to be committed to the
# repository. To use a local copy instead, pass -DOPENCV_MOBILE_DIR=/path/to/sdk
# (the directory that contains sdk/native/jni).
# ---------------------------------------------------------------------------
set(OPENCV_MOBILE_VERSION "4.13.0" CACHE STRING "opencv-mobile version")
set(OPENCV_MOBILE_RELEASE "v35" CACHE STRING "opencv-mobile release tag")

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
