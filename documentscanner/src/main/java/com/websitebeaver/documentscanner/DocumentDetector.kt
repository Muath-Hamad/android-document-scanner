package com.websitebeaver.documentscanner

import android.graphics.Bitmap
import com.websitebeaver.documentscanner.models.Point

/**
 * This class finds document corners. The actual OpenCV work runs in native code
 * (see src/main/cpp), which uses opencv-mobile instead of the OpenCV Java bindings.
 *
 * @constructor creates document detector
 */
class DocumentDetector {

    /**
     * take a photo with a document, and find the document's corners
     *
     * @param image a photo with a document
     * @return a list with document corners (top left, top right, bottom left, bottom right)
     */
    fun findDocumentCorners(image: Bitmap): List<Point>? {
        val corners = NativeOpenCV.findDocumentCorners(image)

        // native code returns an empty array when it can't find the document corners
        if (corners.size < 8) {
            return null
        }

        return listOf(
            Point(corners[0], corners[1]),
            Point(corners[2], corners[3]),
            Point(corners[4], corners[5]),
            Point(corners[6], corners[7])
        )
    }
}
