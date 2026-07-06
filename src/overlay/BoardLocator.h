#pragma once

#include <opencv2/core.hpp>
#include <string>
#include <vector>

#include "ibom/IBomData.h"

namespace ibom::overlay {

/// Result of an automatic board-pose search (see docs/AUTO_ALIGN_PLAN.md).
struct BoardLocateResult {
    bool found = false;

    /// 4 image-space points (px), ordered to match the PCB corners
    /// {TL, TR, BR, BL} of boardInfo.boardBBox — i.e. directly usable as the
    /// `imagePoints` argument of Homography::compute() together with that
    /// same corner order. Only meaningful when found == true.
    std::vector<cv::Point2f> imageCorners;

    /// Edge-agreement score of the winning orientation, in [0, 1] — fraction
    /// of the iBOM's rendered board outline + component edges that land on a
    /// real edge detected in the frame. Not a probability; useful only to
    /// compare candidates or warn the user when it's suspiciously low.
    double score = 0.0;

    /// "depth" or "contour" — which detection strategy produced the result.
    std::string method;

    /// Human-readable diagnostic. Always set: explains the result on success
    /// (method + score) or the reason for failure.
    std::string message;
};

/// Pure-CV automatic board pose estimation: locates the PCB's outline in a
/// camera frame and resolves which of the (up to 4) rotations of that
/// rectangle matches the iBOM layout, without requiring the user to click
/// any points.
///
/// Two detection strategies, tried in order:
///  1. Depth-based plane segmentation (RealSense D405) — robust even on a
///     low-contrast/matte board, since the board is simply a raised plane
///     above the table under the camera.
///  2. 2D contour detection (any camera) — Canny edges + largest
///     board-sized rectangular contour.
///
/// Orientation (the 0/90/180/270 + mirror ambiguity of a bare rectangle) is
/// resolved by rendering the iBOM's own board outline and component bounding
/// boxes through each candidate homography and scoring how well that
/// predicted geometry lines up with the real edges detected in the frame —
/// see docs/AUTO_ALIGN_PLAN.md, "Stage 2".
///
/// Intended for a single on-demand call (e.g. an "Auto-Align" button), not
/// per-frame use — once a pose is found, TrackingWorker's masked ORB tracking
/// takes over to follow the board in real time.
class BoardLocator {
public:
    /// @param colorBgr   Current camera frame (BGR, any size).
    /// @param depth16u   Aligned depth frame (CV_16UC1, millimeters), or an
    ///                   empty Mat if unavailable — falls back to contour
    ///                   detection.
    /// @param project    Loaded iBOM project (board outline + component
    ///                   bounding boxes, used both to validate candidate
    ///                   size and to score orientation).
    /// @param expectedPixelsPerMm  Known/estimated scale (e.g. from
    ///                   calibration or depth-derived px/mm), 0 if unknown.
    ///                   Used to sanity-check candidate quad size; without
    ///                   it the contour fallback is far less reliable.
    /// @param activeLayer  Which side of the board the camera is looking at —
    ///                   only this layer's components are rendered when
    ///                   scoring candidate orientations (mirrors the
    ///                   convention used by OverlayRenderer::setActiveLayer()).
    static BoardLocateResult locate(const cv::Mat& colorBgr,
                                     const cv::Mat& depth16u,
                                     const ibom::IBomProject& project,
                                     double expectedPixelsPerMm,
                                     ibom::Layer activeLayer = ibom::Layer::Front);

private:
    static bool locateViaDepth(const cv::Mat& depth16u,
                                double expectedAreaPx,
                                double expectedAspect,
                                cv::RotatedRect& outRect,
                                std::string& reason);

    static bool locateViaContour(const cv::Mat& colorBgr,
                                  double expectedAreaPx,
                                  double expectedAspect,
                                  cv::RotatedRect& outRect,
                                  std::string& reason);

    /// Rejects `rect` if its area/aspect ratio is too far from what the iBOM
    /// board outline implies at `expectedPixelsPerMm`. A no-op pass-through
    /// (always true) when expectedAreaPx/expectedAspect are <= 0 (unknown).
    static bool validateSize(const cv::RotatedRect& rect,
                              double expectedAreaPx,
                              double expectedAspect,
                              std::string& reason);

    /// Builds the 8 candidate corner orderings of `rect` (4 cyclic rotations
    /// x 2 windings — the winding of a depth/contour blob isn't known a
    /// priori), scores each against `project`, and returns the best one.
    static BoardLocateResult disambiguate(const cv::RotatedRect& rect,
                                           const cv::Mat& colorBgr,
                                           const ibom::IBomProject& project,
                                           const std::string& method,
                                           ibom::Layer activeLayer);

    /// Gray + blur + Canny + dilate — real edges in the frame, computed once
    /// and reused for all 8 orientation candidates.
    static cv::Mat computeEdgeMap(const cv::Mat& colorBgr);

    /// Renders the board outline + component bboxes through the homography
    /// implied by (pcbCorners -> imgCorners) and scores overlap against
    /// `dilatedEdges`. Returns a score in [0, 1] — higher is better.
    static double scoreOrientation(const cv::Mat& dilatedEdges,
                                    const ibom::IBomProject& project,
                                    const std::vector<cv::Point2f>& pcbCorners,
                                    const std::vector<cv::Point2f>& imgCorners,
                                    ibom::Layer activeLayer);
};

} // namespace ibom::overlay
