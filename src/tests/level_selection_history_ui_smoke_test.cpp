#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_scene.h"
#include "tests/level_geometry_test_helpers.h"
#include "tests/level_object_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <QToolButton>
#include <QTreeWidget>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* label)
{
	if (!value) { std::cerr << "FAIL: " << label << '\n'; }
	return value;
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < 30000) {
		QEventLoop loop; QTimer::singleShot(20, &loop, &QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return ready();
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* text, const char*, int) const override {
		const auto source = QString::fromUtf8(text);
		return QStringLiteral("[%1%2]").arg(source, QString(source.size() / 3, QLatin1Char('~')));
	}
};
}
int main(int argc, char** argv)
{
	// Own offscreen Qt widgets and semantic calls only; no OS input/capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv); QTemporaryDir temp; QString error; bool ok = true;
	if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini")); StudioSettings settings;
	settings.setRestoreSession(false); settings.setReducedMotion(true); settings.setLevelViewLayoutPreference("four-views");
	LevelMapDocument map; QString group;
	if (!tests::createGeometryFixture(3, &map, &error) || !createLevelSceneNode(&map, LevelSceneNodeKind::Group, "Pair", {}, &group, &error)
		|| !assignLevelSceneObjects(&map, group, {"brush:0", "brush:1"}, &error)) { return 1; }
	const auto path = temp.filePath("history.map"); QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) { return 1; }
	file.write(serializeLevelMap(map).bytes); file.close();
	const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
	if (!captures.isEmpty()) { QDir().mkpath(captures); }
	const LevelMapSelectionRef a{LevelMapSelectionKind::QuakeBrush, 0}, c{LevelMapSelectionKind::QuakeBrush, 2};
	for (const auto& profile : {QStringLiteral("trenchbroom"), QStringLiteral("netradiant-custom"), QStringLiteral("vibestudio-default")}) {
		for (int scale : {100, 200}) {
			std::cout << "Selection history UI " << profile.toStdString() << " " << scale << std::endl;
			settings.setSelectedEditorProfileId(profile); settings.setTextScalePercent(scale);
			settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastLight); settings.sync();
			Expanded translator; if (scale == 200) { app.installTranslator(&translator); }
			app.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
			{
				ApplicationShell shell; shell.resize(1700, 1100); shell.show(); shell.openPathFromCommandLine(path);
				shell.findChild<QAction*>("shell.mode.levels")->trigger();
				auto* plan = shell.findChild<MapViewport*>("mapViewport"); auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
				auto* history = shell.findChild<QListWidget*>("levelMapHistory"); auto* objects = shell.findChild<LevelObjectList*>("levelMapObjects");
				if (!plan || !camera || !history || !objects || !until([&] { return shell.levelDocument().brushes.size() == 3 && camera->isEnabled() && camera->hasMesh(); })) { return 1; }
				const auto bytes = serializeLevelMap(shell.levelDocument()).bytes;
				const auto revision = shell.levelDocument().revision;
				const bool tb = profile == QStringLiteral("trenchbroom");
				const auto action = [&](const char* id) { return shell.findChild<QAction*>(QString::fromLatin1(id)); };
				const auto pick = [&](const QVector<LevelMapSelectionRef>& refs) { plan->selectSceneObjects(refs, LevelScenePick::Replace); };
				pick({c});
				ok &= expect(shell.levelDocument().undoStack.size() == (tb ? 1 : 0) && shell.levelDocument().revision == revision
					&& shell.levelDocument().editState != QStringLiteral("modified") && action("map.undo")->isEnabled() == tb,
					"profile selection history updates Undo without rebuilding map geometry");
				const auto depth = shell.levelDocument().undoStack.size(); pick({c});
				ok &= expect(shell.levelDocument().undoStack.size() == depth, "repeated pick does not add history");
				if (tb) {
					action("map.undo")->trigger();
					ok &= expect(shell.levelDocument().selection.isEmpty() && action("map.redo")->isEnabled(), "UI selection undo enables redo");
					action("map.redo")->trigger();
					ok &= expect(shell.levelDocument().selection == QVector<LevelMapSelectionRef>{c}, "UI selection redo");
				}
				pick({a}); action("map.openGroup")->trigger();
				ok &= expect(shell.levelDocument().openSceneGroups == QStringList{group} && shell.levelDocument().selection.isEmpty(), "open group via semantic action");
				if (tb) {
					action("map.undo")->trigger();
					ok &= expect(shell.levelDocument().openSceneGroups.isEmpty() && shell.levelDocument().selection.size() == 2, "undo group opening restores closed unit");
					action("map.redo")->trigger();
				}
				pick({a}); action("map.selectAll")->trigger();
				ok &= expect(shell.levelDocument().selection.size() == 2, "Select All uses group context");
				action("map.selectNone")->trigger();
				if (tb) { action("map.undo")->trigger(); ok &= expect(shell.levelDocument().selection.size() == 2, "Select None is undoable"); }
				action("map.closeAllGroups")->trigger();
				tests::setObjectCurrentRow(objects, objects->rowForSelector("brush:2"));
				ok &= expect(shell.levelDocument().selection == QVector<LevelMapSelectionRef>{c}, "Objects list selects independent brush");
				if (tb) { action("map.undo")->trigger(); ok &= expect(shell.levelDocument().selection.size() == 2, "Objects list selection is undoable"); }
				pick({}); camera->triangleClicked(0, 0);
				ok &= expect(shell.levelDocument().selection.size() == 2, "camera pick selects closed unit");
				if (tb) {
					action("map.undo")->trigger(); ok &= expect(shell.levelDocument().selection.isEmpty(), "camera selection is undoable");
					action("map.redo")->trigger();
				}
				camera->trianglePicked(0, 0, static_cast<int>(ModelViewportPick::Toggle));
				ok &= expect(shell.levelDocument().selection.isEmpty(), "camera toggle clears closed unit");
				if (tb) { action("map.undo")->trigger(); ok &= expect(shell.levelDocument().selection.size() == 2, "camera toggle is undoable"); }
				pick({c});
				auto* tree = shell.findChild<QTreeWidget*>("levelSceneTree");
				for (QTreeWidgetItemIterator it(tree); *it; ++it) { if ((*it)->data(0, Qt::UserRole).toString() == group) { tree->setCurrentItem(*it); break; } }
				shell.findChild<QPushButton*>("levelSceneSelect")->click();
				ok &= expect(shell.levelDocument().selection.size() == 2, "scene panel selects members");
				if (tb) { action("map.undo")->trigger(); ok &= expect(shell.levelDocument().selection == QVector<LevelMapSelectionRef>{c}, "scene panel selection uses same history"); }
				ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == bytes && shell.levelDocument().editState != QStringLiteral("modified"),
					"all selection and context UI routes preserve saved content");
				if (!tb) { ok &= expect(shell.levelDocument().undoStack.isEmpty(), "NRC and default selections remain outside undo"); }
				if (tb) {
					ok &= expect(history->count() == shell.levelDocument().undoStack.size() + shell.levelDocument().redoStack.size() + 1,
						"history panel includes applied and undone selection steps");
					for (int row = 0; row < history->count(); ++row) {
						ok &= expect(history->item(row)->data(Qt::AccessibleTextRole).toString().contains("saved"), "equivalent saved history rows have accessible state");
					}
					auto* accessible = QAccessible::queryAccessibleInterface(history);
					ok &= expect(accessible && !accessible->text(QAccessible::Name).isEmpty() && history->focusPolicy() != Qt::NoFocus,
						"history supports accessible name and keyboard focus");
					// Owned widget capture and activation exercise scaling, expansion, RTL,
					// and the keyboard alternative without synthesising keyboard input.
					QWidget* original = history->parentWidget(); history->setParent(nullptr); history->resize(900, 700); history->show(); history->setFocus();
					QImage capture(history->size(), QImage::Format_ARGB32_Premultiplied); history->render(&capture);
					if (!captures.isEmpty()) { capture.save(QDir(captures).filePath(QStringLiteral("history-%1.png").arg(scale))); }
					history->itemActivated(history->item(0));
					ok &= expect(shell.levelDocument().undoStack.isEmpty() && shell.levelDocument().selection.isEmpty()
						&& shell.levelDocument().openSceneGroups.isEmpty(), "history activation navigates full selection timeline");
					history->setParent(original); history->hide();
					if (scale == 100) {
						const auto chooseProfile = [&](const QString& id) {
							for (auto* choice : shell.findChild<QToolButton*>("levelControlsButton")->menu()->actions()) {
								if (choice->data().toString() == id) { choice->trigger(); return true; }
							}
							return false;
						};
						ok &= expect(chooseProfile(QStringLiteral("netradiant-custom")), "switch profile in current session");
						pick({c}); shell.findChild<QPushButton*>("levelSceneSelect")->click();
						ok &= expect(shell.levelDocument().undoStack.isEmpty(), "profile switch updates scene selection history policy");
						ok &= expect(chooseProfile(QStringLiteral("trenchbroom")), "restore TB profile");
						pick({c}); shell.findChild<QPushButton*>("levelSceneSelect")->click();
						ok &= expect(shell.levelDocument().undoStack.size() == 2 && shell.levelDocument().editState != QStringLiteral("modified"),
							"TB resumes shared selection history after profile switch");
					}
				}
				shell.close();
			}
			if (scale == 200) { app.removeTranslator(&translator); }
		}
	}
	return ok ? 0 : 1;
}
