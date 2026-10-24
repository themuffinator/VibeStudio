// map nudge: the Levels page's Nudge Left, Right, Forward and Back, Nudge
// Depth Up and Down, and Nudge Up and Down Along Z. The --direction resolves
// to a world axis in a --view plan view or a camera named by --camera-yaw and
// --camera-pitch (core/level_nudge.h); the --object selection then moves
// --steps grid steps through the same Move placement as the arrow keys, or
// the --set components through the same Transform Components move, as one
// undo step. With --duplicate the objects are copied first and the copies
// move, as Duplicate and Move does (TrenchBroom's Ctrl+arrows). Workflow
// reference: TrenchBroom MapView2D/MapView3D::moveDirection and the
// "Duplicate and move objects" actions at 4c756be1 (GPL-3.0-or-later);
// NetRadiant Custom radiant/select.cpp NudgeSelection and
// Selection_MoveUp/MoveDown at 449778b3 (GPL-2.0-or-later). Original
// implementation; see docs/CREDITS.md.

#include "cli/level_nudge.h"
#include "core/level_nudge.h"
#include "core/level_placement.h"
#include "core/level_scene.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>

#include <cmath>

namespace vibestudio::cli {
namespace {
struct Text {
	Q_DECLARE_TR_FUNCTIONS(LevelNudgeCli)
};

QJsonObject vectorJson(const LevelMapVec3& value)
{
	return {{QStringLiteral("x"), value.x}, {QStringLiteral("y"), value.y}, {QStringLiteral("z"), value.z}};
}
} // namespace

LevelNudgeCliResult runLevelNudge(const QStringList& arguments)
{
	LevelNudgeCliResult result;
	const auto fail = [&result](int code, const QString& message) {
		result.exitCode = code;
		result.error = message;
		return result;
	};
	const QSet<QString> flags {"--cli", "--json", "--quiet", "--verbose", "--overwrite", "--dry-run", "--allow-collapse", "--allow-valve220", "--duplicate"};
	const QSet<QString> options {"--input", "--object", "--set", "--direction", "--view", "--camera-yaw", "--camera-pitch", "--grid", "--steps",
		"--texture-lock", "--uv-lock", "--context", "--output", "--engine", "--map-name", "--settings-file", "--locale", "--catalog-root"};
	const QSet<QString> repeated {"--object", "--set"};
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
			return fail(2, Text::tr("Unknown option for map nudge: %1").arg(name));
		}
		QString value;
		if (equals >= 0) {
			value = argument.mid(equals + 1);
		} else if (i + 1 < arguments.size() && (!arguments[i + 1].startsWith(QStringLiteral("--")))) {
			value = arguments[++i];
		}
		if (value.trimmed().isEmpty()) {
			return fail(2, Text::tr("Missing value for %1.").arg(name));
		}
		values[name] << value;
	}
	if (positional.size() != (seen.contains(QStringLiteral("--input")) ? 2 : 3)) {
		return fail(2, Text::tr("map nudge needs one input map, positionally or with --input, plus --direction and --output."));
	}
	const QString path = seen.contains(QStringLiteral("--input")) ? values.value(QStringLiteral("--input")).first() : positional[2];
	const QString output = values.value(QStringLiteral("--output")).value(0);
	if (output.isEmpty()) {
		return fail(2, Text::tr("map nudge requires --output <save-as path>."));
	}
	LevelNudgeCommand command;
	if (!levelNudgeCommandForName(values.value(QStringLiteral("--direction")).value(0), &command)) {
		return fail(2, Text::tr("map nudge requires --direction %1.").arg(levelNudgeDirectionNames()));
	}

	// The frame: a plan view, or a camera looking along yaw and pitch.
	const bool cameraFrame = seen.contains(QStringLiteral("--camera-yaw")) || seen.contains(QStringLiteral("--camera-pitch"));
	if (cameraFrame && seen.contains(QStringLiteral("--view"))) {
		return fail(2, Text::tr("Name a plan view with --view or a camera with --camera-yaw and --camera-pitch, not both."));
	}
	QString frame = values.value(QStringLiteral("--view")).value(0, QStringLiteral("top")).trimmed().toLower();
	LevelMapVec3 axis;
	if (cameraFrame) {
		bool yawOk = true;
		bool pitchOk = true;
		const double yaw = values.value(QStringLiteral("--camera-yaw")).value(0, QStringLiteral("0")).toDouble(&yawOk);
		const double pitch = values.value(QStringLiteral("--camera-pitch")).value(0, QStringLiteral("0")).toDouble(&pitchOk);
		if (!yawOk || !pitchOk || !std::isfinite(yaw) || !std::isfinite(pitch) || pitch < -90 || pitch > 90) {
			return fail(2, Text::tr("--camera-yaw takes finite degrees and --camera-pitch degrees from -90 to 90."));
		}
		frame = QStringLiteral("camera");
		axis = levelCameraNudgeAxis(yaw, pitch, command.direction);
	} else {
		const QStringList views {QStringLiteral("top"), QStringLiteral("front"), QStringLiteral("side")};
		const auto view = views.indexOf(frame);
		if (view < 0) {
			return fail(2, Text::tr("--view takes top, front or side."));
		}
		axis = levelPlanNudgeAxis(static_cast<MapRenderProjection>(view), command.direction);
	}

	bool gridOk = true;
	const double grid = values.value(QStringLiteral("--grid")).value(0, QStringLiteral("16")).toDouble(&gridOk);
	if (!gridOk || !std::isfinite(grid) || grid <= 0 || grid > 4096) {
		return fail(2, Text::tr("--grid takes units above 0 and at most 4096."));
	}
	bool stepsOk = true;
	const int steps = values.value(QStringLiteral("--steps")).value(0, QStringLiteral("1")).toInt(&stepsOk);
	if (!stepsOk || steps < 1 || steps > 64) {
		return fail(2, Text::tr("--steps takes a whole number from 1 to 64."));
	}
	const LevelMapVec3 delta = levelNudgeDelta(axis, grid, steps);
	if (!delta.valid) {
		return fail(2, Text::tr("The view has no direction to nudge along."));
	}

	const QStringList objects = values.value(QStringLiteral("--object"));
	const QStringList selections = values.value(QStringLiteral("--set"));
	if (objects.isEmpty() == selections.isEmpty()) {
		return fail(2, Text::tr("Name objects with --object kind:id, or picked components with --set brush:ID/vertex|edge|face/IDS or patch:ID/vertex/IDS, not both."));
	}
	const bool componentsMode = !selections.isEmpty();
	const bool duplicate = seen.contains(QStringLiteral("--duplicate"));
	if (componentsMode && duplicate) {
		return fail(2, Text::tr("--duplicate copies --object selections; picked components cannot be copied."));
	}
	if (componentsMode && seen.contains(QStringLiteral("--texture-lock"))) {
		return fail(2, Text::tr("--texture-lock applies to --object moves; components take --uv-lock."));
	}
	if (!componentsMode && (seen.contains(QStringLiteral("--uv-lock")) || seen.contains(QStringLiteral("--allow-collapse")))) {
		return fail(2, Text::tr("--uv-lock and --allow-collapse apply only to --set components."));
	}
	const QString textureLock = values.value(QStringLiteral("--texture-lock")).value(0, QStringLiteral("on"));
	if (textureLock != QLatin1String("on") && textureLock != QLatin1String("off")) {
		return fail(2, Text::tr("--texture-lock requires on or off."));
	}
	const QString uvLock = values.value(QStringLiteral("--uv-lock")).value(0, QStringLiteral("off"));
	if (uvLock != QLatin1String("off") && uvLock != QLatin1String("on") && uvLock != QLatin1String("fit")) {
		return fail(2, Text::tr("--uv-lock requires off, on or fit."));
	}
	const bool allowValve220 = seen.contains(QStringLiteral("--allow-valve220"));
	if (allowValve220 && (componentsMode ? uvLock == QLatin1String("off") : textureLock == QLatin1String("off"))) {
		return fail(2, Text::tr("--allow-valve220 requires texture lock or UV lock."));
	}

	LevelPlacementRequest request;
	if (componentsMode) {
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
				: match.captured(3) == QLatin1String("edge")		  ? LevelBrushComponent::Edge
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
		request.operation = LevelPlacementOperation::TransformComponents;
		request.componentTransform.operation = LevelComponentTransformKind::Move;
		request.componentTransform.delta = delta;
		request.componentTransform.textureLock = uvLock != QLatin1String("off");
		request.componentTransform.fitUvLock = uvLock == QLatin1String("fit");
		request.componentTransform.allowValve220 = allowValve220;
		request.componentTransform.allowCollapse = seen.contains(QStringLiteral("--allow-collapse"));
	} else {
		// Duplicate and Move: one duplicateLevelMapSelection() at the offset.
		request.operation = duplicate ? LevelPlacementOperation::Duplicate : LevelPlacementOperation::Move;
		request.offset = delta;
		request.textures = {textureLock == QLatin1String("on"), allowValve220};
	}

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap({QFileInfo(path).absoluteFilePath(), values.value(QStringLiteral("--map-name")).value(0), values.value(QStringLiteral("--engine")).value(0)},
			&document, &error)) {
		return fail(QFileInfo::exists(path) ? 1 : 3, error);
	}
	if (!levelNudgeAxisSupported(document, axis)) {
		return fail(4, Text::tr("Doom maps nudge along X and Y only; use map edit for sector heights."));
	}
	if (seen.contains(QStringLiteral("--context")) && !openLevelSceneGroup(&document, values.value(QStringLiteral("--context")).first(), &error)) {
		return fail(4, error);
	}
	if (!componentsMode) {
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
				selection.removeAll(LevelMapSelectionRef {kind, id});
				selection << LevelMapSelectionRef {kind, id};
			}
		}
		if (!setLevelMapSelection(&document, selection, &error)) {
			return fail(2, Text::tr("Unable to select map objects: %1").arg(error));
		}
	}
	auto prepared = prepareLevelPlacement(document, request);
	if (!prepared.succeeded) {
		return fail(4, prepared.error);
	}
	document = std::move(prepared.document);
	const LevelMapSaveReport saved = saveLevelMapAs(document, output, seen.contains(QStringLiteral("--dry-run")), seen.contains(QStringLiteral("--overwrite")));
	if (!saved.succeeded()) {
		return fail(4, saved.errors.join(QLatin1Char('\n')));
	}
	const LevelMapComponentReport& report = prepared.componentReport;
	result.payload = QJsonObject {{QStringLiteral("direction"), command.name}, {QStringLiteral("commandId"), command.commandId},
		{QStringLiteral("frame"), frame}, {QStringLiteral("axis"), vectorJson(axis)}, {QStringLiteral("delta"), vectorJson(delta)},
		{QStringLiteral("grid"), grid}, {QStringLiteral("steps"), steps},
		{QStringLiteral("mode"), componentsMode ? QStringLiteral("components") : QStringLiteral("objects")}, {QStringLiteral("duplicate"), duplicate},
		{QStringLiteral("textureLockPolicy"), componentsMode ? uvLock : (textureLock == QLatin1String("on") ? QStringLiteral("locked") : QStringLiteral("source-parameters"))},
		{QStringLiteral("allowValve220"), allowValve220}, {QStringLiteral("objects"), static_cast<int>(document.selection.size())},
		{QStringLiteral("brushes"), report.brushes}, {QStringLiteral("patches"), report.patches},
		{QStringLiteral("collapsedVertices"), report.collapsedVertices}, {QStringLiteral("fittedFaces"), report.fittedFaces},
		{QStringLiteral("output"), saved.outputPath}, {QStringLiteral("written"), saved.written}, {QStringLiteral("dryRun"), saved.dryRun},
		{QStringLiteral("warnings"), QJsonArray::fromStringList(saved.warnings)}};
	const QString offset = Text::tr("%1, %2, %3").arg(delta.x).arg(delta.y).arg(delta.z);
	if (duplicate) {
		QJsonArray copies;
		for (const LevelMapSelectionRef& ref : document.selection) {
			copies.append(levelMapSelectionRefId(ref));
		}
		result.payload.insert(QStringLiteral("copies"), copies);
		const int copied = static_cast<int>(document.selection.size());
		result.lines << Text::tr("Copied %n object(s) and moved the copies %1 by %2.", nullptr, copied).arg(command.name, offset);
	} else if (componentsMode) {
		result.lines << Text::tr("Nudged the components of %n object(s) %1 by %2.", nullptr, report.brushes + report.patches).arg(command.name, offset);
	} else {
		const int moved = static_cast<int>(document.selection.size());
		result.lines << Text::tr("Nudged %n object(s) %1 by %2.", nullptr, moved).arg(command.name, offset);
	}
	result.lines << levelMapSaveReportText(saved);
	return result;
}
} // namespace vibestudio::cli
