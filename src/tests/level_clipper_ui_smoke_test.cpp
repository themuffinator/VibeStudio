#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/editor_profiles.h"
#include "tests/level_geometry_test_helpers.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace {
bool expect(bool value,const char* message,const QString& detail = {})
{
	if (!value) { std::cerr << "FAIL: " << message << ": " << detail.toStdString() << '\n'; } return value;
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer elapsed; elapsed.start();
	while (!ready() && elapsed.elapsed() < 30000) { QEventLoop loop; QTimer::singleShot(20,&loop,&QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents); }
	return ready();
}
bool settle(ModelViewport& view)
{
	return until([&] { QImage image(view.size(),QImage::Format_ARGB32_Premultiplied); view.render(&image); return view.isEnabled() && !view.isRendering(); });
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*,const char* text,const char*,int) const override { return QStringLiteral("[%1 expanded]").arg(QString::fromUtf8(text)); }
};
LevelClipState cut(double x = 0,LevelMapClipKeep keep = LevelMapClipKeep::Back)
{
	LevelClipState state; state.points = {{x,-32,0,true},{x,32,0,true}}; state.keep = keep; return state;
}
}

int main(int argc,char** argv)
{
	// Semantic commands only: no injected keyboard/mouse input or OS captures.
	qputenv("QT_QPA_PLATFORM","offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR",QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc,argv); QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	const auto output = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR"); if (!output.isEmpty()) { QDir().mkpath(output); }
	LevelMapDocument fixture; QString error; if (!tests::createGeometryFixture(2,&fixture,&error)) { return 1; }
	bool ok = true;
	for (int scale : {100,200}) {
		Expanded expanded; if (scale == 200) { app.installTranslator(&expanded); }
		applyStudioTheme(app,studioThemeTokens(scale == 100 ? StudioTheme::HighContrastLight : StudioTheme::HighContrastDark,UiDensity::Standard,scale));
		MapViewport plan; plan.resize(1000,750); plan.setDocument(fixture); plan.setHighContrast(true); plan.setSnapToGrid(true); plan.setGridSize(16);
		plan.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight); plan.setClipMode(true); plan.setClipLine({0,-32},{0,32});
		LevelMapVec3 a,b,c; ok &= expect(plan.clipPlanePoints(&a,&b,&c) && planeFromPoints(b,a,c).normalX == 1,"two plan points preserve the original front convention");
		const auto original = plan.clipState(); const auto start = plan.clipPointPosition(0);
		ok &= expect(plan.beginClipPoint(start) && plan.moveClipPoint(plan.viewPointFor(16,-32)),"existing numbered point moves on the grid");
		ok &= expect(plan.clipState().points[0].x == 16,"moved handle snaps in plane coordinates");
		plan.finishClipPoint(false); ok &= expect(plan.clipState() == original,"cancel restores the entire plane");
		ok &= plan.beginClipPoint(plan.clipPointPosition(0)); plan.setClipState({});
		ok &= expect(!plan.moveClipPoint(start) && plan.clipState().points.isEmpty(),"external draft reset cancels an active plan handle"); plan.setClipState(original);
		plan.clearClipLine(); ok &= expect(!plan.beginClipPoint(plan.viewPointFor(2000000,0)) && !plan.moveClipPoint(start),"out-of-range point placement cannot leave a live invalid handle"); plan.setClipState(original);
		plan.setProjection(MapViewportProjection::FrontXZ); ok &= expect(plan.clipState() == original,"projection changes preserve the world-space plane");
		ok &= plan.beginClipPoint(plan.viewPointFor(32,32)); plan.finishClipPoint(true);
		ok &= expect(plan.clipState().points.size() == 3 && plan.clipPlanePoints(&a,&b,&c) && std::abs(planeFromPoints(b,a,c).normalZ) > 0.1,"third point in another projection defines a sloped plane");
		plan.removeLastClipPoint(); ok &= expect(plan.clipState() == original,"removing the third point restores the original extrusion");
		auto bad = original; bad.points[0].x = std::numeric_limits<double>::infinity(); ok &= expect(!plan.setClipState(bad) && plan.clipState() == original,"non-finite points cannot poison the draft");
		plan.setClipPreview({LevelClipPreview::Status::Ready,{{a,b},{b,c}},QStringLiteral("Checked cut · dashed wire shows kept result")});
		if (!output.isEmpty()) { QImage image(plan.size(),QImage::Format_ARGB32_Premultiplied); plan.render(&image); ok &= image.save(QDir(output).filePath(QStringLiteral("clip-plan-%1.png").arg(scale))); }
		if (scale == 200) { app.removeTranslator(&expanded); }
	}
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini")); StudioSettings settings;
	settings.setReducedMotion(true); settings.setRestoreSession(false); settings.setSelectedEditorProfileId(QStringLiteral("trenchbroom"));
	settings.setLevelViewLayoutPreference(QStringLiteral("four-views")); settings.sync();
	applyStudioTheme(app,studioThemeTokens(StudioTheme::Dark,UiDensity::Standard,100));
	QFile file(temp.filePath("fixture.map")); const auto bytes = serializeLevelMap(fixture).bytes;
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) { return 1; } file.close();
	ApplicationShell shell; shell.resize(1700,1100); shell.show(); shell.openPathFromCommandLine(file.fileName()); shell.findChild<QAction*>("shell.mode.levels")->trigger();
	auto* camera = shell.findChild<ModelViewport*>("mapPreview3D"); auto* top = shell.findChild<MapViewport*>("mapViewport");
	auto* front = shell.findChild<MapViewport*>("mapViewport1"); auto* side = shell.findChild<MapViewport*>("mapViewport2");
	if (!camera || !top || !front || !side || !until([&] { return shell.levelDocument().brushes.size() == 2 && camera->isEnabled() && camera->hasMesh(); })) { return 1; }
	qsizetype selectionDepth = 0;
	const auto choose = [&](QVector<LevelMapSelectionRef> selection) { top->setSelectionSet(selection); top->selectionSetChanged(selection); selectionDepth = shell.levelDocument().undoStack.size(); };
	choose({{LevelMapSelectionKind::QuakeBrush,0},{LevelMapSelectionKind::QuakeBrush,1}});
	shell.findChild<QAction*>("map.grid16")->trigger(); shell.findChild<QCheckBox*>("levelMapSnap")->setChecked(true);
	shell.findChild<QAction*>("map.clipTool")->trigger(); camera->setCameraView({-96,-120,88},50,-30); ok &= settle(*camera);
	const auto before = serializeLevelMap(shell.levelDocument()).bytes;
	for (int i = 0; i < 12; ++i) { top->setClipState(cut(i-8)); }
	ok &= expect(top->clipState() == camera->clipState() && front->clipState() == side->clipState(),"every view shares the newest draft");
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == before && shell.levelDocument().undoStack.size() == selectionDepth,"coalesced preview leaves the source untouched");
	ok &= expect(until([&] { return camera->clipPreview().status == LevelClipPreview::Status::Ready; }),"background clipping completes",camera->clipPreview().message);
	if (!output.isEmpty()) { QImage image(camera->size(),QImage::Format_ARGB32_Premultiplied); camera->render(&image); ok &= image.save(QDir(output).filePath("clip-camera-preview.png")); }
	auto expected = shell.levelDocument(); LevelMapVec3 a,b,c; top->clipPlanePoints(&a,&b,&c);
	ok &= clipLevelMapSelection(&expected,a,b,c,top->clipKeep(),nullptr,&error); camera->requestClip();
	ok &= expect(until([&] { return shell.levelDocument().undoStack.size() == selectionDepth + 1; }) && serializeLevelMap(shell.levelDocument()).bytes == serializeLevelMap(expected).bytes,
		"commit adopts the exact prepared cut, removing the wholly discarded brush",shell.statusBar()->currentMessage());
	shell.findChild<QAction*>("map.undo")->trigger(); ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == before && shell.levelDocument().selection.size() == 2,"undo restores geometry and both selected brushes");
	shell.findChild<QAction*>("map.redo")->trigger(); ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == serializeLevelMap(expected).bytes,"redo restores the prepared result");
	shell.findChild<QAction*>("map.undo")->trigger();
	ok &= until([&] { return camera->isEnabled(); }); ok &= settle(*camera);
	// Pending apply waits for the newest result, and Escape/tool exit cancels it.
	top->setClipState(cut(0,LevelMapClipKeep::Both)); camera->requestClip(); camera->setClipMode(false);
	ok &= until([&] { return camera->isEnabled(); });
	ok &= expect(shell.levelDocument().undoStack.size() == selectionDepth && !top->clipMode() && !front->clipMode(),"leaving clip mode cancels a pending apply in every view");
	shell.findChild<QAction*>("map.clipTool")->trigger(); top->setClipState(cut(0,LevelMapClipKeep::Both)); camera->requestClip();
	ok &= expect(until([&] { return shell.levelDocument().undoStack.size() == selectionDepth + 1; }) && shell.levelDocument().brushes.size() == 3,"pending apply commits a complete split with unaffected brushes retained");
	shell.findChild<QAction*>("map.undo")->trigger();
	// Saving without a geometry revision invalidates the captured file bindings.
	top->setClipState(cut()); ok &= shell.saveLevelDocument(temp.filePath("saved.map"),false,&error); camera->requestClip();
	ok &= expect(shell.levelDocument().undoStack.size() == selectionDepth && camera->clipState().points.isEmpty() && shell.levelDocument().outputPath.endsWith("saved.map"),"saving cancels an obsolete preview without restoring old file bindings");
	choose({{LevelMapSelectionKind::QuakeBrush,0}}); ok &= until([&] { return camera->isEnabled(); }); ok &= settle(*camera);
	// Actual surface placement and exact-face matching under every shipped
	// interaction profile; the tests call semantic methods, never send input.
	for (const auto& id : {QStringLiteral("trenchbroom"),QStringLiteral("netradiant-custom"),QStringLiteral("gtkradiant"),QStringLiteral("quark")}) {
		EditorProfileDescriptor profile; ok &= editorProfileForId(id,&profile); camera->setCameraControls(profile.controls.camera);
		camera->setClipMode(true); camera->setCameraView({-96,-120,88},50,-30); ok &= settle(*camera);
		QPointF facePoint; bool found = false;
		for (int y = 25; y < camera->height()-25 && !found; y += 7) { for (int x = 25; x < camera->width()-25 && !found; x += 7) {
			if (camera->beginClipPoint({double(x),double(y)})) { facePoint = {double(x),double(y)}; camera->finishClipPoint(true); found = true; }
		} }
		ok &= expect(found && camera->clipState().points.size() == 1,"camera places a grid point on a rendered brush face",id);
		if (!found) { break; }
		ok &= camera->beginClipPoint(camera->clipPointPosition(0)); camera->setClipState({});
		ok &= expect(!camera->moveClipPoint(facePoint),"external draft reset cancels an active camera handle");
		ok &= expect(camera->matchClipFace(facePoint) && camera->clipState().points.size() == 3,"camera matches an original brush plane",id);
		const auto exact = camera->clipState(); camera->removeLastClipPoint(); ok &= expect(top->clipState().points.size() == 2,"camera point removal is shared");
		camera->setClipState(exact); const auto savedPlane = top->clipState();
		top->setProjection(MapViewportProjection::FrontXZ); ok &= top->beginClipPoint(top->clipPointPosition(0));
		ok &= top->moveClipPoint(top->clipPointPosition(0)+QPointF(40,0)); top->finishClipPoint(false);
		ok &= expect(camera->clipState() == savedPlane,"cancelling a plan adjustment restores the camera plane");
		camera->setClipState({});
	}
	// Numeric three-point editing is fully reachable without a viewport gesture.
	bool numeric = false;
	Expanded expanded; app.installTranslator(&expanded);
	applyStudioTheme(app,studioThemeTokens(StudioTheme::HighContrastDark,UiDensity::Standard,200)); app.setLayoutDirection(Qt::RightToLeft);
	QTimer::singleShot(0,&shell,[&] {
		auto* dialog = shell.findChild<QDialog*>("clipSelectionDialog"); if (!dialog) { return; }
		dialog->findChild<QComboBox*>("clipPlaneMode")->setCurrentIndex(1);
		for (auto* spin : dialog->findChildren<QDoubleSpinBox*>()) { spin->setValue(0); }
		ok &= expect(!dialog->findChild<QPushButton*>("clipPreview")->isEnabled(),"degenerate numeric points disable Preview before closing the dialog");
		const double points[3][3]{{0,-32,0},{0,32,0},{0,-32,64}};
		for (int i = 0; i < 3; ++i) { for (int j = 0; j < 3; ++j) {
			auto* spin = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("clipPoint%1%2").arg(i+1).arg(QLatin1Char("XYZ"[j])));
			if (!spin || spin->accessibleName().isEmpty()) { dialog->reject(); return; } spin->setValue(points[i][j]);
		} }
		numeric = dialog->findChild<QPushButton*>("clipPreview")->isEnabled();
		ok &= expect(dialog->layoutDirection() == Qt::RightToLeft && dialog->findChild<QPushButton*>("clipApply")->text().contains("Clip"),"numeric dialog retains RTL and its task-specific action label");
		if (!output.isEmpty()) { QImage image(dialog->size(),QImage::Format_ARGB32_Premultiplied); dialog->render(&image); ok &= image.save(QDir(output).filePath("clip-numeric-200.png")); }
		dialog->done(2);
	});
	shell.findChild<QAction*>("map.clipSelection")->trigger();
	app.removeTranslator(&expanded); applyStudioTheme(app,studioThemeTokens(StudioTheme::Dark,UiDensity::Standard,100)); app.setLayoutDirection(Qt::LeftToRight);
	ok &= expect(numeric && until([&] { return camera->clipPreview().status == LevelClipPreview::Status::Ready; }) && shell.levelDocument().undoStack.size() == selectionDepth,"numeric Preview uses the shared asynchronous service without committing");
	// Saved clipper policies use the same checked draft and preserve failure /
	// cancellation semantics. Activate widgets directly, never inject input.
	const auto configure = [&](LevelClipperPreferences preferences,int scale = 100,bool reject = false) {
		bool visited = false;
		if (scale == 200) { app.installTranslator(&expanded); app.setLayoutDirection(Qt::RightToLeft); }
		applyStudioTheme(app,studioThemeTokens(scale == 100 ? StudioTheme::HighContrastLight : StudioTheme::HighContrastDark,UiDensity::Standard,scale));
		QTimer::singleShot(0,&shell,[&] {
			auto* dialog = shell.findChild<QDialog*>("clipperOptionsDialog"); if (!dialog) { return; } visited = true;
			dialog->findChild<QComboBox*>("clipperMaterial")->setCurrentIndex(int(preferences.material));
			dialog->findChild<QComboBox*>("clipperDoubleClick")->setCurrentIndex(int(preferences.doubleClick));
			dialog->findChild<QCheckBox*>("clipperResetPoints")->setChecked(preferences.resetPoints);
			dialog->findChild<QCheckBox*>("clipperResetSide")->setChecked(preferences.resetSide);
			dialog->findChild<QCheckBox*>("clipperTwoPoints")->setChecked(preferences.twoPointsInPlan);
			dialog->findChild<QCheckBox*>("clipperWrapMerge")->setChecked(preferences.wrapMerge);
			auto* name = dialog->findChild<QLineEdit*>("clipperTexture"); auto* accept = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
			name->setText("bad material"); accept->click();
			ok &= expect(dialog->isVisible() && !dialog->findChild<QLabel*>("clipperOptionsError")->isHidden(),"invalid cut material is rejected in the options dialog");
			name->setText(preferences.customTexture);
			dialog->adjustSize(); dialog->layout()->activate();
			auto* form = dialog->findChild<QFormLayout*>();
			for (int row = 0; row < form->rowCount(); ++row) {
				auto* label = form->itemAt(row,QFormLayout::LabelRole); auto* field = form->itemAt(row,QFormLayout::FieldRole);
				if (label && field) { ok &= expect(!label->geometry().intersects(field->geometry()),"scaled RTL labels do not overlap their fields"); }
			}
			for (auto* field : dialog->findChildren<QWidget*>()) {
				if (qobject_cast<QComboBox*>(field) || qobject_cast<QLineEdit*>(field) || qobject_cast<QCheckBox*>(field)) {
					ok &= expect(!field->accessibleName().isEmpty() && field->focusPolicy() != Qt::NoFocus,"clipper option is labelled and keyboard reachable");
				}
			}
			if (!output.isEmpty()) { QImage image(dialog->size(),QImage::Format_ARGB32_Premultiplied); dialog->render(&image); ok &= image.save(QDir(output).filePath(QStringLiteral("clip-options-%1.png").arg(scale))); }
			if (reject) { dialog->reject(); } else { accept->click(); }
		});
		shell.findChild<QAction*>("map.clipperOptions")->trigger(); ok &= expect(visited,"options action opens the dialog");
		if (scale == 200) { app.removeTranslator(&expanded); app.setLayoutDirection(Qt::LeftToRight); }
		applyStudioTheme(app,studioThemeTokens(StudioTheme::Dark,UiDensity::Standard,100));
	};
	LevelClipperPreferences preferences; preferences.material = LevelClipMaterial::Custom; preferences.customTexture = "studio/cut";
	preferences.resetPoints = false; preferences.resetSide = true; preferences.twoPointsInPlan = true; preferences.doubleClick = LevelClipDoubleClick::Split;
	configure(preferences); ok &= expect(settings.levelClipperPreferences() == preferences,"GUI preferences persist through the shared settings service");
	// Replacing a complete two-point plane is reversible until release.
	top->setProjection(MapViewportProjection::TopXY); top->setClipState(cut()); const auto retained = top->clipState();
	ok &= expect(top->beginClipPoint(top->viewPointFor(64,64)) && top->clipState().points.size() == 1,"two-point preference cycles a fresh plan plane");
	ok &= top->moveClipPoint(top->viewPointFor(80,64)); ok &= expect(top->clipState().points.size() == 2,"new cycled plane supports initial drag");
	top->finishClipPoint(false); ok &= expect(top->clipState() == retained && camera->clipState() == retained,"cancel restores the previous complete plane everywhere");
	ok &= until([&] { return top->clipPreview().status == LevelClipPreview::Status::Ready; });
	ok &= expect(top->activateClipDoubleClick() && until([&] { return shell.levelDocument().undoStack.size() == selectionDepth + 1; }),"configured double-click performs a checked split");
	ok &= expect(top->clipState().points.size() == 2 && top->clipKeep() == LevelMapClipKeep::Back && top->clipMode(),"successful cut retains points and resets side independently");
	int customFaces = 0; for (const auto& brush : shell.levelDocument().brushes) { for (const auto& face : brush.faces) { if (face.textureName == "studio/cut") { ++customFaces; } } }
	ok &= expect(customFaces == 2,"GUI uses the explicit cut material on both split faces");
	shell.findChild<QAction*>("map.undo")->trigger(); ok &= expect(top->clipState().points.isEmpty(),"undo invalidates retained cut points");
	// Pending cut is cancelled when options change, even if that dialog is cancelled.
	top->setClipState(cut(1)); camera->requestClip(); configure(preferences,200,true);
	ok &= expect(shell.levelDocument().undoStack.size() == selectionDepth,"opening options cancels a queued apply");
	preferences.material = LevelClipMaterial::Current; preferences.resetSide = false; preferences.resetPoints = true; preferences.doubleClick = LevelClipDoubleClick::Clip;
	configure(preferences);
	auto* picker = shell.findChild<QComboBox*>("levelPaintMaterial"); picker->setEditText("studio/first");
	top->setClipState(cut()); camera->requestClip(); picker->setEditText("studio/second");
	ok &= expect(until([&] { return top->clipPreview().status == LevelClipPreview::Status::Ready; }) && shell.levelDocument().undoStack.size() == selectionDepth
		&& top->clipPreview().message.contains("studio/second"),"changing current material retires a pending apply and rechecks the plane");
	ok &= settle(*camera); ok &= expect(camera->activateClipDoubleClick() && until([&] { return shell.levelDocument().undoStack.size() == selectionDepth + 1; }),"camera double-click applies the current material cut");
	ok &= expect(top->clipState().points.isEmpty() && top->clipMode(),"reset-points policy clears points without leaving normal clipping");
	shell.findChild<QAction*>("map.undo")->trigger();
	// Quick entry is a profile gesture or named action. Only accepted cuts exit.
	camera->setClipMode(false); top->setControls(netRadiantCustomLevelControls().plan); choose({{LevelMapSelectionKind::QuakeBrush,0}});
	ok &= expect(!top->beginQuickClipPoint(top->viewPointFor(0,0),Qt::ShiftModifier),"quick entry does not steal a selection chord");
	ok &= expect(top->beginQuickClipPoint(top->viewPointFor(0,-32),Qt::ControlModifier),"NRC quick clip starts through the shared command"); top->finishClipPoint(true);
	top->setClipState(cut(-100,LevelMapClipKeep::Both)); top->requestClip();
	ok &= expect(until([&] { return top->clipPreview().status == LevelClipPreview::Status::Refused; }) && top->clipMode()
		&& top->clipState().points.size() == 2 && shell.levelDocument().undoStack.size() == selectionDepth,"refused quick cut retains its draft and mode");
	top->setClipState(cut(0,LevelMapClipKeep::Front)); top->requestClip();
	ok &= expect(until([&] { return shell.levelDocument().undoStack.size() == selectionDepth + 1 && !top->clipMode(); }) && !camera->clipMode() && !front->clipMode(),"successful quick cut exits every pane after one undo step");
	shell.findChild<QAction*>("map.undo")->trigger();
	shell.findChild<QAction*>("map.quickClip")->trigger(); top->setClipState(cut()); top->requestClip(); camera->setClipMode(false);
	ok &= expect(shell.levelDocument().undoStack.size() == selectionDepth && !top->clipMode(),"cancelling a pending named quick cut leaves the map untouched");
	preferences.wrapMerge = true; preferences.material = LevelClipMaterial::Custom; preferences.customTexture = "studio/cut";
	configure(preferences,200); camera->setClipMode(true); choose({{LevelMapSelectionKind::QuakeBrush,0}});
	bool onePointEditor = false;
	QTimer::singleShot(0,&shell,[&] {
		auto* dialog = shell.findChild<QDialog*>("clipSelectionDialog"); if (!dialog) { return; }
		dialog->findChild<QComboBox*>("clipPlaneMode")->setCurrentIndex(2);
		for (int j = 0; j < 3; ++j) { dialog->findChild<QDoubleSpinBox*>(QStringLiteral("clipPoint1%1").arg(QLatin1Char("XYZ"[j])))->setValue(j == 0 ? 64 : 0); }
		onePointEditor = dialog->findChild<QPushButton*>("clipPreview")->isEnabled()
			&& !dialog->findChild<QDoubleSpinBox*>("clipPoint1X")->isHidden() && dialog->findChild<QDoubleSpinBox*>("clipPoint2X")->isHidden();
		dialog->done(2);
	});
	shell.findChild<QAction*>("map.clipSelection")->trigger();
	ok &= expect(onePointEditor && top->clipState().points.size() == 1
		&& until([&] { return top->clipPreview().status == LevelClipPreview::Status::Ready; })
		&& top->clipPreview().message.contains("wrap/merge") && shell.levelDocument().undoStack.size() == selectionDepth,"numeric one-point preview is keyboard reachable and names hull behaviour",top->clipPreview().message);
	if (!output.isEmpty()) { QImage image(top->size(),QImage::Format_ARGB32_Premultiplied); top->render(&image); ok &= image.save(QDir(output).filePath("clip-wrap-preview.png")); }
	auto wrapExpected = shell.levelDocument(); const auto wrapBefore = serializeLevelMap(wrapExpected).bytes;
	ok &= clipLevelMapPoints(&wrapExpected,top->clipState().points,2,1,top->clipKeep(),{"studio/cut",true,"studio/second"},nullptr,&error);
	top->requestClip(); ok &= expect(until([&] { return shell.levelDocument().undoStack.size() == selectionDepth + 1; })
		&& serializeLevelMap(shell.levelDocument()).bytes == serializeLevelMap(wrapExpected).bytes,"one-point plan Apply adopts exact shared hull",error);
	shell.findChild<QAction*>("map.undo")->trigger(); ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == wrapBefore,"GUI wrap undo restores original bytes");
	LevelClipState wrapState; wrapState.points = {{80,0,0,true}}; top->setClipState(wrapState); camera->requestClip(); picker->setEditText("studio/third");
	ok &= expect(until([&] { return top->clipPreview().status == LevelClipPreview::Status::Ready; }) && shell.levelDocument().undoStack.size() == selectionDepth
		&& top->clipPreview().message.contains("studio/third"),"current material change retires a wrap even with custom cut material",top->clipPreview().message);
	ok &= settle(*camera); ok &= expect(camera->activateClipDoubleClick() && until([&] { return shell.levelDocument().undoStack.size() == selectionDepth + 1; }),"camera one-point double-click applies checked hull");
	shell.findChild<QAction*>("map.undo")->trigger();
	wrapState.points = {{0,0,0,true}}; top->setClipState(wrapState); top->requestClip();
	ok &= expect(until([&] { return top->clipPreview().status == LevelClipPreview::Status::Refused; }) && shell.levelDocument().undoStack.size() == selectionDepth,"interior point refuses a no-op without history");
	wrapState.points = {{64,0,0,true}}; top->setClipState(wrapState); camera->requestClip(); camera->setClipMode(false);
	ok &= expect(shell.levelDocument().undoStack.size() == selectionDepth,"tool exit cancels a pending wrap");
	preferences = {}; configure(preferences);
	shell.findChild<QAction*>("map.layout.single-3d")->trigger(); camera->setClipMode(false); shell.findChild<QAction*>("map.clipTool")->trigger();
	ok &= expect(!camera->isHidden() && top->QWidget::isHidden() && camera->clipMode(),"clip activation preserves the camera-only layout");
	camera->setSurfaceTool(ModelViewportSurfaceTool::Paint); ok &= expect(!top->clipMode() && !camera->clipMode(),"material tool cleanly exits clipping in all views");
	shell.hide(); StudioSettings::setOverrideFilePath({}); return ok ? 0 : 1;
}
