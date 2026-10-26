// map edit-uv: one brush face's winding or one patch's control points in
// texture space, listed or edited through the same core service as the
// Levels UV Editor. Workflow reference: NetRadiant Custom's TexTool plugin
// (plugins/textool/TexTool.cpp and ControlPointsManager.cpp at 449778b3,
// GPL-2.0-or-later). Original implementation; see docs/CREDITS.md.

#include "cli/level_uv_edit.h"

#include "core/level_materials.h"
#include "core/level_scene.h"
#include "core/level_uv_edit.h"
#include "core/package_archive.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QLocale>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace vibestudio::cli {
namespace {
struct Text {
	Q_DECLARE_TR_FUNCTIONS(LevelUvEditCli)
};

bool parsePair(const QString& text, QPointF* point)
{
	const QStringList parts = text.split(QLatin1Char(','));
	if (parts.size() != 2) {
		return false;
	}
	bool xOk = false, yOk = false;
	const double x = QLocale::c().toDouble(parts[0].trimmed(), &xOk), y = QLocale::c().toDouble(parts[1].trimmed(), &yOk);
	if (!xOk || !yOk || !std::isfinite(x) || !std::isfinite(y)) {
		return false;
	}
	*point = {x, y};
	return true;
}

QString formatId(LevelUvFormat format)
{
	switch (format) {
	case LevelUvFormat::Classic:
		return QStringLiteral("classic");
	case LevelUvFormat::Valve220:
		return QStringLiteral("valve220");
	case LevelUvFormat::BrushPrimitive:
		return QStringLiteral("brush-primitive");
	case LevelUvFormat::Patch:
		return QStringLiteral("patch");
	}
	return {};
}

QString fitId(LevelUvFit fit)
{
	switch (fit) {
	case LevelUvFit::Unchanged:
		return QStringLiteral("unchanged");
	case LevelUvFit::Exact:
		return QStringLiteral("exact");
	case LevelUvFit::Converted:
		return QStringLiteral("converted");
	case LevelUvFit::Approximated:
		return QStringLiteral("approximated");
	}
	return {};
}

QJsonArray pointsJson(const LevelUvLayout& layout, const QVector<QPointF>& uv)
{
	QJsonArray rows;
	for (qsizetype i = 0; i < uv.size() && i < layout.points.size(); ++i) {
		const LevelMapVec3& world = layout.points[i];
		QJsonObject row{{QStringLiteral("point"), static_cast<int>(i + 1)},
			{QStringLiteral("world"), QJsonArray{world.x, world.y, world.z}}, {QStringLiteral("texel"), QJsonArray{uv[i].x(), uv[i].y()}}};
		if (layout.columns > 0) {
			row.insert(QStringLiteral("row"), static_cast<int>(i / layout.columns + 1));
			row.insert(QStringLiteral("column"), static_cast<int>(i % layout.columns + 1));
		}
		if (layout.imageSize.width() > 0 && layout.imageSize.height() > 0) {
			row.insert(QStringLiteral("repeat"), QJsonArray{uv[i].x() / layout.imageSize.width(), uv[i].y() / layout.imageSize.height()});
		}
		rows.append(row);
	}
	return rows;
}
} // namespace

LevelUvEditCliResult runLevelUvEdit(const QStringList& arguments)
{
	LevelUvEditCliResult result;
	const auto fail = [&result](int code, const QString& message) {
		result.exitCode = code;
		result.error = message;
		return result;
	};
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--overwrite", "--dry-run", "--allow-valve220"};
	const QSet<QString> options{"--input", "--target", "--move-point", "--to", "--offset", "--rotate", "--anchor", "--points", "--texture-size",
		"--package", "--palette", "--context", "--output", "--engine", "--map-name", "--settings-file", "--locale", "--catalog-root"};
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const QString argument = arguments[i];
		// Negative numbers (--rotate -15, --offset -8,0) are values.
		if (!argument.startsWith(QStringLiteral("--"))) {
			positional << argument;
			continue;
		}
		const auto equals = argument.indexOf('=');
		const QString name = equals < 0 ? argument : argument.left(equals);
		if (seen.contains(name)) {
			return fail(2, Text::tr("Repeated option: %1.").arg(name));
		}
		seen.insert(name);
		if (flags.contains(name)) {
			if (equals >= 0) {
				return fail(2, Text::tr("Flag %1 does not take a value.").arg(name));
			}
			continue;
		}
		if (!options.contains(name)) {
			return fail(2, Text::tr("Unknown option for map edit-uv: %1").arg(name));
		}
		QString value;
		if (equals >= 0) {
			value = argument.mid(equals + 1);
		} else if (i + 1 < arguments.size() && !arguments[i + 1].startsWith(QStringLiteral("--"))) {
			value = arguments[++i];
		}
		if (value.trimmed().isEmpty()) {
			return fail(2, Text::tr("Missing value for %1.").arg(name));
		}
		values.insert(name, value.trimmed());
	}
	if (positional.size() != (seen.contains(QStringLiteral("--input")) ? 2 : 3) || !seen.contains(QStringLiteral("--target"))) {
		return fail(2, Text::tr("map edit-uv needs one input map, positionally or with --input, and --target face:brushId:faceNumber or patch:id; "
								"changes also need --output."));
	}
	const QString path = seen.contains(QStringLiteral("--input")) ? values.value(QStringLiteral("--input")) : positional[2];
	LevelMaterialTarget target;
	QString error;
	if (!parseLevelMaterialTarget(values.value(QStringLiteral("--target")), &target, &error)
		|| (target.kind != LevelMaterialKind::BrushFace && target.kind != LevelMaterialKind::Patch)) {
		return fail(2, Text::tr("--target takes face:brushId:faceNumber (face numbers start at one) or patch:id."));
	}
	const int changes = static_cast<int>(seen.contains(QStringLiteral("--move-point"))) + static_cast<int>(seen.contains(QStringLiteral("--offset")))
		+ static_cast<int>(seen.contains(QStringLiteral("--rotate")));
	if (changes > 1) {
		return fail(2, Text::tr("Give one change: --move-point with --to, --offset or --rotate."));
	}
	const bool changing = changes == 1;
	const QString output = values.value(QStringLiteral("--output"));
	if (changing && output.isEmpty()) {
		return fail(2, Text::tr("Changing texture coordinates requires --output <save-as path>; use --dry-run to check the change."));
	}
	if (!changing
		&& (!output.isEmpty() || seen.contains(QStringLiteral("--dry-run")) || seen.contains(QStringLiteral("--overwrite"))
			|| seen.contains(QStringLiteral("--allow-valve220")) || seen.contains(QStringLiteral("--to")) || seen.contains(QStringLiteral("--anchor"))
			|| seen.contains(QStringLiteral("--points")))) {
		return fail(2, Text::tr("--output, --dry-run, --overwrite, --allow-valve220, --to, --anchor and --points go with a change: --move-point, "
								"--offset or --rotate."));
	}
	if (seen.contains(QStringLiteral("--move-point")) != seen.contains(QStringLiteral("--to"))) {
		return fail(2, Text::tr("--move-point N and --to U,V go together."));
	}
	if (seen.contains(QStringLiteral("--anchor")) && !seen.contains(QStringLiteral("--rotate"))) {
		return fail(2, Text::tr("--anchor names the point --rotate turns about."));
	}
	if (target.kind == LevelMaterialKind::Patch && seen.contains(QStringLiteral("--allow-valve220"))) {
		return fail(2, Text::tr("--allow-valve220 is for classic brush faces; patches keep their own coordinates."));
	}
	if (target.kind == LevelMaterialKind::BrushFace && seen.contains(QStringLiteral("--points"))) {
		return fail(2, Text::tr("--points chooses patch control points; a face's texture moves as a whole."));
	}
	if (seen.contains(QStringLiteral("--points")) && seen.contains(QStringLiteral("--move-point"))) {
		return fail(2, Text::tr("--points goes with --offset or --rotate; --move-point moves one point."));
	}
	QSize explicitSize;
	if (seen.contains(QStringLiteral("--texture-size"))) {
		QPointF size;
		if (!parsePair(values.value(QStringLiteral("--texture-size")), &size) || size.x() < 1 || size.y() < 1 || size.x() > 16384 || size.y() > 16384
			|| std::floor(size.x()) != size.x() || std::floor(size.y()) != size.y()) {
			return fail(2, Text::tr("--texture-size takes whole width,height values from 1 through 16384."));
		}
		explicitSize = QSize(static_cast<int>(size.x()), static_cast<int>(size.y()));
	}

	LevelMapDocument document;
	if (!loadLevelMap({QFileInfo(path).absoluteFilePath(), values.value(QStringLiteral("--map-name")), values.value(QStringLiteral("--engine"))},
			&document, &error)) {
		return fail(QFileInfo::exists(path) ? 1 : 3, error);
	}
	if (seen.contains(QStringLiteral("--context")) && !openLevelSceneGroup(&document, values.value(QStringLiteral("--context")), &error)) {
		return fail(4, error);
	}
	if (const QString problem = levelUvTargetProblem(document, target); !problem.isEmpty()) {
		return fail(4, problem);
	}

	// The material's image size: explicit, or from the package.
	QSize imageSize = explicitSize;
	QString sizeSource = explicitSize.isValid() ? QStringLiteral("explicit") : QStringLiteral("none");
	if (!imageSize.isValid() && seen.contains(QStringLiteral("--package"))) {
		PackageArchive archive;
		if (!archive.load(values.value(QStringLiteral("--package")), &error)) {
			return fail(3, error.isEmpty() ? Text::tr("Unable to open the package.") : error);
		}
		LevelMapDocument assets = document;
		assets.entities.clear();
		assets.brushes.removeIf([&](const LevelMapBrush& brush) { return target.kind != LevelMaterialKind::BrushFace || brush.id != target.objectId; });
		assets.patches.removeIf([&](const LevelMapPatch& patch) { return target.kind != LevelMaterialKind::Patch || patch.id != target.objectId; });
		LevelPreviewAssetOptions assetOptions;
		assetOptions.paletteId = values.value(QStringLiteral("--palette"));
		const QHash<QString, QSize> sizes = levelPreviewTextureSizes(resolveLevelPreviewAssets(assets, archive, assetOptions));
		QString material;
		for (const LevelMapBrush& brush : std::as_const(assets.brushes)) {
			material = brush.faces.value(target.faceIndex).textureName;
		}
		for (const LevelMapPatch& patch : std::as_const(assets.patches)) {
			material = patch.textureName;
		}
		imageSize = sizes.value(material.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/')).toCaseFolded());
		if (imageSize.isValid()) {
			sizeSource = QStringLiteral("package");
		}
	}
	LevelUvLayout layout;
	if (!readLevelUvLayout(document, target, imageSize, &layout, &error)) {
		return fail(4, error + QLatin1Char(' ') + Text::tr("Use --texture-size W,H or --package."));
	}
	result.payload.insert(QStringLiteral("target"), levelMaterialTargetId(target));
	result.payload.insert(QStringLiteral("format"), formatId(layout.format));
	result.payload.insert(QStringLiteral("material"), layout.material);
	result.payload.insert(QStringLiteral("imageSize"),
		imageSize.isValid() ? QJsonValue(QJsonArray{imageSize.width(), imageSize.height()}) : QJsonValue(QJsonValue::Null));
	result.payload.insert(QStringLiteral("imageSizeSource"), sizeSource);
	if (layout.columns > 0) {
		result.payload.insert(QStringLiteral("columns"), layout.columns);
		result.payload.insert(QStringLiteral("rows"), layout.rows);
	}
	const int count = static_cast<int>(layout.uv.size());

	if (!changing) {
		result.payload.insert(QStringLiteral("mapWritten"), false);
		result.payload.insert(QStringLiteral("points"), pointsJson(layout, layout.uv));
		result.lines << Text::tr("%1 (%2, %3): %n point(s) in texture space, in texels.", nullptr, count)
							.arg(levelMaterialTargetId(target), formatId(layout.format), layout.material);
		for (int i = 0; i < count; ++i) {
			result.lines << Text::tr("Point %1: %2, %3").arg(i + 1).arg(layout.uv[i].x(), 0, 'g', 10).arg(layout.uv[i].y(), 0, 'g', 10);
		}
		return result;
	}

	const auto pointNumber = [&](const QString& option, int* index) {
		bool ok = false;
		const int number = values.value(option).toInt(&ok);
		if (!ok || number < 1 || number > count) {
			return false;
		}
		*index = number - 1;
		return true;
	};
	QVector<int> chosen;
	if (seen.contains(QStringLiteral("--points"))) {
		for (const QString& token : values.value(QStringLiteral("--points")).split(QLatin1Char(','), Qt::KeepEmptyParts)) {
			bool ok = false;
			const int number = token.trimmed().toInt(&ok);
			if (!ok || number < 1 || number > count) {
				return fail(2, Text::tr("--points takes point numbers from 1 to %1, separated by commas.").arg(count));
			}
			if (!chosen.contains(number - 1)) {
				chosen.append(number - 1);
			}
		}
	}
	LevelUvEdit edit;
	edit.allowValve220 = seen.contains(QStringLiteral("--allow-valve220"));
	QString operation;
	if (seen.contains(QStringLiteral("--move-point"))) {
		operation = QStringLiteral("move-point");
		int point = -1;
		QPointF to;
		if (!pointNumber(QStringLiteral("--move-point"), &point)) {
			return fail(2, Text::tr("--move-point takes a point number from 1 to %1.").arg(count));
		}
		if (!parsePair(values.value(QStringLiteral("--to")), &to)) {
			return fail(2, Text::tr("--to takes the point's new texel coordinates as U,V."));
		}
		if (layout.isPatch()) {
			edit.patchUv = levelUvPatchDrag(layout.uv, {point}, point, to);
		} else {
			edit.transform = levelUvFaceDrag(layout.uv, point, -1, to);
		}
	} else if (seen.contains(QStringLiteral("--offset"))) {
		operation = QStringLiteral("offset");
		QPointF delta;
		if (!parsePair(values.value(QStringLiteral("--offset")), &delta)) {
			return fail(2, Text::tr("--offset takes texels to add as U,V."));
		}
		const LevelUvTransform move = LevelUvTransform::translation(delta);
		if (layout.isPatch()) {
			edit.patchUv = levelUvPatchTransformed(layout.uv, chosen, move);
		} else {
			edit.transform = move;
		}
	} else {
		operation = QStringLiteral("rotate");
		bool ok = false;
		const double degrees = QLocale::c().toDouble(values.value(QStringLiteral("--rotate")), &ok);
		if (!ok || !std::isfinite(degrees) || std::abs(degrees) > 360000) {
			return fail(2, Text::tr("--rotate takes degrees, clockwise as the UV Editor draws texture space."));
		}
		int anchor = -1;
		if (seen.contains(QStringLiteral("--anchor")) && !pointNumber(QStringLiteral("--anchor"), &anchor)) {
			return fail(2, Text::tr("--anchor takes a point number from 1 to %1.").arg(count));
		}
		// About the anchor, or else the centre of the points that turn.
		QPointF pivot = layout.centre();
		if (anchor >= 0) {
			pivot = layout.uv[anchor];
		} else if (!chosen.isEmpty()) {
			pivot = {};
			for (const int index : std::as_const(chosen)) {
				pivot += layout.uv[index];
			}
			pivot /= static_cast<double>(chosen.size());
		}
		const LevelUvTransform turn = LevelUvTransform::rotation(pivot, degrees);
		if (layout.isPatch()) {
			edit.patchUv = levelUvPatchTransformed(layout.uv, chosen, turn);
		} else {
			edit.transform = turn;
			edit.anchor = anchor;
		}
		result.payload.insert(QStringLiteral("pivot"), QJsonArray{pivot.x(), pivot.y()});
	}
	LevelSurfaceEditPlan plan;
	LevelUvFit fit = LevelUvFit::Unchanged;
	if (!prepareLevelUvEdit(document, layout, edit, &plan, &error, &fit) || !commitLevelSurfaceEdit(&document, plan, &error)) {
		return fail(4, error);
	}
	LevelUvLayout after;
	if (!readLevelUvLayout(document, target, imageSize, &after, &error)) {
		return fail(4, error);
	}
	const LevelMapSaveReport saved = saveLevelMapAs(document, output, seen.contains(QStringLiteral("--dry-run")), seen.contains(QStringLiteral("--overwrite")));
	if (!saved.succeeded()) {
		return fail(4, saved.errors.join(QLatin1Char('\n')));
	}
	result.payload.insert(QStringLiteral("operation"), operation);
	result.payload.insert(QStringLiteral("fit"), fitId(fit));
	result.payload.insert(QStringLiteral("changed"), fit != LevelUvFit::Unchanged);
	result.payload.insert(QStringLiteral("points"), pointsJson(after, after.uv));
	result.payload.insert(QStringLiteral("mapWritten"), saved.written);
	result.payload.insert(QStringLiteral("output"), saved.outputPath);
	result.payload.insert(QStringLiteral("written"), saved.written);
	result.payload.insert(QStringLiteral("dryRun"), saved.dryRun);
	result.payload.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(saved.warnings));
	switch (fit) {
	case LevelUvFit::Unchanged:
		result.lines << Text::tr("The texture coordinates already read that way; nothing changed.");
		break;
	case LevelUvFit::Exact:
		result.lines << Text::tr("Edited the texture coordinates of %1 in one undo step.").arg(levelMaterialTargetId(target));
		break;
	case LevelUvFit::Converted:
		result.lines << Text::tr("Edited the texture coordinates of %1 in one undo step; the classic face became a Valve 220 face to hold the turn.")
							.arg(levelMaterialTargetId(target));
		break;
	case LevelUvFit::Approximated:
		result.lines << Text::tr("Edited the texture coordinates of %1 in one undo step, as nearly as a classic face can hold the turn. Use "
								 "--allow-valve220 to keep it exactly.")
							.arg(levelMaterialTargetId(target));
		break;
	}
	result.lines << levelMapSaveReportText(saved);
	return result;
}
} // namespace vibestudio::cli
