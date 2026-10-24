// Offscreen checks of the Radiant texture step commands on the Levels page,
// under the NetRadiant Custom and TrenchBroom profiles at 100% and at 200%
// text with the high-visibility theme, an expanding translator and
// right-to-left layout. NetRadiant Custom brings a 45° Rotation step about
// the texture origin, Shift+Page Up/Down for Rotate Texture +/− and
// Ctrl+arrows for Texture Scale U/V −/+; other profiles turn 15° about each
// face centre and leave the scale steps unbound. Each command is one exact
// undo step with NetRadiant Custom's arithmetic, a scale reaching zero flips
// sign, Ctrl+Left in the 2D view steps the texture instead of panning while
// there is a target (and pans without one), hidden objects are not targets, a
// locked brush refuses the step, and the new controls are accessible.

#include "app/application_shell.h"
#include "app/level_surface_tools.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/editor_profiles.h"
#include "core/level_camera_keys.h"
#include "core/level_editor_controls.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/studio_semantics.h"
#include "tests/level_surface_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QLabel>
#include <QMenu>
#include <QScrollBar>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#ifdef VIBESTUDIO_HAVE_QTEST
#include <QTest>
#endif

#include <cmath>
#include <iostream>

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
	while (!ready() && timer.elapsed() < 30000) {
		QEventLoop loop;
		QTimer::singleShot(15, &loop, &QEventLoop::quit);
		loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return ready();
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

bool writeFile(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

} // namespace

int main(int argc, char** argv)
{
	// Owned offscreen Qt widgets and semantic events only; no system input.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "temporary directory")) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	StudioSettings settings;
	settings.setRestoreSession(false);
	settings.setReducedMotion(true);
	// The classic fixture: every face has shift 7 9, rotation 17, scale -0.5 2.
	const QByteArray source = tests::surfaceFixture(QStringLiteral("classic"));
	const QString path = temp.filePath(QStringLiteral("steps.map"));
	LevelMapDocument lockedMap;
	QString error, layer;
	const bool lockedBuilt = loadLevelMapBytes({path, {}, QStringLiteral("idtech3")}, source, &lockedMap, &error)
		&& createLevelSceneNode(&lockedMap, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
		&& assignLevelSceneObjects(&lockedMap, layer, {QStringLiteral("brush:0")}, &error) && setLevelSceneLocked(&lockedMap, layer, true, &error);
	const QString lockedPath = temp.filePath(QStringLiteral("locked.map"));
	if (!expect(writeFile(path, source) && lockedBuilt && writeFile(lockedPath, serializeLevelMap(lockedMap).bytes), "fixtures", error)) {
		return 1;
	}

	phase = QStringLiteral("profiles");
	const auto nrcControls = netRadiantCustomLevelControls();
	bool rowFound = false;
	for (const auto& row : levelEditorControlRows(nrcControls)) {
		rowFound |= row.action == QStringLiteral("Texture rotation step") && row.gesture.contains(QStringLiteral("45")) && row.gesture.contains(QStringLiteral("origin"));
	}
	expect(nrcControls.surfaceRotationStep == 45 && nrcControls.surfaceRotateAboutOrigin && rowFound, "NetRadiant Custom turns 45° about the texture origin");
	const auto defaults = levelEditorControlsForProfile(QStringLiteral("vibestudio-default"));
	expect(defaults.surfaceRotationStep == 15 && !defaults.surfaceRotateAboutOrigin, "the default profile turns 15° about each face centre");
	const auto netradiant = levelEditorControlsForProfile(QStringLiteral("netradiant"));
	bool netradiantKey = false;
	for (const auto& binding : netradiant.keys) {
		netradiantKey |= binding.commandId.startsWith(QLatin1String("map.surfaceScaleStep")) || binding.commandId.startsWith(QLatin1String("map.surfaceRotate"));
	}
	expect(!netradiantKey && !netradiant.surfaceRotateAboutOrigin, "the unaudited NetRadiant baseline keeps the studio defaults");

	const QStringList ids{QStringLiteral("map.surfaceScaleStepLeft"), QStringLiteral("map.surfaceScaleStepRight"), QStringLiteral("map.surfaceScaleStepDown"),
		QStringLiteral("map.surfaceScaleStepUp"), QStringLiteral("map.surfaceRotateLeft"), QStringLiteral("map.surfaceRotateRight")};
	for (const auto& id : ids) {
		CommandPaletteEntry entry;
		expect(commandPaletteEntryForCommandId(id, &entry) && !entry.label.isEmpty() && !entry.summary.isEmpty(), "palette entry", id);
	}

	for (const char* profile : {"netradiant-custom", "trenchbroom"}) {
		for (const int scale : {100, 200}) {
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			std::cout << "Surface step UI " << phase.toStdString() << std::endl;
			const bool nrc = QLatin1String(profile) == QLatin1String("netradiant-custom");
			settings.setSelectedEditorProfileId(QString::fromLatin1(profile));
			settings.setTextScalePercent(scale);
			settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark);
			settings.sync();
			applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, scale));
			Expanded expanded;
			if (scale == 200) {
				app.installTranslator(&expanded);
				app.setLayoutDirection(Qt::RightToLeft);
			}
			{
				ApplicationShell shell;
				shell.resize(scale == 100 ? 1650 : 2400, scale == 100 ? 1000 : 1500);
				for (auto* combo : shell.findChildren<QComboBox*>()) {
					if (combo->objectName() == QStringLiteral("levelMapEngineHint") || combo->accessibleName().contains(QStringLiteral("engine hint"))) {
						combo->setCurrentIndex(combo->findData(QStringLiteral("idtech3")));
					}
				}
				shell.show();
				shell.openPathFromCommandLine(path);
				const auto action = [&](const QString& id) { return shell.findChild<QAction*>(id); };
				action(QStringLiteral("shell.mode.levels"))->trigger();
				auto* view = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
				auto* panel = static_cast<LevelSurfaceTools*>(shell.findChild<QScrollArea*>(QStringLiteral("levelSurfaceTools")));
				if (!expect(view && panel && until([&] { return shell.levelDocument().sourcePath == path && !shell.levelDocument().brushes.isEmpty(); }), "map ready")) {
					return 1;
				}
				auto* rotation = panel->findChild<QDoubleSpinBox*>(QStringLiteral("surfaceQuickRotation"));
				auto* anchor = panel->findChild<QComboBox*>(QStringLiteral("surfaceQuickRotationAnchor"));
				auto* step = panel->findChild<QDoubleSpinBox*>(QStringLiteral("surfaceQuickScaleStep"));
				if (!expect(rotation && anchor && step, "rotation step, anchor and scale step controls")) {
					return 1;
				}
				expect(rotation->value() == (nrc ? 45 : 15) && anchor->currentIndex() == (nrc ? 1 : 0) && panel->rotatesAboutOrigin() == nrc && step->value() == 0.5,
					"the profile's rotation step and anchor", QStringLiteral("%1° anchor %2").arg(rotation->value()).arg(anchor->currentIndex()));

				// Registration, keys, menus and accessible buttons.
				for (const auto& id : ids) {
					QAction* command = action(id);
					if (!expect(command != nullptr, "registered", id)) {
						return 1;
					}
					bool menu = false;
					for (auto* owner : command->associatedObjects()) { menu |= qobject_cast<QMenu*>(owner) != nullptr; }
					auto* button = panel->findChild<QToolButton*>(id);
					expect(!command->text().isEmpty() && !command->statusTip().isEmpty() && menu && !command->isEnabled(), "labelled, in a menu, disabled without a target", id);
					expect(button && !button->accessibleName().isEmpty() && button->focusPolicy() != Qt::NoFocus && QAccessible::queryAccessibleInterface(button),
						"accessible Surfaces button", id);
				}
				const auto keys = [&](const char* id) { return action(QString::fromLatin1(id))->shortcuts(); };
				if (nrc) {
					expect(keys("map.surfaceRotateRight") == QList<QKeySequence>{QKeySequence(Qt::ShiftModifier | Qt::Key_PageUp)}
							&& keys("map.surfaceRotateLeft") == QList<QKeySequence>{QKeySequence(Qt::ShiftModifier | Qt::Key_PageDown)}
							&& keys("map.surfaceScaleStepUp") == QList<QKeySequence>{QKeySequence(Qt::ControlModifier | Qt::Key_Up)}
							&& keys("map.surfaceScaleStepDown") == QList<QKeySequence>{QKeySequence(Qt::ControlModifier | Qt::Key_Down)}
							&& keys("map.surfaceScaleStepLeft") == QList<QKeySequence>{QKeySequence(Qt::ControlModifier | Qt::Key_Left)}
							&& keys("map.surfaceScaleStepRight") == QList<QKeySequence>{QKeySequence(Qt::ControlModifier | Qt::Key_Right)},
						"NetRadiant Custom's texture step keys", QKeySequence::listToString(keys("map.surfaceRotateRight")));
					// The camera's drive keys leave the chords to the commands.
					auto* camera = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
					expect(camera && !cameraKeyMotions(camera->cameraControls(), false, Qt::NoButton, Qt::NoModifier, Qt::ControlModifier).contains(Qt::Key_Up)
							&& !cameraKeyMotions(camera->cameraControls(), false, Qt::NoButton, Qt::NoModifier, Qt::ControlModifier).contains(Qt::Key_Left)
							&& !cameraKeyMotions(camera->cameraControls(), false, Qt::NoButton, Qt::NoModifier, Qt::ShiftModifier).contains(Qt::Key_PageUp),
						"the 3D camera does not claim the texture step chords");
				} else {
					expect(keys("map.surfaceScaleStepUp").isEmpty() && keys("map.surfaceScaleStepLeft").isEmpty() && keys("map.surfaceRotateRight").isEmpty(),
						"no texture step keys outside NetRadiant Custom");
				}

				const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
				const auto face = [&] { return shell.levelDocument().brushes.first().faces.first(); };
				const auto settle = [&] { return until([&] { return !shell.levelSurfaceEditsPending(); }); };
				const auto statusText = [&] { return panel->findChild<QLabel*>(QStringLiteral("surfaceQuickStatus"))->text(); };
				action(QStringLiteral("map.selectAll"))->trigger();
				for (const auto& id : ids) { expect(action(id)->isEnabled(), "enabled with a brush selected", id); }
				const QByteArray baseline = bytes();
				const auto depth = shell.levelDocument().undoStack.size();
				const auto undoBack = [&](const char* label) {
					action(QStringLiteral("map.undo"))->trigger();
					expect(bytes() == baseline && shell.levelDocument().undoStack.size() == depth, label, shell.statusBar()->currentMessage());
				};

				// Texture Scale V +: the scale value takes the step; the shift stays.
				action(QStringLiteral("map.surfaceScaleStepUp"))->trigger();
				expect(settle() && face().scaleY == 2.5 && face().scaleX == -0.5 && face().shiftX == 7 && face().shiftY == 9
						&& shell.levelDocument().undoStack.size() == depth + 1,
					"Texture Scale V + adds the step about the texture origin in one undo step", statusText());
				const QByteArray scaled = bytes();
				undoBack("undo restores the exact bytes");
				action(QStringLiteral("map.redo"))->trigger();
				expect(bytes() == scaled, "redo repeats the step");
				undoBack("undo again");
				// Texture Scale U +: -0.5 + 0.5 would collapse, so the sign flips.
				action(QStringLiteral("map.surfaceScaleStepRight"))->trigger();
				expect(settle() && face().scaleX == 0.5, "a scale reaching zero flips its sign", QString::number(face().scaleX));
				undoBack("undo the flip");
				// Two Scale V − steps in one burst: one batch, one undo step.
				action(QStringLiteral("map.surfaceScaleStepDown"))->trigger();
				action(QStringLiteral("map.surfaceScaleStepDown"))->trigger();
				expect(settle() && face().scaleY == 1 && shell.levelDocument().undoStack.size() == depth + 1, "a burst of steps is one undo step",
					QString::number(face().scaleY));
				undoBack("undo the burst");
				// A queued step can be cancelled before it lands.
				action(QStringLiteral("map.surfaceScaleStepUp"))->trigger();
				expect(shell.levelSurfaceEditsPending(), "the step is queued on the surface worker");
				shell.cancelLevelSurfaceEdits();
				expect(settle() && bytes() == baseline && shell.levelDocument().undoStack.size() == depth, "a cancelled step leaves the map alone");
				// Rotate Texture +: NRC adds 45° and keeps the shift; others turn 15°
				// about each face centre.
				action(QStringLiteral("map.surfaceRotateRight"))->trigger();
				if (nrc) {
					expect(settle() && std::abs(face().rotation - 62) < 1e-6 && face().shiftX == 7 && face().shiftY == 9, "Rotate Texture + turns 45° about the texture origin",
						QStringLiteral("%1 / %2 %3").arg(face().rotation).arg(face().shiftX).arg(face().shiftY));
				} else {
					expect(settle() && std::abs(face().rotation - 32) < 1e-6 && (face().shiftX != 7 || face().shiftY != 9), "Rotate Texture + turns 15° about each face centre",
						QStringLiteral("%1 / %2 %3").arg(face().rotation).arg(face().shiftX).arg(face().shiftY));
				}
				undoBack("undo the turn");
				action(QStringLiteral("map.surfaceRotateLeft"))->trigger();
				expect(settle() && std::abs(face().rotation - (nrc ? -28 : 2)) < 1e-6, "Rotate Texture − subtracts the step", QString::number(face().rotation));
				undoBack("undo the opposite turn");
				// Switching the anchor in the panel changes the next turn.
				anchor->setCurrentIndex(nrc ? 0 : 1);
				action(QStringLiteral("map.surfaceRotateRight"))->trigger();
				expect(settle() && (nrc ? (face().shiftX != 7 || face().shiftY != 9) : (face().shiftX == 7 && face().shiftY == 9)), "Rotate about chooses the anchor");
				undoBack("undo the re-anchored turn");
				anchor->setCurrentIndex(nrc ? 1 : 0);

#ifdef VIBESTUDIO_HAVE_QTEST
				if (nrc && scale == 100) {
					// The keys in the 2D view, ahead of its Ctrl+arrow panning.
					shell.activateWindow();
					expect(until([&] { return QApplication::activeWindow() == &shell; }), "owned offscreen window is active");
					view->setFocus(Qt::OtherFocusReason);
					expect(until([&] { return view->hasFocus(); }), "plan view takes focus");
					int panned = 0;
					const auto connection = QObject::connect(view, &MapViewport::viewChanged, view, [&panned] { ++panned; });
					QTest::keyClick(view, Qt::Key_Left, Qt::ControlModifier);
					expect(settle() && face().scaleX == -1 && panned == 0, "Ctrl+Left steps the U scale instead of panning",
						QStringLiteral("scale %1, %2 pans").arg(face().scaleX).arg(panned));
					undoBack("undo the Ctrl+Left step");
					view->setFocus(Qt::OtherFocusReason);
					QTest::keyClick(view, Qt::Key_PageUp, Qt::ShiftModifier);
					expect(settle() && std::abs(face().rotation - 62) < 1e-6, "Shift+Page Up turns the texture 45°", QString::number(face().rotation));
					undoBack("undo the Shift+Page Up turn");
					action(QStringLiteral("map.selectNone"))->trigger();
					view->setFocus(Qt::OtherFocusReason);
					panned = 0;
					QTest::keyClick(view, Qt::Key_Left, Qt::ControlModifier);
					expect(panned > 0 && bytes() == baseline, "without a target Ctrl+Left still pans the 2D view", QString::number(panned));
					QObject::disconnect(connection);
					action(QStringLiteral("map.selectAll"))->trigger();
				}
#endif

				// Hidden objects are not targets.
				action(QStringLiteral("map.hideSelection"))->trigger();
				expect(!action(QStringLiteral("map.surfaceScaleStepUp"))->isEnabled() && !action(QStringLiteral("map.surfaceRotateRight"))->isEnabled(),
					"hidden objects are not surface targets");
				action(QStringLiteral("map.showAll"))->trigger();

				// Accessible, scalable controls.
				for (QWidget* control : std::initializer_list<QWidget*>{anchor, step}) {
					expect(!control->accessibleName().isEmpty() && !control->accessibleDescription().isEmpty() && control->focusPolicy() != Qt::NoFocus
							&& QAccessible::queryAccessibleInterface(control),
						"accessible step control", control->objectName());
				}
				expect(step->layoutDirection() == Qt::LeftToRight, "the numeric step reads left to right");
				panel->ensureWidgetVisible(step);
				app.processEvents(QEventLoop::ExcludeUserInputEvents);
				expect(panel->horizontalScrollBar()->maximum() == 0, "the Surfaces tab fits without horizontal scrolling");

				// A locked brush refuses the step and says why.
				shell.openPathFromCommandLine(lockedPath);
				action(QStringLiteral("shell.mode.levels"))->trigger();
				if (expect(until([&] { return shell.levelDocument().sourcePath == lockedPath && !shell.levelDocument().scene.nodes.isEmpty(); }), "locked map ready")) {
					action(QStringLiteral("map.selectAll"))->trigger();
					const QByteArray lockedBaseline = bytes();
					if (action(QStringLiteral("map.surfaceScaleStepUp"))->isEnabled()) {
						action(QStringLiteral("map.surfaceScaleStepUp"))->trigger();
						settle();
						expect(statusText().contains(QStringLiteral("Unlock")), "the refusal names the lock", statusText());
					}
					expect(bytes() == lockedBaseline, "a locked brush keeps its mapping");
				}

				// Another profile brings its own step and anchor.
				if (nrc && scale == 100) {
					QAction* trenchbroom = nullptr;
					if (auto* menu = shell.findChild<QMenu*>(QStringLiteral("levelControlsMenu"))) {
						for (QAction* choice : menu->actions()) {
							if (choice->data().toString() == QStringLiteral("trenchbroom")) { trenchbroom = choice; }
						}
					}
					if (expect(trenchbroom != nullptr, "TrenchBroom in the controls menu")) {
						trenchbroom->trigger();
						expect(until([&] { return rotation->value() == 15 && anchor->currentIndex() == 0; }) && keys("map.surfaceScaleStepUp").isEmpty(),
							"switching profile brings its step, anchor and keys", QStringLiteral("%1° anchor %2").arg(rotation->value()).arg(anchor->currentIndex()));
					}
				}
				shell.close();
			}
			if (scale == 200) {
				app.removeTranslator(&expanded);
				app.setLayoutDirection(Qt::LeftToRight);
			}
		}
	}
	QEventLoop drain;
	QTimer::singleShot(200, &drain, &QEventLoop::quit);
	drain.exec();
	std::cout << (ok ? "Surface step UI checks passed" : "Surface step UI checks failed") << std::endl;
	return ok ? 0 : 1;
}
