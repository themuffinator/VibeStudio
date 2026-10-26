#pragma once

#include "core/level_material_paint.h"
#include "core/level_surface.h"

#include <QPointF>
#include <QSize>
#include <QString>
#include <QVector>

namespace vibestudio {

// The UV Editor's service: one brush face's winding or one patch's control
// grid in texture space, as NetRadiant Custom's TexTool plugin shows it.
// Dragging a face's point moves its whole texture so that point follows the
// pointer; once a point has been dragged it is the anchor, and dragging
// another point turns the texture about it. A patch's control points move one
// by one. Workflow reference: NRC plugins/textool/TexTool.cpp,
// ControlPointsManager.cpp and 2DView.cpp at 449778b3 (GPL-2.0-or-later).
// Original implementation, for every face format and patches; see
// docs/CREDITS.md.

// A transform of texture space, in texels: s' = xx s + xy t + dx and
// t' = yx s + yy t + dy. Texture space runs s to the right and t down the
// image, as the UV Editor draws it, so a positive turn reads clockwise there.
struct LevelUvTransform {
	double xx = 1, xy = 0, yx = 0, yy = 1, dx = 0, dy = 0;
	[[nodiscard]] QPointF map(QPointF point) const;
	// This transform, then `next`.
	[[nodiscard]] LevelUvTransform then(const LevelUvTransform& next) const;
	[[nodiscard]] bool isTranslation() const { return xx == 1 && xy == 0 && yx == 0 && yy == 1; }
	[[nodiscard]] bool isIdentity() const { return isTranslation() && dx == 0 && dy == 0; }
	[[nodiscard]] bool isFinite() const;
	static LevelUvTransform translation(QPointF delta);
	// A turn of `clockwiseDegrees` about `pivot`, clockwise as the UV Editor
	// draws texture space. Quarter and half turns are exact.
	static LevelUvTransform rotation(QPointF pivot, double clockwiseDegrees);
};

// The clockwise turn, in degrees from -180 to 180, that takes the direction
// from `pivot` to `from` onto the direction from `pivot` to `to`; 0 when
// either point sits on the pivot.
double levelUvTurnAngle(QPointF pivot, QPointF from, QPointF to);
// `point` on a grid of `step` texels; a step of 0 or less leaves it.
QPointF snapLevelUv(QPointF point, double step);

enum class LevelUvFormat { Classic, Valve220, BrushPrimitive, Patch };

// One brush face's winding, or one patch's control grid, in texture space.
struct LevelUvLayout {
	LevelMaterialTarget target;
	LevelUvFormat format = LevelUvFormat::Classic;
	QString material;
	// Texels in one repeat of the image. Brush primitives and patches count
	// repeats, so their texel coordinates need it; classic and Valve 220
	// faces count texels and use it only to draw the image (it may be empty).
	QSize imageSize;
	// A patch's control grid: `points` and `uv` run row by row. 0 for a face.
	int columns = 0, rows = 0;
	// World positions: the face's winding in order, or the control points.
	QVector<LevelMapVec3> points;
	// Texel coordinates of each point, as the map reads them.
	QVector<QPointF> uv;
	LevelMapBrushFace face;
	LevelMapPatch patch;
	[[nodiscard]] bool isPatch() const { return target.kind == LevelMaterialKind::Patch; }
	// The average of the points' texel coordinates.
	[[nodiscard]] QPointF centre() const;
	// Whether stored coordinates count repeats rather than texels.
	[[nodiscard]] bool countsRepeats() const { return format == LevelUvFormat::BrushPrimitive || format == LevelUvFormat::Patch; }
};

// Why a target cannot open in the UV Editor, or empty: it must be one brush
// face with an area, or one patch, of a Quake-family text map, visible,
// unlocked and inside the open group.
QString levelUvTargetProblem(const LevelMapDocument& document, const LevelMaterialTarget& target);
// Reads one brush face or patch. Brush primitives and patches need a valid
// `imageSize`; classic and Valve 220 faces keep whatever size is given.
bool readLevelUvLayout(const LevelMapDocument& document, const LevelMaterialTarget& target, QSize imageSize, LevelUvLayout* layout,
	QString* error = nullptr);

struct LevelUvEdit {
	// A face: the transform its whole texture takes.
	LevelUvTransform transform;
	// A patch: every control point's new texel coordinates, or empty to keep
	// them all.
	QVector<QPointF> patchUv;
	// A face: the point (from 0) a turn keeps fixed, or -1. A classic face
	// that cannot hold a turn exactly keeps this point's coordinates, or
	// without one every point's on average.
	int anchor = -1;
	// A classic face that cannot hold the transform exactly (a turn of a
	// texture scaled differently along U and V) becomes a Valve 220 face
	// instead of taking the nearest classic mapping.
	bool allowValve220 = false;
};

// Unchanged: the edit changes nothing. Exact: the face or patch holds it in
// its own format. Converted: a classic face became a Valve 220 face to hold
// it. Approximated: a classic face took the nearest classic mapping.
enum class LevelUvFit { Unchanged, Exact, Converted, Approximated };

struct LevelUvResult {
	LevelMapBrushFace face;
	LevelMapPatch patch;
	// The points' texel coordinates after the edit, as the map will read them.
	QVector<QPointF> uv;
	LevelUvFit fit = LevelUvFit::Unchanged;
};

// The face or patch after `edit`, each face in its own format: a move
// changes only the offsets, a turn of a classic face keeps it classic where
// its format holds the turn exactly, and a Valve 220 face keeps its scale
// values. Coordinates are bounded to a million repeats.
bool applyLevelUvEdit(const LevelUvLayout& layout, const LevelUvEdit& edit, LevelUvResult* result, QString* error = nullptr);

// The UV Editor's drags. A face: dragging `point` with no anchor, or the
// anchor itself, moves the whole texture so the point lands on `to`; with
// another point as the anchor it turns the texture about the anchor so the
// point heads towards `to`, in steps of `angleStep` degrees when that is
// above 0.
LevelUvTransform levelUvFaceDrag(const QVector<QPointF>& uv, int point, int anchor, QPointF to, double angleStep = 0);
// A patch: each of `selected` moves by `to` minus where `point` started.
QVector<QPointF> levelUvPatchDrag(const QVector<QPointF>& uv, const QVector<int>& selected, int point, QPointF to);
// A patch: `selected` (every point when empty) taken through `transform`.
QVector<QPointF> levelUvPatchTransformed(const QVector<QPointF>& uv, const QVector<int>& selected, const LevelUvTransform& transform);

// Prepares the edit as a plan that commitLevelSurfaceEdit() makes one undo
// step ("Edit texture coordinates"). Refuses a target that changed since
// `layout` was read, and those levelUvTargetProblem() names; a locked layer
// or group also refuses on commit. An edit that changes nothing gives a
// ready plan that commits nothing.
bool prepareLevelUvEdit(const LevelMapDocument& document, const LevelUvLayout& layout, const LevelUvEdit& edit, LevelSurfaceEditPlan* plan,
	QString* error = nullptr, LevelUvFit* fit = nullptr);

} // namespace vibestudio
