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
QVector<LevelMapSelectionRef> levelSceneSelection(const LevelMapDocument& document, const QString& nodeId);
bool validateLevelScene(const LevelMapDocument& document, const LevelSceneState& state, QString* error = nullptr);

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
bool commitLevelSceneState(LevelMapDocument* document, const LevelSceneState& state, const QString& description, QString* error = nullptr);
LevelSceneState reconcileLevelScene(const LevelMapDocument& after, const LevelMapUndoCommand& command);

// Both carriers store bounded base64 JSON. Bind membership to the exact native
// body and remap through actual serializer emission order, not vector guesses.
QByteArray encodeLevelScene(const LevelMapDocument& document, const QByteArray& bodyHash, const QHash<QString, QString>& emittedObjects,
							QString* error = nullptr);
LevelSceneState decodeLevelScene(const LevelMapDocument& document, const QByteArray& metadata, const QByteArray& bodyHash);
QByteArray levelSceneDoomHash(const QMap<QString, QByteArray>& lumps);
QJsonObject levelSceneJson(const LevelMapDocument& document);
} // namespace vibestudio
