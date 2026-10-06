#include "app/application_shell.h"
#include "app/level_gestures_dialog.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_gestures.h"
#include "tests/level_material_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QLabel>
#include <QKeySequenceEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message, const QString& detail = {})
{
	if (!value) { std::cerr << message << ": " << detail.toStdString() << '\n'; } return value;
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < 30000) { QEventLoop loop; QTimer::singleShot(20, &loop, &QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents); }
	return ready();
}
QByteArray navigation(const LevelViewState& state) { return QJsonDocument(levelBookmarksJson({{"test", "Test", state}})).toJson(); }
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* source, const char*, int) const override { return QStringLiteral("[%1 expanded]").arg(QString::fromUtf8(source)); }
};
int paintedLabelHeight(const QLabel& label)
{
	const int width = label.contentsRect().width() - 2 * label.margin();
	return label.fontMetrics().boundingRect(QRect(0, 0, width, 10000), Qt::TextWordWrap | Qt::TextShowMnemonic, label.text()).height() + 2 * label.margin();
}
bool inspectDialogLayout(QApplication& app, StudioSettings& settings)
{
	bool ok = true;
	for (int scale : {100, 200}) {
		applyStudioTheme(app, studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark, UiDensity::Standard, scale));
		Expanded expansion; if (scale == 200) { app.installTranslator(&expansion); }
		{
			LevelGesturesDialog dialog(settings, "hammer");
			dialog.resize(scale == 100 ? 740 : 1300, scale == 100 ? 690 : 1050);
			if (scale == 200) { dialog.setLayoutDirection(Qt::RightToLeft); }
			dialog.show();
			auto* tabs = dialog.findChild<QTabWidget*>("gestureTabs");
			for (int tab = 0; tab < tabs->count(); ++tab) {
				tabs->setCurrentIndex(tab); app.processEvents(QEventLoop::ExcludeUserInputEvents);
				QEventLoop settle; QTimer::singleShot(50, &settle, &QEventLoop::quit); settle.exec(QEventLoop::ExcludeUserInputEvents);
				for (auto* label : tabs->currentWidget()->findChildren<QLabel*>()) {
					if (!label->objectName().startsWith("gestureLabel.")) { continue; }
					ok &= expect(label->height() >= paintedLabelHeight(*label), "painted label fits its allocated height",
						QStringLiteral("%1 scale=%2 size=%3x%4 hfw=%5 painted=%6").arg(label->objectName()).arg(scale).arg(label->width()).arg(label->height())
						.arg(label->heightForWidth(label->width())).arg(paintedLabelHeight(*label)));
				}
				const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
				if (!captures.isEmpty()) {
					QDir().mkpath(captures); QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); dialog.render(&image);
					ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("layout-%1-%2.png").arg(tab).arg(scale))), "layout inspection rendered");
				}
			}
		}
		if (scale == 200) { app.removeTranslator(&expansion); }
	}
	return ok;
}
}

int main(int argc, char** argv)
{
	// Qt services, actions and rendering only; no injected input or OS capture.
	qputenv("QT_QPA_PLATFORM", "offscreen"); qputenv("QT_SCALE_FACTOR", "1");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	QTemporaryDir temp; if (!temp.isValid()) { return EXIT_FAILURE; }
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini")); StudioSettings settings;
	settings.setRestoreSession(false); settings.setReducedMotion(true);
	settings.setUserShortcuts({{"map.selectAll", {"L"}}});
	if (app.arguments().contains(QStringLiteral("--layout-only"))) { return inspectDialogLayout(app, settings) ? EXIT_SUCCESS : EXIT_FAILURE; }
	LevelMapDocument source; QString error;
	bool ok = expect(tests::createMaterialFixture(temp.path(), &source, &error), "fixture", error);
	const auto map = temp.filePath("gestures.map");
	ok &= expect(tests::putMaterialFile(map, serializeLevelMap(source).bytes), "fixture written");
	for (int scale : {100, 200}) {
		settings.setSelectedEditorProfileId("hammer"); settings.setEditorGestureOverrides("hammer", {});
		settings.setLevelViewLayoutPreference("four-views"); settings.setTextScalePercent(scale);
		settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark); settings.sync();
		applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, scale));
		Expanded expansion; if (scale == 200) { app.installTranslator(&expansion); }
		{
			ApplicationShell shell; shell.resize(scale == 100 ? 1800 : 2900, scale == 100 ? 1200 : 1900); shell.show();
			shell.openPathFromCommandLine(map);
			ok &= expect(until([&] { return shell.levelDocument().brushes.size() == source.brushes.size(); }), "shell map loaded");
			shell.findChild<QAction*>("shell.mode.levels")->trigger();
			shell.findChild<QAction*>("map.selectAll")->trigger();
			auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
			auto* plan = shell.findChild<MapViewport*>("mapViewport");
			const auto original = serializeLevelMap(shell.levelDocument()).bytes;
			plan->moveRequested(16, 0, 0);
			ok &= expect(until([&] { return serializeLevelMap(shell.levelDocument()).bytes != original; }), "edit creates history before customizing gestures");
			LevelViewState initial;
			ok &= expect(until([&] { return shell.captureLevelViewState(&initial, &error); }), "preview and navigation ready");
			auto pose = camera->navigationState(); pose.fieldOfView = 72; pose.perspective = false;
			ok &= expect(camera->restoreNavigationState(pose), "camera zoom customized before gestures");
			shell.activateWindow(); plan->setFocus(Qt::OtherFocusReason);
			shell.findChild<QAction*>("map.maximizeView")->trigger();
			LevelViewState before;
			ok &= expect(shell.captureLevelViewState(&before, &error), "view state captured", error);
			const auto document = serializeLevelMap(shell.levelDocument()).bytes;
			const auto selection = shell.levelDocument().selection;
			auto* command = shell.findChild<QAction*>("editor.gestures");
			if (!expect(command && command->isEnabled(), "gesture command available")) { return EXIT_FAILURE; }
			QTimer::singleShot(0, &shell, [&] {
				auto* dialog = shell.findChild<LevelGesturesDialog*>("levelGesturesDialog");
				if (!expect(dialog, "gesture dialog opened")) { ok = false; return; }
				dialog->resize(scale == 100 ? 740 : 1300, scale == 100 ? 690 : 1050);
				if (scale == 200) { dialog->setLayoutDirection(Qt::RightToLeft); }
				auto set = [&](const char* id, const char* value) {
					auto* field = dialog->findChild<QComboBox*>(QStringLiteral("gesture.") + QString::fromLatin1(id));
					ok &= expect(field && field->findData(QString::fromLatin1(value)) >= 0, "editable gesture choice");
					if (field) { field->setCurrentIndex(field->findData(QString::fromLatin1(value))); }
				};
				auto* apply = dialog->findChild<QPushButton*>("gestureApply");
				set("plan.panButtons", "right+middle"); set("plan.zoomDragButton", "right"); set("plan.zoomDragModifiers", "none");
				ok &= expect(!apply->isEnabled() && !dialog->findChild<QLabel*>("gestureStatus")->text().isEmpty(), "conflict blocks apply with visible diagnostic");
				ok &= expect(settings.editorGestureOverrides("hammer").isEmpty(), "draft does not write preferences");
				set("plan.zoomDragButton", ""); set("plan.zoomDragModifiers", ""); set("camera.wheel", "dolly-to-pointer");
				set("camera.driveButton", "right");
				ok &= expect(!apply->isEnabled(), "steering cannot shadow the existing pan gesture");
				set("camera.panModifiers", "ctrl"); set("camera.discreteDriveKeys", "on"); set("plan.fixedCameraSteps", "on");
				const auto key = [&](const char* id, const QString& value) {
					set(id, "custom");
					auto* editor = dialog->findChild<QKeySequenceEdit*>(QStringLiteral("gestureKey.") + QString::fromLatin1(id));
					ok &= expect(editor && !editor->accessibleName().isEmpty() && QAccessible::queryAccessibleInterface(editor), "native key editor is accessible");
					if (editor) { editor->setKeySequence(QKeySequence::fromString(value, QKeySequence::PortableText)); }
				};
				key("camera.flyKeys.forward", "I"); key("camera.flyKeys.back", "I");
				ok &= expect(!apply->isEnabled(), "duplicate directions cannot be applied from the GUI");
				key("camera.flyKeys.back", "K"); key("camera.flyKeys.left", "J"); key("camera.flyKeys.right", "L");
				key("camera.lookToggleKey", "Ctrl+Space");
				key("camera.lookHoldKey", "I");
				ok &= expect(!apply->isEnabled(), "a hold key cannot shadow camera movement");
				key("camera.lookHoldKey", "P"); key("plan.panHoldKey", "P");
				key("camera.driveKeys.pitchUp", "PgUp"); key("camera.driveKeys.pitchDown", "PgDown");
				auto* overlaps = dialog->findChild<QToolButton*>("gestureShortcutOverlaps");
				ok &= expect(overlaps->isVisible(), "command overlap is visible before applying navigation keys");
				overlaps->click();
				ok &= expect(dialog->findChild<QPlainTextEdit*>("gestureShortcutDetails")->isVisible()
					&& !dialog->findChild<QPlainTextEdit*>("gestureShortcutDetails")->toPlainText().isEmpty(), "overlap details explain command precedence");
				dialog->findChild<QTabWidget*>("gestureTabs")->setCurrentIndex(2);
				{ QEventLoop settle; QTimer::singleShot(50, &settle, &QEventLoop::quit); settle.exec(QEventLoop::ExcludeUserInputEvents); }
				ok &= expect(dialog->rect().contains(QRect(apply->mapTo(dialog, QPoint()), apply->size())), "overlap details keep Apply visible");
				const auto detailCaptures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
				if (!detailCaptures.isEmpty()) {
					QDir().mkpath(detailCaptures); QImage image(dialog->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); dialog->render(&image);
					ok &= expect(image.save(QDir(detailCaptures).filePath(QStringLiteral("gesture-overlaps-%1.png").arg(scale))), "shortcut overlap details rendered");
				}
				overlaps->click();
				ok &= expect(apply->isEnabled(), "valid draft can apply"); apply->click();
				const auto* status = dialog->findChild<QLabel*>("gestureStatus");
				ok &= expect(status->accessibleDescription() == status->text(), "applied status updates accessible description");
				for (auto* pane : {plan, shell.findChild<MapViewport*>("mapViewport1"), shell.findChild<MapViewport*>("mapViewport2")}) {
					ok &= expect(pane->controls().panButtons.contains(Qt::RightButton) && pane->controls().panButtons.contains(Qt::MiddleButton)
						&& pane->controls().panHoldKey == "P", "all plan panes adopt gestures and hold navigation");
				}
				ok &= expect(camera->cameraControls().wheel == CameraWheel::DollyToPointer, "camera adopts gestures");
				ok &= expect(camera->cameraControls().driveButton == Qt::RightButton && camera->cameraControls().panModifiers == Qt::ControlModifier
					&& camera->cameraControls().discreteDriveKeys && plan->controls().fixedCameraSteps, "classic navigation settings apply through the common dialog");
				ok &= expect(camera->cameraControls().flyKeys.forward == "I" && camera->cameraControls().flyKeys.back == "K"
					&& camera->cameraControls().flyKeys.left == "J" && camera->cameraControls().flyKeys.right == "L"
					&& camera->cameraControls().lookToggleKey == "Ctrl+Space" && camera->cameraControls().lookHoldKey == "P"
					&& camera->cameraControls().driveKeys.pitchUp == "PgUp" && camera->cameraControls().driveKeys.pitchDown == "PgDown",
					"camera adopts custom movement, pitch, hold and look-toggle keys");
				LevelViewState after;
				ok &= expect(shell.captureLevelViewState(&after, &error) && navigation(after) == navigation(before), "gesture apply preserves camera FOV, navigation and bookmarks", error);
				ok &= expect(shell.findChild<QAction*>("map.maximizeView")->isChecked() && plan->isVisible(), "gesture apply preserves temporary workspace");
				ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == document && shell.levelDocument().selection == selection, "gesture apply preserves authoring state");
				const auto saved = settings.editorGestureOverrides("hammer");
				const auto file = temp.filePath(QStringLiteral("gesture-%1.json").arg(scale));
				ok &= expect(dialog->exportFile(file, false, &error), "dialog export", error);
				dialog->findChild<QPushButton*>("gestureReset")->click();
				ok &= expect(dialog->draft().isEmpty() && settings.editorGestureOverrides("hammer") == saved, "reset stages without applying");
				ok &= expect(dialog->importFile(file, &error) && dialog->draft() == saved, "dialog import stages exact file", error);
				const auto wrong = temp.filePath("other-profile.json");
				ok &= expect(writeLevelGestures(wrong, "blender", {}, true, &error) && !dialog->importFile(wrong, &error) && dialog->draft() == saved, "wrong-profile import preserves draft");
				ok &= expect(!dialog->exportFile(settings.storageLocation(), true, &error), "settings cannot be overwritten by gesture export");
#ifdef Q_OS_WIN
				ok &= expect(!dialog->exportFile(settings.storageLocation().toUpper(), true, &error), "settings protection respects Windows path casing");
#endif
				auto* tabs = dialog->findChild<QTabWidget*>("gestureTabs");
				for (int tab = 0; tab < tabs->count(); ++tab) {
					tabs->setCurrentIndex(tab); app.processEvents(QEventLoop::ExcludeUserInputEvents);
					// Let deferred form geometry and text layout settle before the
					// final paint; the first exposed frame can precede that layout.
					{ QEventLoop settle; QTimer::singleShot(50, &settle, &QEventLoop::quit); settle.exec(QEventLoop::ExcludeUserInputEvents); }
					ok &= expect(until([&] {
						for (auto* label : tabs->currentWidget()->findChildren<QLabel*>()) {
							if (label->objectName().startsWith("gestureLabel.") && label->height() < paintedLabelHeight(*label)) { return false; }
						}
						return true;
					}), "wrapped gesture labels are fully allocated at every text scale");
					for (auto* field : tabs->currentWidget()->findChildren<QComboBox*>()) {
						ok &= expect(!field->accessibleName().isEmpty() && QAccessible::queryAccessibleInterface(field) && field->focusPolicy() != Qt::NoFocus, "gesture fields have keyboard and accessible controls");
					}
					ok &= expect(dialog->rect().contains(QRect(apply->mapTo(dialog, QPoint()), apply->size())), "footer remains in dialog");
					const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
					if (!captures.isEmpty()) {
						QDir().mkpath(captures); QImage image(dialog->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); dialog->render(&image);
						ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("gestures-%1-%2.png").arg(tab).arg(scale))), "dialog rendered");
						auto* scroll = qobject_cast<QScrollArea*>(tabs->currentWidget());
						if (scroll) {
							scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum()); app.processEvents(QEventLoop::ExcludeUserInputEvents);
							dialog->render(&image);
							ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("gestures-tail-%1-%2.png").arg(tab).arg(scale))), "hold and pitch fields rendered at the end of each page");
						}
					}
				}
				set("plan.panButtons", "none"); dialog->reject();
			});
			command->trigger();
			ok &= expect(settings.editorGestureOverrides("hammer").value("plan.panButtons") == "right+middle", "closing discards unapplied edits");
			shell.findChild<QAction*>("map.undo")->trigger();
			ok &= expect(until([&] { return serializeLevelMap(shell.levelDocument()).bytes == original; }), "gesture apply preserves prior undo history");
			shell.findChild<QAction*>("map.redo")->trigger();
			ok &= expect(until([&] { return serializeLevelMap(shell.levelDocument()).bytes == document; }), "gesture apply preserves redo history");
			shell.findChild<QAction*>("map.undo")->trigger();
			ok &= expect(until([&] { return serializeLevelMap(shell.levelDocument()).bytes == original; }), "fixture restored before closing");
			auto* profile = shell.findChild<QComboBox*>("editorProfileCombo");
			profile->setCurrentIndex(profile->findData("trenchbroom"));
			profile->setCurrentIndex(profile->findData("hammer"));
			ok &= expect(camera->cameraControls().wheel == CameraWheel::DollyToPointer, "profile reselect restores its overrides");
			ok &= expect(camera->cameraControls().flyKeys.forward == "I" && camera->cameraControls().lookHoldKey == "P"
				&& camera->cameraControls().driveKeys.pitchUp == "PgUp" && plan->controls().panHoldKey == "P", "profile reselect restores all navigation keys");
			shell.findChild<QAction*>("levelControlsReference")->trigger();
			auto* reference = shell.findChild<QDialog*>("levelControlsDialog");
			ok &= expect(reference && reference->windowTitle().contains("customized"), "reference identifies effective customization");
			if (reference) { reference->hide(); }
			shell.close();
		}
		if (scale == 200) { app.removeTranslator(&expansion); }
	}
	StudioSettings readOnly(settings.storageLocation(), StudioSettings::AccessMode::ReadOnly);
	LevelGesturesDialog readOnlyDialog(readOnly, "hammer");
	ok &= expect(!readOnlyDialog.findChild<QPushButton*>("gestureApply")->isEnabled()
		&& readOnlyDialog.findChild<QPushButton*>("gestureExport")->isEnabled(), "protected settings disable Apply but allow export");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
