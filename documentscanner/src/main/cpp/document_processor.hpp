#ifndef DOCUMENTSCANNER_DOCUMENT_PROCESSOR_HPP
#define DOCUMENTSCANNER_DOCUMENT_PROCESSOR_HPP

#include <opencv2/core.hpp>
#include <vector>

// Pure C++ port of the image processing that used to live in the Kotlin
// DocumentDetector / ImageUtil classes (which called the OpenCV Java API).
// These functions only depend on opencv_core + opencv_imgproc.
namespace docscanner {

/**
 * Find the document's 4 corners in a photo.
 *
 * @param rgba the photo as an RGBA (CV_8UC4) matrix (Android bitmap pixels)
 * @return 4 corners ordered (top left, top right, bottom left, bottom right) in the
 *         original image coordinate space, or an empty vector if no document was found
 */
std::vector<cv::Point2d> findDocumentCorners(const cv::Mat &rgba);

/**
 * Crop the document out of the photo and warp it to a straight-on rectangle.
 *
 * @param image the source photo matrix (RGBA or RGB)
 * @param corners 4 corners ordered (top left, top right, bottom right, bottom left)
 *        in the source image coordinate space
 * @return the cropped + warped document as an RGB (CV_8UC3) matrix
 */
cv::Mat cropAndWarp(const cv::Mat &image, const std::vector<cv::Point2d> &corners);

} // namespace docscanner

#endif // DOCUMENTSCANNER_DOCUMENT_PROCESSOR_HPP
