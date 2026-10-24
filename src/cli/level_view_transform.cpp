// map view-transform: the Levels page's Roll Clockwise and Counter-clockwise,
// Yaw (Rotate 90° Right and Left), Pitch Clockwise and Counter-clockwise, and
// Flip Horizontal and Vertical. The --transform resolves to one world axis in
// a --view plan view or a camera named by --camera-yaw and --camera-pitch
// (core/level_quick_transform.h); the --object selection then turns or flips
// through the same Rotate and Mirror placements as the shell, as one undo
// step. Workflow reference: TrenchBroom MapViewBase::rotate, rotationAxis and
// flip and MapView2D/MapView3D::flipAxis at 4c756be1 (GPL-3.0-or-later).
// Original implementation; see docs/CREDITS.md.

#include "cli/level_view_transform.h"
#include "core/level_placement.h"
#include "core/level_quick_transform.h"
#include "core/level_scene.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>

#include <cmath>

namespace vibestudio::cli {
namespace {
struct Text {
	Q_DECLARE_TR_FUNCTIONS(LevelViewTransformCli)
};

QJsonObject vectorJson(const LevelMapVec3& value)
{
	return {{QStringLiteral("x"), value.x}, {QStringLiteral("y"), value.y}, {QStringLiteral("z"), value.z}};
}
} // namespace

LevelViewTransformCliResult runLevelViewTransform(const QStringList& arguments)
{
	LevelViewTransformCliResult result;
	const auto fail = [&result](int code, const QString& message) {
		result.exitCode = code;
		result.error = message;
		return result;
	};
	const QSet<QString> flags {"--cli", "--json", "--quiet", "--verbose", "--overwrite", "--dry-run", "--allow-valve220"};
	const QSet<QString> options {"--input", "--object", "--transform", "--view", "--camera-yaw", "--camera-pitch", "--pivot", "--pivot-grid",
		"--texture-lock", "--context", "--output", "--engine", "--map-name", "--settings-file", "--locale", "--catalog-root"};
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
		if (seen.contains(name) && name != QLatin1String("--object")) {
			return fail(2, Text::tr("Repeated option: %1. Only --object may be repeated.").arg(name));
		}
		seen.insert(name);
		if (flags.contains(name)) {
			if (equals >= 0) {
				return fail(2, Text::tr("Flag %1 does not take a value.").arg(name));
			}
			continue;
		}
		if (!options.contains(name)) {
			return fail(2, Text::tr("Unknown option for map view-transform: %1").arg(name));
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
		return fail(2, Text::tr("map view-transform needs one input map, positionally or with --input, plus --transform, --object and --output."));
	}
	const QString path = seen.contains(QStringLiteral("--input")) ? values.value(QStringLiteral("--input")).first() : positional[2];
	const QString output = values.value(QStringLiteral("--output")).value(0);
	if (output.isEmpty()) {
		return fail(2, Text::tr("map view-transform requires --output <save-as path>."));
	}
	LevelViewTransform transform;
	if (!levelViewTransformForName(values.value(QStringLiteral("--transform")).value(0), &transform)) {
		return fail(2, Text::tr("map view-transform requires --transform %1.").arg(levelViewTransformNames()));
	}
	const bool mirror = levelViewTransformMirrors(transform);

	// The frame: a plan view, or a camera looking along yaw and pitch.
	const bool cameraFrame = seen.contains(QStringLiteral("--camera-yaw")) || seen.contains(QStringLiteral("--camera-pitch"));
	if (cameraFrame && seen.contains(QStringLiteral("--view"))) {
		return fail(2, Text::tr("Name a plan view with --view or a camera with --camera-yaw and --camera-pitch, not both."));
	}
	QString frame = values.value(QStringLiteral("--view")).value(0, QStringLiteral("top")).trimmed().toLower();
	LevelAxisTransform resolved;
	bool framed = false;
	if (cameraFrame) {
		bool yawOk = true;
		bool pitchOk = true;
		const double yaw = values.value(QStringLiteral("--camera-yaw")).value(0, QStringLiteral("0")).toDouble(&yawOk);
		const double pitch = values.value(QStringLiteral("--camera-pitch")).value(0, QStringLiteral("0")).toDouble(&pitchOk);
		if (!yawOk || !pitchOk || !std::isfinite(yaw) || !std::isfinite(pitch) || pitch < -90 || pitch > 90) {
			return fail(2, Text::tr("--camera-yaw takes finite degrees and --camera-pitch degrees from -90 to 90."));
		}
		frame = QStringLiteral("camera");
		framed = levelCameraViewTransform(yaw, pitch, transform, &resolved);
	} else {
		const QStringList views {QStringLiteral("top"), QStringLiteral("front"), QStringLiteral("side")};
		const auto view = views.indexOf(frame);
		if (view < 0) {
			return fail(2, Text::tr("--view takes top, front or side."));
		}
		framed = levelPlanViewTransform(static_cast<MapRenderProjection>(view), transform, &resolved);
	}
	if (!framed) {
		return fail(2, Text::tr("The view has no axis to turn or flip about."));
	}

	// The pivot: the exact centre of what moves by default, an explicit point,
	// or the profiles' rounded centre (the grid for turns, half of it for flips).
	if (seen.contains(QStringLiteral("--pivot")) && seen.contains(QStringLiteral("--pivot-grid"))) {
		return fail(2, Text::tr("Use --pivot or --pivot-grid, not both."));
	}
	LevelMapVec3 pivot;
	if (seen.contains(QStringLiteral("--pivot"))) {
		static const QRegularExpression separators(QStringLiteral(R"([,\s]+)"));
		const QStringList parts = values.value(QStringLiteral("--pivot")).first().split(separators, Qt::SkipEmptyParts);
		bool xOk = false;
		bool yOk = false;
		bool zOk = false;
		pivot = {parts.value(0).toDouble(&xOk), parts.value(1).toDouble(&yOk), parts.value(2).toDouble(&zOk), true};
		if (parts.size() != 3 || !xOk || !yOk || !zOk || !std::isfinite(pivot.x) || !std::isfinite(pivot.y) || !std::isfinite(pivot.z)) {
			return fail(2, Text::tr("--pivot takes finite x,y,z coordinates."));
		}
	}
	double pivotGrid = 0;
	if (seen.contains(QStringLiteral("--pivot-grid"))) {
		bool valid = false;
		pivotGrid = values.value(QStringLiteral("--pivot-grid")).first().trimmed().toDouble(&valid);
		if (!valid || !std::isfinite(pivotGrid) || pivotGrid <= 0 || pivotGrid > 65536) {
			return fail(2, Text::tr("--pivot-grid takes a grid size from above 0 to 65536 units."));
		}
	}
	const QString textureLock = values.value(QStringLiteral("--texture-lock")).value(0, QStringLiteral("on"));
	if (textureLock != QLatin1String("on") && textureLock != QLatin1String("off")) {
		return fail(2, Text::tr("--texture-lock requires on or off."));
	}
	const bool allowValve220 = seen.contains(QStringLiteral("--allow-valve220"));
	if (allowValve220 && textureLock == QLatin1String("off")) {
		return fail(2, Text::tr("--allow-valve220 requires texture lock."));
	}
	const QStringList objects = values.value(QStringLiteral("--object"));
	if (objects.isEmpty()) {
		return fail(2, Text::tr("map view-transform requires --object kind:id, repeatable."));
	}
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

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap({QFileInfo(path).absoluteFilePath(), values.value(QStringLiteral("--map-name")).value(0), values.value(QStringLiteral("--engine")).value(0)},
			&document, &error)) {
		return fail(QFileInfo::exists(path) ? 1 : 3, error);
	}
	if (!levelAxisTransformSupported(document, resolved)) {
		return fail(4, Text::tr("Doom maps flip along X or Y and turn about Z only; this view's %1 is world %2.")
			.arg(transform.name, QString(QLatin1Char("XYZ"[resolved.axis]))));
	}
	if (seen.contains(QStringLiteral("--context")) && !openLevelSceneGroup(&document, values.value(QStringLiteral("--context")).first(), &error)) {
		return fail(4, error);
	}
	if (!setLevelMapSelection(&document, selection, &error)) {
		return fail(2, Text::tr("Unable to select map objects: %1").arg(error));
	}
	if (pivotGrid > 0 && !(mirror ? levelMapFlipPivot(document, pivotGrid, &pivot, &error) : levelMapQuickTurnPivot(document, pivotGrid, &pivot, &error))) {
		return fail(4, error);
	}

	LevelPlacementRequest request;
	const LevelMapTextureLockOptions textures {textureLock == QLatin1String("on"), allowValve220};
	if (mirror) {
		request.operation = LevelPlacementOperation::Mirror;
		request.axis = resolved.axis;
		request.mirrorPivot = pivot;
		request.textures = textures;
	} else {
		request.operation = LevelPlacementOperation::Rotate;
		request.rotation.axis = resolved.axis;
		request.rotation.degrees = 90.0 * resolved.quarterTurns;
		request.rotation.pivot = pivot;
		request.rotation.textureLock = textures.enabled;
		request.rotation.allowValve220 = textures.allowValve220;
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
	const QString axis(QLatin1Char("xyz"[resolved.axis]));
	result.payload = QJsonObject {{QStringLiteral("transform"), transform.name}, {QStringLiteral("commandId"), transform.commandId},
		{QStringLiteral("frame"), frame}, {QStringLiteral("axis"), axis}, {QStringLiteral("mirror"), mirror},
		{QStringLiteral("quarterTurns"), resolved.quarterTurns}, {QStringLiteral("degrees"), 90 * resolved.quarterTurns},
		{QStringLiteral("pivotPolicy"), pivotGrid > 0 ? (mirror ? QStringLiteral("half-grid") : QStringLiteral("grid"))
			: pivot.valid ? QStringLiteral("explicit") : QStringLiteral("centre")},
		{QStringLiteral("textureLockPolicy"), document.format == LevelMapFormat::DoomWad ? QStringLiteral("native-offsets")
			: textures.enabled ? QStringLiteral("locked") : QStringLiteral("source-parameters")},
		{QStringLiteral("allowValve220"), allowValve220}, {QStringLiteral("objects"), static_cast<int>(document.selection.size())},
		{QStringLiteral("output"), saved.outputPath}, {QStringLiteral("written"), saved.written}, {QStringLiteral("dryRun"), saved.dryRun},
		{QStringLiteral("warnings"), QJsonArray::fromStringList(saved.warnings)}};
	if (pivot.valid) {
		result.payload.insert(QStringLiteral("pivot"), vectorJson(pivot));
	}
	if (pivotGrid > 0) {
		result.payload.insert(QStringLiteral("pivotGrid"), pivotGrid);
	}
	const int moved = static_cast<int>(document.selection.size());
	if (mirror) {
		result.lines << Text::tr("Flipped %n object(s) along world %1 (%2).", nullptr, moved).arg(axis, transform.name);
	} else {
		const QString degrees = QString::number(90 * resolved.quarterTurns);
		result.lines << Text::tr("Turned %n object(s) %1° about world %2 (%3).", nullptr, moved).arg(degrees, axis, transform.name);
	}
	result.lines << levelMapSaveReportText(saved);
	return result;
}
} // namespace vibestudio::cli
