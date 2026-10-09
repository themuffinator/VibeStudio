#include "app/application_shell.h"
#include "app/level_surface_worker.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "tests/level_material_test_helpers.h"
#include "tests/level_surface_test_helpers.h"
#include "tests/render_test_support.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QLabel>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message, const QString& detail = {})
{
	if (!value) { std::cerr << message << ": " << detail.toStdString() << '\n'; } return value;
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < 40000) { QEventLoop loop; QTimer::singleShot(15, &loop, &QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents); }
	return ready();
}
bool settle(ModelViewport& view)
{
	return until([&] { QImage image(view.size(), QImage::Format_ARGB32_Premultiplied); view.render(&image); return view.isEnabled() && !view.isRendering(); });
}
QByteArray appearance(const ModelMesh& mesh)
{
	QCryptographicHash hash(QCryptographicHash::Sha256);
	for (const auto& surface : mesh.surfaces) {
		hash.addData(surface.name.toUtf8());
		for (const auto& uv : surface.texCoords) { hash.addData(QByteArray::number(uv.u, 'g', 9) + ',' + QByteArray::number(uv.v, 'g', 9) + ';'); }
	}
	return hash.result();
}
QHash<LevelMaterialTarget, QPointF> targets(ModelViewport& view, const LevelMapPreviewMesh& mesh)
{
	QHash<LevelMaterialTarget, QPointF> found;
	for (int y = 30; y < view.height() - 25; y += 5) {
		for (int x = 25; x < view.width() - 25; x += 5) {
			const QPointF point(x, y); const auto hit = view.hitAt(point);
			if (!hit.valid || hit.triangle < 0 || hit.triangle >= mesh.materialTargets.size()) { continue; }
			const auto ref = mesh.materialTargets[hit.triangle];
			if (!found.contains(ref)) { found.insert(ref, point); }
		}
	}
	return found;
}
class Expansion final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		if (!QByteArray(context).endsWith("ApplicationShell") || !QByteArray(source).contains("stroke")) { return {}; }
		const auto value = QString::fromUtf8(source); return QStringLiteral("[%1 %2]").arg(value, QString(value.size() / 2, '~'));
	}
};
class ViewEvents final : public QObject {
public:
	std::function<bool()> pending;
	QString stage;
	int wrapResizes = 0;
	bool eventFilter(QObject*, QEvent* event) override
	{
		if (stage == "wrap-success" && event->type() == QEvent::Resize && pending()) { ++wrapResizes; }
		return false;
	}
};
}
int main(int argc, char** argv)
{
	// Semantic gestures and offscreen widget render targets only. Never inject
	// user input or capture the operating-system desktop/window.
	qputenv("QT_QPA_PLATFORM", "offscreen"); qputenv("QT_SCALE_FACTOR", "1");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv); QApplication::setFont(QFont("Segoe UI"));
	// Draws in 3D: skip where no OpenGL or Vulkan renderer starts.
	if (const int skip = vibestudio::test_support::exitCodeWithoutRenderer("level-surface-stroke-ui-smoke"); skip >= 0) {
		return skip;
	}
	QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini")); StudioSettings settings;
	settings.setRestoreSession(false); settings.setReducedMotion(true); settings.setSelectedEditorProfileId("netradiant-custom");
	bool ok = true; QString error; LevelMapDocument fixture;
	ok &= tests::createMaterialFixture(temp.path(), &fixture, &error);
	const auto path = temp.filePath("stroke.map"); LevelDocumentSaveRequest save; save.path = path; ok &= writeLevelDocument(fixture, save).succeeded();
	PackageArchive archive; ok &= archive.load(temp.filePath("assets"), &error);
	const auto assets = resolveLevelPreviewAssets(fixture, archive);
	// The worker must switch from the copied package to destination dimensions
	// when a wrap changes the clipboard's source, even for the same material name.
	{
		const auto targetPath = temp.filePath("target-assets");
		ok &= tests::putMaterialFile(QDir(targetPath).filePath("textures/studio/grid.png"), tests::materialImage(64, 256));
		auto source = std::make_shared<PackageArchive>(archive); auto target = std::make_shared<PackageArchive>(); ok &= target->load(targetPath, &error);
		LevelSurfaceWork work; ok &= loadLevelMapBytes({"matrix.map", {}, "idtech3"}, tests::surfaceFixture("brushDef"), &work.document, &error);
		ok &= copyLevelSurface(work.document, {0, 0}, &work.clipboard, &error);
		work.archive = target; work.clipboardArchive = source; work.clipboardContextCaptured = true; work.clipboardFormat = work.document.format;
		work.startStroke = true; work.paste.mode = LevelSurfacePasteMode::Seamless; work.paste.mappingOnly = true;
		work.pasteTargets = {{LevelMaterialKind::BrushFace, 0, 2}};
		LevelSurfaceWorker worker; LevelSurfaceResult result; worker.completed = [&](LevelSurfaceResult value) { result = std::move(value); };
		ok &= expect(worker.start(work) && until([&] { return !worker.busy(); }) && result.stroke && result.error.isEmpty(), "worker prepares first stroke hit", result.error);
		if (!result.stroke) { return 1; }
		work.stroke = result.stroke; work.startStroke = false; work.textureSizes = result.textureSizes;
		work.paste.textureSize = result.sourceTextureSize; work.pasteTargets = {{LevelMaterialKind::BrushFace, 0, 4}};
		ok &= expect(worker.start(work) && until([&] { return !worker.busy(); }) && result.stroke && result.error.isEmpty(), "worker advances source package role", result.error);
		LevelSurfaceStroke reference(work.document, work.clipboard); LevelSurfacePasteOptions options = work.paste;
		options.textureSize = {128, 64}; options.materialSizes.insert("studio/grid", {64, 256});
		ok &= reference.append({{LevelMaterialKind::BrushFace, 0, 2}}, options, &error) && reference.append({{LevelMaterialKind::BrushFace, 0, 4}}, options, &error);
		ok &= expect(result.stroke && serializeLevelMap(result.stroke->document()).bytes == serializeLevelMap(reference.document()).bytes, "worker and explicit dimension oracle agree across hits");
		work.stroke = result.stroke; work.finishStroke = true;
		ok &= expect(worker.start(work) && until([&] { return !worker.busy(); }) && result.finishedStroke && result.nextClipboard.ready()
			&& result.document.undoStack.size() == work.document.undoStack.size() + 1, "worker finalizes one undo and the wrapped source", result.error);
	}
	for (int scale : {100, 200}) {
		settings.setTextScalePercent(scale); settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark); settings.sync();
		applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, scale));
		Expansion expansion; if (scale == 200) { app.installTranslator(&expansion); }
		{
			ApplicationShell shell; shell.resize(scale == 100 ? 1750 : 2500, scale == 100 ? 1150 : 1750);
			if (scale == 200) { shell.setLayoutDirection(Qt::RightToLeft); }
			shell.show();
			for (auto* combo : shell.findChildren<QComboBox*>()) { if (combo->accessibleName() == "Level map engine hint") { combo->setCurrentIndex(combo->findData("idTech3")); } }
			shell.openPathFromCommandLine(archive.sourcePath()); shell.openPathFromCommandLine(path);
			shell.findChild<QAction*>("shell.mode.levels")->trigger();
			auto* view = shell.findChild<ModelViewport*>("mapPreview3D"); auto* status = shell.findChild<QLabel*>("levelMaterialPaintStatus");
			auto* cancel = shell.findChild<QToolButton*>("levelCancelMaterialStroke");
			if (!expect(view && status && cancel && until([&] { return view->isEnabled() && view->hasMesh() && view->hasSkin(); }), "stroke shell loaded")) { return 1; }
			ViewEvents events; events.pending = [&] { return shell.levelSurfaceEditsPending(); }; view->installEventFilter(&events);
			shell.findChild<QAction*>("map.selectNone")->trigger(); view->setCameraView({-180, -230, 180}, 52, -28);
			if (!settle(*view)) { return 1; }
			const auto original = shell.levelDocument(); const auto before = serializeLevelMap(original).bytes; const auto baseAppearance = appearance(view->mesh());
			const auto mesh = buildLevelMapPreviewMesh(original, {.modelMeshes = assets.models, .textureSizes = levelPreviewTextureSizes(assets)});
			const auto visible = targets(*view, mesh); QVector<LevelMaterialTarget> faces; LevelMaterialTarget patch;
			for (const auto& target : visible.keys()) {
				if (target.kind == LevelMaterialKind::BrushFace && target.objectId == 1) { faces << target; }
				if (target.kind == LevelMaterialKind::Patch) { patch = target; }
			}
			if (!expect(faces.size() >= 2 && patch.kind == LevelMaterialKind::Patch, "camera has two wall faces and a patch")) { return 1; }
			const auto first = faces[0], second = faces[1];
			ok &= shell.copyLevelSurfaceSettings({0, 0}, &error);
			QElapsedTimer admission; admission.start();
			events.stage = "mixed";
			ok &= expect(view->beginMaterialStroke(visible.value(first), Qt::MiddleButton, Qt::ShiftModifier), "native held gesture begins");
			std::cout << "Stroke admission at " << scale << "%: " << admission.elapsed() << " ms\n";
			ok &= expect(until([&] { QImage image(view->size(), QImage::Format_ARGB32_Premultiplied); view->render(&image);
				return appearance(view->mesh()) != baseAppearance || !view->materialStrokeActive(); }) && view->materialStrokeActive(), "live material preview while button remains held", status->text());
			std::cout << "First preview at " << scale << "%: " << admission.elapsed() << " ms\n";
			if (scale == 200) { ok &= expect(status->text().startsWith('['), "expanded translation is active for stroke status"); }
			ok &= expect(settle(*view) && view->materialStrokeActive(), "live texture preview is rendered during the stroke");
			ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == before && shell.levelDocument().undoStack.size() == original.undoStack.size(), "live preview is not committed");
			ok &= expect(cancel->isEnabled() && cancel->focusPolicy() != Qt::NoFocus && !cancel->accessibleName().isEmpty()
				&& QAccessible::queryAccessibleInterface(cancel)->role() == QAccessible::Button, "stroke cancel is accessible and focusable");
			ok &= expect(!shell.copyLevelSurfaceSettings({0, 1}, &error), "source cannot change during stroke");
			ok &= view->extendMaterialStroke(visible.value(second), Qt::ShiftModifier);
			ok &= view->extendMaterialStroke(visible.value(patch), Qt::ShiftModifier | Qt::ControlModifier);
			ok &= expect(until([&] { return status->accessibleDescription().contains("3 surface"); }), "all held hits reach preview before capture", status->accessibleDescription());
			{ QEventLoop loop; QTimer::singleShot(100, &loop, &QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents); }
			ok &= settle(*view);
			const auto capture = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
			if (!capture.isEmpty()) {
				auto* controls = shell.findChild<QWidget*>("levelMaterialTools");
				ok &= expect(status->isVisible() && cancel->isVisible() && controls->rect().contains(QRect(status->mapTo(controls, QPoint()), status->size()))
					&& controls->rect().contains(QRect(cancel->mapTo(controls, QPoint()), cancel->size())), "stroke status and cancel fit the scaled toolbar");
				QImage image(controls->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); controls->render(&image);
				ok &= image.save(QDir(capture).filePath(QStringLiteral("surface-stroke-controls-%1.png").arg(scale)));
				QImage camera(view->size(), QImage::Format_ARGB32_Premultiplied); camera.fill(Qt::transparent); view->render(&camera);
				ok &= camera.save(QDir(capture).filePath(QStringLiteral("surface-stroke-camera-%1.png").arg(scale)));
			}
			view->finishMaterialStroke(true);
			if (!expect(until([&] { return !shell.levelSurfaceEditsPending(); }) && settle(*view), "stroke release completes", status->text())) { return 1; }
			LevelSurfaceClipboard source; ok &= copyLevelSurface(original, {0, 0}, &source, &error);
			LevelSurfaceStroke reference(original, source); LevelSurfacePasteOptions values; values.mode = LevelSurfacePasteMode::RadiantValues; values.includeSelection = true;
			ok &= reference.append({first}, values, &error) && reference.append({second}, values, &error);
			values.mode = LevelSurfacePasteMode::RadiantProject; values.materialSizes = levelPreviewTextureSizes(assets);
			ok &= reference.append({patch}, values, &error);
			auto expected = original; ok &= reference.commit(&expected, &error);
			ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == serializeLevelMap(expected).bytes
				&& shell.levelDocument().undoStack.size() == original.undoStack.size() + 1 && shell.levelDocument().selection == original.selection,
				"held gesture keeps correct targets after triangle regrouping and commits one undo", status->text());
			shell.findChild<QAction*>("map.undo")->trigger(); ok &= settle(*view);
			ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == before, "GUI stroke exact undo");
			// Cancellation after a visible preview restores both the map and camera.
			events.stage = "cancel";
			ok &= view->beginMaterialStroke(visible.value(first), Qt::MiddleButton, Qt::ShiftModifier);
			ok &= until([&] { return appearance(view->mesh()) != baseAppearance || !view->materialStrokeActive(); });
			cancel->click(); ok &= until([&] { return !shell.levelSurfaceEditsPending(); }) && settle(*view);
			ok &= expect(!view->materialStrokeActive() && serializeLevelMap(shell.levelDocument()).bytes == before
				&& appearance(view->mesh()) == baseAppearance && !cancel->isEnabled(), "cancel discards complete live preview", status->text());
			// Semantic Escape path while hits are still queued, before final adoption.
			events.stage = "cancel-after-release";
			const auto copied = serializeLevelSurfaceClipboard(shell.levelSurfaceClipboard());
			ok &= view->beginMaterialStroke(visible.value(first), Qt::MiddleButton, Qt::ControlModifier);
			ok &= view->extendMaterialStroke(visible.value(second), Qt::ControlModifier);
			view->finishMaterialStroke(true); view->finishMaterialStroke(false);
			ok &= until([&] { return !shell.levelSurfaceEditsPending(); }) && settle(*view);
			ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == before && serializeLevelSurfaceClipboard(shell.levelSurfaceClipboard()) == copied,
				"cancel after release prevents late worker and clipboard adoption");
			LevelSurfaceStroke wrapped(shell.levelDocument(), shell.levelSurfaceClipboard()); LevelSurfacePasteOptions wrap; wrap.mode = LevelSurfacePasteMode::Seamless;
			events.stage = "wrap-success";
			ok &= wrapped.append({first}, wrap, &error) && wrapped.append({second}, wrap, &error);
			ok &= view->beginMaterialStroke(visible.value(first), Qt::MiddleButton, Qt::ControlModifier)
				&& view->extendMaterialStroke(visible.value(second), Qt::ControlModifier);
			view->finishMaterialStroke(true); ok &= until([&] { return !shell.levelSurfaceEditsPending(); }) && settle(*view);
			ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == serializeLevelMap(wrapped.document()).bytes
				&& serializeLevelSurfaceClipboard(shell.levelSurfaceClipboard()) == serializeLevelSurfaceClipboard(wrapped.clipboard()), "successful held wrap adopts the final source only once", status->text());
			ok &= expect(events.wrapResizes == 0, "translated progress never resizes the camera during a pending stroke");
			shell.findChild<QAction*>("map.undo")->trigger(); ok &= settle(*view);
			ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == before, "wrapped stroke has exact GUI undo");
			events.stage = "selection-cancel";
			ok &= view->beginMaterialStroke(visible.value(first), Qt::MiddleButton, Qt::ShiftModifier);
			shell.findChild<QAction*>("map.selectAll")->trigger();
			ok &= until([&] { return !shell.levelSurfaceEditsPending(); }) && settle(*view);
			ok &= expect(!view->materialStrokeActive() && serializeLevelMap(shell.levelDocument()).bytes == before, "selection change discards stale stroke");
			shell.findChild<QAction*>("map.selectNone")->trigger(); ok &= settle(*view);
			events.stage = "queue-limit";
			ok &= view->beginMaterialStroke(visible.value(first), Qt::MiddleButton, Qt::ShiftModifier);
			for (int hit = 0; hit < 300 && view->materialStrokeActive(); ++hit) { view->extendMaterialStroke(visible.value(hit % 2 ? first : second), Qt::ShiftModifier); }
			ok &= until([&] { return !shell.levelSurfaceEditsPending(); }) && settle(*view);
			ok &= expect(!view->materialStrokeActive() && serializeLevelMap(shell.levelDocument()).bytes == before,
				"queue overflow discards every pending hit without partial publication");
			shell.hide();
		}
		if (scale == 200) { app.removeTranslator(&expansion); }
	}
	return ok ? 0 : 1;
}
