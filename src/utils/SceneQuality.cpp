#include "SceneQuality.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>

namespace ibom::utils {

SceneQualityReport analyzeScene(const cv::Mat& frame,
                                const std::vector<cv::Point>& roiPoly,
                                const SceneQualityParams& p)
{
    SceneQualityReport r;
    if (frame.empty()) return r;

    cv::Mat gray;
    if (frame.channels() == 3)      cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    else if (frame.channels() == 1) gray = frame;
    else                            return r;

    cv::Mat roi;
    if (roiPoly.size() >= 3) {
        roi = cv::Mat::zeros(gray.size(), CV_8U);
        cv::fillConvexPoly(roi, roiPoly, cv::Scalar(255));
        if (cv::countNonZero(roi) == 0) roi.release();   // pose off-frame
    }
    const int roiArea = roi.empty()
        ? gray.rows * gray.cols : cv::countNonZero(roi);

    // Exposure statistics from the histogram of the ROI.
    int histSize = 256;
    float range[] = {0.f, 256.f};
    const float* ranges[] = {range};
    cv::Mat hist;
    cv::calcHist(&gray, 1, nullptr, roi, hist, 1, &histSize, ranges);

    double cum = 0.0;
    const double half = roiArea / 2.0;
    int median = 0;
    for (int i = 0; i < 256; ++i) {
        cum += hist.at<float>(i);
        if (cum >= half) { median = i; break; }
    }
    double over = 0.0, under = 0.0;
    for (int i = 0; i < 256; ++i) {
        const double v = hist.at<float>(i);
        if (i >= static_cast<int>(p.glareLumaMin)) over  += v;
        if (i <= 15)                               under += v;
    }
    r.medianLuma = median;
    r.overFrac   = over  / std::max(1, roiArea);
    r.underFrac  = under / std::max(1, roiArea);
    r.dark       = r.medianLuma < p.darkMedianLuma;

    // Glare: largest connected saturated blob within the ROI. A diffuse
    // scattering of hot pixels is normal; one compact specular reflection is
    // the alignment killer (suite 141: "aimant à détections").
    cv::Mat sat;
    cv::threshold(gray, sat, p.glareLumaMin - 1.0, 255, cv::THRESH_BINARY);
    if (!roi.empty()) cv::bitwise_and(sat, roi, sat);
    cv::Mat labels, stats, centroids;
    const int n = cv::connectedComponentsWithStats(sat, labels, stats, centroids, 8);
    int bestArea = 0;
    cv::Rect bestRect;
    for (int i = 1; i < n; ++i) {
        const int area = stats.at<int>(i, cv::CC_STAT_AREA);
        if (area > bestArea) {
            bestArea = area;
            bestRect = cv::Rect(stats.at<int>(i, cv::CC_STAT_LEFT),
                                stats.at<int>(i, cv::CC_STAT_TOP),
                                stats.at<int>(i, cv::CC_STAT_WIDTH),
                                stats.at<int>(i, cv::CC_STAT_HEIGHT));
        }
    }
    r.glareFrac = static_cast<double>(bestArea) / std::max(1, roiArea);
    if (r.glareFrac >= p.glareMinFrac) {
        r.glare     = true;
        r.glareRect = bestRect;
    }

    // Sharpness — same metric as the focus assist / dataset gates.
    cv::Mat lap;
    cv::Laplacian(gray, lap, CV_32F);
    cv::Scalar mean, stddev;
    cv::meanStdDev(lap, mean, stddev);
    r.sharpness = stddev[0] * stddev[0];
    if (p.blurSharpness > 0.0) r.blurry = r.sharpness < p.blurSharpness;

    return r;
}

} // namespace ibom::utils
