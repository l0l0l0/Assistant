#pragma once

#include <opencv2/core.hpp>

#include "ibom/IBomData.h"  // ibom::BBox

namespace ibom::features {

/// Geometry of a board-space canvas: raw PCB mm → canvas px mapping shared by
/// the mosaic (A1), the golden diff (A2) and anything else that needs to
/// rasterize board-space data at a fixed resolution.
struct MosaicGeometry {
    double pxPerMm = 0.0;
    double minXmm  = 0.0;   ///< PCB x of canvas column 0 (board bbox − margin)
    double minYmm  = 0.0;   ///< PCB y of canvas row 0
    int    widthPx = 0;
    int    heightPx = 0;

    bool valid() const { return pxPerMm > 0.0 && widthPx > 0 && heightPx > 0; }
    cv::Point2f pcbToCanvas(cv::Point2f pcbMm) const
    {
        return {static_cast<float>((pcbMm.x - minXmm) * pxPerMm),
                static_cast<float>((pcbMm.y - minYmm) * pxPerMm)};
    }
};

/**
 * @brief Accumulates camera frames warped into board (PCB) space to build a
 *        full orthorectified image of the board — the "board scan" of
 *        docs/FEATURE_PROPOSALS_2026-07.md A1.
 *
 * Pure OpenCV, no Qt — unit-tested in CI (test_board_mosaic). Threading is
 * the caller's business (BoardScanner runs one instance on its own QThread).
 *
 * Blending policy ("sharpest tile wins"): the canvas is divided into a fixed
 * tile grid. A frame region that fully covers a tile replaces it iff its
 * Laplacian-variance sharpness beats the score of what is already stored, so
 * a blurred sweep pass is later repaired by a sharp one. Partially covering
 * regions only fill still-unwritten pixels (no score) — they are placeholders
 * until a full view of that tile arrives.
 */
class BoardMosaic {
public:
    /// Size the canvas for a board bbox (+ margin). pxPerMm is clamped down so
    /// the larger canvas dimension stays ≤ maxCanvasPx (RAM guard: 8192² BGR
    /// = 192 MB worst case). Resets any previous accumulation.
    void initialize(const BBox& boardBBox, double pxPerMm,
                    double marginMm = 2.0, int maxCanvasPx = 8192,
                    int tilePx = 32);

    bool isInitialized() const { return m_geo.valid(); }
    const MosaicGeometry& geometry() const { return m_geo; }

    /// Accumulate one BGR camera frame under pose H (raw PCB mm → image px,
    /// 3×3, any float depth — the live homography as-is, front or back face).
    /// @return number of tiles updated (0 = nothing usable: bad H, no overlap).
    int accumulate(const cv::Mat& frameBgr, const cv::Mat& H);

    const cv::Mat& image() const { return m_canvas; }          ///< CV_8UC3
    const cv::Mat& writtenMask() const { return m_written; }   ///< CV_8U 255=written
    double coverageFraction() const;   ///< written px / canvas px
    int    framesAccumulated() const { return m_frames; }
    void   reset();

private:
    MosaicGeometry m_geo;
    int     m_tilePx = 32;
    cv::Mat m_canvas;      // CV_8UC3
    cv::Mat m_written;     // CV_8U per-pixel written flag
    cv::Mat m_tileScore;   // CV_32F per tile; <0 = no full-tile write yet
    int     m_frames = 0;
};

} // namespace ibom::features
