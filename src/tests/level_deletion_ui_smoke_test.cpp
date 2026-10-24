#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_brush.h"
#include "tests/level_prefab_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QKeyEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <algorithm>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
QString phase;
bool expect(bool value, const char* label, const QString& error = {}) {
	if (!value) { ok = false; std::cerr << phase.toStdString() << ": " << label << ": " << error.toStdString() << std::endl; } return value;
}
bool until(const std::function<bool()>& ready) {
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < 30000) { QEventLoop loop; QTimer::singleShot(15,&loop,&QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents); } return ready();
}
QStringList ids(const QVector<LevelMapComponentSet>& sets) {
	QStringList result;
	for (const auto& set : sets) { for (int id : set.components) { result << QStringLiteral("%1/%2/%3").arg(levelMapSelectionRefId(set.object)).arg(static_cast<int>(set.kind)).arg(id); } }
	result.sort(); return result;
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* text, const char*, int) const override {
		const auto source = QString::fromUtf8(text); return QStringLiteral("[%1%2]").arg(source,QString(source.size()/3,'~'));
	}
};
}
int main(int argc, char** argv)
{
	// Owned offscreen Qt widgets and semantic events only; no system input.
	qputenv("QT_QPA_PLATFORM","offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR",QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc,argv);
	QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini")); StudioSettings settings;
	settings.setRestoreSession(false); settings.setReducedMotion(true); settings.setLevelViewLayoutPreference("four-views");
	LevelMapDocument fixture; QString error;
	if (!expect(tests::loadPrefabFixture(&fixture,&error),"fixture",error)) { return 1; }
	// Peak one door brush; the other stays a box for whole-object and invalid-hull checks.
	const auto first = std::find_if(fixture.brushes.cbegin(),fixture.brushes.cend(),[](const auto& brush) { return brush.id == 0; });
	if (!expect(first != fixture.brushes.cend(),"fixture brush 0")) { return 1; }
	auto peaked = *first;
	if (!expect(insertLevelBrushVertices(&peaked,{{16,32,112,true}},0,false,nullptr,&error)
		&& replaceLevelMapBrushGeometry(&fixture,peaked.id,peaked,&error),"valid face-removal source",error)) { return 1; }
	const auto path = temp.filePath("delete.map"); const auto source = serializeLevelMap(fixture).bytes;
	QFile file(path); if (!file.open(QIODevice::WriteOnly) || file.write(source) != source.size()) { return 1; } file.close();
	for (const auto* profile : {"netradiant-custom","trenchbroom","vibestudio"}) {
		for (int scale : {100,200}) {
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale); std::cout << phase.toStdString() << std::endl;
			settings.setSelectedEditorProfileId(profile); settings.setTextScalePercent(scale);
			settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark); settings.sync();
			applyStudioTheme(app,studioThemeTokens(settings.accessibilityPreferences().theme,UiDensity::Standard,scale));
			Expanded expanded; if (scale == 200) { app.installTranslator(&expanded); app.setLayoutDirection(Qt::RightToLeft); }
			{
				ApplicationShell shell; shell.resize(1600,1000); shell.show(); shell.openPathFromCommandLine(path);
				const auto action = [&](const char* id) { return shell.findChild<QAction*>(QLatin1String(id)); };
				action("shell.mode.levels")->trigger(); action("map.layout.four-views")->trigger();
				auto* view = shell.findChild<MapViewport*>("mapViewport"); auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
				if (!expect(view && camera && until([&] { return shell.levelDocument().sourcePath == path && camera->hasMesh(); }),"map ready")) { return 1; }
				const bool nrc = QLatin1String(profile) == QLatin1String("netradiant-custom"), tb = QLatin1String(profile) == QLatin1String("trenchbroom");
				auto* deletion = action("map.deleteSelection"); auto* removal = action("map.removeComponents");
				expect(deletion && removal && !deletion->toolTip().isEmpty(),"discoverable commands");
				if (nrc) { expect(deletion->shortcuts() == QList<QKeySequence>{QKeySequence("Del"),QKeySequence("Backspace"),QKeySequence("Z")},"all NRC delete aliases"); }
				// Saving writes a component-edited brush after its unedited sibling, so
				// find the peaked brush by shape rather than by its fixture id.
				int peaked = -1;
				for (const auto& brush : shell.levelDocument().brushes) {
					LevelBrushTopology shape;
					if (levelBrushTopology(brush,&shape,nullptr) && std::any_of(shape.faces.cbegin(),shape.faces.cend(),[](const auto& face) { return face.size() == 3; })) { peaked = brush.id; }
				}
				if (!expect(peaked == 0 || peaked == 1,"peaked door brush")) { return 1; }
				const int box = 1-peaked;
				const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
				const auto pick = [&](const QVector<LevelMapSelectionRef>& refs) { view->setSelectionSet(refs); view->selectionSetChanged(refs); view->setFocus(Qt::OtherFocusReason); };
				const auto choose = [&](const QVector<LevelMapComponentSet>& sets) { expect(view->setPickedComponents(sets),"exact component picks"); view->componentPicksChanged(view->pickedComponentCount()); };
				for (const auto* mode : {"map.vertexTool","map.edgeTool","map.faceTool"}) {
					pick({{LevelMapSelectionKind::QuakeBrush,box}}); action(mode)->trigger(); choose({});
					const auto before = bytes(); const auto count = shell.levelDocument().brushes.size();
					expect(deletion->isEnabled() == nrc && !removal->isEnabled(),"empty component selection follows profile"); deletion->trigger();
					if (nrc) { expect(shell.levelDocument().brushes.size() == count-1,"empty NRC component tool deletes objects"); action("map.undo")->trigger(); }
					expect(bytes() == before && view->componentMode() != MapViewport::ComponentMode::Off,"empty fallback preserves tool and exact undo");
					pick({{LevelMapSelectionKind::QuakeBrush,peaked}});
					const auto kind = view->componentMode() == MapViewport::ComponentMode::Faces ? LevelBrushComponent::Face
						: view->componentMode() == MapViewport::ComponentMode::Edges ? LevelBrushComponent::Edge : LevelBrushComponent::Vertex;
					const auto& brushes = shell.levelDocument().brushes;
					const auto target = std::find_if(brushes.cbegin(),brushes.cend(),[peaked](const auto& brush) { return brush.id == peaked; });
					LevelBrushTopology topology;
					if (!expect(target != brushes.cend() && levelBrushTopology(*target,&topology,&error),"picked brush topology",error)) { return 1; }
					int id = 0;
					if (kind == LevelBrushComponent::Face) {
						const auto triangle = std::find_if(topology.faces.cbegin(),topology.faces.cend(),[](const auto& face) { return face.size() == 3; });
						if (!expect(triangle != topology.faces.cend(),"triangular face exists")) { return 1; } id = static_cast<int>(triangle-topology.faces.cbegin());
					}
					const QVector<LevelMapComponentSet> sets{{{LevelMapSelectionKind::QuakeBrush,peaked},kind,{id}}}; choose(sets);
					const auto depth = shell.levelDocument().undoStack.size(); const auto baseline = bytes();
					expect(deletion->isEnabled() && removal->isEnabled(),"picked brush enables deletion"); deletion->trigger();
					expect(bytes() != baseline && shell.levelDocument().brushes.size() == count && shell.levelDocument().undoStack.size() == depth+1,"component deletion is one geometry step",shell.statusBar()->currentMessage());
					for (auto* pane : shell.findChildren<MapViewport*>()) { expect(pane->pickedComponentCount() == 0,"removed handle IDs cleared in every plan"); }
					expect(camera->pickedComponents().isEmpty(),"removed camera handles cleared");
					const auto after = bytes(); action("map.undo")->trigger(); expect(bytes() == baseline,"component undo exact");
					action("map.redo")->trigger(); expect(bytes() == after,"component redo exact"); action("map.undo")->trigger();
					// Patch picks never become an object fallback, including NRC.
					pick({{LevelMapSelectionKind::QuakePatch,0}});
					const QVector<LevelMapComponentSet> patch{{{LevelMapSelectionKind::QuakePatch,0},LevelBrushComponent::Vertex,{0,4}}}; choose(patch);
					expect(!deletion->isEnabled() && !removal->isEnabled(),"patch-only deletion disabled"); deletion->trigger();
					expect(bytes() == baseline && ids(view->pickedComponents()) == ids(patch),"patch-only selection preserved");
					pick({{LevelMapSelectionKind::QuakeBrush,peaked},{LevelMapSelectionKind::QuakePatch,0}}); auto mixed = sets; mixed += patch; choose(mixed);
					expect(deletion->isEnabled() == nrc,"mixed selection follows profile"); deletion->trigger();
					if (nrc) {
						expect(bytes() != baseline && ids(view->pickedComponents()) == ids(patch) && camera->pickedComponents().size() == 2,"NRC retains exact patch picks",shell.statusBar()->currentMessage());
						for (auto* pane : shell.findChildren<MapViewport*>()) { expect(ids(pane->pickedComponents()) == ids(patch),"patch picks shared across plans"); }
						action("map.undo")->trigger();
					}
					expect(bytes() == baseline,"mixed selection undo or refusal exact"); action(mode)->trigger();
				}
				// Removing a box face would flatten the solid; refusal must retain picks.
				pick({{LevelMapSelectionKind::QuakeBrush,box}}); action("map.faceTool")->trigger();
				const QVector<LevelMapComponentSet> invalid{{{LevelMapSelectionKind::QuakeBrush,box},LevelBrushComponent::Face,{0}}}; choose(invalid);
				const auto baseline = bytes(); const auto history = shell.levelDocument().historyRevision; deletion->trigger();
				expect(bytes() == baseline && history == shell.levelDocument().historyRevision && ids(view->pickedComponents()) == ids(invalid),"invalid hull retains geometry, history and picks");
				action("map.faceTool")->trigger(); pick({{LevelMapSelectionKind::QuakeBrush,peaked}}); action("map.vertexTool")->trigger();
				const QVector<LevelMapComponentSet> vertex{{{LevelMapSelectionKind::QuakeBrush,peaked},LevelBrushComponent::Vertex,{0}}}; choose(vertex);
				bool cancelled = false;
				QTimer::singleShot(0,&shell,[&] {
					auto* dialog = shell.findChild<QDialog*>("levelPlacementTaskDialog");
					if (!expect(dialog != nullptr,"background preparation dialog")) { return; }
					auto* cancel = dialog->findChild<QPushButton*>("levelPlacementTaskCancel"); auto* progress = dialog->findChild<QProgressBar*>("levelPlacementTaskProgress");
					expect(cancel && progress && !cancel->accessibleDescription().isEmpty() && QAccessible::queryAccessibleInterface(progress)->role() == QAccessible::ProgressBar,"accessible cancellation and progress");
					const auto capture = qEnvironmentVariable("VIBESTUDIO_DELETION_CAPTURE_DIR");
					if (!capture.isEmpty() && nrc) { dialog->open(); QImage image(dialog->size(),QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); dialog->render(&image); expect(image.save(QDir(capture).filePath(QStringLiteral("delete-%1.png").arg(scale))),"dialog capture"); }
					cancelled = true; dialog->reject();
				}); deletion->trigger();
				expect(cancelled && bytes() == baseline && ids(view->pickedComponents()) == ids(vertex),"cancel retains document and exact picks");
				QTimer::singleShot(0,&shell,[&] { choose({{{LevelMapSelectionKind::QuakeBrush,peaked},LevelBrushComponent::Vertex,{1}}}); }); deletion->trigger();
				expect(bytes() == baseline && ids(view->pickedComponents()) == ids({{{LevelMapSelectionKind::QuakeBrush,peaked},LevelBrushComponent::Vertex,{1}}}),"stale component candidate refused",shell.statusBar()->currentMessage());
				action("map.vertexTool")->trigger();
				pick({{LevelMapSelectionKind::QuakeBrush,peaked}}); action("map.clipTool")->trigger();
				LevelClipState state; state.points = {{8,0,0,true},{8,64,0,true}}; view->setClipState(state);
				const auto clipHistory = shell.levelDocument().historyRevision;
				expect(deletion->isEnabled() == tb,"clip command priority follows profile");
				if (tb) {
					deletion->trigger(); expect(view->clipState().points.size() == 1 && camera->clipState().points.size() == 1,"shared Delete removes last clip point");
					deletion->trigger(); expect(view->clipState().points.isEmpty() && !deletion->isEnabled(),"empty clip has no object fallback");
					for (auto* pane : shell.findChildren<MapViewport*>()) { expect(pane->clipState().points.isEmpty(),"clip points synchronised"); }
				}
				expect(bytes() == baseline && shell.levelDocument().historyRevision == clipHistory,"clip point deletion leaves map history untouched"); action("map.clipTool")->trigger();
				if (tb) {
					QTimer::singleShot(0,&shell,[&] { action("map.clipTool")->trigger(); view->setClipState(state); }); deletion->trigger();
					expect(bytes() == baseline && view->clipMode() && view->clipState().points.size() == 2,"starting clipper invalidates prepared object deletion",shell.statusBar()->currentMessage());
					action("map.clipTool")->trigger();
				}
				auto* profiles = shell.findChild<QComboBox*>("editorProfileCombo");
				if (!expect(profiles != nullptr,"profile control")) { return 1; }
				const int oldProfile = profiles->currentIndex(), nextProfile = profiles->findData(nrc ? "trenchbroom" : "netradiant-custom");
				QTimer::singleShot(0,&shell,[&] { profiles->setCurrentIndex(nextProfile); }); deletion->trigger();
				expect(bytes() == baseline && profiles->currentIndex() == nextProfile,"profile switch invalidates prepared deletion",shell.statusBar()->currentMessage());
				profiles->setCurrentIndex(oldProfile); action("map.layout.four-views")->trigger();
				// Pending publication cannot replace a newly loaded map.
				pick({{LevelMapSelectionKind::QuakeBrush,peaked}});
				QTimer::singleShot(0,&shell,[&] { LevelMapCreateRequest create; create.starterRoom = false; expect(shell.createLevelDocument(create,&error),"replace source during deletion",error); });
				deletion->trigger(); expect(shell.levelDocument().brushes.isEmpty() && shell.levelDocument().undoStack.isEmpty(),"stale object deletion cannot replace new document",shell.statusBar()->currentMessage());
				expect(shell.saveLevelDocument(temp.filePath(QStringLiteral("empty-%1-%2.map").arg(profile).arg(scale)),false,&error),"save test document",error); shell.close();
			}
			if (scale == 200) { app.removeTranslator(&expanded); app.setLayoutDirection(Qt::LeftToRight); }
		}
	}
	QEventLoop drain; QTimer::singleShot(200,&drain,&QEventLoop::quit); drain.exec(); return ok ? 0 : 1;
}
