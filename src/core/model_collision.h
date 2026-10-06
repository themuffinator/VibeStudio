#pragma once

#include "core/level_placement.h"
#include "core/model_mesh.h"
#include <QJsonArray>
#include <array>

namespace vibestudio
{
enum class ModelEditKind;
struct ModelEdit;
struct ModelSelection;
struct ModelTransform;
inline constexpr int modelCollisionBoxLimit = 64;

void setModelCollisionFrames(ModelCollisionBox *box, QVector<ModelCollisionPose> poses);
// Samples a validated box into a static result. Endpoints are exact; intermediate centres and
// sizes are linear, orientations follow the same shortest arc as attachment tags.
bool sampleModelCollisionBox(const ModelCollisionBox &box, int frame, int nextFrame, double amount, ModelCollisionBox *result);

std::array<ModelVec3, 8> modelCollisionCorners(const ModelCollisionBox &box);
std::array<ModelVec3, 3> modelCollisionAxes(const ModelCollisionBox &box);
// Translation/rotation use the chosen basis; size scaling always uses the box's
// own local axes about the world-space pivot.
// Preserves an oriented box without introducing shear. Atomic on failure.
bool transformModelCollisionBox(const ModelCollisionBox &box, const ModelTransform &transform, ModelCollisionBox *result,
								QString *error = nullptr);
QStringList validateModelCollision(const ModelMesh &mesh, const ModelWorkControl &control = {});
const ModelCollisionBox *findModelCollisionBox(const ModelMesh &mesh, const QString &name);
bool validModelCollisionSelection(const ModelMesh &mesh, const ModelSelection &selection);
bool isModelCollisionEdit(ModelEditKind kind);
// Candidate-only edit; applyModelEdit validates and commits the whole state.
bool applyModelCollisionEdit(ModelMesh *candidate, const ModelEdit &edit, ModelSelection *selection, QString *error,
							 const ModelWorkControl &control = {});
QJsonArray modelCollisionJson(const ModelMesh &mesh, QString *error = nullptr, const ModelWorkControl &control = {});
bool parseModelCollision(const QJsonValue &value, ModelMesh *mesh, QString *error, const ModelWorkControl &control = {});

struct ModelCollisionExport
{
	QString target; // quake, quake2, quake3; always explicit
	// Quake III requires a project shader below textures/, without that prefix.
	// Its surfaceparms, not numeric map flags, determine compiler behavior.
	QString material;
	ModelVec3 origin;
	// Required when any box is animated. A map receives this stored pose only.
	int frame = -1;
};
struct ModelCollisionMap
{
	QByteArray bytes;
	QStringList notes;
	QString material;
	int brushCount = 0;
};
// One static worldspawn brush per box. Never changes render meshes or frames.
bool exportModelCollisionMap(const ModelMesh &mesh, const ModelCollisionExport &request, ModelCollisionMap *result,
							 QString *error = nullptr, const ModelWorkControl &control = {});
// Shared placement/history service: one map undo step, scene locks, cancellation,
// target checks. Caller publishes the returned candidate after context checks.
LevelPlacementResult prepareModelCollisionPlacement(const ModelMesh &mesh, const ModelCollisionExport &request, const LevelMapDocument &map,
													const ModelWorkControl &control = {});
QString modelCollisionOmissionNote();
} // namespace vibestudio
