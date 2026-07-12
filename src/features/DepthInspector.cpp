#include "DepthInspector.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace ibom::features {

namespace {

struct Sample { double x, y, z; };

/// Least-squares plane through a set of samples (normal equations, 3×3).
bool solvePlane(const std::vector<Sample>& pts, double& a, double& b, double& c)
{
    if (pts.size() < 3) return false;
    cv::Matx33d AtA = cv::Matx33d::zeros();
    cv::Vec3d   Atz(0, 0, 0);
    for (const auto& p : pts) {
        const cv::Vec3d row(p.x, p.y, 1.0);
        AtA += row * row.t();
        Atz += row * p.z;
    }
    cv::Vec3d sol;
    if (!cv::solve(AtA, Atz, sol, cv::DECOMP_SVD)) return false;
    a = sol[0]; b = sol[1]; c = sol[2];
    return std::isfinite(a) && std::isfinite(b) && std::isfinite(c);
}

} // namespace

BoardPlane fitBoardPlane(const cv::Mat& depthMm,
                         const std::vector<cv::Point2f>& boardQuadImg,
                         double ransacThreshMm,
                         int iterations,
                         int sampleStep)
{
    BoardPlane plane;
    if (depthMm.empty() || depthMm.type() != CV_16UC1) return plane;

    // Region mask: the board polygon when known, else the whole frame.
    cv::Mat region;
    if (boardQuadImg.size() >= 3) {
        region = cv::Mat::zeros(depthMm.size(), CV_8U);
        std::vector<cv::Point> poly;
        poly.reserve(boardQuadImg.size());
        for (const auto& p : boardQuadImg)
            poly.emplace_back(cv::saturate_cast<int>(p.x), cv::saturate_cast<int>(p.y));
        cv::fillConvexPoly(region, poly, cv::Scalar(255));
    }

    std::vector<Sample> samples;
    const int step = std::max(1, sampleStep);
    for (int y = 0; y < depthMm.rows; y += step) {
        const uint16_t* row = depthMm.ptr<uint16_t>(y);
        const uint8_t* reg = region.empty() ? nullptr : region.ptr<uint8_t>(y);
        for (int x = 0; x < depthMm.cols; x += step) {
            if (reg && !reg[x]) continue;
            const uint16_t z = row[x];
            if (z == 0) continue;
            samples.push_back({static_cast<double>(x), static_cast<double>(y),
                               static_cast<double>(z)});
        }
    }
    if (samples.size() < 30) return plane;

    // RANSAC — the board dominates the region, components are the outliers.
    cv::RNG rng(0x5EED);
    const int n = static_cast<int>(samples.size());
    int bestInliers = 0;
    double ba = 0, bb = 0, bc = 0;
    for (int it = 0; it < iterations; ++it) {
        const Sample& p0 = samples[rng.uniform(0, n)];
        const Sample& p1 = samples[rng.uniform(0, n)];
        const Sample& p2 = samples[rng.uniform(0, n)];
        // Degenerate (near-collinear) triple → skip.
        const double area2 = std::abs((p1.x - p0.x) * (p2.y - p0.y)
                                      - (p2.x - p0.x) * (p1.y - p0.y));
        if (area2 < 25.0) continue;
        double a, b, c;
        if (!solvePlane({p0, p1, p2}, a, b, c)) continue;

        int inliers = 0;
        for (const auto& s : samples)
            if (std::abs(s.z - (a * s.x + b * s.y + c)) <= ransacThreshMm)
                ++inliers;
        if (inliers > bestInliers) {
            bestInliers = inliers;
            ba = a; bb = b; bc = c;
        }
    }
    if (bestInliers < std::max(30, n / 5)) return plane;

    // Refine on the consensus set.
    std::vector<Sample> inl;
    inl.reserve(bestInliers);
    for (const auto& s : samples)
        if (std::abs(s.z - (ba * s.x + bb * s.y + bc)) <= ransacThreshMm)
            inl.push_back(s);
    double a = ba, b = bb, c = bc;
    if (solvePlane(inl, a, b, c)) { ba = a; bb = b; bc = c; }

    plane.valid = true;
    plane.a = ba; plane.b = bb; plane.c = bc;
    plane.inlierFrac = static_cast<double>(bestInliers) / n;
    return plane;
}

std::vector<DepthVerdict> inspectComponents(const cv::Mat& depthMm,
                                            const cv::Mat& H,
                                            const std::vector<Component>& components,
                                            Layer layer,
                                            const BoardPlane& plane,
                                            const DepthInspectParams& p)
{
    std::vector<DepthVerdict> verdicts;
    if (depthMm.empty() || depthMm.type() != CV_16UC1 || !plane.valid
        || H.empty() || H.rows != 3 || H.cols != 3)
        return verdicts;

    cv::Mat Hd;
    H.convertTo(Hd, CV_64F);

    for (const auto& comp : components) {
        if (comp.layer != layer) continue;

        // Shrunk bbox corners (raw PCB mm) → image px.
        const auto ctr = comp.bbox.center();
        const double hw = comp.bbox.width()  * 0.5 * p.bboxShrink;
        const double hh = comp.bbox.height() * 0.5 * p.bboxShrink;
        if (hw <= 0.0 || hh <= 0.0) continue;
        std::vector<cv::Point2f> pcb = {
            {static_cast<float>(ctr.x - hw), static_cast<float>(ctr.y - hh)},
            {static_cast<float>(ctr.x + hw), static_cast<float>(ctr.y - hh)},
            {static_cast<float>(ctr.x + hw), static_cast<float>(ctr.y + hh)},
            {static_cast<float>(ctr.x - hw), static_cast<float>(ctr.y + hh)}};
        std::vector<cv::Point2f> img;
        cv::perspectiveTransform(pcb, img, Hd);

        cv::Rect box = cv::boundingRect(img) & cv::Rect(0, 0, depthMm.cols, depthMm.rows);
        if (box.area() < 1) continue;   // outside the frame → not inspectable

        std::vector<cv::Point> poly;
        poly.reserve(4);
        for (const auto& q : img)
            poly.emplace_back(cv::saturate_cast<int>(q.x), cv::saturate_cast<int>(q.y));
        cv::Mat mask = cv::Mat::zeros(box.size(), CV_8U);
        std::vector<cv::Point> local;
        local.reserve(4);
        for (const auto& q : poly) local.emplace_back(q.x - box.x, q.y - box.y);
        cv::fillConvexPoly(mask, local, cv::Scalar(255));

        std::vector<double> heights;
        for (int y = 0; y < box.height; ++y) {
            const uint16_t* drow = depthMm.ptr<uint16_t>(box.y + y);
            const uint8_t*  mrow = mask.ptr<uint8_t>(y);
            for (int x = 0; x < box.width; ++x) {
                if (!mrow[x]) continue;
                const uint16_t z = drow[box.x + x];
                if (z == 0) continue;
                // Component tops are CLOSER to the camera than the board plane.
                const double h = plane.zAt(box.x + x, box.y + y) - z;
                if (h > -5.0 && h < p.maxHeightMm) heights.push_back(h);
            }
        }

        DepthVerdict v;
        v.ref     = comp.reference;
        v.validPx = static_cast<int>(heights.size());
        if (v.validPx < p.minValidPx) {
            v.status = DepthVerdictStatus::Uncertain;
        } else {
            auto mid = heights.begin() + heights.size() / 2;
            std::nth_element(heights.begin(), mid, heights.end());
            v.heightMm = *mid;
            if (v.heightMm >= p.presentMinMm)     v.status = DepthVerdictStatus::Present;
            else if (v.heightMm <= p.absentMaxMm) v.status = DepthVerdictStatus::Absent;
            else                                  v.status = DepthVerdictStatus::Uncertain;
        }
        verdicts.push_back(std::move(v));
    }
    return verdicts;
}

} // namespace ibom::features
