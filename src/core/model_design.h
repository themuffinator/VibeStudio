#pragma once

#include "core/model_work.h"

#include "core/level_map.h"
#include "core/model_mesh.h"
#include "core/package_staging.h"

#include <QHash>
#include <QJsonObject>

namespace vibestudio
{

// Editable, bounded static props. Units and axes match the level editor (Z up).
// The design is the authoring source; MD3 and OBJ are deterministic derivatives.
struct ModelDesignPart {
	QString name = QStringLiteral("part");
	QString primitive = QStringLiteral("box"); // box, cylinder, plane
	ModelVec3 size{64, 64, 64};
	ModelVec3 origin;
	// Rotate about the part centre: X (roll), then Y (pitch), then Z (yaw).
	double roll = 0;
	double pitch = 0;
	double yaw = 0;
	// Scale UVs about (0,0), rotate about (0,0), then offset. Negative scales
	// mirror a texture; zero scales are invalid.
	ModelTexCoord uvScale{1, 1};
	ModelTexCoord uvOffset;
	double uvRotation = 0;
	int segments = 12;
	QString material = QStringLiteral("textures/common/caulk");
};

struct ModelDesign {
	QString name = QStringLiteral("prop");
	QVector<ModelDesignPart> parts;
};

QStringList validateModelDesign(const ModelDesign& design);
QJsonObject modelDesignJson(const ModelDesign& design);
bool parseModelDesign(const QByteArray& json, ModelDesign* design, QString* error = nullptr);
ModelMesh buildModelDesignMesh(const ModelDesign& design);
QByteArray exportModelDesign(const ModelDesign& design, const QString& format, QString* error = nullptr);
// Atomic writes; explicit overwrite, bounded inputs, and source protection are
// shared by the GUI and CLI. No external tools or game data are required.
bool saveModelDesignBytes(const QString& path, const QByteArray& bytes, bool overwrite, QString* error = nullptr,
                          const QString& protectedSource = QString(), const ModelWorkControl& control = {}, bool dryRun = false);
bool loadModelDesign(const QString& path, ModelDesign* design, QString* error = nullptr);

// Both changes are prepared independently and committed together. An invalid
// map, package, path, or model leaves the map and staging state untouched.
bool stageModelDesign(const ModelDesign& design, const QString& virtualPath, PackageStagingModel* staging,
                      LevelMapDocument* placeInMap = nullptr, const LevelMapVec3& origin = {}, bool replaceExisting = false,
                      QString* error = nullptr);
bool stageModelMesh(const ModelMesh& mesh, const QString& virtualPath, PackageStagingModel* staging,
                    LevelMapDocument* placeInMap = nullptr, const LevelMapVec3& origin = {}, bool replaceExisting = false,
                    QString* error = nullptr);
// Stage bytes prepared by exportEditableModel; keeps expensive serialization in
// the document worker. Like addBytes, this does not re-decode the asset payload.
bool stageModelExport(const QByteArray& bytes, const QString& virtualPath, PackageStagingModel* staging,
                      LevelMapDocument* placeInMap = nullptr, const LevelMapVec3& origin = {}, bool replaceExisting = false,
                      QString* error = nullptr);
// Several prepared exports (a base MD3 and its detail levels, path then bytes)
// staged as one package operation, so one undo removes them all; the first is
// the one placed. Each is checked as stageModelExport checks one, and nothing
// is staged when any fails.
bool stageModelExports(const QVector<QPair<QString, QByteArray>>& files, PackageStagingModel* staging,
                       LevelMapDocument* placeInMap = nullptr, const LevelMapVec3& origin = {}, bool replaceExisting = false,
                       QString* error = nullptr);
bool placeLevelModel(LevelMapDocument* document, const QString& virtualPath, const LevelMapVec3& origin, int* entityId = nullptr,
                     QString* error = nullptr);
// The bytes of the package's planned files at `virtualPaths`, matched without
// case as the game's file system matches them: the detail files beside an MD3
// about to be staged, for core/model_lod.h reviewModelLodSiblings. A file that
// cannot be read is returned empty, so the review reports it.
[[nodiscard]] QHash<QString, QByteArray> readStagedModelFiles(const PackageStagingModel& staging, const QStringList& virtualPaths);

} // namespace vibestudio
