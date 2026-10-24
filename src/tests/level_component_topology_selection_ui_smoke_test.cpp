#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "app/studio_sidebar.h"
#include "core/level_patch.h"
#include "tests/level_geometry_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QLayout>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTranslator>
#include <cstdlib>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value,const char* label,const QString& detail = {}) {
	if (!value) { ok = false; std::cerr << label << ": " << detail.toStdString() << '\n'; } return value;
}
bool until(const std::function<bool()>& ready) {
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < 20000) { QEventLoop loop; QTimer::singleShot(10,&loop,&QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents); }
	return ready();
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*,const char* source,const char*,int n) const override { return QStringLiteral("[%1 expanded wording]").arg(QString::fromUtf8(source).replace("%n",QString::number(n))); }
};
void shellChecks(const QString& path,const QString& evidence,int scale) {
	std::cerr << "UI scale " << scale << ": constructing shell\n";
	ApplicationShell shell; shell.resize(scale == 100 ? 1700 : 2600,scale == 100 ? 1100 : 1800); shell.show(); shell.openPathFromCommandLine(path);
	const auto trigger = [&](const char* id) { std::cerr << "Action " << id << '\n'; auto* action = shell.findChild<QAction*>(id); if (expect(action && action->isEnabled(),id)) { action->trigger(); } };
	trigger("shell.mode.levels");
	auto* plan = shell.findChild<MapViewport*>("mapViewport"); auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
	if (!expect(plan && camera && until([&] { return shell.levelDocument().brushes.size() == 2 && camera->hasMesh() && camera->isEnabled(); }),"shell ready")) { return; }
	const auto bytes = serializeLevelMap(shell.levelDocument()).bytes;
	trigger("map.selectAll"); const auto objects = shell.levelDocument().selection; trigger("map.faceTool");
	const auto selectionDepth = shell.levelDocument().undoStack.size();
	const auto pick = [&](LevelMapSelectionKind object,LevelBrushComponent kind,QVector<int> ids) {
		expect(plan->setPickedComponents({{{object,0},kind,ids}}),"exact source selection");
		// Exact-ID setters are also used during pane synchronisation and do
		// not emit recursively. Seed the normal input-pick notification here.
		plan->componentPicksChanged(plan->pickedComponentCount());
	};
	const auto count = [&](int expected) {
		expect(plan->pickedComponentCount() == expected && camera->pickedComponents().size() == expected,"plan/camera pick agreement",QString::number(plan->pickedComponentCount()));
		for (auto* view : shell.findChildren<MapViewport*>()) {
			if (view->componentMode() != MapViewport::ComponentMode::Off) { expect(view->componentMode() == plan->componentMode() && view->pickedComponentCount() == expected,"all plan panes share mode and IDs"); }
		}
	};
	pick(LevelMapSelectionKind::QuakeBrush,LevelBrushComponent::Face,{0});
	trigger("map.vertexTool"); count(4); trigger("map.edgeTool"); count(4); trigger("map.faceTool"); count(1);
	expect(!plan->pickedComponents().isEmpty() && plan->pickedComponents()[0].components == QVector<int>{0},"face conversion round trip keeps source identity");
	trigger("map.growComponents"); count(5); trigger("map.shrinkComponents"); count(1); trigger("map.connectedComponents"); count(6);
	trigger("map.vertexTool"); count(8); pick(LevelMapSelectionKind::QuakeBrush,LevelBrushComponent::Vertex,{0});
	trigger("map.componentTouchingEdges"); count(3); expect(plan->componentMode() == MapViewport::ComponentMode::Edges,"touching command changes shared mode");
	trigger("map.faceTool"); count(0); // Three corner edges cover no whole cube face.
	trigger("map.vertexTool"); pick(LevelMapSelectionKind::QuakeBrush,LevelBrushComponent::Vertex,{0});
	trigger("map.componentTouchingFaces"); count(3);
	trigger("map.vertexTool"); pick(LevelMapSelectionKind::QuakePatch,LevelBrushComponent::Vertex,{12});
	trigger("map.growComponents"); count(5); trigger("map.shrinkComponents"); count(1); trigger("map.connectedComponents"); count(25);
	trigger("map.edgeTool"); count(25); trigger("map.shrinkComponents"); count(9);
	expect(!plan->pickedComponents().isEmpty() && plan->pickedComponents()[0].kind == LevelBrushComponent::Vertex,"patch selection remains control points in edge mode");
	expect(shell.levelDocument().selection == objects && serializeLevelMap(shell.levelDocument()).bytes == bytes
		&& shell.levelDocument().undoStack.size() == selectionDepth && shell.levelDocument().editState != QStringLiteral("modified"),"selection tools retain object context and never create geometry history");
	const QStringList commands{"map.componentTouchingEdges","map.componentTouchingFaces","map.growComponents","map.shrinkComponents","map.connectedComponents"};
	for (const auto& id : commands) {
		auto* button = shell.findChild<QPushButton*>("levelToolsRow-"+id); auto* accessible = button ? QAccessible::queryAccessibleInterface(button) : nullptr;
		expect(button && button->focusPolicy() != Qt::NoFocus && accessible && accessible->role() == QAccessible::Button
			&& !accessible->text(QAccessible::Name).isEmpty(),"selection command has keyboard focus and accessible name",id);
	}
	if (!evidence.isEmpty()) {
		auto* panel = shell.findChild<QWidget*>("levelTools-component-selection");
		if (expect(panel != nullptr,"component selection tools panel")) {
			for (auto* section : shell.findChildren<SidebarSection*>()) { if (section->body() == panel) { section->setExpanded(true); } }
			panel->show();
			panel->ensurePolished(); panel->resize(std::max(360*scale/100,panel->minimumSizeHint().width()),panel->sizeHint().height()); panel->layout()->setGeometry(panel->rect());
			QImage image(panel->size(),QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); panel->render(&image);
			for (auto* button : panel->findChildren<QPushButton*>()) {
				expect(!button->isHidden() && panel->rect().contains(button->geometry()),"tool row fits rendered panel",
					QStringLiteral("%1: row %2,%3 %4x%5, panel %6x%7").arg(button->objectName()).arg(button->x()).arg(button->y()).arg(button->width()).arg(button->height()).arg(panel->width()).arg(panel->height()));
			}
			expect(image.save(QDir(evidence).filePath(QStringLiteral("tools-%1.png").arg(scale))),"owned selection tools render");
		}
	}
	trigger("map.vertexTool"); pick(LevelMapSelectionKind::QuakeBrush,LevelBrushComponent::Vertex,{0}); trigger("map.connectedComponents");
	trigger("map.componentMoveTool"); camera->setComponentXray(true); camera->setCameraView({-160,-180,120},50,-28); camera->setMoveGrid(8);
	expect(tests::settleModelViewport(*camera),"camera component state ready");
	auto handles = camera->componentMoveHandles();
	expect(camera->beginComponentMove(handles[2]) && camera->updateComponentMove(handles[2]+(handles[2]-handles[3])*.5),"pending component edit begins");
	camera->finishComponentMove(true); trigger("map.growComponents");
	expect(until([&] { return !camera->isMovingComponents(); }) && serializeLevelMap(shell.levelDocument()).bytes == bytes && shell.levelDocument().undoStack.size() == selectionDepth,"selection query cancels pending commit even if picks do not change");
	handles = camera->componentMoveHandles();
	expect(camera->beginComponentMove(handles[2]) && camera->updateComponentMove(handles[2]+(handles[2]-handles[3])*.5),"connected selection drives next edit");
	camera->finishComponentMove(true);
	expect(until([&] { return !camera->isMovingComponents(); }) && shell.levelDocument().undoStack.size() == selectionDepth + 1,"connected selection commits as one geometry transaction");
	trigger("map.undo"); expect(serializeLevelMap(shell.levelDocument()).bytes == bytes,"exact undo after connected selection transform");
}
}
int main(int argc,char** argv) {
	qputenv("QT_QPA_PLATFORM","offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR",QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc,argv); QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	QTimer watchdog; watchdog.setSingleShot(true);
	QObject::connect(&watchdog,&QTimer::timeout,&app,[] {
		std::cerr << "UI test watchdog: stalled phase; top-level widgets:\n";
		for (auto* widget : QApplication::topLevelWidgets()) { if (widget->isVisible()) { std::cerr << widget->metaObject()->className() << ": " << widget->windowTitle().toStdString() << '\n'; } }
		std::exit(1);
	});
	watchdog.start(90000);
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini")); StudioSettings settings;
	settings.setRestoreSession(false); settings.setReducedMotion(true); settings.setLevelViewLayoutPreference("four-views"); settings.sync();
	LevelMapDocument map; LevelMapPatch patch; QString error; LevelPatchCreateRequest request; request.rows = request.columns = 5;
	if (!expect(tests::createGeometryFixture(2,&map,&error) && createLevelPatch(request,&patch,&error) && addLevelMapPatch(&map,patch,nullptr,&error),"geometry fixture",error)) { return 1; }
	const auto path = temp.filePath("selection.map"); if (!expect(saveLevelMapAs(map,path).succeeded(),"saved fixture")) { return 1; }
	for (int scale : {100,200}) {
		Expanded expanded; settings.setSelectedEditorProfileId(scale == 100 ? "trenchbroom" : "netradiant-custom");
		settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark); settings.setTextScalePercent(scale); settings.sync();
		applyStudioTheme(app,studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark,UiDensity::Standard,scale));
		if (scale == 200) { app.installTranslator(&expanded); app.setLayoutDirection(Qt::RightToLeft); }
		shellChecks(path,qEnvironmentVariable("VIBESTUDIO_COMPONENT_UI_EVIDENCE"),scale);
		if (scale == 200) { app.removeTranslator(&expanded); app.setLayoutDirection(Qt::LeftToRight); }
	}
	LevelMapDocument dense; expect(tests::createGeometryFixture(129,&dense,&error),"bounded context fixture");
	MapViewport bounded; bounded.setDocument(dense); QVector<LevelMapSelectionRef> objects;
	for (const auto& brush : dense.brushes) { objects << LevelMapSelectionRef{LevelMapSelectionKind::QuakeBrush,brush.id}; }
	bounded.setSelectionSet(objects); bounded.setComponentMode(MapViewport::ComponentMode::Vertices);
	expect(bounded.componentHandlesLimited() && bounded.setPickedComponentHandles({0}),"limited viewport still retains explicit picks");
	QVector<LevelMapComponentSet> output;
	expect(!bounded.queryComponentSelection({LevelComponentSelectionOperation::Grow},&output,&error)
		&& bounded.pickedComponentHandles() == QSet<int>{0} && bounded.componentMode() == MapViewport::ComponentMode::Vertices,"limited topology operation refuses without losing context");
	expect(!bounded.setPickedComponentHandles({99999}) && bounded.pickedComponentHandles() == QSet<int>{0},"stale exact handle set is atomic");
	bounded.clearDocument();
	expect(bounded.componentMode() == MapViewport::ComponentMode::Off && bounded.componentHandleCount() == 0 && bounded.pickedComponentCount() == 0
		&& !bounded.queryComponentSelection({LevelComponentSelectionOperation::Grow},&output,&error),"closing a map releases all component state and rejects stale queries");
	bounded.setDocument(map); bounded.setSelectionSet({{LevelMapSelectionKind::QuakeBrush,0}}); bounded.setComponentMode(MapViewport::ComponentMode::Faces); bounded.pickAllComponents();
	bounded.setDocument(dense);
	expect(bounded.componentMode() == MapViewport::ComponentMode::Off && bounded.componentHandleCount() == 0
		&& !bounded.queryComponentSelection({},&output,&error),"opening a different map cannot reuse previous topology picks");
	return ok ? 0 : 1;
}
