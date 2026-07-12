#pragma once

#include <opencv2/core.hpp>

#include <vector>

namespace ibom::utils {

/**
 * Scene-quality analysis (docs/FEATURE_PROPOSALS_2026-07.md D1): the three
 * physical conditions that field sessions 138-141 identified as the #1 cause
 * of Auto-Align failures — under-exposure, specular glare, defocus — detected
 * on a downscaled frame so the GUI thread can afford it at a low cadence.
 * Pure OpenCV — unit-tested in CI (test_scene_quality). Advisory only: the
 * caller decides what (if anything) to do with the flags.
 */
struct SceneQualityParams {
    double darkMedianLuma = 60.0;    ///< ROI median below this → dark
    double glareLumaMin   = 250.0;   ///< pixel counts as saturated at/above this
    double glareMinFrac   = 0.004;   ///< largest saturated blob ≥ this ROI fraction → glare
    double blurSharpness  = 0.0;     ///< Laplacian variance below this → blurry (≤0 = skip)
};

struct SceneQualityReport {
    double medianLuma = 0.0;
    double overFrac   = 0.0;   ///< saturated fraction of the ROI
    double underFrac  = 0.0;   ///< near-black fraction of the ROI
    double sharpness  = 0.0;   ///< Laplacian variance (whole analyzed image)
    double glareFrac  = 0.0;   ///< largest saturated blob / ROI area
    cv::Rect glareRect;        ///< that blob's bbox, analyzed-image coords

    bool dark   = false;
    bool glare  = false;
    bool blurry = false;
    bool anyIssue() const { return dark || glare || blurry; }
};

/// Analyze a (downscaled) BGR or grayscale frame. roiPoly restricts the
/// exposure/glare statistics to the board region when the pose is known
/// (analyzed-image coords); empty = whole frame. Sharpness is always computed
/// on the full image (defocus is global).
SceneQualityReport analyzeScene(const cv::Mat& frame,
                                const std::vector<cv::Point>& roiPoly = {},
                                const SceneQualityParams& params = {});

} // namespace ibom::utils
