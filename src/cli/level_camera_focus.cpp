// `map camera-focus`: where Level Camera and Focus Camera on Selection would
// put the level camera, from the same core service the Levels View > Camera
// commands use (core/level_camera_focus.h). A query: no map is written.
//
// Workflow reference: NRC camwindow.cpp GlobalCamera_ResetAngles and
// Camera_getFocusPos at 449778b3 (GPL-2.0-or-later). Original Qt
// implementation; see docs/CREDITS.md.

#include "cli/level_camera_focus.h"
#include "core/level_camera_focus.h"
#include "core/level_map.h"
#include "core/level_scene.h"
#include "core/level_visibility.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QSet>

#include <cmath>

namespace vibestudio::cli {
namespace {
struct Text {
	Q_DECLARE_TR_FUNCTIONS(LevelCameraFocusCli)
};

QJsonArray point(const LevelMapVec3& value)
{
	return QJsonArray {value.x, value.y, value.z};
}

bool parseNumber(const QString& text, double* value)
{
	bool ok = false;
	const double parsed = text.trimmed().toDouble(&ok);
	if (!ok || !std::isfinite(parsed)) {
		return false;
	}
	*value = parsed;
	return true;
}

bool parsePoint(const QString& text, LevelMapVec3* value)
{
	const QStringList parts = text.split(QLatin1Char(','));
	if (parts.size() != 3) {
		return false;
	}
	LevelMapVec3 result;
	if (!parseNumber(parts[0], &result.x) || !parseNumber(parts[1], &result.y) || !parseNumber(parts[2], &result.z)) {
		return false;
	}
	result.valid = true;
	*value = result;
	return true;
}

// A width / height ratio written as 1.6, 16:9 or 1920x1080.
bool parseAspect(const QString& text, double* value)
{
	for (const QChar separator : {QLatin1Char(':'), QLatin1Char('x'), QLatin1Char('/')}) {
		const QStringList parts = text.split(separator);
		if (parts.size() == 2) {
			double width = 0.0;
			double height = 0.0;
			if (!parseNumber(parts[0], &width) || !parseNumber(parts[1], &height) || height <= 0.0) {
				return false;
			}
			*value = width / height;
			return true;
		}
	}
	return parseNumber(text, value);
}
} // namespace

LevelCameraFocusCliResult runLevelCameraFocus(const QStringList& arguments)
{
	LevelCameraFocusCliResult result;
	result.payload.insert(QStringLiteral("mapWritten"), false);
	result.payload.insert(QStringLiteral("queryOnly"), true);
	const auto fail = [&](int code, const QString& message) {
		result.exitCode = code;
		result.error = message;
		return result;
	};
	const QSet<QString> flags {QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--quiet"), QStringLiteral("--verbose"), QStringLiteral("--level")};
	const QSet<QString> options {QStringLiteral("--object"), QStringLiteral("--context"), QStringLiteral("--engine"), QStringLiteral("--map"),
		QStringLiteral("--camera-yaw"), QStringLiteral("--camera-pitch"), QStringLiteral("--camera-position"), QStringLiteral("--fov"),
		QStringLiteral("--aspect"), QStringLiteral("--offset"), QStringLiteral("--settings-file"), QStringLiteral("--locale"), QStringLiteral("--catalog-root")};
	QSet<QString> seen;
	QHash<QString, QStringList> values;
	QStringList positional;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const QString arg = arguments[i];
		if (!arg.startsWith(QLatin1Char('-')) || (arg.size() > 1 && (arg[1].isDigit() || arg[1] == QLatin1Char('.')))) {
			positional.append(arg);
			continue;
		}
		const auto equal = arg.indexOf(QLatin1Char('='));
		const QString name = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(name) && name != QLatin1String("--object")) {
			return fail(2, Text::tr("Repeated option: %1.").arg(name));
		}
		seen.insert(name);
		if (flags.contains(name) && equal < 0) {
			continue;
		}
		if (!options.contains(name)) {
			return fail(2, Text::tr("Unexpected option: %1.").arg(arg));
		}
		QString value;
		if (equal >= 0) {
			value = arg.mid(equal + 1);
		} else if (i + 1 < arguments.size()) {
			// Negative angles and offsets are values, not options.
			const QString next = arguments[i + 1];
			if (!next.startsWith(QLatin1Char('-')) || (next.size() > 1 && (next[1].isDigit() || next[1] == QLatin1Char('.')))) {
				value = arguments[++i];
			}
		}
		if (value.trimmed().isEmpty()) {
			return fail(2, Text::tr("Missing value for %1.").arg(name));
		}
		values[name].append(value);
		if (values[name].size() > 16384) {
			return fail(2, Text::tr("At most 16,384 values are allowed for %1.").arg(name));
		}
	}
	if (positional.size() != 3 || !QStringList {QStringLiteral("map"), QStringLiteral("maps"), QStringLiteral("level")}.contains(positional[0])
		|| positional[1] != QLatin1String("camera-focus")) {
		return fail(2, Text::tr("Expected map camera-focus <map> with --object selectors to focus on, --level, or both."));
	}
	const bool level = seen.contains(QStringLiteral("--level"));
	const QStringList selectors = values.value(QStringLiteral("--object"));
	if (!level && selectors.isEmpty()) {
		return fail(2, Text::tr("Name the objects to focus on with --object, or level the camera with --level."));
	}
	LevelCameraFocusRequest request;
	LevelMapVec3 camera {0.0, 0.0, 0.0, true};
	const auto number = [&](const QString& option, double* target) {
		return !values.contains(option) || parseNumber(values.value(option).first(), target);
	};
	if (!number(QStringLiteral("--camera-yaw"), &request.yawDegrees) || !number(QStringLiteral("--camera-pitch"), &request.pitchDegrees)
		|| std::abs(request.pitchDegrees) > 90.0) {
		return fail(2, Text::tr("--camera-yaw takes degrees and --camera-pitch -90 to 90 degrees."));
	}
	if (!number(QStringLiteral("--fov"), &request.fieldOfViewDegrees) || request.fieldOfViewDegrees < 15.0 || request.fieldOfViewDegrees > 150.0) {
		return fail(2, Text::tr("--fov takes the vertical field of view, 15 to 150 degrees."));
	}
	if (values.contains(QStringLiteral("--aspect")) && (!parseAspect(values.value(QStringLiteral("--aspect")).first(), &request.aspect)
		|| request.aspect < 0.01 || request.aspect > 100.0)) {
		return fail(2, Text::tr("--aspect takes the view's width to height ratio, such as 1.6, 16:9 or 1920x1080."));
	}
	if (values.contains(QStringLiteral("--offset"))) {
		bool ok = false;
		request.offsetSteps = values.value(QStringLiteral("--offset")).first().trimmed().toInt(&ok);
		if (!ok || std::abs(request.offsetSteps) > kLevelCameraFocusOffsetLimit) {
			return fail(2, Text::tr("--offset takes whole wheel notches from %1 to %2.").arg(-kLevelCameraFocusOffsetLimit).arg(kLevelCameraFocusOffsetLimit));
		}
	}
	if (values.contains(QStringLiteral("--camera-position")) && !parsePoint(values.value(QStringLiteral("--camera-position")).first(), &camera)) {
		return fail(2, Text::tr("--camera-position takes x,y,z in map units."));
	}
	LevelMapDocument source;
	QString error;
	if (!loadLevelMap({QFileInfo(positional[2]).absoluteFilePath(), values.value(QStringLiteral("--map")).value(0),
			values.value(QStringLiteral("--engine")).value(0)}, &source, &error)) {
		return fail(4, error);
	}
	if (values.contains(QStringLiteral("--context")) && !openLevelSceneGroup(&source, values.value(QStringLiteral("--context")).first(), &error)) {
		return fail(4, error);
	}
	QVector<LevelMapSelectionRef> objects;
	QSet<QString> named;
	for (const QString& selector : selectors) {
		if (!selectLevelMapObject(&source, selector, &error)) {
			return fail(2, error);
		}
		for (const LevelMapSelectionRef& ref : source.selection) {
			const QString id = levelMapSelectionRefId(ref);
			if (!named.contains(id)) {
				objects.append(ref);
				named.insert(id);
			}
		}
	}
	// The Levels views cannot select hidden objects, so the camera never
	// focuses on them here either.
	const QSet<QString> hidden = levelMapHiddenObjects(source);
	for (const LevelMapSelectionRef& ref : objects) {
		if (hidden.contains(levelMapSelectionRefId(ref))) {
			return fail(4, Text::tr("%1 is hidden; show it before focusing the camera on it.").arg(levelMapSelectionRefId(ref)));
		}
	}
	if (level) {
		request.yawDegrees = levelCameraLevelledYaw(request.yawDegrees);
		request.pitchDegrees = 0.0;
	}
	QJsonObject cameraJson {{QStringLiteral("yaw"), request.yawDegrees}, {QStringLiteral("pitch"), request.pitchDegrees},
		{QStringLiteral("fieldOfView"), request.fieldOfViewDegrees}, {QStringLiteral("aspect"), request.aspect}};
	result.payload.insert(QStringLiteral("source"), source.sourcePath);
	result.payload.insert(QStringLiteral("sourceSha256"), QString::fromLatin1(source.sourceContentHash.toHex()));
	result.payload.insert(QStringLiteral("context"), source.openSceneGroups.isEmpty() ? QString() : source.openSceneGroups.last());
	result.payload.insert(QStringLiteral("levelled"), level);
	result.lines.append(Text::tr("Query only: the source map was not changed."));
	if (!objects.isEmpty()) {
		LevelMapVec3 mins;
		LevelMapVec3 maxs;
		if (!levelCameraFocusObjectsBounds(source, objects, &mins, &maxs)) {
			return fail(4, Text::tr("The named objects have no size to focus on."));
		}
		request.mins = mins;
		request.maxs = maxs;
		const LevelCameraFocus focus = levelCameraFocusPosition(request);
		if (!focus.valid) {
			return fail(2, focus.error);
		}
		camera = focus.position;
		QStringList ids;
		for (const LevelMapSelectionRef& ref : objects) {
			ids.append(levelMapSelectionRefId(ref));
		}
		result.payload.insert(QStringLiteral("focus"), QJsonObject {{QStringLiteral("objects"), QJsonArray::fromStringList(ids)},
			{QStringLiteral("mins"), point(mins)}, {QStringLiteral("maxs"), point(maxs)}, {QStringLiteral("centre"), point(focus.centre)},
			{QStringLiteral("fitDistance"), focus.fitDistance}, {QStringLiteral("distance"), focus.distance},
			{QStringLiteral("offset"), request.offsetSteps}, {QStringLiteral("minimumDistance"), kLevelCameraFocusMinimumDistance}});
		const int count = static_cast<int>(ids.size());
		result.lines.append(Text::tr("Focused on %n object(s), %1 units from their centre.", nullptr, count).arg(focus.distance, 0, 'f', 2));
	}
	cameraJson.insert(QStringLiteral("position"), point(camera));
	result.payload.insert(QStringLiteral("camera"), cameraJson);
	result.lines.append(Text::tr("Camera at %1, %2, %3, heading %4 degrees, pitch %5 degrees.")
		.arg(camera.x, 0, 'f', 2).arg(camera.y, 0, 'f', 2).arg(camera.z, 0, 'f', 2).arg(request.yawDegrees, 0, 'g', 8).arg(request.pitchDegrees, 0, 'g', 8));
	return result;
}
} // namespace vibestudio::cli
