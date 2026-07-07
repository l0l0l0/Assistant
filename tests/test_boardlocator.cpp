// First tests for BoardLocator (451 lines, zero coverage until 2026-07 —
// docs/INVESTIGATION_360_2026-07.md §8.2, despite a field-error history:
// ERREUR #41/#44). Synthetic scenes with a known ground-truth pose:
//  - contour path: light board on a dark bench, no depth frame;
//  - depth path: board plane raised above the table (D405-style CV_16UC1 mm);
//  - negative: featureless frame must not "find" a board.

#include <catch2/catch_test_macros.hpp>

#include "overlay/BoardLocator.h"
#include "overlay/Homography.h"
#include "ibom/IBomData.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using ibom::overlay::BoardLocator;
using ibom::overlay::BoardLocateResult;

namespace {

constexpr double kScale = 6.0;                  // ground truth px/mm
constexpr double kRotDeg = 8.0;                 // board rotation on the bench
const cv::Size   kFrame(900, 720);

// Ground-truth similarity: PCB mm → image px (rotation + translation).
cv::Point2f gt(cv::Point2f p)
{
    const double th = kRotDeg * CV_PI / 180.0;
    return { static_cast<float>(kScale * std::cos(th) * p.x
                                - kScale * std::sin(th) * p.y + 120.0),
             static_cast<float>(kScale * std::sin(th) * p.x
                                + kScale * std::cos(th) * p.y + 90.0) };
}

// Ground-truth BACK view: the flipped board shows point (x,y) where the
// front view would show (boardW−x, y) — mirror about the board's vertical
// mid-axis, then the same bench similarity.
cv::Point2f gtBack(cv::Point2f p)
{
    return gt({ 100.f - p.x, p.y });
}

using ViewFn = cv::Point2f (*)(cv::Point2f);

// Asymmetric component layout (a symmetric one would leave the 0/90/180/270
// disambiguation genuinely ambiguous — not what these tests are about).
ibom::IBomProject makeProject(std::vector<cv::Point2f>& centersOut,
                              ibom::Layer layer = ibom::Layer::Front)
{
    ibom::IBomProject p;
    std::mt19937 rng(31);
    std::uniform_real_distribution<float> ux(8.f, 92.f);
    std::uniform_real_distribution<float> uy(8.f, 72.f);
    std::vector<cv::Point2f> pts;
    while (pts.size() < 25) {
        cv::Point2f c(ux(rng), uy(rng));
        bool ok = true;
        for (const auto& q : pts)
            if (std::hypot(c.x - q.x, c.y - q.y) < 8.f) { ok = false; break; }
        if (!ok) continue;
        pts.push_back(c);
        ibom::Component comp;
        comp.reference = "U" + std::to_string(pts.size());
        comp.layer     = layer;
        comp.position  = { c.x, c.y };
        comp.bbox      = { c.x - 1.5, c.y - 1.5, c.x + 1.5, c.y + 1.5 };
        p.components.push_back(std::move(comp));
    }
    centersOut = pts;
    p.boardInfo.boardBBox = { 0.0, 0.0, 100.0, 80.0 };
    return p;
}

// Board quad (PCB bbox corners) in image space, ground truth.
std::vector<cv::Point> boardQuadImg(ViewFn view = gt)
{
    std::vector<cv::Point> quad;
    for (const cv::Point2f& c : { cv::Point2f(0, 0), cv::Point2f(100, 0),
                                  cv::Point2f(100, 80), cv::Point2f(0, 80) }) {
        const cv::Point2f q = view(c);
        quad.emplace_back(cvRound(q.x), cvRound(q.y));
    }
    return quad;
}

// Light board + dark component bodies on a dark bench.
cv::Mat renderColor(const std::vector<cv::Point2f>& centers, ViewFn view = gt)
{
    cv::Mat img(kFrame, CV_8UC3, cv::Scalar(25, 25, 25));
    cv::fillConvexPoly(img, boardQuadImg(view), cv::Scalar(190, 195, 190));
    for (const auto& c : centers) {
        std::vector<cv::Point> body;
        for (const cv::Point2f& k : { cv::Point2f(c.x - 1.5f, c.y - 1.5f),
                                      cv::Point2f(c.x + 1.5f, c.y - 1.5f),
                                      cv::Point2f(c.x + 1.5f, c.y + 1.5f),
                                      cv::Point2f(c.x - 1.5f, c.y + 1.5f) }) {
            const cv::Point2f q = view(k);
            body.emplace_back(cvRound(q.x), cvRound(q.y));
        }
        cv::fillConvexPoly(img, body, cv::Scalar(40, 40, 45));
    }
    return img;
}

// Validate a result against ground truth: fit the homography exactly as
// Application does (PCB bbox corners → result.imageCorners) and require the
// component centers to land on their drawn positions. Robust to whichever of
// the 8 corner orderings won, as long as it is the CORRECT one.
void requireAligned(const BoardLocateResult& r,
                    const ibom::IBomProject& project,
                    const std::vector<cv::Point2f>& centers,
                    ViewFn view = gt)
{
    REQUIRE(r.found);
    REQUIRE(r.imageCorners.size() == 4);

    const auto& bb = project.boardInfo.boardBBox;
    const std::vector<cv::Point2f> pcb = {
        { static_cast<float>(bb.minX), static_cast<float>(bb.minY) },
        { static_cast<float>(bb.maxX), static_cast<float>(bb.minY) },
        { static_cast<float>(bb.maxX), static_cast<float>(bb.maxY) },
        { static_cast<float>(bb.minX), static_cast<float>(bb.maxY) }
    };
    ibom::overlay::Homography h;
    REQUIRE(h.compute(pcb, r.imageCorners));

    std::vector<double> errs;
    for (const auto& c : centers) {
        const auto proj = h.pcbToImage(c);
        errs.push_back(cv::norm(cv::Point2f(proj.x - view(c).x, proj.y - view(c).y)));
    }
    std::nth_element(errs.begin(), errs.begin() + errs.size() / 2, errs.end());
    INFO("method=" << r.method << " score=" << r.score
                   << " median center error=" << errs[errs.size() / 2] << "px");
    // Generous: minAreaRect on dilated Canny edges carries a few px of bias;
    // what matters is that the ORIENTATION is right (a wrong one errs by
    // hundreds of px) and the pose is close enough to seed live tracking.
    REQUIRE(errs[errs.size() / 2] < 12.0);
}

} // namespace

TEST_CASE("BoardLocator finds and orients the board from contours alone", "[boardlocator]")
{
    std::vector<cv::Point2f> centers;
    const ibom::IBomProject project = makeProject(centers);
    const cv::Mat color = renderColor(centers);

    const auto r = BoardLocator::locate(color, cv::Mat(), project, kScale,
                                        ibom::Layer::Front);
    INFO(r.message);
    requireAligned(r, project, centers);
    REQUIRE(r.method == "contour");
}

TEST_CASE("BoardLocator uses the raised depth plane when depth is available", "[boardlocator]")
{
    std::vector<cv::Point2f> centers;
    const ibom::IBomProject project = makeProject(centers);
    const cv::Mat color = renderColor(centers);

    // D405-style aligned depth (mm): table at 320 mm, board plane at 280 mm —
    // safely more than the ±15 mm plane band apart. The board covers the
    // frame center, so the center-ROI median lands on the board plane.
    cv::Mat depth(kFrame, CV_16UC1, cv::Scalar(320));
    cv::fillConvexPoly(depth, boardQuadImg(), cv::Scalar(280));

    const auto r = BoardLocator::locate(color, depth, project, kScale,
                                        ibom::Layer::Front);
    INFO(r.message);
    requireAligned(r, project, centers);
    REQUIRE(r.method == "depth");
}

TEST_CASE("BoardLocator orients the BACK side (mirrored view, negative-det pose)", "[boardlocator]")
{
    // Same board flipped over: back-layer components, mirrored scene. The
    // 8-candidate disambiguation (4 rotations x 2 windings) must pick a
    // MIRRORED corner ordering — i.e. a homography with negative determinant.
    std::vector<cv::Point2f> centers;
    const ibom::IBomProject project = makeProject(centers, ibom::Layer::Back);
    const cv::Mat color = renderColor(centers, gtBack);

    const auto r = BoardLocator::locate(color, cv::Mat(), project, kScale,
                                        ibom::Layer::Back);
    INFO(r.message);
    requireAligned(r, project, centers, gtBack);
}

TEST_CASE("BoardLocator reports not-found on a featureless frame", "[boardlocator]")
{
    std::vector<cv::Point2f> centers;
    const ibom::IBomProject project = makeProject(centers);

    cv::Mat blank(kFrame, CV_8UC3, cv::Scalar(90, 90, 90));
    const auto r = BoardLocator::locate(blank, cv::Mat(), project, kScale,
                                        ibom::Layer::Front);
    INFO(r.message);
    REQUIRE_FALSE(r.found);
}
