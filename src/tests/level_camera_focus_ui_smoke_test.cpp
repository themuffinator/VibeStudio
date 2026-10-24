// View > Camera in the Levels page: Level Camera, Focus Camera on Selection
// (with the last-selection fallback), Reset Camera Zoom and the held
// free-focus key, under the NetRadiant Custom and TrenchBroom profiles at
// 100% and at 200% with high contrast, right-to-left and expanded text.
// Each moves only the camera: the plan views, the map bytes, its revision
// and undo history stay as they were. Owned offscreen widgets and semantic
// Qt events only.

#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_camera_focus.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QKeyEvent>
#include <QMenu>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <QWheelEvent>

#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <utility>

using namespace vibestudio;

namespace {

bool ok = true;
QString phase;

bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << phase.toStdString() << ": " << label << ": " << detail.toStdString() << std::endl;
	}
	return value;
}

bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 45000) {
		QEventLoop loop;
		QTimer::singleShot(15, &loop, &QEventLoop::quit);
		loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return ready();
}

bool nearly(double a, double b, double tolerance = 1.0e-3)
{
	return std::abs(a - b) <= tolerance * std::max(1.0, std::max(std::abs(a), std::abs(b)));
}

bool samePosition(const ModelVec3& a, const LevelMapVec3& b)
{
	return nearly(a.x, b.x) && nearly(a.y, b.y) && nearly(a.z, b.z);
}

bool samePosition(const ModelVec3& a, const ModelVec3& b)
{
	return nearly(a.x, b.x) && nearly(a.y, b.y) && nearly(a.z, b.z);
}

double headingDifference(double a, double b)
{
	const double difference = std::fmod(std::abs(a - b), 360.0);
	return std::min(difference, 360.0 - difference);
}

class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* source, const char*, int) const override
	{
		const auto text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, QLatin1Char('~')));
	}
};

QString box(std::array<double, 3> lo, std::array<double, 3> hi)
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
			lines << QStringLiteral("%1 %2 %3 base/wall 0 0 0 0.5 0.5 0 0 0").arg(point(p), point(a), point(b));
		}
	}
	lines << QStringLiteral("}");
	return lines.join(QLatin1Char('\n'));
}

// The focus the camera should reach for the 0..64 brush from its current
// heading, pitch, field of view and shape, with `offset` wheel notches.
LevelCameraFocus expectedFocus(const ModelViewport& camera, int offset = 0)
{
	LevelCameraFocusRequest request;
	request.mins = {0, 0, 0, true};
	request.maxs = {64, 64, 64, true};
	request.yawDegrees = camera.cameraYaw();
	request.pitchDegrees = camera.cameraPitch();
	request.fieldOfViewDegrees = camera.fieldOfView();
	request.aspect = static_cast<double>(camera.width()) / std::max(1, camera.height());
	request.offsetSteps = offset;
	return levelCameraFocusPosition(request);
}

void sendKey(QWidget* target, QEvent::Type type, int key)
{
	QKeyEvent event(type, key, Qt::NoModifier);
	QApplication::sendEvent(target, &event);
}

void sendWheel(QWidget* target, int delta, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
	const QPointF centre(target->width() / 2.0, target->height() / 2.0);
	QWheelEvent event(centre, target->mapToGlobal(centre), QPoint(), QPoint(0, delta), Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
	QApplication::sendEvent(target, &event);
}

} // namespace

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	StudioSettings settings;
	settings.setRestoreSession(false);
	settings.setReducedMotion(true);
	settings.setLevelViewLayoutPreference(QStringLiteral("four-views"));
	const QByteArray source = QStringLiteral("{\n\"classname\" \"worldspawn\"\n%1\n%2\n}\n")
								  .arg(box({0, 0, 0}, {64, 64, 64}), box({256, -32, 0}, {320, 32, 128}))
								  .toUtf8();
	const QString path = temp.filePath(QStringLiteral("camera.map"));
	QFile file(path);
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(), "fixture")) {
		return 1;
	}
	file.close();
	for (const char* profile : {"netradiant-custom", "trenchbroom"}) {
		for (int scale : {100, 200}) {
			const bool nrc = QLatin1String(profile) == QLatin1String("netradiant-custom");
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			std::cout << "Level camera focus UI " << phase.toStdString() << std::endl;
			settings.setSelectedEditorProfileId(QString::fromLatin1(profile));
			settings.setTextScalePercent(scale);
			const auto theme = scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark;
			settings.setTheme(theme);
			settings.sync();
			applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, scale));
			Expanded expanded;
			if (scale == 200) {
				app.installTranslator(&expanded);
				app.setLayoutDirection(Qt::RightToLeft);
			}
			{
				ApplicationShell shell;
				shell.resize(scale == 100 ? 1700 : 2600, scale == 100 ? 1050 : 1800);
				shell.show();
				shell.openPathFromCommandLine(path);
				const auto action = [&](const char* id) { return shell.findChild<QAction*>(QString::fromLatin1(id)); };
				action("shell.mode.levels")->trigger();
				action("map.layout.four-views")->trigger();
				auto* top = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
				auto* camera = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
				if (!expect(top && camera && until([&] { return shell.levelDocument().sourcePath == path && camera->isEnabled() && camera->hasMesh(); }),
						"map and camera ready")) {
					return 1;
				}
				auto* level = action("map.levelCamera");
				auto* focus = action("map.focusCameraOnSelection");
				auto* reset = action("map.resetCameraZoom");
				if (!expect(level && focus && reset, "camera commands exist")) {
					return 1;
				}
				expect(until([&] { return level->isEnabled(); }) && reset->isEnabled() && !focus->isEnabled(),
					"levelling and zoom are ready; focusing waits for a first selection");
				for (QAction* command : {level, focus, reset}) {
					expect(!command->text().isEmpty() && !command->statusTip().isEmpty(), "command text and status tip", command->objectName());
					bool inMenu = false;
					for (QObject* owner : command->associatedObjects()) {
						inMenu = inMenu || qobject_cast<QMenu*>(owner) != nullptr;
					}
					expect(inMenu, "the command is in a menu", command->objectName());
					auto* button = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-") + command->objectName());
					expect(button && !button->accessibleName().isEmpty() && button->focusPolicy() != Qt::NoFocus && QAccessible::queryAccessibleInterface(button),
						"the Tools tab offers an accessible keyboard button", command->objectName());
				}
				expect(nrc ? level->shortcuts() == QList<QKeySequence> {QKeySequence(QStringLiteral("End"))} : level->shortcuts().isEmpty(),
					"End levels the camera only in the Radiant profile", level->shortcut().toString());
				expect(nrc ? reset->shortcuts().isEmpty() : reset->shortcuts() == QList<QKeySequence> {QKeySequence(QStringLiteral("Ctrl+Alt+Z"))},
					"Ctrl+Alt+Z resets the zoom in TrenchBroom", reset->shortcut().toString());
				const QByteArray bytes = serializeLevelMap(shell.levelDocument()).bytes;
				// Each camera command leaves the revision and undo history as they
				// were; TrenchBroom's selection history may add entries between them.
				const auto runCamera = [&](QAction* command) {
					const auto revision = shell.levelDocument().revision;
					const auto history = shell.levelDocument().undoStack.size();
					command->trigger();
					expect(shell.levelDocument().revision == revision && shell.levelDocument().undoStack.size() == history,
						"a camera command adds no edit or undo step", command->objectName());
				};
				QVector<MapViewport*> plans = shell.findChildren<MapViewport*>();
				const auto planStates = [&] {
					QVector<std::pair<QPointF, double>> states;
					for (MapViewport* plan : plans) {
						states.push_back({plan->navigationState().center, plan->zoom()});
					}
					return states;
				};

				// Level Camera keeps the position, drops the pitch and snaps the heading.
				camera->setCameraView({100, -50, 80}, 50, -30);
				auto plansBefore = planStates();
				runCamera(level);
				expect(samePosition(camera->cameraPosition(), ModelVec3 {100, -50, 80}) && nearly(camera->cameraPitch(), 0)
						&& headingDifference(camera->cameraYaw(), 45) < 1.0e-6 && planStates() == plansBefore,
					"Level Camera levels where the camera stands", QStringLiteral("%1 %2").arg(camera->cameraYaw()).arg(camera->cameraPitch()));
				expect(!shell.statusBar()->currentMessage().isEmpty(), "Level Camera reports what it did");

				// Focus Camera on Selection keeps the angles and leaves the plans.
				const QVector<LevelMapSelectionRef> selected {{LevelMapSelectionKind::QuakeBrush, 0}};
				top->setSelectionSet(selected);
				top->selectionSetChanged(selected);
				expect(until([&] { return focus->isEnabled(); }), "a selection enables focusing");
				camera->setCameraView({900, 700, 300}, 200, -20);
				plansBefore = planStates();
				runCamera(focus);
				LevelCameraFocus wanted = expectedFocus(*camera);
				expect(wanted.valid && samePosition(camera->cameraPosition(), wanted.position) && headingDifference(camera->cameraYaw(), 200) < 1.0e-6
						&& nearly(camera->cameraPitch(), -20) && planStates() == plansBefore,
					"Focus Camera on Selection fits the box along the view and leaves the plans");
				const ModelVec3 focused = camera->cameraPosition();
				// With nothing selected the last selection is focused again.
				top->setSelectionSet({});
				top->selectionSetChanged({});
				expect(until([&] { return shell.levelDocument().selection.isEmpty(); }) && focus->isEnabled(), "the last selection keeps focusing available");
				camera->setCameraView({-400, 50, 20}, 200, -20);
				runCamera(focus);
				expect(samePosition(camera->cameraPosition(), focused), "an empty selection focuses on the last selection",
					shell.statusBar()->currentMessage());

				// Reset Camera Zoom restores the profile's field of view.
				const double profileFov = nrc ? 100.0 : 90.0;
				if (nrc) {
					auto state = camera->navigationState();
					state.fieldOfView = 60;
					expect(camera->restoreNavigationState(state), "a saved view's narrower field of view");
				} else {
					sendWheel(camera, 120 * 3, Qt::ShiftModifier);
				}
				const ModelVec3 zoomedAt = camera->cameraPosition();
				expect(camera->fieldOfView() < profileFov - 1, "the field of view is narrowed", QString::number(camera->fieldOfView()));
				runCamera(reset);
				expect(nearly(camera->fieldOfView(), profileFov, 1.0e-9) && samePosition(camera->cameraPosition(), zoomedAt), "Reset Camera Zoom",
					QString::number(camera->fieldOfView()));
				runCamera(reset);
				expect(nearly(camera->fieldOfView(), profileFov, 1.0e-9) && !shell.statusBar()->currentMessage().isEmpty(), "a second reset only reports");

				if (nrc) {
					// Free focus: Tab held during mouse look keeps the selection
					// framed as the camera turns; the wheel moves it nearer.
					top->setSelectionSet(selected);
					top->selectionSetChanged(selected);
					camera->setCameraView({-300, -300, 200}, 30, -10);
					const ModelVec3 idle = camera->cameraPosition();
					camera->setFocus(Qt::OtherFocusReason);
					sendKey(camera, QEvent::KeyPress, Qt::Key_Tab);
					sendKey(camera, QEvent::KeyRelease, Qt::Key_Tab);
					expect(samePosition(camera->cameraPosition(), idle), "Tab outside mouse look leaves the camera alone");
					camera->setLooking(true);
					expect(camera->isLooking(), "mouse look on");
					camera->setFocus(Qt::OtherFocusReason);
					sendKey(camera, QEvent::KeyPress, Qt::Key_Tab);
					wanted = expectedFocus(*camera);
					expect(samePosition(camera->cameraPosition(), wanted.position), "holding Tab focuses at once");
					camera->turnCamera(40, 15);
					wanted = expectedFocus(*camera);
					expect(samePosition(camera->cameraPosition(), wanted.position) && headingDifference(camera->cameraYaw(), 70) < 1.0e-6,
						"turning keeps the selection framed along the new view");
					sendWheel(camera, 120 * 8);
					wanted = expectedFocus(*camera, 8);
					expect(camera->isLooking() && samePosition(camera->cameraPosition(), wanted.position)
							&& nearly(wanted.distance, wanted.fitDistance / 2),
						"eight notches away halve the distance");
					sendKey(camera, QEvent::KeyRelease, Qt::Key_Tab);
					const ModelVec3 released = camera->cameraPosition();
					camera->turnCamera(-25, 0);
					expect(samePosition(camera->cameraPosition(), released), "releasing Tab ends the free focus");
					// Leaving mouse look ends it too, even with the key still down.
					sendKey(camera, QEvent::KeyPress, Qt::Key_Tab);
					camera->setLooking(false);
					const ModelVec3 afterLook = camera->cameraPosition();
					camera->turnCamera(10, 0);
					expect(samePosition(camera->cameraPosition(), afterLook), "ending mouse look ends the free focus");
					sendKey(camera, QEvent::KeyRelease, Qt::Key_Tab);
				}
				expect(serializeLevelMap(shell.levelDocument()).bytes == bytes, "camera commands never change the map");
			}
			if (scale == 200) {
				app.removeTranslator(&expanded);
				app.setLayoutDirection(Qt::LeftToRight);
			}
		}
	}
	if (ok) {
		std::cout << "Level camera focus UI: levelling, focus, fallback, zoom reset and free focus passed." << std::endl;
	}
	return ok ? 0 : 1;
}
