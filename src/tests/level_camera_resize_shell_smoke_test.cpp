#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "tests/level_geometry_test_helpers.h"
#include "tests/level_udmf_test_helpers.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSplitter>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message, const QString& detail = {})
{
	if (!value) { std::cerr << "FAIL: " << message << ": " << detail.toStdString() << '\n'; }
	return value;
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer elapsed; elapsed.start();
	while (!ready() && elapsed.elapsed() < 30000) {
		QEventLoop loop; QTimer::singleShot(20,&loop,&QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return ready();
}
bool writeMap(const QString& path, const LevelMapDocument& document)
{
	QFile file(path); const auto bytes = serializeLevelMap(document).bytes;
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
bool noHandles(ModelViewport& view)
{
	const auto handles = view.selectionResizeHandles();
	return std::none_of(handles.cbegin(),handles.cend(),[](const auto& point) { return std::isfinite(point.x()); });
}
}
int main(int argc, char** argv)
{
	// Semantic commands and widget render targets only; no native input, OS
	// capture, user settings, installed game files or external assets.
	qputenv("QT_QPA_PLATFORM","offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR",QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc,argv); QTemporaryDir temp;
	if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	StudioSettings settings; settings.setReducedMotion(true); settings.setRestoreSession(false);
	settings.setSelectedEditorProfileId(QStringLiteral("trenchbroom")); settings.setLevelViewLayoutPreference(QStringLiteral("four-views"));
	settings.sync(); applyStudioTheme(app,studioThemeTokens(StudioTheme::Dark,UiDensity::Standard,100));
	LevelMapDocument fixture; QString error;
	if (!tests::createGeometryFixture(2,&fixture,&error) || !writeMap(temp.filePath("fixture.map"),fixture)) { return 1; }
	ApplicationShell shell; shell.resize(1700,1100); shell.show(); shell.openPathFromCommandLine(temp.filePath("fixture.map"));
	shell.findChild<QAction*>("shell.mode.levels")->trigger();
	auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
	auto* top = shell.findChild<MapViewport*>("mapViewport");
	auto* front = shell.findChild<MapViewport*>("mapViewport1");
	auto* side = shell.findChild<MapViewport*>("mapViewport2");
	if (!camera || !top || !front || !side) { return 1; }
	bool ok = expect(until([&] { return shell.levelDocument().brushes.size() == 2 && camera->hasMesh() && camera->isEnabled(); }),"map and camera load");
	auto choose = [&](int id) {
		const QVector<LevelMapSelectionRef> selected{{LevelMapSelectionKind::QuakeBrush,id}};
		top->setSelectionSet(selected); top->selectionSetChanged(selected);
	};
	shell.findChild<QAction*>("map.grid16")->trigger(); shell.findChild<QCheckBox*>("levelMapSnap")->setChecked(true); choose(0);
	camera->setCameraView({-96,-120,88},50,-30);
	// Drain initial workspace layout and queued preview refreshes before a
	// gesture starts; either change legitimately invalidates its camera plane.
	QEventLoop initialLayout; QTimer::singleShot(100,&initialLayout,&QEventLoop::quit); initialLayout.exec(QEventLoop::ExcludeUserInputEvents);
	QImage image(camera->size(),QImage::Format_ARGB32_Premultiplied);
	ok &= until([&] { camera->render(&image); return camera->isEnabled() && !camera->isRendering(); });
	const ResizeBox from{{-16,-16,-16},{16,16,16}},to{{-16,-16,-16},{48,16,16}};
	ok &= expect(camera->selectionResizeBox() == from && !noHandles(*camera),"shell installs source selection bounds in camera");
	ok &= expect(shell.findChild<QAction*>("map.resizeSelection")->isEnabled(),"numeric resize remains available for keyboard editing");
	const auto original = serializeLevelMap(shell.levelDocument()).bytes;
	auto expected = shell.levelDocument();
	ok &= resizeLevelMapSelection(&expected,{-16,-16,-16,true},{48,16,16,true},&error);
	const auto expectedBytes = serializeLevelMap(expected).bytes;
	camera->setSelectionResizeBox(to); const auto end = camera->selectionResizeHandles()[1]; camera->setSelectionResizeBox(from);
	const auto start = camera->selectionResizeHandles()[1];
	const auto gestureSize = camera->size();
	const auto gestureView = camera->navigationState();
	ok &= expect(camera->beginSelectionResize(start) && camera->updateSelectionResize(end),"shell camera begins snapped resize");
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == original && shell.levelDocument().undoStack.isEmpty(),"preview leaves shared document untouched");
	ok &= until([&] { camera->render(&image); return !camera->isRendering(); });
	if (!expect(camera->isResizingSelection() && camera->selectionResizeBox() == to,"completed preview retains the pending gesture",
		QStringLiteral("active=%1 size=%2x%3->%4x%5 viewSame=%6 enabled=%7")
		.arg(camera->isResizingSelection()).arg(gestureSize.width()).arg(gestureSize.height()).arg(camera->width()).arg(camera->height())
		.arg(camera->navigationState().position == gestureView.position && camera->navigationState().yaw == gestureView.yaw
			&& camera->navigationState().pitch == gestureView.pitch).arg(camera->isEnabled()))) { return 1; }
	const auto output = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
	if (!output.isEmpty()) {
		QDir().mkpath(output); auto* views = shell.findChild<QSplitter*>("levelMapViews");
		QImage frame(views->size(),QImage::Format_ARGB32_Premultiplied); views->render(&frame);
		ok &= frame.save(QDir(output).filePath("camera-resize-workspace.png"));
	}
	camera->finishSelectionResize(true);
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == expectedBytes && shell.levelDocument().undoStack.size() == 1,
		"camera release commits the shared resize result as one undo step",shell.statusBar()->currentMessage());
	for (auto* view : {top,front,side}) {
		ok &= expect(serializeLevelMap(view->displayDocument()).bytes == expectedBytes,"camera commit refreshes each plan pane");
	}
	ok &= expect(until([&] { return camera->isEnabled() && camera->selectionResizeBox() == to; }),"rebuilt camera uses committed bounds");
	shell.findChild<QAction*>("map.undo")->trigger();
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == original,"camera resize supports exact undo");
	shell.findChild<QAction*>("map.redo")->trigger();
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == expectedBytes,"camera resize supports exact redo");
	const auto savedPath = temp.filePath("resized.map");
	ok &= expect(shell.saveLevelDocument(savedPath,false,&error),"resized map saves",error);
	LevelMapDocument saved;
	ok &= expect(loadLevelMap({savedPath,{},{}},&saved,&error) && serializeLevelMap(saved).bytes == expectedBytes,"saved resize reloads exactly",error);
	ok &= until([&] { return camera->isEnabled() && camera->selectionResizeBox() == to; });
	ok &= camera->beginSelectionResize(camera->selectionResizeHandles()[1]); choose(1); camera->finishSelectionResize(true);
	ok &= expect(!camera->isResizingSelection() && serializeLevelMap(shell.levelDocument()).bytes == expectedBytes,
		"changing selection cancels the old camera gesture without editing the replacement");
	choose(0); ok &= camera->beginSelectionResize(camera->selectionResizeHandles()[1]);
	const auto depth = shell.levelDocument().undoStack.size(); top->moveRequested(0,16,0); camera->finishSelectionResize(true);
	ok &= expect(!camera->isResizingSelection() && shell.levelDocument().undoStack.size() == depth+1,
		"a concurrent plan edit cancels camera resize and creates only its own undo step");
	ok &= shell.saveLevelDocument(savedPath,true,&error);
	ok &= until([&] { return camera->isEnabled() && !noHandles(*camera); });
	ok &= camera->beginSelectionResize(camera->selectionResizeHandles()[1]);
	QString layer;
	ok &= createLevelSceneNode(&fixture,LevelSceneNodeKind::Layer,QStringLiteral("Locked geometry"),{},&layer,&error);
	ok &= assignLevelSceneObjects(&fixture,layer,{QStringLiteral("brush:0")},&error);
	ok &= setLevelSceneLocked(&fixture,layer,true,&error);
	const auto lockedPath = temp.filePath("locked.map"); ok &= writeMap(lockedPath,fixture); shell.openPathFromCommandLine(lockedPath);
	ok &= expect(until([&] { return shell.levelDocument().sourcePath == lockedPath && camera->isEnabled(); }) && !camera->isResizingSelection(),
		"document replacement cancels the old camera gesture");
	choose(0);
	ok &= expect(noHandles(*camera) && shell.levelDocument().undoStack.isEmpty(),"locked geometry has no resize affordance or accidental edit");
	const auto doomPath = temp.filePath("things.wad");
	{
		const auto text = tests::udmf::textmap() + QByteArray("\r\nthing { x = 192.25; y = 128.25; height = 0.5; type = 1; }\r\n");
		QFile file(doomPath); const auto wad = tests::udmf::fixture(text);
		ok &= file.open(QIODevice::WriteOnly) && file.write(wad) == wad.size();
	}
	shell.openPathFromCommandLine(doomPath);
	ok &= expect(until([&] { return shell.levelDocument().doomUdmf && camera->isEnabled() && camera->hasMesh(); }),"UDMF document and camera load");
	const QVector<LevelMapSelectionRef> sector{{LevelMapSelectionKind::DoomSector,0}};
	top->setSelectionSet(sector); top->selectionSetChanged(sector);
	ok &= expect(noHandles(*camera) && shell.findChild<QAction*>("map.resizeSelection")->isEnabled(),
		"UDMF topology retains numeric editing without an incomplete camera preview");
	const QVector<LevelMapSelectionRef> things{{LevelMapSelectionKind::DoomThing,0},{LevelMapSelectionKind::DoomThing,1}};
	top->setSelectionSet(things); top->selectionSetChanged(things); camera->setCameraView({-80,-100,180},50,-25);
	const auto doomFrom = camera->selectionResizeBox(); auto doomTo = doomFrom; doomTo.maxs[0] = 224;
	ok &= expect(!noHandles(*camera) && !std::isfinite(camera->selectionResizeHandles()[5].x()),"Doom things expose spacing handles only in XY");
	auto expectedDoom = shell.levelDocument();
	ok &= resizeLevelMapSelection(&expectedDoom,{doomTo.mins[0],doomTo.mins[1],doomTo.mins[2],true},
		{doomTo.maxs[0],doomTo.maxs[1],doomTo.maxs[2],true},&error);
	camera->setSelectionResizeBox(doomTo,{},3); const auto doomEnd = camera->selectionResizeHandles()[1];
	// Refresh through the shell to restore point-origin visual metadata.
	top->selectionSetChanged(things);
	ok &= expect(camera->beginSelectionResize(camera->selectionResizeHandles()[1]) && camera->updateSelectionResize(doomEnd),"Doom thing spacing previews");
	camera->finishSelectionResize(true);
	const bool doomExact = serializeLevelMap(shell.levelDocument()).bytes == serializeLevelMap(expectedDoom).bytes;
	ok &= expect(doomExact && shell.levelDocument().undoStack.size() == 1,"Doom camera resize shares the lossless native transaction");
	ok &= shell.saveLevelDocument(temp.filePath("resized-things.wad"),false,&error);
	std::cout << QJsonDocument(QJsonObject{{"ok",ok},{"undoRedoExact",serializeLevelMap(saved).bytes == expectedBytes},
		{"planPanes",3},{"savedBrushes",saved.brushes.size()},{"udmfThingResizeExact",doomExact}}).toJson(QJsonDocument::Compact).constData() << '\n';
	return ok ? 0 : 1;
}
