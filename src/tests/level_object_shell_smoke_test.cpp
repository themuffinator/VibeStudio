#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/ui_primitives.h"
#include "tests/level_object_test_helpers.h"
#include "tests/level_geometry_test_helpers.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool condition, const char* message, const QString& error = {})
{
	if (!condition) { std::cerr << message << ": " << error.toStdString() << '\n'; ok = false; }
	return condition;
}
void drain(int ms = 30)
{
	QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents);
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < 60000) { drain(20); }
	return ready();
}
bool write(const QString& path, const LevelMapDocument& document)
{
	QFile file(path); const auto bytes = serializeLevelMap(document).bytes;
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QTreeWidgetItem* field(QTreeWidget* inspector, const QString& path)
{
	if (!inspector) { return nullptr; }
	for (QTreeWidgetItemIterator item(inspector); *item; ++item) {
		if ((*item)->data(0, Qt::UserRole + 11).toString() == path) { return *item; }
	}
	return nullptr;
}
QString statisticsText(const DetailDrawer* drawer)
{
	for (const auto& section : drawer->sections()) {
		if (section.id == QStringLiteral("statistics")) { return section.content; }
	}
	return {};
}
QString tileFacts(const QListWidget* tiles, const QString& name)
{
	for (int row = 0; row < tiles->count(); ++row) {
		if (tiles->item(row)->data(Qt::UserRole).toString() == name) { return tiles->item(row)->data(Qt::AccessibleTextRole).toString(); }
	}
	return {};
}
}

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv); QTemporaryDir temp; QString error;
	if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	StudioSettings settings; settings.setRestoreSession(false); settings.setReducedMotion(true);
	settings.setLevelViewLayoutPreference("four-views"); settings.sync();
	LevelMapDocument source;
	if (!expect(tests::createGeometryFixture(3, &source, &error), "source fixture", error)) { return 1; }
	for (int id : {1, 2}) {
		LevelMapEntity entity; entity.id = id; entity.className = "light";
		entity.properties = {{"classname", "light"}, {"origin", QStringLiteral("%1 24 64").arg(id * 24)}, {"light", "300"}};
		entity.origin = {id * 24.0, 24, 64, true}; source.entities.append(entity);
	}
	LevelSceneNode layer; layer.id = "3b412c0a-7067-459e-86d4-5b4c9d0283a1"; layer.name = "Hidden layer";
	layer.kind = LevelSceneNodeKind::Layer; layer.visible = false; layer.objects = {"brush:0"}; source.scene.nodes.append(layer);
	const auto quakePath = temp.filePath("objects.map");
	if (!expect(write(quakePath, source), "write source")) { return 1; }
	ApplicationShell shell; shell.resize(1500, 1000); shell.show(); shell.openPathFromCommandLine(quakePath);
	auto* list = shell.findChild<LevelObjectList*>("levelMapObjects");
	auto* filter = shell.findChild<QLineEdit*>("levelObjectFilter");
	auto* plan = shell.findChild<MapViewport*>("mapViewport");
	auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
	auto* inspector = shell.findChild<QTreeWidget*>("entityInspector");
	auto* materials = shell.findChild<QComboBox*>("levelPaintMaterial");
	auto* textures = shell.findChild<QListWidget*>("levelMapTextures");
	DetailDrawer* details = nullptr;
	for (auto* frame : shell.findChildren<QFrame*>()) {
		if (frame->accessibleName() == QStringLiteral("Level map detail drawer")) { details = static_cast<DetailDrawer*>(frame); break; }
	}
	if (!expect(list && filter && plan && camera && inspector && details && materials && textures, "shell components")) { return 1; }
	shell.findChild<QAction*>("shell.mode.levels")->trigger();
	expect(until([&] { return shell.levelDocument().sourcePath == quakePath && camera->isEnabled() && list->objectModel()->objectCount() == 6; }), "loaded object model");
	const auto original = serializeLevelMap(shell.levelDocument()).bytes;
	tests::setObjectCurrentRow(list, list->rowForSelector("brush:2"));
	expect(shell.levelDocument().selectedObjectId == 2 && plan->selectedObjectId() == 2, "list selection reaches document and plan");
	expect(until([&] { return camera->highlightedTriangleCount() > 0; }), "list selection reaches camera");
	details->showSection("textures");
	LevelBrushPrimitiveRequest primitive; primitive.texture = "studio/new"; primitive.mins = {640, 640, 0, true}; primitive.maxs = {704, 704, 64, true};
	expect(shell.applyLevelBrushPrimitive(primitive, &error) && list->rowForSelector("brush:3") >= 0 &&
		list->currentIndex().data(Qt::UserRole).toString() == "brush:3", "creation refresh selects new model row", error);
	const auto* newTexture = field(inspector, "face:3:0:texture");
	expect(newTexture && newTexture->data(1, Qt::UserRole + 9).toStringList() == QStringList{"studio/cache", "studio/new"},
		"creation refresh gives the selected brush current map material suggestions");
	expect(statisticsText(details).contains("Brushes: 4 ") && statisticsText(details).contains("24 / 2 unique"),
		"creation updates drawer geometry and material statistics");
	expect(shell.levelDocument().brushes.constData() == list->objectModel()->snapshot().document.brushes.constData(),
		"read-only inspector/camera refresh retains the brush array shared with the Objects model");
	expect(shell.levelDocument().entities.constData() == list->objectModel()->snapshot().document.entities.constData(),
		"read-only inspector/camera refresh retains the entity array shared with the Objects model");
	expect(details->currentSectionId() == "textures" && details->currentSectionText() == "studio/cache\nstudio/new" &&
		materials->findText("studio/new") >= 0 && tileFacts(textures, "studio/new").contains("used 6 time") &&
		tileFacts(textures, "studio/cache").contains("used 18 time"), "creation keeps the inspected section and updates picker/tile counts from the same map");
	shell.findChild<QAction*>("map.undo")->trigger();
	expect(serializeLevelMap(shell.levelDocument()).bytes == original && list->rowForSelector("brush:3") == -1 &&
		list->currentIndex().data(Qt::UserRole).toString() == "brush:2", "exact undo restores selection identity and removes deleted row");
	const auto* restoredTexture = field(inspector, "face:2:0:texture");
	expect(restoredTexture && restoredTexture->data(1, Qt::UserRole + 9).toStringList() == QStringList{"studio/cache"},
		"undo removes the retired material from inspector suggestions");
	expect(statisticsText(details).contains("Brushes: 3 ") && statisticsText(details).contains("18 / 1 unique"),
		"undo restores drawer geometry and material statistics");
	expect(details->currentSectionId() == "textures" && details->currentSectionText() == "studio/cache" &&
		tileFacts(textures, "studio/new").contains("not in the map now") && tileFacts(textures, "studio/cache").contains("used 18 time"),
		"undo refreshes actual usage while preserving recent material choices and the inspected section");
	plan->setSelectionSet({{LevelMapSelectionKind::QuakeBrush, 1}});
	plan->selectionSetChanged(plan->selectionSet());
	expect(shell.levelDocument().selectedObjectId == 1 && field(inspector, "face:1:0:texture") &&
		list->currentIndex().data(Qt::UserRole).toString() == "brush:1", "plan selection synchronizes list and inspector");
	expect(details->currentSectionId() == "textures" && details->currentSectionText() == "studio/cache",
		"selection-only refresh keeps the user's detail context with live material names");

	tests::setObjectCurrentRow(list, list->rowForSelector("brush:1"));
	shell.findChild<QAction*>("map.hideSelection")->trigger();
	expect(plan->isHidden(LevelMapSelectionKind::QuakeBrush, 0) && plan->isHidden(LevelMapSelectionKind::QuakeBrush, 1), "scene and temporary visibility preserved");
	filter->setText("kind=brush");
	expect(list->isFiltering(), "query exposes pending state");
	filter->returnPressed();
	expect(until([&] { return !list->isFiltering() && shell.levelDocument().selection.size() == 1; }), "Enter queues current query selection");
	expect(shell.levelDocument().selectedObjectId == 2 && list->matchingCount() == 3 &&
		shell.statusBar()->currentMessage().contains("2 hidden"), "matching selection excludes both kinds of hidden object");
	filter->setText("class=light");
	expect(until([&] { return !list->isFiltering(); }) && shell.levelDocument().selectedObjectId == 2 &&
		list->selectedReferences().size() == 1, "filtering preserves a selected object outside results");
	filter->returnPressed();
	expect(shell.levelDocument().selection.size() == 2 && shell.levelDocument().selectionKind == LevelMapSelectionKind::Entity &&
		plan->selectionSet().size() == 2, "multi-result handoff retains shared selection");
	const auto entities = shell.levelDocument().selection;
	filter->setText("kind=brush"); filter->returnPressed();
	expect(shell.statusBar()->currentMessage() == "Filtering objects…", "queued selection exposes its pending status");
	filter->clear();
	expect(shell.statusBar()->currentMessage().isEmpty(), "cancelling queued selection clears its pending status immediately");
	drain(250);
	expect(shell.levelDocument().selection == entities && !list->isFiltering(), "clearing cancels queued Enter as well as query work");
	filter->setText("kind=brush"); filter->returnPressed();
	shell.statusBar()->showMessage("another operation"); filter->clear();
	expect(shell.statusBar()->currentMessage() == "another operation", "cancelling query preserves another operation's status");
	filter->setText("claass=light");
	expect(until([&] { return !list->isFiltering(); }) && shell.statusBar()->currentMessage().contains("Nothing here has claass"), "unknown keys remain actionable");
	filter->clear();
	tests::setObjectCurrentRow(list, list->rowForSelector("brush:1"));
	expect(!plan->isHidden(LevelMapSelectionKind::QuakeBrush, 1) &&
		!list->currentIndex().data(Qt::AccessibleTextRole).toString().contains("hidden"), "selecting a temporarily hidden row reveals it and refreshes accessibility");
	expect(serializeLevelMap(shell.levelDocument()).bytes == original, "queries and visibility do not alter source bytes");
	auto* shift = field(inspector, "face:1:0:shiftx");
	if (expect(shift != nullptr, "selected face field before save")) {
		shift->parent()->setExpanded(true); inspector->setCurrentItem(shift, 1);
	}
	expect(shell.saveLevelDocument(quakePath, true, &error), "save still shares the document", error);
	shift = field(inspector, "face:1:0:shiftx");
	expect(shift && inspector->currentItem() == shift && inspector->currentColumn() == 1 && shift->parent()->isExpanded(),
		"save refresh restores the inspected field and expanded face");

	LevelMapCreateRequest request; request.game = "doom"; LevelMapDocument doom;
	expect(createLevelMap(request, &doom, &error), "Doom fixture", error);
	const auto doomPath = temp.filePath("objects.wad"); expect(write(doomPath, doom), "write Doom fixture");
	filter->setText("class=light"); filter->returnPressed(); shell.openPathFromCommandLine(doomPath);
	expect(until([&] { return shell.levelDocument().sourcePath == doomPath && !list->isFiltering(); }), "replace source while filter pending");
	expect(list->matchingCount() == 0 && shell.levelDocument().selection.isEmpty() && list->selectedReferences().isEmpty(), "old query/selection cannot attach to another map");
	filter->setText("kind=sector"); filter->returnPressed();
	expect(until([&] { return !list->isFiltering() && shell.levelDocument().selectionKind == LevelMapSelectionKind::DoomSector; }), "Doom query selects native sectors");
	expect(list->rowForSelector("entity:0") == -1 && list->rowForSelector("thing:0") >= 0, "Doom mirrored entities are listed once");
	shell.close(); drain(100);
	return ok ? 0 : 1;
}
