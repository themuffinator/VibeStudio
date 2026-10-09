#include "app/application_shell.h"
#include "app/level_primitive_dialog.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "tests/level_udmf_test_helpers.h"
#include "tests/render_test_support.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSplitter>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message, const QString& detail = {})
{
	if (!value) { std::cerr << "FAIL: " << message << ": " << detail.toStdString() << '\n'; }
	return value;
}
void drain(int milliseconds = 80)
{
	QEventLoop loop; QTimer::singleShot(milliseconds,&loop,&QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents);
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer elapsed; elapsed.start();
	while (!ready() && elapsed.elapsed() < 30000) { drain(20); }
	return ready();
}
bool writeMap(const QString& path, const LevelMapDocument& document)
{
	QFile file(path); const auto bytes = serializeLevelMap(document).bytes;
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
bool draw(ModelViewport& camera)
{
	return camera.beginBrushDraw({camera.width()*0.40,camera.height()*0.55})
		&& camera.updateBrushDraw({camera.width()*0.68,camera.height()*0.75});
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* source, const char*, int) const override { return QStringLiteral("[%1 expanded]").arg(QString::fromUtf8(source)); }
};
}
int main(int argc, char** argv)
{
	// Semantic application/widget methods only. Input events are excluded from
	// local event loops and images come directly from QWidget render targets.
	qputenv("QT_QPA_PLATFORM","offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR",QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc,argv); QTemporaryDir temp; QString error; bool ok = temp.isValid();
	// Draws in 3D: skip where no OpenGL or Vulkan renderer starts.
	if (const int skip = vibestudio::test_support::exitCodeWithoutRenderer("level-camera-brush-shell-smoke"); skip >= 0) {
		return skip;
	}
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	StudioSettings settings; settings.setReducedMotion(true); settings.setRestoreSession(false);
	settings.setSelectedEditorProfileId(QStringLiteral("trenchbroom")); settings.setLevelViewLayoutPreference(QStringLiteral("four-views")); settings.sync();
	applyStudioTheme(app,studioThemeTokens(StudioTheme::Dark,UiDensity::Standard,100));
	ApplicationShell shell; shell.resize(1700,1100); shell.show();
	LevelMapCreateRequest create; create.starterRoom = false;
	ok &= shell.createLevelDocument(create,&error); shell.findChild<QAction*>("shell.mode.levels")->trigger();
	auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
	auto* top = shell.findChild<MapViewport*>("mapViewport");
	auto* front = shell.findChild<MapViewport*>("mapViewport1");
	auto* side = shell.findChild<MapViewport*>("mapViewport2");
	auto* tool = shell.findChild<QComboBox*>("levelMaterialTool");
	auto* plane = shell.findChild<QComboBox*>("levelBrushPlane");
	auto* base = shell.findChild<QDoubleSpinBox*>("levelBrushBase");
	auto* depth = shell.findChild<QDoubleSpinBox*>("levelBrushDepth");
	auto* direction = shell.findChild<QComboBox*>("levelBrushDirection");
	auto* cameraSurface = shell.findChild<QToolButton*>("levelBrushCameraSurface");
	auto* material = shell.findChild<QComboBox*>("levelPaintMaterial");
	auto* command = shell.findChild<QAction*>("map.drawBrush");
	if (!expect(camera && top && front && side && tool && plane && base && depth && direction && cameraSurface && material && command && command->isEnabled(),"construction controls and registered command")) { return 1; }
	ok &= expect(until([&] { return camera->isEnabled() && camera->hasMesh(); }),"empty map camera finishes loading");
	ok &= expect(material->currentText() == QStringLiteral("__TB_empty"),"empty maps expose their default creation material");
	shell.findChild<QAction*>("map.grid16")->trigger(); shell.findChild<QCheckBox*>("levelMapSnap")->setChecked(true);
	material->setEditText(QStringLiteral("studio/new_brush")); command->trigger(); drain();
	ok &= expect(base->singleStep() == 16 && depth->singleStep() == 16,"numeric depth follows the shared grid");
	shell.findChild<QCheckBox*>("levelMapSnap")->setChecked(false);
	ok &= expect(base->singleStep() == 1 && depth->singleStep() == 1,"free construction uses one-unit numeric steps");
	shell.findChild<QCheckBox*>("levelMapSnap")->setChecked(true); drain();
	ok &= expect(camera->brushDrawTool() && command->isChecked() && tool->currentIndex() == 3 && plane->isVisible(),"palette action and camera tool stay synchronized");
	ok &= expect(base->value() == 0 && depth->value() == 64,"empty work zone starts at 0–64");
	camera->setCameraView({-180,-220,240},50,-32); drain();
	const auto original = serializeLevelMap(shell.levelDocument()).bytes;
	if (!expect(draw(*camera) && camera->adjustBrushDrawDepth(1),"empty map accepts a camera draft")) { return 1; }
	const auto bounds = camera->brushDrawBox(); auto expected = shell.levelDocument();
	ok &= addLevelMapBoxBrush(&expected,{bounds.mins[0],bounds.mins[1],bounds.mins[2],true},
		{bounds.maxs[0],bounds.maxs[1],bounds.maxs[2],true},QStringLiteral("studio/new_brush"),nullptr,&error);
	const auto expectedBytes = serializeLevelMap(expected).bytes;
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == original && shell.levelDocument().undoStack.isEmpty(),"draft leaves source and history untouched");
	for (auto* view : {top,front,side}) { ok &= expect(view->hasCameraBrushDraft() && serializeLevelMap(view->displayDocument()).bytes == original,
		"plan draft outlines share camera bounds without changing their document"); }
	const auto output = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
	if (!output.isEmpty()) {
		QDir().mkpath(output); auto* views = shell.findChild<QSplitter*>("levelMapViews");
		QImage image(views->size(),QImage::Format_ARGB32_Premultiplied); views->render(&image);
		ok &= image.save(QDir(output).filePath("camera-brush-workspace.png"));
	}
	camera->finishBrushDraw(true);
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == expectedBytes && shell.levelDocument().undoStack.size() == 1,
		"camera release uses the shared brush transaction with one undo step",shell.statusBar()->currentMessage());
	for (auto* view : {top,front,side}) { ok &= expect(!view->hasCameraBrushDraft() && serializeLevelMap(view->displayDocument()).bytes == expectedBytes,"created brush replaces the draft in every plan pane"); }
	ok &= expect(until([&] { return camera->isEnabled() && camera->hasMesh(); }),"created brush camera rebuild completes");
	shell.findChild<QAction*>("map.undo")->trigger(); ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == original,"undo restores exact empty source");
	shell.findChild<QAction*>("map.redo")->trigger(); ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == expectedBytes,"redo restores exact creation");
	const auto savedPath = temp.filePath("drawn.map"); ok &= shell.saveLevelDocument(savedPath,false,&error);
	LevelMapDocument saved; ok &= expect(loadLevelMap({savedPath,{},{}},&saved,&error) && serializeLevelMap(saved).bytes == expectedBytes,"save/reload preserves the new brush",error);
	ok &= until([&] { return camera->isEnabled(); }); drain();
	const QVector<LevelMapSelectionRef> selection{{LevelMapSelectionKind::QuakeBrush,0}};
	top->setSelectionSet(selection); top->selectionSetChanged(selection);
	plane->setCurrentIndex(1); drain();
	ok &= expect(base->value() == bounds.mins[1] && depth->value() == bounds.maxs[1]-bounds.mins[1],"XZ construction inherits the plan selection's Y work zone",
		QStringLiteral("base=%1/%2 depth=%3/%4").arg(base->value()).arg(bounds.mins[1]).arg(depth->value()).arg(bounds.maxs[1]-bounds.mins[1]));
	plane->setCurrentIndex(0); base->setValue(16); depth->setValue(96); drain();
	ok &= draw(*camera); material->setEditText(QStringLiteral("studio/replacement")); camera->finishBrushDraw(true);
	ok &= expect(!camera->isDrawingBrush() && serializeLevelMap(shell.levelDocument()).bytes == expectedBytes,"material changes cancel pending creation");
	for (auto* view : {top,front,side}) { ok &= expect(!view->hasCameraBrushDraft(),"cancellation clears every plan draft"); }
	ok &= draw(*camera); auto* cancel = shell.findChild<QToolButton*>("levelCancelMaterialStroke");
	ok &= expect(cancel->isEnabled() && cancel->text() == QStringLiteral("Cancel Draft"),"draft exposes a named cancellation control");
	cancel->click(); ok &= expect(!camera->isDrawingBrush() && !cancel->isEnabled() && serializeLevelMap(shell.levelDocument()).bytes == expectedBytes,
		"Cancel Draft clears previews without creating history");
	ok &= draw(*camera); top->setSelectionSet({}); top->selectionSetChanged({}); camera->finishBrushDraw(true);
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == expectedBytes,"selection changes cancel pending creation");
	ok &= draw(*camera); shell.findChild<QAction*>("map.paintMaterial")->trigger();
	ok &= expect(!camera->isDrawingBrush() && !camera->brushDrawTool() && !command->isChecked() && tool->currentIndex() == 1,"painting replaces the draft and unchecks the action");
	command->trigger(); drain();
	// The numeric route starts with the camera plane/depth and the same material.
	base->setValue(24); depth->setValue(80); drain();
	bool numeric = false; QString numericMaterial = QStringLiteral("studio/replacement"); QTimer driver; driver.setInterval(20);
	double numericLow = 24, numericHigh = 104;
	QObject::connect(&driver,&QTimer::timeout,&shell,[&] {
		auto* modal = QApplication::activeModalWidget();
		if (!modal || modal->objectName() != QStringLiteral("addBrushDialog")) { return; }
		auto* dialog = static_cast<LevelPrimitiveDialog*>(modal);
		const auto request = dialog->request();
		numeric = request.mins.z == numericLow && request.maxs.z == numericHigh && request.texture == numericMaterial;
		dialog->reject(); driver.stop();
	});
	driver.start(); shell.findChild<QToolButton*>("levelBrushNumeric")->click(); driver.stop();
	ok &= expect(numeric && serializeLevelMap(shell.levelDocument()).bytes == expectedBytes,"numeric creation shares camera depth/material and cancellation is lossless");
	material->setEditText(QString()); numeric = false; numericMaterial = QStringLiteral("studio/new_brush");
	driver.start(); shell.findChild<QToolButton*>("levelBrushNumeric")->click(); driver.stop();
	ok &= expect(numeric,"cleared material picker uses the same last-drawn fallback in numeric creation");
	direction->setCurrentIndex(direction->findData(-1)); numericLow = -56; numericHigh = 24; numeric = false;
	driver.start(); shell.findChild<QToolButton*>("levelBrushNumeric")->click(); driver.stop();
	ok &= expect(numeric && camera->brushDrawDirection() == -1,"numeric brush bounds follow negative camera extrusion");
	const auto beforeNegative = serializeLevelMap(shell.levelDocument()).bytes;
	const auto negativeHistory = shell.levelDocument().undoStack.size();
	ok &= draw(*camera); const auto negativeBox = camera->brushDrawBox();
	ok &= expect(negativeBox.mins[2] == -56 && negativeBox.maxs[2] == 24,"negative camera draft preserves the construction plane");
	camera->finishBrushDraw(true);
	ok &= expect(shell.levelDocument().brushes.size() == saved.brushes.size()+1 && shell.levelDocument().undoStack.size() == negativeHistory+1,
		"negative camera extrusion commits through the shared undo transaction");
	shell.findChild<QAction*>("map.undo")->trigger();
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == beforeNegative,"negative camera brush undo restores exact source");
	ok &= until([&] { return camera->isEnabled() && camera->hasMesh(); }); drain();
	// Surface picking uses current rendered geometry and synchronises the
	// numeric plane/direction without changing source or selection.
	const auto midY = (bounds.mins[1]+bounds.maxs[1])/2, midZ = (bounds.mins[2]+bounds.maxs[2])/2;
	camera->setCameraView({float(bounds.mins[0]-128),float(midY),float(midZ)},0,0);
	const QPointF aim(camera->width()*0.5,camera->height()*0.5);
	ok &= until([&] { return camera->hitAt(aim).valid; }); cameraSurface->click();
	ok &= expect(plane->currentData().toInt() == 0 && direction->currentData().toInt() == -1
		&& std::abs(base->value()-bounds.mins[0]) < 0.001 && camera->brushDrawDirection() == -1,
		"camera wall plane and outward direction synchronise every construction control");
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == beforeNegative && shell.levelDocument().undoStack.size() == negativeHistory,
		"sampling a surface changes construction state without editing map history");
	direction->setCurrentIndex(direction->findData(1));
	plane->setCurrentIndex(plane->findData(2));
	// Scene destinations can change without a source revision. Preserve the
	// destination captured at gesture start rather than silently retargeting.
	LevelMapDocument layered = saved; QString layer,locked;
	ok &= createLevelSceneNode(&layered,LevelSceneNodeKind::Layer,QStringLiteral("Authoring"),{},&layer,&error);
	ok &= createLevelSceneNode(&layered,LevelSceneNodeKind::Layer,QStringLiteral("Locked"),{},&locked,&error);
	ok &= setLevelSceneLocked(&layered,locked,true,&error);
	const auto layeredPath = temp.filePath("layers.map"); ok &= writeMap(layeredPath,layered); shell.openPathFromCommandLine(layeredPath);
	ok &= until([&] { return shell.levelDocument().sourcePath == layeredPath && camera->isEnabled(); });
	command->trigger(); drain(); camera->setCameraView({-180,-220,240},50,-32);
	auto* destination = shell.findChild<QComboBox*>("levelSceneCreation");
	ok &= draw(*camera); destination->setCurrentIndex(destination->findData(layer)); camera->finishBrushDraw(true);
	ok &= expect(shell.levelDocument().brushes.size() == saved.brushes.size() && shell.levelDocument().undoStack.isEmpty(),"changed creation destination cancels the old draft");
	ok &= draw(*camera); camera->finishBrushDraw(true);
	ok &= expect(shell.levelDocument().brushes.size() == saved.brushes.size()+1 && shell.levelDocument().undoStack.size() == 1
		&& levelSceneMembership(shell.levelDocument().scene,QStringLiteral("brush:1")) == layer,"new camera brush joins its creation layer in the same undo command");
	ok &= until([&] { return camera->isEnabled(); }); drain();
	destination->setCurrentIndex(destination->findData(locked));
	const auto protectedBytes = serializeLevelMap(shell.levelDocument()).bytes; const auto history = shell.levelDocument().undoStack.size();
	ok &= draw(*camera); camera->finishBrushDraw(true);
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == protectedBytes && shell.levelDocument().undoStack.size() == history,
		"locked creation layer refuses camera insertion atomically",shell.statusBar()->currentMessage());
	ok &= shell.saveLevelDocument(layeredPath,true,&error);
	const auto doomPath = temp.filePath("unsupported.wad"); QFile wad(doomPath); const auto bytes = tests::udmf::fixture(tests::udmf::textmap());
	ok &= wad.open(QIODevice::WriteOnly) && wad.write(bytes) == bytes.size(); wad.close();
	shell.openPathFromCommandLine(doomPath); ok &= until([&] { return shell.levelDocument().doomUdmf && camera->isEnabled(); });
	ok &= expect(!command->isEnabled() && !camera->brushDrawTool() && tool->currentIndex() == 0,"Doom keeps its native sector workflow and clears brush mode");
	shell.hide();
	{
		Expanded expanded; app.installTranslator(&expanded);
		settings.setTheme(StudioTheme::HighContrastDark); settings.setTextScalePercent(200); settings.sync();
		applyStudioTheme(app,studioThemeTokens(StudioTheme::HighContrastDark,UiDensity::Standard,200));
		ApplicationShell large; large.resize(2200,1450); large.setLayoutDirection(Qt::RightToLeft); large.show();
		ok &= large.createLevelDocument(create,&error); auto* largeCamera = large.findChild<ModelViewport*>("mapPreview3D");
		ok &= until([&] { return largeCamera->isEnabled() && largeCamera->hasMesh(); });
		large.findChild<QAction*>("map.drawBrush")->trigger(); drain();
		auto* panel = large.findChild<QWidget*>("levelMaterialTools");
		QVector<QRect> controls;
		for (const auto* name : {"levelMaterialTool","levelPaintMaterial","levelBrushPlane","levelBrushBase","levelBrushDepth","levelBrushDirection","levelBrushWorkZone","levelBrushCameraSurface","levelBrushNumeric"}) {
			auto* control = large.findChild<QWidget*>(name);
			const QRect bounds(control->mapTo(panel,QPoint()),control->size());
			ok &= expect(control->isVisible() && !control->accessibleName().isEmpty() && control->focusPolicy() != Qt::NoFocus
				&& panel->rect().contains(bounds),"expanded RTL construction control stays visible, named and focusable",QString::fromLatin1(name));
			for (const auto& other : controls) { ok &= expect(!bounds.intersects(other),"construction controls do not overlap"); }
			controls.append(bounds);
		}
		ok &= expect(large.findChild<QComboBox*>("levelMaterialTool")->itemText(3).startsWith('['),"tool caption uses the expanded translator");
		if (!output.isEmpty()) {
			QImage image(panel->size(),QImage::Format_ARGB32_Premultiplied); panel->render(&image);
			ok &= image.save(QDir(output).filePath("camera-brush-tools-expanded-rtl.png"));
		}
		large.hide(); app.removeTranslator(&expanded);
	}
	std::cout << QJsonDocument(QJsonObject{{"ok",ok},{"savedBrushes",saved.brushes.size()},{"planPanes",3},{"numericCameraContext",numeric}}).toJson(QJsonDocument::Compact).constData() << '\n';
	return ok ? 0 : 1;
}
