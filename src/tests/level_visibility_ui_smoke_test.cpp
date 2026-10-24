#include "app/application_shell.h"
#include "app/level_placement_task_dialog.h"
#include "app/level_placement_dialog.h"
#include "app/map_viewport.h"
#include "app/level_scene_panel.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "tests/level_geometry_test_helpers.h"
#include "core/level_scene.h"
#include "tests/level_object_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QComboBox>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value, const char* message, const QString& detail = {}) {
	if (!value) { ok = false; std::cerr << "FAIL: " << message << ": " << detail.toStdString() << std::endl; } return value;
}
bool until(const std::function<bool()>& ready) {
	QElapsedTimer time; time.start();
	while (!ready() && time.elapsed() < 30000) { QEventLoop loop; QTimer::singleShot(15,&loop,&QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents); }
	return ready();
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*,const char* text,const char*,int) const override {
		const auto source = QString::fromUtf8(text); return QStringLiteral("[%1%2]").arg(source,QString(source.size()/3,'~'));
	}
};
}
int main(int argc, char** argv)
{
	// Semantic calls into owned offscreen Qt widgets; no OS input or capture.
	qputenv("QT_QPA_PLATFORM","offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR",QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc,argv); QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini")); StudioSettings settings;
	settings.setRestoreSession(false); settings.setReducedMotion(true); settings.setLevelViewLayoutPreference("four-views");
	LevelMapDocument fixture; QString error; tests::createGeometryFixture(3,&fixture,&error);
	const auto path = temp.filePath("visibility.map"); const auto original = serializeLevelMap(fixture).bytes;
	QFile file(path); if (!file.open(QIODevice::WriteOnly) || file.write(original) != original.size()) { return 1; } file.close();
	auto grouped = fixture; QString group, empty, destination;
	expect(createLevelSceneNode(&grouped,LevelSceneNodeKind::Group,"Pair",{},&group,&error)
		&& createLevelSceneNode(&grouped,LevelSceneNodeKind::Group,"Empty",group,&empty,&error)
		&& assignLevelSceneObjects(&grouped,group,{"brush:0","brush:1"},&error)
		&& createLevelSceneNode(&grouped,LevelSceneNodeKind::Layer,"Destination",{},&destination,&error),"group fixture",error);
	const auto groupedBytes = serializeLevelMap(grouped).bytes, groupPath = temp.filePath("grouped.map").toUtf8();
	QFile groupFile(QString::fromUtf8(groupPath));
	if (!groupFile.open(QIODevice::WriteOnly) || groupFile.write(groupedBytes) != groupedBytes.size()) { return 1; } groupFile.close();
	const QVector<LevelMapSelectionRef> chosen{{LevelMapSelectionKind::QuakeBrush,0}}, other{{LevelMapSelectionKind::QuakeBrush,1}};
	for (const auto& profile : {QStringLiteral("trenchbroom"),QStringLiteral("netradiant-custom"),QStringLiteral("vibestudio-default")}) {
		for (int scale : {100,200}) {
			std::cout << "Visibility UI " << profile.toStdString() << ' ' << scale << '%' << std::endl;
			settings.setSelectedEditorProfileId(profile); settings.setTextScalePercent(scale);
			settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark); settings.sync();
			applyStudioTheme(app,studioThemeTokens(settings.accessibilityPreferences().theme,UiDensity::Standard,scale));
			Expanded expanded; if (scale == 200) { app.installTranslator(&expanded); app.setLayoutDirection(Qt::RightToLeft); }
			ApplicationShell shell; shell.resize(1600,1000); shell.show(); shell.openPathFromCommandLine(path);
			const auto action = [&](const char* id) { return shell.findChild<QAction*>(QLatin1String(id)); };
			action("shell.mode.levels")->trigger(); action("map.layout.four-views")->trigger();
			auto* top = shell.findChild<MapViewport*>("mapViewport"); auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
			if (!expect(top && until([&] { return shell.levelDocument().sourcePath == path && top->isEnabled(); }),"map ready")) { return 1; }
			const auto views = shell.findChildren<MapViewport*>();
			const auto pick = [&](const auto& refs) { top->setSelectionSet(refs); top->selectionSetChanged(refs); top->setFocus(Qt::OtherFocusReason); };
			const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
			const bool tb = profile == "trenchbroom";
			const bool fresh = profile == "netradiant-custom";
			expect(shell.levelDocument().insertionVisibility == (fresh ? LevelInsertionVisibility::FreshInherited : LevelInsertionVisibility::PreserveAndReveal),
				"loaded document receives profile insertion policy");
			expect(!action("map.showAll")->isEnabled() && !action("map.hideSelection")->isEnabled(),"empty visibility commands disabled");
			pick(chosen); const auto depth = shell.levelDocument().undoStack.size(); action("map.hideSelection")->trigger();
			expect(shell.levelDocument().hiddenObjects == QSet<QString>{"brush:0"} && shell.levelDocument().selection.isEmpty()
				&& shell.levelDocument().undoStack.size() == depth+(tb ? 1 : 0),"profile visibility history and deselection",shell.statusBar()->currentMessage());
			for (auto* view : views) { expect(view->isHidden(LevelMapSelectionKind::QuakeBrush,0) && view->selectionSet().isEmpty()
				&& view->displayDocument().brushes.size() == 2,"every plan shares document visibility"); }
			expect(camera && until([&] { return camera->hasMesh() && camera->mesh().triangleCount == 24; }),"camera omits hidden geometry");
			if (tb) {
				action("map.undo")->trigger(); expect(shell.levelDocument().hiddenObjects.isEmpty() && shell.levelDocument().selection == chosen,"undo hide restores selection");
				action("map.redo")->trigger(); expect(shell.levelDocument().hiddenObjects.contains("brush:0"),"redo hide");
			}
			auto* objects = shell.findChild<LevelObjectList*>("levelMapObjects");
			tests::setObjectCurrentRow(objects,objects->rowForSelector("brush:0"));
			expect(shell.levelDocument().hiddenObjects.isEmpty() && shell.levelDocument().selection == chosen,"object list reveals authoritative visibility");
			if (tb) { action("map.undo")->trigger(); expect(shell.levelDocument().hiddenObjects.contains("brush:0") && shell.levelDocument().selection.isEmpty(),"list reveal is one undo step"); }
			if (action("map.showAll")->isEnabled()) { action("map.showAll")->trigger(); }
			pick(chosen); action("map.isolateSelection")->trigger();
			expect(shell.levelDocument().hiddenObjects == QSet<QString>{"brush:1","brush:2"} && shell.levelDocument().selection == chosen,"isolation preserves kept selection");
			expect(camera && until([&] { return camera->isEnabled() && camera->hasMesh() && camera->mesh().triangleCount == 12; }),"camera publishes isolated geometry");
			expect(camera && until([&] { return !camera->isRendering(); }),"isolated camera render completes");
			for (auto* view : views) { expect(view->displayDocument().brushes.size() == 1 && view->selectionSet() == chosen,"isolation shared by every plan"); }
			if (tb) {
				const auto capture = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
				if (!capture.isEmpty()) { QDir().mkpath(capture); QImage image(shell.size(),QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); shell.render(&image);
					expect(image.save(QDir(capture).filePath(QStringLiteral("isolate-%1.png").arg(scale))),"render isolated shell"); }
			}
			action("map.showAll")->trigger(); expect(shell.levelDocument().hiddenObjects.isEmpty() && !action("map.showAll")->isEnabled(),"show all command updates enablement");
			pick(chosen);
			for (const auto* tool : {"map.vertexTool","map.edgeTool","map.faceTool","map.clipTool"}) {
				action(tool)->trigger(); expect(!action("map.hideSelection")->isEnabled() && !action("map.isolateSelection")->isEnabled(),"active tools protect drafts");
				action(tool)->trigger();
			}
			pick(chosen); bool cancelled = false;
			QTimer::singleShot(0,&shell,[&] { if (auto* dialog = shell.findChild<QDialog*>("levelPlacementTaskDialog")) { cancelled = true; dialog->reject(); } });
			action("map.isolateSelection")->trigger(); expect(cancelled && shell.levelDocument().hiddenObjects.isEmpty() && shell.levelDocument().selection == chosen,"cancel keeps all state");
			QTimer::singleShot(0,&shell,[&] { pick(other); }); action("map.isolateSelection")->trigger();
			expect(shell.levelDocument().hiddenObjects.isEmpty() && shell.levelDocument().selection == other && shell.statusBar()->currentMessage().contains("changed"),"stale selection cannot publish visibility");
			auto* profiles = shell.findChild<QComboBox*>("editorProfileCombo");
			if (!expect(profiles != nullptr,"profile control")) { return 1; }
			const auto beforePolicyChange = shell.levelDocument();
			QTimer::singleShot(0,&shell,[&] { profiles->setCurrentIndex(profiles->findData(fresh ? "trenchbroom" : "netradiant-custom")); });
			action("map.duplicateSelection")->trigger();
			expect(shell.levelDocument().brushes.size() == beforePolicyChange.brushes.size()
				&& shell.levelDocument().undoStack.size() == beforePolicyChange.undoStack.size()
				&& shell.levelDocument().historyRevision > beforePolicyChange.historyRevision && shell.statusBar()->currentMessage().contains("changed"),
				"profile change invalidates pending authoring without publishing old policy");
			profiles->setCurrentIndex(profiles->findData(profile));
			expect(bytes() == original && levelMapHistoryMatchesSaved(shell.levelDocument(),static_cast<int>(shell.levelDocument().undoStack.size())),"workflow preserves bytes and saved marker");
			shell.openPathFromCommandLine(QString::fromUtf8(groupPath));
			expect(until([&] { return shell.levelDocument().sourcePath == QString::fromUtf8(groupPath) && top->isEnabled(); }),"group map ready");
			pick(chosen); action("map.hideSelection")->trigger();
			const auto groupKey = QStringLiteral("group:")+group;
			expect(shell.levelDocument().hiddenObjects == (tb ? QSet<QString>{groupKey} : QSet<QString>{"brush:0","brush:1"}),
				"profile hides container or current members",shell.statusBar()->currentMessage());
			for (auto* view : views) { expect(view->displayDocument().brushes.size() == 1 && view->temporaryHiddenCount() == 2,"group hiding and counts reach all plan panes"); }
			expect(until([&] { return camera->hasMesh() && camera->mesh().triangleCount == 12; }),"group-hidden camera");
			auto* scene = shell.findChild<LevelScenePanel*>();
			if (!expect(scene != nullptr,"scene panel exists")) { return 1; }
			scene->contextRequested(group,false);
			expect(shell.levelDocument().openSceneGroups.contains(group),"open hidden group before selecting an individual child");
			tests::setObjectCurrentRow(objects,objects->rowForSelector("brush:0"));
			expect(!levelMapHiddenObjects(shell.levelDocument()).contains("brush:0") && levelMapHiddenObjects(shell.levelDocument()).contains("brush:1")
				&& (!tb || shell.levelDocument().shownObjects == QSet<QString>{"brush:0"}),"object list reveals child without siblings",shell.statusBar()->currentMessage());
			for (auto* view : views) { expect(!view->isHidden(LevelMapSelectionKind::QuakeBrush,0) && view->isHidden(LevelMapSelectionKind::QuakeBrush,1)
				&& view->displayDocument().brushes.size() == 2,"shown-only cache changes reach all plan panes"); }
			expect(until([&] { return camera->hasMesh() && camera->mesh().triangleCount == 24; }),"shown-only cache changes reach camera");
			if (tb) {
				action("map.undo")->trigger(); expect(shell.levelDocument().shownObjects.isEmpty() && shell.levelDocument().hiddenObjects.contains(groupKey),"undo shown child");
				action("map.redo")->trigger(); expect(shell.levelDocument().shownObjects.contains("brush:0"),"redo shown child");
				// Use the Scene panel's owned Qt controls, not OS input.
				auto* tree = scene->findChild<QTreeWidget*>("levelSceneTree");
				auto* parents = scene->findChild<QComboBox*>("levelSceneParent");
				auto* move = scene->findChild<QPushButton*>("levelSceneMove");
				QTreeWidgetItem* row = nullptr;
				for (QTreeWidgetItemIterator it(tree); *it; ++it) { if ((*it)->data(0,Qt::UserRole).toString() == group) { row = *it; break; } }
				if (!expect(row && parents && move,"scene transfer controls")) { return 1; }
				tree->setCurrentItem(row); parents->setCurrentIndex(parents->findData(destination));
				const auto transferDepth = shell.levelDocument().undoStack.size(); move->click();
				expect(shell.levelDocument().shownObjects.isEmpty() && shell.levelDocument().selection.isEmpty()
					&& shell.levelDocument().undoStack.size() == transferDepth+1,"scene move resets shown exception in same undo step");
				for (auto* view : views) { expect(view->isHidden(LevelMapSelectionKind::QuakeBrush,0) && view->displayDocument().brushes.size() == 1,"layer transfer updates every plan pane"); }
				expect(until([&] { return camera->hasMesh() && camera->mesh().triangleCount == 12; }),"layer transfer updates camera");
				action("map.undo")->trigger();
				expect(shell.levelDocument().shownObjects.contains("brush:0") && shell.levelDocument().selection == chosen,"transfer undo restores shown child and selection");
				expect(until([&] { return camera->hasMesh() && camera->mesh().triangleCount == 24; }),"transfer undo restores camera visibility");
			}
			action("map.showAll")->trigger(); scene->contextRequested({},true); pick(chosen); action("map.isolateSelection")->trigger();
			expect(!tb || shell.levelDocument().shownObjects.contains(QStringLiteral("group:")+empty),"isolation retains empty descendants");
			action("map.showAll")->trigger();
			expect(shell.levelDocument().hiddenObjects.isEmpty() && shell.levelDocument().shownObjects.isEmpty()
				&& !action("map.showAll")->isEnabled() && bytes() == groupedBytes,"group workflow leaves saved bytes intact");
			scene->contextRequested(group,false); pick(other); action("map.hideSelection")->trigger();
			scene->contextRequested({},true); pick(QVector<LevelMapSelectionRef>{chosen.first(),other.first()});
			if (!expect(shell.levelDocument().hiddenObjects == QSet<QString>{"brush:1"},"plan group selection preserves hidden descendants")) { return 1; }
			expect(until([&] { return camera->hasMesh() && camera->mesh().triangleCount == 24; }),"hidden child absent before camera group pick");
			camera->triangleClicked(0,0);
			if (!expect(shell.levelDocument().selection.size() == 2 && shell.levelDocument().hiddenObjects == QSet<QString>{"brush:1"},
				"camera group pick preserves hidden descendants")) { return 1; }
			action("map.duplicateSelection")->trigger();
			if (!expect(shell.levelDocument().brushes.size() == 5 && shell.levelDocument().hiddenObjects.size() == (fresh ? 1 : 2),
				"duplicate applies profile policy to hidden descendants",shell.statusBar()->currentMessage())) { return 1; }
			for (auto* view : views) { expect(view->displayDocument().brushes.size() == (fresh ? 4 : 3),"profile clone visibility reaches every plan"); }
			expect(until([&] { return camera->hasMesh() && camera->mesh().triangleCount == (fresh ? 48 : 36); }),"profile clone visibility reaches camera");
			const auto cloneCapture = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
			if (!cloneCapture.isEmpty()) {
				QImage image(shell.size(),QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); shell.render(&image);
				expect(image.save(QDir(cloneCapture).filePath(QStringLiteral("clone-%1-%2.png").arg(profile).arg(scale))),"render profile clone visibility");
			}
			action("map.undo")->trigger();
			expect(shell.levelDocument().brushes.size() == 3 && shell.levelDocument().hiddenObjects == QSet<QString>{"brush:1"},"profile clone exact visibility undo");
			if (fresh && scale == 100) {
				bool inspected = false;
				QTimer preview; preview.setInterval(10);
				QObject::connect(&preview,&QTimer::timeout,&shell,[&] {
					auto* dialog = dynamic_cast<LevelPlacementDialog*>(shell.findChild<QDialog*>("levelPlacementDialog")); if (!dialog || !dialog->isReady() || inspected) { return; }
					inspected = true;
					expect(dialog->previewValid() && dialog->previewDocument().brushes.size() == 5
						&& dialog->previewDocument().hiddenObjects == QSet<QString>{"brush:1"},"offset dialog uses the document insertion policy");
					dialog->accept();
				});
				preview.start(); QTimer::singleShot(45000,&shell,[&] { if (auto* dialog = shell.findChild<QDialog*>("levelPlacementDialog")) { dialog->reject(); } });
				action("map.duplicateWithOffset")->trigger(); preview.stop();
				expect(inspected && shell.levelDocument().brushes.size() == 5,"offset duplicate publishes fresh clone visibility");
				action("map.undo")->trigger();
			}
			action("map.showAll")->trigger();
			expect(bytes() == groupedBytes,"profile clone workflow preserves saved source after undo");
			if (scale == 200) { app.removeTranslator(&expanded); app.setLayoutDirection(Qt::LeftToRight); }
		}
	}
	// A forced-shown child must survive the viewport's entity filtering too.
	auto owned = fixture; setLevelMapSelection(&owned,{{LevelMapSelectionKind::QuakeBrush,0},{LevelMapSelectionKind::QuakeBrush,1}}); int owner = -1;
	expect(tieLevelMapSelectionToEntity(&owned,"func_detail",{},&owner,&error),"viewport owner fixture",error);
	LevelMapSelectionRef child;
	for (const auto& brush : owned.brushes) { if (brush.entityId == owner) { child = {LevelMapSelectionKind::QuakeBrush,brush.id}; break; } }
	owned.hiddenObjects = {levelMapSelectionRefId({LevelMapSelectionKind::Entity,owner})};
	expect(selectAndRevealLevelMapObjects(&owned,{child},false,&error),"reveal owned child",error);
	MapViewport standalone; standalone.setDocument(owned);
	expect(!standalone.isHidden(child.kind,child.objectId) && standalone.displayDocument().brushes.size() == 2,"shown child survives hidden owner culling");
	standalone.setFilteredObjects({{LevelMapSelectionKind::Entity,owner}});
	expect(standalone.isHidden(child.kind,child.objectId) && standalone.displayDocument().brushes.size() == 1,"explicit filters still mask shown descendants");
	LevelMapDocument dense; tests::createGeometryFixture(100000,&dense,&error); setLevelMapSelection(&dense,chosen);
	LevelPlacementRequest request; request.operation = LevelPlacementOperation::Visibility; request.visibility = {LevelVisibilityOperation::Isolate,true};
	for (int scale : {100,200}) {
		settings.setTextScalePercent(scale); settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark); settings.sync();
		applyStudioTheme(app,studioThemeTokens(settings.accessibilityPreferences().theme,UiDensity::Standard,scale));
		QElapsedTimer elapsed; elapsed.start(); qint64 last = 0, gap = 0; int beats = 0; bool inspected = false;
		QTimer pulse; pulse.setInterval(5); QObject::connect(&pulse,&QTimer::timeout,&app,[&] { const auto now = elapsed.elapsed(); gap = std::max(gap,now-last); last = now; ++beats; }); pulse.start();
		QTimer inspect; inspect.setInterval(10); QObject::connect(&inspect,&QTimer::timeout,&app,[&] {
			for (auto* widget : QApplication::topLevelWidgets()) {
				auto* dialog = dynamic_cast<LevelPlacementTaskDialog*>(widget); if (!dialog || inspected) { continue; }
				auto* phase = dialog->findChild<QLabel*>("levelPlacementTaskPhase"); auto* progress = dialog->findChild<QProgressBar*>("levelPlacementTaskProgress");
				if (!phase || !progress || !phase->text().contains(levelPlacementPhaseName(LevelPlacementPhase::Visibility))) { continue; }
				auto* cancel = dialog->findChild<QPushButton*>("levelPlacementTaskCancel"); inspected = true;
				expect(cancel && cancel->focusPolicy() != Qt::NoFocus && !cancel->accessibleDescription().isEmpty()
					&& QAccessible::queryAccessibleInterface(progress)->role() == QAccessible::ProgressBar,"accessible dense visibility progress"); cancel->click(); break;
			}
		}); inspect.start(); const auto result = LevelPlacementTaskDialog::prepare(nullptr,dense,request); inspect.stop(); pulse.stop();
		expect(inspected && beats > 0 && gap < 1000 && result.cancelled && !result.visibilityReport.changed && dense.hiddenObjects.isEmpty()
			&& dense.selection == chosen && dense.undoStack.isEmpty(),"dense cancellation stays responsive and atomic",result.error);
		std::cout << scale << "% visibility: " << beats << " heartbeats, maximum gap " << gap << " ms" << std::endl;
		const auto capture = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
		if (!capture.isEmpty()) { QDir().mkpath(capture); QFile evidence(QDir(capture).filePath(QStringLiteral("dense-visibility-%1.json").arg(scale)));
			const QJsonObject metrics{{"brushes",100000},{"scale",scale},{"heartbeats",beats},{"maximumGapMs",gap},{"progressInspected",inspected},{"cancelled",result.cancelled}};
			expect(evidence.open(QIODevice::WriteOnly) && evidence.write(QJsonDocument(metrics).toJson()) > 0,"record responsiveness"); }
	}
	return ok ? 0 : 1;
}
