#include "GoldenDiff.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace ibom::features {

cv::Mat computeDiffMap(const cv::Mat& golden, const cv::Mat& goldenMask,
                       const cv::Mat& current, const cv::Mat& currentMask,
                       const GoldenDiffParams& p)
{
    cv::Mat out;
    if (golden.empty() || current.empty()
        || golden.size() != current.size()
        || goldenMask.size() != golden.size()
        || currentMask.size() != golden.size())
        return out;

    cv::Mat valid;
    cv::bitwise_and(goldenMask, currentMask, valid);
    out = cv::Mat(golden.size(), CV_32F, cv::Scalar(-1.0f));
    if (cv::countNonZero(valid) == 0) return out;

    cv::Mat gGray, cGray;
    cv::cvtColor(golden,  gGray, cv::COLOR_BGR2GRAY);
    cv::cvtColor(current, cGray, cv::COLOR_BGR2GRAY);
    cv::Mat gF, cF;
    gGray.convertTo(gF, CV_32F);
    cGray.convertTo(cF, CV_32F);

    // Global lighting-gain compensation: bring the golden to the current
    // frame's overall brightness before any per-tile comparison.
    const double meanG = cv::mean(gF, valid)[0];
    const double meanC = cv::mean(cF, valid)[0];
    const double gain  = std::clamp(meanC / std::max(1.0, meanG), 0.5, 2.0);
    gF *= gain;

    const int tile = std::max(8, p.tilePx);
    for (int y = 0; y < out.rows; y += tile) {
        for (int x = 0; x < out.cols; x += tile) {
            const cv::Rect r = cv::Rect(x, y, tile, tile)
                             & cv::Rect(0, 0, out.cols, out.rows);
            const cv::Mat vm = valid(r);
            const int nValid = cv::countNonZero(vm);
            if (nValid < p.minValidFrac * r.area()) continue;

            cv::Scalar mA, sA, mB, sB;
            cv::meanStdDev(gF(r), mA, sA, vm);
            cv::meanStdDev(cF(r), mB, sB, vm);

            double score;
            if (sA[0] >= p.flatStd && sB[0] >= p.flatStd) {
                // Both textured: NCC on the valid pixels (gain/offset
                // invariant). E[(a-ma)(b-mb)] via a masked mean of the product.
                cv::Mat prod = (gF(r) - mA[0]).mul(cF(r) - mB[0]);
                const double cov = cv::mean(prod, vm)[0];
                const double ncc = cov / std::max(1e-6, sA[0] * sB[0]);
                score = std::clamp(0.5 * (1.0 - ncc), 0.0, 1.0);
            } else {
                // At least one flat tile: compare brightness + contrast
                // (catches "dark body → bright bare pads" even when both
                // sides are individually textureless).
                score = std::min(1.0, std::abs(mB[0] - mA[0]) / p.meanDiffNorm
                                      + std::abs(sB[0] - sA[0]) / p.stdDiffNorm);
            }
            out(r).setTo(static_cast<float>(score), vm);
        }
    }
    return out;
}

std::vector<ComponentAnomaly> scoreComponents(const cv::Mat& diffMap,
                                              const MosaicGeometry& geo,
                                              const std::vector<Component>& components,
                                              Layer layer,
                                              double minCoveredFrac)
{
    std::vector<ComponentAnomaly> result;
    if (diffMap.empty() || diffMap.type() != CV_32F || !geo.valid())
        return result;

    for (const auto& comp : components) {
        if (comp.layer != layer) continue;

        const cv::Point2f tl = geo.pcbToCanvas(
            {static_cast<float>(comp.bbox.minX), static_cast<float>(comp.bbox.minY)});
        const cv::Point2f br = geo.pcbToCanvas(
            {static_cast<float>(comp.bbox.maxX), static_cast<float>(comp.bbox.maxY)});
        cv::Rect r(cv::Point(static_cast<int>(std::floor(std::min(tl.x, br.x))),
                             static_cast<int>(std::floor(std::min(tl.y, br.y)))),
                   cv::Point(static_cast<int>(std::ceil(std::max(tl.x, br.x))),
                             static_cast<int>(std::ceil(std::max(tl.y, br.y)))));
        r &= cv::Rect(0, 0, diffMap.cols, diffMap.rows);
        if (r.area() < 4) continue;

        // Comparable pixels only (score ≥ 0); uncovered components are
        // skipped — "not scanned" must not read as "anomalous".
        const cv::Mat patch = diffMap(r);
        cv::Mat cmpMask = patch >= 0.0f;   // CV_8U
        const int nCmp = cv::countNonZero(cmpMask);
        if (nCmp < minCoveredFrac * r.area()) continue;

        ComponentAnomaly a;
        a.ref       = comp.reference;
        a.score     = cv::mean(patch, cmpMask)[0];
        a.pcbCenter = comp.bbox.center();
        result.push_back(std::move(a));
    }

    std::sort(result.begin(), result.end(),
              [](const ComponentAnomaly& a, const ComponentAnomaly& b) {
                  return a.score > b.score;
              });
    return result;
}

} // namespace ibom::features
