#pragma once

#include <QImage>
#include <opencv2/core.hpp>
#include <memory>
#include <string>

namespace ibom::utils {

/// Image conversion and processing utilities
class ImageUtils {
public:
    /// Convert cv::Mat (BGR) to QImage (RGB)
    static QImage matToQImage(const cv::Mat& mat);

    /// ZERO-COPY wrap of a BGR (CV_8UC3 → Format_BGR888) or grayscale
    /// (CV_8UC1 → Format_Grayscale8) Mat into a QImage. The Mat header is
    /// heap-kept and released by the QImage's cleanup hook, so the pixel
    /// buffer stays alive exactly as long as (any implicit-shared copy of)
    /// the QImage — no cvtColor, no deep copy. This is the display hot path:
    /// the previous per-frame BGR→RGB conversion + QImage::copy() moved
    /// ~2×6 MB per 1080p frame on the GUI thread (INVESTIGATION_360 §3.1).
    /// Unsupported types fall back to the converting matToQImage().
    /// The buffer must not be written after wrapping (fresh cvtColor/remap
    /// outputs and immutable FrameRefs both qualify).
    static QImage wrapMatOwned(cv::Mat mat);

    /// Same zero-copy wrap for an immutable shared frame (camera FrameRef =
    /// shared_ptr<const cv::Mat>): the QImage holds a shared_ptr copy, so the
    /// capture buffer outlives the camera loop for as long as it is displayed.
    static QImage wrapMatShared(std::shared_ptr<const cv::Mat> frame);

    /// Convert QImage to cv::Mat (BGR)
    static cv::Mat qImageToMat(const QImage& image);

    /// Resize keeping aspect ratio
    static cv::Mat resizeKeepAspect(const cv::Mat& src, int maxWidth, int maxHeight);

    /// Auto-brightness / contrast (CLAHE)
    static cv::Mat autoEnhance(const cv::Mat& src);

    /// White balance correction
    static cv::Mat whiteBalance(const cv::Mat& src);

    /// Save image with timestamp filename
    static std::string saveTimestamped(const cv::Mat& image,
                                        const std::string& directory,
                                        const std::string& prefix = "capture",
                                        const std::string& ext = ".png");

    /// Compute image sharpness (Laplacian variance)
    static double computeSharpness(const cv::Mat& image);

    /// Check if image is too blurry
    static bool isBlurry(const cv::Mat& image, double threshold = 100.0);

    /// Create a side-by-side comparison image
    static cv::Mat sideBySide(const cv::Mat& left, const cv::Mat& right,
                               int gap = 4);

    /// Draw text with background for readability
    static void drawTextWithBg(cv::Mat& image, const std::string& text,
                                cv::Point origin, double fontScale = 0.5,
                                cv::Scalar textColor = {255, 255, 255},
                                cv::Scalar bgColor = {0, 0, 0, 180});
};

} // namespace ibom::utils
