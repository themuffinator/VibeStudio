#include "app/application_shell.h"
#include "app/level_surface_tools.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_face_clipboard.h"
#include "core/map_preview_mesh.h"
#include "tests/level_type_selection_test_helpers.h"
#include "tests/level_material_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
QString phase;
bool expect(bool value,const char* label,const QString& error = {})
{
	if (!value) { ok = false; std::cerr << "FAIL: " << phase.toStdString() << ": " << label << ": " << error.toStdString() << std::endl; } return value;
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < 30000) { QEventLoop loop; QTimer::singleShot(15,&loop,&QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents); } return ready();
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*,const char* source,const char*,int) const override {
		const auto text = QString::fromUtf8(source); return QStringLiteral("[%1%2]").arg(text,QString(text.size()/3,'~'));
	}
};
QStringList componentIds(const QVector<LevelMapComponentSet>& sets)
{
	QStringList result;
	for (const auto& set : sets) { for (int id : set.components) { result << QStringLiteral("%1/%2/%3").arg(levelMapSelectionRefId(set.object)).arg(static_cast<int>(set.kind)).arg(id); } }
	result.sort(); return result;
}
}
int main(int argc,char** argv)
{
	// Owned offscreen Qt widgets, semantic events and render targets only.
	qputenv("QT_QPA_PLATFORM","offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR",QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc,argv);
#ifdef Q_OS_WIN
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini")); StudioSettings settings;
	settings.setRestoreSession(false); settings.setReducedMotion(true); settings.setLevelViewLayoutPreference("four-views");
	const auto source = tests::typeSelectionFixture(), path = temp.filePath("textured.map").toUtf8(); QFile file(QString::fromUtf8(path));
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(),"fixture")) { return 1; } file.close();
	const auto assets = temp.filePath("assets");
	if (!expect(tests::putMaterialFile(QDir(assets).filePath("textures/studio/checker.png"),tests::materialImage(128,64))
		&& tests::putMaterialFile(QDir(assets).filePath("textures/metal.png"),tests::materialImage(64,64)),"original fixture material dimensions")) { return 1; }
	for (const auto* profile : {"netradiant-custom","trenchbroom","vibestudio-default"}) {
		for (int scale : {100,200}) {
			std::cout << "Textured selection UI " << profile << ' ' << scale << '%' << std::endl;
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			settings.setSelectedEditorProfileId(profile); settings.setTextScalePercent(scale);
			settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark); settings.sync();
			applyStudioTheme(app,studioThemeTokens(settings.accessibilityPreferences().theme,UiDensity::Standard,scale));
			Expanded expanded; if (scale == 200) { app.installTranslator(&expanded); app.setLayoutDirection(Qt::RightToLeft); }
			{
				ApplicationShell shell; shell.resize(1600,1000); shell.show(); shell.openPathFromCommandLine(QString::fromUtf8(path));
				const auto action = [&](const char* id) { return shell.findChild<QAction*>(QLatin1String(id)); };
				action("shell.mode.levels")->trigger(); action("map.layout.four-views")->trigger();
				auto* view = shell.findChild<MapViewport*>("mapViewport"); auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
				if (!expect(view && camera && until([&] { return shell.levelDocument().sourcePath == QString::fromUtf8(path) && camera->hasMesh(); }),"map ready")) { return 1; }
				shell.openPathFromCommandLine(assets); action("shell.mode.levels")->trigger();
				expect(until([&] { return camera->isEnabled() && !camera->isRendering(); }),"package-backed camera ready");
				auto* query = action("map.selectTextured"); auto* tool = shell.findChild<QPushButton*>("levelToolsRow-map.selectTextured");
				auto* material = shell.findChild<QComboBox*>("levelPaintMaterial"); auto* target = shell.findChild<QComboBox*>("surfaceQuickTarget");
				auto* summary = shell.findChild<QLabel*>("surfaceQuickSummary");
				if (!expect(query && tool && material && target && summary,"surface query controls")) { return 1; }
				const bool native = QLatin1String(profile) == QLatin1String("netradiant-custom");
				expect(query->shortcuts() == (native ? QList<QKeySequence>{QKeySequence("Ctrl+Shift+A")} : QList<QKeySequence>{}),"NRC-only shortcut",query->shortcut().toString());
				expect(!query->toolTip().isEmpty() && !tool->accessibleName().isEmpty() && tool->focusPolicy() != Qt::NoFocus
					&& QAccessible::queryAccessibleInterface(tool),"accessible keyboard query control");
				const auto pick = [&](const QVector<LevelMapSelectionRef>& refs) { view->setSelectionSet(refs); view->selectionSetChanged(refs); view->setFocus(Qt::OtherFocusReason); };
				const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
				const auto wait = [&] { return until([&] { return !shell.levelSurfaceEditsPending(); }); };
				pick({}); const auto baseline = bytes(); const auto revision = shell.levelDocument().revision;
				material->setEditText("METAL"); tool->click();
				camera->setCameraView({-96,-120,88},50,-30);
				expect(until([&] { return camera->isEnabled() && !camera->isRendering(); }),"independent selection camera ready");
				expect(view->texturedSurfaceFaces() == QVector<LevelSurfaceFace>{{0,0}} && shell.levelDocument().selection.isEmpty()
					&& view->componentMode() == MapViewport::ComponentMode::Off && view->pickedComponentCount() == 0,"independent face without parent object or handles",shell.statusBar()->currentMessage());
				expect(camera->texturedSurfaceCount() == 1 && camera->highlightedTriangleCount() == 2,"camera independent face highlight");
				if (native && scale == 100) {
					const auto capture = qEnvironmentVariable("VIBESTUDIO_TEXTURED_SELECTION_CAPTURE");
					if (!capture.isEmpty()) {
						QImage plan(view->size(),QImage::Format_ARGB32_Premultiplied); view->render(&plan); expect(plan.save(capture+".plan.png"),"plan face outline capture");
						QImage shot(shell.size(),QImage::Format_ARGB32_Premultiplied); shell.render(&shot); expect(shot.save(capture+".normal.png"),"normal shell capture");
					}
				}
				expect(action("map.selectNone")->isEnabled() && action("map.copySurface")->isEnabled() && action("map.applyTexture")->isEnabled(),"faces alone enable surface actions and clear");
				expect(summary->accessibleDescription() == summary->text() && !summary->text().isEmpty(),"accessible target counts");
				for (auto* pane : shell.findChildren<MapViewport*>()) {
					expect(pane->texturedSurfaceFaces() == QVector<LevelSurfaceFace>{{0,0}} && pane->accessibleDescription().contains("textured"),"all plan panes expose independent picks");
				}
				query->trigger(); material->setEditText("missing"); query->trigger();
				expect(view->texturedSurfaceFaces().size() == 1 && shell.levelDocument().revision == revision && bytes() == baseline,"repeat and no-match preserve picks and content");
				action("map.copySurface")->trigger(); QVector<LevelMapBrushFace> copied; QString error;
				expect(parseLevelFaceText(QApplication::clipboard()->text(),&copied,&error) && copied.size() == 1,"copy independent face",error);
				const auto before = shell.levelDocument().brushes[0].faces;
				action("map.surfaceShiftRight")->trigger(); expect(wait(),"surface adjustment completes");
				expect(shell.levelDocument().brushes[0].faces[0].shiftX == before[0].shiftX+8
					&& shell.levelDocument().brushes[0].faces[1].shiftX == before[1].shiftX && view->texturedSurfaceFaces().size() == 1,"exact target edit retains independent identity");
				action("map.undo")->trigger(); expect(bytes() == baseline && view->texturedSurfaceFaces().size() == 1,"surface undo keeps face identity");
				action("map.surfaceShiftRight")->trigger(); action("map.selectNone")->trigger();
				expect(wait() && bytes() == baseline && view->texturedSurfaceFaces().isEmpty(),"clearing picks cancels pending surface work");
				material->setEditText("metal"); query->trigger();
				QKeyEvent clear(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier); QApplication::sendEvent(view,&clear);
				expect(view->texturedSurfaceFaces().isEmpty() && camera->texturedSurfaceCount() == 0,"plan Escape clears independent faces");
				// Each component mode retains exact existing handle identities while
				// adding faces from unselected brushes and whole patch objects.
				for (const auto* mode : {"map.vertexTool","map.edgeTool","map.faceTool"}) {
					phase = QStringLiteral("%1 %2% %3").arg(QLatin1String(profile)).arg(scale).arg(QLatin1String(mode));
					pick({{LevelMapSelectionKind::QuakeBrush,0}}); action(mode)->trigger();
					const auto kind = view->componentMode() == MapViewport::ComponentMode::Faces ? LevelBrushComponent::Face
						: view->componentMode() == MapViewport::ComponentMode::Edges ? LevelBrushComponent::Edge : LevelBrushComponent::Vertex;
					const QVector<LevelMapComponentSet> components{{{LevelMapSelectionKind::QuakeBrush,0},kind,{0}}};
					expect(view->setPickedComponents(components),"seed exact component"); view->componentPicksChanged(1);
					material->setEditText("studio/checker"); const auto componentMode = view->componentMode(); query->trigger();
					expect(view->componentMode() == componentMode && componentIds(view->pickedComponents()) == componentIds(components)
						&& camera->pickedComponents().size() == 1,"query preserves current component tool and picks",shell.statusBar()->currentMessage());
					expect(view->texturedSurfaceFaces().size() == 11 && shell.levelDocument().selection.size() == 4
						&& !shell.levelDocument().selection.contains({LevelMapSelectionKind::QuakeBrush,1}),"faces and whole patches have distinct selection kinds");
					for (auto* pane : shell.findChildren<MapViewport*>()) { expect(componentIds(pane->pickedComponents()) == componentIds(components),"all plan component picks preserved"); }
					expect(summary->text().contains("3") && summary->text().contains("patch"),"whole-patch surface targets visible in every component mode");
					const auto objects = shell.levelDocument().selection;
					const auto mapping = shell.levelDocument();
					action("map.surfaceShiftRight")->trigger(); expect(wait(),"component-mode surface edit completes");
					expect(shell.levelDocument().brushes[1].faces[0].shiftX == mapping.brushes[1].faces[0].shiftX+8
						&& shell.levelDocument().patches[0].controlU != mapping.patches[0].controlU
						&& componentIds(view->pickedComponents()) == componentIds(components),"query faces and whole patches are editable without changing component picks",shell.statusBar()->currentMessage());
					action("map.undo")->trigger(); expect(bytes() == baseline && view->texturedSurfaceFaces().size() == 11,"patch and face surface undo preserves query selection");
					expect(until([&] { return camera->isEnabled() && !camera->isRendering(); }),"camera ready after mapping undo");
					QKeyEvent cameraClear(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier); QApplication::sendEvent(camera,&cameraClear);
					expect(view->texturedSurfaceFaces().isEmpty() && view->pickedComponentCount() == 0 && view->componentMode() == componentMode
						&& shell.levelDocument().selection == objects,"camera Escape clears face and handle picks before objects");
					action("map.selectNone")->trigger(); expect(view->componentMode() == MapViewport::ComponentMode::Off && shell.levelDocument().selection == objects,"next clear leaves component mode");
					action("map.selectNone")->trigger(); expect(shell.levelDocument().selection.isEmpty(),"next clear removes objects");
				}
				if (native && scale == 100) {
					material->setEditText("metal"); bool cancelled = false;
					QTimer::singleShot(0,&shell,[&] { if (auto* dialog = shell.findChild<QDialog*>("levelPlacementTaskDialog")) { cancelled = true; dialog->reject(); } }); query->trigger();
					expect(cancelled && view->texturedSurfaceFaces().isEmpty() && shell.levelDocument().selection.isEmpty(),"cancel query is atomic");
					QTimer::singleShot(0,&shell,[&] { material->setEditText("studio/checker"); }); query->trigger();
					expect(view->texturedSurfaceFaces().isEmpty() && shell.levelDocument().selection.isEmpty() && shell.statusBar()->currentMessage().contains("changed"),"stale material rejects query");
					material->setEditText("metal");
					QTimer::singleShot(0,&shell,[&] { view->setFilteredObjects({{LevelMapSelectionKind::QuakeBrush,0}}); }); query->trigger();
					expect(view->texturedSurfaceFaces().isEmpty() && shell.levelDocument().selection.isEmpty(),"stale filter rejects query"); view->setFilteredObjects({});
					query->trigger(); pick({{LevelMapSelectionKind::QuakeBrush,0}}); view->moveRequested(8,0,0);
					expect(bytes() != baseline && view->texturedSurfaceFaces().isEmpty(),"geometry change retires independent face identities");
					action("map.undo")->trigger(); expect(bytes() == baseline && view->texturedSurfaceFaces().isEmpty(),"geometry undo does not revive stale face indices");
					pick({}); query->trigger(); shell.openPathFromCommandLine(QString::fromUtf8(path));
					expect(until([&] { return view->texturedSurfaceFaces().isEmpty(); }),"reloading same source clears independent picks");
				}
				expect(bytes() == baseline,"query and selection workflows leave source bytes unchanged");
				material->setEditText("metal"); query->trigger();
				if (native && scale == 200) {
					const auto capture = qEnvironmentVariable("VIBESTUDIO_TEXTURED_SELECTION_CAPTURE");
					expect(until([&] { return camera->isEnabled() && !camera->isRendering(); }),"expanded selection camera ready");
					if (!capture.isEmpty()) { QImage image(shell.size(),QImage::Format_ARGB32_Premultiplied); shell.render(&image); expect(image.save(capture),"owned Qt capture"); }
				}
			}
			if (scale == 200) { app.removeTranslator(&expanded); app.setLayoutDirection(Qt::LeftToRight); }
		}
	}
	// Capacity is checked before publication; existing exact picks survive a
	// rejected addition even when the matching patch occurs earlier in the map.
	{
		LevelMapDocument document; QString error;
		expect(loadLevelMapBytes({"capacity.map",{},"idtech3"},source,&document,&error),"capacity fixture",error);
		const auto prototype = document.patches.first(); document.patches.clear(); document.brushes.clear();
		QVector<LevelMapSelectionRef> selected;
		for (int id = 0; id < 129; ++id) { auto patch = prototype; patch.id = id; document.patches.append(patch); if (id > 0) { selected.append({LevelMapSelectionKind::QuakePatch,id}); } }
		MapViewport viewport; viewport.setDocument(document); viewport.setSelectionSet(selected); viewport.setComponentMode(MapViewport::ComponentMode::Vertices);
		expect(!viewport.componentHandlesLimited() && viewport.setPickedComponents({{{LevelMapSelectionKind::QuakePatch,128},LevelBrushComponent::Vertex,{0}}}),"full but untruncated component selection");
		const auto picks = componentIds(viewport.pickedComponents()); const auto handles = viewport.componentHandleCount();
		expect(!viewport.canAddComponentPatches({0}) && viewport.canAddComponentPatches({128})
			&& componentIds(viewport.pickedComponents()) == picks && viewport.componentHandleCount() == handles,"patch capacity preflight has no side effects");
	}
	return ok ? 0 : 1;
}
