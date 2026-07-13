#pragma once

#include <QObject>
#include <QString>
#include <vector>
#include <string>
#include <unordered_set>
#include "../ibom/IBomData.h"

namespace ibom {
struct Component;
}

namespace ibom::features {

/// Pick-and-Place guided workflow: 
/// highlights next component to place, tracks placement order
class PickAndPlace : public QObject {
    Q_OBJECT

public:
    explicit PickAndPlace(QObject* parent = nullptr);
    ~PickAndPlace() override = default;

    struct PlacementStep {
        std::string reference;
        std::string value;
        std::string footprint;
        Layer       layer = Layer::Front;
        Point2D     position;          ///< PCB mm (route ordering, navigation)
        bool        placed = false;
        int         order  = 0;
    };

    /// Load component list and generate placement order
    void loadComponents(const std::vector<Component>& components);

    /// Sort by value grouping (place all 100nF caps together, etc.)
    void sortByValueGroup();

    /// Sort by value grouping with most numerous group first
    /// (minimizes SMD reel changes)
    void sortByValueGroupCount();

    /// Sort by physical position (top-left to bottom-right)
    void sortByPosition();

    /// Custom sort by footprint size (smallest first)
    void sortByFootprintSize();

    /// Shortest-travel route: greedy nearest-neighbor walk over component
    /// positions, starting from the top-left-most part — the guided-tour
    /// order (FEATURE_PROPOSALS B1). At the microscope this minimizes stage
    /// travel between consecutive components.
    void sortByNearestNeighbor();

    /// Get current step
    const PlacementStep& currentStep() const;

    /// Mark current as placed and advance
    void markPlaced();

    /// Skip current component
    void skip();

    /// Go back to previous
    void goBack();

    /// Reset all placements
    void reset();

    /// Undo support (FEATURE_PROPOSALS B3): un-mark a placed component and
    /// make it the current step again. Emits currentStepChanged and
    /// progressChanged. @return false if the ref is unknown or not placed.
    bool unplace(const std::string& reference);

    /// Restore a previous session: mark the given references as placed and
    /// position on the first unplaced step. Emits progressChanged and
    /// currentStepChanged (or allPlaced) once.
    /// @return number of steps actually matched and marked placed.
    int restorePlaced(const std::unordered_set<std::string>& placedRefs);

    /// Get all steps
    const std::vector<PlacementStep>& steps() const { return m_steps; }

    int currentIndex() const { return m_currentIndex; }
    int totalSteps()   const { return static_cast<int>(m_steps.size()); }
    int placedCount()  const;

    bool isComplete() const;

signals:
    void currentStepChanged(const PlacementStep& step);
    void stepPlaced(const std::string& reference);
    void allPlaced();
    void progressChanged(int placed, int total);

private:
    void emitProgress();

    std::vector<PlacementStep> m_steps;
    int m_currentIndex = 0;
};

} // namespace ibom::features
