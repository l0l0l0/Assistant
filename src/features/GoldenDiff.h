#pragma once

#include <opencv2/core.hpp>

#include <string>
#include <vector>

#include "features/BoardMosaic.h"  // MosaicGeometry
#include "ibom/IBomData.h"

namespace ibom::features {

/**
 * Golden-board comparison (docs/FEATURE_PROPOSALS_2026-07.md A2): tile-wise
 * dissimilarity between two board-space mosaics of the SAME geometry (a
 * validated "golden" board vs the board under inspection), aggregated per
 * iBOM component. Pure OpenCV — unit-tested in CI (test_golden_diff).
 *
 * Robustness to workshop lighting: a global gain (current/golden mean ratio)
 * is compensated first; textured tiles are then compared by normalized
 * cross-correlation (invariant to residual gain/offset) and flat tiles by
 * gain-compensated mean/contrast difference — so "component removed" (body
 * texture → flat bare pads) scores high while "same board, brighter lamp"
 * scores low.
 */
struct GoldenDiffParams {
    int    tilePx        = 32;
    double minValidFrac  = 0.6;   ///< tile compared only if both mosaics cover this much
    double flatStd       = 10.0;  ///< std-dev below which a tile counts as textureless
    double meanDiffNorm  = 96.0;  ///< |Δmean| mapped to score 1.0 at this value
    double stdDiffNorm   = 64.0;  ///< |Δstd| contribution normalizer
};

/// Per-component anomaly score in [0,1] (higher = more different from golden).
struct ComponentAnomaly {
    std::string ref;
    double      score = 0.0;
    Point2D     pcbCenter;   ///< raw PCB mm (for heatmap / navigation)
};

/// Tile-quantized dissimilarity map. golden/current: CV_8UC3 mosaics of
/// identical size; masks: CV_8U written masks (255 = covered). Returns
/// CV_32F, same size: score in [0,1] where both mosaics are covered,
/// -1 where not comparable.
cv::Mat computeDiffMap(const cv::Mat& golden, const cv::Mat& goldenMask,
                       const cv::Mat& current, const cv::Mat& currentMask,
                       const GoldenDiffParams& params = {});

/// Aggregate the diff map per component of `layer`: mean of comparable diff
/// pixels inside the component's bbox (projected via `geo`). Components whose
/// bbox is covered less than minCoveredFrac are skipped (not inspected ≠
/// anomalous). Result sorted by score, highest first.
std::vector<ComponentAnomaly> scoreComponents(const cv::Mat& diffMap,
                                              const MosaicGeometry& geo,
                                              const std::vector<Component>& components,
                                              Layer layer,
                                              double minCoveredFrac = 0.4);

} // namespace ibom::features
