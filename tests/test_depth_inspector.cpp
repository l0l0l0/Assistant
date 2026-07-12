// DepthInspector (src/features/DepthInspector.cpp) — depth-based presence
// check (FEATURE_PROPOSALS A3). Synthetic D405-style depth frame (CV_16U,
// mm): a tilted board plane with raised boxes where components sit. The
// plane fit must recover the tilt through the component "outliers", and the
// per-component verdicts must separate present / absent / too-small.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <opencv2/imgproc.hpp>

#include "features/DepthInspector.h"

using Catch::Approx;
using namespace ibom::features;

namespace {

// Camera model for the test: PCB mm → image px at 5 px/mm with an offset.
const cv::Mat kH = (cv::Mat_<double>(3, 3) << 5, 0, 50, 0, 5, 40, 0, 0, 1);

ibom::Component makeComp(const std::string& ref,
                         double x0, double y0, double x1, double y1)
{
    ibom::Component c;
    c.reference = ref;
    c.layer = ibom::Layer::Front;
    c.bbox.minX = x0; c.bbox.minY = y0; c.bbox.maxX = x1; c.bbox.maxY = y1;
    c.position = {(x0 + x1) / 2.0, (y0 + y1) / 2.0};
    return c;
}

/// Board plane depth(x,y) = 300 + 0.01x + 0.02y mm, with raised regions.
cv::Mat makeDepth(const std::vector<std::pair<cv::Rect, double>>& raisedPx)
{
    cv::Mat depth(480, 640, CV_16UC1);
    for (int y = 0; y < depth.rows; ++y)
        for (int x = 0; x < depth.cols; ++x)
            depth.at<uint16_t>(y, x) =
                static_cast<uint16_t>(std::lround(300.0 + 0.01 * x + 0.02 * y));
    for (const auto& [r, hMm] : raisedPx) {
        // Raised = closer to the camera = SMALLER depth.
        for (int y = r.y; y < r.br().y; ++y)
            for (int x = r.x; x < r.br().x; ++x) {
                auto& z = depth.at<uint16_t>(y, x);
                z = static_cast<uint16_t>(std::lround(z - hMm));
            }
    }
    // Sprinkle invalid pixels (dropouts) deterministically.
    cv::RNG rng(99);
    for (int i = 0; i < 3000; ++i)
        depth.at<uint16_t>(rng.uniform(0, 480), rng.uniform(0, 640)) = 0;
    return depth;
}

/// Component bbox (shrunk is inside this) → raised image-px rect under kH.
cv::Rect pxRect(const ibom::Component& c)
{
    return {static_cast<int>(50 + 5 * c.bbox.minX),
            static_cast<int>(40 + 5 * c.bbox.minY),
            static_cast<int>(5 * c.bbox.width()),
            static_cast<int>(5 * c.bbox.height())};
}

} // namespace

TEST_CASE("depth: plane fit recovers the tilt through component outliers", "[depth]")
{
    const auto u1 = makeComp("U1", 20, 20, 40, 36);      // big IC, 2.5 mm tall
    const cv::Mat depth = makeDepth({{pxRect(u1), 2.5}});

    const std::vector<cv::Point2f> quad = {
        {50, 40}, {550, 40}, {550, 440}, {50, 440}};
    const BoardPlane plane = fitBoardPlane(depth, quad);
    REQUIRE(plane.valid);
    REQUIRE(plane.a == Approx(0.01).margin(0.005));
    REQUIRE(plane.b == Approx(0.02).margin(0.005));
    REQUIRE(plane.c == Approx(300.0).margin(2.0));
    REQUIRE(plane.inlierFrac > 0.6);
}

TEST_CASE("depth: present / absent / tiny verdicts", "[depth]")
{
    const auto u1  = makeComp("U1", 20, 20, 40, 36);     // present, 2.5 mm
    const auto c2  = makeComp("C2", 60, 20, 66, 25);     // ABSENT (no relief)
    const auto j3  = makeComp("J3", 20, 50, 44, 62);     // present, 8 mm
    const auto r01 = makeComp("R01", 80, 60, 80.4, 60.2);// 0402-ish: too small
    const cv::Mat depth = makeDepth({{pxRect(u1), 2.5}, {pxRect(j3), 8.0}});

    const BoardPlane plane = fitBoardPlane(
        depth, {{50, 40}, {550, 40}, {550, 440}, {50, 440}});
    REQUIRE(plane.valid);

    const auto verdicts = inspectComponents(
        depth, kH, {u1, c2, j3, r01}, ibom::Layer::Front, plane);
    REQUIRE(verdicts.size() == 4);

    auto find = [&](const std::string& ref) {
        for (const auto& v : verdicts)
            if (v.ref == ref) return v;
        FAIL("ref not found: " << ref);
        return DepthVerdict{};
    };

    const auto vu1 = find("U1");
    REQUIRE(vu1.status == DepthVerdictStatus::Present);
    REQUIRE(vu1.heightMm == Approx(2.5).margin(0.6));

    REQUIRE(find("C2").status == DepthVerdictStatus::Absent);

    const auto vj3 = find("J3");
    REQUIRE(vj3.status == DepthVerdictStatus::Present);
    REQUIRE(vj3.heightMm == Approx(8.0).margin(0.8));

    // Sub-pixel component: honest Uncertain, not a guess.
    REQUIRE(find("R01").status == DepthVerdictStatus::Uncertain);
}

TEST_CASE("depth: guards — bad inputs, off-frame components", "[depth]")
{
    const cv::Mat depth = makeDepth({});
    const BoardPlane plane = fitBoardPlane(depth, {});
    REQUIRE(plane.valid);

    // Wrong depth type / invalid plane / bad H → empty result.
    cv::Mat wrongType(480, 640, CV_8U, cv::Scalar(0));
    REQUIRE(inspectComponents(wrongType, kH, {}, ibom::Layer::Front, plane).empty());
    REQUIRE(inspectComponents(depth, cv::Mat(), {}, ibom::Layer::Front, plane).empty());
    REQUIRE(inspectComponents(depth, kH, {}, ibom::Layer::Front, BoardPlane{}).empty());

    // A component projecting outside the frame is silently skipped.
    const auto far = makeComp("X1", 500, 500, 520, 520);
    const auto verdicts = inspectComponents(
        depth, kH, {far}, ibom::Layer::Front, plane);
    REQUIRE(verdicts.empty());

    // Depth full of zeros → no plane.
    cv::Mat empty(480, 640, CV_16UC1, cv::Scalar(0));
    REQUIRE(!fitBoardPlane(empty, {}).valid);
}
