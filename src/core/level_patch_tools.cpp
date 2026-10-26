/*
 * Patch matrix, texture and thicken operations after GtkRadiant 1.5 and
 * NetRadiant Custom: radiant/patch.cpp (Patch::InsertPoints, RemovePoints,
 * TransposeMatrix, Redisperse, Smooth, NaturalTexture, CapTexture,
 * createThickenedOpposite, createThickenedWall) and radiant/patchmanip.cpp
 * (Patch_thicken), GPL-2.0-or-later, revision
 * 8216133984031afaa9a857b56ea66dd9c3d54b26. The code here is VibeStudio's
 * own: it reproduces those commands' results, not their source. See
 * docs/CREDITS.md.
 */
#include "core/level_patch_tools.h"

#include "core/level_scene_locks.h"
#include "core/level_texture_mapping.h"

#include <QCoreApplication>
#include <QMap>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace vibestudio {
namespace {

bool fail(QString* error, const char* message)
{
	if (error) {
		*error = QCoreApplication::translate("VibeStudioLevelPatch", message);
	}
	return false;
}

struct Control {
	LevelMapVec3 p;
	double u = 0;
	double v = 0;
};

Control mix(const Control& a, const Control& b, double t)
{
	return {{a.p.x + (b.p.x - a.p.x) * t, a.p.y + (b.p.y - a.p.y) * t, a.p.z + (b.p.z - a.p.z) * t, true}, a.u + (b.u - a.u) * t,
		a.v + (b.v - a.v) * t};
}

LevelMapVec3 sub(const LevelMapVec3& a, const LevelMapVec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z, true}; }
LevelMapVec3 plus(const LevelMapVec3& a, const LevelMapVec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z, true}; }
LevelMapVec3 times(const LevelMapVec3& a, double s) { return {a.x * s, a.y * s, a.z * s, true}; }
double dot(const LevelMapVec3& a, const LevelMapVec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
LevelMapVec3 cross(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x, true};
}
double length(const LevelMapVec3& a) { return std::sqrt(dot(a, a)); }

using Rows = QVector<QVector<Control>>;

Rows rowsOf(const LevelMapPatch& patch)
{
	Rows rows(patch.height);
	for (int row = 0; row < patch.height; ++row) {
		rows[row].reserve(patch.width);
		for (int column = 0; column < patch.width; ++column) {
			const int i = row * patch.width + column;
			rows[row] << Control {patch.controlPoints.at(i), patch.controlU.at(i), patch.controlV.at(i)};
		}
	}
	return rows;
}

void setRows(LevelMapPatch* patch, const Rows& rows)
{
	patch->height = static_cast<int>(rows.size());
	patch->width = rows.isEmpty() ? 0 : static_cast<int>(rows.first().size());
	patch->controlPoints.clear();
	patch->controlU.clear();
	patch->controlV.clear();
	for (const auto& row : rows) {
		for (const auto& control : row) {
			patch->controlPoints << control.p;
			patch->controlU << control.u;
			patch->controlV << control.v;
		}
	}
}

LevelMapPatch transposed(const LevelMapPatch& patch)
{
	LevelMapPatch result = patch;
	result.width = patch.height;
	result.height = patch.width;
	for (int row = 0; row < patch.height; ++row) {
		for (int column = 0; column < patch.width; ++column) {
			const int from = row * patch.width + column;
			const int to = column * result.width + row;
			result.controlPoints[to] = patch.controlPoints.at(from);
			result.controlU[to] = patch.controlU.at(from);
			result.controlV[to] = patch.controlV.at(from);
		}
	}
	std::swap(result.subdivisionsX, result.subdivisionsY);
	return result;
}

// Splits the first or last span of every row in two at its middle, exactly
// (de Casteljau at one half), positions and texture coordinates alike.
bool insertColumns(LevelMapPatch* patch, bool atEnd, QString* error)
{
	if (patch->width + 2 > kLevelPatchMaxDimension) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "The patch already has 31 control columns or rows, the most it can have."));
	}
	Rows rows = rowsOf(*patch);
	const int first = atEnd ? patch->width - 3 : 0;
	for (auto& row : rows) {
		const Control a = mix(row[first], row[first + 1], 0.5);
		const Control b = mix(row[first + 1], row[first + 2], 0.5);
		row[first + 1] = a;
		row.insert(first + 2, b);
		row.insert(first + 2, mix(a, b, 0.5));
	}
	setRows(patch, rows);
	return true;
}

// Joins the first or last two spans of every row into one passing through
// the point they shared.
bool deleteColumns(LevelMapPatch* patch, bool atEnd, QString* error)
{
	if (patch->width - 2 < 3) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "The patch has only three control columns or rows, the fewest it can have."));
	}
	Rows rows = rowsOf(*patch);
	const int first = atEnd ? patch->width - 5 : 0;
	for (auto& row : rows) {
		const Control& start = row[first];
		const Control& shared = row[first + 2];
		const Control& end = row[first + 4];
		// The middle control of the joined span that puts its midpoint on the
		// point the two spans shared.
		Control middle;
		middle.p = sub(times(shared.p, 2.0), times(plus(start.p, end.p), 0.5));
		middle.u = shared.u * 2.0 - (start.u + end.u) * 0.5;
		middle.v = shared.v * 2.0 - (start.v + end.v) * 0.5;
		row[first + 1] = middle;
		row.remove(first + 2, 2);
	}
	setRows(patch, rows);
	return true;
}

void redisperseColumns(LevelMapPatch* patch)
{
	Rows rows = rowsOf(*patch);
	for (auto& row : rows) {
		for (int span = 0; span + 2 < row.size(); span += 2) {
			row[span + 1].p = mix(row[span], row[span + 2], 0.5).p;
		}
	}
	setRows(patch, rows);
}

// Whether every row's ends meet, as round a cylinder: Radiant's test.
bool rowsWrap(const LevelMapPatch& patch, double tolerance)
{
	for (int row = 0; row < patch.height; ++row) {
		const LevelMapVec3 gap = sub(patch.controlPoints.at(row * patch.width), patch.controlPoints.at(row * patch.width + patch.width - 1));
		if (dot(gap, gap) > tolerance) {
			return false;
		}
	}
	return true;
}

void smoothColumns(LevelMapPatch* patch)
{
	const bool wrap = patch->width >= 5 && rowsWrap(*patch, 1.0);
	Rows rows = rowsOf(*patch);
	for (auto& row : rows) {
		const int last = static_cast<int>(row.size()) - 1;
		for (int joint = 2; joint < last; joint += 2) {
			row[joint].p = mix(row[joint - 1], row[joint + 1], 0.5).p;
		}
		if (wrap) {
			row[0].p = row[last].p = mix(row[last - 1], row[1], 0.5).p;
		}
	}
	setRows(patch, rows);
}

LevelMapVec3 point(const LevelMapPatch& patch, int row, int column) { return patch.controlPoints.at(row * patch.width + column); }

} // namespace

QString levelPatchMatrixOperationId(LevelPatchMatrixOperation operation)
{
	return levelPatchMatrixOperationIds().value(static_cast<int>(operation));
}

QStringList levelPatchMatrixOperationIds()
{
	return {QStringLiteral("insert-columns"), QStringLiteral("add-columns"), QStringLiteral("insert-rows"), QStringLiteral("add-rows"),
		QStringLiteral("delete-first-columns"), QStringLiteral("delete-last-columns"), QStringLiteral("delete-first-rows"),
		QStringLiteral("delete-last-rows"), QStringLiteral("transpose"), QStringLiteral("invert"), QStringLiteral("redisperse-columns"),
		QStringLiteral("redisperse-rows"), QStringLiteral("smooth-columns"), QStringLiteral("smooth-rows")};
}

bool parseLevelPatchMatrixOperation(const QString& id, LevelPatchMatrixOperation* operation)
{
	const int index = static_cast<int>(levelPatchMatrixOperationIds().indexOf(id.trimmed().toLower()));
	if (index < 0) {
		return false;
	}
	if (operation) {
		*operation = static_cast<LevelPatchMatrixOperation>(index);
	}
	return true;
}

bool applyLevelPatchMatrixOperation(LevelMapPatch* patch, LevelPatchMatrixOperation operation, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!patch || !validateLevelPatch(*patch, error)) {
		return patch ? false : fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "Missing patch destination."));
	}
	LevelMapPatch result = *patch;
	// Row operations are the column operations on the transposed grid.
	const auto byRows = [&result, error](const std::function<bool(LevelMapPatch*, QString*)>& columns) {
		LevelMapPatch turned = transposed(result);
		if (!columns(&turned, error)) {
			return false;
		}
		result = transposed(turned);
		return true;
	};
	bool done = true;
	switch (operation) {
	case LevelPatchMatrixOperation::InsertColumns: done = insertColumns(&result, false, error); break;
	case LevelPatchMatrixOperation::AddColumns: done = insertColumns(&result, true, error); break;
	case LevelPatchMatrixOperation::InsertRows:
		done = byRows([](LevelMapPatch* p, QString* e) { return insertColumns(p, false, e); });
		break;
	case LevelPatchMatrixOperation::AddRows:
		done = byRows([](LevelMapPatch* p, QString* e) { return insertColumns(p, true, e); });
		break;
	case LevelPatchMatrixOperation::DeleteFirstColumns: done = deleteColumns(&result, false, error); break;
	case LevelPatchMatrixOperation::DeleteLastColumns: done = deleteColumns(&result, true, error); break;
	case LevelPatchMatrixOperation::DeleteFirstRows:
		done = byRows([](LevelMapPatch* p, QString* e) { return deleteColumns(p, false, e); });
		break;
	case LevelPatchMatrixOperation::DeleteLastRows:
		done = byRows([](LevelMapPatch* p, QString* e) { return deleteColumns(p, true, e); });
		break;
	case LevelPatchMatrixOperation::Transpose: result = transposed(result); break;
	case LevelPatchMatrixOperation::Invert: done = invertLevelPatch(&result, error); break;
	case LevelPatchMatrixOperation::RedisperseColumns: redisperseColumns(&result); break;
	case LevelPatchMatrixOperation::RedisperseRows:
		done = byRows([](LevelMapPatch* p, QString*) {
			redisperseColumns(p);
			return true;
		});
		break;
	case LevelPatchMatrixOperation::SmoothColumns: smoothColumns(&result); break;
	case LevelPatchMatrixOperation::SmoothRows:
		done = byRows([](LevelMapPatch* p, QString*) {
			smoothColumns(p);
			return true;
		});
		break;
	default: return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "Unknown patch matrix operation."));
	}
	if (!done) {
		return false;
	}
	refreshLevelPatchBounds(&result);
	result.definitionDirty = result.geometryDirty = true;
	if (!validateLevelPatch(result, error)) {
		return false;
	}
	*patch = result;
	return true;
}

LevelMapVec3 levelPatchAverageNormal(const LevelMapPatch& patch)
{
	// Radiant's Calculate_AvgAxes: the rows' and columns' end-to-end spans,
	// summed, with the longest chord standing in for a span that cancels out.
	LevelMapVec3 across {0, 0, 0, true}, down {0, 0, 0, true};
	if (patch.width < 2 || patch.height < 2 || patch.controlPoints.size() != patch.width * patch.height) {
		return {0, 0, 1, true};
	}
	for (int row = 0; row < patch.height; ++row) {
		across = plus(across, sub(point(patch, row, patch.width - 1), point(patch, row, 0)));
	}
	for (int column = 0; column < patch.width; ++column) {
		down = plus(down, sub(point(patch, patch.height - 1, column), point(patch, 0, column)));
	}
	const auto longest = [&patch](bool columns) {
		LevelMapVec3 best {0, 0, 0, true};
		double bestLength = 0;
		for (int row = 0; row + (columns ? 0 : 1) < patch.height; ++row) {
			for (int column = 0; column + (columns ? 1 : 0) < patch.width; ++column) {
				const LevelMapVec3 chord = sub(point(patch, row + (columns ? 0 : 1), column + (columns ? 1 : 0)), point(patch, row, column));
				if (length(chord) - bestLength > 1.0) {
					bestLength = length(chord);
					best = chord;
				}
			}
		}
		return best;
	};
	if (length(across) < 1.0) {
		across = longest(true);
	}
	if (length(down) < 1.0) {
		down = longest(false);
	}
	LevelMapVec3 normal = cross(across, down);
	if (length(normal) < 0.1) {
		return {0, 0, 1, true};
	}
	return times(normal, 1.0 / length(normal));
}

bool naturalLevelPatchTexture(LevelMapPatch* patch, double unitsPerRepeatU, double unitsPerRepeatV, QString* error)
{
	if (!patch || !validateLevelPatch(*patch, error)) {
		return patch ? false : fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "Missing patch destination."));
	}
	if (!std::isfinite(unitsPerRepeatU) || !std::isfinite(unitsPerRepeatV) || unitsPerRepeatU <= 0 || unitsPerRepeatV <= 0) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "A natural patch texture needs a positive size for each repeat."));
	}
	// Each column's U is the last one's plus the longest step any row takes
	// between them; V likewise down the rows, running the way Radiant's does.
	double u = 0;
	for (int column = 0; column < patch->width; ++column) {
		double next = u;
		for (int row = 0; row < patch->height; ++row) {
			patch->controlU[row * patch->width + column] = u;
			if (column + 1 < patch->width) {
				next = std::max(next, u + length(sub(point(*patch, row, column + 1), point(*patch, row, column))) / unitsPerRepeatU);
			}
		}
		u = next;
	}
	double v = 0;
	for (int row = 0; row < patch->height; ++row) {
		double next = v;
		for (int column = 0; column < patch->width; ++column) {
			patch->controlV[row * patch->width + column] = -v;
			if (row + 1 < patch->height) {
				next = std::max(next, v + length(sub(point(*patch, row + 1, column), point(*patch, row, column))) / unitsPerRepeatV);
			}
		}
		v = next;
	}
	patch->definitionDirty = patch->geometryDirty = true;
	return validateLevelPatch(*patch, error);
}

bool planarLevelPatchTexture(LevelMapPatch* patch, double unitsPerRepeatU, double unitsPerRepeatV, QString* error)
{
	if (!patch || !validateLevelPatch(*patch, error)) {
		return patch ? false : fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "Missing patch destination."));
	}
	if (!std::isfinite(unitsPerRepeatU) || !std::isfinite(unitsPerRepeatV) || unitsPerRepeatU <= 0 || unitsPerRepeatV <= 0) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "A projected patch texture needs a positive size for each repeat."));
	}
	const auto [s, t] = levelTexturePrimitiveBasis(levelPatchAverageNormal(*patch));
	for (int i = 0; i < patch->controlPoints.size(); ++i) {
		patch->controlU[i] = dot(patch->controlPoints.at(i), s) / unitsPerRepeatU;
		patch->controlV[i] = dot(patch->controlPoints.at(i), t) / unitsPerRepeatV;
	}
	patch->definitionDirty = patch->geometryDirty = true;
	return validateLevelPatch(*patch, error);
}

// NetRadiant Custom's TextureProjectAxial, Ortho and Cam on a patch
// (Patch::ProjectTexture with Texdef_Construct_local2tex4projection: an axial
// basis for the average normal, or ComputeAxisBase for a direction) at
// 449778b3 (GPL-2.0-or-later). Original code; see docs/CREDITS.md. Its
// direction projection runs V down the second axis for patches but up it for
// Valve 220 faces; here patches take the faces' way, which reads the right
// way round, as Cap Texture does.
bool axialLevelPatchTexture(LevelMapPatch* patch, double unitsPerRepeatU, double unitsPerRepeatV, QString* error)
{
	if (!patch || !validateLevelPatch(*patch, error)) {
		return patch ? false : fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "Missing patch destination."));
	}
	if (!std::isfinite(unitsPerRepeatU) || !std::isfinite(unitsPerRepeatV) || unitsPerRepeatU <= 0 || unitsPerRepeatV <= 0) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "A projected patch texture needs a positive size for each repeat."));
	}
	// A face facing the patch's way takes the same world axes as Natural.
	LevelMapBrushFace facing;
	facing.explicitPlane = true;
	facing.planeNormal = levelPatchAverageNormal(*patch);
	const auto projection = levelNaturalTextureProjection(facing, unitsPerRepeatU, unitsPerRepeatV);
	if (!projection.valid) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "The patch has no direction to project its texture along."));
	}
	for (int i = 0; i < patch->controlPoints.size(); ++i) {
		const QPointF uv = projection.at(patch->controlPoints.at(i));
		patch->controlU[i] = uv.x();
		patch->controlV[i] = uv.y();
	}
	patch->definitionDirty = patch->geometryDirty = true;
	return validateLevelPatch(*patch, error);
}

bool projectedLevelPatchTexture(LevelMapPatch* patch, const LevelMapVec3& direction, double unitsPerRepeatU, double unitsPerRepeatV, QString* error)
{
	if (!patch || !validateLevelPatch(*patch, error)) {
		return patch ? false : fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "Missing patch destination."));
	}
	if (!std::isfinite(unitsPerRepeatU) || !std::isfinite(unitsPerRepeatV) || unitsPerRepeatU <= 0 || unitsPerRepeatV <= 0) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "A projected patch texture needs a positive size for each repeat."));
	}
	const double size = length(direction);
	if (!std::isfinite(size) || size < 1e-9) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "The patch has no direction to project its texture along."));
	}
	const auto [s, t] = levelTexturePrimitiveBasis(times(direction, 1.0 / size));
	for (int i = 0; i < patch->controlPoints.size(); ++i) {
		patch->controlU[i] = dot(patch->controlPoints.at(i), s) / unitsPerRepeatU;
		patch->controlV[i] = dot(patch->controlPoints.at(i), t) / unitsPerRepeatV;
	}
	patch->definitionDirty = patch->geometryDirty = true;
	return validateLevelPatch(*patch, error);
}

bool thickenLevelPatch(const LevelMapPatch& source, const LevelPatchThickenRequest& request, QVector<LevelMapPatch>* created, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!created) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "Missing patch destination."));
	}
	if (!validateLevelPatch(source, error)) {
		return false;
	}
	if (!std::isfinite(request.thickness) || std::abs(request.thickness) < 0.01 || std::abs(request.thickness) > 65536
		|| request.axis < -1 || request.axis > 2) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch",
			"Thicken needs a thickness from 0.01 to 65536 units either way, along the patch's normals or the x, y or z axis."));
	}
	const int width = source.width, height = source.height;
	// Rows closing on themselves need no walls at their ends, and their
	// normals are taken across the seam, as round a cylinder.
	const auto closes = [&source](bool alongRows) {
		const int lines = alongRows ? source.width : source.height;
		for (int line = 0; line < lines; ++line) {
			const LevelMapVec3 a = alongRows ? point(source, 0, line) : point(source, line, 0);
			const LevelMapVec3 b = alongRows ? point(source, source.height - 1, line) : point(source, line, source.width - 1);
			if (dot(sub(a, b), sub(a, b)) > 0.1) {
				return false;
			}
		}
		return true;
	};
	const bool rowsClose = closes(true);
	const bool columnsClose = closes(false);
	// Each control point's offset: along the normal behind the surface (the
	// side the renderer culls), or along the world axis asked for.
	QVector<LevelMapVec3> offsets(width * height);
	QVector<bool> known(width * height, false);
	if (request.axis >= 0) {
		const LevelMapVec3 axis {request.axis == 0 ? 1.0 : 0.0, request.axis == 1 ? 1.0 : 0.0, request.axis == 2 ? 1.0 : 0.0, true};
		offsets.fill(axis);
		known.fill(true);
	} else {
		const auto tangent = [&](int row, int column, bool alongColumns) {
			const int count = alongColumns ? width : height;
			const int at = alongColumns ? column : row;
			const bool wraps = alongColumns ? columnsClose : rowsClose;
			int before = at - 1, after = at + 1;
			if (at == 0) {
				before = wraps ? count - 2 : 0;
			}
			if (at == count - 1) {
				after = wraps ? 1 : count - 1;
			}
			const auto at2 = [&](int index) { return alongColumns ? point(source, row, index) : point(source, index, column); };
			return sub(at2(after), at2(before));
		};
		for (int row = 0; row < height; ++row) {
			for (int column = 0; column < width; ++column) {
				const LevelMapVec3 across = tangent(row, column, true);
				const LevelMapVec3 down = tangent(row, column, false);
				const LevelMapVec3 normal = cross(down, across);
				const double size = length(normal);
				if (size > 1e-9 * std::max(1.0, length(across) * length(down))) {
					offsets[row * width + column] = times(normal, 1.0 / size);
					known[row * width + column] = true;
				}
			}
		}
		// A row or column folded onto one point, as at a sphere's pole, takes
		// the mean of the whole next row's or column's normals, which points
		// along the axis; any other point without a normal takes the mean of
		// its neighbours'.
		const auto collapsed = [&](bool row, int line) {
			const int count = row ? width : height;
			const LevelMapVec3 first = row ? point(source, line, 0) : point(source, 0, line);
			for (int i = 1; i < count; ++i) {
				const LevelMapVec3 other = row ? point(source, line, i) : point(source, i, line);
				if (length(sub(other, first)) > 1e-6) {
					return false;
				}
			}
			return true;
		};
		for (const bool row : {true, false}) {
			const int lines = row ? height : width, count = row ? width : height;
			for (int line = 0; line < lines; ++line) {
				if (!collapsed(row, line)) {
					continue;
				}
				// The fan of triangles from the point round the next line points
				// along the axis however unevenly that line is spaced; the next
				// line's own normals say which way along it.
				LevelMapVec3 sum {0, 0, 0, true};
				LevelMapVec3 fan {0, 0, 0, true};
				const LevelMapVec3 apex = row ? point(source, line, 0) : point(source, 0, line);
				for (const int next : {line - 1, line + 1}) {
					if (next < 0 || next >= lines) {
						continue;
					}
					for (int i = 0; i < count; ++i) {
						const int index = row ? next * width + i : i * width + next;
						if (known[index]) {
							sum = plus(sum, offsets[index]);
						}
						if (i + 1 < count) {
							const int following = row ? next * width + i + 1 : (i + 1) * width + next;
							fan = plus(fan, cross(sub(source.controlPoints.at(index), apex), sub(source.controlPoints.at(following), apex)));
						}
					}
				}
				LevelMapVec3 normal = sum;
				if (length(fan) > 1e-9 && length(sum) > 1e-9) {
					normal = dot(fan, sum) < 0 ? times(fan, -1.0) : fan;
				}
				for (int i = 0; length(normal) > 1e-9 && i < count; ++i) {
					const int index = row ? line * width + i : i * width + line;
					offsets[index] = times(normal, 1.0 / length(normal));
					known[index] = true;
				}
			}
		}
		for (bool progress = true; progress;) {
			progress = false;
			for (int row = 0; row < height; ++row) {
				for (int column = 0; column < width; ++column) {
					if (known[row * width + column]) {
						continue;
					}
					LevelMapVec3 sum {0, 0, 0, true};
					for (int dr = -1; dr <= 1; ++dr) {
						for (int dc = -1; dc <= 1; ++dc) {
							const int r = row + dr, c = column + dc;
							if ((dr != 0 || dc != 0) && r >= 0 && r < height && c >= 0 && c < width && known[r * width + c]) {
								sum = plus(sum, offsets[r * width + c]);
							}
						}
					}
					if (length(sum) > 1e-9) {
						offsets[row * width + column] = times(sum, 1.0 / length(sum));
						known[row * width + column] = true;
						progress = true;
					}
				}
			}
		}
		const LevelMapVec3 behind = times(levelPatchAverageNormal(source), -1.0);
		for (int i = 0; i < offsets.size(); ++i) {
			if (!known[i]) {
				offsets[i] = behind;
			}
		}
	}
	LevelMapPatch opposite = source;
	for (int i = 0; i < opposite.controlPoints.size(); ++i) {
		opposite.controlPoints[i] = plus(source.controlPoints.at(i), times(offsets.at(i), request.thickness));
	}
	opposite.sourceLines.clear();
	opposite.sourceFirstLine = 0;
	opposite.startLine = opposite.endLine = 0;
	opposite.id = -1;
	opposite.definitionDirty = opposite.geometryDirty = true;
	refreshLevelPatchBounds(&opposite);
	const auto flat = [](const LevelMapPatch& patch) {
		const double x = patch.maxs.x - patch.mins.x, y = patch.maxs.y - patch.mins.y, z = patch.maxs.z - patch.mins.z;
		return (x < 0.01 && y < 0.01) || (y < 0.01 && z < 0.01) || (x < 0.01 && z < 0.01);
	};
	QVector<LevelMapPatch> result;
	if (request.seams) {
		// Walls 0 and 1 close the first and last rows, 2 and 3 the first and
		// last columns, each three rows deep from the source edge to the
		// thickened one.
		for (int wall = rowsClose ? 2 : 0; wall < (columnsClose ? 2 : 4); ++wall) {
			const bool rowEdge = wall < 2;
			const int edge = rowEdge ? width : height;
			const int fixed = wall == 0 || wall == 2 ? 0 : (rowEdge ? height - 1 : width - 1);
			const int inward = wall == 0 || wall == 2 ? 1 : -1;
			// An edge folded onto one point, as at a sphere's pole, has no
			// width to wall.
			const LevelMapVec3 start = rowEdge ? point(source, fixed, 0) : point(source, 0, fixed);
			bool folded = true;
			for (int along = 1; along < edge && folded; ++along) {
				folded = length(sub(rowEdge ? point(source, fixed, along) : point(source, along, fixed), start)) <= 1e-6;
			}
			if (folded) {
				continue;
			}
			LevelMapPatch seam = source;
			seam.sourceLines.clear();
			seam.sourceFirstLine = seam.startLine = seam.endLine = 0;
			seam.id = -1;
			seam.width = edge;
			seam.height = 3;
			seam.controlPoints.clear();
			seam.controlU.clear();
			seam.controlV.clear();
			if (source.fixedSubdivisions) {
				seam.subdivisionsX = rowEdge ? source.subdivisionsX : source.subdivisionsY;
				seam.subdivisionsY = 1;
			}
			for (int depth = 0; depth < 3; ++depth) {
				for (int along = 0; along < edge; ++along) {
					const int index = rowEdge ? fixed * width + along : along * width + fixed;
					seam.controlPoints << plus(source.controlPoints.at(index), times(offsets.at(index), request.thickness * depth * 0.5));
					// The texture runs on along the edge, one repeat across.
					seam.controlU << (rowEdge ? source.controlU.at(index) : source.controlV.at(index));
					seam.controlV << depth * 0.5;
				}
			}
			refreshLevelPatchBounds(&seam);
			if (flat(seam)) {
				continue;
			}
			// Face away from the shell: against the step from the edge into
			// the source patch.
			double facing = 0;
			for (int along = 0; along < edge; ++along) {
				const int index = rowEdge ? fixed * width + along : along * width + fixed;
				const int next = rowEdge ? (fixed + inward) * width + along : along * width + fixed + inward;
				const LevelMapVec3 into = sub(source.controlPoints.at(next), source.controlPoints.at(index));
				const int a = std::max(0, along - 1), b = std::min(edge - 1, along + 1);
				const LevelMapVec3 seamAcross = sub(seam.controlPoints.at(edge + b), seam.controlPoints.at(edge + a));
				const LevelMapVec3 seamDown = sub(seam.controlPoints.at(2 * edge + along), seam.controlPoints.at(along));
				facing += dot(cross(seamAcross, seamDown), into);
			}
			if (facing > 0 && !invertLevelPatch(&seam, error)) {
				return false;
			}
			seam.definitionDirty = seam.geometryDirty = true;
			if (!validateLevelPatch(seam, error)) {
				return false;
			}
			result << seam;
		}
	}
	// The thickened copy faces the other way, out of the shell.
	if (!flat(opposite)) {
		if (!invertLevelPatch(&opposite, error) || !validateLevelPatch(opposite, error)) {
			return false;
		}
		result.prepend(opposite);
	}
	if (result.isEmpty()) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "Thickening this patch makes nothing but flat pieces."));
	}
	*created = result;
	return true;
}

QVector<int> levelMapSelectedPatchIds(const LevelMapDocument& document)
{
	QSet<int> patches, entities;
	for (const auto& ref : document.selection) {
		if (ref.kind == LevelMapSelectionKind::QuakePatch) {
			patches.insert(ref.objectId);
		} else if (ref.kind == LevelMapSelectionKind::Entity) {
			entities.insert(ref.objectId);
		}
	}
	QVector<int> result;
	for (const auto& patch : document.patches) {
		if (patches.contains(patch.id) || entities.contains(patch.entityId)) {
			result << patch.id;
		}
	}
	return result;
}

bool applyLevelMapPatchMatrixOperation(LevelMapDocument* document, const QVector<int>& patchIds, LevelPatchMatrixOperation operation,
	int* changed, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!document || document->format != LevelMapFormat::Quake3Map) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "Open a Quake III-family map to edit a patch."));
	}
	if (patchIds.isEmpty() || patchIds.size() > 256) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "Select between 1 and 256 patches."));
	}
	QMap<int, LevelMapPatch> replacements;
	const QSet<int> wanted(patchIds.cbegin(), patchIds.cend());
	for (const auto& patch : std::as_const(document->patches)) {
		if (!wanted.contains(patch.id)) {
			continue;
		}
		LevelMapPatch after = patch;
		QString problem;
		if (!applyLevelPatchMatrixOperation(&after, operation, &problem)) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioLevelPatch", "Patch %1: %2").arg(patch.id).arg(problem);
			}
			return false;
		}
		const bool samePoints = std::equal(after.controlPoints.cbegin(), after.controlPoints.cend(), patch.controlPoints.cbegin(),
			patch.controlPoints.cend(), [](const LevelMapVec3& a, const LevelMapVec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; });
		if (after.width != patch.width || after.height != patch.height || !samePoints || after.controlU != patch.controlU
			|| after.controlV != patch.controlV) {
			replacements.insert(patch.id, after);
		}
	}
	if (replacements.isEmpty() && wanted.size() > 0) {
		for (int id : patchIds) {
			if (std::none_of(document->patches.cbegin(), document->patches.cend(), [id](const auto& patch) { return patch.id == id; })) {
				return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "The patch no longer exists."));
			}
		}
	}
	if (changed) {
		*changed = static_cast<int>(replacements.size());
	}
	return replacements.isEmpty() || replaceLevelMapPatches(document, replacements, error);
}

bool thickenLevelMapPatches(LevelMapDocument* document, const QVector<int>& patchIds, const LevelPatchThickenRequest& request,
	QVector<int>* created, QString* error)
{
	QVector<int> staged;
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
			return thickenLevelMapPatches(candidate, patchIds, request, created ? &staged : nullptr, error);
		});
		guarded.has_value()) {
		if (*guarded && created) {
			*created = staged;
		}
		return *guarded;
	}
	if (error) {
		error->clear();
	}
	if (!document || document->format != LevelMapFormat::Quake3Map) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "Open a Quake III-family map to edit a patch."));
	}
	if (patchIds.isEmpty() || patchIds.size() > 64) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "Select between 1 and 64 patches to thicken."));
	}
	// Each source's pieces join its own entity, as Radiant puts them.
	QMap<int, QVector<LevelMapPatch>> byOwner;
	const QSet<int> wanted(patchIds.cbegin(), patchIds.cend());
	int found = 0;
	for (const auto& patch : std::as_const(document->patches)) {
		if (!wanted.contains(patch.id)) {
			continue;
		}
		++found;
		QVector<LevelMapPatch> pieces;
		QString problem;
		if (!thickenLevelPatch(patch, request, &pieces, &problem)) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioLevelPatch", "Patch %1: %2").arg(patch.id).arg(problem);
			}
			return false;
		}
		byOwner[patch.entityId] += pieces;
	}
	if (found != wanted.size()) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "The patch no longer exists."));
	}
	LevelMapDocument working = *document;
	QVector<int> ids;
	int steps = 0;
	for (auto owner = byOwner.cbegin(); owner != byOwner.cend(); ++owner) {
		for (int start = 0; start < owner.value().size(); start += 64) {
			QVector<int> added;
			if (!addLevelMapPatches(&working, owner.value().mid(start, 64), owner.key(), &added, error)) {
				return false;
			}
			ids += added;
			++steps;
		}
	}
	if (steps > 1
		&& !collapseLevelMapUndoSteps(&working, steps,
			QCoreApplication::translate("VibeStudioLevelPatch", "Thicken %n patch(es)", nullptr, found),
			QCoreApplication::translate("VibeStudioLevelPatch", "Remove the thickened patches"), error)) {
		return false;
	}
	QVector<LevelMapSelectionRef> selection;
	for (int id : std::as_const(ids)) {
		selection << LevelMapSelectionRef {LevelMapSelectionKind::QuakePatch, id};
	}
	setLevelMapSelection(&working, selection);
	*document = std::move(working);
	if (created) {
		*created = ids;
	}
	return true;
}

} // namespace vibestudio
