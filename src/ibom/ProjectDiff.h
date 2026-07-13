#pragma once

#include <string>
#include <vector>

#include "IBomData.h"

namespace ibom {

/**
 * Structural diff between two revisions of a board (FEATURE_PROPOSALS C1) —
 * the "update rework" view: which components must be removed, added, or
 * exchanged to bring the board on the bench (rev A) to the target revision
 * (rev B). Keyed by reference designator; pure logic, unit-tested in CI
 * (test_project_diff).
 */
struct RevisionDiff {
    struct Changed {
        std::string ref;
        std::string oldValue, newValue;          // differ when valueChanged
        std::string oldFootprint, newFootprint;  // differ when footprintChanged
        bool   valueChanged     = false;
        bool   footprintChanged = false;
        bool   layerChanged     = false;
        bool   moved            = false;         // position delta > tolerance
        double moveDistMm       = 0.0;
    };

    std::vector<std::string> removed;   ///< refs present only in A (desolder)
    std::vector<std::string> added;     ///< refs present only in B (place)
    std::vector<Changed>     changed;   ///< same ref, different part/spot
    int unchanged = 0;

    bool empty() const
    {
        return removed.empty() && added.empty() && changed.empty();
    }
};

/// Compare `current` (the board on the bench) against `target` (the revision
/// to reach). Components are matched by reference; a position delta beyond
/// moveTolMm flags the component as moved.
RevisionDiff diffProjects(const IBomProject& current,
                          const IBomProject& target,
                          double moveTolMm = 0.5);

} // namespace ibom
