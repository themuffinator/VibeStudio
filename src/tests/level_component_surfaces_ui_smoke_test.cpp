#include "app/application_shell.h"
#include "app/level_surface_dialog.h"
#include "app/level_surface_tools.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_face_clipboard.h"
#include "core/map_preview_mesh.h"
#include "tests/level_type_selection_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QStatusBar>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) { ok = false; std::cerr << "FAIL: " << label << ": " << detail.toStdString() << '\n'; } return value;
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < 30000) { QEventLoop loop; QTimer::singleShot(10,&loop,&QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents); }
	return ready();
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* source, const char*, int) const override
	{
		const auto value = QString::fromUtf8(source); return QStringLiteral("[%1%2]").arg(value,QString(value.size()/3,'~'));
	}
};
QStringList componentIds(const QVector<LevelMapComponentSet>& sets)
{
	QStringList result;
	for (const auto& set : sets) { for (int id : set.components) {
		result.append(QStringLiteral("%1/%2/%3").arg(levelMapSelectionRefId(set.object)).arg(static_cast<int>(set.kind)).arg(id));
	} }
	result.sort(); return result;
}
}
int main(int argc,char** argv)
{
	// Owned Qt controls and render targets only; no injected or OS input/capture.
	qputenv("QT_QPA_PLATFORM","offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR",QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc,argv);
#ifdef Q_OS_WIN
	// Match the Windows shell font instead of the offscreen plugin's first
	// available font, which can be a decorative face with compressed metrics.
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini")); StudioSettings settings;
	settings.setRestoreSession(false); settings.setReducedMotion(true); settings.setLevelViewLayoutPreference("four-views");
	const auto path = temp.filePath("components.map"); QFile file(path); const auto source = tests::typeSelectionFixture();
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(),"UI fixture")) { return 1; } file.close();
	LevelMapDocument copied; QString error; loadLevelMapBytes({"copy.map",{},"idtech3"},source,&copied,&error);
	auto& sourceFace = copied.brushes[0].faces[0]; sourceFace.textureName = "studio/pasted"; sourceFace.shiftX = 37;
	sourceFace.contentFlags = 512; sourceFace.surfaceFlags = 128; sourceFace.surfaceValue = 77; sourceFace.flagsWritten = true;
	const auto raw = levelFaceSelectionText(copied,{{0,0}},&error);
	const QVector<LevelSurfaceFace> pickedFaces{{0,1},{0,4},{1,3}};
	const QVector<LevelMapComponentSet> pickedSets{
		{{LevelMapSelectionKind::QuakeBrush,0},LevelBrushComponent::Face,{1,4}},
		{{LevelMapSelectionKind::QuakeBrush,1},LevelBrushComponent::Face,{3}},
		{{LevelMapSelectionKind::QuakePatch,2},LevelBrushComponent::Vertex,{0}}};
	for (const auto* profile : {"trenchbroom","netradiant-custom","vibestudio-default"}) {
		for (int scale : {100,200}) {
			std::cout << "Component surface UI " << profile << ' ' << scale << '%' << std::endl;
			QElapsedTimer phase; phase.start();
			settings.setSelectedEditorProfileId(profile); settings.setTextScalePercent(scale);
			settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark); settings.sync();
			applyStudioTheme(app,studioThemeTokens(settings.accessibilityPreferences().theme,UiDensity::Standard,scale));
			Expanded expanded; if (scale == 200) { app.installTranslator(&expanded); app.setLayoutDirection(Qt::RightToLeft); }
			{
				ApplicationShell shell; shell.resize(1600,1000); shell.show(); shell.openPathFromCommandLine(path);
				const auto action = [&](const char* id) { return shell.findChild<QAction*>(QLatin1String(id)); };
				action("shell.mode.levels")->trigger(); action("map.layout.four-views")->trigger();
				auto* view = shell.findChild<MapViewport*>("mapViewport"); auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
				if (!expect(view && camera && until([&] { return shell.levelDocument().sourcePath == path && camera->hasMesh(); }),"ready")) { return 1; }
				std::cout << "Map ready: " << phase.elapsed() << " ms" << std::endl;
				auto* panel = static_cast<LevelSurfaceTools*>(shell.findChild<QScrollArea*>("levelSurfaceTools"));
				auto* target = panel->findChild<QComboBox*>("surfaceQuickTarget");
				expect(target && !target->accessibleName().isEmpty() && QAccessible::queryAccessibleInterface(target),"accessible target control");
				view->setSelectionSet({{LevelMapSelectionKind::Entity,1},{LevelMapSelectionKind::QuakePatch,2}});
				view->selectionSetChanged(view->selectionSet()); action("map.faceTool")->trigger();
				const auto pick = [&](const QVector<LevelMapComponentSet>& sets) {
					expect(view->setPickedComponents(sets),"complete component picks"); view->componentPicksChanged(view->pickedComponentCount()); view->setFocus(Qt::OtherFocusReason);
					if (view->componentMode() == MapViewport::ComponentMode::Faces) {
						QVector<LevelMaterialTarget> wanted;
						for (const auto& set : sets) { if (set.object.kind == LevelMapSelectionKind::QuakeBrush && set.kind == LevelBrushComponent::Face) {
							for (int face : set.components) { wanted.append({LevelMaterialKind::BrushFace,set.object.objectId,face}); }
						} }
						const auto preview = buildLevelMapPreviewMesh(shell.levelDocument()); int triangles = 0;
						for (const auto& materialTarget : preview.materialTargets) { triangles += wanted.contains(materialTarget); }
						expect(camera->highlightedTriangleCount() == triangles,"camera highlights exactly the picked brush-face components");
					}
				};
				const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
				const auto wait = [&] { return until([&] { return !shell.levelSurfaceEditsPending(); }); };
				const auto original = bytes(); const auto originalMap = shell.levelDocument();
				const auto undo = [&] { action("map.undo")->trigger(); expect(bytes() == original,"exact undo restores map bytes"); };
				const auto unchangedLines = [&](const QVector<LevelSurfaceFace>& changed) {
					const auto before = original.split('\n'), after = bytes().split('\n'); QSet<int> lines;
					for (const auto& face : changed) { lines.insert(originalMap.brushes[face.brushId].faces[face.faceIndex].line-1); }
					if (!expect(before.size() == after.size(),"no extra geometry or source lines")) { return; }
					for (int i = 0; i < before.size(); ++i) { if (!lines.contains(i)) { expect(before[i] == after[i],"unselected face, patch and entity bytes unchanged"); } }
				};
				for (int scope : {0,1}) {
					pick(pickedSets); target->setCurrentIndex(scope); const auto depth = shell.levelDocument().undoStack.size();
					expect(action("map.surfaceShiftRight")->isEnabled(),"component picks enable surface command"); action("map.surfaceShiftRight")->trigger();
					expect(wait() && shell.levelDocument().undoStack.size() == depth+1,"one surface undo transaction");
					for (const auto& face : pickedFaces) { expect(shell.levelDocument().brushes[face.brushId].faces[face.faceIndex].shiftX
						== originalMap.brushes[face.brushId].faces[face.faceIndex].shiftX+8,"exact selected face shift"); }
					unchangedLines(pickedFaces);
					expect(shell.levelDocument().selection == originalMap.selection && view->componentMode() == MapViewport::ComponentMode::Faces
						&& componentIds(view->pickedComponents()) == componentIds(pickedSets),"surface edit preserves objects, mode and patch-point picks");
					const auto changed = bytes(); undo(); action("map.redo")->trigger(); expect(bytes() == changed,"exact redo"); undo();
				}
				pick(pickedSets); action("map.copySurface")->trigger(); QVector<LevelMapBrushFace> records;
				expect(parseLevelFaceText(QApplication::clipboard()->text(),&records,&error) && records.size() == 3
					&& shell.levelSurfaceClipboard().ready(),"Copy Surface exports component faces to both clipboards",error);
				if (QLatin1String(profile) == "trenchbroom") {
					action("map.copySelection")->trigger(); expect(parseLevelFaceText(QApplication::clipboard()->text(),&records,&error) && records.size() == 3,"TB normal copy uses component faces");
				}
				QApplication::clipboard()->setText(raw); action("map.paste")->trigger(); expect(wait(),"raw paste completed"); unchangedLines(pickedFaces);
				for (const auto& face : pickedFaces) { const auto& value = shell.levelDocument().brushes[face.brushId].faces[face.faceIndex];
					expect(value.textureName == "studio/pasted" && value.contentFlags == originalMap.brushes[face.brushId].faces[face.faceIndex].contentFlags
						&& value.surfaceFlags == 128 && value.surfaceValue == 77,"raw paste reaches only components and retains contents"); }
				undo();
				pick(pickedSets); expect(shell.copyLevelSurfaceSettings({0,0},&error),"sample surface",error);
				panel->findChild<QComboBox*>("surfacePasteMode")->setCurrentIndex(5); action("map.pasteSurface")->trigger();
				expect(wait(),"panel attributes paste completed"); unchangedLines(pickedFaces);
				for (const auto& face : pickedFaces) { expect(shell.levelDocument().brushes[face.brushId].faces[face.faceIndex].textureName == "metal","panel paste uses component targets"); } undo();
				if (QLatin1String(profile) == "netradiant-custom" && scale == 100) {
					// Native selection-inclusive transfers keep the hit separate from
					// component targets, including across the stroke worker's reset.
					const LevelMaterialTarget hit{LevelMaterialKind::BrushFace,1,0};
					auto changed = pickedFaces; changed.append({1,0});
					for (bool stroke : {false,true}) {
						pick(pickedSets); const auto depth = shell.levelDocument().undoStack.size();
						if (stroke) {
							const auto preview = buildLevelMapPreviewMesh(shell.levelDocument());
							const int triangle = static_cast<int>(preview.materialTargets.indexOf(hit));
							expect(triangle >= 0,"stroke target exists in preview");
							camera->materialStrokeBegan(); camera->materialStrokeTouched(triangle,CameraMaterialGesture::ValuesSelection);
							camera->materialStrokeEnded(true);
						} else {
							LevelSurfacePasteOptions options; options.mode = LevelSurfacePasteMode::RadiantValues; options.includeSelection = true;
							expect(shell.pasteLevelSurfaceTargets({hit},options,&error),"selection-inclusive transfer starts",error);
						}
						expect(wait() && shell.levelDocument().undoStack.size() == depth+1,"hit and component transfer is one undo step");
						unchangedLines(changed);
						for (const auto& face : changed) { expect(shell.levelDocument().brushes[face.brushId].faces[face.faceIndex].textureName == "metal","hit and exact component faces receive material"); }
						expect(componentIds(view->pickedComponents()) == componentIds(pickedSets),"native transfer retains component picks"); undo();
					}
				}
				std::cout << "Surface transfers: " << phase.elapsed() << " ms" << std::endl;
				// Changing picks or mode retires queued work before it can publish.
				pick(pickedSets); action("map.surfaceShiftRight")->trigger(); pick({}); expect(wait() && bytes() == original,"changed picks discard queued adjustment");
				pick(pickedSets); QApplication::clipboard()->setText(raw); action("map.paste")->trigger(); action("map.edgeTool")->trigger();
				expect(wait() && bytes() == original,"changed component mode discards surface paste");
				for (const auto* mode : {"map.vertexTool","map.edgeTool","map.faceTool"}) {
					action(mode)->trigger(); pick({}); QApplication::clipboard()->setText(raw); action("map.paste")->trigger();
					expect(!shell.levelSurfaceEditsPending() && bytes() == original && !action("map.surfaceShiftRight")->isEnabled()
						&& !action("map.copySurface")->isEnabled() && !action("map.alignSurfaces")->isEnabled(),"empty or non-face components never fall back to whole brushes");
					expect(!shell.queueLevelSurfaceEdit({LevelSurfaceOperation::Shift,1,0},&error),"direct empty surface request refuses");
				}
				pick(pickedSets);
				// The query's geometry-face output must drive a real surface edit.
				shell.findChild<QComboBox*>("levelPaintMaterial")->setEditText("metal"); action("map.selectAllOfType")->trigger();
				action("map.surfaceShiftRight")->trigger(); expect(wait() && shell.levelDocument().brushes[0].faces[0].shiftX == originalMap.brushes[0].faces[0].shiftX+8,"type query feeds surface command");
				unchangedLines({{0,0}}); undo(); pick(pickedSets);
				if (QLatin1String(profile) == "netradiant-custom" && scale == 100) {
					for (bool stale : {false,true}) {
						pick(pickedSets);
						QTimer::singleShot(0,&shell,[&] {
							auto* dialog = static_cast<LevelSurfaceDialog*>(shell.findChild<QDialog*>("levelSurfaceDialog"));
							if (!expect(dialog != nullptr,"alignment dialog")) { return; }
							expect(dialog->selectedFaces() == pickedFaces,"alignment dialog receives exact component faces");
							dialog->setRequest({LevelSurfaceOperation::Shift,3,0});
							expect(until([&] { return dialog->isReady() && dialog->previewValid(); }),"alignment preview");
							if (stale) { pick({}); }
							dialog->accept(); if (dialog->isVisible()) { expect(stale,"valid alignment accepted"); dialog->reject(); }
						});
						action("map.alignSurfaces")->trigger();
						if (stale) { expect(bytes() == original,"stale alignment component selection rejected"); }
						else { unchangedLines(pickedFaces); expect(bytes() != original,"alignment committed"); undo(); }
					}
				}
				if (QLatin1String(profile) == "netradiant-custom" && scale == 200) {
					const auto capture = qEnvironmentVariable("VIBESTUDIO_COMPONENT_SURFACES_CAPTURE");
					if (!capture.isEmpty()) { QImage image(shell.size(),QImage::Format_ARGB32_Premultiplied); shell.render(&image); expect(image.save(capture),"owned render saved"); }
				}
				action("map.faceTool")->trigger();
				const auto objectTargets = levelSurfacePasteSelectionTargets(shell.levelDocument());
				const auto objectPreview = buildLevelMapPreviewMesh(shell.levelDocument()); int objectTriangles = 0;
				for (const auto& materialTarget : objectPreview.materialTargets) { objectTriangles += objectTargets.contains(materialTarget); }
				expect(view->componentMode() == MapViewport::ComponentMode::Off && camera->highlightedTriangleCount() == objectTriangles,
					"leaving Face mode restores whole-object highlights");
				expect(bytes() == original,"selection and completed undo preserve entire source");
				std::cout << "Workflow complete: " << phase.elapsed() << " ms" << std::endl;
			}
			if (scale == 200) { app.removeTranslator(&expanded); app.setLayoutDirection(Qt::LeftToRight); }
			std::cout << "Profile released: " << phase.elapsed() << " ms" << std::endl;
		}
	}
	return ok ? 0 : 1;
}
