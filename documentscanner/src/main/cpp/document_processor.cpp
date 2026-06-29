#include "document_processor.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace docscanner {

namespace {

// shrink photo to this height (px) to make it easier/faster to find document corners
const double kShrunkImageHeight = 500.0;

/**
 * Take an image matrix with a document (single color channel), and find the document's
 * corners. This is a 1:1 port of DocumentDetector.findCorners.
 *
 * @param channel a single color channel of the (shrunk) photo
 * @param outQuad receives the 4 corner points when a document is found
 * @return true if a 4 sided, large, convex contour was found
 */
bool findCorners(const cv::Mat &channel, std::vector<cv::Point> &outQuad) {
    cv::Mat output;

    // blur image to help remove noise
    cv::GaussianBlur(channel, output, cv::Size(5, 5), 0.0);

    // convert all pixels to either black or white (Otsu picks the threshold)
    cv::threshold(output, output, 0.0, 255.0, cv::THRESH_BINARY | cv::THRESH_OTSU);

    // detect the document's border using the Canny edge detection algorithm
    cv::Canny(output, output, 50.0, 200.0);

    // the detected edges might have gaps, so try to close those
    cv::Mat kernel = cv::Mat::ones(cv::Size(5, 5), CV_8U);
    cv::morphologyEx(output, output, cv::MORPH_CLOSE, kernel);

    // get outline of document edges, and outlines of other shapes in photo
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(output, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

    // approximate outlines using polygons, then keep only large, convex, 4 sided ones,
    // and return the one with the largest area
    double bestArea = -1.0;
    bool found = false;
    for (const auto &contour : contours) {
        std::vector<cv::Point2f> contour2f(contour.begin(), contour.end());
        std::vector<cv::Point2f> approx;
        cv::approxPolyDP(contour2f, approx, 0.02 * cv::arcLength(contour2f, true), true);

        if (approx.size() != 4) {
            continue;
        }

        std::vector<cv::Point> quad;
        quad.reserve(4);
        for (const auto &p : approx) {
            quad.emplace_back(cvRound(p.x), cvRound(p.y));
        }

        double area = cv::contourArea(quad);
        if (area > 1000.0 && cv::isContourConvex(quad) && area > bestArea) {
            bestArea = area;
            outQuad = quad;
            found = true;
        }
    }

    return found;
}

} // namespace

std::vector<cv::Point2d> findDocumentCorners(const cv::Mat &rgba) {
    const double imageWidth = rgba.cols;
    const double imageHeight = rgba.rows;

    // shrink photo to make it easier to find document corners (preserve aspect ratio)
    cv::Mat mat;
    cv::resize(
        rgba,
        mat,
        cv::Size(
            cvRound(kShrunkImageHeight * imageWidth / imageHeight),
            cvRound(kShrunkImageHeight)));

    // convert photo to LUV colorspace to avoid glares caused by lights
    // (matches the original code, which also passed the 4 channel matrix here)
    cv::cvtColor(mat, mat, cv::COLOR_BGR2Luv);

    // separate photo into 3 parts (L, U and V)
    std::vector<cv::Mat> channels;
    cv::split(mat, channels);

    // find corners for each color channel, then pick the quad with the largest area
    std::vector<cv::Point> best;
    double bestArea = -1.0;
    bool found = false;
    for (const auto &channel : channels) {
        std::vector<cv::Point> quad;
        if (findCorners(channel, quad)) {
            double area = cv::contourArea(quad);
            if (area > bestArea) {
                bestArea = area;
                best = quad;
                found = true;
            }
        }
    }

    if (!found) {
        return {};
    }

    // scale points to account for shrinking the image before document detection
    // (the resize used a uniform scale factor of imageHeight / shrunkImageHeight)
    std::vector<cv::Point2d> corners;
    corners.reserve(4);
    for (const auto &p : best) {
        corners.emplace_back(
            p.x * imageHeight / kShrunkImageHeight,
            p.y * imageHeight / kShrunkImageHeight);
    }

    // sort points to force this order (top left, top right, bottom left, bottom right):
    // sort by y, split into the top pair and bottom pair, then sort each pair by x
    std::sort(
        corners.begin(),
        corners.end(),
        [](const cv::Point2d &a, const cv::Point2d &b) { return a.y < b.y; });
    if (corners[0].x > corners[1].x) {
        std::swap(corners[0], corners[1]);
    }
    if (corners[2].x > corners[3].x) {
        std::swap(corners[2], corners[3]);
    }

    return corners;
}

cv::Mat cropAndWarp(const cv::Mat &image, const std::vector<cv::Point2d> &corners) {
    // drop the alpha channel if present, so out-of-bounds warp pixels are opaque black
    // (matches the original behaviour, where the image matrix was 3 channel RGB)
    cv::Mat src;
    if (image.channels() == 4) {
        cv::cvtColor(image, src, cv::COLOR_RGBA2RGB);
    } else {
        src = image;
    }

    // corners order: top left, top right, bottom right, bottom left
    const cv::Point2f tLC(corners[0].x, corners[0].y);
    const cv::Point2f tRC(corners[1].x, corners[1].y);
    const cv::Point2f bRC(corners[2].x, corners[2].y);
    const cv::Point2f bLC(corners[3].x, corners[3].y);

    auto distance = [](const cv::Point2f &a, const cv::Point2f &b) {
        return std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
    };

    // take the smaller of the 2 opposing edges for width and height
    const double width = std::min(distance(tLC, tRC), distance(bLC, bRC));
    const double height = std::min(distance(tLC, bLC), distance(tRC, bRC));

    std::vector<cv::Point2f> source{tLC, tRC, bRC, bLC};
    std::vector<cv::Point2f> destination{
        cv::Point2f(0.0f, 0.0f),
        cv::Point2f(static_cast<float>(width), 0.0f),
        cv::Point2f(static_cast<float>(width), static_cast<float>(height)),
        cv::Point2f(0.0f, static_cast<float>(height))};

    cv::Mat output;
    cv::warpPerspective(
        src,
        output,
        cv::getPerspectiveTransform(source, destination),
        cv::Size(cvRound(width), cvRound(height)));

    return output;
}

} // namespace docscanner
