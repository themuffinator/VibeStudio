#pragma once
#include "core/level_brush.h"
#include <QPointF>
#include <QSet>
#include <optional>

namespace vibestudio {
enum class LevelComponentPick { Replace, Add, Subtract, Toggle };
// Click toggles a coincident group together; a lasso toggles individual hits.
QSet<int> selectLevelComponentHandles(const QSet<int>& current,const QSet<int>& hits,
	LevelComponentPick operation,bool groupToggle = false);
// Profiles supply their ordinary toggle/add roles. Component mode supplies
// Ctrl/Shift alternatives when an object-selection role is disabled/conflicts.
std::optional<LevelComponentPick> levelComponentPickOperation(Qt::KeyboardModifiers keys,
	Qt::KeyboardModifiers toggle,Qt::KeyboardModifiers add,bool band,bool bandToggles);
// Order-independent solved geometry, not its centre: crossing edges and
// concentric faces must remain different handles. Coordinates are quantised
// to 1e-6 map units to absorb independent plane-solver roundoff.
QString levelComponentGeometryKey(const QVector<LevelMapVec3>& vertices);

struct LevelComponentCycleHit {
	int handle = -1;
	double depth = 0, distanceSquared = 0; // Larger depth is nearer the viewer.
	QString geometryKey;
};
struct LevelComponentCycleResult {
	QSet<int> picked;
	int ordinal = 0, count = 0, handle = -1;
};
// Cycle full-geometry groups near to far (positive steps), preserving picks
// outside the overlap. Multiple selected groups start a fresh cycle.
LevelComponentCycleResult cycleLevelComponentHits(const QVector<LevelComponentCycleHit>& hits,
	const QSet<int>& picked, int steps);
struct LevelComponentCycleCursor {
	std::optional<QPointF> point;
	double remainder = 0;
	int ordinal = 0, count = 0, handle = -1;
	void reset() { *this = {}; }
	void clearResult() { ordinal = count = 0; handle = -1; }
	void aim(const QPointF& position);
	int wheelSteps(int delta);
};

enum class LevelComponentSelectionOperation { Convert, Grow, Shrink, Connected };
struct LevelComponentSelectionRequest {
	LevelComponentSelectionOperation operation = LevelComponentSelectionOperation::Convert;
	LevelBrushComponent target = LevelBrushComponent::Vertex;
	// Upward conversion normally requires every incident source component.
	// Touching accepts any incident source; downward conversion always uses any.
	bool touching = false;
	int steps = 1; // Grow/Shrink only, 1..256; Connected visits the whole graph.
};
// Compact incidence data cached with the viewport's existing solved handles.
// Brush adjacency: vertices share an edge; edges share an endpoint; faces
// share an edge. Patch points use four grid neighbours; the open grid border
// erodes on Shrink. Operations never cross object boundaries implicitly.
struct LevelComponentSelectionTopology {
	LevelMapSelectionRef object;
	int vertexCount = 0;
	QVector<std::array<int,2>> edges;
	QVector<QVector<int>> faces;
	int patchWidth = 0, patchHeight = 0;
};
LevelComponentSelectionTopology levelComponentSelectionTopology(const LevelMapSelectionRef& object, const LevelBrushTopology& topology);
LevelComponentSelectionTopology levelComponentSelectionTopology(const LevelMapPatch& patch);
// Pure queries: stable sorted IDs, no document/undo mutation, no partial output
// on invalid/stale IDs. Patch selections stay control points in every mode.
bool queryLevelComponentSelection(const QVector<LevelComponentSelectionTopology>& topology,
	const QVector<LevelMapComponentSet>& input, const LevelComponentSelectionRequest& request,
	QVector<LevelMapComponentSet>* output, QString* error = nullptr);
bool selectLevelMapComponents(const LevelMapDocument& document, const QVector<LevelMapComponentSet>& input,
	const LevelComponentSelectionRequest& request, QVector<LevelMapComponentSet>* output, QString* error = nullptr);
} // namespace vibestudio
