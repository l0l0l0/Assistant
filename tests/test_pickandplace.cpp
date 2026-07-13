// PickAndPlace guided-tour extensions (FEATURE_PROPOSALS B1/B3): the
// nearest-neighbor route must visit every component with a much shorter
// travel than a value-sorted order, and unplace() must revert a placement
// and re-target the step (the Ctrl+Z path).

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <set>
#include <vector>

#include "features/PickAndPlace.h"

using namespace ibom;
using features::PickAndPlace;

namespace {

/// Components on a grid, deliberately loaded in a travel-hostile order
/// (alternating far corners) with values that interleave the columns.
std::vector<Component> gridComponents()
{
    std::vector<Component> comps;
    int n = 0;
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 8; ++x) {
            Component c;
            c.reference = "R" + std::to_string(++n);
            c.value     = (x % 2 == 0) ? "10k" : "100n";
            c.footprint = "R_0603";
            c.position  = {x * 10.0, y * 10.0};
            comps.push_back(c);
        }
    }
    return comps;
}

double routeLength(const std::vector<PickAndPlace::PlacementStep>& steps)
{
    double len = 0.0;
    for (size_t i = 1; i < steps.size(); ++i) {
        const double dx = steps[i].position.x - steps[i - 1].position.x;
        const double dy = steps[i].position.y - steps[i - 1].position.y;
        len += std::sqrt(dx * dx + dy * dy);
    }
    return len;
}

} // namespace

TEST_CASE("nearest-neighbor route: complete, starts top-left, much shorter", "[pnp]")
{
    PickAndPlace pnp;
    pnp.loadComponents(gridComponents());   // loadComponents applies value sort
    const double valueSortedLen = routeLength(pnp.steps());

    pnp.sortByNearestNeighbor();
    const auto& steps = pnp.steps();

    // Every component exactly once.
    std::set<std::string> refs;
    for (const auto& s : steps) refs.insert(s.reference);
    REQUIRE(refs.size() == 40);

    // Entry point = top-left-most component of the grid.
    REQUIRE(steps.front().position.x == 0.0);
    REQUIRE(steps.front().position.y == 0.0);

    // On a grid the greedy walk is near-optimal (~10 mm hops); the
    // value-grouped order zig-zags across columns. Expect a big win.
    const double nnLen = routeLength(steps);
    REQUIRE(nnLen < valueSortedLen * 0.5);

    // Orders renumbered to match the new sequence.
    for (int i = 0; i < static_cast<int>(steps.size()); ++i)
        REQUIRE(steps[i].order == i);
}

TEST_CASE("unplace: reverts a placement and re-targets the step", "[pnp]")
{
    PickAndPlace pnp;
    pnp.loadComponents(gridComponents());
    pnp.sortByNearestNeighbor();

    const std::string first  = pnp.currentStep().reference;
    pnp.markPlaced();
    const std::string second = pnp.currentStep().reference;
    pnp.markPlaced();
    REQUIRE(pnp.placedCount() == 2);
    REQUIRE(pnp.currentIndex() == 2);

    // Undo the second placement: current step returns to it.
    REQUIRE(pnp.unplace(second));
    REQUIRE(pnp.placedCount() == 1);
    REQUIRE(pnp.currentStep().reference == second);
    REQUIRE(!pnp.currentStep().placed);

    // Not placed / unknown refs are refused.
    REQUIRE(!pnp.unplace(second));           // already reverted
    REQUIRE(!pnp.unplace("NOPE"));

    // Undo the first as well — back to a clean slate for both.
    REQUIRE(pnp.unplace(first));
    REQUIRE(pnp.placedCount() == 0);
    REQUIRE(pnp.currentStep().reference == first);
}
