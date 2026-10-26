#include "core/level_surface.h"
#include "core/level_camera_texture.h"
#include "core/level_patch.h"
#include "core/level_patch_tools.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_texture_mapping.h"
#include "core/level_visibility.h"
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
bool cameraOperation(LevelSurfaceOperation operation)
{
	return operation == LevelSurfaceOperation::CameraShift || operation == LevelSurfaceOperation::CameraFlip;
}
bool projectOperation(LevelSurfaceOperation operation)
{
	return operation == LevelSurfaceOperation::ProjectAxial || operation == LevelSurfaceOperation::ProjectDirection;
}
// Why a projection left a surface as it was: it runs along the surface, or
// a classic face cannot hold it.
enum class Skip { None, EdgeOn, Classic };
// Fit's scale factors for an axis that spans `extent` (texels on classic and
// Valve 220 faces, repeats on brush-primitive faces and patches) and `size`
// texels a repeat: a count of 0 keeps that axis (factor 1), unless keeping
// proportions, when it takes the other axis's factor.
// Workflow reference: NRC radiant/brush_primit.cpp Texdef_FitTexture (fit
// width or height, with and without only_dimension) at 449778b3
// (GPL-2.0-or-later). Original code; see docs/CREDITS.md.
std::pair<double, double> fitFactors(const LevelSurfaceRequest &request, double extentU, double extentV, double sizeU, double sizeV)
{
	double u = request.x > 0 ? request.x * sizeU / extentU : 1.0, v = request.y > 0 ? request.y * sizeV / extentV : 1.0;
	if (request.keepProportions && (request.x > 0) != (request.y > 0)) {
		if (request.x > 0) {
			v = u;
		} else {
			u = v;
		}
	}
	return {u, v};
}
bool fitsAxis(const LevelSurfaceRequest &request, bool uAxis)
{
	return (uAxis ? request.x : request.y) > 0 || (request.keepProportions && (request.x > 0) != (request.y > 0));
}
// Whether a patch's texture coordinates all lie along one line, as a flat
// projection running along the patch leaves them: the texture would smear.
bool collapsedCoordinates(const LevelMapPatch &patch)
{
	const qsizetype count = patch.controlU.size();
	const auto distance = [&patch](qsizetype a, qsizetype b) {
		return std::hypot(patch.controlU[a] - patch.controlU[b], patch.controlV[a] - patch.controlV[b]);
	};
	// Two coordinates about as far apart as any, then how far the others lie
	// off the line through them.
	qsizetype first = 0, second = 0;
	for (qsizetype i = 1; i < count; ++i) {
		first = distance(0, i) > distance(0, first) ? i : first;
	}
	for (qsizetype i = 0; i < count; ++i) {
		second = distance(first, i) > distance(first, second) ? i : second;
	}
	const double span = count > 0 ? distance(first, second) : 0.0;
	if (!std::isfinite(span) || span <= 1e-9) {
		return true;
	}
	const double du = (patch.controlU[second] - patch.controlU[first]) / span, dv = (patch.controlV[second] - patch.controlV[first]) / span;
	double off = 0;
	for (qsizetype i = 0; i < count; ++i) {
		off = std::max(off, std::abs((patch.controlU[i] - patch.controlU[first]) * dv - (patch.controlV[i] - patch.controlV[first]) * du));
	}
	return off <= 1e-6 * span;
}
// A camera move or flip as the face's own shift or mirror, once its texture
// axes are placed on screen (core/level_camera_texture.h).
LevelSurfaceRequest cameraRequest(const LevelSurfaceRequest &request, const LevelCameraTextureAxes &axes)
{
	LevelSurfaceRequest local = request;
	if (request.operation == LevelSurfaceOperation::CameraShift) {
		const QPointF shift = levelCameraTextureShift(axes, request.x, request.y);
		local.operation = LevelSurfaceOperation::Shift;
		local.x = shift.x();
		local.y = shift.y();
		return local;
	}
	const bool across = request.x != 0, upward = request.y != 0;
	local.operation = LevelSurfaceOperation::Scale;
	local.x = (axes.uHorizontal ? across : upward) ? -1 : 1;
	local.y = (axes.uHorizontal ? upward : across) ? -1 : 1;
	return local;
}
bool editFace(LevelMapBrushFace *face, const MapFacePolygon &polygon, const LevelSurfaceRequest &request, QSize imageSize, QString *error,
	Skip *skip = nullptr)
{
	const auto original = levelTextureProjection(*face);
	if (!original.valid || !polygon.isValid()) {
		return fail(error, SurfaceText::tr("The face has no valid winding or texture projection."));
	}
	if (cameraOperation(request.operation)) {
		if (request.x == 0 && request.y == 0) {
			return true;
		}
		const auto plane = face->explicitPlane ? MapPlane{face->planeNormal.x, face->planeNormal.y, face->planeNormal.z, 0, true}
											   : planeFromPoints(face->p0, face->p1, face->p2);
		const auto axes = levelCameraTextureAxes(original.u, original.v, {plane.normalX, plane.normalY, plane.normalZ, true}, request.cameraRight,
			request.cameraUp);
		if (!axes.valid) {
			return fail(error, SurfaceText::tr("The face's texture mapping is collapsed on its plane. Reset its mapping before moving it from the camera."));
		}
		return editFace(face, polygon, cameraRequest(request, axes), imageSize, error);
	}
	if (request.operation == LevelSurfaceOperation::ClockwiseRotate) {
		// TrenchBroom's turn: clockwise from the face's front, keeping the
		// offsets. Workflow reference: TB MapViewBase::rotateUV and
		// rotateUVAngle at 4c756be1 (GPL-3.0-or-later); original code, see
		// docs/CREDITS.md.
		if (std::remainder(request.degrees, 360.0) == 0) {
			return true;
		}
		// Which way a positive Radiant turn goes on screen depends on the
		// face: a classic face turns its projection axes whatever its scale
		// signs, a Valve 220 face its stored axes, and a brush-primitive face
		// always turns anticlockwise from its front. A quarter turn of a copy
		// shows which.
		LevelMapBrushFace probe = *face;
		if (!turn(&probe, 90, true, error)) {
			return false;
		}
		const auto turned = levelTextureProjection(probe);
		const LevelTextureTurn positive = turned.valid
			? levelTextureTurnSense(original.u, turned.u, {polygon.plane.normalX, polygon.plane.normalY, polygon.plane.normalZ, true})
			: LevelTextureTurn::None;
		if (positive == LevelTextureTurn::None) {
			return fail(error, SurfaceText::tr("The face's texture mapping is collapsed on its plane. Reset its mapping before rotating it."));
		}
		LevelSurfaceRequest local = request;
		local.operation = LevelSurfaceOperation::RadiantRotate;
		local.degrees = positive == LevelTextureTurn::Counterclockwise ? -request.degrees : request.degrees;
		return editFace(face, polygon, local, imageSize, error);
	}
	if (request.operation == LevelSurfaceOperation::Reset
		|| (request.operation == LevelSurfaceOperation::RadiantReset && !face->explicitTextureAxes && !face->explicitTextureMatrix)) {
		// One reset whatever the face's format, keeping it: a classic face to
		// its natural numbers, others into their plane. Workflow reference: TB
		// MapViewBase::resetUV (Reset Texture Alignment) and NRC
		// SurfaceInspector_ResetTexture (TextureReset/Cap) at 4c756be1
		// (GPL-3.0-or-later) and 449778b3 (GPL-2.0-or-later). Original code;
		// see docs/CREDITS.md. A Radiant reset of a Valve 220 or primitive face
		// takes its plane's basis below.
		LevelSurfaceRequest local = request;
		local.operation = !face->explicitTextureAxes && !face->explicitTextureMatrix ? LevelSurfaceOperation::Natural : LevelSurfaceOperation::FaceAligned;
		return editFace(face, polygon, local, imageSize, error);
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
	if (projectOperation(request.operation)) {
		// NetRadiant Custom's Project Axial, Ortho and Camera: the default
		// texture projected flat. Workflow reference: NRC radiant/surfacedialog.cpp
		// SurfaceInspector_ProjectTexture and radiant/brush_primit.cpp
		// Texdef_ProjectTexture at 449778b3 (GPL-2.0-or-later). Original code;
		// see docs/CREDITS.md. That editor gives brush-primitive faces the
		// direction projection with V running the other way from Valve 220
		// faces; here every format takes the Valve 220 result, which reads the
		// right way round from the side the direction points to.
		LevelMapBrushFace projected = *face;
		if (request.operation == LevelSurfaceOperation::ProjectAxial) {
			// Natural's projection along the world axis the face looks down most.
			LevelSurfaceRequest natural = request;
			natural.operation = LevelSurfaceOperation::Natural;
			if (!editFace(&projected, polygon, natural, imageSize, error)) {
				return false;
			}
		} else {
			const auto &d = request.projectionDirection;
			const double size = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
			const LevelMapVec3 unit{d.x / size, d.y / size, d.z / size, true};
			const auto &plane = polygon.plane;
			if (std::abs(unit.x * plane.normalX + unit.y * plane.normalY + unit.z * plane.normalZ) < 1e-6) {
				// The direction runs along the face: the texture would smear.
				if (skip) {
					*skip = Skip::EdgeOn;
				}
				return true;
			}
			const auto [s, t] = levelTexturePrimitiveBasis(unit);
			const double width = matrix ? imageSize.width() : 1.0, height = matrix ? imageSize.height() : 1.0;
			const double u = 1.0 / (request.x * width), v = 1.0 / (request.y * height);
			LevelTextureProjection projection;
			projection.u = {s.x * u, s.y * u, s.z * u, true};
			projection.v = {t.x * v, t.y * v, t.z * v, true};
			projection.normalizedCoordinates = matrix;
			projection.valid = true;
			QString problem;
			if (!setLevelTextureProjection(&projected, projection, false, &problem)) {
				if (!face->explicitTextureAxes && !matrix) {
					// A classic face holds only projections along its own axis.
					if (skip) {
						*skip = Skip::Classic;
					}
					return true;
				}
				return fail(error, problem);
			}
			if (projected.explicitTextureAxes) {
				// Valve 220 keeps the scale apart from its unit axes, as Radiant writes it.
				projected.uAxis = {s.x, s.y, s.z, true};
				projected.vAxis = {t.x, t.y, t.z, true};
				projected.scaleX = request.x;
				projected.scaleY = request.y;
			}
		}
		if (matrix) {
			// Radiant writes a projected matrix's offsets within one repeat
			// (BPTexdef_fromST011).
			for (int index : {2, 5}) {
				projected.textureMatrix[index] -= std::floor(projected.textureMatrix[index]);
			}
		}
		if (!writable(&projected)) {
			return fail(error, SurfaceText::tr("The result is too large, too small or collapsed at map precision. Adjust the surface values."));
		}
		if (matrix) {
			for (int index : {2, 5}) {
				// An offset just under one repeat rounds to it at map precision.
				if (projected.textureMatrix[index] >= 1.0) {
					projected.textureMatrix[index] -= 1.0;
				}
			}
		}
		*face = projected;
		return true;
	}
	if (request.operation == LevelSurfaceOperation::Natural || request.operation == LevelSurfaceOperation::FaceAligned
		|| request.operation == LevelSurfaceOperation::RadiantReset) {
		// A reset mapping, in the face's own dialect. Classic faces take the
		// natural one as plain numbers; a face-aligned slope may need axes.
		const auto projection = request.operation == LevelSurfaceOperation::Natural
			? levelNaturalTextureProjection(*face, request.x, request.y, imageSize)
			: request.operation == LevelSurfaceOperation::FaceAligned
			? levelFaceAlignedTextureProjection(*face, request.x, request.y, imageSize)
			: levelPlaneBasisTextureProjection(*face, request.x, request.y, imageSize);
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
		// A repeat count of zero keeps that axis as it is, or, keeping
		// proportions, scales it as the other and starts it at its minimum.
		const auto extent = maximum - minimum;
		if ((request.x > 0 && extent.x() <= 1e-10) || (request.y > 0 && extent.y() <= 1e-10)) {
			return fail(error, SurfaceText::tr("The face's texture coordinates are collapsed; set a valid projection before fitting."));
		}
		const auto [x, y] = fitFactors(request, extent.x(), extent.y(), width, height);
		multiply(face, x, y);
		offset(face, fitsAxis(request, true) ? -minimum.x() * x : 0.0, fitsAxis(request, false) ? -minimum.y() * y : 0.0);
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

// The directions in which a patch's texture coordinates grow, and its
// surface normal, at its middle control point: from the control points on
// either side of it along the row and down the column. A patch whose middle
// has no spread falls back to the whole grid's, first to last. Flat patches
// with an even mapping give the same directions anywhere.
bool patchTextureDirections(const LevelMapPatch &patch, LevelMapVec3 *gradientU, LevelMapVec3 *gradientV, LevelMapVec3 *normal)
{
	if (patch.width < 2 || patch.height < 2 || patch.controlPoints.size() != patch.width * patch.height) {
		return false;
	}
	const auto at = [&patch](int row, int column) { return row * patch.width + column; };
	const auto solve = [&](int row0, int row1, int column0, int column1, int middleRow, int middleColumn) {
		// Tangents along the row (s) and down the column (t), with the
		// coordinate changes along each.
		const auto &a = patch.controlPoints[at(middleRow, column0)], &b = patch.controlPoints[at(middleRow, column1)];
		const auto &c = patch.controlPoints[at(row0, middleColumn)], &d = patch.controlPoints[at(row1, middleColumn)];
		const LevelMapVec3 s{b.x - a.x, b.y - a.y, b.z - a.z, true}, t{d.x - c.x, d.y - c.y, d.z - c.z, true};
		const double du[2] = {patch.controlU[at(middleRow, column1)] - patch.controlU[at(middleRow, column0)],
							  patch.controlU[at(row1, middleColumn)] - patch.controlU[at(row0, middleColumn)]};
		const double dv[2] = {patch.controlV[at(middleRow, column1)] - patch.controlV[at(middleRow, column0)],
							  patch.controlV[at(row1, middleColumn)] - patch.controlV[at(row0, middleColumn)]};
		const double ss = s.x * s.x + s.y * s.y + s.z * s.z, tt = t.x * t.x + t.y * t.y + t.z * t.z, st = s.x * t.x + s.y * t.y + s.z * t.z;
		const double determinant = ss * tt - st * st;
		if (!std::isfinite(determinant) || determinant <= 1e-12 * ss * tt || ss <= 1e-12 || tt <= 1e-12) {
			return false;
		}
		// The in-surface vector g with g.s and g.t equal to the changes.
		const auto gradient = [&](const double change[2]) {
			const double alpha = (change[0] * tt - change[1] * st) / determinant, beta = (change[1] * ss - change[0] * st) / determinant;
			return LevelMapVec3{alpha * s.x + beta * t.x, alpha * s.y + beta * t.y, alpha * s.z + beta * t.z, true};
		};
		*gradientU = gradient(du);
		*gradientV = gradient(dv);
		*normal = {s.y * t.z - s.z * t.y, s.z * t.x - s.x * t.z, s.x * t.y - s.y * t.x, true};
		return true;
	};
	const int row = (patch.height - 1) / 2, column = (patch.width - 1) / 2;
	return solve(std::max(row - 1, 0), std::min(row + 1, patch.height - 1), std::max(column - 1, 0), std::min(column + 1, patch.width - 1), row, column)
		|| solve(0, patch.height - 1, 0, patch.width - 1, row, column);
}

// The same adjustments on a patch's control-point coordinates, which count
// repeats. Rotation and scaling keep the middle of the patch's coordinates
// where it is, as they keep a face's centre.
bool editPatch(LevelMapPatch *patch, const LevelSurfaceRequest &request, QSize imageSize, QString *error, Skip *skip = nullptr)
{
	const int count = static_cast<int>(patch->controlPoints.size());
	if (count == 0 || patch->controlU.size() != count || patch->controlV.size() != count) {
		return fail(error, SurfaceText::tr("The patch has no texture coordinates."));
	}
	if (request.operation == LevelSurfaceOperation::Reset || request.operation == LevelSurfaceOperation::RadiantReset) {
		// TrenchBroom's reset leaves a patch its natural spread along the
		// surface; Radiant's caps it, projecting along its average normal.
		LevelSurfaceRequest local = request;
		local.operation = request.operation == LevelSurfaceOperation::Reset ? LevelSurfaceOperation::Natural : LevelSurfaceOperation::FaceAligned;
		return editPatch(patch, local, imageSize, error);
	}
	if (cameraOperation(request.operation)) {
		if (request.x == 0 && request.y == 0) {
			return true;
		}
		LevelMapVec3 gradientU, gradientV, normal;
		const auto axes = patchTextureDirections(*patch, &gradientU, &gradientV, &normal)
			? levelCameraTextureAxes(gradientU, gradientV, normal, request.cameraRight, request.cameraUp)
			: LevelCameraTextureAxes{};
		if (!axes.valid) {
			return fail(error, SurfaceText::tr("The patch's texture coordinates have no direction at its middle. Reset them before moving them from the camera."));
		}
		return editPatch(patch, cameraRequest(request, axes), imageSize, error);
	}
	if (request.operation == LevelSurfaceOperation::ClockwiseRotate) {
		// Clockwise from the side the patch's normal faces, about coordinate
		// 0,0. A patch turns its coordinates, so a positive turn reads
		// anticlockwise on a texture the right way round and clockwise on a
		// mirrored one; a quarter turn of a copy shows which.
		if (std::remainder(request.degrees, 360.0) == 0) {
			return true;
		}
		LevelSurfaceRequest quarter = request;
		quarter.operation = LevelSurfaceOperation::RadiantRotate;
		quarter.degrees = 90;
		LevelMapPatch probe = *patch;
		if (!editPatch(&probe, quarter, imageSize, error)) {
			return false;
		}
		LevelMapVec3 gradientU, gradientV, normal, turnedU, turnedV, turnedNormal;
		const LevelTextureTurn positive = patchTextureDirections(*patch, &gradientU, &gradientV, &normal)
				&& patchTextureDirections(probe, &turnedU, &turnedV, &turnedNormal)
			? levelTextureTurnSense(gradientU, turnedU, normal)
			: LevelTextureTurn::None;
		if (positive == LevelTextureTurn::None) {
			return fail(error, SurfaceText::tr("The patch's texture coordinates have no direction at its middle. Reset them before rotating them."));
		}
		LevelSurfaceRequest local = request;
		local.operation = LevelSurfaceOperation::RadiantRotate;
		local.degrees = positive == LevelTextureTurn::Counterclockwise ? -request.degrees : request.degrees;
		return editPatch(patch, local, imageSize, error);
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
		// Coordinates count repeats, so one factor on both keeps proportions.
		const auto [factorU, factorV] = fitFactors(request, maxU - minU, maxV - minV, 1.0, 1.0);
		const bool fitU = fitsAxis(request, true), fitV = fitsAxis(request, false);
		for (int i = 0; i < count; ++i) {
			if (fitU) {
				patch->controlU[i] = (patch->controlU[i] - minU) * factorU;
			}
			if (fitV) {
				patch->controlV[i] = (patch->controlV[i] - minV) * factorV;
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
	case LevelSurfaceOperation::ProjectAxial:
	case LevelSurfaceOperation::ProjectDirection: {
		const LevelMapPatch original = *patch;
		const bool projected = request.operation == LevelSurfaceOperation::ProjectAxial
			? axialLevelPatchTexture(patch, width * request.x, height * request.y, &problem)
			: projectedLevelPatchTexture(patch, request.projectionDirection, width * request.x, height * request.y, &problem);
		if (!projected) {
			return fail(error, problem);
		}
		if (collapsedCoordinates(*patch)) {
			// The projection runs along the patch: the texture would smear.
			*patch = original;
			if (skip) {
				*skip = Skip::EdgeOn;
			}
			return true;
		}
		break;
	}
	case LevelSurfaceOperation::CameraShift:
	case LevelSurfaceOperation::CameraFlip:
	case LevelSurfaceOperation::ClockwiseRotate:
	case LevelSurfaceOperation::Reset:
	case LevelSurfaceOperation::RadiantReset:
		// Resolved to a shift, a mirror, a Radiant turn or a reset above.
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

// A Reset Textures filter's key: as materialKey, also without textures/,
// which Quake III patches leave off their names.
QString resetKey(const QString &name)
{
	QString key = materialKey(name);
	return key.startsWith(QStringLiteral("textures/")) ? key.mid(9) : key;
}

// bobToolz's Reset Textures writes the ticked values into each face's
// texture definition, and Radiant's Surface Inspector gives Valve 220 and
// brush-primitive faces the same absolute values. Workflow reference: NRC
// contrib/bobtoolz/DBrush.cpp DBrush::ResetTextures and radiant/brush_primit.cpp
// Texdef_Assign, BPTexdef_Assign and TexMatToFakeTexCoords at 449778b3
// (GPL-2.0-or-later). Original code; see docs/CREDITS.md.
bool assignTextureValues(LevelMapBrushFace *face, const LevelTextureResetRequest &request, QSize imageSize, QString *error)
{
	if (!request.changesValues()) {
		return true;
	}
	if (face->explicitTextureMatrix) {
		if (imageSize.width() <= 0 || imageSize.height() <= 0) {
			return fail(error, SurfaceText::tr("Material %1 needs a known image size. Open its package or provide an explicit texture size.")
								   .arg(face->textureName));
		}
		auto &m = face->textureMatrix;
		const double width = imageSize.width(), height = imageSize.height();
		// Radiant reads a matrix in texels as a scale from each row's length,
		// negative where the row's own axis runs backwards, a turn from the
		// first row and a shift from the offsets.
		const double lengthU = std::hypot(m[0], m[1]) * width, lengthV = std::hypot(m[3], m[4]) * height;
		if (!std::isfinite(lengthU) || !std::isfinite(lengthV) || lengthU < 1e-12 || lengthV < 1e-12) {
			return fail(error, SurfaceText::tr("The face's texture matrix is collapsed. Reset its mapping before setting its values."));
		}
		const double scaleU = (m[0] < 0 ? -1.0 : 1.0) / lengthU, scaleV = (m[4] < 0 ? -1.0 : 1.0) / lengthV;
		double rotation = std::acos(std::clamp(m[0] * width / lengthU, -1.0, 1.0)) * 180.0 / 3.14159265358979323846;
		if (m[1] > 0) {
			rotation = -rotation;
		}
		if (request.setShiftU) {
			m[2] = -request.shiftU / width;
		}
		if (request.setShiftV) {
			m[5] = request.shiftV / height;
		}
		if (request.setScaleU) {
			m[0] *= scaleU / request.scaleU;
			m[1] *= scaleU / request.scaleU;
		}
		if (request.setScaleV) {
			m[3] *= scaleV / request.scaleV;
			m[4] *= scaleV / request.scaleV;
		}
		return !request.setRotation || turn(face, request.rotation - rotation, true, error);
	}
	if (request.setRotation) {
		// Valve 220 axes carry the orientation, so they turn by the change.
		if (face->explicitTextureAxes && !turn(face, request.rotation - face->rotation, true, error)) {
			return false;
		}
		face->rotation = std::fmod(request.rotation, 360.0);
	}
	if (request.setShiftU) {
		(face->explicitTextureAxes ? face->uOffset : face->shiftX) = request.shiftU;
	}
	if (request.setShiftV) {
		(face->explicitTextureAxes ? face->vOffset : face->shiftY) = request.shiftV;
	}
	if (request.setScaleU) {
		face->scaleX = request.scaleU;
	}
	if (request.setScaleV) {
		face->scaleY = request.scaleV;
	}
	return true;
}
} // namespace

bool levelSurfaceOperationNeedsSize(LevelSurfaceOperation operation, bool repeatsCoordinates)
{
	if (repeatsCoordinates) {
		// A camera shift is a shift in texels; a camera flip is a mirror.
		return operation == LevelSurfaceOperation::Shift || operation == LevelSurfaceOperation::Rotate || operation == LevelSurfaceOperation::Natural
			|| operation == LevelSurfaceOperation::FaceAligned || operation == LevelSurfaceOperation::RadiantRotate
			|| operation == LevelSurfaceOperation::RadiantScale || operation == LevelSurfaceOperation::CameraShift
			|| operation == LevelSurfaceOperation::ClockwiseRotate || operation == LevelSurfaceOperation::Reset
			|| operation == LevelSurfaceOperation::RadiantReset || projectOperation(operation);
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
		const bool reset = request.operation == LevelSurfaceOperation::Natural || request.operation == LevelSurfaceOperation::FaceAligned
			|| request.operation == LevelSurfaceOperation::Reset || request.operation == LevelSurfaceOperation::RadiantReset
			|| projectOperation(request.operation);
		if (!std::isfinite(request.x) || !std::isfinite(request.y) || !std::isfinite(request.degrees) || std::abs(request.x) > 1e6 ||
			std::abs(request.y) > 1e6 || std::abs(request.degrees) > 360000 ||
			(request.operation == LevelSurfaceOperation::Scale && (std::abs(request.x) < 1e-6 || std::abs(request.y) < 1e-6)) ||
			(request.operation == LevelSurfaceOperation::Fit &&
			 (request.x < 0 || request.y < 0 || (request.x < 1e-6 && request.y < 1e-6) || (request.x > 0 && request.x < 1e-6) ||
			  (request.y > 0 && request.y < 1e-6))) ||
			(reset && (request.x < 1e-6 || request.y < 1e-6)) ||
			request.operation < LevelSurfaceOperation::Shift || request.operation > LevelSurfaceOperation::ProjectDirection ||
			request.alignU < LevelSurfaceAlignment::Keep || request.alignU > LevelSurfaceAlignment::Maximum ||
			request.alignV < LevelSurfaceAlignment::Keep || request.alignV > LevelSurfaceAlignment::Maximum) {
			return fail(error, SurfaceText::tr(
								   "Surface values must be finite and in range. Fit repeats must be positive; scale factors cannot be zero."));
		}
		if (cameraOperation(request.operation) && !levelCameraTextureBasisValid(request.cameraRight, request.cameraUp)) {
			return fail(error, SurfaceText::tr("Camera texture moves need the camera's right and up directions, which must not be parallel."));
		}
		if (request.operation == LevelSurfaceOperation::ProjectDirection) {
			const auto &d = request.projectionDirection;
			const double size = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
			if (!d.valid || !std::isfinite(size) || size < 1e-9 || size > 1e9) {
				return fail(error, SurfaceText::tr("A projection along a direction needs a finite direction that is not zero."));
			}
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
	// Faces and patches targeted, to tell a projection that skips them all.
	int targetCount = 0;
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
			Skip skipped = Skip::None;
			for (const auto &request : requests) {
				if (cancelled && cancelled()) { return fail(error, SurfaceText::tr("Surface preview cancelled.")); }
				Skip skip = Skip::None;
				if (!editFace(&face, geometry.faces[index], request, sizes.value(materialKey(face.textureName)), &problem, &skip)) {
					return fail(error, SurfaceText::tr("Brush %1, face %2: %3").arg(it.key()).arg(index + 1).arg(problem));
				}
				skipped = skipped == Skip::None ? skip : skipped;
			}
			++targetCount;
			result.m_skippedEdgeOn += skipped == Skip::EdgeOn;
			result.m_skippedClassic += skipped == Skip::Classic;
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
		Skip skipped = Skip::None;
		for (const auto &request : requests) {
			QString problem;
			Skip skip = Skip::None;
			if (!editPatch(&after, request, sizes.value(materialKey(patch.textureName)), &problem, &skip)) {
				return fail(error, SurfaceText::tr("Patch %1: %2").arg(patch.id).arg(problem));
			}
			skipped = skipped == Skip::None ? skip : skipped;
		}
		++targetCount;
		result.m_skippedEdgeOn += skipped == Skip::EdgeOn;
		if (after.controlU != patch.controlU || after.controlV != patch.controlV) {
			after.definitionDirty = after.geometryDirty = true;
			result.m_patchBefore.append(patch);
			result.m_patchAfter.append(after);
		}
	}
	if (!wanted.isEmpty()) {
		return fail(error, SurfaceText::tr("Patch %1 no longer exists.").arg(*wanted.cbegin()));
	}
	const int skipped = result.m_skippedEdgeOn + result.m_skippedClassic;
	if (skipped > 0 && skipped == targetCount && result.m_faceCount == 0 && result.m_patchAfter.isEmpty()) {
		// A projection that can take none of its targets says why.
		if (result.m_skippedClassic == 0) {
			return fail(error, SurfaceText::tr("The projection runs along every targeted surface, so none was changed. Project from a view that faces them."));
		}
		if (result.m_skippedEdgeOn == 0) {
			return fail(error, SurfaceText::tr("Classic faces cannot hold this projection, so none of the targeted faces was changed. Project from a view that looks straight at them, or use Valve 220 or brush-primitive faces."));
		}
		return fail(error, SurfaceText::tr("None of the targeted surfaces can take this projection: it runs along some of them, and the classic faces among the others cannot hold it."));
	}
	result.m_ready = true;
	*plan = std::move(result);
	return true;
}

bool levelTextureResetTargets(const LevelMapDocument &document, const LevelTextureResetRequest &request, QVector<LevelSurfaceFace> *faces,
							  QVector<int> *patches, QStringList *sizedMaterials, QString *error)
{
	if (error) {
		error->clear();
	}
	if (!faces || !patches) {
		return fail(error, SurfaceText::tr("Missing Reset Textures targets."));
	}
	faces->clear();
	patches->clear();
	if (sizedMaterials) {
		sizedMaterials->clear();
	}
	if (document.format != LevelMapFormat::QuakeMap && document.format != LevelMapFormat::Quake3Map) {
		return fail(error, SurfaceText::tr("Reset Textures requires a Quake-family text map."));
	}
	const QString filter = resetKey(request.onlyTexture);
	const auto matches = [&filter](const QString &name) { return filter.isEmpty() || resetKey(name) == filter; };
	QSet<QString> sized;
	const auto add = [&](const LevelMapBrush &brush, int index) {
		const auto &face = brush.faces[index];
		if (!matches(face.textureName)) {
			return;
		}
		faces->append({brush.id, index});
		if (request.changesValues() && face.explicitTextureMatrix) {
			sized.insert(materialKey(face.textureName));
		}
	};
	if (request.wholeMap) {
		// Editable means visible, unlocked and inside the open group, as Find
		// and Replace Textures reads the map; closed groups stay editable.
		const auto context = levelSceneSelectionContext(document);
		const auto excluded = levelMapHiddenObjects(document) | levelSceneLockedObjects(document) | request.excludedObjects;
		const auto editable = [&](LevelMapSelectionKind kind, int id, int owner) {
			const QString key = levelMapSelectionRefId({kind, id});
			return context.allowedObjects.contains(key) && !excluded.contains(key)
				&& !excluded.contains(levelMapSelectionRefId({LevelMapSelectionKind::Entity, owner}));
		};
		for (const auto &brush : document.brushes) {
			if (editable(LevelMapSelectionKind::QuakeBrush, brush.id, brush.entityId)) {
				for (int index = 0; index < brush.faces.size(); ++index) {
					add(brush, index);
				}
			}
		}
		for (const auto &patch : document.patches) {
			if (editable(LevelMapSelectionKind::QuakePatch, patch.id, patch.entityId) && matches(patch.textureName)) {
				patches->append(patch.id);
			}
		}
	} else {
		QHash<int, const LevelMapBrush *> brushes;
		for (const auto &brush : document.brushes) {
			brushes.insert(brush.id, &brush);
		}
		QSet<QPair<int, int>> seen;
		for (const auto &face : request.faces) {
			const auto *brush = brushes.value(face.brushId);
			if (!brush || face.faceIndex < 0 || face.faceIndex >= brush->faces.size()) {
				return fail(error, SurfaceText::tr("Brush %1 has no face %2.").arg(face.brushId).arg(face.faceIndex + 1));
			}
			if (!seen.contains({face.brushId, face.faceIndex})) {
				seen.insert({face.brushId, face.faceIndex});
				add(*brush, face.faceIndex);
			}
		}
		QSet<int> wanted(request.patches.cbegin(), request.patches.cend());
		for (const auto &patch : document.patches) {
			if (wanted.remove(patch.id) && matches(patch.textureName)) {
				patches->append(patch.id);
			}
		}
		if (!wanted.isEmpty()) {
			return fail(error, SurfaceText::tr("Patch %1 no longer exists.").arg(*wanted.cbegin()));
		}
	}
	if (faces->size() > 262144 || patches->size() > 65536) {
		return fail(error, SurfaceText::tr("Reset Textures works on at most 262,144 brush faces and 65,536 patches at once. Filter by texture or reset part of the map."));
	}
	if (sizedMaterials) {
		*sizedMaterials = QStringList(sized.cbegin(), sized.cend());
		sizedMaterials->sort();
	}
	return true;
}

bool prepareLevelTextureReset(const LevelMapDocument &document, const LevelTextureResetRequest &request, const QHash<QString, QSize> &textureSizes,
							  LevelSurfaceEditPlan *plan, QString *error, const std::function<bool()> &cancelled)
{
	if (error) {
		error->clear();
	}
	if (!plan) {
		return fail(error, SurfaceText::tr("Missing surface edit plan."));
	}
	*plan = {};
	if (!request.changesSomething()) {
		return fail(error, SurfaceText::tr("Tick a value to set, or give a new texture name, before resetting textures."));
	}
	const auto finite = [](bool set, double value, double limit) { return !set || (std::isfinite(value) && std::abs(value) <= limit); };
	if (!finite(request.setScaleU, request.scaleU, 1e6) || !finite(request.setScaleV, request.scaleV, 1e6) || !finite(request.setShiftU, request.shiftU, 1e6)
		|| !finite(request.setShiftV, request.shiftV, 1e6) || !finite(request.setRotation, request.rotation, 360000)
		|| (request.setScaleU && std::abs(request.scaleU) < 1e-6) || (request.setScaleV && std::abs(request.scaleV) < 1e-6)) {
		return fail(error, SurfaceText::tr("Reset values must be finite and in range, and a texture scale cannot be 0."));
	}
	const QString name = request.renameTo.trimmed();
	for (const QChar ch : name) {
		if (ch.isSpace() || !ch.isPrint() || QStringLiteral("\"{}()[]\\").contains(ch)) {
			return fail(error, SurfaceText::tr("A texture name cannot contain spaces, quotes, braces, brackets, parentheses or backslashes."));
		}
	}
	if (name.contains(QStringLiteral("//"))) {
		return fail(error, SurfaceText::tr("A texture name cannot contain //, which starts a comment."));
	}
	QVector<LevelSurfaceFace> faces;
	QVector<int> patches;
	if (!levelTextureResetTargets(document, request, &faces, &patches, nullptr, error)) {
		return false;
	}
	QHash<QString, QSize> sizes;
	for (auto it = textureSizes.cbegin(); it != textureSizes.cend(); ++it) {
		sizes.insert(materialKey(it.key()), it.value());
	}
	LevelSurfaceEditPlan result;
	result.m_sourcePath = document.sourcePath;
	result.m_sourceHash = document.sourceContentHash;
	result.m_revision = document.revision;
	result.m_format = document.format;
	result.m_reset = true;
	QMap<int, QSet<int>> targets;
	for (const auto &face : faces) {
		targets[face.brushId].insert(face.faceIndex);
	}
	for (const auto &brush : document.brushes) {
		if (cancelled && cancelled()) {
			return fail(error, SurfaceText::tr("Reset Textures cancelled."));
		}
		const auto found = targets.constFind(brush.id);
		if (found == targets.cend()) {
			continue;
		}
		auto after = brush;
		int changed = 0;
		bool renamed = false, remapped = false;
		for (int index : *found) {
			auto &face = after.faces[index];
			QString problem;
			if (!assignTextureValues(&face, request, sizes.value(materialKey(face.textureName)), &problem)) {
				return fail(error, SurfaceText::tr("Brush %1, face %2: %3").arg(brush.id).arg(index + 1).arg(problem));
			}
			if (request.changesValues() && !writable(&face)) {
				return fail(error, SurfaceText::tr("Brush %1, face %2: %3")
									   .arg(brush.id)
									   .arg(index + 1)
									   .arg(SurfaceText::tr("The result is too large, too small or collapsed at map precision. Adjust the values.")));
			}
			const bool rename = !name.isEmpty() && face.textureName != name;
			if (rename) {
				face.textureName = name;
				face.textureDirty = true;
				renamed = true;
				++result.m_renamed;
			}
			// A rename alone keeps the face's numbers as they are written.
			if (!sameMapping(brush.faces[index], face)) {
				face.textureParametersDirty = true;
				remapped = true;
			} else {
				const QString kept = face.textureName;
				face = brush.faces[index];
				if (!rename) {
					continue;
				}
				face.textureName = kept;
				face.textureDirty = true;
			}
			++changed;
		}
		if (changed == 0) {
			continue;
		}
		after.textureParametersDirty = brush.textureParametersDirty || remapped;
		if (renamed) {
			after.texturesDirty = true;
			after.textureNames.clear();
			for (const auto &face : after.faces) {
				if (!face.textureName.trimmed().isEmpty() && !after.textureNames.contains(face.textureName)) {
					after.textureNames.push_back(face.textureName);
				}
			}
		}
		result.m_before.append(brush);
		result.m_after.append(after);
		result.m_faceCount += changed;
	}
	if (!name.isEmpty()) {
		const QSet<int> wanted(patches.cbegin(), patches.cend());
		for (const auto &patch : document.patches) {
			if (cancelled && cancelled()) {
				return fail(error, SurfaceText::tr("Reset Textures cancelled."));
			}
			if (!wanted.contains(patch.id)) {
				continue;
			}
			auto after = patch;
			after.textureName = levelPatchMaterialToken(name, patch.fixedSubdivisions);
			if (after.textureName == patch.textureName) {
				continue;
			}
			after.textureDirty = true;
			result.m_patchBefore.append(patch);
			result.m_patchAfter.append(after);
			++result.m_renamed;
		}
	}
	result.m_ready = true;
	*plan = std::move(result);
	return true;
}
} // namespace vibestudio
