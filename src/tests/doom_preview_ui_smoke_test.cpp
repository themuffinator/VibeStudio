#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/level_dependency_dialog.h"
#include "app/studio_theme.h"
#include "tests/doom_preview_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/render_test_support.h"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDialog>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QStatusBar>
#include <QToolButton>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QLabel>
#include <QListWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QPushButton>
#include <QTreeWidget>
#include <iostream>

using namespace vibestudio;
namespace d = vibestudio::tests::doom;
bool expect(bool value, const char* label, const QString& detail = {}) { if (!value) { std::cerr << label << ": " << detail.toStdString() << '\n'; } return value; }
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer elapsed; elapsed.start();
	while (!ready() && elapsed.elapsed()<45000) { QEventLoop loop; QTimer::singleShot(20,&loop,&QEventLoop::quit); loop.exec(); }
	return ready();
}
int main(int argc, char** argv)
{
	// Semantic API calls and offscreen widget render targets; no injected input.
	qputenv("QT_QPA_PLATFORM","offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR",QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc,argv); QTemporaryDir temp; bool ok=temp.isValid(); QString error;
	// Draws in 3D: skip where no OpenGL or Vulkan renderer starts.
	if (const int skip = vibestudio::test_support::exitCodeWithoutRenderer("doom-preview-ui-smoke"); skip >= 0) {
		return skip;
	}
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	StudioSettings settings; settings.setReducedMotion(true);
	const auto assetPath=temp.filePath(QStringLiteral("assets.wad")), mapPath=temp.filePath(QStringLiteral("room.wad"));
	ok &= d::write(assetPath,d::wad(d::assets())) && d::write(mapPath,d::fixture());
	PackageArchive archive; ok &= archive.load(assetPath,&error);
	for (int scale : {100,200}) {
		const auto theme=scale==100?StudioTheme::Dark:StudioTheme::HighContrastDark;
		settings.setTheme(theme); settings.setTextScalePercent(scale); settings.sync();
		applyStudioTheme(app,studioThemeTokens(theme,UiDensity::Standard,scale));
		app.setLayoutDirection(scale==200?Qt::RightToLeft:Qt::LeftToRight);
		ApplicationShell shell; shell.resize(scale==100?1600:2400,scale==100?1000:1600);
		if (scale==200) {shell.setLayoutDirection(Qt::RightToLeft);}
		shell.show(); shell.openPathFromCommandLine(assetPath); shell.openPathFromCommandLine(mapPath);
		auto* view=shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
		auto* action=shell.findChild<QAction*>(QStringLiteral("map.paintMaterial"));
		if (!expect(view && action,"camera and paint action exist")) {return 1;}
		action->trigger();
		if (!expect(until([&] {return view->isEnabled() && view->hasMesh() && view->hasSkin();}),"Doom images and geometry loaded")) {return 1;}
		auto* textures=shell.findChild<QListWidget*>(QStringLiteral("levelMapTextures"));
		QListWidgetItem *flat=nullptr, *wall=nullptr;
		if (textures) {
			for (int row=0;row<textures->count();++row) {
				auto* item=textures->item(row); const auto key=item->data(Qt::UserRole+12).toString();
				if (key==QStringLiteral("flats/stone")) {flat=item;}
				if (key==QStringLiteral("textures/stone")) {wall=item;}
			}
		}
		if (!expect(flat && wall,"flat and wall have separate material tiles")) {return 1;}
		ok &= expect(flat->text()!=wall->text() && flat->data(Qt::AccessibleTextRole)!=wall->data(Qt::AccessibleTextRole)
			&& flat->data(Qt::UserRole+11)!=wall->data(Qt::UserRole+11),"namespace labels and thumbnail identities differ");
		const auto flatImage=flat->data(Qt::UserRole+8).value<QImage>(), wallImage=wall->data(Qt::UserRole+8).value<QImage>();
		ok &= expect(!flatImage.isNull() && !wallImage.isNull() && flatImage.pixelColor(0,0)!=wallImage.pixelColor(0,0),"material tiles use the correct namespace image");
		ok &= expect(flat->data(Qt::UserRole+1).toString()==QStringLiteral("STONE") && wall->data(Qt::UserRole+1).toString()==QStringLiteral("TEXTURE1")
			&& flat->data(Qt::UserRole+13).toLongLong()!=wall->data(Qt::UserRole+13).toLongLong(),"material source handoff preserves exact flat and definition identities");
		textures->setCurrentItem(wall);
		for (QWidget* parent=textures->parentWidget(); parent; parent=parent->parentWidget()) {
			if (auto* tabs=qobject_cast<QTabWidget*>(parent)) {
				for (int i=0;i<tabs->count();++i) {if (tabs->widget(i)->isAncestorOf(textures)) {tabs->setCurrentIndex(i);}}
				break;
			}
		}
		auto controls=view->cameraControls(); controls.perspective=true; view->setCameraControls(controls);
		view->setCameraView({-150,-150,80},45,-25);
		ok &= expect(tests::settleModelViewport(*view),"Doom camera settles");
		const auto findTarget=[&](LevelMaterialKind kind) {
			const auto assets=resolveLevelPreviewAssets(shell.levelDocument(),archive);
			const auto mesh=buildLevelMapPreviewMesh(shell.levelDocument(),{.textureSizes=levelPreviewTextureSizes(assets)});
			for (int y=45;y<view->height()-20;y+=13) {for(int x=25;x<view->width()-20;x+=13) {
				const auto hit=view->hitAt(QPointF(x,y));
				if (hit.valid && hit.triangle<mesh.materialTargets.size() && mesh.materialTargets[hit.triangle].kind==kind) {return QPointF(x,y);}
			}}
			return QPointF(-1,-1);
		};
		const auto floorPoint=findTarget(LevelMaterialKind::SectorFloor), wallPoint=findTarget(LevelMaterialKind::WallMiddle);
		ok &= expect(floorPoint.x()>=0 && wallPoint.x()>=0 && view->backfaceCulling(),"interior floor and wall are visible and pickable");
		const auto before=serializeLevelMap(shell.levelDocument()).bytes; const auto revision=shell.levelDocument().revision;
		shell.chooseLevelPaintMaterial(QStringLiteral("ALTFLAT"));
		ok &= expect(view->beginSurfaceStroke(floorPoint),"begin floor paint"); view->finishSurfaceStroke(true);
		ok &= expect(shell.levelDocument().doomSectors[0].floorTexture==QStringLiteral("ALTFLAT") && shell.levelDocument().revision==revision+1 && !shell.levelDocument().doomGeometryChanged,"camera paints floor without staling nodes");
		ok &= expect(until([&]{return view->isEnabled();}) && tests::settleModelViewport(*view),"flat image refresh");
		ok &= expect(textures->currentItem() && textures->currentItem()->data(Qt::UserRole+12).toString()==QStringLiteral("textures/stone"),"material selection survives an asynchronous namespace refresh");
		view->setSurfaceTool(ModelViewportSurfaceTool::Sample);
		ok &= expect(view->sampleSurfaceAt(findTarget(LevelMaterialKind::SectorFloor)) && shell.levelPaintMaterial()==QStringLiteral("ALTFLAT"),"sample floor source material");
		shell.findChild<QAction*>(QStringLiteral("map.undo"))->trigger();
		ok &= expect(serializeLevelMap(shell.levelDocument()).bytes==before,"floor paint undo preserves entire WAD");
		ok &= until([&]{return view->isEnabled();}) && tests::settleModelViewport(*view);
		shell.findChild<QAction*>(QStringLiteral("map.redo"))->trigger();
		ok &= until([&]{return view->isEnabled();}) && tests::settleModelViewport(*view);
		view->setCameraView({-150,-150,80},45,25); ok &= tests::settleModelViewport(*view);
		const auto ceilingPoint=findTarget(LevelMaterialKind::SectorCeiling);
		ok &= expect(ceilingPoint.x()>=0 && view->sampleSurfaceAt(ceilingPoint) && shell.levelPaintMaterial()==QStringLiteral("CEIL"),"ceiling is independently visible and sampled");
		shell.chooseLevelPaintMaterial(QStringLiteral("ALTFLAT")); view->setSurfaceTool(ModelViewportSurfaceTool::Paint);
		ok &= view->beginSurfaceStroke(ceilingPoint); view->finishSurfaceStroke(true);
		ok &= expect(shell.levelDocument().doomSectors[0].ceilingTexture==QStringLiteral("ALTFLAT"),"camera paints ceiling");
		ok &= until([&]{return view->isEnabled();}) && tests::settleModelViewport(*view);
		// Aimed level at a wall, the crosshair commands slide its texture a unit at a time.
		view->setCameraView({0,0,64},0,0); ok &= tests::settleModelViewport(*view);
		const auto offsets=[&] {QPointF sum; for (const auto& side : shell.levelDocument().doomSidedefs) {sum+=QPointF(side.offsetX,side.offsetY);} return sum;};
		const auto unnudged=offsets();
		for (const char* id : {"map.nudgeCameraTextureLeft","map.nudgeCameraTextureLeft","map.nudgeCameraTextureDown"}) {
			shell.findChild<QAction*>(QString::fromLatin1(id))->trigger();
			ok &= until([&]{return view->isEnabled();}) && tests::settleModelViewport(*view);
		}
		ok &= expect(offsets()==unnudged+QPointF(2,-1),"crosshair nudges slide the wall texture",QStringLiteral("%1, %2").arg(offsets().x()).arg(offsets().y()));
		// Auto-align from that wall: the other three, 512 long on a 64-wide texture, take the same offsets.
		shell.findChild<QAction*>(QStringLiteral("map.alignWallTexturesCrosshair"))->trigger();
		ok &= until([&]{return view->isEnabled();}) && tests::settleModelViewport(*view);
		ok &= expect(offsets()==unnudged+QPointF(8,-4),"Align Textures at Crosshair carries the offsets round the room",QStringLiteral("%1, %2").arg(offsets().x()).arg(offsets().y()));
		shell.findChild<QAction*>(QStringLiteral("map.undo"))->trigger();
		ok &= expect(offsets()==unnudged+QPointF(2,-1),"one undo takes the alignment back");
		// Drag Textures in Camera: dragging the wall right and up slides its texture after the pointer.
		{
			auto* drag=shell.findChild<QAction*>(QStringLiteral("map.dragCameraTextures"));
			ok &= until([&]{return view->isEnabled();}) && tests::settleModelViewport(*view);
			if (expect(drag && drag->isEnabled(),"Drag Textures in Camera is offered")) {
				drag->trigger();
				const QPointF middle=QRectF(view->rect()).center();
				const auto send=[view](QEvent::Type type,QPointF at,Qt::MouseButton button,Qt::MouseButtons buttons) {
					QMouseEvent event(type,at,view->mapToGlobal(at),button,buttons,Qt::NoModifier); QCoreApplication::sendEvent(view,&event);
				};
				const QPointF before=offsets();
				const int steps=static_cast<int>(shell.levelDocument().undoStack.size());
				send(QEvent::MouseButtonPress,middle,Qt::LeftButton,Qt::LeftButton);
				send(QEvent::MouseMove,middle+QPointF(20,-10),Qt::NoButton,Qt::LeftButton);
				send(QEvent::MouseMove,middle+QPointF(40,-20),Qt::NoButton,Qt::LeftButton);
				send(QEvent::MouseButtonRelease,middle+QPointF(40,-20),Qt::LeftButton,Qt::NoButton);
				const QPointF after=offsets();
				ok &= expect(after.x()<before.x() && after.y()>before.y() && shell.levelDocument().undoStack.size()==steps+1,
					"the texture follows the pointer right and up, as one undo step",QStringLiteral("%1, %2 -> %3, %4").arg(before.x()).arg(before.y()).arg(after.x()).arg(after.y()));
				shell.findChild<QAction*>(QStringLiteral("map.undo"))->trigger();
				ok &= expect(offsets()==before,"one undo puts the texture back");
				ok &= until([&]{return view->isEnabled();}) && tests::settleModelViewport(*view);
				// Escape during a drag puts it back too.
				send(QEvent::MouseButtonPress,middle,Qt::LeftButton,Qt::LeftButton);
				send(QEvent::MouseMove,middle+QPointF(30,0),Qt::NoButton,Qt::LeftButton);
				QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier); QCoreApplication::sendEvent(view,&escape);
				send(QEvent::MouseButtonRelease,middle+QPointF(30,0),Qt::LeftButton,Qt::NoButton);
				ok &= expect(offsets()==before && shell.levelDocument().undoStack.size()==steps,"Escape during a drag leaves no trace");
				QKeyEvent leave(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier); QCoreApplication::sendEvent(view,&leave);
				ok &= expect(!drag->isChecked(),"Escape again turns dragging off");
			}
		}
		for (int step=0;step<3;++step) {shell.findChild<QAction*>(QStringLiteral("map.undo"))->trigger();}
		ok &= expect(offsets()==unnudged,"each nudge undoes on its own");
		ok &= until([&]{return view->isEnabled();}) && tests::settleModelViewport(*view);
		view->setCameraView({-150,-150,80},45,-15); ok &= tests::settleModelViewport(*view);
		const auto captures=qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
		if (!captures.isEmpty()) {
			QImage image(shell.size(),QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); shell.render(&image);
			ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("doom-camera-%1.png").arg(scale))),"save rendered Doom camera");
		}
		auto* dependencies=showLevelDependencyDialog(&shell,shell.levelDocument(),archive,[](const QStringList&) {});
		auto* tree=dependencies->findChild<QTreeWidget*>(QStringLiteral("levelDependencies"));
		auto* exportAssets=dependencies->findChild<QPushButton*>(QStringLiteral("exportLevelAssets"));
		ok &= expect(tree && exportAssets && until([&] {return tree->topLevelItemCount()>=8;}),"Doom dependency input rows appear");
		ok &= expect(!exportAssets->isEnabled(),"Doom subset export stays explicitly unavailable");
		ok &= expect(dependencies->layoutDirection()==app.layoutDirection() && exportAssets->toolTip().contains(QStringLiteral("Doom")),"dependency review follows layout direction and explains export coverage");
		if (!captures.isEmpty()) {
			QImage image(dependencies->size(),QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); dependencies->render(&image);
			ok &= image.save(QDir(captures).filePath(QStringLiteral("doom-dependencies-%1.png").arg(scale)));
		}
		dependencies->close();
		const auto output=temp.filePath(QStringLiteral("painted-%1.wad").arg(scale));
		ok &= expect(shell.saveLevelDocument(output,false,&error),"save camera material edits",error);
		LevelMapDocument reopened; ok &= expect(loadLevelMap({output,QStringLiteral("MAP01"),{}},&reopened,&error) && reopened.doomSectors[0].ceilingTexture==QStringLiteral("ALTFLAT") && reopened.doomSectors[0].floorTexture==QStringLiteral("ALTFLAT"),"saved floor and ceiling persist",error);
		// The Shapes tab's Rectangle drawn as a box in the Top view, as Doom Builder's rectangle mode does.
		auto* rectangle=shell.findChild<QToolButton*>(QStringLiteral("levelShape-sector-rectangle"));
		auto* draw=shell.findChild<QPushButton*>(QStringLiteral("levelShapeDraw"));
		auto* plan=shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
		if (expect(rectangle && draw && plan,"the sector shapes and the Top view are there")) {
			rectangle->click(); draw->click();
			ok &= expect(plan->shapeDrawMode() && draw->text()==QStringLiteral("Draw in a View"),"Draw in a View turns on shape drawing");
			const auto sectors=shell.levelDocument().doomSectors.size();
			// A drag about 128 units across, starting a little off the view's middle, inside the room.
			const QPointF middle=QRectF(plan->rect()).center();
			const auto world=[plan](QPointF at) {const auto p=plan->worldPositionAt(at,0.0); return QPointF(p.x,p.y);};
			const double perPixel=std::max(0.01,std::abs(world(middle+QPointF(100,0)).x()-world(middle).x())/100.0);
			const QPointF from=middle-QPointF(64.0/perPixel,64.0/perPixel), to=middle+QPointF(32.0/perPixel,32.0/perPixel);
			const auto send=[plan](QEvent::Type type,QPointF at,Qt::MouseButton button,Qt::MouseButtons buttons) {
				QMouseEvent event(type,at,plan->mapToGlobal(at),button,buttons,Qt::NoModifier); QCoreApplication::sendEvent(plan,&event);
			};
			send(QEvent::MouseButtonPress,from,Qt::LeftButton,Qt::LeftButton);
			send(QEvent::MouseMove,(from+to)/2.0,Qt::NoButton,Qt::LeftButton);
			send(QEvent::MouseMove,to,Qt::NoButton,Qt::LeftButton);
			send(QEvent::MouseButtonRelease,to,Qt::LeftButton,Qt::NoButton);
			ok &= expect(shell.levelDocument().doomSectors.size()==sectors+1,"a box dragged in the Top view becomes a rectangle sector",
				QStringLiteral("%1 -> %2: %3").arg(sectors).arg(shell.levelDocument().doomSectors.size()).arg(shell.statusBar()->currentMessage()));
			shell.findChild<QAction*>(QStringLiteral("map.undo"))->trigger();
			ok &= expect(shell.levelDocument().doomSectors.size()==sectors,"one undo takes the sector away");
			QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier); QCoreApplication::sendEvent(plan,&escape);
			ok &= expect(!plan->shapeDrawMode(),"Escape stops shape drawing");
			// Make Sector Mode: a click inside the room makes its area a new sector in the room's place.
			auto* makeSector=shell.findChild<QAction*>(QStringLiteral("map.makeSectorMode"));
			if (expect(makeSector && makeSector->isEnabled(),"Make Sector Mode is offered on a Doom map")) {
				makeSector->trigger();
				ok &= expect(plan->makeSectorMode() && makeSector->isChecked(),"Make Sector Mode turns on");
				const QByteArray room=serializeLevelMap(shell.levelDocument()).bytes;
				const QPointF middle=QRectF(plan->rect()).center();
				QMouseEvent press(QEvent::MouseButtonPress,middle,plan->mapToGlobal(middle),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
				QCoreApplication::sendEvent(plan,&press);
				QMouseEvent release(QEvent::MouseButtonRelease,middle,plan->mapToGlobal(middle),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
				QCoreApplication::sendEvent(plan,&release);
				ok &= expect(shell.statusBar()->currentMessage().startsWith(QStringLiteral("Made sector:")) && shell.levelDocument().doomSectors.size()==sectors
						&& shell.levelDocument().selection.size()==1 && shell.levelDocument().selection.first().kind==LevelMapSelectionKind::DoomSector,
					"a click in the room makes its sector anew, selected",shell.statusBar()->currentMessage());
				shell.findChild<QAction*>(QStringLiteral("map.undo"))->trigger();
				ok &= expect(serializeLevelMap(shell.levelDocument()).bytes==room,"one undo restores the room exactly");
				QKeyEvent leave(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier); QCoreApplication::sendEvent(plan,&leave);
				ok &= expect(!plan->makeSectorMode() && !makeSector->isChecked(),"Escape leaves Make Sector Mode");
			}
		}
	}
	return ok?0:1;
}
