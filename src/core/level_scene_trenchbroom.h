#pragma once

// TrenchBroom's layers, groups and linked groups in a Quake-family .map file.
//
// TrenchBroom keeps them in the map itself. Each layer and group is a
// func_group entity holding its brushes and patches, with "_tb_type"
// "_tb_layer" or "_tb_group", a number "_tb_id" and a "_tb_name". A layer adds
// "_tb_layer_sort_index", "_tb_layer_hidden" and "_tb_layer_locked"; a group
// in a layer names it with "_tb_layer", and a group nested in another names
// that with "_tb_group". Entities name their group or layer the same way. A
// linked group adds "_tb_linked_group_id", the UUID its link set shares, and
// "_tb_transformation", its frame as 16 numbers, a 4x4 matrix row by row. An
// entity's protected keys are listed in "_tb_protected_properties", separated
// by semicolons.
//
// Implemented independently from TrenchBroom's manual ("Linked Groups in the
// Map File") and the maps TrenchBroom writes; no TrenchBroom code. See
// docs/CREDITS.md.
//
// VibeStudio's document holds none of this: opening such a map moves the
// brushes and patches of its layers and groups into worldspawn, drops the
// member keys and builds the scene from them, and saving with TrenchBroom
// groups (LevelSceneStorage::TrenchBroom) writes them back, with VibeStudio's
// own scene note after them. When that note is present and still matches the
// map, it wins: it also holds what TrenchBroom does not keep, such as hidden
// or locked groups, out-of-sync marks and which objects correspond. A note
// that no longer matches is set aside and the TrenchBroom keys are read.

#include "core/level_map.h"

namespace vibestudio {

[[nodiscard]] QString levelSceneStorageId(LevelSceneStorage storage);
// "vibestudio" or "trenchbroom", in any case. False for anything else.
bool levelSceneStorageFromId(const QString& id, LevelSceneStorage* storage);

// A layer or group as TrenchBroom's entity gave it.
struct LevelTrenchBroomNode {
	LevelSceneNodeKind kind = LevelSceneNodeKind::Group;
	QString name;
	// Index of the parent node, or -1 at the scene root.
	int parent = -1;
	bool visible = true;
	bool locked = false;
	// "_tb_linked_group_id" as written; empty for a group that is not linked.
	QString link;
	std::array<double, 12> frame {1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0};
	bool frameValid = true;
	// Other keys of the entity, "_tb_id" included, kept for writing back.
	QMap<QString, QString> properties;
};

// What opening a map takes out of its TrenchBroom keys.
struct LevelTrenchBroomImport {
	// True when the map has TrenchBroom layer or group entities or member keys.
	bool found = false;
	// The map without them, to be parsed as the document; empty when they
	// could not be taken out (see `warnings`), and the map opens as it is.
	QString text;
	// The 1-based source line each line of `text` came from.
	QVector<int> sourceLines;
	QVector<LevelTrenchBroomNode> nodes;
	// The line of `text` an object's block opens on -> its node.
	QHash<int, int> members;
	// The line of `text` an entity's block opens on -> its protected keys.
	QHash<int, QStringList> protectedKeys;
	QStringList warnings;
};

// Reads `source`, a map parsed as it is on disk.
[[nodiscard]] LevelTrenchBroomImport readLevelTrenchBroomScene(const LevelMapDocument& source);

// Gives `document`, parsed from `import.text`, its scene: layers and groups
// with ids worked out from the keys, so every open of the file gives the same
// ones; link ids as canonical UUIDs; frames from the top three rows of
// "_tb_transformation"; and corresponding objects matched as Update Linked
// Groups would leave them, by position within each group. Members of a link
// set that do not match the rest are marked out of sync. False, with `error`
// and the document unchanged, when the result is not a valid scene.
bool applyLevelTrenchBroomScene(LevelMapDocument* document, const LevelTrenchBroomImport& import, QStringList* warnings,
	QString* error = nullptr);

// A map written with TrenchBroom groups.
struct LevelTrenchBroomExport {
	QString text;
	// Each object of the source, by selector -> the selector opening `text`
	// gives it, once its layer and group brushes are back in worldspawn.
	QHash<QString, QString> objects;
	QStringList warnings;
	QStringList errors;
};

// Writes `source`, a document parsed from its own saved text (so every object
// has source lines), with the layers and groups of `scene`, given in the
// selectors of `source`.
[[nodiscard]] LevelTrenchBroomExport writeLevelTrenchBroomScene(const LevelMapDocument& source, const LevelSceneState& scene);

} // namespace vibestudio
