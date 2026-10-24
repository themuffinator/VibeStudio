#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/level_surface_tools.h"
#include "core/level_face_clipboard.h"
#include "tests/level_surface_test_helpers.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
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
	while (!ready() && timer.elapsed() < 30000) { QEventLoop loop; QTimer::singleShot(15, &loop, &QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents); }
	return ready();
}
bool settle(ModelViewport* camera)
{
	return until([&] { QImage image(camera->size(), QImage::Format_ARGB32_Premultiplied); camera->render(&image); return camera->isEnabled() && !camera->isRendering(); });
}
}
int main(int argc, char** argv)
{
	// Semantic calls on owned offscreen widgets. No OS input or screen capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv); QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini")); StudioSettings settings;
	settings.setRestoreSession(false); settings.setReducedMotion(true); settings.setLevelViewLayoutPreference("four-views");
	const auto path = temp.filePath("face.map");
	{ QFile file(path); if (!file.open(QIODevice::WriteOnly) || file.write(tests::surfaceFixture("classic")) < 0) { return 1; } }
	QString error; LevelMapDocument source;
	if (!loadLevelMapBytes({"source.map", {}, "idtech3"}, tests::surfaceFixture("classic"), &source, &error)) { return 1; }
	source.brushes[0].faces[0].textureName = "studio/first";
	auto& last = source.brushes[0].faces[2]; last.textureName = "studio/last"; last.shiftX = 37; last.scaleX = 2;
	last.contentFlags = 1024; last.surfaceFlags = 128; last.surfaceValue = 77;
	const auto raw = levelFaceSelectionText(source, {{0, 0}, {0, 2}}, &error);
	for (const auto& profile : {QStringLiteral("trenchbroom"), QStringLiteral("netradiant-custom"), QStringLiteral("vibestudio-default")}) {
		std::cout << "Face clipboard UI " << profile.toStdString() << std::endl;
		settings.setSelectedEditorProfileId(profile); settings.sync(); ApplicationShell shell; shell.resize(1600, 1000); shell.show();
		shell.openPathFromCommandLine(path);
		const auto action = [&](const char* id) { return shell.findChild<QAction*>(QLatin1String(id)); };
		action("shell.mode.levels")->trigger(); action("map.layout.four-views")->trigger();
		auto* top = shell.findChild<MapViewport*>("mapViewport"); auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
		if (!expect(top && camera && until([&] { return shell.levelDocument().sourcePath == path && camera->isEnabled(); }) && settle(camera), "map and view ready")) { return 1; }
		const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
		const auto original = bytes();
		const auto select = [&](bool selected) {
			top->setSelectionSet(selected ? QVector<LevelMapSelectionRef>{{LevelMapSelectionKind::QuakeBrush, 0}} : QVector<LevelMapSelectionRef>{});
			top->selectionSetChanged(top->selectionSet());
		};
		const auto focus = [&] { shell.activateWindow(); expect(until([&] { return QApplication::activeWindow() == &shell; }), "owned active window"); camera->setFocus(Qt::OtherFocusReason); expect(until([&] { return camera->hasFocus(); }), "camera focus"); };
		const auto pick = [&](int face) {
			const auto mesh = buildLevelMapPreviewMesh(shell.levelDocument());
			for (int i = 0; i < mesh.owners.size(); ++i) {
				if (mesh.owners[i].kind == LevelMapSelectionKind::QuakeBrush && mesh.owners[i].objectId == 0 && mesh.ownerFaces.value(i, -1) == face) {
					camera->trianglePicked(0, i, int(ModelViewportPick::FaceToggle)); return true;
				}
			}
			return false;
		};
		const auto wait = [&] { return until([&] { return !shell.levelSurfaceEditsPending(); }); };
		qsizetype selectionDepth = 0;
		const auto paste = [&](const char* id) { selectionDepth = shell.levelDocument().undoStack.size(); action(id)->trigger(); };
		const auto undo = [&] { action("map.undo")->trigger(); expect(bytes() == original, "exact undo"); expect(settle(camera), "undo preview settles"); };
		select(false); expect(pick(0) && pick(2), "pick two camera faces"); focus(); action("map.copySelection")->trigger();
		QVector<LevelMapBrushFace> copied;
		if (profile == "trenchbroom") {
			expect(parseLevelFaceText(QApplication::clipboard()->text(), &copied, &error) && copied.size() == 2, "TB normal copy uses ordered picked faces", error);
			expect(!action("map.cutSelection")->isEnabled(), "TB face selection cannot cut owner brush");
			action("map.cutSelection")->trigger(); expect(bytes() == original, "disabled cut leaves map intact");
		} else {
			expect(QApplication::clipboard()->text().contains('{') && !looksLikeLevelFaceText(QApplication::clipboard()->text()), "other profiles retain object copy");
		}
		select(false); select(true);
		// Explicit Copy Surface exports plain text in every profile; sampling stays local.
		auto* inspector = shell.findChild<QTreeWidget*>("entityInspector"); bool found = false;
		for (QTreeWidgetItemIterator it(inspector); *it; ++it) {
			if ((*it)->data(0, Qt::UserRole + 11).toString() == "face:0:1:texture") { inspector->setCurrentItem(*it); found = true; break; }
		}
		expect(found && until([&] { return action("map.copySurface")->isEnabled(); }), "inspected source enables Copy Surface");
		action("map.copySurface")->trigger();
		expect(parseLevelFaceText(QApplication::clipboard()->text(), &copied, &error) && copied.size() == 1 && shell.levelSurfaceClipboard().ready(), "Copy Surface supplies both clipboards", error);
		QApplication::clipboard()->setText("sentinel");
		expect(shell.copyLevelSurfaceSettings({0, 2}, &error) && QApplication::clipboard()->text() == "sentinel", "sampling preserves system clipboard", error);
		const auto privateClipboard = serializeLevelSurfaceClipboard(shell.levelSurfaceClipboard());
		for (const auto* command : {"map.paste", "map.pasteAtOriginal", "map.pasteAtCursor", "map.pasteToCamera"}) {
			select(false); expect(settle(camera) && pick(0) && pick(2), "target face picks");
			QApplication::clipboard()->setText(raw); paste(command);
			expect(shell.levelSurfaceEditsPending() && bytes() == original, "raw paste starts asynchronously");
			expect(wait() && shell.levelDocument().undoStack.size() == selectionDepth + 1 && shell.levelDocument().brushes.size() == 1, "all normal paste variants route faces to one surface transaction");
			const auto& faces = shell.levelDocument().brushes[0].faces;
			for (int i = 0; i < faces.size(); ++i) {
				const bool changed = i == 0 || i == 2;
				expect(faces[i].contentFlags == 2 && faces[i].textureName == (changed ? "studio/last" : "studio/grid")
					&& faces[i].surfaceFlags == (changed ? 128 : 4) && faces[i].surfaceValue == (changed ? 77 : 8), "only picked faces receive last record, retaining contents");
			}
			expect(serializeLevelSurfaceClipboard(shell.levelSurfaceClipboard()) == privateClipboard, "raw paste preserves sampled source");
			const auto after = bytes(); undo(); action("map.redo")->trigger(); expect(bytes() == after, "exact redo"); undo();
		}
		select(false); select(true); QApplication::clipboard()->setText(raw); paste("map.paste");
		expect(wait() && shell.levelDocument().undoStack.size() == selectionDepth + 1, "whole brush attributes paste completes");
		for (const auto& face : shell.levelDocument().brushes[0].faces) { expect(face.textureName == "studio/last" && face.contentFlags == 2, "no picks targets all selected brush faces"); } undo();
		// The explicit panel mode shares the normal-paste transaction service.
		select(false); expect(pick(0), "panel source pick"); QApplication::clipboard()->setText(raw); paste("map.paste");
		expect(wait() && shell.copyLevelSurfaceSettings({0, 0}, &error), "sample newly pasted source", error); undo();
		select(false); expect(pick(2), "panel target pick");
		auto* panel = static_cast<LevelSurfaceTools*>(shell.findChild<QScrollArea*>("levelSurfaceTools"));
		panel->findChild<QComboBox*>("surfaceQuickTarget")->setCurrentIndex(1);
		panel->findChild<QComboBox*>("surfacePasteMode")->setCurrentIndex(5);
		expect(until([&] { return action("map.pasteSurface")->isEnabled(); }), "panel paste enabled"); paste("map.pasteSurface");
		expect(wait() && shell.levelDocument().undoStack.size() == selectionDepth + 1 && shell.levelDocument().brushes[0].faces[2].textureName == "studio/last"
			&& shell.levelDocument().brushes[0].faces[2].contentFlags == 2 && shell.levelDocument().brushes[0].faces[0].textureName == "studio/grid", "panel Attributes pastes only target face"); undo();
		select(false); expect(pick(0), "stale selection starting pick"); QApplication::clipboard()->setText(raw); paste("map.paste");
		expect(pick(2) && wait() && bytes() == original && shell.levelDocument().undoStack.size() == selectionDepth, "changed picks on same owner discard pending paste");
		QApplication::clipboard()->setText(raw); paste("map.paste"); shell.cancelLevelSurfaceEdits();
		expect(wait() && bytes() == original, "explicit cancellation has no mutation");
		QApplication::clipboard()->setText(raw + "garbage"); paste("map.paste");
		expect(wait() && bytes() == original && shell.levelDocument().undoStack.size() == selectionDepth, "malformed payload cannot partially paste");
		select(false); QApplication::clipboard()->setText(raw); paste("map.paste");
		expect(!shell.levelSurfaceEditsPending() && bytes() == original && shell.statusBar()->currentMessage().contains("Select"), "missing targets refuse without insertion");
		focus(); select(true); action("map.copySelection")->trigger(); expect(QApplication::clipboard()->text().contains('{'), "normal object copy still works");
	}
	std::cout << (ok ? "Face clipboard UI routing checks passed.\n" : "Face clipboard UI checks failed.\n"); return ok ? 0 : 1;
}
