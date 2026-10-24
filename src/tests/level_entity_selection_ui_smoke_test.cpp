#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_object_selection.h"
#include "tests/level_entity_selection_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value, const char* label, const QString& error = {})
{
	if (!value) { ok = false; std::cerr << "FAIL: " << label << ": " << error.toStdString() << std::endl; }
	return value;
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < 30000) { QEventLoop loop; QTimer::singleShot(15,&loop,&QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents); }
	return ready();
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*,const char* source,const char*,int) const override {
		const auto text = QString::fromUtf8(source); return QStringLiteral("[%1%2]").arg(text,QString(text.size()/3,'~'));
	}
};
}
int main(int argc, char** argv)
{
	// Only owned offscreen Qt methods and widget render targets. No native input.
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
	const auto original = tests::entitySelectionFixture(); const auto path = temp.filePath("connected.map"); QFile file(path);
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(original) == original.size(),"write map")) { return 1; } file.close();
	const QVector<LevelMapSelectionRef> seed{{LevelMapSelectionKind::QuakePatch,2}};
	for (const auto* profile : {"trenchbroom","netradiant-custom","vibestudio-default"}) {
		for (int scale : {100,200}) {
			std::cout << "Connected selection UI " << profile << ' ' << scale << '%' << std::endl;
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
				auto* connected = action("map.selectConnectedEntities"); auto* siblings = action("map.selectSiblings");
				auto* tool = shell.findChild<QPushButton*>("levelToolsRow-map.selectConnectedEntities");
				if (!expect(connected && siblings && tool,"registered action and Tools route")) { return 1; }
				expect(!connected->text().isEmpty() && !connected->toolTip().isEmpty() && !tool->accessibleName().isEmpty()
					&& tool->focusPolicy() != Qt::NoFocus && QAccessible::queryAccessibleInterface(tool),"accessible keyboard-focusable selection control");
				const bool tb = QLatin1String(profile) == QLatin1String("trenchbroom"), native = QLatin1String(profile) == QLatin1String("netradiant-custom");
				expect(connected->shortcuts() == (native ? QList<QKeySequence>{QKeySequence("Ctrl+Shift+E")} : QList<QKeySequence>{}),"native shortcut only in NRC");
				if (native) { expect(connected->shortcutContext() == Qt::WidgetWithChildrenShortcut,"connected shortcut scoped to Levels"); }
				if (tb) { expect(action("map.selectCompleteTall")->shortcuts().contains(QKeySequence("Ctrl+Shift+E")),"TB retains complete-tall shortcut"); }
				const auto pick = [&](const QVector<LevelMapSelectionRef>& refs) { view->setSelectionSet(refs); view->selectionSetChanged(refs); view->setFocus(Qt::OtherFocusReason); };
				const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
				pick({}); expect(!connected->isEnabled() && !tool->isEnabled(),"empty selection disables routes");
				pick(seed); const auto depth = shell.levelDocument().undoStack.size(); tool->click();
				if (!expect(shell.levelDocument().selection == tests::connectedEntityFixtureResult() && bytes() == original
					&& shell.levelDocument().undoStack.size() == depth+(tb ? 1 : 0),"Tools selection is additive, one hop and follows history policy",shell.statusBar()->currentMessage())) { return 1; }
				if (tb) { action("map.undo")->trigger(); expect(shell.levelDocument().selection == seed,"selection undo restores seed"); action("map.redo")->trigger(); }
				connected->trigger();
				expect(shell.levelDocument().selection.contains({LevelMapSelectionKind::Entity,4}) && !shell.levelDocument().selection.contains({LevelMapSelectionKind::Entity,5}),"second action adds next hop but ignores killtarget");
				connected->trigger(); const auto settled = shell.levelDocument().selection; const auto settledDepth = shell.levelDocument().undoStack.size();
				connected->trigger(); expect(shell.levelDocument().selection == settled && shell.levelDocument().undoStack.size() == settledDepth,"settled query has no duplicate picks or history");
				for (const auto* mode : {"map.vertexTool","map.edgeTool","map.faceTool"}) {
					pick({{LevelMapSelectionKind::QuakeBrush,0}}); action(mode)->trigger();
					const auto kind = QLatin1String(mode) == QLatin1String("map.vertexTool") ? LevelBrushComponent::Vertex
						: QLatin1String(mode) == QLatin1String("map.edgeTool") ? LevelBrushComponent::Edge : LevelBrushComponent::Face;
					const auto component = [&](int id) {
						expect(view->setPickedComponents({{{LevelMapSelectionKind::QuakeBrush,0},kind,{id}}}),"seed exact component identity");
						view->componentPicksChanged(view->pickedComponentCount());
					};
					component(0);
					expect(connected->isEnabled() && !siblings->isEnabled(),"NRC command permits component mode while sibling selection does not",QLatin1String(mode));
					auto expected = shell.levelDocument(); LevelObjectSelectionRequest request; request.mode = LevelObjectSelectionMode::ConnectedEntities;
					QString error; expect(selectLevelMapObjects(&expected,request,nullptr,&error),"expected component-mode picks",error);
					connected->trigger(); expect(shell.levelDocument().selection == expected.selection,"component-mode action publishes connected objects",shell.statusBar()->currentMessage());
					for (auto* pane : shell.findChildren<MapViewport*>()) {
						if (pane->componentMode() == MapViewport::ComponentMode::Off) { continue; }
						const auto picks = pane->pickedComponents();
						expect(picks.size() == 1 && picks.first().object == LevelMapSelectionRef{LevelMapSelectionKind::QuakeBrush,0}
							&& picks.first().kind == kind && picks.first().components == QVector<int>{0},"connected selection preserves component identities in every plan");
					}
					expect(camera->pickedComponents().size() == 1,"camera retains component pick");
					if (native && scale == 100) {
						QTimer::singleShot(0,&shell,[&] { component(1); }); connected->trigger();
						expect(shell.levelDocument().selection == expected.selection && shell.statusBar()->currentMessage().contains("changed"),"worker rejects changed component picks",shell.statusBar()->currentMessage());
					}
					action(mode)->trigger();
				}
				pick({{LevelMapSelectionKind::QuakeBrush,0}}); action("map.clipTool")->trigger();
				expect(!connected->isEnabled(),"active clipper blocks selection replacement"); action("map.clipTool")->trigger();
				pick(seed); view->setFilteredObjects({{LevelMapSelectionKind::Entity,1}}); connected->trigger();
				expect(shell.levelDocument().selection == QVector<LevelMapSelectionRef>{seed.first(),{LevelMapSelectionKind::Entity,3},{LevelMapSelectionKind::Entity,6}},"viewport filter excludes owner and descendants",shell.statusBar()->currentMessage());
				view->setFilteredObjects({});
				if (scale == 100) {
					pick(seed); const auto before = shell.levelDocument().undoStack.size(); bool cancelled = false;
					QTimer::singleShot(0,&shell,[&] { if (auto* dialog = shell.findChild<QDialog*>("levelPlacementTaskDialog")) { cancelled = true; dialog->reject(); } });
					connected->trigger(); expect(cancelled && shell.levelDocument().selection == seed && shell.levelDocument().undoStack.size() == before,"cancel worker preserves picks and history");
					const QVector<LevelMapSelectionRef> replacement{{LevelMapSelectionKind::Entity,5}};
					QTimer::singleShot(0,&shell,[&] { pick(replacement); }); connected->trigger();
					expect(shell.levelDocument().selection == replacement && shell.statusBar()->currentMessage().contains("changed"),"worker rejects stale selection",shell.statusBar()->currentMessage());
					pick(seed); QTimer::singleShot(0,&shell,[&] { view->setFilteredObjects({{LevelMapSelectionKind::Entity,1}}); }); connected->trigger();
					expect(shell.levelDocument().selection == seed && shell.statusBar()->currentMessage().contains("changed"),"worker rejects changed filter",shell.statusBar()->currentMessage()); view->setFilteredObjects({});
					auto* profiles = shell.findChild<QComboBox*>("editorProfileCombo"); if (!expect(profiles != nullptr,"profile selector")) { return 1; }
					pick(seed); QTimer::singleShot(0,&shell,[&] { profiles->setCurrentIndex(profiles->findData(tb ? "netradiant-custom" : "trenchbroom")); }); connected->trigger();
					expect(shell.levelDocument().selection == seed && shell.statusBar()->currentMessage().contains("changed"),"worker rejects changed selection-history policy",shell.statusBar()->currentMessage());
					profiles->setCurrentIndex(profiles->findData(profile));
				}
				expect(bytes() == original && levelMapHistoryMatchesSaved(shell.levelDocument(),static_cast<int>(shell.levelDocument().undoStack.size())),"entire selection workflow preserves native content and saved state");
				if (native && scale == 200) {
					const auto capture = qEnvironmentVariable("VIBESTUDIO_CONNECTED_SELECTION_CAPTURE");
					if (!capture.isEmpty()) { QImage image(shell.size(),QImage::Format_ARGB32_Premultiplied); shell.render(&image); expect(image.save(capture),"owned Qt render evidence"); }
				}
			}
			if (scale == 200) { app.removeTranslator(&expanded); app.setLayoutDirection(Qt::LeftToRight); }
		}
	}
	return ok ? 0 : 1;
}
