#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_document.h"
#include "core/level_repeat.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QProgressBar>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		std::cerr << "FAIL: " << label << ": " << detail.toStdString() << '\n';
	}
	return value;
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 30000) {
		QEventLoop loop;
		QTimer::singleShot(20, &loop, &QEventLoop::quit);
		loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return ready();
}
class Expanded final : public QTranslator
{
  public:
	bool isEmpty() const override
	{
		return false;
	}
	QString translate(const char*, const char* text, const char*, int) const override
	{
		const auto source = QString::fromUtf8(text);
		return QStringLiteral("[%1%2]").arg(source, QString(source.size() / 3, QLatin1Char('~')));
	}
};
} // namespace
int main(int argc, char** argv)
{
	// Owned offscreen widgets, semantic methods and render targets only. No
	// operating-system keyboard/mouse input or screen capture is used.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	StudioSettings settings;
	settings.setRestoreSession(false);
	settings.setReducedMotion(true);
	settings.setSelectedEditorProfileId("trenchbroom");
	settings.sync();
	LevelMapDocument map;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	QString error;
	if (!createLevelMap(create, &map, &error) ||
	    !addLevelMapBoxBrush(&map, {0, 0, 0, true}, {32, 64, 96, true}, "studio/checker", nullptr, &error) ||
	    !addLevelMapBoxBrush(&map, {256, 0, 0, true}, {288, 64, 96, true}, "studio/checker", nullptr, &error)) {
		return 1;
	}
	const auto path = temp.filePath("repeat.map");
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return 1;
	}
	file.write(serializeLevelMap(map).bytes);
	file.close();
	const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
	if (!captures.isEmpty()) {
		QDir().mkpath(captures);
	}
	bool ok = true;
	for (int scale : {100, 200}) {
		settings.setTheme(scale == 200 ? StudioTheme::HighContrastLight : StudioTheme::Dark);
		settings.setTextScalePercent(scale);
		settings.sync();
		Expanded translator;
		if (scale == 200) {
			app.installTranslator(&translator);
		}
		app.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
		{
			ApplicationShell shell;
			shell.resize(1800, 1200);
			shell.show();
			shell.openPathFromCommandLine(path);
			shell.findChild<QAction*>("shell.mode.levels")->trigger();
			auto* plan = shell.findChild<MapViewport*>("mapViewport");
			auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
			if (!plan || !camera || !until([&] { return shell.levelDocument().brushes.size() == 2 && camera->isEnabled(); })) {
				return 1;
			}
			auto* repeat = shell.findChild<QAction*>("map.repeat");
			auto* clear = shell.findChild<QAction*>("map.clearRepeat");
			auto* copy = shell.findChild<QAction*>("map.copyRepeatRecipe");
			auto* compiler = shell.findChild<QAction*>("compiler.run");
			if (!expect(repeat && clear && copy && compiler, "registered repeat actions")) {
				return 1;
			}
			ok &= expect(!repeat->isEnabled() && !clear->isEnabled() && !copy->isEnabled(), "empty repeat state disables commands");
			ok &= expect(repeat->toolTip().contains("Ctrl+R") && clear->toolTip().contains("Ctrl+Shift+R") &&
			                 compiler->toolTip().contains("Ctrl+Alt+B"),
			             "TB shortcut hints coexist with compiler keys");
			const auto pick = [&](int id) { plan->selectSceneObjects({{LevelMapSelectionKind::QuakeBrush, id}}, LevelScenePick::Replace); };
			pick(0);
			shell.findChild<QAction*>("map.rotateRight")->trigger();
			shell.findChild<QAction*>("map.duplicateSelection")->trigger();
			ok &= expect(shell.levelDocument().repeat.actions.size() == 2 && repeat->isEnabled() && clear->isEnabled(),
			             "shell transforms record a sequence", shell.statusBar()->currentMessage());
			pick(1);
			copy->trigger();
			QVector<LevelRepeatAction> parsed;
			ok &= expect(
			    parseLevelRepeatCommands(QJsonDocument::fromJson(QApplication::clipboard()->text().toUtf8()).object(), &parsed, &error) &&
			        parsed == shell.levelDocument().repeat.actions,
			    "GUI exports a reusable CLI recipe", error);
			const auto before = serializeLevelMap(shell.levelDocument()).bytes;
			const auto depth = shell.levelDocument().undoStack.size();
			for (int mode : {0, 1}) {
				bool visited = false;
				QTimer drive;
				drive.setSingleShot(true);
				QObject::connect(&drive, &QTimer::timeout, &shell, [&] {
					auto* dialog = shell.findChild<QDialog*>("levelPlacementTaskDialog");
					if (!dialog) {
						return;
					}
					visited = true;
					auto* bar = dialog->findChild<QProgressBar*>("levelPlacementTaskProgress");
					auto* cancel = dialog->findChild<QPushButton*>("levelPlacementTaskCancel");
					ok &= expect(bar && cancel && QAccessible::queryAccessibleInterface(bar)->role() == QAccessible::ProgressBar &&
					                 !cancel->accessibleDescription().isEmpty(),
					             "accessible cancellable repeat worker");
					dialog->ensurePolished();
					dialog->adjustSize();
					if (!captures.isEmpty()) {
						QImage image(dialog->size(), QImage::Format_ARGB32_Premultiplied);
						dialog->render(&image);
						ok &= image.save(QDir(captures).filePath(QStringLiteral("repeat-progress-%1.png").arg(scale)));
					}
					if (mode == 0) {
						dialog->reject();
					} else {
						clear->trigger();
					}
				});
				drive.start(0);
				repeat->trigger();
				drive.stop();
				ok &= expect(visited && serializeLevelMap(shell.levelDocument()).bytes == before &&
				                 shell.levelDocument().undoStack.size() == depth,
				             mode == 0 ? "cancel publishes no partial repeat" : "clearing repeat history rejects an in-flight candidate",
				             shell.statusBar()->currentMessage());
			}
			ok &= expect(shell.levelDocument().repeat.actions.isEmpty() && !repeat->isEnabled(),
			             "stale completion cannot restore cleared history");
			shell.findChild<QAction*>("map.rotateRight")->trigger();
			auto expected = shell.levelDocument();
			const auto original = serializeLevelMap(expected).bytes;
			const auto originalDepth = expected.undoStack.size();
			ok &= expect(repeatLevelMapCommands(&expected, {settings.levelTextureLock(), settings.levelAllowValve220()}, &error),
			             "reference repeat", error);
			repeat->trigger();
			const auto after = serializeLevelMap(shell.levelDocument()).bytes;
			ok &= expect(after == serializeLevelMap(expected).bytes && shell.levelDocument().undoStack.size() == originalDepth + 1,
			             "shell replay equals shared core and creates one undo", shell.statusBar()->currentMessage());
			shell.findChild<QAction*>("map.undo")->trigger();
			ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == original && shell.levelDocument().repeat.actions.isEmpty(),
			             "undo restores geometry and clears repeat");
			shell.findChild<QAction*>("map.redo")->trigger();
			ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == after && !repeat->isEnabled(), "redo leaves repeat cleared");
			pick(0);
			shell.findChild<QAction*>("map.rotateRight")->trigger();
			const auto recorded = shell.levelDocument().repeat;
			const auto saved = temp.filePath(QStringLiteral("saved-%1.map").arg(scale));
			ok &= expect(shell.saveLevelDocument(saved, false, &error) && shell.levelDocument().repeat == recorded && repeat->isEnabled(),
			             "Save As retains session repeat history", error);
			for (const auto id : {"map.repeat", "map.clearRepeat", "map.copyRepeatRecipe"}) {
				auto* row = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-") + id);
				ok &= expect(row && row->focusPolicy() == Qt::TabFocus && !row->accessibleName().isEmpty() && !row->toolTip().isEmpty(),
				             "Tools repeat rows have keyboard focus and accessible names");
				if (row && !captures.isEmpty()) {
					// Inspect the actual button at its layout size hint, independent
					// of a collapsed sidebar's retained child geometry.
					QPushButton preview(row->icon(), row->text());
					preview.setFont(row->font());
					preview.setPalette(row->palette());
					preview.setLayoutDirection(row->layoutDirection());
					preview.resize(preview.sizeHint());
					preview.ensurePolished();
					QImage image(preview.size(), QImage::Format_ARGB32_Premultiplied);
					preview.render(&image);
					ok &= image.save(QDir(captures).filePath(QStringLiteral("%1-%2.png").arg(id).arg(scale)));
				}
			}
			shell.openPathFromCommandLine(saved);
			ok &= expect(until([&] { return shell.levelDocument().repeat.actions.isEmpty() && !repeat->isEnabled(); }),
			             "Reopen starts new repeat history");
			shell.hide();
		}
		if (scale == 200) {
			app.removeTranslator(&translator);
		}
	}
	QEventLoop drain;
	QTimer::singleShot(200, &drain, &QEventLoop::quit);
	drain.exec(QEventLoop::ExcludeUserInputEvents);
	if (ok) {
		std::cout << "Repeat GUI recording, keys, recipe, cancel, stale clear, undo, save, reopen and 100/200% RTL accessibility passed.\n";
	}
	return ok ? 0 : 1;
}
