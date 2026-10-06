#include "tests/level_object_test_helpers.h"
using vibestudio::tests::objectIndex;
using vibestudio::tests::setObjectCurrentRow;
using vibestudio::tests::setObjectSelected;
#include "app/application_shell.h"
#include "app/level_scene_panel.h"
#include "app/map_viewport.h"
#include "app/studio_theme.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include <QCheckBox>
#include "core/map_preview_mesh.h"
#include "tests/level_geometry_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTranslator>
#include <QTreeWidget>
#include <algorithm>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool pass, const char* label, const QString& error = {}) {
	if (!pass) {
		std::cerr << label << ": " << error.toStdString() << '\n';
	}
	return pass;
}
class Expansion final : public QTranslator {
  public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override {
		if (QByteArray(context) != "vibestudio::LevelScenePanel") {
			return {};
		}
		const auto label = QString::fromUtf8(source);
		return QStringLiteral("[%1 %2]").arg(label, QString(label.size() / 3, QLatin1Char('~')));
	}
};
QTreeWidgetItem* item(QTreeWidget* tree, const QString& id) {
	for (QTreeWidgetItemIterator at(tree); *at; ++at) {
		if ((*at)->data(0, Qt::UserRole).toString() == id) {
			return *at;
		}
	}
	return nullptr;
}
} // namespace
int main(int argc, char** argv) {
	// No input injection or OS capture: semantic Qt controls and render targets.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
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
	settings.sync();
	bool ok = true;
	QString error;
	LevelMapDocument fixture;
	ok &= expect(tests::createGeometryFixture(4, &fixture, &error), "fixture", error);
	for (int scale : {100, 200}) {
		Expansion expanded;
		if (scale == 200) {
			app.installTranslator(&expanded);
			app.setLayoutDirection(Qt::RightToLeft);
		}
		applyStudioTheme(app,
						 studioThemeTokens(scale == 200 ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scale));
		auto document = fixture;
		LevelScenePanel panel(&document);
		panel.resize(scale == 200 ? 1000 : 580, scale == 200 ? 1100 : 730);
		panel.show();
		auto* tree = panel.findChild<QTreeWidget*>(QStringLiteral("levelSceneTree"));
		auto* name = panel.findChild<QLineEdit*>(QStringLiteral("levelSceneName"));
		const auto click = [&](const char* id) {
			panel.findChild<QPushButton*>(QString::fromLatin1(id))->click();
			QApplication::processEvents();
		};
		name->setText(QStringLiteral("Architecture"));
		click("levelSceneNewLayer");
		if (!expect(document.scene.nodes.size() == 1, "panel creates layer")) {
			return 1;
		}
		const auto layer = document.scene.nodes.first().id;
		tree->setCurrentItem(item(tree, layer));
		panel.findChild<QComboBox*>(QStringLiteral("levelSceneParent"))->setCurrentIndex(1);
		name->setText(QStringLiteral("Room"));
		click("levelSceneNewGroup");
		const auto group = document.scene.nodes.last().id;
		ok &= expect(document.scene.nodes.size() == 2 && document.scene.nodes.last().parentId == layer, "panel nests group");
		tree->setCurrentItem(item(tree, group));
		setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 0}});
		panel.refreshSelection();
		click("levelSceneAssign");
		ok &= expect(levelSceneMembership(document.scene, QStringLiteral("brush:0")) == group, "assign selection through shared service");
		MapViewport top, side;
		top.setDocument(document);
		side.synchronizeSceneFrom(top);
		const auto before = serializeLevelMap(document).bytes;
		item(tree, layer)->setCheckState(0, Qt::Unchecked);
		QApplication::processEvents();
		top.updateDocument(document);
		side.synchronizeSceneFrom(top);
		ok &= expect(top.isHidden(LevelMapSelectionKind::QuakeBrush, 0) && side.isHidden(LevelMapSelectionKind::QuakeBrush, 0) &&
						 top.displayDocument().brushes.size() == 3 && side.displayDocument().brushes.size() == 3 &&
						 document.selection.isEmpty(),
					 "hidden groups disappear from linked panes and selection");
		const auto preview = buildLevelMapPreviewMesh(document);
		ok &= expect(std::none_of(preview.owners.cbegin(), preview.owners.cend(),
								  [](const auto& ref) { return ref.kind == LevelMapSelectionKind::QuakeBrush && ref.objectId == 0; }),
					 "camera uses same visibility");
		ok &= expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == before && document.selection.size() == 1,
					 "visibility undo restores content and selection", error);
		panel.refresh();
		top.updateDocument(document);
		side.synchronizeSceneFrom(top);
		ok &=
			expect(top.displayDocument().brushes.size() == 4 && side.displayDocument().brushes.size() == 4, "undo restores linked scenes");
		clearLevelMapSelection(&document);
		tree->setCurrentItem(item(tree, group));
		click("levelSceneSelect");
		ok &= expect(document.selection.size() == 1 && document.selection.first().objectId == 0, "group selection");
		tree->setCurrentItem(item(tree, layer));
		auto* lock = panel.findChild<QCheckBox*>(QStringLiteral("levelSceneLock"));
		lock->click();
		QApplication::processEvents();
		ok &= expect(levelSceneNode(document.scene, layer)->locked && lock->isChecked() &&
			!moveLevelMapObject(&document, "brush", 0, 8, 0, 0, &error), "native lock control protects shared edits", error);
		tree->setCurrentItem(item(tree, group));
		ok &= expect(lock->isChecked() && !lock->isEnabled(), "inherited lock state is explicit");
		const auto* lockAccessible = QAccessible::queryAccessibleInterface(lock);
		ok &= expect(lockAccessible && lockAccessible->role() == QAccessible::CheckBox && lockAccessible->state().checked &&
			!lockAccessible->text(QAccessible::Name).isEmpty(), "native accessible lock state");
		const auto* accessible = QAccessible::queryAccessibleInterface(tree);
		ok &= expect(accessible && accessible->role() == QAccessible::Tree && !accessible->text(QAccessible::Name).isEmpty() &&
						 tree->focusPolicy() != Qt::NoFocus && name->focusPolicy() != Qt::NoFocus,
					 "accessible native tree and keyboard focus");
		QApplication::processEvents();
		for (auto* button : panel.findChildren<QPushButton*>()) {
			if (button->isVisible()) {
				ok &= expect(button->width() >= button->fontMetrics().horizontalAdvance(button->text()) + 12 &&
								 button->height() >= button->fontMetrics().height(),
							 "scaled translated button text fits", button->text());
			}
		}
		if (scale == 200) {
			ok &= expect(tree->headerItem()->text(0).startsWith('['), "real namespaced translation expansion");
		}
		const auto captureRoot = qEnvironmentVariable("VIBESTUDIO_SCENE_CAPTURES");
		tree->expandAll();
		QApplication::processEvents();
		if (!captureRoot.isEmpty()) {
			QDir().mkpath(captureRoot);
			QImage capture(panel.size(), QImage::Format_ARGB32_Premultiplied);
			capture.fill(Qt::transparent);
			panel.render(&capture);
			ok &= expect(capture.save(QDir(captureRoot).filePath(QStringLiteral("scene-%1.png").arg(scale))), "widget render capture");
		}
		// A stale displayed revision must not apply the form to a changed map.
		createLevelSceneNode(&document, LevelSceneNodeKind::Layer, QStringLiteral("External operation"), {});
		const auto count = document.scene.nodes.size();
		name->setText(QStringLiteral("Stale"));
		click("levelSceneNewLayer");
		ok &=
			expect(document.scene.nodes.size() == count && !panel.findChild<QLabel*>(QStringLiteral("levelSceneStatus"))->text().isEmpty(),
				   "stale scene controls refuse edit");
		if (scale == 200) {
			app.removeTranslator(&expanded);
			app.setLayoutDirection(Qt::LeftToRight);
		}
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	const auto path = temp.filePath(QStringLiteral("shell.map"));
	QFile file(path);
	ok &= file.open(QIODevice::WriteOnly);
	file.write(serializeLevelMap(fixture).bytes);
	file.close();
	ApplicationShell shell;
	shell.resize(1600, 1000);
	shell.show();
	shell.openPathFromCommandLine(path);
	shell.findChild<QAction*>(QStringLiteral("shell.mode.levels"))->trigger();
	auto* panel = shell.findChild<LevelScenePanel*>();
	if (!expect(panel, "Scene panel integrated into actual shell")) {
		return 1;
	}
	auto* tree = panel->findChild<QTreeWidget*>(QStringLiteral("levelSceneTree"));
	panel->findChild<QLineEdit*>(QStringLiteral("levelSceneName"))->setText(QStringLiteral("Shell layer"));
	panel->findChild<QPushButton*>(QStringLiteral("levelSceneNewLayer"))->click();
	const auto layer = shell.levelDocument().scene.nodes.first().id;
	tree->setCurrentItem(item(tree, layer));
	auto* objects = shell.findChild<LevelObjectList*>(QStringLiteral("levelMapObjects"));
	for (int i = 0; i < objects->model()->rowCount(); ++i) {
		if (objectIndex(objects, i).data(Qt::UserRole).toString() == QStringLiteral("brush:0")) {
			setObjectCurrentRow(objects, i);
			break;
		}
	}
	panel->findChild<QPushButton*>(QStringLiteral("levelSceneAssign"))->click();
	ok &=
		expect(levelSceneMembership(shell.levelDocument().scene, QStringLiteral("brush:0")) == layer, "shell object selection assignment");
	panel->findChild<QCheckBox*>(QStringLiteral("levelSceneLock"))->click();
	ok &= expect(levelSceneNode(shell.levelDocument().scene, layer)->locked, "shell lock transaction");
	item(tree, layer)->setCheckState(0, Qt::Unchecked);
	QApplication::processEvents();
	for (auto* view : shell.findChildren<MapViewport*>()) {
		ok &= expect(view->isHidden(LevelMapSelectionKind::QuakeBrush, 0), "shell synchronizes scene visibility to every plan pane");
	}
	ok &= expect(shell.saveLevelDocument(temp.filePath(QStringLiteral("saved.map")), false, &error), "shell save scene", error);
	LevelMapDocument saved;
	ok &= expect(loadLevelMap({temp.filePath(QStringLiteral("saved.map")), {}, {}}, &saved, &error) &&
					 saved.scene == shell.levelDocument().scene && saved.brushes.size() == 4,
				 "shell save carries scene and complete geometry", error);
	// Doom visibility keeps indexed records intact while hiding both picking
	// targets and the camera surfaces of a hidden sector.
	LevelMapCreateRequest doomRequest;
	doomRequest.game = QStringLiteral("doom");
	LevelMapDocument doom;
	createLevelMap(doomRequest, &doom);
	QString room;
	createLevelSceneNode(&doom, LevelSceneNodeKind::Layer, QStringLiteral("Room"), {}, &room);
	assignLevelSceneObjects(&doom, room, {QStringLiteral("sector:0")});
	setLevelSceneVisible(&doom, room, false);
	MapViewport doomView;
	doomView.setDocument(doom);
	ok &= expect(doomView.displayDocument().doomVertices.size() == doom.doomVertices.size() &&
					 doomView.displayDocument().doomLinedefs.size() == doom.doomLinedefs.size() &&
					 doomView.isHidden(LevelMapSelectionKind::DoomLinedef, 0) && buildLevelMapPreviewMesh(doom).triangles == 0,
				 "Doom filtering preserves indices and hides surfaces");
	std::cout << (ok ? "Level scene UI smoke passed\n" : "Level scene UI smoke failed\n");
	return ok ? 0 : 1;
}
