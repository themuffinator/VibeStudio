#include "core/level_patch_cap.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace vibestudio
{
namespace
{
bool fail(QString *error, const char *message)
{
	if (error) {
		*error = QCoreApplication::translate("LevelPatchCap", message);
	}
	return false;
}
LevelMapVec3 add(const LevelMapVec3 &a, const LevelMapVec3 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z, true}; }
LevelMapVec3 scale(const LevelMapVec3 &a, double s) { return {a.x * s, a.y * s, a.z * s, true}; }
LevelMapVec3 sub(const LevelMapVec3 &a, const LevelMapVec3 &b) { return add(a, scale(b, -1)); }
double dot(const LevelMapVec3 &a, const LevelMapVec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
LevelMapVec3 cross(const LevelMapVec3 &a, const LevelMapVec3 &b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x, true};
}
double length(const LevelMapVec3 &a) { return std::hypot(a.x, a.y, a.z); }

bool build(const LevelMapPatch &source, LevelPatchBoundary edge, const LevelPatchCapRequest &request, LevelPatchCapResult *out,
		   QString *error)
{
	const auto indices = levelPatchBoundaryIndices(source, edge);
	if (indices.isEmpty()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelPatchCap", "Choose a valid patch boundary."));
	}
	QVector<LevelMapVec3> points;
	for (int i : indices) {
		points << source.controlPoints[i];
	}
	const bool closed = length(sub(points.first(), points.last())) <= 1e-7;
	LevelMapVec3 center{0, 0, 0, true};
	if (closed) {
		for (qsizetype i = 0; i < points.size() - 1; ++i) {
			center = add(center, points[i]);
		}
		center = scale(center, 1.0 / (points.size() - 1));
	} else {
		center = scale(add(points.first(), points.last()), 0.5);
	}
	if (request.customCenter) {
		if (!closed && length(sub(request.center, center)) > 1e-7) {
			return fail(error,
						QT_TRANSLATE_NOOP("LevelPatchCap",
										  "An open boundary closes along its endpoint chord; its center must be the chord midpoint."));
		}
		center = request.center;
	}
	if (!center.valid || !std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) || std::abs(center.x) > 1048576 ||
		std::abs(center.y) > 1048576 || std::abs(center.z) > 1048576) {
		return fail(error, QT_TRANSLATE_NOOP("LevelPatchCap", "The cap center must contain finite coordinates within ±1048576."));
	}
	// Sum cross products around the translated polygon for a stable plane normal.
	LevelMapVec3 normal{0, 0, 0, true};
	for (qsizetype i = 0; i < points.size(); ++i) {
		normal = add(normal, cross(sub(points[i], center), sub(points[(i + 1) % points.size()], center)));
	}
	const double area = length(normal);
	if (area < 1e-8) {
		return fail(error,
					QT_TRANSLATE_NOOP("LevelPatchCap", "The boundary is collapsed, straight or self-crossing and cannot form a cap."));
	}
	normal = scale(normal, 1.0 / area);
	double radius = 0;
	for (const auto &p : points) {
		radius = std::max(radius, length(sub(p, center)));
	}
	for (const auto &p : points) {
		const auto v = sub(p, center);
		if (std::abs(dot(v, normal)) > std::max(1e-7, radius * 1e-10)) {
			return fail(error, QT_TRANSLATE_NOOP("LevelPatchCap",
												 "The boundary and center must share one plane. Flatten the boundary before capping it."));
		}
		if (length(v) < 1e-7) {
			return fail(error,
						QT_TRANSLATE_NOOP("LevelPatchCap",
										  "A boundary control lies at the cap center. Choose another center or reshape the boundary."));
		}
	}
	// A positive angular sweep of at most one turn makes each radial ray unique.
	// For a quadratic arc, positive cross(p0,p1), cross(p1,p2) and cross(p0,p2)
	// imply cross(B(t),B'(t)) >= 0 throughout the span, not just at samples.
	double sweep = 0;
	for (qsizetype i = 1; i < points.size(); ++i) {
		const auto a = sub(points[i - 1], center), b = sub(points[i], center);
		const double angle = std::atan2(dot(normal, cross(a, b)), dot(a, b));
		if (angle < -1e-10 || angle >= std::numbers::pi - 1e-10) {
			return fail(error, QT_TRANSLATE_NOOP(
								   "LevelPatchCap",
								   "The boundary folds around the cap center. Reshape it or choose a center inside its visible region."));
		}
		sweep += std::max(0.0, angle);
	}
	for (qsizetype i = 0; i + 2 < points.size(); i += 2) {
		const auto a = sub(points[i], center), b = sub(points[i + 2], center);
		if (dot(normal, cross(a, b)) < -1e-10 * length(a) * length(b) ||
			(length(cross(a, b)) < 1e-10 * length(a) * length(b) && dot(a, b) > 0)) {
			return fail(error,
						QT_TRANSLATE_NOOP(
							"LevelPatchCap",
							"Each curved span must cover a positive angle of at most half a turn. Subdivide the boundary before capping."));
		}
	}
	const double expected = closed ? 2 * std::numbers::pi : std::numbers::pi;
	if (std::abs(sweep - expected) > 1e-7) {
		return fail(error, QT_TRANSLATE_NOOP("LevelPatchCap", "The boundary overlaps itself or cannot close as a single radial cap."));
	}
	const bool reversed = (edge == LevelPatchBoundary::LastRow || edge == LevelPatchBoundary::FirstColumn) != request.invert;
	const auto uAxis = scale(sub(points.first(), center), 1.0 / length(sub(points.first(), center))), vAxis = cross(normal, uAxis);
	double centerU = 0, centerV = 0;
	for (int index : indices) {
		centerU += source.controlU[index] / indices.size();
		centerV += source.controlV[index] / indices.size();
	}
	LevelMapPatch cap;
	cap.entityId = source.entityId;
	cap.width = static_cast<int>(indices.size());
	cap.height = 3;
	cap.textureName = request.texture.isEmpty() ? source.textureName : levelPatchMaterialToken(request.texture, source.fixedSubdivisions);
	cap.fixedSubdivisions = source.fixedSubdivisions;
	cap.subdivisionsX =
		source.fixedSubdivisions
			? ((edge == LevelPatchBoundary::FirstRow || edge == LevelPatchBoundary::LastRow) ? source.subdivisionsX : source.subdivisionsY)
			: 0;
	cap.subdivisionsY = source.fixedSubdivisions ? 1 : 0;
	cap.headerTail = source.headerTail;
	cap.controlGridNormalized = cap.definitionDirty = true;
	for (int row = 0; row < 3; ++row) {
		const double radial = row * 0.5;
		for (int column = 0; column < cap.width; ++column) {
			const int i = reversed ? cap.width - 1 - column : column;
			const auto p = row == 2 ? points[i] : add(center, scale(sub(points[i], center), radial));
			cap.controlPoints << p;
			if (request.uv == LevelPatchCapUv::Planar) {
				cap.controlU << 0.5 + dot(sub(p, center), uAxis) / request.unitsPerTile;
				cap.controlV << 0.5 + dot(sub(p, center), vAxis) / request.unitsPerTile;
			} else {
				cap.controlU << centerU + (source.controlU[indices[i]] - centerU) * radial;
				cap.controlV << centerV + (source.controlV[indices[i]] - centerV) * radial;
			}
		}
	}
	refreshLevelPatchBounds(&cap);
	if (!validateLevelPatch(cap, error)) {
		return false;
	}
	out->caps << cap;
	out->boundaries << edge;
	out->centers << center;
	out->closed << closed;
	out->reversed << reversed;
	return true;
}
} // namespace

bool prepareLevelPatchCaps(const LevelMapPatch &source, const LevelPatchCapRequest &request, LevelPatchCapResult *result, QString *error,
						   const std::function<bool()> &cancelled)
{
	if (error) {
		error->clear();
	}
	if (!result || !validateLevelPatch(source, error)) {
		return false;
	}
	if (request.boundaries.isEmpty() || request.boundaries.size() > 4 || (request.customCenter && request.boundaries.size() != 1) ||
		!std::isfinite(request.unitsPerTile) || request.unitsPerTile <= 0 ||
		(request.uv != LevelPatchCapUv::Planar && request.uv != LevelPatchCapUv::Boundary)) {
		return fail(error,
					QT_TRANSLATE_NOOP(
						"LevelPatchCap",
						"Choose one to four distinct boundaries, a positive texture scale, and at most one boundary for a custom center."));
	}
	LevelPatchCapResult prepared;
	QSet<int> seen;
	for (auto edge : request.boundaries) {
		if (cancelled && cancelled()) {
			return fail(error, QT_TRANSLATE_NOOP("LevelPatchCap", "Patch cap preparation cancelled."));
		}
		if (seen.contains(static_cast<int>(edge))) {
			return fail(error, QT_TRANSLATE_NOOP("LevelPatchCap", "A cap boundary was selected more than once."));
		}
		seen.insert(static_cast<int>(edge));
		if (!build(source, edge, request, &prepared, error)) {
			if (error) {
				*error = QCoreApplication::translate("LevelPatchCap", "Cannot cap %1: %2").arg(levelPatchBoundaryId(edge), *error);
			}
			return false;
		}
	}
	if (request.uv == LevelPatchCapUv::Boundary) {
		prepared.warnings << QCoreApplication::translate(
			"LevelPatchCap",
			"Boundary UVs are retained; seams and distortion converge at the cap center. Planar mapping avoids this for flat caps.");
	}
	if (request.invert) {
		prepared.warnings << QCoreApplication::translate(
			"LevelPatchCap", "Cap facing is inverted from the source boundary. Review the visible side before applying.");
	}
	prepared.warnings << QCoreApplication::translate("LevelPatchCap",
													 "Caps are separate patch surfaces and do not create a solid brush. Compiler collision "
													 "and tessellation follow the chosen material and engine.");
	*result = std::move(prepared);
	return true;
}

bool capLevelMapPatch(LevelMapDocument *document, int patchId, const LevelPatchCapRequest &request, LevelPatchCapResult *result,
					  QString *error)
{
	if (!document || document->format != LevelMapFormat::Quake3Map) {
		return fail(error, QT_TRANSLATE_NOOP("LevelPatchCap", "Open a Quake III-family map to add patch caps."));
	}
	const auto source = std::find_if(document->patches.cbegin(), document->patches.cend(), [&](const auto &p) { return p.id == patchId; });
	if (source == document->patches.cend()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelPatchCap", "The source patch no longer exists."));
	}
	LevelPatchCapResult prepared;
	if (!prepareLevelPatchCaps(*source, request, &prepared, error)) {
		return false;
	}
	QVector<int> ids;
	if (!addLevelMapPatches(document, prepared.caps, source->entityId, &ids, error)) {
		return false;
	}
	for (int i = 0; i < ids.size(); ++i) {
		prepared.caps[i].id = ids[i];
	}
	if (result) {
		*result = std::move(prepared);
	}
	return true;
}

QJsonObject levelPatchCapReportJson(const LevelPatchCapResult &result)
{
	QJsonArray caps, warnings;
	for (qsizetype i = 0; i < result.caps.size(); ++i) {
		const auto &cap = result.caps[i];
		const auto center = result.centers[i];
		caps << QJsonObject{{QStringLiteral("id"), cap.id},
							{QStringLiteral("entity"), cap.entityId},
							{QStringLiteral("boundary"), levelPatchBoundaryId(result.boundaries[i])},
							{QStringLiteral("closed"), result.closed[i]},
							{QStringLiteral("reversed"), result.reversed[i]},
							{QStringLiteral("grid"), QJsonArray{cap.width, cap.height}},
							{QStringLiteral("center"), QJsonArray{center.x, center.y, center.z}},
							{QStringLiteral("material"), cap.textureName}};
	}
	for (const auto &warning : result.warnings) {
		warnings << warning;
	}
	return {{QStringLiteral("caps"), caps}, {QStringLiteral("warnings"), warnings}};
}
} // namespace vibestudio
