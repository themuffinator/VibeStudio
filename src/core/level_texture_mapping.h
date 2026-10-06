#pragma once

#include "core/level_map.h"
#include <QPointF>

namespace vibestudio
{

// Classic and Valve coordinates are in texels; brush primitives store repeats.
// Material resolution belongs to the asset service, not to this geometry math.
struct LevelTextureProjection {
	LevelMapVec3 u, v;
	double offsetU = 0, offsetV = 0;
	bool normalizedCoordinates = false;
	bool valid = false;
	QPointF at(const LevelMapVec3 &point) const;
};

LevelTextureProjection levelTextureProjection(const LevelMapBrushFace &face);
// Rigidly transport UVs around the planes' intersection, keeping the shared
// edge fixed. Parallel/opposite planes retain the source world projection.
LevelTextureProjection wrappedLevelTextureProjection(const LevelMapBrushFace& source, const LevelMapBrushFace& target);
// Express a world projection in the target's existing mapping family. Callers
// convert texels/repeats using real image dimensions first. Classic shear needs
// explicit permission to convert the target face to Valve 220 axes.
bool setLevelTextureProjection(LevelMapBrushFace* face, const LevelTextureProjection& projection,
	bool allowValve220, QString* error = nullptr);
// Empty for no brush faces, otherwise classic, valve220, brushDef, brushDef3
// or mixed. Clipboard and prefab insertion share the same compatibility rule.
QString levelMapBrushDialect(const LevelMapDocument& document);

// Columns of the linear transform followed by its translation: p' = A p + t.
// Brush planes are transformed by the caller; UV covectors use A^-T so every
// corresponding point keeps its coordinates, including nonuniform scaling.
struct LevelTextureAffine {
	std::array<LevelMapVec3, 3> columns{{{1, 0, 0, true}, {0, 1, 0, true}, {0, 0, 1, true}}};
	LevelMapVec3 translation{0, 0, 0, true};
};
bool lockTransformedLevelTexture(const LevelMapBrushFace &before, LevelMapBrushFace *after, const LevelTextureAffine &transform,
								 bool allowValve220, QString *error = nullptr);
LevelMapVec3 rotateLevelVector(const LevelMapVec3 &vector, int axis, double degrees);
LevelMapVec3 rotateLevelPoint(const LevelMapVec3 &point, const LevelMapVec3 &pivot, int axis, double degrees);
// idTech model orientation: yaw around Z, pitch around Y, roll around X.
LevelMapVec3 rotateLevelAngles(const LevelMapVec3 &pitchYawRoll, int axis, double degrees);

// `after` already has its rotated plane. Preserve UV at every point on that
// plane, retaining the original dialect whenever it can express the mapping.
// Classic projections that need shear require explicit Valve 220 conversion.
bool lockRotatedLevelTexture(const LevelMapBrushFace &before, LevelMapBrushFace *after, int axis, double degrees, const LevelMapVec3 &pivot,
							 bool allowValve220, QString *error = nullptr);

} // namespace vibestudio
