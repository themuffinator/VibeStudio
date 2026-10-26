// The UV Editor's service: one brush face's winding or one patch's control
// grid in texture space, and the edits its drags, turns and nudges make.
// Workflow reference: NetRadiant Custom's TexTool plugin
// (plugins/textool/TexTool.cpp, ControlPointsManager.cpp and 2DView.cpp at
// 449778b3, GPL-2.0-or-later), which drags points in texture space and turns
// a face about an anchor point. Original implementation, extended from
// brush primitives to every face format and patches; see docs/CREDITS.md.

#include "core/level_uv_edit.h"

#include "core/level_patch.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_texture_mapping.h"
#include "core/level_visibility.h"
#include "core/map_geometry.h"

#include <QCoreApplication>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>

namespace vibestudio {
namespace {

struct Text {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioLevelUvEdit)
};

bool fail(QString* error, const QString& message)
{
	if (error) {
		*error = message;
	}
	return false;
}

// Texel coordinates beyond this many texels are refused, as are stored
// coordinates beyond a million repeats.
constexpr double kTexelLimit = 1e9;
constexpr double kRepeatLimit = 1e6;

bool finitePoint(QPointF p)
{
	return std::isfinite(p.x()) && std::isfinite(p.y());
}

bool samePoint(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return a.valid == b.valid && a.x == b.x && a.y == b.y && a.z == b.z;
}

LevelMapVec3 scaled(const LevelMapVec3& v, double s)
{
	return {v.x * s, v.y * s, v.z * s, v.valid};
}

// Every field the UV Editor reads or writes back, so an edit never carries a
// stale value over a change made elsewhere.
bool sameFace(const LevelMapBrushFace& a, const LevelMapBrushFace& b)
{
	return a.line == b.line && a.textureName == b.textureName && samePoint(a.p0, b.p0) && samePoint(a.p1, b.p1) && samePoint(a.p2, b.p2)
		&& a.explicitPlane == b.explicitPlane && samePoint(a.planeNormal, b.planeNormal) && a.planeDistance == b.planeDistance
		&& a.explicitTextureMatrix == b.explicitTextureMatrix && a.textureMatrix == b.textureMatrix && a.explicitTextureAxes == b.explicitTextureAxes
		&& samePoint(a.uAxis, b.uAxis) && samePoint(a.vAxis, b.vAxis) && a.uOffset == b.uOffset && a.vOffset == b.vOffset && a.shiftX == b.shiftX
		&& a.shiftY == b.shiftY && a.scaleX == b.scaleX && a.scaleY == b.scaleY && a.rotation == b.rotation && a.contentFlags == b.contentFlags
		&& a.surfaceFlags == b.surfaceFlags && a.surfaceValue == b.surfaceValue;
}

bool sameMapping(const LevelMapBrushFace& a, const LevelMapBrushFace& b)
{
	const auto pa = levelTextureProjection(a), pb = levelTextureProjection(b);
	const auto same = [](double x, double y) { return std::abs(x - y) <= 1e-12 * std::max(1.0, std::max(std::abs(x), std::abs(y))); };
	return same(pa.u.x, pb.u.x) && same(pa.u.y, pb.u.y) && same(pa.u.z, pb.u.z) && same(pa.v.x, pb.v.x) && same(pa.v.y, pb.v.y)
		&& same(pa.v.z, pb.v.z) && same(pa.offsetU, pb.offsetU) && same(pa.offsetV, pb.offsetV);
}

const LevelMapBrush* findBrush(const LevelMapDocument& document, int id)
{
	const auto found = std::find_if(document.brushes.cbegin(), document.brushes.cend(), [id](const LevelMapBrush& brush) { return brush.id == id; });
	return found == document.brushes.cend() ? nullptr : &*found;
}

const LevelMapPatch* findPatch(const LevelMapDocument& document, int id)
{
	const auto found = std::find_if(document.patches.cbegin(), document.patches.cend(), [id](const LevelMapPatch& patch) { return patch.id == id; });
	return found == document.patches.cend() ? nullptr : &*found;
}

bool usableSize(QSize size)
{
	return size.width() > 0 && size.height() > 0 && size.width() <= 65536 && size.height() <= 65536;
}

// Texels per stored unit along U and V: the image size where coordinates
// count repeats, otherwise one.
QPointF texelScale(const LevelUvLayout& layout)
{
	return layout.countsRepeats() ? QPointF(layout.imageSize.width(), layout.imageSize.height()) : QPointF(1, 1);
}

QVector<QPointF> faceUv(const LevelMapBrushFace& face, const QVector<LevelMapVec3>& points, QPointF scale)
{
	const LevelTextureProjection projection = levelTextureProjection(face);
	QVector<QPointF> uv;
	uv.reserve(points.size());
	for (const LevelMapVec3& point : points) {
		const QPointF at = projection.at(point);
		uv.append({at.x() * scale.x(), at.y() * scale.y()});
	}
	return uv;
}

} // namespace

QPointF LevelUvTransform::map(QPointF point) const
{
	return {xx * point.x() + xy * point.y() + dx, yx * point.x() + yy * point.y() + dy};
}

LevelUvTransform LevelUvTransform::then(const LevelUvTransform& next) const
{
	LevelUvTransform result;
	result.xx = next.xx * xx + next.xy * yx;
	result.xy = next.xx * xy + next.xy * yy;
	result.yx = next.yx * xx + next.yy * yx;
	result.yy = next.yx * xy + next.yy * yy;
	result.dx = next.xx * dx + next.xy * dy + next.dx;
	result.dy = next.yx * dx + next.yy * dy + next.dy;
	return result;
}

bool LevelUvTransform::isFinite() const
{
	return std::isfinite(xx) && std::isfinite(xy) && std::isfinite(yx) && std::isfinite(yy) && std::isfinite(dx) && std::isfinite(dy);
}

LevelUvTransform LevelUvTransform::translation(QPointF delta)
{
	LevelUvTransform result;
	result.dx = delta.x();
	result.dy = delta.y();
	return result;
}

LevelUvTransform LevelUvTransform::rotation(QPointF pivot, double clockwiseDegrees)
{
	// rotateLevelVector() keeps quarter and half turns exact.
	const LevelMapVec3 trig = rotateLevelVector({1, 0, 0, true}, 2, clockwiseDegrees);
	const double c = trig.x, s = trig.y;
	LevelUvTransform result;
	if (!trig.valid) {
		result.xx = result.yy = std::numeric_limits<double>::quiet_NaN();
		return result;
	}
	// With t running down the view, (1, 0) turning towards (0, 1) reads
	// clockwise.
	result.xx = c;
	result.xy = -s;
	result.yx = s;
	result.yy = c;
	result.dx = pivot.x() - (c * pivot.x() - s * pivot.y());
	result.dy = pivot.y() - (s * pivot.x() + c * pivot.y());
	return result;
}

double levelUvTurnAngle(QPointF pivot, QPointF from, QPointF to)
{
	const QPointF a = from - pivot, b = to - pivot;
	if (!finitePoint(a) || !finitePoint(b) || std::hypot(a.x(), a.y()) < 1e-9 || std::hypot(b.x(), b.y()) < 1e-9) {
		return 0;
	}
	return std::atan2(a.x() * b.y() - a.y() * b.x(), a.x() * b.x() + a.y() * b.y()) * 180.0 / std::numbers::pi;
}

QPointF snapLevelUv(QPointF point, double step)
{
	if (!std::isfinite(step) || step <= 0 || !finitePoint(point)) {
		return point;
	}
	const auto snap = [step](double value) {
		const double snapped = std::round(value / step) * step;
		return snapped == 0 ? 0.0 : snapped;
	};
	return {snap(point.x()), snap(point.y())};
}

QPointF LevelUvLayout::centre() const
{
	if (uv.isEmpty()) {
		return {};
	}
	QPointF sum;
	for (const QPointF& point : uv) {
		sum += point;
	}
	return sum / static_cast<double>(uv.size());
}

QString levelUvTargetProblem(const LevelMapDocument& document, const LevelMaterialTarget& target)
{
	if (document.format != LevelMapFormat::QuakeMap && document.format != LevelMapFormat::Quake3Map) {
		return Text::tr("The UV Editor works on brush faces and patches. Open a Quake, Quake II or Quake III map.");
	}
	const QSet<QString> hidden = levelMapHiddenObjects(document);
	const QSet<QString> locked = levelSceneLockedObjects(document);
	std::optional<LevelSceneSelectionContext> context;
	if (!document.openSceneGroups.isEmpty()) {
		context = levelSceneSelectionContext(document);
	}
	const auto check = [&](const QString& self, int entityId, const QString& name) -> QString {
		const QString owner = levelMapSelectionRefId({LevelMapSelectionKind::Entity, entityId});
		if (hidden.contains(self) || hidden.contains(owner)) {
			return Text::tr("%1 is hidden. Show it before editing its texture coordinates.").arg(name);
		}
		if (locked.contains(self) || locked.contains(owner)) {
			return Text::tr("%1 is in a locked layer or group. Unlock it before editing its texture coordinates.").arg(name);
		}
		if (context && !context->openGroup.isEmpty() && !context->allowedObjects.contains(self)) {
			return Text::tr("%1 is outside the open group. Close the group before editing its texture coordinates.").arg(name);
		}
		return {};
	};
	if (target.kind == LevelMaterialKind::BrushFace) {
		const LevelMapBrush* brush = findBrush(document, target.objectId);
		if (!brush || target.faceIndex < 0 || target.faceIndex >= brush->faces.size()) {
			return Text::tr("Brush %1 has no face %2.").arg(target.objectId).arg(qint64(target.faceIndex) + 1);
		}
		const QString problem = check(levelMapSelectionRefId({LevelMapSelectionKind::QuakeBrush, brush->id}), brush->entityId,
			Text::tr("Brush %1").arg(brush->id));
		if (!problem.isEmpty()) {
			return problem;
		}
		const MapBrushGeometry geometry = solveBrushGeometry(brush->faces, brush->id, brush->entityId);
		if (!geometry.solved || target.faceIndex >= geometry.faces.size() || geometry.faces[target.faceIndex].points.size() < 3) {
			return Text::tr("Brush %1, face %2 has no area to map. Fix the brush's planes first.").arg(brush->id).arg(qint64(target.faceIndex) + 1);
		}
		return {};
	}
	if (target.kind == LevelMaterialKind::Patch) {
		const LevelMapPatch* patch = findPatch(document, target.objectId);
		if (!patch) {
			return Text::tr("Patch %1 no longer exists.").arg(target.objectId);
		}
		const QString problem = check(levelMapSelectionRefId({LevelMapSelectionKind::QuakePatch, patch->id}), patch->entityId,
			Text::tr("Patch %1").arg(patch->id));
		if (!problem.isEmpty()) {
			return problem;
		}
		QString invalid;
		if (!validateLevelPatch(*patch, &invalid)) {
			return invalid;
		}
		return {};
	}
	return Text::tr("Pick one brush face, or select one patch, to edit its texture coordinates.");
}

bool readLevelUvLayout(const LevelMapDocument& document, const LevelMaterialTarget& target, QSize imageSize, LevelUvLayout* layout, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!layout) {
		return fail(error, Text::tr("Missing UV layout."));
	}
	LevelUvLayout result;
	result.target = target;
	result.imageSize = imageSize;
	if (target.kind == LevelMaterialKind::BrushFace) {
		const LevelMapBrush* brush = findBrush(document, target.objectId);
		if (!brush || target.faceIndex < 0 || target.faceIndex >= brush->faces.size()) {
			return fail(error, Text::tr("Brush %1 has no face %2.").arg(target.objectId).arg(qint64(target.faceIndex) + 1));
		}
		const LevelMapBrushFace& face = brush->faces[target.faceIndex];
		const MapBrushGeometry geometry = solveBrushGeometry(brush->faces, brush->id, brush->entityId);
		if (!geometry.solved || target.faceIndex >= geometry.faces.size() || geometry.faces[target.faceIndex].points.size() < 3) {
			return fail(error,
				Text::tr("Brush %1, face %2 has no area to map. Fix the brush's planes first.").arg(brush->id).arg(qint64(target.faceIndex) + 1));
		}
		result.format = face.explicitTextureMatrix ? LevelUvFormat::BrushPrimitive
			: face.explicitTextureAxes			  ? LevelUvFormat::Valve220
												  : LevelUvFormat::Classic;
		if (result.format == LevelUvFormat::BrushPrimitive && !usableSize(imageSize)) {
			return fail(error, Text::tr("Brush-primitive faces count texture coordinates in repeats. Give the material's image size."));
		}
		if (!levelTextureProjection(face).valid) {
			return fail(error, Text::tr("Brush %1, face %2 has a texture mapping that is not finite. Reset it first.")
								   .arg(brush->id)
								   .arg(qint64(target.faceIndex) + 1));
		}
		result.material = face.textureName;
		result.face = face;
		result.points = geometry.faces[target.faceIndex].points;
		result.uv = faceUv(face, result.points, texelScale(result));
	} else if (target.kind == LevelMaterialKind::Patch) {
		const LevelMapPatch* patch = findPatch(document, target.objectId);
		if (!patch) {
			return fail(error, Text::tr("Patch %1 no longer exists.").arg(target.objectId));
		}
		if (!validateLevelPatch(*patch, error)) {
			return false;
		}
		if (!usableSize(imageSize)) {
			return fail(error, Text::tr("Patches count texture coordinates in repeats. Give the material's image size."));
		}
		result.format = LevelUvFormat::Patch;
		result.material = patch->textureName;
		result.patch = *patch;
		result.columns = patch->width;
		result.rows = patch->height;
		result.points = patch->controlPoints;
		for (qsizetype i = 0; i < patch->controlPoints.size(); ++i) {
			result.uv.append({patch->controlU.value(i) * imageSize.width(), patch->controlV.value(i) * imageSize.height()});
		}
	} else {
		return fail(error, Text::tr("Pick one brush face, or select one patch, to edit its texture coordinates."));
	}
	for (const QPointF& point : std::as_const(result.uv)) {
		if (!finitePoint(point)) {
			return fail(error, Text::tr("The surface's texture coordinates are not finite. Reset its mapping first."));
		}
	}
	*layout = std::move(result);
	return true;
}

bool applyLevelUvEdit(const LevelUvLayout& layout, const LevelUvEdit& edit, LevelUvResult* result, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!result) {
		return fail(error, Text::tr("Missing UV result."));
	}
	LevelUvResult out;
	out.face = layout.face;
	out.patch = layout.patch;
	out.uv = layout.uv;
	if (layout.isPatch()) {
		if (!edit.patchUv.isEmpty()) {
			if (edit.patchUv.size() != layout.uv.size() || layout.patch.controlU.size() != layout.uv.size()
				|| layout.patch.controlV.size() != layout.uv.size() || !usableSize(layout.imageSize)) {
				const int named = static_cast<int>(edit.patchUv.size());
				return fail(error, Text::tr("The edit names %n control point(s), but the patch has a different number.", nullptr, named));
			}
			const double width = layout.imageSize.width(), height = layout.imageSize.height();
			bool changed = false;
			for (qsizetype i = 0; i < edit.patchUv.size(); ++i) {
				const QPointF point = edit.patchUv[i];
				if (!finitePoint(point) || std::abs(point.x() / width) > kRepeatLimit || std::abs(point.y() / height) > kRepeatLimit) {
					return fail(error, Text::tr("Texture coordinates must be finite and within a million repeats."));
				}
				// Untouched points keep their stored numbers exactly.
				if (point == layout.uv[i]) {
					continue;
				}
				out.patch.controlU[i] = point.x() / width;
				out.patch.controlV[i] = point.y() / height;
				out.uv[i] = point;
				changed = true;
			}
			if (changed) {
				out.patch.definitionDirty = out.patch.geometryDirty = true;
				out.fit = LevelUvFit::Exact;
			}
		}
		*result = std::move(out);
		return true;
	}
	const LevelUvTransform& t = edit.transform;
	if (!t.isFinite()) {
		return fail(error, Text::tr("The texture transform must be finite."));
	}
	if (t.isIdentity()) {
		*result = std::move(out);
		return true;
	}
	const double determinant = t.xx * t.yy - t.xy * t.yx;
	const double size = std::max({std::abs(t.xx), std::abs(t.xy), std::abs(t.yx), std::abs(t.yy), 1e-300});
	if (std::abs(determinant) <= 1e-12 * size * size) {
		return fail(error, Text::tr("The texture transform collapses the texture onto a line."));
	}
	const QPointF unit = texelScale(layout);
	LevelMapBrushFace face = layout.face;
	if (t.isTranslation()) {
		// A move changes only the offsets, in each format's own units.
		const double x = t.dx / unit.x(), y = t.dy / unit.y();
		if (face.explicitTextureMatrix) {
			face.textureMatrix[2] += x;
			face.textureMatrix[5] += y;
		} else if (face.explicitTextureAxes) {
			face.uOffset += x;
			face.vOffset += y;
		} else {
			face.shiftX += x;
			face.shiftY += y;
		}
		out.fit = LevelUvFit::Exact;
	} else {
		const LevelTextureProjection before = levelTextureProjection(layout.face);
		if (!before.valid) {
			return fail(error, Text::tr("The face's texture mapping is not finite. Reset it first."));
		}
		// In texels, where the image's own proportions hold, then back.
		const LevelMapVec3 u = scaled(before.u, unit.x()), v = scaled(before.v, unit.y());
		const double offsetU = before.offsetU * unit.x(), offsetV = before.offsetV * unit.y();
		LevelTextureProjection after;
		after.u = scaled({t.xx * u.x + t.xy * v.x, t.xx * u.y + t.xy * v.y, t.xx * u.z + t.xy * v.z, true}, 1 / unit.x());
		after.v = scaled({t.yx * u.x + t.yy * v.x, t.yx * u.y + t.yy * v.y, t.yx * u.z + t.yy * v.z, true}, 1 / unit.y());
		after.offsetU = (t.xx * offsetU + t.xy * offsetV + t.dx) / unit.x();
		after.offsetV = (t.yx * offsetU + t.yy * offsetV + t.dy) / unit.y();
		after.normalizedCoordinates = before.normalizedCoordinates;
		after.valid = true;
		QString problem;
		switch (layout.format) {
		case LevelUvFormat::BrushPrimitive:
			if (!setLevelTextureProjection(&face, after, false, &problem)) {
				return fail(error, problem);
			}
			out.fit = LevelUvFit::Exact;
			break;
		case LevelUvFormat::Valve220: {
			// Keep the face's scale values: the axes carry the turn.
			const double scaleU = face.scaleX == 0 ? 1 : face.scaleX, scaleV = face.scaleY == 0 ? 1 : face.scaleY;
			face.uAxis = scaled(after.u, scaleU);
			face.vAxis = scaled(after.v, scaleV);
			face.uOffset = after.offsetU;
			face.vOffset = after.offsetV;
			out.fit = LevelUvFit::Exact;
			break;
		}
		case LevelUvFormat::Classic:
		case LevelUvFormat::Patch:
			if (setLevelTextureProjection(&face, after, false, &problem)) {
				out.fit = LevelUvFit::Exact;
			} else if (edit.allowValve220) {
				if (!setLevelTextureProjection(&face, after, true, &problem)) {
					return fail(error, problem);
				}
				out.fit = LevelUvFit::Converted;
			} else {
				// The nearest classic mapping, keeping the anchor where it
				// would go, or every point on average without one.
				QVector<QPair<LevelMapVec3, QPointF>> anchors;
				for (qsizetype i = 0; i < layout.points.size(); ++i) {
					if (edit.anchor < 0 || edit.anchor >= layout.points.size() || edit.anchor == i) {
						anchors.append({layout.points[i], t.map(layout.uv[i])});
					}
				}
				const LevelTextureProjection nearest = nearestClassicLevelTextureProjection(face, after, anchors);
				face = layout.face;
				if (!nearest.valid || !setLevelTextureProjection(&face, nearest, false, &problem)) {
					return fail(error, Text::tr("A classic face cannot hold this turn. Allow Valve 220 conversion to keep it exactly."));
				}
				out.fit = LevelUvFit::Approximated;
			}
			break;
		}
	}
	const QVector<QPointF> uv = faceUv(face, layout.points, unit);
	for (const QPointF& point : uv) {
		if (!finitePoint(point) || std::abs(point.x()) > kTexelLimit || std::abs(point.y()) > kTexelLimit
			|| std::abs(point.x() / unit.x()) > kRepeatLimit || std::abs(point.y() / unit.y()) > kRepeatLimit) {
			return fail(error, Text::tr("Texture coordinates must be finite and within a million repeats."));
		}
	}
	if (sameMapping(face, layout.face)) {
		*result = std::move(out);
		result->fit = LevelUvFit::Unchanged;
		return true;
	}
	face.textureParametersDirty = true;
	out.face = face;
	out.uv = uv;
	*result = std::move(out);
	return true;
}

LevelUvTransform levelUvFaceDrag(const QVector<QPointF>& uv, int point, int anchor, QPointF to, double angleStep)
{
	if (point < 0 || point >= uv.size() || !finitePoint(to)) {
		return {};
	}
	const QPointF start = uv[point];
	if (anchor < 0 || anchor >= uv.size() || anchor == point) {
		return LevelUvTransform::translation(to - start);
	}
	const QPointF pivot = uv[anchor];
	double angle = levelUvTurnAngle(pivot, start, to);
	if (std::isfinite(angleStep) && angleStep > 0) {
		angle = std::round(angle / angleStep) * angleStep;
	}
	return angle == 0 ? LevelUvTransform {} : LevelUvTransform::rotation(pivot, angle);
}

QVector<QPointF> levelUvPatchDrag(const QVector<QPointF>& uv, const QVector<int>& selected, int point, QPointF to)
{
	QVector<QPointF> result = uv;
	if (point < 0 || point >= uv.size() || !finitePoint(to)) {
		return result;
	}
	const QPointF delta = to - uv[point];
	QSet<int> moved(selected.cbegin(), selected.cend());
	moved.insert(point);
	for (const int index : std::as_const(moved)) {
		if (index >= 0 && index < result.size()) {
			result[index] = uv[index] + delta;
		}
	}
	return result;
}

QVector<QPointF> levelUvPatchTransformed(const QVector<QPointF>& uv, const QVector<int>& selected, const LevelUvTransform& transform)
{
	QVector<QPointF> result = uv;
	if (selected.isEmpty()) {
		for (QPointF& point : result) {
			point = transform.map(point);
		}
		return result;
	}
	const QSet<int> chosen(selected.cbegin(), selected.cend());
	for (const int index : chosen) {
		if (index >= 0 && index < result.size()) {
			result[index] = transform.map(uv[index]);
		}
	}
	return result;
}

bool prepareLevelUvEdit(const LevelMapDocument& document, const LevelUvLayout& layout, const LevelUvEdit& edit, LevelSurfaceEditPlan* plan,
	QString* error, LevelUvFit* fit)
{
	if (error) {
		error->clear();
	}
	if (!plan) {
		return fail(error, Text::tr("Missing UV edit plan."));
	}
	if (const QString problem = levelUvTargetProblem(document, layout.target); !problem.isEmpty()) {
		return fail(error, problem);
	}
	const QString changed = Text::tr("The surface changed after the UV Editor read it. Open the UV Editor again.");
	const LevelMapBrush* brush = nullptr;
	const LevelMapPatch* patch = nullptr;
	if (layout.isPatch()) {
		patch = findPatch(document, layout.target.objectId);
		if (!patch || patch->entityId != layout.patch.entityId || patch->textureName != layout.patch.textureName
			|| levelPatchDefinition(*patch) != levelPatchDefinition(layout.patch)) {
			return fail(error, changed);
		}
	} else {
		brush = findBrush(document, layout.target.objectId);
		if (!brush || layout.target.faceIndex < 0 || layout.target.faceIndex >= brush->faces.size()
			|| !sameFace(brush->faces[layout.target.faceIndex], layout.face)) {
			return fail(error, changed);
		}
		// The other planes also define this face's winding. Moving an adjacent
		// plane can invalidate its point numbers and rotation anchor without
		// changing the selected face's own stored plane or texture attributes.
		const MapBrushGeometry geometry = solveBrushGeometry(brush->faces, brush->id, brush->entityId);
		const auto& points = geometry.faces[layout.target.faceIndex].points;
		if (points.size() != layout.points.size() || !std::equal(points.cbegin(), points.cend(), layout.points.cbegin(), samePoint)) {
			return fail(error, changed);
		}
	}
	LevelUvResult result;
	if (!applyLevelUvEdit(layout, edit, &result, error)) {
		return false;
	}
	LevelSurfaceEditPlan out;
	out.m_sourcePath = document.sourcePath;
	out.m_sourceHash = document.sourceContentHash;
	out.m_revision = document.revision;
	out.m_format = document.format;
	out.m_uv = true;
	if (result.fit != LevelUvFit::Unchanged) {
		if (patch) {
			out.m_patchBefore.append(*patch);
			out.m_patchAfter.append(result.patch);
		} else {
			LevelMapBrush after = *brush;
			after.faces[layout.target.faceIndex] = result.face;
			after.textureParametersDirty = true;
			out.m_before.append(*brush);
			out.m_after.append(after);
			out.m_faceCount = 1;
			out.m_convertedFaces = result.fit == LevelUvFit::Converted ? 1 : 0;
		}
	}
	out.m_ready = true;
	*plan = std::move(out);
	if (fit) {
		*fit = result.fit;
	}
	return true;
}

} // namespace vibestudio
