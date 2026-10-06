#include "app/application_shell.h"
#include "app/model_viewport.h"
#include "app/level_dependency_dialog.h"
#include "app/studio_theme.h"
#include "tests/doom_preview_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDialog>
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
	}
	return ok?0:1;
}
