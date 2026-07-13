#include "ProjectDiff.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace ibom {

RevisionDiff diffProjects(const IBomProject& current,
                          const IBomProject& target,
                          double moveTolMm)
{
    RevisionDiff diff;

    std::map<std::string, const Component*> curByRef, tgtByRef;
    for (const auto& c : current.components) curByRef[c.reference] = &c;
    for (const auto& c : target.components)  tgtByRef[c.reference] = &c;

    for (const auto& [ref, cur] : curByRef) {
        const auto it = tgtByRef.find(ref);
        if (it == tgtByRef.end()) {
            diff.removed.push_back(ref);
            continue;
        }
        const Component* tgt = it->second;

        RevisionDiff::Changed ch;
        ch.ref = ref;
        if (cur->value != tgt->value) {
            ch.valueChanged = true;
            ch.oldValue = cur->value;
            ch.newValue = tgt->value;
        }
        if (cur->footprint != tgt->footprint) {
            ch.footprintChanged = true;
            ch.oldFootprint = cur->footprint;
            ch.newFootprint = tgt->footprint;
        }
        ch.layerChanged = cur->layer != tgt->layer;

        const double dx = cur->position.x - tgt->position.x;
        const double dy = cur->position.y - tgt->position.y;
        ch.moveDistMm = std::sqrt(dx * dx + dy * dy);
        ch.moved = ch.moveDistMm > moveTolMm;

        if (ch.valueChanged || ch.footprintChanged || ch.layerChanged || ch.moved)
            diff.changed.push_back(std::move(ch));
        else
            ++diff.unchanged;
    }

    for (const auto& [ref, tgt] : tgtByRef)
        if (!curByRef.count(ref))
            diff.added.push_back(ref);

    // std::map iteration already yields sorted refs; keep changed sorted too.
    std::sort(diff.changed.begin(), diff.changed.end(),
              [](const RevisionDiff::Changed& a, const RevisionDiff::Changed& b) {
                  return a.ref < b.ref;
              });
    return diff;
}

} // namespace ibom
