// ProjectDiff (src/ibom/ProjectDiff.cpp) — revision diff for update rework
// (FEATURE_PROPOSALS C1): added / removed / changed classification keyed by
// reference, with a move tolerance.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ibom/ProjectDiff.h"

using Catch::Approx;
using namespace ibom;

namespace {

Component makeComp(const std::string& ref, const std::string& value,
                   const std::string& footprint, double x, double y,
                   Layer layer = Layer::Front)
{
    Component c;
    c.reference = ref;
    c.value     = value;
    c.footprint = footprint;
    c.position  = {x, y};
    c.layer     = layer;
    return c;
}

IBomProject makeProject(std::vector<Component> comps)
{
    IBomProject p;
    p.components = std::move(comps);
    return p;
}

} // namespace

TEST_CASE("diff: identical revisions are empty", "[revdiff]")
{
    const auto a = makeProject({makeComp("R1", "10k", "R_0603", 10, 10),
                                makeComp("C1", "100n", "C_0402", 20, 10)});
    const auto d = diffProjects(a, a);
    REQUIRE(d.empty());
    REQUIRE(d.unchanged == 2);
}

TEST_CASE("diff: added / removed / value change / move", "[revdiff]")
{
    const auto current = makeProject({
        makeComp("R1", "10k",  "R_0603", 10, 10),
        makeComp("R2", "4k7",  "R_0603", 15, 10),   // removed in target
        makeComp("C1", "100n", "C_0402", 20, 10),   // value changes
        makeComp("U1", "MCU",  "QFP-32", 40, 30),   // moves 3 mm
    });
    const auto target = makeProject({
        makeComp("R1", "10k",  "R_0603", 10, 10),               // unchanged
        makeComp("C1", "1u",   "C_0402", 20, 10),               // new value
        makeComp("U1", "MCU",  "QFP-32", 43, 30),               // moved
        makeComp("D5", "LED",  "LED_0603", 5, 5),               // added
    });

    const auto d = diffProjects(current, target);
    REQUIRE(d.removed == std::vector<std::string>{"R2"});
    REQUIRE(d.added   == std::vector<std::string>{"D5"});
    REQUIRE(d.unchanged == 1);
    REQUIRE(d.changed.size() == 2);

    REQUIRE(d.changed[0].ref == "C1");
    REQUIRE(d.changed[0].valueChanged);
    REQUIRE(d.changed[0].oldValue == "100n");
    REQUIRE(d.changed[0].newValue == "1u");
    REQUIRE(!d.changed[0].moved);

    REQUIRE(d.changed[1].ref == "U1");
    REQUIRE(!d.changed[1].valueChanged);
    REQUIRE(d.changed[1].moved);
    REQUIRE(d.changed[1].moveDistMm == Approx(3.0).margin(1e-9));
}

TEST_CASE("diff: move tolerance and layer change", "[revdiff]")
{
    const auto current = makeProject({
        makeComp("R1", "10k", "R_0603", 10.0, 10.0),
        makeComp("C1", "1u",  "C_0402", 20.0, 10.0, Layer::Front),
    });
    const auto target = makeProject({
        makeComp("R1", "10k", "R_0603", 10.3, 10.0),            // within 0.5 mm
        makeComp("C1", "1u",  "C_0402", 20.0, 10.0, Layer::Back),
    });

    const auto d = diffProjects(current, target, 0.5);
    REQUIRE(d.unchanged == 1);          // R1: 0.3 mm < tolerance
    REQUIRE(d.changed.size() == 1);
    REQUIRE(d.changed[0].ref == "C1");
    REQUIRE(d.changed[0].layerChanged);

    // Tighter tolerance flags the small move too.
    const auto d2 = diffProjects(current, target, 0.1);
    REQUIRE(d2.changed.size() == 2);
}
