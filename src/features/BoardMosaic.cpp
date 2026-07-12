#include "BoardMosaic.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace ibom::features {

namespace {

/// Laplacian variance — same sharpness metric as the dataset gates, so the
/// "sharpest tile wins" policy is consistent with what the rest of the app
/// calls sharp.
double laplacianVariance(const cv::Mat& gray)
{
    cv::Mat lap;
    cv::Laplacian(gray, lap, CV_32F);
    cv::Scalar mean, stddev;
    cv::meanStdDev(lap, mean, stddev);
    return stddev[0] * stddev[0];
}

} // namespace

void BoardMosaic::initialize(const BBox& boardBBox, double pxPerMm,
                             double marginMm, int maxCanvasPx, int tilePx)
{
    reset();
    m_geo = {};

    const double wMm = boardBBox.width()  + 2.0 * marginMm;
    const double hMm = boardBBox.height() + 2.0 * marginMm;
    if (wMm <= 0.0 || hMm <= 0.0 || pxPerMm <= 0.0) return;

    // RAM guard: clamp the resolution so the larger dimension fits.
    const double maxDimMm = std::max(wMm, hMm);
    const double clamped  = std::min(pxPerMm, maxCanvasPx / maxDimMm);

    m_geo.pxPerMm  = clamped;
    m_geo.minXmm   = boardBBox.minX - marginMm;
    m_geo.minYmm   = boardBBox.minY - marginMm;
    m_geo.widthPx  = std::max(1, static_cast<int>(std::lround(wMm * clamped)));
    m_geo.heightPx = std::max(1, static_cast<int>(std::lround(hMm * clamped)));
    m_tilePx       = std::max(8, tilePx);

    m_canvas  = cv::Mat::zeros(m_geo.heightPx, m_geo.widthPx, CV_8UC3);
    m_written = cv::Mat::zeros(m_geo.heightPx, m_geo.widthPx, CV_8U);
    const int tilesX = (m_geo.widthPx  + m_tilePx - 1) / m_tilePx;
    const int tilesY = (m_geo.heightPx + m_tilePx - 1) / m_tilePx;
    m_tileScore = cv::Mat(tilesY, tilesX, CV_32F, cv::Scalar(-1.0f));
}

void BoardMosaic::reset()
{
    m_canvas.release();
    m_written.release();
    m_tileScore.release();
    m_geo    = {};
    m_frames = 0;
}

double BoardMosaic::coverageFraction() const
{
    if (m_written.empty()) return 0.0;
    return static_cast<double>(cv::countNonZero(m_written))
         / (static_cast<double>(m_written.rows) * m_written.cols);
}

int BoardMosaic::accumulate(const cv::Mat& frameBgr, const cv::Mat& H)
{
    if (!isInitialized() || frameBgr.empty() || frameBgr.type() != CV_8UC3
        || H.empty() || H.rows != 3 || H.cols != 3)
        return 0;

    cv::Mat Hd;
    H.convertTo(Hd, CV_64F);
    // |det| may legitimately be large or negative (back view); only reject a
    // genuinely singular pose.
    if (std::abs(cv::determinant(Hd)) < 1e-12) return 0;

    // canvas ← image  =  S ∘ H⁻¹, with S mapping raw PCB mm → canvas px.
    const double s = m_geo.pxPerMm;
    const cv::Mat S = (cv::Mat_<double>(3, 3) <<
        s, 0, -m_geo.minXmm * s,
        0, s, -m_geo.minYmm * s,
        0, 0, 1);
    const cv::Mat A = S * Hd.inv();

    // Destination footprint of the frame, snapped outward to tile boundaries.
    std::vector<cv::Point2f> corners = {
        {0.f, 0.f},
        {static_cast<float>(frameBgr.cols), 0.f},
        {static_cast<float>(frameBgr.cols), static_cast<float>(frameBgr.rows)},
        {0.f, static_cast<float>(frameBgr.rows)}};
    std::vector<cv::Point2f> proj;
    cv::perspectiveTransform(corners, proj, A);
    for (const auto& p : proj)
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) return 0;

    cv::Rect roi = cv::boundingRect(proj);
    roi.x = (roi.x / m_tilePx) * m_tilePx;
    roi.y = (roi.y / m_tilePx) * m_tilePx;
    roi.width  = ((roi.br().x + m_tilePx - 1) / m_tilePx) * m_tilePx - roi.x;
    roi.height = ((roi.br().y + m_tilePx - 1) / m_tilePx) * m_tilePx - roi.y;
    roi &= cv::Rect(0, 0, m_geo.widthPx, m_geo.heightPx);
    if (roi.empty()) return 0;

    // Warp pixels + validity mask into the ROI. The mask is eroded to drop
    // the interpolation fringe at the frame border.
    const cv::Mat T = (cv::Mat_<double>(3, 3) <<
        1, 0, -roi.x,
        0, 1, -roi.y,
        0, 0, 1);
    cv::Mat warped, mask;
    cv::warpPerspective(frameBgr, warped, T * A, roi.size(), cv::INTER_LINEAR);
    const cv::Mat ones(frameBgr.rows, frameBgr.cols, CV_8U, cv::Scalar(255));
    cv::warpPerspective(ones, mask, T * A, roi.size(), cv::INTER_NEAREST);
    cv::erode(mask, mask,
              cv::getStructuringElement(cv::MORPH_RECT, {3, 3}),
              cv::Point(-1, -1), 2);

    cv::Mat gray;
    cv::cvtColor(warped, gray, cv::COLOR_BGR2GRAY);

    int updated = 0;
    for (int ty = roi.y / m_tilePx; ty * m_tilePx < roi.br().y; ++ty) {
        for (int tx = roi.x / m_tilePx; tx * m_tilePx < roi.br().x; ++tx) {
            const cv::Rect tileRect =
                cv::Rect(tx * m_tilePx, ty * m_tilePx, m_tilePx, m_tilePx)
                & cv::Rect(0, 0, m_geo.widthPx, m_geo.heightPx);
            if (tileRect.empty()) continue;
            const cv::Rect local(tileRect.x - roi.x, tileRect.y - roi.y,
                                 tileRect.width, tileRect.height);

            const cv::Mat tileMask = mask(local);
            const int covered = cv::countNonZero(tileMask);
            if (covered == 0) continue;
            const double cov = static_cast<double>(covered) / tileRect.area();

            if (cov >= 0.98) {
                // Full view of the tile: replace iff sharper than stored.
                const double score = laplacianVariance(gray(local));
                float& stored = m_tileScore.at<float>(ty, tx);
                if (score > stored) {
                    warped(local).copyTo(m_canvas(tileRect));
                    m_written(tileRect).setTo(255);
                    stored = static_cast<float>(score);
                    ++updated;
                }
            } else if (cov >= 0.15) {
                // Partial view: fill only still-unwritten pixels, no score —
                // the first full view of this tile will overwrite them.
                cv::Mat fresh;
                cv::bitwise_and(tileMask, ~m_written(tileRect), fresh);
                if (cv::countNonZero(fresh) > 0) {
                    warped(local).copyTo(m_canvas(tileRect), fresh);
                    m_written(tileRect).setTo(255, fresh);
                    ++updated;
                }
            }
        }
    }

    if (updated > 0) ++m_frames;
    return updated;
}

} // namespace ibom::features
