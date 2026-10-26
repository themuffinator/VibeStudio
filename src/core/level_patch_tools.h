#pragma once

// The rest of Radiant's Patch menu for Quake III patches: the matrix
// commands (insert, add and delete control rows and columns, transpose,
// invert, re-disperse and smooth), Thicken, and the patch texture
// operations Natural, Fit and Cap. Behaviour follows GtkRadiant 1.5 and
// NetRadiant Custom (radiant/patch.cpp and radiant/patchmanip.cpp,
// GPL-2.0-or-later, revision 8216133984031afaa9a857b56ea66dd9c3d54b26);
// the implementations are VibeStudio's own. See docs/CREDITS.md.

#include "core/level_patch.h"

#include <QHash>
#include <QSize>
#include <QStringList>
#include <QVector>

namespace vibestudio {

enum class LevelPatchMatrixOperation {
	// Split the first span of every row in two, exactly: two more columns at
	// the start (Radiant's Insert Columns). AddColumns does it at the end.
	InsertColumns,
	AddColumns,
	InsertRows,
	AddRows,
	// Join the first or last two spans into one through the point they
	// shared: two columns or rows fewer (Radiant's Delete Columns/Rows).
	DeleteFirstColumns,
	DeleteLastColumns,
	DeleteFirstRows,
	DeleteLastRows,
	// Swap rows and columns; the patch then faces the other way.
	Transpose,
	// Turn the patch to face the other way.
	Invert,
	// Put each span's middle control point halfway between its ends, along
	// rows (columns) or columns (rows): Radiant's Re-disperse.
	RedisperseColumns,
	RedisperseRows,
	// Put each point two spans share halfway between its neighbouring
	// controls, so the spans meet without a crease; a closed row is smoothed
	// across its seam too: NetRadiant Custom's Smooth.
	SmoothColumns,
	SmoothRows,
};

[[nodiscard]] QString levelPatchMatrixOperationId(LevelPatchMatrixOperation operation);
[[nodiscard]] bool parseLevelPatchMatrixOperation(const QString& id, LevelPatchMatrixOperation* operation);
[[nodiscard]] QStringList levelPatchMatrixOperationIds();
// Transactional: an operation the grid cannot take leaves the patch as it was.
bool applyLevelPatchMatrixOperation(LevelMapPatch* patch, LevelPatchMatrixOperation operation, QString* error = nullptr);

// The patch's facing, as the average of its control rows and columns
// crossed: the normal Radiant's Cap Texture projects along.
[[nodiscard]] LevelMapVec3 levelPatchAverageNormal(const LevelMapPatch& patch);

// Patch texture coordinates are in texture repeats.
// Radiant's Natural: U runs along the columns and V down the rows by the
// distance between control points, the longest row or column setting each
// step, so a texture keeps its proportions along the surface.
bool naturalLevelPatchTexture(LevelMapPatch* patch, double unitsPerRepeatU, double unitsPerRepeatV, QString* error = nullptr);
// Radiant's Cap Texture: a flat projection across the patch along its
// average normal, with the axes brush primitives use for that plane.
bool planarLevelPatchTexture(LevelMapPatch* patch, double unitsPerRepeatU, double unitsPerRepeatV, QString* error = nullptr);
// NetRadiant Custom's Project Axial on a patch: a flat projection along the
// world axis nearest its average normal (the axis a face facing that way is
// projected along, q3map2's TextureAxisFromPlane), with no shift or turn.
bool axialLevelPatchTexture(LevelMapPatch* patch, double unitsPerRepeatU, double unitsPerRepeatV, QString* error = nullptr);
// Its Project Ortho and Project Camera on a patch: a flat projection along
// `direction` (any length but zero), with the axes brush primitives use for a
// plane facing that way (q3map2's ComputeAxisBase).
bool projectedLevelPatchTexture(LevelMapPatch* patch, const LevelMapVec3& direction, double unitsPerRepeatU, double unitsPerRepeatV,
	QString* error = nullptr);

struct LevelPatchThickenRequest {
	// Map units; negative thickens the other way.
	double thickness = 16;
	// -1 offsets each control point along the surface's normal there, as
	// NetRadiant Custom's Thicken does; 0, 1 or 2 along that world axis.
	int axis = -1;
	// Also close the open edges with walls joining the two surfaces.
	bool seams = true;
};
// Radiant's Thicken: a copy of the patch offset by the thickness and turned
// to face away, and walls along its open edges (none along an edge that
// closes on itself, as round a cylinder). The source is left as it was.
bool thickenLevelPatch(const LevelMapPatch& source, const LevelPatchThickenRequest& request, QVector<LevelMapPatch>* created,
	QString* error = nullptr);

// The patches the selection names, directly or through the entity owning
// them, in document order.
[[nodiscard]] QVector<int> levelMapSelectedPatchIds(const LevelMapDocument& document);
// One matrix operation on each patch as one undo step. All or nothing.
bool applyLevelMapPatchMatrixOperation(LevelMapDocument* document, const QVector<int>& patchIds, LevelPatchMatrixOperation operation,
	int* changed = nullptr, QString* error = nullptr);
// Thickens each patch into its own entity as one undo step, selecting what
// was made.
bool thickenLevelMapPatches(LevelMapDocument* document, const QVector<int>& patchIds, const LevelPatchThickenRequest& request,
	QVector<int>* created = nullptr, QString* error = nullptr);

} // namespace vibestudio
