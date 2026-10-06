#pragma once

#include "core/level_map.h"

namespace vibestudio {

// Authorable quadratic grids have odd dimensions, matching q3map2's 31-point
// axis limit (external/compilers/q3map2-nrc/tools/quake3/q3map2/q3map2.h).
inline constexpr int kLevelPatchMaxDimension = 31;

struct LevelPatchCreateRequest {
	QString shape = QStringLiteral("plane"); // plane, cylinder, cone
	QString plane = QStringLiteral("xy");    // plane preset only: xy, xz, yz
	int columns = 3;
	int rows = 3;
	LevelMapVec3 center{0, 0, 0, true};
	LevelMapVec3 size{128, 128, 128, true};
	QString texture = QStringLiteral("textures/common/caulk");
};

bool validateLevelPatch(const LevelMapPatch& patch, QString* error = nullptr);
// q3map2 prefixes patchDef2 shader tokens with "textures/" itself. patchDef3
// retains its material path. Normalize external material choices once on
// assignment; LevelMapPatch::textureName stores the map token thereafter.
QString levelPatchMaterialToken(const QString& material, bool patchDef3);
bool createLevelPatch(const LevelPatchCreateRequest& request, LevelMapPatch* patch, QString* error = nullptr);
// Canonical width-major .map definition. Keeps header extension fields and
// comments from sourceLines when an authored patch replaces its old block.
QStringList levelPatchDefinition(const LevelMapPatch& patch);
void refreshLevelPatchBounds(LevelMapPatch* patch);
// Each operation is transactional; invalid input leaves the patch untouched.
bool moveLevelPatchPoints(LevelMapPatch* patch, const QVector<int>& points, const LevelMapVec3& delta, double grid = 0.0,
                          QString* error = nullptr);
// De Casteljau splitting preserves the quadratic surface and its UV mapping.
bool subdivideLevelPatch(LevelMapPatch* patch, bool columns, QString* error = nullptr);
bool invertLevelPatch(LevelMapPatch* patch, QString* error = nullptr);

// The document owns persistence, revisions, selection and undo. A replacement
// keeps its id and entity owner; caller-supplied source bindings are ignored.
bool addLevelMapPatch(LevelMapDocument* document, const LevelMapPatch& patch, int* patchId = nullptr, QString* error = nullptr);
// Insert into an explicit existing owner atomically; validate all source grids
// and their serialized definitions before changing the document or selection.
bool addLevelMapPatches(LevelMapDocument* document, const QVector<LevelMapPatch>& patches, int entityId,
	QVector<int>* patchIds = nullptr, QString* error = nullptr);
bool replaceLevelMapPatch(LevelMapDocument* document, int patchId, const LevelMapPatch& patch, QString* error = nullptr);
// Validate every replacement first, then commit all changed patches in one undo
// command. Keys bind the destination; supplied ownership/source metadata is ignored.
bool replaceLevelMapPatches(LevelMapDocument* document, const QMap<int, LevelMapPatch>& patches, QString* error = nullptr);

} // namespace vibestudio
