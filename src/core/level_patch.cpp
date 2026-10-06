#include "core/level_patch.h"

#include <QCoreApplication>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace vibestudio {
namespace {
bool fail(QString* error, const char* message)
{
	if (error) {
		*error = QCoreApplication::translate("VibeStudioLevelPatch", message);
	}
	return false;
}
bool finitePoint(const LevelMapVec3& p)
{
	return p.valid && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::abs(p.x) <= 1048576 &&
	       std::abs(p.y) <= 1048576 && std::abs(p.z) <= 1048576;
}
bool dimension(int n)
{
	return n >= 3 && n <= kLevelPatchMaxDimension && n % 2 == 1;
}
QString number(double n)
{
	return QString::number(n == 0.0 ? 0.0 : n, 'g', 17);
}

QStringList commentsIn(const QString& text)
{
	QStringList result;
	for (qsizetype i = 0; i < text.size();) {
		if (text.at(i) == QLatin1Char('"')) {
			++i;
			while (i < text.size()) {
				if (text.at(i) == QLatin1Char('\\')) {
					i += 2;
				} else if (text.at(i++) == QLatin1Char('"')) {
					break;
				}
			}
		} else if (text.mid(i, 2) == QStringLiteral("//")) {
			const qsizetype end = text.indexOf(QLatin1Char('\n'), i);
			result << text.mid(i, end < 0 ? -1 : end - i);
			i = end < 0 ? text.size() : end + 1;
		} else if (text.mid(i, 2) == QStringLiteral("/*")) {
			const qsizetype end = text.indexOf(QStringLiteral("*/"), i + 2);
			if (end < 0) {
				break;
			}
			result << text.mid(i, end + 2 - i);
			i = end + 2;
		} else {
			++i;
		}
	}
	return result;
}
} // namespace

QString levelPatchMaterialToken(const QString& material, bool patchDef3)
{
	QString name = material.trimmed();
	if (!patchDef3 && name.startsWith(QStringLiteral("textures/"), Qt::CaseInsensitive)) {
		name.remove(0, 9);
	}
	return name;
}

bool validateLevelPatch(const LevelMapPatch& patch, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!patch.controlGridNormalized || !dimension(patch.width) || !dimension(patch.height)) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch",
		                                     "A patch needs an odd, rectangular control grid of 3 to 31 points on each axis."));
	}
	const int count = patch.width * patch.height;
	if (patch.controlPoints.size() != count || patch.controlU.size() != count || patch.controlV.size() != count) {
		return fail(error,
		            QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "Every patch control point needs a position and two texture coordinates."));
	}
	for (int i = 0; i < count; ++i) {
		if (!finitePoint(patch.controlPoints.at(i)) || !std::isfinite(patch.controlU.at(i)) || !std::isfinite(patch.controlV.at(i)) ||
		    std::abs(patch.controlU.at(i)) > 1048576 || std::abs(patch.controlV.at(i)) > 1048576) {
			return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch",
			                                     "Patch positions and texture coordinates must be finite and within ±1048576."));
		}
	}
	if (patch.textureName.isEmpty() || patch.textureName.size() > 1024 || patch.textureName.contains(QStringLiteral("//")) ||
	    patch.textureName.contains(QStringLiteral("/*"))) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "A patch needs a valid shader or texture name."));
	}
	for (QChar ch : patch.textureName) {
		if (ch.isSpace() || !ch.isPrint() || QStringLiteral("\"{}()[]\\").contains(ch)) {
			return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch",
			                                     "A patch texture name cannot contain whitespace, quotes, delimiters or backslashes."));
		}
	}
	if (patch.fixedSubdivisions &&
	    (patch.subdivisionsX < 1 || patch.subdivisionsX > 64 || patch.subdivisionsY < 1 || patch.subdivisionsY > 64)) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "Fixed patch subdivision counts must be between 1 and 64."));
	}
	if (patch.headerTail.size() > 16) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "The patch header has too many extension fields."));
	}
	for (double field : patch.headerTail) {
		if (!std::isfinite(field)) {
			return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "Patch header fields must be finite."));
		}
	}
	return true;
}

void refreshLevelPatchBounds(LevelMapPatch* patch)
{
	if (!patch) {
		return;
	}
	patch->mins = {};
	patch->maxs = {};
	for (const auto& p : patch->controlPoints) {
		if (!patch->mins.valid) {
			patch->mins = patch->maxs = p;
		} else {
			patch->mins.x = std::min(patch->mins.x, p.x);
			patch->maxs.x = std::max(patch->maxs.x, p.x);
			patch->mins.y = std::min(patch->mins.y, p.y);
			patch->maxs.y = std::max(patch->maxs.y, p.y);
			patch->mins.z = std::min(patch->mins.z, p.z);
			patch->maxs.z = std::max(patch->maxs.z, p.z);
		}
	}
}

bool createLevelPatch(const LevelPatchCreateRequest& request, LevelMapPatch* patch, QString* error)
{
	if (!patch) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "Missing patch destination."));
	}
	if (!finitePoint(request.center) || !finitePoint(request.size) || request.size.x < 1 || request.size.y < 1 || request.size.z < 1) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch",
		                                     "Patch sizes must be positive and the center must contain finite coordinates."));
	}
	if (request.shape != QStringLiteral("plane") && request.shape != QStringLiteral("cylinder") &&
	    request.shape != QStringLiteral("cone")) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "Patch shape must be plane, cylinder or cone."));
	}
	if (request.plane != QStringLiteral("xy") && request.plane != QStringLiteral("xz") && request.plane != QStringLiteral("yz")) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "Patch plane must be xy, xz or yz."));
	}
	LevelMapPatch built;
	built.width = request.shape == QStringLiteral("plane") ? request.columns : 9;
	built.height = request.rows;
	built.textureName = levelPatchMaterialToken(request.texture, false);
	built.controlGridNormalized = true;
	built.definitionDirty = true;
	if (!dimension(built.width) || !dimension(built.height)) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "Patch grid dimensions must be odd numbers from 3 to 31."));
	}
	constexpr double ringX[9]{1, 1, 0, -1, -1, -1, 0, 1, 1};
	constexpr double ringY[9]{0, 1, 1, 1, 0, -1, -1, -1, 0};
	for (int row = 0; row < built.height; ++row) {
		const double v = static_cast<double>(row) / (built.height - 1);
		for (int col = 0; col < built.width; ++col) {
			const double u = static_cast<double>(col) / (built.width - 1);
			LevelMapVec3 p = request.center;
			if (request.shape == QStringLiteral("plane")) {
				if (request.plane == QStringLiteral("xy")) {
					p.x += (u - 0.5) * request.size.x;
					p.y += (v - 0.5) * request.size.y;
				} else if (request.plane == QStringLiteral("xz")) {
					p.x += (u - 0.5) * request.size.x;
					p.z += (v - 0.5) * request.size.z;
				} else {
					p.y += (u - 0.5) * request.size.y;
					p.z += (v - 0.5) * request.size.z;
				}
			} else {
				const double taper = request.shape == QStringLiteral("cone") ? 1.0 - v : 1.0;
				p.x += ringX[col] * request.size.x * 0.5 * taper;
				p.y += ringY[col] * request.size.y * 0.5 * taper;
				p.z += (v - 0.5) * request.size.z;
			}
			built.controlPoints << p;
			built.controlU << u;
			built.controlV << v;
		}
	}
	refreshLevelPatchBounds(&built);
	if (!validateLevelPatch(built, error)) {
		return false;
	}
	*patch = built;
	return true;
}

QStringList levelPatchDefinition(const LevelMapPatch& patch)
{
	QStringList lines{QStringLiteral("{")};
	lines.append(commentsIn(patch.sourceLines.join(QLatin1Char('\n'))));
	lines << (patch.fixedSubdivisions ? QStringLiteral("patchDef3") : QStringLiteral("patchDef2")) << QStringLiteral("{")
	      << patch.textureName;
	QStringList header{QString::number(patch.width), QString::number(patch.height)};
	if (patch.fixedSubdivisions) {
		header << QString::number(patch.subdivisionsX) << QString::number(patch.subdivisionsY);
	}
	for (double field : patch.headerTail) {
		header << number(field);
	}
	lines << QStringLiteral("( %1 )").arg(header.join(QLatin1Char(' '))) << QStringLiteral("(");
	for (int col = 0; col < patch.width; ++col) {
		QStringList points;
		for (int row = 0; row < patch.height; ++row) {
			const int i = row * patch.width + col;
			const auto p = patch.controlPoints.value(i);
			points << QStringLiteral("( %1 %2 %3 %4 %5 )")
			              .arg(number(p.x), number(p.y), number(p.z), number(patch.controlU.value(i)), number(patch.controlV.value(i)));
		}
		lines << QStringLiteral("( %1 )").arg(points.join(QLatin1Char(' ')));
	}
	lines << QStringLiteral(")") << QStringLiteral("}") << QStringLiteral("}");
	return lines;
}

bool moveLevelPatchPoints(LevelMapPatch* patch, const QVector<int>& points, const LevelMapVec3& delta, double grid, QString* error)
{
	if (!patch || !validateLevelPatch(*patch, error)) {
		return false;
	}
	if (points.isEmpty() || !finitePoint(delta) || !std::isfinite(grid) || grid < 0) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch",
		                                     "Select control points and enter a finite movement and non-negative grid size."));
	}
	LevelMapPatch result = *patch;
	QSet<int> moved;
	for (int i : points) {
		if (i < 0 || i >= result.controlPoints.size()) {
			return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "A selected patch control point does not exist."));
		}
		if (moved.contains(i)) {
			continue;
		}
		moved.insert(i);
		auto& p = result.controlPoints[i];
		p = snapLevelMapPosition({p.x + delta.x, p.y + delta.y, p.z + delta.z, true}, grid);
	}
	if (!validateLevelPatch(result, error)) {
		return false;
	}
	refreshLevelPatchBounds(&result);
	result.definitionDirty = result.geometryDirty = true;
	*patch = result;
	return true;
}

bool subdivideLevelPatch(LevelMapPatch* patch, bool columns, QString* error)
{
	if (!patch || !validateLevelPatch(*patch, error)) {
		return false;
	}
	const int length = columns ? patch->width : patch->height;
	if (length * 2 - 1 > kLevelPatchMaxDimension) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelPatch", "Subdividing this axis would exceed 31 control points."));
	}
	LevelMapPatch result = *patch;
	result.width = columns ? length * 2 - 1 : patch->width;
	result.height = columns ? patch->height : length * 2 - 1;
	result.controlPoints.resize(result.width * result.height);
	result.controlU.resize(result.controlPoints.size());
	result.controlV.resize(result.controlPoints.size());
	const int cross = columns ? patch->height : patch->width;
	for (int line = 0; line < cross; ++line) {
		for (int segment = 0; segment < (length - 1) / 2; ++segment) {
			const auto source = [&](int n) {
				return columns ? line * patch->width + segment * 2 + n : (segment * 2 + n) * patch->width + line;
			};
			const double weights[5][3]{{1, 0, 0}, {0.5, 0.5, 0}, {0.25, 0.5, 0.25}, {0, 0.5, 0.5}, {0, 0, 1}};
			for (int n = 0; n < 5; ++n) {
				const int out = columns ? line * result.width + segment * 4 + n : (segment * 4 + n) * result.width + line;
				LevelMapVec3 p{0, 0, 0, true};
				double u = 0, v = 0;
				for (int k = 0; k < 3; ++k) {
					const int in = source(k);
					const double w = weights[n][k];
					const auto q = patch->controlPoints.at(in);
					p.x += q.x * w;
					p.y += q.y * w;
					p.z += q.z * w;
					u += patch->controlU.at(in) * w;
					v += patch->controlV.at(in) * w;
				}
				result.controlPoints[out] = p;
				result.controlU[out] = u;
				result.controlV[out] = v;
			}
		}
	}
	result.definitionDirty = result.geometryDirty = true;
	refreshLevelPatchBounds(&result);
	*patch = result;
	return true;
}

bool invertLevelPatch(LevelMapPatch* patch, QString* error)
{
	if (!patch || !validateLevelPatch(*patch, error)) {
		return false;
	}
	for (int row = 0; row < patch->height; ++row) {
		for (int col = 0; col < patch->width / 2; ++col) {
			const int a = row * patch->width + col, b = row * patch->width + patch->width - 1 - col;
			std::swap(patch->controlPoints[a], patch->controlPoints[b]);
			std::swap(patch->controlU[a], patch->controlU[b]);
			std::swap(patch->controlV[a], patch->controlV[b]);
		}
	}
	patch->definitionDirty = patch->geometryDirty = true;
	return true;
}
} // namespace vibestudio
