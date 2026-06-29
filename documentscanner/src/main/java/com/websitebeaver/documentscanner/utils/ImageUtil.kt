package com.websitebeaver.documentscanner.utils

import android.content.ContentResolver
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Matrix
import android.media.ExifInterface
import android.net.Uri
import com.websitebeaver.documentscanner.NativeOpenCV
import com.websitebeaver.documentscanner.models.Quad
import java.io.File

/**
 * This class contains helper functions for processing images
 *
 * @constructor creates image util
 */
class ImageUtil {
    /**
     * decode an image file to a bitmap
     *
     * @param filePath image is saved here
     * @return image bitmap
     */
    private fun decodeImageFromFilePath(filePath: String): Bitmap {
        // read image using OpenCV (native)
        NativeOpenCV.readImageFromFile(filePath)?.let { return it }

        // if OpenCV fails to read the image then make sure the file is actually readable
        if (!File(filePath).exists()) {
            throw Exception("File doesn't exist - $filePath")
        }

        if (!File(filePath).canRead()) {
            throw Exception("You don't have permission to read $filePath")
        }

        // try reading image without OpenCV, applying EXIF rotation
        var imageBitmap = BitmapFactory.decodeFile(filePath)
        val rotation = when (ExifInterface(filePath).getAttributeInt(
            ExifInterface.TAG_ORIENTATION,
            ExifInterface.ORIENTATION_NORMAL
        )) {
            ExifInterface.ORIENTATION_ROTATE_90 -> 90
            ExifInterface.ORIENTATION_ROTATE_180 -> 180
            ExifInterface.ORIENTATION_ROTATE_270 -> 270
            else -> 0
        }
        imageBitmap = Bitmap.createBitmap(
            imageBitmap,
            0,
            0,
            imageBitmap.width,
            imageBitmap.height,
            Matrix().apply { postRotate(rotation.toFloat()) },
            true
        )

        return imageBitmap
    }

    /**
     * get bitmap image from file path
     *
     * @param filePath image is saved here
     * @return image bitmap
     */
    fun getImageFromFilePath(filePath: String): Bitmap {
        return this.decodeImageFromFilePath(filePath)
    }

    /**
     * take a photo with a document, crop everything out but document, and force it to display
     * as a rectangle
     *
     * @param photoFilePath original image is saved here
     * @param corners the 4 document corners
     * @return bitmap with cropped and warped document
     */
    fun crop(photoFilePath: String, corners: Quad): Bitmap {
        // read image
        val image = this.decodeImageFromFilePath(photoFilePath)

        // convert top left, top right, bottom right, and bottom left document corners into
        // the flat (x, y) array the native crop expects
        val cornerPoints = doubleArrayOf(
            corners.topLeftCorner.x.toDouble(),
            corners.topLeftCorner.y.toDouble(),
            corners.topRightCorner.x.toDouble(),
            corners.topRightCorner.y.toDouble(),
            corners.bottomRightCorner.x.toDouble(),
            corners.bottomRightCorner.y.toDouble(),
            corners.bottomLeftCorner.x.toDouble(),
            corners.bottomLeftCorner.y.toDouble()
        )

        // crop and warp the document out of the rest of the photo
        return NativeOpenCV.crop(image, cornerPoints)
            ?: throw Exception("unable to crop image")
    }

    /**
     * get bitmap image from file uri
     *
     * @param fileUriString image is saved here and starts with file:///
     * @return bitmap image
     */
    fun readBitmapFromFileUriString(
        fileUriString: String,
        contentResolver: ContentResolver
    ): Bitmap {
        return BitmapFactory.decodeStream(
            contentResolver.openInputStream(Uri.parse(fileUriString))
        )
    }
}
