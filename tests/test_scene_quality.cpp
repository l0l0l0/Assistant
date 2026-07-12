// SceneQuality (src/utils/SceneQuality.cpp) — the scene advisor's analysis
// core (FEATURE_PROPOSALS D1): under-exposure, specular glare and defocus
// flags on synthetic frames with known defects.

#include <catch2/catch_test_macros.hpp>

#include <opencv2/imgproc.hpp>

#include "utils/SceneQuality.h"

using namespace ibom::utils;

namespace {

/// Textured mid-gray frame (median ≈ base, decent sharpness).
cv::Mat texturedFrame(int base = 120)
{
    cv::Mat img(240, 320, CV_8UC3, cv::Scalar(base, base, base));
    cv::Mat noise(img.size(), CV_8UC3);
    cv::RNG rng(5);
    rng.fill(noise, cv::RNG::UNIFORM, 0, 40);
    img += noise;
    return img;
}

} // namespace

TEST_CASE("scene: normal scene raises no flags", "[scene]")
{
    const auto r = analyzeScene(texturedFrame());
    REQUIRE(!r.dark);
    REQUIRE(!r.glare);
    REQUIRE(!r.blurry);
    REQUIRE(r.medianLuma > 100);
}

TEST_CASE("scene: dark scene flagged", "[scene]")
{
    const auto r = analyzeScene(texturedFrame(20));
    REQUIRE(r.dark);
    REQUIRE(r.medianLuma < 60);
    REQUIRE(!r.glare);
}

TEST_CASE("scene: compact specular blob flagged with its location", "[scene]")
{
    cv::Mat img = texturedFrame();
    cv::circle(img, {250, 60}, 25, cv::Scalar(255, 255, 255), cv::FILLED);
    const auto r = analyzeScene(img);
    REQUIRE(r.glare);
    REQUIRE(r.glareFrac > 0.02);
    // The reported rect must land on the blob.
    REQUIRE(r.glareRect.contains(cv::Point(250, 60)));
    REQUIRE(!r.dark);
}

TEST_CASE("scene: glare outside the board ROI is ignored", "[scene]")
{
    cv::Mat img = texturedFrame();
    cv::circle(img, {250, 60}, 25, cv::Scalar(255, 255, 255), cv::FILLED);
    // Board region = left half of the frame; the blob is on the right.
    const std::vector<cv::Point> roi = {{10, 10}, {150, 10}, {150, 230}, {10, 230}};
    const auto r = analyzeScene(img, roi);
    REQUIRE(!r.glare);
}

TEST_CASE("scene: blur flag against a threshold", "[scene]")
{
    const cv::Mat sharp = texturedFrame();
    cv::Mat blurred;
    cv::GaussianBlur(sharp, blurred, {31, 31}, 8.0);

    SceneQualityParams p;
    // Calibrate the threshold between the two measured values.
    const double sharpV = analyzeScene(sharp).sharpness;
    const double blurV  = analyzeScene(blurred).sharpness;
    REQUIRE(sharpV > blurV * 5.0);

    p.blurSharpness = (sharpV + blurV) / 2.0;
    REQUIRE(!analyzeScene(sharp, {}, p).blurry);
    REQUIRE(analyzeScene(blurred, {}, p).blurry);
}

TEST_CASE("scene: empty input is a no-op report", "[scene]")
{
    const auto r = analyzeScene(cv::Mat());
    REQUIRE(!r.anyIssue());
    REQUIRE(r.sharpness == 0.0);
}
