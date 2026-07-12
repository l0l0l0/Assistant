#pragma once

#include <opencv2/core.hpp>

#include <string>
#include <vector>

#include "ibom/IBomData.h"

namespace ibom::features {

/**
 * Depth-based component presence check (docs/FEATURE_PROPOSALS_2026-07.md A3):
 * fit the bare-board plane in an aligned depth frame (D405: CV_16U, 1 mm per
 * unit, 0 = invalid), then measure each component's median height above that
 * plane inside its projected bbox. A populated component rises above the
 * plane; a missing one reads ~0. Pure OpenCV — unit-tested in CI
 * (test_depth_inspector). V1 scope: reliable on parts ≥ ~1 mm tall and a few
 * pixels wide; smaller parts return Uncertain rather than a guess.
 */

/// Board plane in image coordinates: depth(x, y) ≈ a·x + b·y + c (all mm).
struct BoardPlane {
    bool   valid = false;
    double a = 0.0, b = 0.0, c = 0.0;
    double inlierFrac = 0.0;
    double zAt(double x, double y) const { return a * x + b * y + c; }
};

/// RANSAC plane fit on the depth pixels inside `boardQuadImg` (image-px
/// polygon of the board; empty = whole frame). Deterministic (fixed RNG seed).
BoardPlane fitBoardPlane(const cv::Mat& depthMm,
                         const std::vector<cv::Point2f>& boardQuadImg,
                         double ransacThreshMm = 0.8,
                         int iterations = 250,
                         int sampleStep = 4);

enum class DepthVerdictStatus { Absent, Present, Uncertain };

struct DepthVerdict {
    std::string        ref;
    DepthVerdictStatus status = DepthVerdictStatus::Uncertain;
    double             heightMm = 0.0;   ///< median height above the plane
    int                validPx  = 0;     ///< depth pixels behind the verdict
};

struct DepthInspectParams {
    double presentMinMm = 0.4;   ///< median height ≥ this → Present
    double absentMaxMm  = 0.15;  ///< median height ≤ this → Absent
    int    minValidPx   = 12;    ///< fewer valid depth px → Uncertain
    double bboxShrink   = 0.6;   ///< sample the bbox core (avoid edge mixing)
    double maxHeightMm  = 60.0;  ///< reject implausible heights (hand, cable)
};

/// One verdict per component of `layer` whose shrunk bbox projects (via H,
/// raw PCB mm → image px) inside the depth frame.
std::vector<DepthVerdict> inspectComponents(const cv::Mat& depthMm,
                                            const cv::Mat& H,
                                            const std::vector<Component>& components,
                                            Layer layer,
                                            const BoardPlane& plane,
                                            const DepthInspectParams& params = {});

} // namespace ibom::features
