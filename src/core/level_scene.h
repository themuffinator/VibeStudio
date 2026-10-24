#pragma once

#include "core/level_map.h"

#include <QJsonObject>
#include <QSet>

namespace vibestudio {
inline constexpr int kLevelSceneMaxNodes = 1000;
inline constexpr int kLevelSceneMaxMembers = 100000;
inline constexpr int kLevelSceneMaxDepth = 32;
inline constexpr qsizetype kLevelSceneMaxMetadataBytes = 4 * 1024 * 1024;

// Default is implicit and has an empty id. Doom entities alias their things;
// worldspawn is a container, not an assignable scene object.
QStringList levelSceneObjects(const LevelMapDocument& document);
QString levelSceneCanonicalObject(const LevelMapDocument& document, const QString& selector);
const LevelSceneNode* levelSceneNode(const LevelSceneState& state, const QString& id);
QString levelSceneMembership(const LevelSceneState& state, const QString& selector);
QStringList levelSceneMembers(const LevelMapDocument& document, const QString& nodeId, bool recursive = true);
QSet<QString> levelSceneHiddenObjects(const LevelMapDocument& document);
QSet<QString> expandLevelSceneHiddenObjects(const LevelMapDocument& document, QSet<QString> hidden);
QVector<LevelMapSelectionRef> levelSceneSelection(const LevelMapDocument& document, const QString& nodeId);
bool validateLevelScene(const LevelMapDocument& document, const LevelSceneState& state, QString* error = nullptr);

enum class LevelScenePick { Replace, Add, Toggle };
// Prepared once per scene/view revision, not once per hit. Closed groups are
// selection units; opening a group exposes its direct objects and subgroups.
struct LevelSceneSelectionContext {
	QString openGroup;
	QSet<QString> allowedObjects;
	QHash<QString, QString> closedGroups;
	QHash<QString, QVector<LevelMapSelectionRef>> groupSelections;
};
LevelSceneSelectionContext levelSceneSelectionContext(const LevelMapDocument& document);
QVector<LevelMapSelectionRef> pickLevelSceneObjects(const LevelSceneSelectionContext& context,
	const QVector<LevelMapSelectionRef>& selected, const QVector<LevelMapSelectionRef>& hits, LevelScenePick mode);
bool openLevelSceneGroup(LevelMapDocument* document, const QString& id, QString* error = nullptr);
bool closeLevelSceneGroup(LevelMapDocument* document, bool all = false, QString* error = nullptr);
// Empty id closes the current group (or all groups). Selection and editing
// context are restored together by Undo; map contents are unchanged.
bool changeLevelSceneGroupContext(LevelMapDocument* document, const QString& id, bool closeAll = false, QString* error = nullptr);
// Repair after undo, deletion or loading a different scene. Never changes map
// bytes or undo history; session context changes invalidate prepared workers.
void reconcileLevelSceneContext(LevelMapDocument* document);

// Each change is one map undo step. Refusals leave both document and history
// untouched. Unknown/stale metadata requires an explicit reset first.
bool createLevelSceneNode(LevelMapDocument* document, LevelSceneNodeKind kind, const QString& name, const QString& parentId,
						  QString* createdId = nullptr, QString* error = nullptr);
bool renameLevelSceneNode(LevelMapDocument* document, const QString& id, const QString& name, QString* error = nullptr);
bool reparentLevelSceneNode(LevelMapDocument* document, const QString& id, const QString& parentId, QString* error = nullptr);
bool assignLevelSceneObjects(LevelMapDocument* document, const QString& nodeId, const QStringList& objects, QString* error = nullptr);
bool setLevelSceneVisible(LevelMapDocument* document, const QString& id, bool visible, QString* error = nullptr);
// Removing a node moves its members and children to its parent; never geometry.
bool removeLevelSceneNode(LevelMapDocument* document, const QString& id, QString* error = nullptr);
bool resetLevelScene(LevelMapDocument* document, QString* error = nullptr);

// Shared undo integration. Normal callers use the operations above.
bool commitLevelSceneState(LevelMapDocument* document, const LevelSceneState& state, const QString& description, QString* error = nullptr,
	const QHash<QString,QString>& visibilityOrigins = {});
LevelSceneState reconcileLevelScene(const LevelMapDocument& after, const LevelMapUndoCommand& command);

// Copy complete closed groups, including empty descendants. Open ancestors and
// layers remain editing context. Object ids are native record ids, not selection
// indices; the map must include owned brushes/patches copied with their entity.
// Partial owner wrappers carry properties but cannot claim complete groups.
LevelSceneState captureLevelSceneCopy(const LevelMapDocument& source, const QSet<QString>& objects, const QSet<QString>& partialObjects = {});
bool mergeLevelSceneCopy(const LevelMapDocument& source, const LevelMapDocument& target,
	const QHash<QString, QString>& objects, bool sameDocument, LevelSceneState* state, QString* error = nullptr,
	const QSet<QString>& partialObjects = {}, QHash<QString,QString>* copiedNodes = nullptr);

// Both carriers store bounded base64 JSON. Bind membership to the exact native
// body and remap through actual serializer emission order, not vector guesses.
// With TrenchBroom storage the body carries TrenchBroom's layer and group
// entities, and the objects are numbered as the map reads once they are taken
// out; the metadata says so (version 7).
QByteArray encodeLevelScene(const LevelMapDocument& document, const QByteArray& bodyHash, const QHash<QString, QString>& emittedObjects,
							QString* error = nullptr, LevelSceneStorage storage = LevelSceneStorage::VibeStudio);
LevelSceneState decodeLevelScene(const LevelMapDocument& document, const QByteArray& metadata, const QByteArray& bodyHash);
// TrenchBroom when the metadata was written with TrenchBroom storage, so it
// binds to the map with TrenchBroom's entities taken out; VibeStudio for
// anything else, including metadata from before VibeStudio read them.
LevelSceneStorage levelSceneMetadataStorage(const QByteArray& metadata);
QByteArray levelSceneDoomHash(const QMap<QString, QByteArray>& lumps);
QJsonObject levelSceneJson(const LevelMapDocument& document);
} // namespace vibestudio
