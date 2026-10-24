// map round-to-grid: Radiant's Snap To Grid. Every brush vertex, patch control
// point and point-entity origin of the --object selection rounds onto the grid
// on its own, or only the --set components, through the same core service as
// the Levels page's Round Selection to Grid. Workflow reference: NetRadiant
// Custom Selection_SnapToGrid (radiant/select.cpp at 449778b3,
// GPL-2.0-or-later). Original implementation; see docs/CREDITS.md.

#include "cli/level_grid_round.h"
#include "core/level_grid_round.h"
#include "core/level_scene.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>

#include <cmath>

namespace vibestudio::cli {
namespace {
struct Text {
	Q_DECLARE_TR_FUNCTIONS(LevelGridRoundCli)
};
} // namespace

LevelGridRoundCliResult runLevelGridRound(const QStringList& arguments)
{
	LevelGridRoundCliResult result;
	const auto fail = [&result](int code, const QString& message) {
		result.exitCode = code;
		result.error = message;
		return result;
	};
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--overwrite", "--dry-run", "--allow-collapse", "--allow-valve220"};
	const QSet<QString> options{"--input", "--object", "--set", "--grid", "--uv-lock", "--context", "--output", "--engine", "--map-name",
		"--settings-file", "--locale", "--catalog-root"};
	const QSet<QString> repeated{"--object", "--set"};
	QSet<QString> seen;
	QHash<QString, QStringList> values;
	QStringList positional;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const QString argument = arguments[i];
		if (!argument.startsWith('-')) {
			positional << argument;
			continue;
		}
		const auto equals = argument.indexOf('=');
		const QString name = equals < 0 ? argument : argument.left(equals);
		if (seen.contains(name) && !repeated.contains(name)) {
			return fail(2, Text::tr("Repeated option: %1. Only --object and --set may be repeated.").arg(name));
		}
		seen.insert(name);
		if (flags.contains(name)) {
			if (equals >= 0) {
				return fail(2, Text::tr("Flag %1 does not take a value.").arg(name));
			}
			continue;
		}
		if (!options.contains(name)) {
			return fail(2, Text::tr("Unknown option for map round-to-grid: %1").arg(name));
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
		values[name] << value;
	}
	if (positional.size() != (seen.contains(QStringLiteral("--input")) ? 2 : 3)) {
		return fail(2, Text::tr("map round-to-grid needs one input map, positionally or with --input, plus --grid and --output."));
	}
	const QString path = seen.contains(QStringLiteral("--input")) ? values.value(QStringLiteral("--input")).first() : positional[2];
	const QString output = values.value(QStringLiteral("--output")).value(0);
	if (output.isEmpty()) {
		return fail(2, Text::tr("map round-to-grid requires --output <save-as path>."));
	}
	LevelMapGridRoundRequest request;
	bool gridOk = false;
	request.grid = values.value(QStringLiteral("--grid")).value(0).toDouble(&gridOk);
	if (!gridOk || !std::isfinite(request.grid) || request.grid <= 0 || request.grid > 4096) {
		return fail(2, Text::tr("map round-to-grid requires --grid <units> above 0 and at most 4096."));
	}
	const QString uvLock = values.value(QStringLiteral("--uv-lock")).value(0, QStringLiteral("off"));
	if (uvLock != QLatin1String("off") && uvLock != QLatin1String("on") && uvLock != QLatin1String("fit")) {
		return fail(2, Text::tr("--uv-lock requires off, on or fit."));
	}
	request.textureLock = uvLock != QLatin1String("off");
	request.fitUvLock = uvLock == QLatin1String("fit");
	request.allowValve220 = seen.contains(QStringLiteral("--allow-valve220"));
	if (request.allowValve220 && !request.textureLock) {
		return fail(2, Text::tr("--allow-valve220 requires --uv-lock on or fit."));
	}
	const QStringList objects = values.value(QStringLiteral("--object"));
	const QStringList selections = values.value(QStringLiteral("--set"));
	if (objects.isEmpty() == selections.isEmpty()) {
		return fail(2, Text::tr("Name objects with --object kind:id, or picked components with --set brush:ID/vertex|edge|face/IDS or patch:ID/vertex/IDS, not both."));
	}
	request.allowCollapse = seen.contains(QStringLiteral("--allow-collapse"));
	if (request.allowCollapse && selections.isEmpty()) {
		return fail(2, Text::tr("--allow-collapse applies only to --set components; whole objects never merge corners."));
	}
	static const QRegularExpression pattern(QStringLiteral("^(brush|patch):([0-9]+)/(vertex|edge|face)/([0-9]+(?:,[0-9]+)*)$"));
	if (selections.size() > 256) {
		return fail(2, Text::tr("Repeat --set for at most 256 objects."));
	}
	for (const QString& text : selections) {
		const auto match = pattern.match(text);
		bool valid = false;
		const int id = match.captured(2).toInt(&valid);
		if (!match.hasMatch() || !valid) {
			return fail(2, Text::tr("Invalid --set: %1. Use brush:0/face/4,5 or patch:0/vertex/0,1.").arg(text));
		}
		LevelMapComponentSet set;
		set.object = {match.captured(1) == QLatin1String("brush") ? LevelMapSelectionKind::QuakeBrush : LevelMapSelectionKind::QuakePatch, id};
		set.kind = match.captured(3) == QLatin1String("vertex") ? LevelBrushComponent::Vertex
			: match.captured(3) == QLatin1String("edge")	   ? LevelBrushComponent::Edge
															   : LevelBrushComponent::Face;
		for (const QString& index : match.captured(4).split(',')) {
			const int component = index.toInt(&valid);
			if (!valid) {
				return fail(2, Text::tr("Component ID is out of range in --set: %1").arg(text));
			}
			set.components << component;
		}
		request.components << set;
	}
	LevelMapDocument document;
	QString error;
	if (!loadLevelMap({QFileInfo(path).absoluteFilePath(), values.value(QStringLiteral("--map-name")).value(0), values.value(QStringLiteral("--engine")).value(0)},
			&document, &error)) {
		return fail(QFileInfo::exists(path) ? 1 : 3, error);
	}
	if (seen.contains(QStringLiteral("--context")) && !openLevelSceneGroup(&document, values.value(QStringLiteral("--context")).first(), &error)) {
		return fail(4, error);
	}
	if (!objects.isEmpty()) {
		QVector<LevelMapSelectionRef> selection;
		for (const QString& value : objects) {
			for (const QString& selector : value.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
				const QStringList parts = selector.trimmed().split(QLatin1Char(':'));
				bool valid = false;
				const int id = parts.value(1).toInt(&valid);
				const LevelMapSelectionKind kind = parts.size() == 2 ? levelMapSelectionKindFromId(parts.first().trimmed().toLower()) : LevelMapSelectionKind::None;
				if (kind == LevelMapSelectionKind::None || !valid || id < 0) {
					return fail(2, Text::tr("Unknown map object selector: %1").arg(selector.trimmed()));
				}
				selection.removeAll(LevelMapSelectionRef{kind, id});
				selection << LevelMapSelectionRef{kind, id};
			}
		}
		if (!setLevelMapSelection(&document, selection, &error)) {
			return fail(2, Text::tr("Unable to select map objects: %1").arg(error));
		}
	}
	LevelMapGridRoundReport report;
	if (!roundLevelMapSelectionToGrid(&document, request, &report, &error)) {
		return fail(4, error);
	}
	const LevelMapSaveReport saved = saveLevelMapAs(document, output, seen.contains(QStringLiteral("--dry-run")), seen.contains(QStringLiteral("--overwrite")));
	if (!saved.succeeded()) {
		return fail(4, saved.errors.join(QLatin1Char('\n')));
	}
	const int objectsRounded = report.brushes + report.patches + report.entities;
	result.payload = QJsonObject{{QStringLiteral("grid"), request.grid},
		{QStringLiteral("mode"), request.components.isEmpty() ? QStringLiteral("objects") : QStringLiteral("components")},
		{QStringLiteral("uvLock"), uvLock}, {QStringLiteral("brushes"), report.brushes}, {QStringLiteral("patches"), report.patches},
		{QStringLiteral("entities"), report.entities}, {QStringLiteral("collapsedVertices"), report.collapsedVertices},
		{QStringLiteral("fittedFaces"), report.fittedFaces}, {QStringLiteral("changed"), report.changed()},
		{QStringLiteral("skipped"), QJsonArray::fromStringList(report.skipped)}, {QStringLiteral("output"), saved.outputPath},
		{QStringLiteral("written"), saved.written}, {QStringLiteral("dryRun"), saved.dryRun},
		{QStringLiteral("warnings"), QJsonArray::fromStringList(saved.warnings)}};
	result.lines << Text::tr("Rounded %n object(s) to grid %1.", nullptr, objectsRounded).arg(request.grid);
	if (report.fittedFaces > 0) {
		result.lines << Text::tr("Fitted the UVs of %n face(s) to three corners.", nullptr, report.fittedFaces);
	}
	for (const QString& reason : std::as_const(report.skipped)) {
		result.lines << Text::tr("Left as it was: %1").arg(reason);
	}
	result.lines << levelMapSaveReportText(saved);
	return result;
}
} // namespace vibestudio::cli
