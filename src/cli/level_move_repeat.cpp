// map move --camera and map repeat-transforms.
//
// map move --camera x,y,z centres the --object selection on that point by
// Paste to Camera's translation, snapped to --grid (core/level_paste.h), as
// the Levels page's Move to Camera does: one undo step that keeps the
// objects' identities and source lines. map repeat-transforms copies the
// --object selection and applies --scale, --shear, --rotate and --translate
// to the copies about their centre or --pivot, or applies them to the --set
// components, as NetRadiant Custom's Repeat Transforms does
// (core/level_repeat.h).
//
// Workflow reference: NetRadiant Custom radiant/select.cpp MoveToCamera,
// TranslateToCamera and "RepeatTransforms", and radiant/selection.cpp
// RadiantSelectionSystem::repeatTransforms at 449778b3 (GPL-2.0-or-later).
// Original implementation; see docs/CREDITS.md.

#include "cli/level_move_repeat.h"
#include "core/entity_definitions.h"
#include "core/level_brush.h"
#include "core/level_paste.h"
#include "core/level_placement.h"
#include "core/level_repeat.h"
#include "core/level_scene.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include <cmath>

namespace vibestudio::cli {
namespace {
struct Text {
	Q_DECLARE_TR_FUNCTIONS(LevelMoveRepeatCli)
};

QJsonObject vectorJson(const LevelMapVec3& value)
{
	return {{QStringLiteral("x"), value.x}, {QStringLiteral("y"), value.y}, {QStringLiteral("z"), value.z}};
}

// Three finite numbers separated by commas.
bool parseTriple(const QString& text, double* a, double* b, double* c)
{
	const QStringList parts = text.split(QLatin1Char(','));
	if (parts.size() != 3) {
		return false;
	}
	double values[3] = {};
	for (int i = 0; i < 3; ++i) {
		bool ok = false;
		values[i] = parts[i].trimmed().toDouble(&ok);
		if (!ok || !std::isfinite(values[i])) {
			return false;
		}
	}
	*a = values[0];
	*b = values[1];
	*c = values[2];
	return true;
}

int axisIndex(const QString& text)
{
	const QString name = text.trimmed().toLower();
	return name == QLatin1String("x") ? 0 : name == QLatin1String("y") ? 1 : name == QLatin1String("z") ? 2 : -1;
}

// The strict option scan both commands share: known flags and options, no
// repeats except `repeated`, and a value for every option.
struct Parsed {
	QSet<QString> seen;
	QHash<QString, QStringList> values;
	QStringList positional;
	QString error;
};

Parsed parseArguments(const QStringList& arguments, const QString& command, const QSet<QString>& flags, const QSet<QString>& options,
	const QSet<QString>& repeated)
{
	Parsed parsed;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const QString argument = arguments[i];
		if (!argument.startsWith('-') || (argument.size() > 1 && (argument[1].isDigit() || argument[1] == QLatin1Char('.')))) {
			parsed.positional << argument;
			continue;
		}
		const auto equals = argument.indexOf('=');
		const QString name = equals < 0 ? argument : argument.left(equals);
		if (parsed.seen.contains(name) && !repeated.contains(name)) {
			parsed.error = Text::tr("Repeated option: %1.").arg(name);
			return parsed;
		}
		parsed.seen.insert(name);
		if (flags.contains(name)) {
			if (equals >= 0) {
				parsed.error = Text::tr("Flag %1 does not take a value.").arg(name);
				return parsed;
			}
			continue;
		}
		if (!options.contains(name)) {
			parsed.error = Text::tr("Unknown option for %1: %2").arg(command, name);
			return parsed;
		}
		QString value;
		if (equals >= 0) {
			value = argument.mid(equals + 1);
		} else if (i + 1 < arguments.size() && !arguments[i + 1].startsWith(QStringLiteral("--"))) {
			value = arguments[++i];
		}
		if (value.trimmed().isEmpty()) {
			parsed.error = Text::tr("Missing value for %1.").arg(name);
			return parsed;
		}
		parsed.values[name] << value;
	}
	return parsed;
}

bool selectObjects(LevelMapDocument* document, const QStringList& objects, QString* error)
{
	QVector<LevelMapSelectionRef> selection;
	for (const QString& value : objects) {
		for (const QString& selector : value.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
			const QStringList parts = selector.trimmed().split(QLatin1Char(':'));
			bool valid = false;
			const int id = parts.value(1).toInt(&valid);
			const LevelMapSelectionKind kind = parts.size() == 2 ? levelMapSelectionKindFromId(parts.first().trimmed().toLower()) : LevelMapSelectionKind::None;
			if (kind == LevelMapSelectionKind::None || !valid || id < 0) {
				*error = Text::tr("Unknown map object selector: %1").arg(selector.trimmed());
				return false;
			}
			selection.removeAll(LevelMapSelectionRef {kind, id});
			selection << LevelMapSelectionRef {kind, id};
		}
	}
	QString problem;
	if (!setLevelMapSelection(document, selection, &problem)) {
		*error = Text::tr("Unable to select map objects: %1").arg(problem);
		return false;
	}
	return true;
}

bool parseComponents(const QStringList& selections, QVector<LevelMapComponentSet>* sets, QString* error)
{
	static const QRegularExpression pattern(QStringLiteral("^(brush|patch):([0-9]+)/(vertex|edge|face)/([0-9]+(?:,[0-9]+)*)$"));
	if (selections.size() > 256) {
		*error = Text::tr("Repeat --set for at most 256 objects.");
		return false;
	}
	for (const QString& text : selections) {
		const auto match = pattern.match(text);
		bool valid = false;
		const int id = match.captured(2).toInt(&valid);
		if (!match.hasMatch() || !valid) {
			*error = Text::tr("Invalid --set: %1. Use brush:0/face/4,5 or patch:0/vertex/0,1.").arg(text);
			return false;
		}
		LevelMapComponentSet set;
		set.object = {match.captured(1) == QLatin1String("brush") ? LevelMapSelectionKind::QuakeBrush : LevelMapSelectionKind::QuakePatch, id};
		set.kind = match.captured(3) == QLatin1String("vertex") ? LevelBrushComponent::Vertex
			: match.captured(3) == QLatin1String("edge")		  ? LevelBrushComponent::Edge
																  : LevelBrushComponent::Face;
		for (const QString& index : match.captured(4).split(',')) {
			const int component = index.toInt(&valid);
			if (!valid) {
				*error = Text::tr("Component ID is out of range in --set: %1").arg(text);
				return false;
			}
			set.components << component;
		}
		*sets << set;
	}
	return true;
}

bool loadMap(const Parsed& parsed, const QString& path, LevelMapDocument* document, LevelMoveRepeatCliResult* result)
{
	QString error;
	if (!loadLevelMap({QFileInfo(path).absoluteFilePath(), parsed.values.value(QStringLiteral("--map-name")).value(0),
						  parsed.values.value(QStringLiteral("--engine")).value(0)},
			document, &error)) {
		result->exitCode = QFileInfo::exists(path) ? 1 : 3;
		result->error = error;
		return false;
	}
	if (parsed.seen.contains(QStringLiteral("--context"))
		&& !openLevelSceneGroup(document, parsed.values.value(QStringLiteral("--context")).first(), &error)) {
		result->exitCode = 4;
		result->error = error;
		return false;
	}
	return true;
}

void addSave(const LevelMapSaveReport& saved, LevelMoveRepeatCliResult* result)
{
	result->payload.insert(QStringLiteral("output"), saved.outputPath);
	result->payload.insert(QStringLiteral("written"), saved.written);
	result->payload.insert(QStringLiteral("dryRun"), saved.dryRun);
	result->payload.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(saved.warnings));
	result->lines << levelMapSaveReportText(saved);
}

const QSet<QString>& commonOptions()
{
	static const QSet<QString> options {"--input", "--object", "--texture-lock", "--context", "--output", "--engine", "--map-name",
		"--settings-file", "--locale", "--catalog-root"};
	return options;
}

} // namespace

LevelMoveRepeatCliResult runLevelMoveToCamera(const QStringList& arguments)
{
	LevelMoveRepeatCliResult result;
	const auto fail = [&result](int code, const QString& message) {
		result.exitCode = code;
		result.error = message;
		return result;
	};
	const QString command = QStringLiteral("map move");
	QSet<QString> options = commonOptions();
	options += QSet<QString> {"--camera", "--grid", "--definitions"};
	const Parsed parsed = parseArguments(arguments, command,
		{"--cli", "--json", "--quiet", "--verbose", "--overwrite", "--dry-run", "--allow-valve220"}, options, {"--object"});
	if (!parsed.error.isEmpty()) {
		return fail(2, parsed.error);
	}
	if (parsed.positional.size() != (parsed.seen.contains(QStringLiteral("--input")) ? 2 : 3)) {
		return fail(2, Text::tr("map move --camera needs one input map, positionally or with --input, plus --object and --output."));
	}
	const QString path = parsed.seen.contains(QStringLiteral("--input")) ? parsed.values.value(QStringLiteral("--input")).first() : parsed.positional[2];
	const QString output = parsed.values.value(QStringLiteral("--output")).value(0);
	if (output.isEmpty()) {
		return fail(2, Text::tr("map move requires --output <save-as path>."));
	}
	LevelPastePlacement placement;
	placement.mode = LevelPasteMode::Camera;
	placement.point.valid = parseTriple(parsed.values.value(QStringLiteral("--camera")).value(0), &placement.point.x, &placement.point.y, &placement.point.z);
	if (!placement.point.valid) {
		return fail(2, Text::tr("--camera takes the camera position as x,y,z."));
	}
	bool gridOk = true;
	placement.grid = parsed.values.value(QStringLiteral("--grid")).value(0, QStringLiteral("0")).toDouble(&gridOk);
	if (!gridOk || !std::isfinite(placement.grid) || placement.grid < 0 || placement.grid > 32768) {
		return fail(2, Text::tr("--grid takes units from 0 (no snapping) to 32768."));
	}
	const QStringList objects = parsed.values.value(QStringLiteral("--object"));
	if (objects.isEmpty()) {
		return fail(2, Text::tr("map move --camera requires --object kind:id, repeatable."));
	}
	const QString textureLock = parsed.values.value(QStringLiteral("--texture-lock")).value(0, QStringLiteral("on"));
	if (textureLock != QLatin1String("on") && textureLock != QLatin1String("off")) {
		return fail(2, Text::tr("--texture-lock requires on or off."));
	}
	const bool allowValve220 = parsed.seen.contains(QStringLiteral("--allow-valve220"));
	if (allowValve220 && textureLock == QLatin1String("off")) {
		return fail(2, Text::tr("--allow-valve220 requires texture lock."));
	}
	if (parsed.seen.contains(QStringLiteral("--definitions"))) {
		const auto definitions = loadEntityDefinitions({parsed.values.value(QStringLiteral("--definitions")).first()});
		if (!definitions.error.isEmpty() || definitions.isEmpty()) {
			return fail(4, Text::tr("Could not load entity definitions: %1").arg(definitions.error));
		}
		for (const auto& definition : definitions.classes) {
			if (definition.kind == EntityClassKind::Point && definition.hasSize) {
				placement.entityBounds.insert(definition.className.toCaseFolded(),
					{{definition.mins[0], definition.mins[1], definition.mins[2]}, {definition.maxs[0], definition.maxs[1], definition.maxs[2]}});
			}
		}
	}

	LevelMapDocument document;
	if (!loadMap(parsed, path, &document, &result)) {
		return result;
	}
	QString error;
	if (!selectObjects(&document, objects, &error)) {
		return fail(2, error);
	}
	LevelPlacementRequest request;
	request.operation = LevelPlacementOperation::MoveToCamera;
	request.paste = placement;
	request.textures = {textureLock == QLatin1String("on"), allowValve220};
	auto prepared = prepareLevelPlacement(document, request);
	if (!prepared.succeeded) {
		return fail(4, prepared.error);
	}
	document = std::move(prepared.document);
	const LevelMapSaveReport saved = saveLevelMapAs(document, output, parsed.seen.contains(QStringLiteral("--dry-run")), parsed.seen.contains(QStringLiteral("--overwrite")));
	if (!saved.succeeded()) {
		return fail(4, saved.errors.join(QLatin1Char('\n')));
	}
	const LevelMapVec3& delta = prepared.pasteOffset;
	const int moved = static_cast<int>(document.selection.size());
	result.payload = QJsonObject {{QStringLiteral("placement"), QStringLiteral("camera")}, {QStringLiteral("camera"), vectorJson(placement.point)},
		{QStringLiteral("delta"), vectorJson(delta)}, {QStringLiteral("grid"), placement.grid}, {QStringLiteral("objects"), moved},
		{QStringLiteral("textureLockPolicy"), textureLock == QLatin1String("on") ? QStringLiteral("locked") : QStringLiteral("source-parameters")},
		{QStringLiteral("allowValve220"), allowValve220}};
	result.lines << Text::tr("Moved %n object(s) by %1, %2, %3 to centre them on the camera.", nullptr, moved).arg(delta.x).arg(delta.y).arg(delta.z);
	addSave(saved, &result);
	return result;
}

LevelMoveRepeatCliResult runLevelRepeatTransforms(const QStringList& arguments)
{
	LevelMoveRepeatCliResult result;
	const auto fail = [&result](int code, const QString& message) {
		result.exitCode = code;
		result.error = message;
		return result;
	};
	const QString command = QStringLiteral("map repeat-transforms");
	QSet<QString> options = commonOptions();
	options += QSet<QString> {"--set", "--translate", "--rotate", "--scale", "--shear", "--pivot", "--uv-lock"};
	const Parsed parsed = parseArguments(arguments, command,
		{"--cli", "--json", "--quiet", "--verbose", "--overwrite", "--dry-run", "--allow-valve220", "--allow-collapse"}, options,
		{"--object", "--set"});
	if (!parsed.error.isEmpty()) {
		return fail(2, parsed.error);
	}
	if (parsed.positional.size() != (parsed.seen.contains(QStringLiteral("--input")) ? 2 : 3)) {
		return fail(2, Text::tr("map repeat-transforms needs one input map, positionally or with --input, plus --object or --set, a transform and --output."));
	}
	const QString path = parsed.seen.contains(QStringLiteral("--input")) ? parsed.values.value(QStringLiteral("--input")).first() : parsed.positional[2];
	const QString output = parsed.values.value(QStringLiteral("--output")).value(0);
	if (output.isEmpty()) {
		return fail(2, Text::tr("map repeat-transforms requires --output <save-as path>."));
	}

	// The transforms, as NetRadiant Custom's Repeat menu lists them.
	LevelRepeatTransforms transforms;
	const auto value = [&parsed](const char* name) { return parsed.values.value(QLatin1String(name)).value(0); };
	if (parsed.seen.contains(QStringLiteral("--translate"))
		&& !parseTriple(value("--translate"), &transforms.translation[0], &transforms.translation[1], &transforms.translation[2])) {
		return fail(2, Text::tr("--translate takes x,y,z."));
	}
	if (parsed.seen.contains(QStringLiteral("--scale"))
		&& (!parseTriple(value("--scale"), &transforms.scale[0], &transforms.scale[1], &transforms.scale[2])
			|| transforms.scale[0] == 0 || transforms.scale[1] == 0 || transforms.scale[2] == 0)) {
		return fail(2, Text::tr("--scale takes three nonzero factors x,y,z; a negative factor mirrors."));
	}
	if (parsed.seen.contains(QStringLiteral("--rotate"))) {
		const QStringList parts = value("--rotate").split(QLatin1Char(','));
		bool ok = false;
		transforms.rotationAxis = axisIndex(parts.value(0));
		transforms.rotationDegrees = parts.value(1).trimmed().toDouble(&ok);
		if (parts.size() != 2 || transforms.rotationAxis < 0 || !ok || !std::isfinite(transforms.rotationDegrees)) {
			return fail(2, Text::tr("--rotate takes an axis and degrees, such as z,90."));
		}
	}
	if (parsed.seen.contains(QStringLiteral("--shear"))) {
		const QStringList parts = value("--shear").split(QLatin1Char(','));
		bool ok = false;
		transforms.skewAxis = axisIndex(parts.value(0));
		transforms.skewAlong = axisIndex(parts.value(1));
		transforms.skewAmount = parts.value(2).trimmed().toDouble(&ok);
		if (parts.size() != 3 || transforms.skewAxis < 0 || transforms.skewAlong < 0 || transforms.skewAxis == transforms.skewAlong || !ok
			|| !std::isfinite(transforms.skewAmount) || std::abs(transforms.skewAmount) > 1024) {
			return fail(2, Text::tr("--shear takes the axis that slides, the axis it slides by and a factor, such as x,z,0.5."));
		}
	}
	if (transforms.isIdentity()) {
		return fail(2, Text::tr("Name at least one transform with --translate, --rotate, --scale or --shear."));
	}
	LevelMapVec3 pivot;
	if (parsed.seen.contains(QStringLiteral("--pivot"))) {
		pivot.valid = parseTriple(value("--pivot"), &pivot.x, &pivot.y, &pivot.z);
		if (!pivot.valid) {
			return fail(2, Text::tr("--pivot takes x,y,z."));
		}
	}

	const QStringList objects = parsed.values.value(QStringLiteral("--object"));
	const QStringList selections = parsed.values.value(QStringLiteral("--set"));
	if (objects.isEmpty() == selections.isEmpty()) {
		return fail(2, Text::tr("Name objects with --object kind:id, or picked components with --set brush:ID/vertex|edge|face/IDS or patch:ID/vertex/IDS, not both."));
	}
	const bool componentsMode = !selections.isEmpty();
	if (componentsMode && parsed.seen.contains(QStringLiteral("--texture-lock"))) {
		return fail(2, Text::tr("--texture-lock applies to --object copies; components take --uv-lock."));
	}
	if (!componentsMode && (parsed.seen.contains(QStringLiteral("--uv-lock")) || parsed.seen.contains(QStringLiteral("--allow-collapse")))) {
		return fail(2, Text::tr("--uv-lock and --allow-collapse apply only to --set components."));
	}
	const QString textureLock = parsed.values.value(QStringLiteral("--texture-lock")).value(0, QStringLiteral("on"));
	if (textureLock != QLatin1String("on") && textureLock != QLatin1String("off")) {
		return fail(2, Text::tr("--texture-lock requires on or off."));
	}
	const QString uvLock = parsed.values.value(QStringLiteral("--uv-lock")).value(0, QStringLiteral("off"));
	if (uvLock != QLatin1String("off") && uvLock != QLatin1String("on") && uvLock != QLatin1String("fit")) {
		return fail(2, Text::tr("--uv-lock requires off, on or fit."));
	}
	const bool allowValve220 = parsed.seen.contains(QStringLiteral("--allow-valve220"));
	if (allowValve220 && (componentsMode ? uvLock == QLatin1String("off") : textureLock == QLatin1String("off"))) {
		return fail(2, Text::tr("--allow-valve220 requires texture lock or UV lock."));
	}

	LevelPlacementRequest request;
	QString error;
	if (componentsMode) {
		if (!parseComponents(selections, &request.components, &error)) {
			return fail(2, error);
		}
		request.operation = LevelPlacementOperation::TransformComponents;
		request.componentTransform.pivot = pivot;
		request.componentTransform.textureLock = uvLock != QLatin1String("off");
		request.componentTransform.fitUvLock = uvLock == QLatin1String("fit");
		request.componentTransform.allowValve220 = allowValve220;
		request.componentTransform.allowCollapse = parsed.seen.contains(QStringLiteral("--allow-collapse"));
		if (!levelRepeatTransformsComponentRequest(transforms, &request.componentTransform, &error)) {
			return fail(2, error);
		}
	} else {
		request.operation = LevelPlacementOperation::RepeatTransforms;
		request.repeatPivot = pivot;
		request.textures = {textureLock == QLatin1String("on"), allowValve220};
	}

	LevelMapDocument document;
	if (!loadMap(parsed, path, &document, &result)) {
		return result;
	}
	if (!componentsMode && !selectObjects(&document, objects, &error)) {
		return fail(2, error);
	}
	document.repeat.transforms = transforms;
	++document.repeat.revision;
	auto prepared = prepareLevelPlacement(document, request);
	if (!prepared.succeeded) {
		return fail(4, prepared.error);
	}
	document = std::move(prepared.document);
	const LevelMapSaveReport saved = saveLevelMapAs(document, output, parsed.seen.contains(QStringLiteral("--dry-run")), parsed.seen.contains(QStringLiteral("--overwrite")));
	if (!saved.succeeded()) {
		return fail(4, saved.errors.join(QLatin1Char('\n')));
	}
	const QString applied = levelRepeatTransformsDescription(transforms).join(QStringLiteral("; "));
	result.payload = QJsonObject {{QStringLiteral("transforms"), levelRepeatTransformsJson(transforms)},
		{QStringLiteral("mode"), componentsMode ? QStringLiteral("components") : QStringLiteral("objects")},
		{QStringLiteral("textureLockPolicy"), componentsMode ? uvLock : (textureLock == QLatin1String("on") ? QStringLiteral("locked") : QStringLiteral("source-parameters"))},
		{QStringLiteral("allowValve220"), allowValve220}};
	if (componentsMode) {
		const LevelMapComponentReport& report = prepared.componentReport;
		result.payload.insert(QStringLiteral("brushes"), report.brushes);
		result.payload.insert(QStringLiteral("patches"), report.patches);
		result.payload.insert(QStringLiteral("collapsedVertices"), report.collapsedVertices);
		result.payload.insert(QStringLiteral("fittedFaces"), report.fittedFaces);
		if (pivot.valid) {
			result.payload.insert(QStringLiteral("pivot"), vectorJson(pivot));
		}
		result.lines << Text::tr("Repeated %1 on the components of %n object(s).", nullptr, report.brushes + report.patches).arg(applied);
	} else {
		QJsonArray copies;
		for (const LevelMapSelectionRef& ref : document.selection) {
			copies.append(levelMapSelectionRefId(ref));
		}
		result.payload.insert(QStringLiteral("copies"), copies);
		result.payload.insert(QStringLiteral("pivot"), vectorJson(prepared.repeatPivot));
		const int copied = static_cast<int>(copies.size());
		result.lines << Text::tr("Copied %n object(s) and repeated %1 about %2, %3, %4.", nullptr, copied)
			.arg(applied).arg(prepared.repeatPivot.x).arg(prepared.repeatPivot.y).arg(prepared.repeatPivot.z);
	}
	addSave(saved, &result);
	return result;
}
} // namespace vibestudio::cli
