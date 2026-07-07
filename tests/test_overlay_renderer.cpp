// OverlayRenderer board-space buffer: active-layer filtering and the
// mirrored (view-space) back-side rendering added for the double-sided
// workflow (docs/INVESTIGATION_360_2026-07.md §6.1). Runs headless: the
// QGuiApplication is created on the offscreen platform (QPainter text
// rendering needs one).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "overlay/OverlayRenderer.h"
#include "ibom/IBomData.h"

#include <QGuiApplication>
#include <QTransform>

#include <memory>

using Catch::Approx;
using ibom::overlay::OverlayRenderer;
using ibom::overlay::OverlayInputs;

namespace {

// One QGuiApplication for the whole binary (offscreen — no display needed).
QGuiApplication* app()
{
    static int argc = 1;
    static char arg0[] = "test_overlay_renderer";
    static char* argv[] = { arg0, nullptr };
    static bool env = (qputenv("QT_QPA_PLATFORM", "offscreen"), true);
    (void)env;
    static QGuiApplication a(argc, argv);
    return &a;
}

// 100×80 mm board: one front component at (20,40), one back at (80,20).
std::shared_ptr<ibom::IBomProject> makeProject()
{
    auto p = std::make_shared<ibom::IBomProject>();
    p->boardInfo.boardBBox = { 0.0, 0.0, 100.0, 80.0 };

    const auto addComp = [&](const char* ref, ibom::Layer layer,
                             double cx, double cy) {
        ibom::Component c;
        c.reference = ref;
        c.layer     = layer;
        c.position  = { cx, cy };
        c.bbox      = { cx - 3.0, cy - 3.0, cx + 3.0, cy + 3.0 };
        ibom::Pad pad;
        pad.position = { cx, cy };
        pad.sizeX = 6.0;
        pad.sizeY = 6.0;
        c.pads.push_back(pad);
        p->components.push_back(std::move(c));
    };
    addComp("F1", ibom::Layer::Front, 20.0, 40.0);
    addComp("B1", ibom::Layer::Back,  80.0, 20.0);
    return p;
}

OverlayInputs makeInputs(std::shared_ptr<ibom::IBomProject> project,
                         ibom::Layer layer)
{
    OverlayInputs in;
    in.project     = std::move(project);
    in.cSelected   = QColor(0, 229, 255);
    in.cPlaced     = QColor(72, 200, 72);
    in.cNormal     = QColor(170, 170, 68);
    in.labelNormal = QColor(255, 255, 255);
    in.activeLayer = layer;
    return in;
}

bool opaqueAt(const QImage& img, QPointF p)
{
    const QPoint q(qRound(p.x()), qRound(p.y()));
    if (!img.rect().contains(q)) return false;
    return qAlpha(img.pixel(q)) > 0;
}

} // namespace

TEST_CASE("front render draws only front components, in raw PCB space", "[overlay]")
{
    app();
    const auto project = makeProject();
    const auto bo = OverlayRenderer::renderBoardSpace(
        makeInputs(project, ibom::Layer::Front));
    REQUIRE(!bo.image.isNull());

    // Front pad body is painted at its mapped center…
    REQUIRE(opaqueAt(bo.image, bo.pcbToBuffer.map(QPointF(20.0, 40.0))));
    // …and the back component's location stays empty (layer filtered out).
    REQUIRE_FALSE(opaqueAt(bo.image, bo.pcbToBuffer.map(QPointF(80.0, 20.0))));
}

TEST_CASE("back render mirrors the view and keeps pcbToBuffer consistent", "[overlay]")
{
    app();
    const auto project = makeProject();
    const auto front = OverlayRenderer::renderBoardSpace(
        makeInputs(project, ibom::Layer::Front));
    const auto back = OverlayRenderer::renderBoardSpace(
        makeInputs(project, ibom::Layer::Back));
    REQUIRE(!back.image.isNull());

    // pcbToBuffer still maps RAW pcb coords: the back component's pad must be
    // painted exactly where its raw center maps.
    const QPointF b1 = back.pcbToBuffer.map(QPointF(80.0, 20.0));
    REQUIRE(opaqueAt(back.image, b1));
    // The front component is filtered out of the back render.
    REQUIRE_FALSE(opaqueAt(back.image, back.pcbToBuffer.map(QPointF(20.0, 40.0))));

    // The view is MIRRORED about the board's vertical mid-axis (x = 50 mm):
    // raw x = 80 must land where the front mapping would put x = 20.
    const QPointF mirrored = front.pcbToBuffer.map(QPointF(20.0, 20.0));
    REQUIRE(b1.x() == Approx(mirrored.x()).margin(0.51));
    REQUIRE(b1.y() == Approx(mirrored.y()).margin(0.51));

    // And the mirror flips orientation: negative determinant of the linear
    // part (this is what keeps the on-screen composition, together with a
    // negative-det back homography, orientation-preserving → readable text).
    const qreal det = back.pcbToBuffer.m11() * back.pcbToBuffer.m22()
                    - back.pcbToBuffer.m12() * back.pcbToBuffer.m21();
    REQUIRE(det < 0.0);
}
