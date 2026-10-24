#include "app/application_shell.h"
#include "app/level_scene_panel.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_linked_groups.h"
#include "core/level_protected_properties.h"
#include "core/level_scene.h"
#include "tests/level_prefab_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLayout>
#include <QProgressBar>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <QTreeWidget>
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
int nativeId(const LevelMapDocument& document, const QString& group, const QString& identity)
{
	const auto* node = levelSceneNode(document.scene, group);
	if (node) {
		for (auto it = node->linkObjects.cbegin(); it != node->linkObjects.cend(); ++it) {
			if (it.value() == identity) {
				return it.key().section(':', 1).toInt();
			}
		}
	}
	return -1;
}
} // namespace
int main(int argc, char** argv)
{
	// Direct application methods and owned render targets only. No mouse/key
	// injection, native screen capture or game launch is used by this fixture.
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
	QString error, group, nested, copy;
	if (!tests::loadPrefabFixture(&map, &error) || !createLevelSceneNode(&map, LevelSceneNodeKind::Group, "Assembly", {}, &group, &error) ||
	    !createLevelSceneNode(&map, LevelSceneNodeKind::Group, "Relay", group, &nested, &error) ||
	    !assignLevelSceneObjects(&map, group, {"entity:1", "patch:0"}, &error) || !assignLevelSceneObjects(&map, nested, {"entity:2"}, &error) ||
	    !createLinkedLevelGroup(&map, group, {256, 0, 0, true}, &copy, &error)) {
		return 1;
	}
	const auto firstBrush = levelSceneNode(map.scene, group)->linkObjects.value("brush:0");
	const auto otherBrush = levelSceneNode(map.scene, group)->linkObjects.value("brush:1");
	const auto relay = levelSceneNode(map.scene, group)->linkObjects.value("entity:2");
	const auto path = temp.filePath("assembly.map");
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
			if (!plan || !camera || !until([&] { return shell.levelDocument().scene.nodes.size() == 4 && camera->isEnabled(); })) {
				return 1;
			}
			auto* action = shell.findChild<QAction*>("map.extractLinkedGroups");
			ok &= expect(action && !action->isEnabled(), "extraction is discoverable and disabled outside an open group");
			const auto pick = [&](const QString& identity) {
				plan->selectSceneObjects({{LevelMapSelectionKind::QuakeBrush, nativeId(shell.levelDocument(), group, identity)}},
				                         LevelScenePick::Replace);
			};
			pick(firstBrush);
			shell.findChild<QAction*>("map.openGroup")->trigger();
			pick(firstBrush);
			ok &= expect(action->isEnabled() && shell.levelDocument().openSceneGroups == QStringList{group},
			             "partial selection enables extraction inside the group");
			plan->selectSceneObjects({{LevelMapSelectionKind::Entity, nativeId(shell.levelDocument(), group, relay)}}, LevelScenePick::Replace);
			ok &= expect(action->isEnabled() && shell.levelDocument().selection.size() == 1,
			             "a closed nested group is a unit extraction can take");
			pick(firstBrush);
			const auto before = serializeLevelMap(shell.levelDocument()).bytes;
			const auto selection = shell.levelDocument().selection;
			for (int mode : {0, 1, 2}) {
				pick(firstBrush);
				const auto selectionDepth = shell.levelDocument().undoStack.size();
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
					ok &=
					    expect(bar && cancel && QAccessible::queryAccessibleInterface(bar)->role() == QAccessible::ProgressBar &&
					               !cancel->accessibleDescription().isEmpty() && dialog->accessibleName().contains("Extract Linked Groups"),
					           "extraction uses accessible asynchronous preparation");
					dialog->ensurePolished();
					dialog->adjustSize();
					if (!captures.isEmpty()) {
						QImage image(dialog->size(), QImage::Format_ARGB32_Premultiplied);
						dialog->render(&image);
						ok &= image.save(QDir(captures).filePath(QStringLiteral("extraction-progress-%1.png").arg(scale)));
					}
					if (mode == 0) {
						dialog->reject();
					} else if (mode == 1) {
						pick(otherBrush);
					}
				});
				drive.start(0);
				action->trigger();
				drive.stop();
				ok &= expect(visited, "the worker enters the GUI event loop");
				if (mode < 2) {
					ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == before
					                 && shell.levelDocument().undoStack.size() == selectionDepth + (mode == 1 ? 1 : 0)
					                 && shell.levelDocument().editState != QStringLiteral("modified"),
					             mode == 0 ? "cancel publishes nothing" : "stale selection rejects the prepared split",
					             shell.statusBar()->currentMessage());
				} else {
					ok &= expect(shell.levelDocument().scene.nodes.size() == 6 && shell.levelDocument().openSceneGroups.isEmpty() &&
					                 shell.levelDocument().entities.size() == map.entities.size() + 2,
					             "GUI publishes both partial-owner splits", shell.statusBar()->currentMessage());
				}
			}
			const auto after = serializeLevelMap(shell.levelDocument()).bytes;
			shell.findChild<QAction*>("map.undo")->trigger();
			ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == before && shell.levelDocument().selection == selection &&
			                 shell.levelDocument().openSceneGroups == QStringList{group} && action->isEnabled(),
			             "GUI undo restores the exact selection and open context");
			shell.findChild<QAction*>("map.redo")->trigger();
			ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == after && !action->isEnabled(),
			             "GUI redo restores the new closed groups");
			auto* scene = shell.findChild<LevelScenePanel*>();
			if (scene && !captures.isEmpty()) {
				// Render the same panel/data at its inspection size. Its live
				// sidebar parent would otherwise reapply its collapsed geometry.
				auto captured = shell.levelDocument();
				LevelScenePanel preview(&captured, &shell);
				preview.setFont(scene->font());
				preview.setAutoFillBackground(true);
				preview.resize(scale == 200 ? 1050 : 650, 950);
				preview.ensurePolished();
				preview.show();
				preview.findChild<QTreeWidget*>()->expandAll();
				preview.layout()->activate();
				QImage image(preview.size(), QImage::Format_ARGB32_Premultiplied);
				preview.render(&image);
				ok &= image.save(QDir(captures).filePath(QStringLiteral("extracted-scene-%1.png").arg(scale)));
			}
			ok &= expect(shell.saveLevelDocument(temp.filePath(QStringLiteral("saved-%1.map").arg(scale)), false, &error),
			             "GUI save uses the normal document pipeline", error);
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
		std::cout << "Linked extraction GUI eligibility, cancellation, stale context, undo, save, scaling and RTL passed.\n";
	}
	return ok ? 0 : 1;
}
