// Level Camera and Focus Camera on Selection (core/level_camera_focus.h):
// the 22.5-degree heading steps, the frustum box fit with its 64-unit
// minimum and near clearance on fixed and pseudo-random boxes and angles,
// the wheel offset curve, refusals, hidden and missing objects left out of
// the focused box, the free-focus key rules (Tab allowed only there), the
// profile keys (End, Tab, Ctrl+Alt+Z) and `map camera-focus` over the same
// service. With the vibestudio binary as the first argument the CLI is
// checked too.

#include "core/editor_profiles.h"
#include "core/level_camera_focus.h"
#include "core/level_camera_keys.h"
#include "core/level_gestures.h"
#include "core/level_map.h"
#include "core/level_scene.h"
#include "core/level_visibility.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <utility>

using namespace vibestudio;

namespace {

bool ok = true;

bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << label << ": " << detail.toStdString() << std::endl;
	}
	return value;
}

bool nearly(double a, double b, double tolerance = 1.0e-6)
{
	return std::abs(a - b) <= tolerance * std::max(1.0, std::max(std::abs(a), std::abs(b)));
}

QString box(std::array<double, 3> lo, std::array<double, 3> hi, const QString& material)
{
	QStringList lines {QStringLiteral("{")};
	const auto point = [](const std::array<double, 3>& v) {
		return QStringLiteral("( %1 %2 %3 )").arg(v[0], 0, 'g', 10).arg(v[1], 0, 'g', 10).arg(v[2], 0, 'g', 10);
	};
	for (int axis = 0; axis < 3; ++axis) {
		for (const bool positive : {false, true}) {
			std::array<double, 3> p = lo;
			p[axis] = positive ? hi[axis] : lo[axis];
			auto a = p;
			auto b = p;
			int first = (axis + 1) % 3;
			int second = (axis + 2) % 3;
			if (positive) {
				std::swap(first, second);
			}
			a[first] += 1;
			b[second] += 1;
			lines << QStringLiteral("%1 %2 %3 %4 0 0 0 0.5 0.5 0 0 0").arg(point(p), point(a), point(b), material);
		}
	}
	lines << QStringLiteral("}");
	return lines.join(QLatin1Char('\n'));
}

// brush:0 spans 0..64 on every axis, brush:1 128..192 x, -32..32 y, 0..256 z.
QByteArray fixture()
{
	return QStringLiteral("{\n\"classname\" \"worldspawn\"\n%1\n%2\n}\n{\n\"classname\" \"light\"\n\"origin\" \"320 0 64\"\n}\n")
		.arg(box({0, 0, 0}, {64, 64, 64}, QStringLiteral("base/wall")), box({128, -32, 0}, {192, 32, 256}, QStringLiteral("base/wall")))
		.toUtf8();
}

// Every corner inside the view with the near clearance, and the fit tight:
// at the minimum distance or with some corner on a side of the view.
void checkFit(const LevelCameraFocusRequest& request, const LevelCameraFocus& focus, const QString& label)
{
	if (!expect(focus.valid, "focus is valid", label + QLatin1Char(' ') + focus.error)) {
		return;
	}
	const double yaw = request.yawDegrees * std::numbers::pi / 180.0;
	const double pitch = request.pitchDegrees * std::numbers::pi / 180.0;
	const std::array<double, 3> forward {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
	const std::array<double, 3> right {std::sin(yaw), -std::cos(yaw), 0.0};
	const std::array<double, 3> up {forward[1] * right[2] - forward[2] * right[1], forward[2] * right[0] - forward[0] * right[2],
		forward[0] * right[1] - forward[1] * right[0]};
	const double tanV = std::tan(request.fieldOfViewDegrees * std::numbers::pi / 360.0);
	const double tanH = tanV * request.aspect;
	bool touches = nearly(focus.fitDistance, kLevelCameraFocusMinimumDistance);
	bool inside = true;
	for (int corner = 0; corner < 8; ++corner) {
		const std::array<double, 3> p {(corner & 1) ? request.maxs.x : request.mins.x, (corner & 2) ? request.maxs.y : request.mins.y,
			(corner & 4) ? request.maxs.z : request.mins.z};
		const std::array<double, 3> v {p[0] - focus.position.x, p[1] - focus.position.y, p[2] - focus.position.z};
		const double depth = v[0] * forward[0] + v[1] * forward[1] + v[2] * forward[2];
		const double side = std::abs(v[0] * right[0] + v[1] * right[1] + v[2] * right[2]);
		const double rise = std::abs(v[0] * up[0] + v[1] * up[1] + v[2] * up[2]);
		const double slack = 1.0e-6 * std::max(1.0, depth);
		inside = inside && depth >= kLevelCameraFocusNearClearance - 1.0e-6 && side <= tanH * depth + slack && rise <= tanV * depth + slack;
		touches = touches || nearly(side, tanH * depth, 1.0e-6) || nearly(rise, tanV * depth, 1.0e-6) || nearly(depth, kLevelCameraFocusNearClearance, 1.0e-6);
	}
	expect(inside, "every corner is inside the view", label);
	expect(touches, "the fit is the closest that holds", label);
	// The camera stands on the line through the centre along the view.
	const std::array<double, 3> back {focus.centre.x - focus.position.x, focus.centre.y - focus.position.y, focus.centre.z - focus.position.z};
	expect(nearly(back[0], forward[0] * focus.distance, 1.0e-6) && nearly(back[1], forward[1] * focus.distance, 1.0e-6)
			&& nearly(back[2], forward[2] * focus.distance, 1.0e-6),
		"the camera keeps its view on the box centre", label);
}

void levelling()
{
	const QVector<std::pair<double, double>> cases {{0, 0}, {10, 0}, {12, 22.5}, {100, 90}, {101.25, 112.5}, {359, 0}, {-10, 0}, {-30, 337.5},
		{765, 45}, {180, 180}, {-180, 180}, {33.74, 22.5}, {33.76, 45}};
	for (const auto& [input, levelled] : cases) {
		expect(nearly(levelCameraLevelledYaw(input), levelled), "heading snaps to the nearest 22.5 degrees", QString::number(input));
	}
	expect(std::isnan(levelCameraLevelledYaw(std::nan(""))) && std::isnan(levelCameraLevelledYaw(std::numeric_limits<double>::infinity())), "a heading that is not finite stays unset");
	expect(nearly(levelCameraFocusOffsetScale(0), 1) && nearly(levelCameraFocusOffsetScale(8), 0.5) && nearly(levelCameraFocusOffsetScale(-8), 2)
			&& nearly(levelCameraFocusOffsetScale(1000), 1.0 / 64) && nearly(levelCameraFocusOffsetScale(-1000), 64),
		"eight wheel notches halve or double the distance, within the limit");
}

void fitting()
{
	LevelCameraFocusRequest request;
	request.mins = {-32, -128, -128, true};
	request.maxs = {32, 128, 128, true};
	request.fieldOfViewDegrees = 90;
	request.aspect = 1;
	LevelCameraFocus focus = levelCameraFocusPosition(request);
	expect(focus.valid && nearly(focus.fitDistance, 160) && nearly(focus.distance, 160) && nearly(focus.position.x, -160) && nearly(focus.position.y, 0)
			&& nearly(focus.position.z, 0),
		"a wide box backs the camera off until its near face fills the view", QString::number(focus.fitDistance));
	checkFit(request, focus, QStringLiteral("wide box"));
	request.aspect = 2;
	focus = levelCameraFocusPosition(request);
	expect(nearly(focus.fitDistance, 160), "the taller of the two fits decides", QString::number(focus.fitDistance));
	request.mins = {-128, -128, -16, true};
	request.maxs = {128, 128, 16, true};
	request.yawDegrees = 0;
	request.aspect = 1;
	focus = levelCameraFocusPosition(request);
	expect(nearly(focus.fitDistance, 256), "a box's depth along the view adds to the distance", QString::number(focus.fitDistance));
	// A point never comes closer than the minimum.
	request.mins = request.maxs = {10, 20, 30, true};
	focus = levelCameraFocusPosition(request);
	expect(focus.valid && nearly(focus.fitDistance, kLevelCameraFocusMinimumDistance) && nearly(focus.position.x, 10 - kLevelCameraFocusMinimumDistance),
		"a point is focused from 64 units", QString::number(focus.fitDistance));
	// A thin box straight down the view keeps its near end off the camera.
	request.mins = {-1000, 0, 0, true};
	request.maxs = {1000, 0, 0, true};
	focus = levelCameraFocusPosition(request);
	expect(nearly(focus.fitDistance, 1000 + kLevelCameraFocusNearClearance), "the near clearance keeps the box in front of the camera",
		QString::number(focus.fitDistance));
	request.offsetSteps = 8;
	focus = levelCameraFocusPosition(request);
	expect(nearly(focus.distance, focus.fitDistance / 2) && nearly(focus.position.x, -focus.distance), "the wheel offset scales the fitted distance");
	request.offsetSteps = 0;
	std::mt19937 random(20261022);
	std::uniform_real_distribution<double> coordinate(-4096, 4096), extent(0, 1024), heading(-720, 720), pitch(-89, 89), fov(15, 150), aspect(0.3, 3.5);
	for (int sample = 0; sample < 400; ++sample) {
		LevelCameraFocusRequest random_request;
		const std::array<double, 3> lo {coordinate(random), coordinate(random), coordinate(random)};
		random_request.mins = {lo[0], lo[1], lo[2], true};
		random_request.maxs = {lo[0] + extent(random), lo[1] + extent(random), lo[2] + extent(random), true};
		random_request.yawDegrees = heading(random);
		random_request.pitchDegrees = pitch(random);
		random_request.fieldOfViewDegrees = fov(random);
		random_request.aspect = aspect(random);
		checkFit(random_request, levelCameraFocusPosition(random_request), QStringLiteral("sample %1").arg(sample));
	}
	LevelCameraFocusRequest bad;
	bad.mins = {1, 0, 0, true};
	bad.maxs = {0, 0, 0, true};
	expect(!levelCameraFocusPosition(bad).valid && !levelCameraFocusPosition(bad).error.isEmpty(), "an inside-out box is refused");
	bad.maxs = {2, 2, 2, true};
	bad.fieldOfViewDegrees = 10;
	expect(!levelCameraFocusPosition(bad).valid, "a field of view below 15 degrees is refused");
	bad.fieldOfViewDegrees = 90;
	bad.aspect = 0;
	expect(!levelCameraFocusPosition(bad).valid, "a view with no width is refused");
	bad.aspect = 1;
	bad.pitchDegrees = 91;
	expect(!levelCameraFocusPosition(bad).valid, "a pitch past straight up is refused");
	bad.pitchDegrees = 0;
	bad.mins.x = std::nan("");
	expect(!levelCameraFocusPosition(bad).valid, "a coordinate that is not finite is refused");
}

void objectBounds()
{
	LevelMapDocument document;
	QString error;
	if (!expect(loadLevelMapBytes({QStringLiteral("focus.map"), {}, QStringLiteral("idtech3")}, fixture(), &document, &error), "fixture loads", error)) {
		return;
	}
	const LevelMapSelectionRef first {LevelMapSelectionKind::QuakeBrush, 0};
	const LevelMapSelectionRef second {LevelMapSelectionKind::QuakeBrush, 1};
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	expect(levelCameraFocusObjectsBounds(document, {first, second}, &mins, &maxs) && nearly(mins.x, 0) && nearly(mins.y, -32) && nearly(maxs.x, 192)
			&& nearly(maxs.z, 256),
		"two brushes give their combined box");
	expect(levelCameraFocusObjectsBounds(document, {first, {LevelMapSelectionKind::QuakeBrush, 99}}, &mins, &maxs) && nearly(maxs.x, 64) && nearly(maxs.z, 64),
		"an object that no longer exists is left out");
	expect(!levelCameraFocusObjectsBounds(document, {}, &mins, &maxs), "nothing to measure is refused");
	expect(setLevelMapSelection(&document, {second}, &error), "select the tall brush", error);
	expect(changeLevelMapVisibility(&document, {LevelVisibilityOperation::HideSelected}, nullptr, &error), "hide the tall brush", error);
	expect(levelCameraFocusObjectsBounds(document, {first, second}, &mins, &maxs) && nearly(maxs.x, 64) && nearly(maxs.z, 64),
		"a hidden object never counts towards the focused box");
	expect(!levelCameraFocusObjectsBounds(document, {second}, &mins, &maxs), "only hidden objects leave nothing to focus on");
}

void keys()
{
	QString normalized;
	QString error;
	expect(normalizeCameraFocusKey(QStringLiteral("Tab"), &normalized, &error) && normalized == QLatin1String("Tab"), "Tab may hold free focus", error);
	expect(normalizeCameraFocusKey(QStringLiteral("F"), &normalized, &error) && normalized == QLatin1String("F"), "a letter may hold free focus", error);
	expect(!normalizeCameraFocusKey(QStringLiteral("Ctrl+Tab"), &normalized, &error) && !error.isEmpty(), "a chord cannot hold free focus");
	expect(!normalizeCameraFocusKey(QStringLiteral("Escape"), &normalized, &error), "Escape stays for cancelling");
	expect(!normalizeCameraKey(QStringLiteral("Tab"), false, &normalized, &error), "Tab stays reserved for every other camera key");
	EditorProfileDescriptor nrc;
	EditorProfileDescriptor trenchBroom;
	EditorProfileDescriptor studio;
	if (!expect(editorProfileForId(QStringLiteral("netradiant-custom"), &nrc) && editorProfileForId(QStringLiteral("trenchbroom"), &trenchBroom)
			&& editorProfileForId(QStringLiteral("vibestudio-default"), &studio), "profiles")) {
		return;
	}
	expect(cameraFocusKeyMatches(nrc.controls.camera, Qt::Key_Tab, Qt::NoModifier) && !cameraFocusKeyMatches(nrc.controls.camera, Qt::Key_Tab, Qt::ShiftModifier)
			&& !cameraFocusKeyMatches(trenchBroom.controls.camera, Qt::Key_Tab, Qt::NoModifier),
		"NetRadiant Custom holds free focus on Tab, unmodified; TrenchBroom has no free focus");
	expect(cameraKeyProblems(nrc.controls.camera).isEmpty() && levelEditorControlProblems(nrc.controls).isEmpty(), "the NRC camera keys are consistent",
		cameraKeyProblems(nrc.controls.camera).join(QLatin1Char(' ')) + levelEditorControlProblems(nrc.controls).join(QLatin1Char(' ')));
	LevelEditorControls custom;
	QJsonObject cleaned;
	expect(applyLevelGestureOverrides(nrc.controls, {{QStringLiteral("camera.focusHoldKey"), QStringLiteral("G")}}, &custom, &cleaned, &error)
			&& custom.camera.focusHoldKey == QLatin1String("G") && cleaned.value(QStringLiteral("camera.focusHoldKey")) == QLatin1String("G"),
		"the free-focus key is a gesture preference", error);
	expect(applyLevelGestureOverrides(nrc.controls, {{QStringLiteral("camera.focusHoldKey"), QStringLiteral("none")}}, &custom, nullptr, &error)
			&& custom.camera.focusHoldKey.isEmpty(),
		"none turns free focus off", error);
	expect(!applyLevelGestureOverrides(nrc.controls, {{QStringLiteral("camera.focusHoldKey"), QStringLiteral("W")}}, nullptr, nullptr, &error)
			&& !error.isEmpty(),
		"a fly key cannot also hold free focus");
	expect(!applyLevelGestureOverrides(nrc.controls, {{QStringLiteral("camera.focusHoldKey"), QStringLiteral("Shift+G")}}, nullptr, nullptr, &error),
		"free focus takes an unmodified key");
	expect(applyLevelGestureOverrides(trenchBroom.controls, {{QStringLiteral("camera.focusHoldKey"), QStringLiteral("Tab")}}, &custom, nullptr, &error)
			&& custom.camera.focusHoldKey == QLatin1String("Tab"),
		"TrenchBroom users may opt in to Tab", error);
	expect(!applyLevelGestureOverrides(studio.controls, {{QStringLiteral("camera.focusHoldKey"), QStringLiteral("G")}}, nullptr, nullptr, &error),
		"the orbiting camera has no free focus");
	expect(!levelCameraShortcutWarnings(nrc, nrc.controls, {{QStringLiteral("map.selectAll"), {QStringLiteral("Tab")}}}).isEmpty(),
		"a command rebound to the free-focus key is reported as an overlap during mouse look");
	// The profiles' keys: End in the audited Radiant family, Ctrl+Alt+Z in
	// TrenchBroom, no key where the scheme's sources do not confirm one.
	const auto keysOf = [](const QString& profile, const QString& command) {
		EditorProfileDescriptor descriptor;
		if (!editorProfileForId(profile, &descriptor)) {
			return QStringList {QStringLiteral("<missing profile>")};
		}
		for (const LevelEditorKeyBinding& binding : descriptor.controls.keys) {
			if (binding.commandId == command) {
				return binding.keys;
			}
		}
		return QStringList();
	};
	for (const char* profile : {"netradiant-custom", "netradiant", "gtkradiant-1-6", "gtkradiant-1-5", "gtkradiant-1-4", "q3radiant", "doomedit"}) {
		expect(keysOf(QString::fromLatin1(profile), QStringLiteral("map.levelCamera")) == QStringList {QStringLiteral("End")}, "End levels the camera", QString::fromLatin1(profile));
	}
	for (const char* profile : {"qeradiant", "darkradiant", "trenchbroom", "vibestudio-default", "hammer"}) {
		expect(keysOf(QString::fromLatin1(profile), QStringLiteral("map.levelCamera")).isEmpty(), "no unaudited Level Camera key", QString::fromLatin1(profile));
	}
	expect(keysOf(QStringLiteral("trenchbroom"), QStringLiteral("map.resetCameraZoom")) == QStringList {QStringLiteral("Ctrl+Alt+Z")}
			&& keysOf(QStringLiteral("netradiant-custom"), QStringLiteral("map.resetCameraZoom")).isEmpty(),
		"Ctrl+Alt+Z resets the camera zoom in TrenchBroom");
	EditorProfileDescriptor netRadiant;
	expect(editorProfileForId(QStringLiteral("netradiant"), &netRadiant) && netRadiant.controls.camera.focusHoldKey == QLatin1String("Tab"),
		"NetRadiant keeps Tab for free focus");
	for (const EditorProfileDescriptor& profile : editorProfileDescriptors()) {
		expect(levelEditorControlProblems(profile.controls).isEmpty() && cameraKeyProblems(profile.controls.camera).isEmpty(), "every profile stays consistent",
			profile.id + QLatin1String(": ") + levelEditorControlProblems(profile.controls).join(QLatin1Char(' ')) + cameraKeyProblems(profile.controls.camera).join(QLatin1Char(' ')));
	}
}

void cli(const QString& executable)
{
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "CLI temporary directory")) {
		return;
	}
	const QString path = temp.filePath(QStringLiteral("focus.map"));
	const QString hiddenPath = temp.filePath(QStringLiteral("hidden.map"));
	const QByteArray source = fixture();
	QFile file(path);
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(), "CLI fixture")) {
		return;
	}
	file.close();
	{
		// The same map with the tall brush in a hidden layer.
		LevelMapDocument document;
		QString error;
		QString layer;
		const bool built = loadLevelMapBytes({QStringLiteral("hidden.map"), {}, QStringLiteral("idtech3")}, source, &document, &error)
			&& createLevelSceneNode(&document, LevelSceneNodeKind::Layer, QStringLiteral("Hidden"), {}, &layer, &error)
			&& assignLevelSceneObjects(&document, layer, {QStringLiteral("brush:1")}, &error) && setLevelSceneVisible(&document, layer, false, &error);
		const QByteArray bytes = serializeLevelMap(document).bytes;
		QFile hidden(hiddenPath);
		expect(built && hidden.open(QIODevice::WriteOnly) && hidden.write(bytes) == bytes.size(), "hidden layer fixture", error);
	}
	const QString settingsPath = temp.filePath(QStringLiteral("settings.ini"));
	const auto run = [&](const QStringList& args, int code = 0, bool json = true) {
		QProcess process;
		process.setWorkingDirectory(temp.path());
		QStringList command {QStringLiteral("--cli")};
		if (json) {
			command << QStringLiteral("--json");
		}
		command << QStringLiteral("--settings-file") << settingsPath << QStringLiteral("map") << QStringLiteral("camera-focus");
		process.start(executable, command + args);
		const bool done = process.waitForStarted() && process.waitForFinished(30000);
		const QByteArray out = process.readAllStandardOutput();
		const QByteArray err = process.readAllStandardError();
		expect(done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == code, "CLI exit status",
			args.join(QLatin1Char(' ')) + QLatin1String(" -> ") + QString::fromUtf8(out + err));
		if (!done) {
			process.kill();
			process.waitForFinished();
		}
		return std::make_pair(QJsonDocument::fromJson(out).object(), QString::fromUtf8(out + err));
	};
	const auto position = [](const QJsonObject& report) {
		const QJsonArray at = report.value(QStringLiteral("camera")).toObject().value(QStringLiteral("position")).toArray();
		return std::array<double, 3> {at.at(0).toDouble(), at.at(1).toDouble(), at.at(2).toDouble()};
	};
	auto [report, text] = run({path, QStringLiteral("--object"), QStringLiteral("brush:0"), QStringLiteral("--camera-yaw"), QStringLiteral("30"),
		QStringLiteral("--camera-pitch"), QStringLiteral("-20"), QStringLiteral("--fov"), QStringLiteral("100"), QStringLiteral("--aspect"), QStringLiteral("16:9")});
	LevelCameraFocusRequest request;
	request.mins = {0, 0, 0, true};
	request.maxs = {64, 64, 64, true};
	request.yawDegrees = 30;
	request.pitchDegrees = -20;
	request.fieldOfViewDegrees = 100;
	request.aspect = 16.0 / 9.0;
	const LevelCameraFocus expected = levelCameraFocusPosition(request);
	const auto at = position(report);
	const QJsonObject focus = report.value(QStringLiteral("focus")).toObject();
	expect(report.value(QStringLiteral("queryOnly")) == true && report.value(QStringLiteral("mapWritten")) == false && nearly(at[0], expected.position.x)
			&& nearly(at[1], expected.position.y) && nearly(at[2], expected.position.z) && nearly(focus.value(QStringLiteral("fitDistance")).toDouble(), expected.fitDistance)
			&& focus.value(QStringLiteral("objects")).toArray() == QJsonArray {QStringLiteral("brush:0")}
			&& nearly(report.value(QStringLiteral("camera")).toObject().value(QStringLiteral("yaw")).toDouble(), 30)
			&& report.value(QStringLiteral("sourceSha256")).toString().size() == 64,
		"the CLI focuses through the same service", text);
	std::tie(report, text) = run({path, QStringLiteral("--level"), QStringLiteral("--camera-yaw"), QStringLiteral("50"), QStringLiteral("--camera-pitch"),
		QStringLiteral("20"), QStringLiteral("--camera-position"), QStringLiteral("1,2,3")});
	const auto levelled = position(report);
	expect(report.value(QStringLiteral("levelled")) == true && nearly(report.value(QStringLiteral("camera")).toObject().value(QStringLiteral("yaw")).toDouble(), 45)
			&& report.value(QStringLiteral("camera")).toObject().value(QStringLiteral("pitch")).toDouble() == 0.0 && nearly(levelled[0], 1) && nearly(levelled[1], 2)
			&& nearly(levelled[2], 3) && !report.contains(QStringLiteral("focus")),
		"--level levels the camera where it stands", text);
	std::tie(report, text) = run({path, QStringLiteral("--level"), QStringLiteral("--object"), QStringLiteral("brush:0"), QStringLiteral("--object"),
		QStringLiteral("brush:1"), QStringLiteral("--camera-yaw"), QStringLiteral("-100"), QStringLiteral("--camera-pitch"), QStringLiteral("-45"),
		QStringLiteral("--offset"), QStringLiteral("8")});
	request = {};
	request.mins = {0, -32, 0, true};
	request.maxs = {192, 64, 256, true};
	request.yawDegrees = levelCameraLevelledYaw(-100);
	request.offsetSteps = 8;
	const LevelCameraFocus both = levelCameraFocusPosition(request);
	const auto combined = position(report);
	expect(nearly(combined[0], both.position.x) && nearly(combined[1], both.position.y) && nearly(combined[2], both.position.z)
			&& nearly(report.value(QStringLiteral("focus")).toObject().value(QStringLiteral("distance")).toDouble(), both.fitDistance / 2),
		"--level then focus, with a wheel offset", text);
	std::tie(report, text) = run({path, QStringLiteral("--object"), QStringLiteral("brush:0")}, 0, false);
	expect(text.contains(QStringLiteral("not changed")) && text.contains(QStringLiteral("Camera at")), "CLI text report", text);
	run({hiddenPath, QStringLiteral("--object"), QStringLiteral("brush:0")});
	std::tie(report, text) = run({hiddenPath, QStringLiteral("--object"), QStringLiteral("brush:1")}, 4);
	expect(report.value(QStringLiteral("message")).toString().contains(QStringLiteral("brush:1")), "a hidden object is refused", text);
	for (const QStringList& bad : QVector<QStringList> {{}, {path}, {path, QStringLiteral("--fov"), QStringLiteral("200"), QStringLiteral("--level")},
			 {path, QStringLiteral("--aspect"), QStringLiteral("0"), QStringLiteral("--level")}, {path, QStringLiteral("--offset"), QStringLiteral("99"), QStringLiteral("--level")},
			 {path, QStringLiteral("--camera-pitch"), QStringLiteral("120"), QStringLiteral("--level")}, {path, QStringLiteral("--object"), QStringLiteral("bogus")},
			 {path, QStringLiteral("--level"), QStringLiteral("--level")}, {path, QStringLiteral("--level"), QStringLiteral("--output"), QStringLiteral("x.map")},
			 {path, QStringLiteral("--level"), QStringLiteral("--camera-position"), QStringLiteral("1,2")}}) {
		run(bad, 2);
	}
	run({temp.filePath(QStringLiteral("missing.map")), QStringLiteral("--level")}, 4);
	expect(file.open(QIODevice::ReadOnly) && file.readAll() == source, "the CLI never changes the map");
	file.close();
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	levelling();
	fitting();
	objectBounds();
	keys();
	if (argc == 2) {
		cli(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath());
	}
	if (ok) {
		std::cout << "Level camera focus: levelling, frustum fit, offsets, hidden objects, keys" << (argc == 2 ? " and CLI" : "") << " passed." << std::endl;
	}
	return ok ? 0 : 1;
}
