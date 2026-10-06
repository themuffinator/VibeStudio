#include "core/level_patch_stitch.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace vibestudio
{
namespace
{
bool fail(QString *error, const char *message)
{
	if (error) {
		*error = QCoreApplication::translate("LevelPatchStitch", message);
	}
	return false;
}
LevelMapVec3 add(const LevelMapVec3 &a, const LevelMapVec3 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z, true}; }
LevelMapVec3 scale(const LevelMapVec3 &a, double s) { return {a.x * s, a.y * s, a.z * s, true}; }
LevelMapVec3 subtract(const LevelMapVec3 &a, const LevelMapVec3 &b) { return add(a, scale(b, -1)); }
double length(const LevelMapVec3 &a) { return std::hypot(a.x, a.y, a.z); }
bool columns(LevelPatchBoundary e) { return e == LevelPatchBoundary::FirstRow || e == LevelPatchBoundary::LastRow; }
bool validEdge(LevelPatchBoundary e) { return static_cast<int>(e) >= 0 && static_cast<int>(e) <= 3; }
} // namespace
QString levelPatchBoundaryId(LevelPatchBoundary edge)
{
	switch (edge) {
	case LevelPatchBoundary::FirstRow:
		return QStringLiteral("first-row");
	case LevelPatchBoundary::LastRow:
		return QStringLiteral("last-row");
	case LevelPatchBoundary::FirstColumn:
		return QStringLiteral("first-column");
	case LevelPatchBoundary::LastColumn:
		return QStringLiteral("last-column");
	}
	return {};
}
bool parseLevelPatchBoundary(const QString &id, LevelPatchBoundary *edge)
{
	if (!edge) {
		return false;
	}
	for (int i = 0; i < 4; ++i) {
		const auto candidate = static_cast<LevelPatchBoundary>(i);
		if (id == levelPatchBoundaryId(candidate)) {
			*edge = candidate;
			return true;
		}
	}
	return false;
}
QVector<int> levelPatchBoundaryIndices(const LevelMapPatch &patch, LevelPatchBoundary edge, bool inner)
{
	QVector<int> result;
	if (!validEdge(edge) || patch.width < 3 || patch.height < 3 || patch.width > kLevelPatchMaxDimension ||
		patch.height > kLevelPatchMaxDimension) {
		return result;
	}
	if (columns(edge)) {
		const int row = edge == LevelPatchBoundary::FirstRow ? (inner ? 1 : 0) : patch.height - 1 - (inner ? 1 : 0);
		for (int c = 0; c < patch.width; ++c) {
			result << row * patch.width + c;
		}
	} else {
		const int col = edge == LevelPatchBoundary::FirstColumn ? (inner ? 1 : 0) : patch.width - 1 - (inner ? 1 : 0);
		for (int r = 0; r < patch.height; ++r) {
			result << r * patch.width + col;
		}
	}
	return result;
}
bool refineLevelPatch(LevelMapPatch *patch, bool alongColumns, int segments, QString *error)
{
	if (!patch || !validateLevelPatch(*patch, error)) {
		return false;
	}
	const int oldLength = alongColumns ? patch->width : patch->height, oldSegments = (oldLength - 1) / 2;
	if (segments < oldSegments || segments > (kLevelPatchMaxDimension - 1) / 2 || segments % oldSegments != 0) {
		return fail(error,
					QT_TRANSLATE_NOOP("LevelPatchStitch",
									  "Exact patch refinement needs a multiple of the original segment count, within 31 control points."));
	}
	if (segments == oldSegments) {
		return true;
	}
	auto output = *patch;
	output.width = alongColumns ? segments * 2 + 1 : patch->width;
	output.height = alongColumns ? patch->height : segments * 2 + 1;
	output.controlPoints.resize(output.width * output.height);
	output.controlU.resize(output.controlPoints.size());
	output.controlV.resize(output.controlPoints.size());
	const int factor = segments / oldSegments, cross = alongColumns ? patch->height : patch->width;
	for (int line = 0; line < cross; ++line) {
		for (int s = 0; s < segments; ++s) {
			const double t = static_cast<double>(s % factor) / factor, end = static_cast<double>(s % factor + 1) / factor, d = end - t;
			// Restricted quadratic: B(t), B(t) + (end-t) B'(t)/2, B(end).
			// Original implementation; the same weights apply to positions and UVs.
			const double w[3][3]{{(1 - t) * (1 - t), 2 * t * (1 - t), t * t},
								 {(1 - t) * (1 - t) - d * (1 - t), 2 * t * (1 - t) + d * (1 - 2 * t), t * t + d * t},
								 {(1 - end) * (1 - end), 2 * end * (1 - end), end * end}};
			for (int n = 0; n < 3; ++n) {
				const int out = alongColumns ? line * output.width + 2 * s + n : (2 * s + n) * output.width + line;
				LevelMapVec3 p{0, 0, 0, true};
				double u = 0, v = 0;
				for (int k = 0; k < 3; ++k) {
					const int at = alongColumns ? line * patch->width + 2 * (s / factor) + k : (2 * (s / factor) + k) * patch->width + line;
					p = add(p, scale(patch->controlPoints[at], w[n][k]));
					u += patch->controlU[at] * w[n][k];
					v += patch->controlV[at] * w[n][k];
				}
				output.controlPoints[out] = p;
				output.controlU[out] = u;
				output.controlV[out] = v;
			}
		}
	}
	output.definitionDirty = output.geometryDirty = true;
	refreshLevelPatchBounds(&output);
	if (!validateLevelPatch(output, error)) {
		return false;
	}
	*patch = std::move(output);
	return true;
}
bool prepareLevelPatchStitch(const LevelMapPatch &first, const LevelMapPatch &second, const LevelPatchStitchRequest &request,
							 LevelPatchStitchResult *result, QString *error, const std::function<bool()> &cancelled)
{
	if (error) {
		error->clear();
	}
	const auto stopped = [&] { return cancelled && cancelled(); };
	if (stopped()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelPatchStitch", "Patch stitching cancelled."));
	}
	if (!result || !validEdge(request.first) || !validEdge(request.second) || static_cast<int>(request.order) < 0 ||
		static_cast<int>(request.order) > 2 || static_cast<int>(request.target) < 0 || static_cast<int>(request.target) > 2 ||
		static_cast<int>(request.uv) < 0 || static_cast<int>(request.uv) > 3 || !std::isfinite(request.maxDistance) ||
		request.maxDistance < 0 || request.maxDistance > 1048576) {
		return fail(error,
					QT_TRANSLATE_NOOP("LevelPatchStitch", "Choose valid patch boundaries and a maximum gap from 0 to 1048576 map units."));
	}
	if (first.id == second.id || first.entityId != second.entityId) {
		return fail(error, QT_TRANSLATE_NOOP("LevelPatchStitch", "Choose two distinct patches belonging to the same entity."));
	}
	if (!validateLevelPatch(first, error) || !validateLevelPatch(second, error)) {
		return false;
	}
	LevelPatchStitchResult draft;
	draft.first = first;
	draft.second = second;
	const int segments = std::lcm(((columns(request.first) ? first.width : first.height) - 1) / 2,
								  ((columns(request.second) ? second.width : second.height) - 1) / 2);
	if (!refineLevelPatch(&draft.first, columns(request.first), segments, error) ||
		!refineLevelPatch(&draft.second, columns(request.second), segments, error)) {
		return false;
	}
	const auto a = levelPatchBoundaryIndices(draft.first, request.first), b = levelPatchBoundaryIndices(draft.second, request.second);
	const auto ah = levelPatchBoundaryIndices(draft.first, request.first, true),
			   bh = levelPatchBoundaryIndices(draft.second, request.second, true);
	draft.boundaryPoints = static_cast<int>(a.size());
	double forward = 0, reverse = 0;
	for (int i = 0; i < a.size(); ++i) {
		forward += std::pow(length(subtract(draft.first.controlPoints[a[i]], draft.second.controlPoints[b[i]])), 2);
		reverse += std::pow(length(subtract(draft.first.controlPoints[a[i]], draft.second.controlPoints[b[b.size() - 1 - i]])), 2);
	}
	if (request.order == LevelPatchStitchOrder::Automatic && std::abs(forward - reverse) <= 1e-12 * std::max({1.0, forward, reverse})) {
		return fail(error,
					QT_TRANSLATE_NOOP("LevelPatchStitch", "The boundary direction is ambiguous. Choose Forward or Reversed explicitly."));
	}
	draft.reversed =
		request.order == LevelPatchStitchOrder::Reversed || (request.order == LevelPatchStitchOrder::Automatic && reverse < forward);
	for (int i = 0; i < a.size(); ++i) {
		if (stopped()) {
			return fail(error, QT_TRANSLATE_NOOP("LevelPatchStitch", "Patch stitching cancelled."));
		}
		const int j = draft.reversed ? static_cast<int>(b.size()) - 1 - i : i;
		const auto p = draft.first.controlPoints[a[i]], q = draft.second.controlPoints[b[j]];
		const double gap = length(subtract(p, q));
		draft.maxGap = std::max(draft.maxGap, gap);
		if (gap > request.maxDistance + 1e-9) {
			return fail(error,
						QT_TRANSLATE_NOOP("LevelPatchStitch",
										  "The paired boundaries exceed the maximum gap. Choose another edge, direction or gap limit."));
		}
		const auto target = request.target == LevelPatchStitchTarget::First	   ? p
							: request.target == LevelPatchStitchTarget::Second ? q
																			   : scale(add(p, q), 0.5);
		const auto pa = subtract(draft.first.controlPoints[ah[i]], p), qb = subtract(draft.second.controlPoints[bh[j]], q);
		const auto tangent = scale(subtract(pa, qb), 0.5);
		if (request.matchTangents && length(tangent) < 1e-9) {
			return fail(error,
						QT_TRANSLATE_NOOP(
							"LevelPatchStitch",
							"Matching tangents would collapse an adjacent control row. Adjust the patches or turn off tangent matching."));
		}
		const auto newA = add(target, request.matchTangents ? tangent : pa),
				   newB = add(target, request.matchTangents ? scale(tangent, -1) : qb);
		for (const double movement :
			 {length(subtract(target, p)), length(subtract(target, q)), length(subtract(newA, draft.first.controlPoints[ah[i]])),
			  length(subtract(newB, draft.second.controlPoints[bh[j]]))}) {
			draft.maxMovement = std::max(draft.maxMovement, movement);
		}
		draft.first.controlPoints[a[i]] = draft.second.controlPoints[b[j]] = target;
		draft.first.controlPoints[ah[i]] = newA;
		draft.second.controlPoints[bh[j]] = newB;
		if (request.uv != LevelPatchStitchUv::Preserve) {
			for (auto pair :
				 {std::pair{&draft.first.controlU, &draft.second.controlU}, std::pair{&draft.first.controlV, &draft.second.controlV}}) {
				const double x = pair.first->at(a[i]), y = pair.second->at(b[j]);
				const double targetUv = request.uv == LevelPatchStitchUv::First	   ? x
										: request.uv == LevelPatchStitchUv::Second ? y
																				   : (x + y) * 0.5;
				// Move the inner UV handle with its edge to preserve its derivative.
				(*pair.first)[ah[i]] += targetUv - x;
				(*pair.second)[bh[j]] += targetUv - y;
				(*pair.first)[a[i]] = (*pair.second)[b[j]] = targetUv;
			}
		}
	}
	const int firstSubdiv = columns(request.first) ? first.subdivisionsX : first.subdivisionsY;
	const auto winding = [](LevelPatchBoundary edge) {
		return edge == LevelPatchBoundary::FirstRow || edge == LevelPatchBoundary::LastColumn ? 1 : -1;
	};
	if (winding(request.first) != winding(request.second) * (draft.reversed ? 1 : -1)) {
		draft.warnings << QCoreApplication::translate(
			"LevelPatchStitch", "The boundary order reverses surface facing across this join. Review patch facing or invert one patch.");
	}
	const int secondSubdiv = columns(request.second) ? second.subdivisionsX : second.subdivisionsY;
	if (first.fixedSubdivisions != second.fixedSubdivisions || (first.fixedSubdivisions && firstSubdiv != secondSubdiv)) {
		draft.warnings << QCoreApplication::translate(
			"LevelPatchStitch",
			"The boundary curves match, but their fixed/adaptive tessellation settings differ. Review the compiled seam.");
	}
	for (auto *patch : {&draft.first, &draft.second}) {
		patch->definitionDirty = patch->geometryDirty = true;
		refreshLevelPatchBounds(patch);
		if (!validateLevelPatch(*patch, error)) {
			return false;
		}
	}
	*result = std::move(draft);
	return true;
}
bool stitchLevelMapPatches(LevelMapDocument *document, int firstId, int secondId, const LevelPatchStitchRequest &request,
						   LevelPatchStitchResult *result, QString *error)
{
	if (!document || document->format != LevelMapFormat::Quake3Map) {
		return fail(error, QT_TRANSLATE_NOOP("LevelPatchStitch", "Open a Quake III-family map to stitch patches."));
	}
	const auto find = [&](int id) {
		return std::find_if(document->patches.cbegin(), document->patches.cend(), [&](const auto &p) { return p.id == id; });
	};
	const auto first = find(firstId), second = find(secondId);
	if (first == document->patches.cend() || second == document->patches.cend()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelPatchStitch", "A selected patch no longer exists."));
	}
	LevelPatchStitchResult draft;
	if (!prepareLevelPatchStitch(*first, *second, request, &draft, error) ||
		!replaceLevelMapPatches(document, {{firstId, draft.first}, {secondId, draft.second}}, error)) {
		return false;
	}
	if (result) {
		*result = std::move(draft);
	}
	return true;
}
QJsonObject levelPatchStitchReportJson(const LevelPatchStitchResult &result)
{
	return {{QStringLiteral("firstPatch"), result.first.id},
			{QStringLiteral("secondPatch"), result.second.id},
			{QStringLiteral("boundaryPoints"), result.boundaryPoints},
			{QStringLiteral("reversed"), result.reversed},
			{QStringLiteral("maximumGap"), result.maxGap},
			{QStringLiteral("maximumMovement"), result.maxMovement},
			{QStringLiteral("firstGrid"), QJsonArray{result.first.width, result.first.height}},
			{QStringLiteral("secondGrid"), QJsonArray{result.second.width, result.second.height}},
			{QStringLiteral("warnings"), QJsonArray::fromStringList(result.warnings)}};
}
} // namespace vibestudio
