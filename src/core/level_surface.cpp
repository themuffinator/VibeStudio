#include "core/level_surface.h"
#include "core/level_patch_tools.h"
#include "core/level_texture_mapping.h"
#include "core/map_geometry.h"
#include <QCoreApplication>
#include <QMap>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio
{
namespace
{
bool fail(QString *error, const QString &message)
{
	if (error) {
		*error = message;
	}
	return false;
}
struct SurfaceText {
	Q_DECLARE_TR_FUNCTIONS(LevelSurface)
};
QString materialKey(QString name) { return name.trimmed().replace('\\', '/').toCaseFolded(); }
double rounded(double value)
{
	return std::abs(value - std::round(value)) < 1e-6 ? std::round(value) : QString::number(value, 'f', 6).toDouble();
}
bool sameMapping(const LevelMapBrushFace &a, const LevelMapBrushFace &b)
{
	const auto pa = levelTextureProjection(a), pb = levelTextureProjection(b);
	const auto same = [](double x, double y) { return std::abs(x - y) <= 1e-12; };
	return same(pa.u.x, pb.u.x) && same(pa.u.y, pb.u.y) && same(pa.u.z, pb.u.z) && same(pa.v.x, pb.v.x) && same(pa.v.y, pb.v.y) &&
		   same(pa.v.z, pb.v.z) && same(pa.offsetU, pb.offsetU) && same(pa.offsetV, pb.offsetV);
}
void offset(LevelMapBrushFace *face, double x, double y)
{
	if (face->explicitTextureMatrix) {
		face->textureMatrix[2] += x;
		face->textureMatrix[5] += y;
	} else if (face->explicitTextureAxes) {
		face->uOffset += x;
		face->vOffset += y;
	} else {
		face->shiftX += x;
		face->shiftY += y;
	}
}
void multiply(LevelMapBrushFace *face, double x, double y)
{
	if (face->explicitTextureMatrix) {
		for (int i = 0; i < 3; ++i) {
			face->textureMatrix[i] *= x;
			face->textureMatrix[i + 3] *= y;
		}
	} else {
		face->scaleX = (face->scaleX == 0 ? 1 : face->scaleX) / x;
		face->scaleY = (face->scaleY == 0 ? 1 : face->scaleY) / y;
		if (face->explicitTextureAxes) {
			face->uOffset *= x;
			face->vOffset *= y;
		} else {
			face->shiftX *= x;
			face->shiftY *= y;
		}
	}
}
// NetRadiant Custom's texture turn for each face format, about the texture
// origin: offsets stay, so the UV where the world origin projects stays put.
// `radiantAngle` keeps a stored rotation in that editor's range (fmod by
// 360); otherwise it wraps to -180..180, as the centre turn writes it.
// Workflow reference: NRC radiant/brush_primit.cpp Texdef_Rotate,
// Valve220_rotate and BPTexdef_Rotate at 449778b3 (GPL-2.0-or-later).
// Original implementation; see docs/CREDITS.md.
bool turn(LevelMapBrushFace *face, double degrees, bool radiantAngle, QString *error)
{
	const auto trig = rotateLevelVector({1, 0, 0, true}, 2, degrees); // cos, sin
	if (face->explicitTextureMatrix) {
		// The rows map the plane's own coordinates; turning those turns the
		// texture rigidly on the face, whatever the image's proportions.
		for (int row : {0, 3}) {
			const double s = face->textureMatrix[row], t = face->textureMatrix[row + 1];
			face->textureMatrix[row] = s * trig.x + t * trig.y;
			face->textureMatrix[row + 1] = t * trig.x - s * trig.y;
		}
		return true;
	}
	if (face->explicitTextureAxes) {
		// About the axes' own normal by the opposite angle, which reads as the
		// classic rotation it adds to; the projection direction is kept.
		const LevelMapVec3 u = face->uAxis, v = face->vAxis;
		LevelMapVec3 k{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x, true};
		const double length = std::sqrt(k.x * k.x + k.y * k.y + k.z * k.z);
		if (!std::isfinite(length) || length < 1e-12) {
			return fail(error, SurfaceText::tr("The face's Valve 220 texture axes are parallel. Reset its mapping before turning it."));
		}
		k = {k.x / length, k.y / length, k.z / length, true};
		const double c = trig.x, s = -trig.y;
		const auto rotate = [&k, c, s](const LevelMapVec3 &a) {
			const double along = k.x * a.x + k.y * a.y + k.z * a.z;
			return LevelMapVec3{a.x * c + (k.y * a.z - k.z * a.y) * s + k.x * along * (1 - c),
								a.y * c + (k.z * a.x - k.x * a.z) * s + k.y * along * (1 - c),
								a.z * c + (k.x * a.y - k.y * a.x) * s + k.z * along * (1 - c), true};
		};
		face->uAxis = rotate(u);
		face->vAxis = rotate(v);
	}
	face->rotation = radiantAngle ? std::fmod(face->rotation + degrees, 360.0) : std::remainder(face->rotation + degrees, 360.0);
	return true;
}
// NetRadiant Custom's additive scale step about the texture origin. A
// brush-primitive matrix stores no scale; as that editor does, read one from
// each row's length in texels and rescale the row by the old scale over the
// new one, keeping its offset.
// Workflow reference: NRC radiant/brush_primit.cpp Texdef_Scale and
// BPTexdef_Scale at 449778b3 (GPL-2.0-or-later). Original implementation;
// see docs/CREDITS.md.
bool radiantScale(LevelMapBrushFace *face, double x, double y, QSize imageSize, QString *error)
{
	if (!face->explicitTextureMatrix) {
		face->scaleX = levelSurfaceRadiantScaleStep(face->scaleX, x);
		face->scaleY = levelSurfaceRadiantScaleStep(face->scaleY, y);
		return true;
	}
	const auto row = [face](int first, double texels, bool negative, double increment) {
		if (increment == 0) {
			return true;
		}
		const double length = std::hypot(face->textureMatrix[first], face->textureMatrix[first + 1]) * texels;
		if (!std::isfinite(length) || length < 1e-12) {
			return false;
		}
		const double scale = negative ? -1 / length : 1 / length;
		const double factor = std::abs(scale + increment) > 1e-5 ? scale / (scale + increment) : -1.0;
		face->textureMatrix[first] *= factor;
		face->textureMatrix[first + 1] *= factor;
		return true;
	};
	if (!row(0, imageSize.width(), face->textureMatrix[0] < 0, x) || !row(3, imageSize.height(), face->textureMatrix[4] < 0, y)) {
		return fail(error, SurfaceText::tr("The face's texture matrix is collapsed. Reset its mapping before scaling it."));
	}
	return true;
}
bool writable(LevelMapBrushFace *face)
{
	// Match the map writer's precision before checking for a collapsed UV map.
	const auto number = [](double &n) {
		if (!std::isfinite(n) || std::abs(n) > 1e6) {
			return false;
		}
		n = rounded(n);
		return true;
	};
	if (face->explicitTextureMatrix) {
		for (double &n : face->textureMatrix) {
			if (!number(n)) {
				return false;
			}
		}
	} else {
		if (!number(face->scaleX) || !number(face->scaleY) || face->scaleX == 0 || face->scaleY == 0 || !number(face->rotation)) {
			return false;
		}
		if (face->explicitTextureAxes) {
			for (double *n : {&face->uAxis.x, &face->uAxis.y, &face->uAxis.z, &face->vAxis.x, &face->vAxis.y, &face->vAxis.z,
							  &face->uOffset, &face->vOffset}) {
				if (!number(*n)) {
					return false;
				}
			}
		} else if (!number(face->shiftX) || !number(face->shiftY)) {
			return false;
		}
	}
	const auto p = levelTextureProjection(*face);
	const double crossX = p.u.y * p.v.z - p.u.z * p.v.y, crossY = p.u.z * p.v.x - p.u.x * p.v.z, crossZ = p.u.x * p.v.y - p.u.y * p.v.x;
	const auto plane = face->explicitPlane ? MapPlane{face->planeNormal.x, face->planeNormal.y, face->planeNormal.z, 0, true}
										   : planeFromPoints(face->p0, face->p1, face->p2);
	const double area = std::abs(crossX * plane.normalX + crossY * plane.normalY + crossZ * plane.normalZ);
	return p.valid && std::isfinite(area) && area > 1e-14;
}
bool editFace(LevelMapBrushFace *face, const MapFacePolygon &polygon, const LevelSurfaceRequest &request, QSize imageSize, QString *error)
{
	const auto original = levelTextureProjection(*face);
	if (!original.valid || !polygon.isValid()) {
		return fail(error, SurfaceText::tr("The face has no valid winding or texture projection."));
	}
	const bool turning = request.operation == LevelSurfaceOperation::Rotate || request.operation == LevelSurfaceOperation::RadiantRotate;
	if ((request.operation == LevelSurfaceOperation::Shift && request.x == 0 && request.y == 0) ||
		(request.operation == LevelSurfaceOperation::Scale && request.x == 1 && request.y == 1) ||
		(request.operation == LevelSurfaceOperation::RadiantScale && request.x == 0 && request.y == 0) ||
		(turning && std::remainder(request.degrees, 360.0) == 0) ||
		(request.operation == LevelSurfaceOperation::Align && request.alignU == LevelSurfaceAlignment::Keep &&
		 request.alignV == LevelSurfaceAlignment::Keep)) {
		return true;
	}
	const bool matrix = face->explicitTextureMatrix;
	// A matrix turns its plane coordinates, so only a patch turn needs texels.
	const bool needsSize = levelSurfaceOperationNeedsSize(request.operation, matrix) && !(matrix && turning);
	if (needsSize && (imageSize.width() <= 0 || imageSize.height() <= 0)) {
		return fail(error, SurfaceText::tr("Material %1 needs a known image size. Open its package or provide an explicit texture size.")
							   .arg(face->textureName));
	}
	if (request.operation == LevelSurfaceOperation::Natural || request.operation == LevelSurfaceOperation::FaceAligned) {
		// A reset mapping, in the face's own dialect. Classic faces take the
		// natural one as plain numbers; a face-aligned slope may need axes.
		const auto projection = request.operation == LevelSurfaceOperation::Natural
			? levelNaturalTextureProjection(*face, request.x, request.y, imageSize)
			: levelFaceAlignedTextureProjection(*face, request.x, request.y, imageSize);
		if (!projection.valid) {
			return fail(error, SurfaceText::tr("The face's plane has no projection to reset to."));
		}
		if (!face->explicitTextureMatrix && !face->explicitTextureAxes && request.operation == LevelSurfaceOperation::Natural) {
			face->shiftX = 0;
			face->shiftY = 0;
			face->rotation = 0;
			face->scaleX = request.x;
			face->scaleY = request.y;
		} else {
			QString problem;
			if (!setLevelTextureProjection(face, projection, false, &problem)) {
				return fail(error, face->explicitTextureAxes || face->explicitTextureMatrix
					? problem
					: SurfaceText::tr("Classic faces cannot turn a texture into a slope's plane; use Natural, or convert the map to Valve 220."));
			}
			if (face->explicitTextureAxes) {
				// Valve 220 keeps the scale apart from its axes, as editors write it.
				face->uAxis = {face->uAxis.x * request.x, face->uAxis.y * request.x, face->uAxis.z * request.x, true};
				face->vAxis = {face->vAxis.x * request.y, face->vAxis.y * request.y, face->vAxis.z * request.y, true};
				face->scaleX = request.x;
				face->scaleY = request.y;
			}
		}
		if (!writable(face)) {
			return fail(error, SurfaceText::tr("The result is too large, too small or collapsed at map precision. Adjust the surface values."));
		}
		return true;
	}
	const double width = matrix ? 1 : imageSize.width(), height = matrix ? 1 : imageSize.height();
	QPointF minimum(std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
	QPointF maximum(-minimum.x(), -minimum.y());
	LevelMapVec3 center{0, 0, 0, true};
	for (const auto &point : polygon.points) {
		const auto uv = original.at(point);
		minimum.setX(std::min(minimum.x(), uv.x()));
		minimum.setY(std::min(minimum.y(), uv.y()));
		maximum.setX(std::max(maximum.x(), uv.x()));
		maximum.setY(std::max(maximum.y(), uv.y()));
		center.x += point.x / polygon.points.size();
		center.y += point.y / polygon.points.size();
		center.z += point.z / polygon.points.size();
	}
	const auto anchor = original.at(center);
	if (request.operation == LevelSurfaceOperation::Shift) {
		offset(face, request.x / (matrix ? imageSize.width() : 1), request.y / (matrix ? imageSize.height() : 1));
	} else if (request.operation == LevelSurfaceOperation::Scale) {
		multiply(face, 1 / request.x, 1 / request.y);
		const auto moved = levelTextureProjection(*face).at(center);
		offset(face, anchor.x() - moved.x(), anchor.y() - moved.y());
	} else if (request.operation == LevelSurfaceOperation::Rotate) {
		// The Radiant turn, then the offset that keeps the face centre's UV, so
		// both anchors turn every face format the same way round.
		if (!turn(face, request.degrees, false, error)) {
			return false;
		}
		const auto moved = levelTextureProjection(*face).at(center);
		offset(face, anchor.x() - moved.x(), anchor.y() - moved.y());
	} else if (request.operation == LevelSurfaceOperation::RadiantRotate) {
		if (!turn(face, request.degrees, true, error)) {
			return false;
		}
	} else if (request.operation == LevelSurfaceOperation::RadiantScale) {
		if (!radiantScale(face, request.x, request.y, imageSize, error)) {
			return false;
		}
	} else if (request.operation == LevelSurfaceOperation::Fit) {
		// A repeat count of zero keeps that axis as it is.
		const auto extent = maximum - minimum;
		if ((request.x > 0 && extent.x() <= 1e-10) || (request.y > 0 && extent.y() <= 1e-10)) {
			return fail(error, SurfaceText::tr("The face's texture coordinates are collapsed; set a valid projection before fitting."));
		}
		const double x = request.x > 0 ? request.x * width / extent.x() : 1.0, y = request.y > 0 ? request.y * height / extent.y() : 1.0;
		multiply(face, x, y);
		offset(face, request.x > 0 ? -minimum.x() * x : 0.0, request.y > 0 ? -minimum.y() * y : 0.0);
	} else {
		const auto delta = [](LevelSurfaceAlignment mode, double lo, double hi, double size) {
			if (mode == LevelSurfaceAlignment::Minimum) {
				return -lo;
			}
			if (mode == LevelSurfaceAlignment::Maximum) {
				return size - hi;
			}
			if (mode == LevelSurfaceAlignment::Center) {
				return (size - lo - hi) / 2;
			}
			return 0.0;
		};
		offset(face, delta(request.alignU, minimum.x(), maximum.x(), width), delta(request.alignV, minimum.y(), maximum.y(), height));
	}
	if (!writable(face)) {
		return fail(error, SurfaceText::tr("The result is too large, too small or collapsed at map precision. Adjust the surface values."));
	}
	return true;
}

// The same adjustments on a patch's control-point coordinates, which count
// repeats. Rotation and scaling keep the middle of the patch's coordinates
// where it is, as they keep a face's centre.
bool editPatch(LevelMapPatch *patch, const LevelSurfaceRequest &request, QSize imageSize, QString *error)
{
	const int count = static_cast<int>(patch->controlPoints.size());
	if (count == 0 || patch->controlU.size() != count || patch->controlV.size() != count) {
		return fail(error, SurfaceText::tr("The patch has no texture coordinates."));
	}
	const bool turning = request.operation == LevelSurfaceOperation::Rotate || request.operation == LevelSurfaceOperation::RadiantRotate;
	if ((request.operation == LevelSurfaceOperation::Shift && request.x == 0 && request.y == 0) ||
		(request.operation == LevelSurfaceOperation::Scale && request.x == 1 && request.y == 1) ||
		(request.operation == LevelSurfaceOperation::RadiantScale && request.x == 0 && request.y == 0) ||
		(turning && std::remainder(request.degrees, 360.0) == 0) ||
		(request.operation == LevelSurfaceOperation::Align && request.alignU == LevelSurfaceAlignment::Keep &&
		 request.alignV == LevelSurfaceAlignment::Keep)) {
		return true;
	}
	// A Radiant scale step multiplies coordinates, so it needs no image.
	if (levelSurfaceOperationNeedsSize(request.operation, true) && request.operation != LevelSurfaceOperation::RadiantScale
		&& (imageSize.width() <= 0 || imageSize.height() <= 0)) {
		return fail(error, SurfaceText::tr("Material %1 needs a known image size. Open its package or provide an explicit texture size.")
							   .arg(patch->textureName));
	}
	const double width = imageSize.width(), height = imageSize.height();
	double minU = std::numeric_limits<double>::max(), minV = minU, maxU = -minU, maxV = -minU;
	for (int i = 0; i < count; ++i) {
		minU = std::min(minU, patch->controlU[i]);
		maxU = std::max(maxU, patch->controlU[i]);
		minV = std::min(minV, patch->controlV[i]);
		maxV = std::max(maxV, patch->controlV[i]);
	}
	const double centreU = (minU + maxU) / 2, centreV = (minV + maxV) / 2;
	QString problem;
	switch (request.operation) {
	case LevelSurfaceOperation::Shift:
		for (int i = 0; i < count; ++i) {
			patch->controlU[i] += request.x / width;
			patch->controlV[i] += request.y / height;
		}
		break;
	case LevelSurfaceOperation::Scale:
		for (int i = 0; i < count; ++i) {
			patch->controlU[i] = centreU + (patch->controlU[i] - centreU) / request.x;
			patch->controlV[i] = centreV + (patch->controlV[i] - centreV) / request.y;
		}
		break;
	case LevelSurfaceOperation::Rotate:
	case LevelSurfaceOperation::RadiantRotate: {
		// In texels, so a non-square image keeps its proportions; the same
		// turn a classic face's rotation makes, as Radiant turns patches
		// (NRC Patch::RotateTexture, about coordinate 0,0 for RadiantRotate).
		const bool origin = request.operation == LevelSurfaceOperation::RadiantRotate;
		const double pivotU = origin ? 0.0 : centreU, pivotV = origin ? 0.0 : centreV;
		const auto trig = rotateLevelVector({1, 0, 0, true}, 2, request.degrees);
		for (int i = 0; i < count; ++i) {
			const double u = (patch->controlU[i] - pivotU) * width, v = (patch->controlV[i] - pivotV) * height;
			patch->controlU[i] = pivotU + (trig.x * u - trig.y * v) / width;
			patch->controlV[i] = pivotV + (trig.y * u + trig.x * v) / height;
		}
		break;
	}
	case LevelSurfaceOperation::RadiantScale: {
		// A patch stores no scale. NetRadiant Custom (Patch_convertScale at
		// 449778b3, GPL-2.0-or-later) multiplies its coordinates by a positive
		// step and divides them by a negative one's size, about 0,0.
		const auto factor = [](double step) { return step > 0 ? step : step < 0 ? -1 / step : 1.0; };
		const double factorU = factor(request.x), factorV = factor(request.y);
		for (int i = 0; i < count; ++i) {
			patch->controlU[i] *= factorU;
			patch->controlV[i] *= factorV;
		}
		break;
	}
	case LevelSurfaceOperation::Fit: {
		if ((request.x > 0 && maxU - minU <= 1e-10) || (request.y > 0 && maxV - minV <= 1e-10)) {
			return fail(error, SurfaceText::tr("The patch's texture coordinates are collapsed; reset them before fitting."));
		}
		for (int i = 0; i < count; ++i) {
			if (request.x > 0) {
				patch->controlU[i] = (patch->controlU[i] - minU) * request.x / (maxU - minU);
			}
			if (request.y > 0) {
				patch->controlV[i] = (patch->controlV[i] - minV) * request.y / (maxV - minV);
			}
		}
		break;
	}
	case LevelSurfaceOperation::Align: {
		const auto delta = [](LevelSurfaceAlignment mode, double lo, double hi) {
			return mode == LevelSurfaceAlignment::Minimum ? -lo
				: mode == LevelSurfaceAlignment::Maximum ? 1.0 - hi
				: mode == LevelSurfaceAlignment::Center ? (1.0 - lo - hi) / 2 : 0.0;
		};
		const double du = delta(request.alignU, minU, maxU), dv = delta(request.alignV, minV, maxV);
		for (int i = 0; i < count; ++i) {
			patch->controlU[i] += du;
			patch->controlV[i] += dv;
		}
		break;
	}
	case LevelSurfaceOperation::Natural:
		if (!naturalLevelPatchTexture(patch, width * request.x, height * request.y, &problem)) {
			return fail(error, problem);
		}
		break;
	case LevelSurfaceOperation::FaceAligned:
		if (!planarLevelPatchTexture(patch, width * request.x, height * request.y, &problem)) {
			return fail(error, problem);
		}
		break;
	}
	for (int i = 0; i < count; ++i) {
		// The map writer keeps seventeen digits; clean values stay clean.
		patch->controlU[i] = rounded(patch->controlU[i]);
		patch->controlV[i] = rounded(patch->controlV[i]);
	}
	if (!validateLevelPatch(*patch, &problem)) {
		return fail(error, problem);
	}
	return true;
}
} // namespace

bool levelSurfaceOperationNeedsSize(LevelSurfaceOperation operation, bool repeatsCoordinates)
{
	if (repeatsCoordinates) {
		return operation == LevelSurfaceOperation::Shift || operation == LevelSurfaceOperation::Rotate || operation == LevelSurfaceOperation::Natural
			|| operation == LevelSurfaceOperation::FaceAligned || operation == LevelSurfaceOperation::RadiantRotate
			|| operation == LevelSurfaceOperation::RadiantScale;
	}
	return operation == LevelSurfaceOperation::Fit || operation == LevelSurfaceOperation::Align;
}

double levelSurfaceDefaultScale(const LevelMapDocument& document)
{
	return document.format == LevelMapFormat::Quake3Map ? 0.5 : 1.0;
}

double levelSurfaceRadiantScaleStep(double value, double increment)
{
	if (increment == 0) {
		return value;
	}
	const double scale = value == 0 ? 1.0 : value;
	return std::abs(scale + increment) > 1e-5 ? scale + increment : -scale;
}

QVector<LevelSurfaceFace> levelMapSelectedSurfaces(const LevelMapDocument &document)
{
	QSet<int> brushes, entities;
	for (const auto &ref : document.selection) {
		if (ref.kind == LevelMapSelectionKind::QuakeBrush) {
			brushes.insert(ref.objectId);
		}
		if (ref.kind == LevelMapSelectionKind::Entity) {
			entities.insert(ref.objectId);
		}
	}
	QVector<LevelSurfaceFace> result;
	for (const auto &brush : document.brushes) {
		if (brushes.contains(brush.id) || entities.contains(brush.entityId)) {
			for (int i = 0; i < brush.faces.size(); ++i) {
				result.append({brush.id, i});
			}
		}
	}
	return result;
}

bool prepareLevelSurfaceEdit(const LevelMapDocument &document, const QVector<LevelSurfaceFace> &faces, const LevelSurfaceRequest &request,
							 const QHash<QString, QSize> &textureSizes, LevelSurfaceEditPlan *plan, QString *error,
							 const std::function<bool()> &cancelled)
{
	return prepareLevelSurfaceEdits(document, faces, {request}, textureSizes, plan, error, cancelled);
}

bool prepareLevelSurfaceEdits(const LevelMapDocument &document, const QVector<LevelSurfaceFace> &faces,
							  const QVector<LevelSurfaceRequest> &requests, const QHash<QString, QSize> &textureSizes,
							  LevelSurfaceEditPlan *plan, QString *error, const std::function<bool()> &cancelled)
{
	return prepareLevelSurfaceEdits(document, faces, {}, requests, textureSizes, plan, error, cancelled);
}

bool prepareLevelSurfaceEdits(const LevelMapDocument &document, const QVector<LevelSurfaceFace> &faces, const QVector<int> &patches,
							  const QVector<LevelSurfaceRequest> &requests, const QHash<QString, QSize> &textureSizes,
							  LevelSurfaceEditPlan *plan, QString *error, const std::function<bool()> &cancelled)
{
	if (error) {
		error->clear();
	}
	if (!plan) {
		return fail(error, SurfaceText::tr("Missing surface edit plan."));
	}
	*plan = {};
	if (document.format != LevelMapFormat::QuakeMap && document.format != LevelMapFormat::Quake3Map) {
		return fail(error, SurfaceText::tr("Surface alignment requires a Quake-family text map."));
	}
	if (patches.isEmpty() && (faces.isEmpty() || faces.size() > 16384)) {
		return fail(error, SurfaceText::tr("Select between 1 and 16,384 brush faces for surface alignment."));
	}
	if (faces.size() > 16384 || patches.size() > 256) {
		return fail(error, SurfaceText::tr("Select at most 16,384 brush faces and 256 patches for surface alignment."));
	}
	if (requests.isEmpty() || requests.size() > 64) {
		return fail(error, SurfaceText::tr("A surface batch needs between 1 and 64 adjustments."));
	}
	for (const auto &request : requests) {
		const bool reset = request.operation == LevelSurfaceOperation::Natural || request.operation == LevelSurfaceOperation::FaceAligned;
		if (!std::isfinite(request.x) || !std::isfinite(request.y) || !std::isfinite(request.degrees) || std::abs(request.x) > 1e6 ||
			std::abs(request.y) > 1e6 || std::abs(request.degrees) > 360000 ||
			(request.operation == LevelSurfaceOperation::Scale && (std::abs(request.x) < 1e-6 || std::abs(request.y) < 1e-6)) ||
			(request.operation == LevelSurfaceOperation::Fit &&
			 (request.x < 0 || request.y < 0 || (request.x < 1e-6 && request.y < 1e-6) || (request.x > 0 && request.x < 1e-6) ||
			  (request.y > 0 && request.y < 1e-6))) ||
			(reset && (request.x < 1e-6 || request.y < 1e-6)) ||
			request.operation < LevelSurfaceOperation::Shift || request.operation > LevelSurfaceOperation::RadiantRotate ||
			request.alignU < LevelSurfaceAlignment::Keep || request.alignU > LevelSurfaceAlignment::Maximum ||
			request.alignV < LevelSurfaceAlignment::Keep || request.alignV > LevelSurfaceAlignment::Maximum) {
			return fail(error, SurfaceText::tr(
								   "Surface values must be finite and in range. Fit repeats must be positive; scale factors cannot be zero."));
		}
	}
	QHash<QString, QSize> sizes;
	for (auto it = textureSizes.cbegin(); it != textureSizes.cend(); ++it) {
		const auto key = materialKey(it.key());
		if (sizes.contains(key) && sizes.value(key) != it.value()) {
			return fail(error, SurfaceText::tr("Conflicting image sizes were supplied for %1.").arg(it.key()));
		}
		sizes.insert(key, it.value());
	}
	QMap<int, QSet<int>> targets;
	for (const auto &face : faces) {
		targets[face.brushId].insert(face.faceIndex);
	}
	QHash<int, const LevelMapBrush *> brushes;
	for (const auto &brush : document.brushes) {
		if (cancelled && cancelled()) { return fail(error, SurfaceText::tr("Surface preview cancelled.")); }
		if (targets.contains(brush.id)) { brushes.insert(brush.id, &brush); }
	}
	LevelSurfaceEditPlan result;
	result.m_sourcePath = document.sourcePath;
	result.m_sourceHash = document.sourceContentHash;
	result.m_revision = document.revision;
	result.m_format = document.format;
	for (auto it = targets.cbegin(); it != targets.cend(); ++it) {
		if (cancelled && cancelled()) {
			return fail(error, SurfaceText::tr("Surface preview cancelled."));
		}
		const auto *found = brushes.value(it.key());
		if (!found || found->faces.size() > 128) {
			return fail(error, SurfaceText::tr("Brush %1 is missing or exceeds the 128-face alignment limit.").arg(it.key()));
		}
		const auto geometry = solveBrushGeometry(found->faces, found->id, found->entityId);
		if (!geometry.solved) {
			return fail(error, SurfaceText::tr("Brush %1 must enclose a valid volume before aligning its surfaces.").arg(it.key()));
		}
		auto after = *found;
		int changed = 0;
		for (int index : it.value()) {
			if (cancelled && cancelled()) {
				return fail(error, SurfaceText::tr("Surface preview cancelled."));
			}
			if (index < 0 || index >= after.faces.size() || index >= geometry.faces.size()) {
				return fail(error, SurfaceText::tr("Brush %1 has no face %2.").arg(it.key()).arg(index + 1));
			}
			auto &face = after.faces[index];
			QString problem;
			for (const auto &request : requests) {
				if (cancelled && cancelled()) { return fail(error, SurfaceText::tr("Surface preview cancelled.")); }
				if (!editFace(&face, geometry.faces[index], request, sizes.value(materialKey(face.textureName)), &problem)) {
					return fail(error, SurfaceText::tr("Brush %1, face %2: %3").arg(it.key()).arg(index + 1).arg(problem));
				}
			}
			if (!sameMapping(found->faces[index], face)) {
				face.textureParametersDirty = true;
				++changed;
			} else {
				face = found->faces[index];
			}
		}
		if (changed > 0) {
			after.textureParametersDirty = true;
			result.m_before.append(*found);
			result.m_after.append(after);
			result.m_faceCount += changed;
		}
	}
	QSet<int> wanted(patches.cbegin(), patches.cend());
	for (const auto &patch : document.patches) {
		if (cancelled && cancelled()) {
			return fail(error, SurfaceText::tr("Surface preview cancelled."));
		}
		if (!wanted.remove(patch.id)) {
			continue;
		}
		auto after = patch;
		for (const auto &request : requests) {
			QString problem;
			if (!editPatch(&after, request, sizes.value(materialKey(patch.textureName)), &problem)) {
				return fail(error, SurfaceText::tr("Patch %1: %2").arg(patch.id).arg(problem));
			}
		}
		if (after.controlU != patch.controlU || after.controlV != patch.controlV) {
			after.definitionDirty = after.geometryDirty = true;
			result.m_patchBefore.append(patch);
			result.m_patchAfter.append(after);
		}
	}
	if (!wanted.isEmpty()) {
		return fail(error, SurfaceText::tr("Patch %1 no longer exists.").arg(*wanted.cbegin()));
	}
	result.m_ready = true;
	*plan = std::move(result);
	return true;
}
} // namespace vibestudio
