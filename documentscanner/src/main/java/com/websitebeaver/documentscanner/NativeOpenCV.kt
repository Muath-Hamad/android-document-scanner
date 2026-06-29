package com.websitebeaver.documentscanner

import android.graphics.Bitmap

/**
 * Thin JNI bridge to the native (opencv-mobile based) image processing code. The
 * heavy OpenCV work runs in C++ (see src/main/cpp), so the library no longer depends
 * on the OpenCV Java bindings / opencv_java4.so.
 */
internal object NativeOpenCV {
    init {
        System.loadLibrary("documentscanner")
    }

    /**
     * Detect the document's corners in a photo.
     *
     * @param bitmap the photo (ARGB_8888)
     * @return 8 doubles (x, y for top left, top right, bottom left, bottom right) or an
     *         empty array if no document was found
     */
    external fun findDocumentCorners(bitmap: Bitmap): DoubleArray

    /**
     * Read an image file into a bitmap using OpenCV.
     *
     * @param filePath image file path
     * @return the decoded bitmap, or null if OpenCV couldn't read the file
     */
    external fun readImageFromFile(filePath: String): Bitmap?

    /**
     * Crop the document out of the photo and warp it to a straight-on rectangle.
     *
     * @param bitmap the source photo (ARGB_8888)
     * @param corners 8 doubles (x, y for top left, top right, bottom right, bottom left)
     * @return the cropped + warped document bitmap, or null on failure
     */
    external fun crop(bitmap: Bitmap, corners: DoubleArray): Bitmap?
}
