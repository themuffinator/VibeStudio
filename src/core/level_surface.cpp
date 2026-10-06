#include "core/level_surface.h"
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
	if ((request.operation == LevelSurfaceOperation::Shift && request.x == 0 && request.y == 0) ||
		(request.operation == LevelSurfaceOperation::Scale && request.x == 1 && request.y == 1) ||
		(request.operation == LevelSurfaceOperation::Rotate && std::remainder(request.degrees, 360.0) == 0) ||
		(request.operation == LevelSurfaceOperation::Align && request.alignU == LevelSurfaceAlignment::Keep &&
		 request.alignV == LevelSurfaceAlignment::Keep)) {
		return true;
	}
	const bool matrix = face->explicitTextureMatrix;
	const bool needsSize =
		(!matrix && (request.operation == LevelSurfaceOperation::Fit || request.operation == LevelSurfaceOperation::Align)) ||
		(matrix && (request.operation == LevelSurfaceOperation::Shift || request.operation == LevelSurfaceOperation::Rotate));
	if (needsSize && (imageSize.width() <= 0 || imageSize.height() <= 0)) {
		return fail(error, SurfaceText::tr("Material %1 needs a known image size. Open its package or provide an explicit texture size.")
							   .arg(face->textureName));
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
		const auto trig = rotateLevelVector({1, 0, 0, true}, 2, -request.degrees);
		if (matrix) {
			// Rotate in texel space, so a non-square image does not stretch.
			const auto before = face->textureMatrix;
			for (int i = 0; i < 3; ++i) {
				face->textureMatrix[i] = trig.x * before[i] - trig.y * before[i + 3] * imageSize.height() / imageSize.width();
				face->textureMatrix[i + 3] = trig.y * before[i] * imageSize.width() / imageSize.height() + trig.x * before[i + 3];
			}
		} else {
			if (face->explicitTextureAxes) {
				const auto &p = polygon.plane;
				const auto turn = [&](const LevelMapVec3 &v) {
					const double along = p.normalX * v.x + p.normalY * v.y + p.normalZ * v.z;
					return LevelMapVec3{v.x * trig.x + (p.normalY * v.z - p.normalZ * v.y) * trig.y + p.normalX * along * (1 - trig.x),
										v.y * trig.x + (p.normalZ * v.x - p.normalX * v.z) * trig.y + p.normalY * along * (1 - trig.x),
										v.z * trig.x + (p.normalX * v.y - p.normalY * v.x) * trig.y + p.normalZ * along * (1 - trig.x),
										true};
				};
				face->uAxis = turn(face->uAxis);
				face->vAxis = turn(face->vAxis);
			}
			face->rotation = std::remainder(face->rotation + request.degrees, 360.0);
		}
		const auto moved = levelTextureProjection(*face).at(center);
		offset(face, anchor.x() - moved.x(), anchor.y() - moved.y());
	} else if (request.operation == LevelSurfaceOperation::Fit) {
		const auto extent = maximum - minimum;
		if (extent.x() <= 1e-10 || extent.y() <= 1e-10) {
			return fail(error, SurfaceText::tr("The face's texture coordinates are collapsed; set a valid projection before fitting."));
		}
		const double x = request.x * width / extent.x(), y = request.y * height / extent.y();
		multiply(face, x, y);
		offset(face, -minimum.x() * x, -minimum.y() * y);
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
} // namespace

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
	if (faces.isEmpty() || faces.size() > 16384) {
		return fail(error, SurfaceText::tr("Select between 1 and 16,384 brush faces for surface alignment."));
	}
	if (requests.isEmpty() || requests.size() > 64) {
		return fail(error, SurfaceText::tr("A surface batch needs between 1 and 64 adjustments."));
	}
	for (const auto &request : requests) {
		if (!std::isfinite(request.x) || !std::isfinite(request.y) || !std::isfinite(request.degrees) || std::abs(request.x) > 1e6 ||
			std::abs(request.y) > 1e6 || std::abs(request.degrees) > 360000 ||
			(request.operation == LevelSurfaceOperation::Scale && (std::abs(request.x) < 1e-6 || std::abs(request.y) < 1e-6)) ||
			(request.operation == LevelSurfaceOperation::Fit && (request.x < 1e-6 || request.y < 1e-6)) ||
			request.operation < LevelSurfaceOperation::Shift || request.operation > LevelSurfaceOperation::Align ||
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
	result.m_ready = true;
	*plan = std::move(result);
	return true;
}
} // namespace vibestudio
