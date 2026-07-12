// GoldenDiff (src/features/GoldenDiff.cpp) — golden-board comparison
// (FEATURE_PROPOSALS A2). Synthetic mosaics with known tampering: a removed
// component must rank first with a high score, a pure lighting change must
// stay quiet, and uncovered regions must be skipped, not flagged.

#include <catch2/catch_test_macros.hpp>

#include <opencv2/imgproc.hpp>

#include "features/GoldenDiff.h"

using namespace ibom::features;

namespace {

// Canvas: 5 px/mm over an 80×60 mm board, origin at PCB (0,0).
MosaicGeometry makeGeo()
{
    MosaicGeometry g;
    g.pxPerMm = 5.0;
    g.minXmm = 0.0; g.minYmm = 0.0;
    g.widthPx = 400; g.heightPx = 300;
    return g;
}

ibom::Component makeComp(const std::string& ref,
                         double x0, double y0, double x1, double y1)
{
    ibom::Component c;
    c.reference = ref;
    c.layer = ibom::Layer::Front;
    c.bbox.minX = x0; c.bbox.minY = y0; c.bbox.maxX = x1; c.bbox.maxY = y1;
    c.position = {(x0 + x1) / 2.0, (y0 + y1) / 2.0};
    return c;
}

cv::Rect canvasRect(const MosaicGeometry& g, const ibom::Component& c)
{
    const cv::Point2f tl = g.pcbToCanvas({static_cast<float>(c.bbox.minX),
                                          static_cast<float>(c.bbox.minY)});
    const cv::Point2f br = g.pcbToCanvas({static_cast<float>(c.bbox.maxX),
                                          static_cast<float>(c.bbox.maxY)});
    return {cv::Point(static_cast<int>(tl.x), static_cast<int>(tl.y)),
            cv::Point(static_cast<int>(br.x), static_cast<int>(br.y))};
}

/// Golden board: textured substrate + dark textured component bodies.
cv::Mat makeGolden(const MosaicGeometry& g, const std::vector<ibom::Component>& comps)
{
    cv::Mat img(g.heightPx, g.widthPx, CV_8UC3, cv::Scalar(60, 110, 70));
    cv::RNG rng(7);
    cv::Mat noise(img.size(), CV_8UC3);
    rng.fill(noise, cv::RNG::UNIFORM, 0, 50);
    img += noise;
    for (const auto& c : comps) {
        const cv::Rect r = canvasRect(g, c);
        cv::Mat body = img(r);
        body.setTo(cv::Scalar(35, 35, 40));
        cv::Mat bn(body.size(), CV_8UC3);
        rng.fill(bn, cv::RNG::UNIFORM, 0, 30);
        body += bn;
        // A "marking" stripe for texture.
        cv::line(img, {r.x + 2, r.y + r.height / 2},
                 {r.br().x - 2, r.y + r.height / 2}, cv::Scalar(200, 200, 200), 2);
    }
    return img;
}

} // namespace

TEST_CASE("golden diff: removed component ranks first", "[golden]")
{
    const MosaicGeometry geo = makeGeo();
    std::vector<ibom::Component> comps = {
        makeComp("U1", 10, 10, 26, 22),
        makeComp("R5", 40, 15, 48, 19),
        makeComp("C9", 55, 40, 63, 46),
    };
    const cv::Mat golden = makeGolden(geo, comps);
    const cv::Mat full(golden.size(), CV_8U, cv::Scalar(255));

    // "Remove" U1: its body becomes flat bright bare-pad substrate.
    cv::Mat current = golden.clone();
    current(canvasRect(geo, comps[0])).setTo(cv::Scalar(150, 160, 155));

    const cv::Mat diff = computeDiffMap(golden, full, current, full);
    REQUIRE(!diff.empty());

    const auto anomalies = scoreComponents(diff, geo, comps, ibom::Layer::Front);
    REQUIRE(anomalies.size() == 3);
    REQUIRE(anomalies[0].ref == "U1");
    REQUIRE(anomalies[0].score > 0.35);
    // Untouched components stay quiet.
    for (size_t i = 1; i < anomalies.size(); ++i)
        REQUIRE(anomalies[i].score < 0.15);
}

TEST_CASE("golden diff: lighting gain change stays quiet", "[golden]")
{
    const MosaicGeometry geo = makeGeo();
    std::vector<ibom::Component> comps = {
        makeComp("U1", 10, 10, 26, 22),
        makeComp("R5", 40, 15, 48, 19),
    };
    const cv::Mat golden = makeGolden(geo, comps);
    const cv::Mat full(golden.size(), CV_8U, cv::Scalar(255));

    // Brighter lamp: gain 1.3 + offset 12 (saturating).
    cv::Mat current;
    golden.convertTo(current, CV_8UC3, 1.3, 12.0);

    const cv::Mat diff = computeDiffMap(golden, full, current, full);
    const auto anomalies = scoreComponents(diff, geo, comps, ibom::Layer::Front);
    REQUIRE(anomalies.size() == 2);
    for (const auto& a : anomalies)
        REQUIRE(a.score < 0.2);
}

TEST_CASE("golden diff: uncovered components are skipped, not flagged", "[golden]")
{
    const MosaicGeometry geo = makeGeo();
    std::vector<ibom::Component> comps = {
        makeComp("U1", 10, 10, 26, 22),
        makeComp("R5", 40, 15, 48, 19),
    };
    const cv::Mat golden = makeGolden(geo, comps);
    const cv::Mat full(golden.size(), CV_8U, cv::Scalar(255));

    // Current scan never covered R5's corner of the board.
    cv::Mat curMask = full.clone();
    cv::Rect notScanned = canvasRect(geo, comps[1]);
    notScanned.x -= 10; notScanned.y -= 10;
    notScanned.width += 20; notScanned.height += 20;
    curMask(notScanned & cv::Rect(0, 0, curMask.cols, curMask.rows)).setTo(0);

    const cv::Mat diff = computeDiffMap(golden, full, golden, curMask);
    const auto anomalies = scoreComponents(diff, geo, comps, ibom::Layer::Front);
    REQUIRE(anomalies.size() == 1);
    REQUIRE(anomalies[0].ref == "U1");
    REQUIRE(anomalies[0].score < 0.1);
}

TEST_CASE("golden diff: size mismatch and layer filter", "[golden]")
{
    const MosaicGeometry geo = makeGeo();
    const cv::Mat a(300, 400, CV_8UC3, cv::Scalar(1, 2, 3));
    const cv::Mat b(200, 400, CV_8UC3, cv::Scalar(1, 2, 3));
    const cv::Mat ma(300, 400, CV_8U, cv::Scalar(255));
    const cv::Mat mb(200, 400, CV_8U, cv::Scalar(255));
    REQUIRE(computeDiffMap(a, ma, b, mb).empty());

    // Back-layer component excluded when scoring the front face.
    std::vector<ibom::Component> comps = {makeComp("U1", 10, 10, 26, 22)};
    comps[0].layer = ibom::Layer::Back;
    const cv::Mat diff = computeDiffMap(a, ma, a, ma);
    REQUIRE(scoreComponents(diff, geo, comps, ibom::Layer::Front).empty());
}
