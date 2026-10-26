#include "cli/level_hollow_options.h"
#include "core/level_clipper.h"
#include "core/level_material_paint.h"
#include "core/level_scene.h"
#include "core/studio_settings.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>

#include <cmath>

namespace vibestudio::cli {
namespace { struct Text { Q_DECLARE_TR_FUNCTIONS(LevelHollowCli) }; }

bool levelHollowOptionsFromArgs(const LevelMapDocument& document, bool caulk, bool caulkTextureGiven, const QString& caulkTexture,
	const QStringList& openFaceSelectors, LevelMapHollowOptions* options, QString* error)
{
	if (error) { error->clear(); }
	const auto fail = [error](const QString& message) { if (error) { *error = message; } return false; };
	if (!options) { return fail(Text::tr("No hollow options to fill.")); }
	LevelMapHollowOptions result;
	if (caulkTextureGiven) {
		result.caulkTexture = caulkTexture.trimmed();
		if (!validLevelClipTexture(result.caulkTexture)) {
			return fail(Text::tr("--caulk-texture needs a material name without spaces, quotes, braces, brackets, parentheses, backslashes or comments."));
		}
	} else if (caulk) {
		result.caulkTexture = levelMapCaulkTexture(document);
		if (result.caulkTexture.isEmpty()) {
			return fail(Text::tr("This map's game has no caulk material; name one with --caulk-texture."));
		}
	}
	// The brushes --object reaches: brushes, and those of selected entities.
	QSet<int> entities, reached;
	for (const LevelMapSelectionRef& ref : document.selection) {
		if (ref.kind == LevelMapSelectionKind::QuakeBrush) { reached.insert(ref.objectId); }
		else if (ref.kind == LevelMapSelectionKind::Entity) { entities.insert(ref.objectId); }
	}
	QHash<int, int> faceCounts;
	for (const LevelMapBrush& brush : document.brushes) {
		if (entities.contains(brush.entityId)) { reached.insert(brush.id); }
		faceCounts.insert(brush.id, static_cast<int>(brush.faces.size()));
	}
	for (const QString& selector : openFaceSelectors) {
		LevelMaterialTarget target;
		if (!parseLevelMaterialTarget(selector, &target) || target.kind != LevelMaterialKind::BrushFace) {
			return fail(Text::tr("--open-face requires face:brushId:faceNumber with a one-based face number, as map select-textured prints it."));
		}
		if (!reached.contains(target.objectId)) {
			return fail(Text::tr("--open-face %1 is not on a brush that --object selects.").arg(selector.trimmed()));
		}
		if (target.faceIndex < 0 || target.faceIndex >= faceCounts.value(target.objectId)) {
			return fail(Text::tr("Brush %1 has no face %2 to leave open.").arg(target.objectId).arg(target.faceIndex + 1));
		}
		const LevelSurfaceFace face {target.objectId, target.faceIndex};
		if (!result.openFaces.contains(face)) { result.openFaces.append(face); }
	}
	*options = std::move(result);
	return true;
}

void addLevelHollowDetails(const LevelMapHollowOptions& options, const LevelMapHollowReport& report, QJsonObject* details)
{
	if (!details) { return; }
	QJsonArray open;
	for (const LevelSurfaceFace& face : options.openFaces) {
		open.append(levelMaterialTargetId({LevelMaterialKind::BrushFace, face.brushId, face.faceIndex}));
	}
	details->insert(QStringLiteral("caulkTexture"), options.caulkTexture);
	details->insert(QStringLiteral("caulkedFaces"), report.caulkedFaces);
	details->insert(QStringLiteral("wallCount"), report.walls);
	details->insert(QStringLiteral("openFaces"), open);
	details->insert(QStringLiteral("openFaceCount"), report.openFaces);
	details->insert(QStringLiteral("keepInner"), options.keepInner);
	details->insert(QStringLiteral("innerKept"), report.innerKept);
}

LevelRoomOptionsCliResult runLevelRoomOptions(const QStringList& arguments)
{
	const auto fail = [](const QString& message) { return LevelRoomOptionsCliResult {2, message, {}, {}}; };
	const QSet<QString> flags {QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--quiet"), QStringLiteral("--verbose"),
		QStringLiteral("--reset"), QStringLiteral("--dry-run")};
	const QSet<QString> valued {QStringLiteral("--set"), QStringLiteral("--settings-file"), QStringLiteral("--locale"), QStringLiteral("--catalog-root")};
	QSet<QString> seen;
	QStringList positional;
	QJsonObject changes;
	// arguments[0] is the program, as for the other editor commands.
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const QString& argument = arguments.at(i);
		if (!argument.startsWith(QLatin1Char('-'))) {
			positional << argument;
			continue;
		}
		const qsizetype equal = argument.indexOf(QLatin1Char('='));
		const QString key = equal < 0 ? argument : argument.left(equal);
		if (seen.contains(key) && key != QLatin1String("--set")) {
			return fail(Text::tr("%1 was given more than once.").arg(key));
		}
		seen.insert(key);
		if (flags.contains(key) && equal < 0) {
			continue;
		}
		if (!valued.contains(key)) {
			return fail(Text::tr("editor make-room does not take %1.").arg(key));
		}
		QString value;
		if (equal >= 0) {
			value = argument.mid(equal + 1);
		} else if (i + 1 < arguments.size() && !arguments.at(i + 1).startsWith(QStringLiteral("--"))) {
			value = arguments.at(++i);
		}
		if (value.isEmpty()) {
			return fail(Text::tr("%1 needs a value.").arg(key));
		}
		if (key != QLatin1String("--set")) {
			continue;
		}
		const qsizetype split = value.indexOf(QLatin1Char('='));
		const QString field = value.left(split);
		const QString choice = value.mid(split + 1);
		if (split <= 0 || changes.contains(field)) {
			return fail(Text::tr("Each --set needs its own field=value: caulk=on|off, caulkTexture=NAME or openPickedFaces=on|off."));
		}
		if (field == QLatin1String("caulk") || field == QLatin1String("openPickedFaces")) {
			if (choice != QLatin1String("on") && choice != QLatin1String("off")) {
				return fail(Text::tr("%1 takes on or off.").arg(field));
			}
			changes.insert(field, choice == QLatin1String("on"));
		} else {
			// caulkTexture; an empty name means the game's own caulk, and an
			// unknown field fails in updateLevelRoomPreferences().
			changes.insert(field, choice);
		}
	}
	const bool reset = seen.contains(QStringLiteral("--reset"));
	const bool dryRun = seen.contains(QStringLiteral("--dry-run"));
	const bool writing = reset || !changes.isEmpty();
	if (positional.size() != 2 || (reset && !changes.isEmpty()) || (dryRun && !writing)) {
		return fail(Text::tr("Use editor make-room to see Make Room's options, --set field=value to change them, or --reset. --dry-run previews a change."));
	}
	StudioSettings reader(StudioSettings::AccessMode::ReadOnly);
	LevelRoomPreferences preferences = reset ? LevelRoomPreferences {} : reader.levelRoomPreferences();
	QString error;
	if (!updateLevelRoomPreferences(changes, &preferences, &error)) {
		return fail(error);
	}
	if (writing && !dryRun) {
		StudioSettings writer;
		if (!writer.setLevelRoomPreferences(preferences)) {
			return {1, Text::tr("Unable to save the Make Room options."), {}, {}};
		}
	}
	const QJsonObject values = levelRoomPreferencesJson(preferences);
	LevelRoomOptionsCliResult result;
	result.payload = {{QStringLiteral("values"), values}, {QStringLiteral("applied"), writing && !dryRun}, {QStringLiteral("dryRun"), dryRun}};
	const QString on = QStringLiteral("on");
	const QString off = QStringLiteral("off");
	result.lines << QStringLiteral("caulk = %1").arg(preferences.caulk ? on : off);
	result.lines << (preferences.caulkTexture.isEmpty() ? Text::tr("caulkTexture = (the game's own caulk)")
														: QStringLiteral("caulkTexture = %1").arg(preferences.caulkTexture));
	result.lines << QStringLiteral("openPickedFaces = %1").arg(preferences.openPickedFaces ? on : off);
	if (dryRun) {
		result.lines << Text::tr("Dry run: nothing was saved.");
	}
	return result;
}

QStringList levelHollowDetailLines(const LevelMapHollowOptions& options, const LevelMapHollowReport& report)
{
	QStringList lines;
	if (!options.caulkTexture.isEmpty()) {
		const int caulked = report.caulkedFaces;
		lines << Text::tr("Caulked %n wall face(s) with %1.", nullptr, caulked).arg(options.caulkTexture);
	}
	if (report.openFaces > 0) {
		const int open = report.openFaces;
		lines << Text::tr("Left %n face(s) open.", nullptr, open);
	}
	if (report.innerKept > 0) {
		const int inner = report.innerKept;
		lines << Text::tr("Kept %n inner brush(es).", nullptr, inner);
	}
	return lines;
}
bool levelCsgExcludedFacesFromArgs(const LevelMapDocument& document, const QStringList& excludeSelectors, const QStringList& onlySelectors,
	bool facingGiven, const QString& facing, QVector<LevelSurfaceFace>* excluded, QString* error)
{
	if (error) { error->clear(); }
	const auto fail = [error](const QString& message) { if (error) { *error = message; } return false; };
	if (!excluded) { return fail(Text::tr("No faces to fill.")); }
	QHash<int, int> faceCounts;
	const QVector<int> reached = levelCsgReachedBrushIds(document);
	for (const LevelMapBrush& brush : document.brushes) {
		if (reached.contains(brush.id)) { faceCounts.insert(brush.id, static_cast<int>(brush.faces.size())); }
	}
	const auto parse = [&](const QString& option, const QString& selector, LevelSurfaceFace* face) {
		LevelMaterialTarget target;
		if (!parseLevelMaterialTarget(selector, &target) || target.kind != LevelMaterialKind::BrushFace) {
			return fail(Text::tr("%1 requires face:brushId:faceNumber with a one-based face number, as map select-textured prints it.").arg(option));
		}
		if (!faceCounts.contains(target.objectId)) {
			return fail(Text::tr("%1 %2 is not on a brush that --object selects.").arg(option, selector.trimmed()));
		}
		if (target.faceIndex < 0 || target.faceIndex >= faceCounts.value(target.objectId)) {
			return fail(Text::tr("Brush %1 has no face %2.").arg(target.objectId).arg(target.faceIndex + 1));
		}
		*face = {target.objectId, target.faceIndex};
		return true;
	};
	QVector<LevelSurfaceFace> result;
	const auto add = [&result](const LevelSurfaceFace& face) {
		if (!result.contains(face)) { result << face; }
	};
	for (const QString& selector : excludeSelectors) {
		LevelSurfaceFace face;
		if (!parse(QStringLiteral("--exclude-face"), selector, &face)) { return false; }
		add(face);
	}
	if (!onlySelectors.isEmpty()) {
		QVector<LevelSurfaceFace> only;
		for (const QString& selector : onlySelectors) {
			LevelSurfaceFace face;
			if (!parse(QStringLiteral("--only-face"), selector, &face)) { return false; }
			only << face;
		}
		for (const LevelSurfaceFace& face : levelCsgExcludedFaces(document, LevelCsgFacePolicy::OnlyPicked, only)) { add(face); }
	}
	if (facingGiven) {
		const QString key = facing.trimmed().toLower();
		LevelMapVec3 axis {0.0, 0.0, 0.0, true};
		bool valid = true;
		if (key == QLatin1String("x")) {
			axis.x = 1.0;
		} else if (key == QLatin1String("y")) {
			axis.y = 1.0;
		} else if (key == QLatin1String("z")) {
			axis.z = 1.0;
		} else {
			const QStringList parts = key.split(QLatin1Char(','));
			valid = parts.size() == 3;
			double* values[] {&axis.x, &axis.y, &axis.z};
			for (int index = 0; valid && index < 3; ++index) {
				*values[index] = parts.at(index).trimmed().toDouble(&valid);
				valid = valid && std::isfinite(*values[index]);
			}
			valid = valid && (axis.x != 0.0 || axis.y != 0.0 || axis.z != 0.0);
		}
		if (!valid) { return fail(Text::tr("--exclude-facing takes x, y, z or a direction dx,dy,dz.")); }
		for (const LevelSurfaceFace& face : levelMapFacesFacingAxis(document, axis)) { add(face); }
	}
	*excluded = std::move(result);
	return true;
}

LevelRoomOptionsCliResult runLevelBrushOffset(const QStringList& arguments, bool shrink)
{
	const QString name = shrink ? QStringLiteral("map shrink-brushes") : QStringLiteral("map expand-brushes");
	LevelRoomOptionsCliResult result;
	const auto fail = [&result](int code, const QString& message) {
		result.exitCode = code;
		result.error = message;
		return result;
	};
	const QSet<QString> flags {QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--quiet"), QStringLiteral("--verbose"),
		QStringLiteral("--overwrite"), QStringLiteral("--dry-run")};
	const QSet<QString> valued {QStringLiteral("--input"), QStringLiteral("--object"), QStringLiteral("--thickness"), QStringLiteral("--exclude-face"),
		QStringLiteral("--only-face"), QStringLiteral("--exclude-facing"), QStringLiteral("--context"), QStringLiteral("--output"),
		QStringLiteral("--engine"), QStringLiteral("--map-name"), QStringLiteral("--settings-file"), QStringLiteral("--locale"),
		QStringLiteral("--catalog-root")};
	const QSet<QString> repeated {QStringLiteral("--object"), QStringLiteral("--exclude-face"), QStringLiteral("--only-face")};
	QSet<QString> seen;
	QHash<QString, QStringList> values;
	QStringList positional;
	// arguments[0] is the program.
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const QString& argument = arguments.at(i);
		if (!argument.startsWith(QLatin1Char('-'))) {
			positional << argument;
			continue;
		}
		const qsizetype equals = argument.indexOf(QLatin1Char('='));
		const QString key = equals < 0 ? argument : argument.left(equals);
		if (seen.contains(key) && !repeated.contains(key)) {
			return fail(2, Text::tr("%1 was given more than once.").arg(key));
		}
		seen.insert(key);
		if (flags.contains(key)) {
			if (equals >= 0) { return fail(2, Text::tr("Flag %1 does not take a value.").arg(key)); }
			continue;
		}
		if (!valued.contains(key)) {
			return fail(2, Text::tr("%1 does not take %2.").arg(name, key));
		}
		QString value;
		if (equals >= 0) {
			value = argument.mid(equals + 1);
		} else if (i + 1 < arguments.size() && !arguments.at(i + 1).startsWith(QStringLiteral("--"))) {
			value = arguments.at(++i);
		}
		if (value.trimmed().isEmpty()) {
			return fail(2, Text::tr("%1 needs a value.").arg(key));
		}
		values[key] << value;
	}
	if (positional.size() != (seen.contains(QStringLiteral("--input")) ? 2 : 3)) {
		return fail(2, Text::tr("%1 needs one input map, positionally or with --input, plus --object, --thickness and --output.").arg(name));
	}
	const QString path = seen.contains(QStringLiteral("--input")) ? values.value(QStringLiteral("--input")).first() : positional.at(2);
	const QString output = values.value(QStringLiteral("--output")).value(0);
	if (output.trimmed().isEmpty()) {
		return fail(2, Text::tr("%1 requires --output <save-as path>.").arg(name));
	}
	bool thicknessOk = false;
	const double thickness = values.value(QStringLiteral("--thickness")).value(0).trimmed().toDouble(&thicknessOk);
	if (!thicknessOk || !std::isfinite(thickness) || thickness <= 0.0 || thickness > kLevelCsgMaximumThickness) {
		return fail(2, Text::tr("%1 requires --thickness <units> above 0 and at most 4096.").arg(name));
	}
	QVector<LevelMapSelectionRef> selection;
	for (const QString& value : values.value(QStringLiteral("--object"))) {
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
	if (selection.isEmpty()) {
		return fail(2, Text::tr("%1 requires --object kind:id, repeatable.").arg(name));
	}

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap({QFileInfo(path).absoluteFilePath(), values.value(QStringLiteral("--map-name")).value(0), values.value(QStringLiteral("--engine")).value(0)},
			&document, &error)) {
		return fail(QFileInfo::exists(path) ? 1 : 3, Text::tr("Unable to load map: %1").arg(error));
	}
	if (seen.contains(QStringLiteral("--context")) && !openLevelSceneGroup(&document, values.value(QStringLiteral("--context")).first(), &error)) {
		return fail(4, error);
	}
	if (!setLevelMapSelection(&document, selection, &error)) {
		return fail(2, Text::tr("Unable to select map objects: %1").arg(error));
	}
	QVector<LevelSurfaceFace> excluded;
	if (!levelCsgExcludedFacesFromArgs(document, values.value(QStringLiteral("--exclude-face")), values.value(QStringLiteral("--only-face")),
			seen.contains(QStringLiteral("--exclude-facing")), values.value(QStringLiteral("--exclude-facing")).value(0), &excluded, &error)) {
		return fail(2, error);
	}
	LevelMapBrushOffsetReport report;
	if (!offsetLevelMapSelectionFaces(&document, shrink ? -thickness : thickness, excluded, &report, &error)) {
		return fail(1, shrink ? Text::tr("Unable to shrink: %1").arg(error) : Text::tr("Unable to expand: %1").arg(error));
	}
	const LevelMapSaveReport saved = saveLevelMapAs(document, output, seen.contains(QStringLiteral("--dry-run")), seen.contains(QStringLiteral("--overwrite")));
	if (!saved.succeeded()) {
		return fail(4, saved.errors.join(QLatin1Char('\n')));
	}
	QJsonArray left;
	for (const LevelSurfaceFace& face : std::as_const(excluded)) {
		left.append(levelMaterialTargetId({LevelMaterialKind::BrushFace, face.brushId, face.faceIndex}));
	}
	result.payload = {{QStringLiteral("operation"), shrink ? QStringLiteral("shrink") : QStringLiteral("expand")},
		{QStringLiteral("thickness"), thickness}, {QStringLiteral("offset"), shrink ? -thickness : thickness},
		{QStringLiteral("changed"), report.changed}, {QStringLiteral("movedFaces"), report.movedFaces},
		{QStringLiteral("excludedFaces"), left}, {QStringLiteral("skipped"), QJsonArray::fromStringList(report.skipped)},
		{QStringLiteral("output"), saved.outputPath}, {QStringLiteral("written"), saved.written}, {QStringLiteral("dryRun"), saved.dryRun},
		{QStringLiteral("warnings"), QJsonArray::fromStringList(saved.warnings)}};
	const QString units = QString::number(thickness, 'g', 10);
	const int changed = report.changed;
	result.lines << (shrink ? Text::tr("Shrank %n brush(es) by %1 units.", nullptr, changed).arg(units)
							: Text::tr("Expanded %n brush(es) by %1 units.", nullptr, changed).arg(units));
	for (const QString& reason : report.skipped) {
		result.lines << Text::tr("Left as it was: %1").arg(reason);
	}
	result.lines << levelMapSaveReportText(saved);
	return result;
}

} // namespace vibestudio::cli
