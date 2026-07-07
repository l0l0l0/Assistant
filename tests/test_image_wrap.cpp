// Zero-copy cv::Mat → QImage wraps (ImageUtils::wrapMatOwned/wrapMatShared) —
// the display hot path fix of docs/INVESTIGATION_360_2026-07.md §3.1. These
// tests prove the three properties the GUI relies on: no pixel copy (the
// QImage aliases the Mat buffer), correct lifetime (the buffer lives exactly
// as long as the QImage), and correct color interpretation (BGR888).
// Pure QImage data ops — no QGuiApplication needed.

#include <catch2/catch_test_macros.hpp>

#include "utils/ImageUtils.h"

#include <opencv2/core.hpp>

#include <memory>

using ibom::utils::ImageUtils;

TEST_CASE("wrapMatOwned aliases the BGR buffer and reads correct colors", "[imagewrap]")
{
    cv::Mat bgr(2, 4, CV_8UC3, cv::Scalar(255, 0, 0));       // blue in BGR
    bgr.at<cv::Vec3b>(1, 2) = cv::Vec3b(0, 0, 255);          // one red pixel
    const uchar* pixels = bgr.data;

    const QImage img = ImageUtils::wrapMatOwned(std::move(bgr));
    REQUIRE(img.format() == QImage::Format_BGR888);
    REQUIRE(img.constBits() == pixels);                       // zero-copy proof
    REQUIRE(img.pixelColor(0, 0) == QColor(0, 0, 255));       // blue
    REQUIRE(img.pixelColor(2, 1) == QColor(255, 0, 0));       // red
}

TEST_CASE("wrapMatShared pins the FrameRef exactly as long as the QImage", "[imagewrap]")
{
    auto frame = std::make_shared<const cv::Mat>(
        cv::Mat(3, 3, CV_8UC3, cv::Scalar(10, 20, 30)));
    REQUIRE(frame.use_count() == 1);

    {
        QImage img = ImageUtils::wrapMatShared(frame);
        REQUIRE(frame.use_count() == 2);          // wrap holds one reference
        REQUIRE(img.constBits() == frame->data);  // zero-copy proof

        QImage shared2 = img;                     // implicit sharing —
        REQUIRE(frame.use_count() == 2);          // no extra pixel ownership
        REQUIRE(shared2.pixelColor(1, 1) == QColor(30, 20, 10));  // BGR→RGB
    }
    // All QImages gone → the cleanup hook released the reference.
    REQUIRE(frame.use_count() == 1);
}

TEST_CASE("grayscale frames wrap as Grayscale8", "[imagewrap]")
{
    cv::Mat gray(2, 2, CV_8UC1, cv::Scalar(200));
    gray.at<uchar>(0, 1) = 7;
    const uchar* pixels = gray.data;

    const QImage img = ImageUtils::wrapMatOwned(std::move(gray));
    REQUIRE(img.format() == QImage::Format_Grayscale8);
    REQUIRE(img.constBits() == pixels);
    REQUIRE(qGray(img.pixel(0, 0)) == 200);
    REQUIRE(qGray(img.pixel(1, 0)) == 7);
}

TEST_CASE("non-continuous ROI views keep their stride", "[imagewrap]")
{
    // A column-range view: step stays the parent's (30 bytes), width shrinks.
    cv::Mat parent(4, 10, CV_8UC3, cv::Scalar(1, 2, 3));
    parent.at<cv::Vec3b>(2, 5) = cv::Vec3b(9, 8, 7);
    cv::Mat roi = parent(cv::Range::all(), cv::Range(4, 8));
    REQUIRE_FALSE(roi.isContinuous());

    const QImage img = ImageUtils::wrapMatOwned(roi);
    REQUIRE(img.width() == 4);
    REQUIRE(static_cast<size_t>(img.bytesPerLine()) == roi.step);
    REQUIRE(img.pixelColor(1, 2) == QColor(7, 8, 9));  // parent (2,5) in RGB
}

TEST_CASE("unsupported types fall back to a converting copy", "[imagewrap]")
{
    cv::Mat bgra(2, 2, CV_8UC4, cv::Scalar(1, 2, 3, 255));
    const uchar* pixels = bgra.data;
    const QImage img = ImageUtils::wrapMatOwned(bgra.clone());
    REQUIRE_FALSE(img.isNull());
    REQUIRE(img.constBits() != pixels);  // fallback converts, doesn't alias

    REQUIRE(ImageUtils::wrapMatOwned(cv::Mat()).isNull());
    REQUIRE(ImageUtils::wrapMatShared(nullptr).isNull());
}
