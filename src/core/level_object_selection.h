#pragma once
#include "core/level_brush.h"
#include <QSet>
#include <optional>

namespace vibestudio {
// Layers runs expandLevelMapSelectionToLayers (core/level_scene_layers.h) for
// any map with a scene, with excludedObjects and undoSelection.
enum class LevelObjectSelectionMode { Siblings, SourceLines, ConnectedEntities, RadiantType, Layers };
enum class LevelTypeSelectionScope { Objects, Vertices, Edges, Faces };
struct LevelObjectSelectionRequest {
	LevelObjectSelectionMode mode = LevelObjectSelectionMode::Siblings;
	QVector<int> lines;
	QSet<QString> excludedObjects;
	bool undoSelection = false;
	LevelTypeSelectionScope typeScope = LevelTypeSelectionScope::Objects;
	std::optional<QString> material; // RadiantType: required for material fallback or faces.
};
struct LevelObjectSelectionReport {
	QVector<LevelMapSelectionRef> selection;
	QStringList matchedClasses;
	bool materialFallback = false;
	bool componentSelectionChanged = false;
	QVector<LevelMapComponentSet> components;
	// Layers: the layers expanded ("" is Default) and closed groups left out.
	QStringList layers;
	int skippedGroups = 0;
};
// Positive, one-based numbers separated by commas or whitespace. Failure leaves
// the output untouched; the shared limit also bounds headless requests.
bool parseLevelMapSourceLines(const QString& text, QVector<int>* lines, QString* error = nullptr);
// Quake-family objects. Source lines refer to the text snapshot at load time,
// just like the object's diagnostic line numbers, until the map is reopened.
// New objects have no source span. Closed groups are atomic selection units;
// hidden, locked, filtered and out-of-context objects cannot be selected.
// A failure or worker cancellation never publishes a partial selection/report.
// ConnectedEntities adds direct incoming then outgoing target/targetname
// neighbours from the original picks; it never traverses a transitive closure.
// It preserves existing picks, reads selected brush/patch owners, and excludes
// worldspawn from matching. Names and property keys are case-sensitive.
// RadiantType replaces object picks with every selected nonworld owner's exact
// classname, or visible brushes/patches using material if there are no owners.
// Faces replaces component picks on selected brushes (including selected owner
// geometry) without changing object picks or history. Vertices/Edges are no-ops.
// Material comparison ignores case but preserves whitespace. GUI callers must
// validate that every returned component fits their current handle set before
// replacing picks; a false componentSelectionChanged means leave picks intact.
bool selectLevelMapObjects(LevelMapDocument* document, const LevelObjectSelectionRequest& request,
	LevelObjectSelectionReport* report = nullptr, QString* error = nullptr);
} // namespace vibestudio
