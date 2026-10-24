#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "tests/level_type_selection_test_helpers.h"
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
#include <QListWidget>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value,const char* label,const QString& error = {})
{
	if (!value) { ok = false; std::cerr << "FAIL: " << label << ": " << error.toStdString() << std::endl; } return value;
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
}
int main(int argc,char** argv)
{
	// Owned offscreen widgets only: no operating-system input or capture.
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
	const auto source = tests::typeSelectionFixture(); const auto path = temp.filePath("types.map"); QFile file(path);
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(),"UI fixture")) { return 1; } file.close();
	const QVector<LevelMapSelectionRef> seed{{LevelMapSelectionKind::QuakePatch,1}};
	for (const auto* profile : {"trenchbroom","netradiant-custom","vibestudio-default"}) {
		for (int scale : {100,200}) {
			std::cout << "Type selection UI " << profile << ' ' << scale << '%' << std::endl;
			QElapsedTimer phase; phase.start();
			settings.setSelectedEditorProfileId(profile); settings.setTextScalePercent(scale);
			settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark); settings.sync();
			applyStudioTheme(app,studioThemeTokens(settings.accessibilityPreferences().theme,UiDensity::Standard,scale));
			Expanded expanded; if (scale == 200) { app.installTranslator(&expanded); app.setLayoutDirection(Qt::RightToLeft); }
			{
				const auto savedSheet = app.styleSheet();
				const auto savedFont = app.font();
				ApplicationShell shell;
				expect(!savedSheet.isEmpty() && app.styleSheet() == savedSheet && app.font() == savedFont,
					"page construction preserves application theme and scaled font");
				shell.resize(1600,1000); shell.show(); shell.openPathFromCommandLine(path);
				const auto action = [&](const char* id) { return shell.findChild<QAction*>(QLatin1String(id)); };
				action("shell.mode.levels")->trigger(); action("map.layout.four-views")->trigger();
				auto* view = shell.findChild<MapViewport*>("mapViewport"); auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
				if (!expect(view && camera && until([&] { return shell.levelDocument().sourcePath == path && camera->hasMesh(); }),"map ready")) { return 1; }
				std::cout << "Map ready: " << phase.elapsed() << " ms" << std::endl;
				auto* type = action("map.selectAllOfType"); auto* legacy = action("map.selectSimilar");
				auto* tool = shell.findChild<QPushButton*>("levelToolsRow-map.selectAllOfType");
				auto* material = shell.findChild<QComboBox*>("levelPaintMaterial"); auto* textures = shell.findChild<QListWidget*>("levelMapTextures");
				if (!expect(type && legacy && tool && material && textures,"command, tools and material routes")) { return 1; }
				expect(!type->toolTip().isEmpty() && !tool->accessibleName().isEmpty() && tool->focusPolicy() != Qt::NoFocus
					&& QAccessible::queryAccessibleInterface(tool),"accessible focusable type action");
				const bool tb = QLatin1String(profile) == QLatin1String("trenchbroom"), native = QLatin1String(profile) == QLatin1String("netradiant-custom");
				expect(type->shortcuts() == (native ? QList<QKeySequence>{QKeySequence("Shift+A")} : QList<QKeySequence>{}),"profile-isolated native type shortcut");
				if (native) { expect(legacy->shortcuts().isEmpty() && type->shortcutContext() == Qt::WidgetWithChildrenShortcut,"native key is scoped and does not invoke legacy class selector"); }
				const auto pick = [&](const QVector<LevelMapSelectionRef>& refs) { view->setSelectionSet(refs); view->selectionSetChanged(refs); view->setFocus(Qt::OtherFocusReason); };
				const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
				pick(seed); const auto depth = shell.levelDocument().undoStack.size(); tool->click();
				if (!expect(shell.levelDocument().selection == tests::doorTypeSelection() && shell.levelDocument().undoStack.size() == depth+(tb ? 1 : 0),"Tools action uses owner class and history policy",shell.statusBar()->currentMessage())) { return 1; }
				if (tb) { action("map.undo")->trigger(); expect(shell.levelDocument().selection == seed,"type selection undo"); action("map.redo")->trigger(); }
				type->trigger(); expect(shell.levelDocument().undoStack.size() == depth+(tb ? 1 : 0),"same class result has no extra undo");
				pick({}); material->setEditText("STUDIO/CHECKER"); expect(type->isEnabled(),"empty selection permits material fallback"); type->trigger();
				expect(shell.levelDocument().selection == tests::materialTypeSelection(),"picker supplies current material without selected owner",shell.statusBar()->currentMessage());
				material->setEditText("missing"); pick({{LevelMapSelectionKind::QuakePatch,0}}); type->trigger(); expect(shell.levelDocument().selection.isEmpty(),"no match clears old objects");
				bool browserPick = false;
				for (int i = 0; i < textures->count(); ++i) { if (textures->item(i)->data(Qt::UserRole).toString() == "metal") { textures->setCurrentRow(i); browserPick = true; break; } }
				expect(browserPick && shell.levelPaintMaterial() == "metal","texture browser updates shared material"); type->trigger();
				expect(shell.levelDocument().selection == QVector<LevelMapSelectionRef>{{LevelMapSelectionKind::QuakeBrush,0}},"browser-driven material fallback");
				std::cout << "Object queries: " << phase.elapsed() << " ms" << std::endl;
				pick({{LevelMapSelectionKind::Entity,1},{LevelMapSelectionKind::QuakePatch,2}}); action("map.faceTool")->trigger();
				expect(view->setPickedComponents({{{LevelMapSelectionKind::QuakeBrush,0},LevelBrushComponent::Face,{0}},{{LevelMapSelectionKind::QuakePatch,2},LevelBrushComponent::Vertex,{0}}}),"seed face and patch picks"); view->componentPicksChanged(view->pickedComponentCount());
				material->setEditText("studio/checker"); const auto objects = shell.levelDocument().selection; const auto faceDepth = shell.levelDocument().undoStack.size(); type->trigger();
				expect(shell.levelDocument().selection == objects && shell.levelDocument().undoStack.size() == faceDepth,"face selection preserves object picks and history");
				for (auto* pane : shell.findChildren<MapViewport*>()) { if (pane->componentMode() == MapViewport::ComponentMode::Faces) { expect(pane->pickedComponentCount() == 11 && pane->pickedComponents().size() == 2,"all plans select only matching brush faces"); } }
				const auto matching = view->pickedComponents();
				expect(matching.size() == 2 && matching[0].object == LevelMapSelectionRef{LevelMapSelectionKind::QuakeBrush,0}
					&& matching[0].components == QVector<int>{1,2,3,4,5},"viewport retains the exact matching face identities");
				expect(camera->pickedComponents().size() == 11,"camera matches selected faces");
				if (native && scale == 100) {
					QTimer::singleShot(0,&shell,[&] {
						expect(view->setPickedComponents({{{LevelMapSelectionKind::QuakeBrush,0},LevelBrushComponent::Face,{0}}}),"changed face seed");
						view->componentPicksChanged(view->pickedComponentCount());
					}); type->trigger();
					expect(view->pickedComponentCount() == 1 && view->pickedComponents().first().components == QVector<int>{0}
						&& shell.levelDocument().selection == objects && shell.statusBar()->currentMessage().contains("changed"),"face worker rejects stale component picks",shell.statusBar()->currentMessage());
					QTimer::singleShot(0,&shell,[&] { material->setEditText("metal"); }); type->trigger();
					expect(view->pickedComponentCount() == 1 && shell.statusBar()->currentMessage().contains("changed"),"face worker rejects changed material");
				}
				material->setEditText("missing"); type->trigger(); expect(view->pickedComponentCount() == 0 && camera->pickedComponents().isEmpty() && shell.levelDocument().selection == objects,"no face match clears components only");
				std::cout << "Face queries: " << phase.elapsed() << " ms" << std::endl;
				action("map.faceTool")->trigger();
				for (const auto* mode : {"map.vertexTool","map.edgeTool"}) {
					action(mode)->trigger(); const auto kind = QLatin1String(mode) == QLatin1String("map.vertexTool") ? LevelBrushComponent::Vertex : LevelBrushComponent::Edge;
					expect(view->setPickedComponents({{{LevelMapSelectionKind::QuakeBrush,0},kind,{0}}}),"non-face component seed"); view->componentPicksChanged(view->pickedComponentCount());
					expect(!type->isEnabled() && !tool->isEnabled(),"vertex and edge modes disable inapplicable command"); type->trigger();
					expect(view->pickedComponentCount() == 1 && shell.levelDocument().selection == objects,"disabled component command preserves picks"); action(mode)->trigger();
				}
				pick(seed); view->setFilteredObjects({{LevelMapSelectionKind::Entity,1}}); type->trigger();
				expect(shell.levelDocument().selection == QVector<LevelMapSelectionRef>{{LevelMapSelectionKind::Entity,10}},"view filters apply to new class results"); view->setFilteredObjects({});
				if (scale == 100) {
					pick(seed); bool cancelled = false; const auto before = shell.levelDocument().undoStack.size();
					QTimer::singleShot(0,&shell,[&] { if (auto* dialog = shell.findChild<QDialog*>("levelPlacementTaskDialog")) { cancelled = true; dialog->reject(); } }); type->trigger();
					expect(cancelled && shell.levelDocument().selection == seed && shell.levelDocument().undoStack.size() == before,"cancel query without selection or history changes");
					QTimer::singleShot(0,&shell,[&] { material->setEditText("changed-material"); }); type->trigger();
					expect(shell.levelDocument().selection == seed && shell.statusBar()->currentMessage().contains("changed"),"pending query rejects changed material",shell.statusBar()->currentMessage());
					QTimer::singleShot(0,&shell,[&] { view->setFilteredObjects({{LevelMapSelectionKind::QuakeBrush,1}}); }); type->trigger();
					expect(shell.levelDocument().selection == seed && shell.statusBar()->currentMessage().contains("changed"),"pending query rejects changed filter"); view->setFilteredObjects({});
					auto* profiles = shell.findChild<QComboBox*>("editorProfileCombo"); if (!expect(profiles != nullptr,"profile selector")) { return 1; }
					QTimer::singleShot(0,&shell,[&] { profiles->setCurrentIndex(profiles->findData(tb ? "netradiant-custom" : "trenchbroom")); }); type->trigger();
					expect(shell.levelDocument().selection == seed && shell.statusBar()->currentMessage().contains("changed"),"pending query rejects changed history policy"); profiles->setCurrentIndex(profiles->findData(profile));
				}
				pick({{LevelMapSelectionKind::Entity,1}}); legacy->trigger();
				expect(shell.levelDocument().selection.contains({LevelMapSelectionKind::Entity,14}) && shell.levelDocument().selection.last() == LevelMapSelectionRef{LevelMapSelectionKind::Entity,1},"legacy primary class command preserves case-insensitive behaviour and primary identity");
				expect(bytes() == source && levelMapHistoryMatchesSaved(shell.levelDocument(),static_cast<int>(shell.levelDocument().undoStack.size())),"entire workflow preserves native source and saved state");
				std::cout << "Workflow complete: " << phase.elapsed() << " ms" << std::endl;
				if (native && scale == 200) {
					const auto capture = qEnvironmentVariable("VIBESTUDIO_TYPE_SELECTION_CAPTURE");
					if (!capture.isEmpty()) { QImage image(shell.size(),QImage::Format_ARGB32_Premultiplied); shell.render(&image); expect(image.save(capture),"owned Qt evidence"); }
				}
			}
			if (scale == 200) { app.removeTranslator(&expanded); app.setLayoutDirection(Qt::LeftToRight); }
			std::cout << "Profile released: " << phase.elapsed() << " ms" << std::endl;
		}
	}
	return ok ? 0 : 1;
}
