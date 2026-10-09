#include "app/application_shell.h"
#include "app/level_object_list.h"
#include "app/level_surface_tools.h"
#include "app/level_surface_worker.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/package_staging.h"
#include "tests/level_material_test_helpers.h"
#include "tests/level_object_test_helpers.h"
#include "tests/level_surface_test_helpers.h"
#include "tests/render_test_support.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QScrollBar>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <atomic>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message, const QString& detail = {})
{
	if (!value) { std::cerr << message << ": " << detail.toStdString() << '\n'; }
	return value;
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer time; time.start();
	while (!ready() && time.elapsed() < 45000) { QEventLoop loop; QTimer::singleShot(15, &loop, &QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents); }
	return ready();
}
bool settled(ModelViewport& view)
{
	return until([&] { QImage image(view.size(), QImage::Format_ARGB32_Premultiplied); view.render(&image); return view.isEnabled() && !view.isRendering(); });
}
bool inspect(ApplicationShell& shell, int brush, int face)
{
	auto* objects = shell.findChild<LevelObjectList*>("levelMapObjects");
	for (int row = 0; row < objects->model()->rowCount(); ++row) {
		if (tests::objectIndex(objects, row).data(Qt::UserRole).toString() == QStringLiteral("brush:%1").arg(brush)) {
			tests::setObjectCurrentRow(objects, row, QItemSelectionModel::ClearAndSelect); break;
		}
	}
	auto* inspector = shell.findChild<QTreeWidget*>("entityInspector");
	for (QTreeWidgetItemIterator it(inspector); *it; ++it) {
		if ((*it)->data(0, Qt::UserRole + 11).toString() == QStringLiteral("face:%1:%2:texture").arg(brush).arg(face)) {
			inspector->setCurrentItem(*it); return true;
		}
	}
	return false;
}
class Expansion final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		if (QByteArray(context) != "LevelSurfaceTools") { return {}; }
		const auto text = QString::fromUtf8(source); return QStringLiteral("[%1 %2]").arg(text, QString(text.size() / 2, '~'));
	}
};
class BlockingReader final : public PackageArchiveReader {
public:
	PackageArchive archive; mutable std::atomic_bool entered{false}, armed{true}; mutable QSemaphore release;
	PackageArchiveFormat format() const override { return archive.format(); }
	QString sourcePath() const override { return archive.sourcePath(); }
	bool isOpen() const override { return archive.isOpen(); }
	QVector<PackageEntry> entries() const override { return archive.entries(); }
	bool readEntryBytes(const QString& name, QByteArray* data, QString* error, qint64 limit) const override { return archive.readEntryBytes(name, data, error, limit); }
	bool readEntryAt(qsizetype index, QByteArray* data, QString* error, qint64 limit) const override
	{
		if (armed.exchange(false)) { entered = true; if (!release.tryAcquire(1, 10000)) { if (error) { *error = "Gate timed out"; } return false; } }
		return archive.readEntryAt(index, data, error, limit);
	}
};
}
int main(int argc, char** argv)
{
	// Semantic calls on offscreen widgets; no mouse/keyboard injection or OS capture.
	qputenv("QT_QPA_PLATFORM", "offscreen"); qputenv("QT_SCALE_FACTOR", "1");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv); QApplication::setFont(QFont("Segoe UI"));
	// Draws in 3D: skip where no OpenGL or Vulkan renderer starts.
	if (const int skip = vibestudio::test_support::exitCodeWithoutRenderer("level-surface-clipboard-ui-smoke"); skip >= 0) {
		return skip;
	}
	QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini")); StudioSettings settings;
	settings.setRestoreSession(false); settings.setReducedMotion(true);
	bool ok = true; QString error, layer; LevelMapDocument fixture;
	ok &= tests::createMaterialFixture(temp.path(), &fixture, &error);
	LevelSurfaceClipboard clipboard; ok &= copyLevelSurface(fixture, {0, 0}, &clipboard, &error);
	auto json = QJsonDocument::fromJson(serializeLevelSurfaceClipboard(clipboard)).object();
	json.insert("flags", QJsonArray{32, 64, 128}); auto mapping = json.value("mapping").toObject(); mapping.insert("shift", QJsonArray{17, -9}); json.insert("mapping", mapping);
	ok &= parseLevelSurfaceClipboard(QJsonDocument(json).toJson(), &clipboard, &error);
	QVector<LevelSurfaceFace> sourceFaces; for (int i = 0; i < fixture.brushes[0].faces.size(); ++i) { sourceFaces << LevelSurfaceFace{0, i}; }
	LevelSurfaceEditPlan fixturePlan;
	ok &= prepareLevelSurfacePaste(fixture, sourceFaces, clipboard, {}, &fixturePlan, &error) && commitLevelSurfaceEdit(&fixture, fixturePlan, &error);
	ok &= createLevelSceneNode(&fixture, LevelSceneNodeKind::Layer, "Source protected", {}, &layer, &error)
		&& assignLevelSceneObjects(&fixture, layer, {"brush:0"}, &error) && setLevelSceneLocked(&fixture, layer, true, &error);
	const auto path = temp.filePath("surfaces.map"), matrixPath = temp.filePath("matrix.map");
	const auto classicPath = temp.filePath("classic.map");
	LevelDocumentSaveRequest save; save.path = path; ok &= writeLevelDocument(fixture, save).succeeded();
	ok &= tests::putMaterialFile(matrixPath, tests::surfaceFixture("brushDef").replace("0.0078125", "0.015625"));
	ok &= tests::putMaterialFile(classicPath, tests::surfaceFixture("classic"));
	PackageArchive archive; ok &= archive.load(temp.filePath("assets"), &error);
	if (!expect(ok, "fixtures", error)) { return 1; }
	{
		auto reader = std::make_shared<BlockingReader>(); ok &= reader->archive.load(archive.sourcePath(), &error);
		LevelSurfaceWork work; work.archive = reader; work.clipboard = clipboard; work.paste.mode = LevelSurfacePasteMode::Project; work.faces = {{0, 0}}; work.token = 91;
		ok &= loadLevelMap({matrixPath, {}, "idtech3"}, &work.document, &error);
		LevelSurfaceWorker worker; LevelSurfaceResult result; worker.completed = [&](LevelSurfaceResult value) { result = std::move(value); };
		int beats = 0; QTimer heartbeat; heartbeat.setInterval(1); QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++beats; }); heartbeat.start();
		ok &= expect(worker.start(work) && until([&] { return reader->entered && beats >= 3; }), "package dimensions resolve off the GUI thread");
		worker.cancel(); reader->release.release();
		ok &= expect(until([&] { return !worker.busy(); }) && result.cancelled, "in-flight projection cancellation");
		ok &= expect(worker.start(work) && until([&] { return !worker.busy(); }) && result.pasted && result.error.isEmpty()
			&& result.changedFaces == 1 && result.textureSizes.value("studio/grid") == QSize(128, 64), "projection uses original package image dimensions", result.error);
		work.paste.mode = LevelSurfacePasteMode::Seamless; work.faces = {{0, 2}};
		reader->armed = true; reader->entered = false;
		ok &= expect(worker.start(work) && until([&] { return reader->entered.load(); }), "wrap resolves dimensions asynchronously");
		worker.cancel(); reader->release.release();
		ok &= expect(until([&] { return !worker.busy(); }) && result.cancelled && !result.nextClipboard.ready(), "cancelled wrap cannot advance the clipboard");
		ok &= expect(worker.start(work) && until([&] { return !worker.busy(); }) && result.error.isEmpty() && result.nextClipboard.ready(), "single wrapped face becomes a candidate clipboard", result.error);
		LevelSurfaceClipboard next; ok &= copyLevelSurface(result.document, {0, 2}, &next, &error);
		ok &= expect(serializeLevelSurfaceClipboard(next) == serializeLevelSurfaceClipboard(result.nextClipboard), "candidate clipboard describes the actual wrapped face");
		ok &= expect(result.nextClipboardFormat == work.document.format && result.nextClipboardEngineFamily == work.document.engineFamily,
			"wrapped clipboard retains the new source asset rules");
		work.faces = {{0, 2}, {0, 3}};
		ok &= expect(worker.start(work) && until([&] { return !worker.busy(); }) && result.error.isEmpty() && !result.nextClipboard.ready(), "multi-face wrap retains original clipboard source", result.error);
	}
	{
		const auto targetPackage = temp.filePath("target-assets");
		ok &= tests::putMaterialFile(QDir(targetPackage).filePath("textures/studio/grid.png"), tests::materialImage(64, 256));
		auto source = std::make_shared<BlockingReader>(); ok &= source->archive.load(archive.sourcePath(), &error);
		auto target = std::make_shared<PackageArchive>(); ok &= target->load(targetPackage, &error);
		LevelSurfaceWork work; ok &= loadLevelMap({matrixPath, {}, "idtech3"}, &work.document, &error);
		ok &= copyLevelSurface(work.document, {0, 0}, &work.clipboard, &error);
		work.archive = target; work.clipboardArchive = source; work.clipboardContextCaptured = true;
		work.clipboardEngineFamily = "idtech3";
		work.clipboardFormat = work.document.format;
		work.paste.mode = LevelSurfacePasteMode::RadiantValues; work.paste.mappingOnly = true; work.pasteTargets = {{LevelMaterialKind::BrushFace, 0, 0}};
		const auto before = work.document.brushes[0].faces[0].textureMatrix;
		LevelSurfaceWorker worker; LevelSurfaceResult result; worker.completed = [&](LevelSurfaceResult value) { result = std::move(value); };
		int beats = 0; QTimer heartbeat; heartbeat.setInterval(1); QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++beats; }); heartbeat.start();
		ok &= expect(worker.start(work) && until([&] { return source->entered && beats >= 3; }), "copy-time source package resolves asynchronously");
		worker.cancel(); source->release.release();
		ok &= expect(until([&] { return !worker.busy(); }) && result.cancelled, "source dimension lookup is cancellable");
		ok &= expect(worker.start(work) && until([&] { return !worker.busy(); }) && result.error.isEmpty() && result.changedFaces == 1, "different packages with same material name resolve separately", result.error);
		if (!result.document.brushes.isEmpty()) {
			const auto after = result.document.brushes[0].faces[0].textureMatrix;
			for (int i = 0; i < 3; ++i) { ok &= expect(after[i] == before[i] * 2 && after[i + 3] == before[i + 3] / 4, "worker retains copied texel density"); }
		}
		work.paste.mappingOnly = false;
		ok &= expect(worker.start(work) && until([&] { return !worker.busy(); }) && result.error.isEmpty() && result.changedFaces == 1,
			"full native values also resolves original and current image dimensions", result.error);
		if (!result.document.brushes.isEmpty()) {
			const auto after = result.document.brushes[0].faces[0].textureMatrix;
			for (int i = 0; i < 3; ++i) { ok &= expect(after[i] == before[i] * 2 && after[i + 3] == before[i + 3] / 4, "full native values retains copied texel density"); }
		}
		work.archive.reset();
		ok &= expect(worker.start(work) && until([&] { return !worker.busy(); }) && !result.error.isEmpty() && result.changedFaces == 0, "missing target package cannot borrow original image dimensions");
		work.archive = target; work.paste.mappingOnly = true;
		work.clipboardArchive.reset();
		ok &= expect(worker.start(work) && until([&] { return !worker.busy(); }) && !result.error.isEmpty() && result.changedFaces == 0,
			"missing original package cannot substitute same-named target image");
		work.clipboardArchive = source; work.document.engineFamily = "idtech2"; work.document.format = LevelMapFormat::QuakeMap;
		work.paste.materialSizes.insert("studio/grid", QSize(64, 256));
		auto shader = QJsonDocument::fromJson(serializeLevelSurfaceClipboard(work.clipboard)).object(); shader.insert("material", "studio/shader");
		ok &= parseLevelSurfaceClipboard(QJsonDocument(shader).toJson(), &work.clipboard, &error);
		ok &= expect(worker.start(work) && until([&] { return !worker.busy(); }) && result.error.isEmpty(), "copied shader retains its source format and engine's asset rules", result.error);
		if (!result.document.brushes.isEmpty()) {
			const auto after = result.document.brushes[0].faces[0].textureMatrix;
			for (int i = 0; i < 3; ++i) { ok &= expect(after[i] == before[i] && after[i + 3] == before[i + 3] / 8, "source shader editor image supplies original dimensions"); }
		}
	}
	{
		PackageStagingModel source, target;
		ok &= source.createEmpty(PackageArchiveFormat::Zip, {}, &error) && target.createEmpty(PackageArchiveFormat::Zip, {}, &error);
		ok &= source.addBytes(tests::materialImage(128, 64), "textures/studio/grid.png", &error);
		ok &= target.addBytes(tests::materialImage(64, 256), "textures/studio/grid.png", &error);
		LevelSurfaceWork work; ok &= loadLevelMap({matrixPath, {}, "idtech3"}, &work.document, &error);
		ok &= copyLevelSurface(work.document, {0, 0}, &work.clipboard, &error);
		work.clipboardContextCaptured = true; work.clipboardFormat = work.document.format; work.clipboardEngineFamily = work.document.engineFamily;
		work.clipboardStaging = std::make_shared<const PackageStagingModel>(source);
		work.staging = std::make_shared<const PackageStagingModel>(target);
		// A stale base archive must never supersede staged bytes. Later staging
		// edits must not alter either immutable operation snapshot.
		work.archive = std::make_shared<const PackageArchive>(archive);
		work.clipboardArchive = work.archive;
		ok &= source.addBytes(tests::materialImage(16, 32), "textures/studio/grid.png", &error, PackageStageConflictResolution::ReplaceExisting);
		ok &= target.addBytes(tests::materialImage(32, 16), "textures/studio/grid.png", &error, PackageStageConflictResolution::ReplaceExisting);
		work.paste.mode = LevelSurfacePasteMode::RadiantValues; work.paste.mappingOnly = true;
		work.pasteTargets = {{LevelMaterialKind::BrushFace, 0, 0}};
		const auto before = work.document.brushes[0].faces[0].textureMatrix;
		LevelSurfaceWorker worker; LevelSurfaceResult result; worker.completed = [&](LevelSurfaceResult value) { result = std::move(value); };
		ok &= expect(worker.start(work) && until([&] { return !worker.busy(); }) && result.error.isEmpty() && result.changedFaces == 1,
			"source and target staging indexes are prepared on the worker", result.error);
		if (!result.document.brushes.isEmpty()) {
			const auto after = result.document.brushes[0].faces[0].textureMatrix;
			for (int i = 0; i < 3; ++i) { ok &= expect(after[i] == before[i] * 2 && after[i + 3] == before[i + 3] / 4, "frozen staged dimensions override base and later edits"); }
		}
		ok &= expect(result.textureSizes.value("studio/grid") == QSize(64, 256), "target staged texture dimensions reach the result cache");
		work.paste.mode = LevelSurfacePasteMode::RadiantProject; work.pasteTargets = {{LevelMaterialKind::BrushFace, 0, 2}};
		ok &= expect(worker.start(work) && until([&] { return !worker.busy(); }) && result.error.isEmpty() && result.edgeOnFaces == 1 && !result.nextClipboard.ready(),
			"native edge-on projection is reported without advancing the clipboard", result.error);
	}
	for (int scale : {100, 200}) {
		std::cerr << "Surface clipboard scale " << scale << std::endl;
		settings.setSelectedEditorProfileId("q3radiant"); settings.setTextScalePercent(scale);
		settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark); settings.sync();
		applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, scale));
		Expansion expansion; if (scale == 200) { app.installTranslator(&expansion); }
		{
			ApplicationShell shell; shell.resize(scale == 100 ? 1700 : 2500, scale == 100 ? 1120 : 1750);
			if (scale == 200) { shell.setLayoutDirection(Qt::RightToLeft); }
			shell.show();
			for (auto* combo : shell.findChildren<QComboBox*>()) {
				if (combo->accessibleName() == "Level map engine hint") {
					const int index = combo->findData(QStringLiteral("idtech3"));
					ok &= expect(index >= 0, "Quake III engine hint exists"); combo->setCurrentIndex(index);
				}
			}
			shell.openPathFromCommandLine(archive.sourcePath()); shell.openPathFromCommandLine(path);
			const auto action = [&](const char* id) { return shell.findChild<QAction*>(QString::fromLatin1(id)); };
			action("shell.mode.levels")->trigger(); auto* view = shell.findChild<ModelViewport*>("mapPreview3D");
			auto* panel = static_cast<LevelSurfaceTools*>(shell.findChild<QScrollArea*>("levelSurfaceTools"));
			if (!expect(view && panel && until([&] { return shell.levelDocument().sourcePath == path && view->isEnabled() && view->hasMesh(); }), "loaded shell")) { return 1; }
			const auto original = serializeLevelMap(shell.levelDocument()).bytes;
			ok &= expect(inspect(shell, 0, 0), "source inspector");
			ok &= until([&] { return action("map.copySurface")->isEnabled(); }); action("map.copySurface")->trigger();
			ok &= expect(shell.levelSurfaceClipboard().ready() && serializeLevelMap(shell.levelDocument()).bytes == original, "copy from locked source is read-only");
			ok &= expect(inspect(shell, 1, 0), "destination inspector"); panel->findChild<QComboBox*>("surfaceQuickTarget")->setCurrentIndex(1);
			ok &= until([&] { return action("map.pasteSurface")->isEnabled(); });
			const auto before = shell.levelDocument(); auto expected = before; LevelSurfaceEditPlan plan;
			ok &= prepareLevelSurfacePaste(expected, {{1, 0}}, shell.levelSurfaceClipboard(), {}, &plan, &error) && commitLevelSurfaceEdit(&expected, plan, &error);
			action("map.pasteSurface")->trigger();
			ok &= expect(shell.levelSurfaceEditsPending() && serializeLevelMap(shell.levelDocument()).bytes == original, "paste queues without synchronous mutation");
			ok &= expect(!shell.pasteLevelSurfaceSettings({{1, 1}}, {}, &error) && !shell.queueLevelSurfaceEdit({}, &error), "pending paste owns its batch");
			ok &= expect(until([&] { return !shell.levelSurfaceEditsPending(); }) && serializeLevelMap(shell.levelDocument()).bytes == serializeLevelMap(expected).bytes
				&& shell.levelDocument().selection == before.selection, "GUI paste matches shared service", error);
			action("map.undo")->trigger(); ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == original, "GUI exact undo");
			LevelSurfacePasteOptions wrap; wrap.mode = LevelSurfacePasteMode::Seamless;
			const auto copiedBefore = serializeLevelSurfaceClipboard(shell.levelSurfaceClipboard());
			ok &= shell.pasteLevelSurfaceSettings({{1, 0}}, wrap, &error); shell.cancelLevelSurfaceEdits();
			ok &= expect(until([&] { return !shell.levelSurfaceEditsPending(); }) && serializeLevelMap(shell.levelDocument()).bytes == original
				&& serializeLevelSurfaceClipboard(shell.levelSurfaceClipboard()) == copiedBefore, "cancelled wrap changes neither map nor clipboard");
			ok &= shell.pasteLevelSurfaceSettings({{1, 0}}, wrap, &error); action("map.selectNone")->trigger();
			ok &= expect(until([&] { return !shell.levelSurfaceEditsPending(); }) && serializeLevelMap(shell.levelDocument()).bytes == original
				&& serializeLevelSurfaceClipboard(shell.levelSurfaceClipboard()) == copiedBefore, "selection change invalidates map and clipboard publication");
			auto* profiles = shell.findChild<QComboBox*>("editorProfileCombo");
			for (const auto& profile : {"q3radiant", "gtkradiant-1-6", "netradiant", "netradiant-custom"}) {
				const bool nrc = QLatin1String(profile) == QLatin1String("netradiant-custom"), net = QLatin1String(profile) == QLatin1String("netradiant");
				profiles->setCurrentIndex(profiles->findData(QLatin1String(profile))); view->setCameraView({-180, -230, 180}, 52, -28);
				if (!expect(settled(*view), "camera ready")) { return 1; }
				const auto mesh = buildLevelMapPreviewMesh(shell.levelDocument());
				QPointF sourcePoint(-1, -1), targetPoint(-1, -1), patchPoint(-1, -1); LevelSurfaceFace targetFace;
				for (int y = 30; y < view->height() - 25; y += 6) {
					for (int x = 25; x < view->width() - 25; x += 6) {
						const auto hit = view->hitAt({double(x), double(y)});
						if (!hit.valid || hit.triangle < 0 || hit.triangle >= mesh.materialTargets.size()) { continue; }
						const auto target = mesh.materialTargets[hit.triangle];
						if (target.kind == LevelMaterialKind::Patch && target.objectId == 0) { patchPoint = {double(x), double(y)}; }
						if (target.kind != LevelMaterialKind::BrushFace) { continue; }
						if (target.objectId == 0) { sourcePoint = {double(x), double(y)}; }
						if (target.objectId == 1) { targetPoint = {double(x), double(y)}; targetFace = {1, target.faceIndex}; }
					}
				}
				if (!expect(sourcePoint.x() >= 0 && targetPoint.x() >= 0, "both source and target visible")) { return 1; }
				ok &= expect(view->materialGestureAt(sourcePoint, Qt::MiddleButton, Qt::NoModifier) && shell.levelSurfaceClipboard().ready(), "middle-click captures definition");
				if (nrc) {
					auto* objects = shell.findChild<LevelObjectList*>("levelMapObjects");
					bool first = true;
					for (int row = 0; row < objects->model()->rowCount(); ++row) {
						const auto id = tests::objectIndex(objects, row).data(Qt::UserRole).toString();
						if (id != "brush:1" && id != "patch:0") { continue; }
						tests::setObjectCurrentRow(objects, row, first ? QItemSelectionModel::ClearAndSelect : QItemSelectionModel::Select); first = false;
					}
					ok &= expect(patchPoint.x() >= 0 && shell.levelDocument().selection.size() == 2 && settled(*view), "mixed native selection and visible patch");
					for (bool project : {false, true}) { for (bool mappingOnly : {false, true}) {
						for (bool patchHit : {false, true}) {
							LevelSurfacePasteOptions values; values.mode = project ? LevelSurfacePasteMode::RadiantProject : LevelSurfacePasteMode::RadiantValues; values.includeSelection = true; values.mappingOnly = mappingOnly;
							values.materialSizes = {{"studio/grid", {128, 64}}, {"studio/shader", {64, 32}}, {"studio/animated", {32, 64}}};
							const LevelMaterialTarget hit = patchHit ? LevelMaterialTarget{LevelMaterialKind::Patch, 0, 0} : LevelMaterialTarget{LevelMaterialKind::BrushFace, 1, targetFace.faceIndex};
							auto reference = shell.levelDocument();
							ok &= prepareLevelSurfaceTransfer(reference, {hit}, shell.levelSurfaceClipboard(), values, &plan, &error) && commitLevelSurfaceEdit(&reference, plan, &error);
							ok &= expect(view->materialGestureAt(patchHit ? patchPoint : targetPoint, Qt::MiddleButton, Qt::ShiftModifier | (mappingOnly ? Qt::AltModifier : Qt::NoModifier) | (project ? Qt::ControlModifier : Qt::NoModifier)), "native values/project gesture dispatch");
							ok &= expect(until([&] { return !shell.levelSurfaceEditsPending(); }) && serializeLevelMap(shell.levelDocument()).bytes == serializeLevelMap(reference).bytes,
								"native values applies hit plus mixed selection atomically", panel->findChild<QLabel*>("surfaceQuickStatus")->text());
							action("map.undo")->trigger(); ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == original && settled(*view), "mixed native values exact undo");
						}
					} }
					LevelSurfacePasteOptions onlyWrap = wrap; onlyWrap.mappingOnly = true; auto reference = shell.levelDocument();
					ok &= prepareLevelSurfacePaste(reference, {targetFace}, shell.levelSurfaceClipboard(), onlyWrap, &plan, &error) && commitLevelSurfaceEdit(&reference, plan, &error);
					ok &= expect(view->materialGestureAt(targetPoint, Qt::MiddleButton, Qt::ControlModifier | Qt::AltModifier)
						&& until([&] { return !shell.levelSurfaceEditsPending(); }) && serializeLevelMap(shell.levelDocument()).bytes == serializeLevelMap(reference).bytes, "native mapping-only wrap targets hit face", error);
					action("map.undo")->trigger(); action("map.selectNone")->trigger(); ok &= settled(*view);
					ok &= view->materialGestureAt(sourcePoint, Qt::MiddleButton, Qt::NoModifier);
				}
				for (bool wholeBrush : {false, true}) {
					if (wholeBrush && (net || nrc)) { continue; }
					auto reference = shell.levelDocument(); QVector<LevelSurfaceFace> faces{targetFace};
					if (wholeBrush) { faces.clear(); for (int i = 0; i < reference.brushes[1].faces.size(); ++i) { faces << LevelSurfaceFace{1, i}; } }
					const auto options = nrc ? wrap : LevelSurfacePasteOptions{};
					ok &= prepareLevelSurfacePaste(reference, faces, shell.levelSurfaceClipboard(), options, &plan, &error) && commitLevelSurfaceEdit(&reference, plan, &error);
					const auto modifiers = net ? Qt::KeyboardModifiers(Qt::ShiftModifier) : (nrc || wholeBrush) ? Qt::KeyboardModifiers(Qt::ControlModifier) : Qt::ControlModifier | Qt::ShiftModifier;
					ok &= expect(view->materialGestureAt(targetPoint, Qt::MiddleButton, modifiers) && shell.levelSurfaceEditsPending(), "native paste chord dispatches");
					ok &= expect(until([&] { return !shell.levelSurfaceEditsPending(); }) && serializeLevelMap(shell.levelDocument()).bytes == serializeLevelMap(reference).bytes, "native chord targets exact face or brush", error);
					if (nrc) {
						LevelSurfaceClipboard chained; ok &= copyLevelSurface(reference, targetFace, &chained, &error);
						ok &= expect(serializeLevelSurfaceClipboard(shell.levelSurfaceClipboard()) == serializeLevelSurfaceClipboard(chained), "native wrap advances source only after publication");
						const LevelSurfaceFace nextFace{1, (targetFace.faceIndex + 2) % 6}; auto chainedMap = reference;
						ok &= prepareLevelSurfacePaste(chainedMap, {nextFace}, chained, wrap, &plan, &error) && commitLevelSurfaceEdit(&chainedMap, plan, &error);
						ok &= shell.pasteLevelSurfaceSettings({nextFace}, wrap, &error);
						ok &= expect(until([&] { return !shell.levelSurfaceEditsPending(); }) && serializeLevelMap(shell.levelDocument()).bytes == serializeLevelMap(chainedMap).bytes,
							"second wrap uses the newly pasted source", error);
						action("map.undo")->trigger(); ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == serializeLevelMap(reference).bytes, "second wrap is its own undo step");
					}
					action("map.undo")->trigger(); ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == original && settled(*view), "native paste undo and preview");
				}
				ok &= view->materialGestureAt(targetPoint, Qt::MiddleButton, Qt::NoModifier);
				const auto clipboardBeforeLock = serializeLevelSurfaceClipboard(shell.levelSurfaceClipboard());
				ok &= view->materialGestureAt(sourcePoint, Qt::MiddleButton, net ? Qt::ShiftModifier : Qt::ControlModifier);
				ok &= expect(until([&] { return !shell.levelSurfaceEditsPending(); }) && serializeLevelMap(shell.levelDocument()).bytes == original
					&& panel->findChild<QLabel*>("surfaceQuickStatus")->text().contains("lock", Qt::CaseInsensitive)
					&& serializeLevelSurfaceClipboard(shell.levelSurfaceClipboard()) == clipboardBeforeLock, "native paste honors locks without advancing clipboard");
			}
			ok &= shell.copyLevelSurfaceSettings({0, 0}, &error);
			shell.openPathFromCommandLine(matrixPath);
			ok &= expect(until([&] { return shell.levelDocument().sourcePath == matrixPath && view->isEnabled(); }) && shell.levelSurfaceClipboard().ready(), "clipboard survives document handoff");
			ok &= inspect(shell, 0, 0); panel->findChild<QComboBox*>("surfaceQuickTarget")->setCurrentIndex(1);
			panel->findChild<QComboBox*>("surfacePasteMode")->setCurrentIndex(1);
			ok &= until([&] { return action("map.pasteSurface")->isEnabled(); }); action("map.pasteSurface")->trigger();
			ok &= expect(until([&] { return !shell.levelSurfaceEditsPending(); }) && shell.levelDocument().undoStack.size() == 1
				&& shell.levelDocument().brushes[0].faces[0].surfaceFlags == 64, "cross-format GUI projection resolves package dimensions", panel->findChild<QLabel*>("surfaceQuickStatus")->text());
			ok &= shell.copyLevelSurfaceSettings({0, 1}, &error);
			action("map.undo")->trigger(); // Leave the matrix map clean before testing another document.
			shell.openPathFromCommandLine(classicPath);
			ok &= until([&] { return shell.levelDocument().sourcePath == classicPath && view->isEnabled(); });
			ok &= expect(shell.levelDocument().engineFamily.compare("idTech3", Qt::CaseInsensitive) == 0, "classic Q3 source retains explicit engine asset rules");
			ok &= inspect(shell, 0, 1); const auto classicBytes = serializeLevelMap(shell.levelDocument()).bytes;
			ok &= until([&] { return action("map.pasteSurface")->isEnabled(); }); action("map.pasteSurface")->trigger();
			ok &= expect(until([&] { return !shell.levelSurfaceEditsPending(); }) && serializeLevelMap(shell.levelDocument()).bytes == classicBytes
				&& panel->findChild<QLabel*>("surfaceQuickStatus")->text().contains("Valve 220"), "GUI conversion needs explicit opt-in");
			panel->findChild<QCheckBox*>("surfacePasteAllowValve")->setChecked(true); action("map.pasteSurface")->trigger();
			ok &= expect(until([&] { return !shell.levelSurfaceEditsPending(); }) && shell.levelDocument().undoStack.size() == 1
				&& panel->findChild<QLabel*>("surfaceQuickStatus")->text().contains("throughout"), "GUI reports map-wide conversion", panel->findChild<QLabel*>("surfaceQuickStatus")->text());
			for (const auto& face : shell.levelDocument().brushes[0].faces) { ok &= expect(face.explicitTextureAxes, "GUI map dialect is consistent"); }
			action("map.undo")->trigger(); ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == classicBytes, "GUI conversion and paste share exact undo");
			for (const auto& name : {"map.copySurface", "map.pasteSurface"}) {
				auto* button = panel->findChild<QToolButton*>(QString::fromLatin1(name));
				ok &= expect(button && !button->accessibleName().isEmpty() && button->focusPolicy() != Qt::NoFocus, "clipboard commands expose keyboard and accessible controls");
			}
			auto* conversion = panel->findChild<QCheckBox*>("surfacePasteAllowValve");
			panel->findChild<QComboBox*>("surfacePasteMode")->setCurrentIndex(2);
			ok &= expect(panel->pasteOptions().mode == LevelSurfacePasteMode::Seamless && panel->pasteOptions().allowValve220, "wrap shares explicit conversion consent");
			ok &= expect(conversion->isEnabled() && QAccessible::queryAccessibleInterface(conversion)->role() == QAccessible::CheckBox, "conversion is an explicit native control");
			auto* mappingOnly = panel->findChild<QCheckBox*>("surfacePasteMappingOnly"); mappingOnly->setChecked(true);
			panel->findChild<QComboBox*>("surfacePasteMode")->setCurrentIndex(3);
			ok &= expect(panel->pasteOptions().mode == LevelSurfacePasteMode::RadiantValues && panel->pasteOptions().mappingOnly && !panel->pasteOptions().allowValve220
				&& !conversion->isEnabled() && mappingOnly->focusPolicy() != Qt::NoFocus && !mappingOnly->accessibleName().isEmpty()
				&& QAccessible::queryAccessibleInterface(mappingOnly)->role() == QAccessible::CheckBox, "Radiant values and mapping-only have accessible keyboard controls");
			panel->findChild<QComboBox*>("surfacePasteMode")->setCurrentIndex(4);
			ok &= expect(panel->pasteOptions().mode == LevelSurfacePasteMode::RadiantProject && panel->pasteOptions().mappingOnly && !conversion->isEnabled(), "Radiant projection is available in every profile without format conversion");
			app.processEvents(QEventLoop::ExcludeUserInputEvents); panel->verticalScrollBar()->setValue(0);
			{ QEventLoop loop; QTimer::singleShot(75, &loop, &QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents); }
			ok &= expect(panel->horizontalScrollBar()->maximum() == 0, "clipboard controls fit scaled expanded RTL panel");
			const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
			if (!captures.isEmpty()) {
				QDir().mkpath(captures); QImage image(panel->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); panel->render(&image);
				ok &= image.save(QDir(captures).filePath(QStringLiteral("surface-clipboard-%1.png").arg(scale)));
			}
		}
		if (scale == 200) { app.removeTranslator(&expansion); }
	}
	std::cout << (ok ? "Surface clipboard UI checks passed.\n" : "Surface clipboard UI checks failed.\n"); return ok ? 0 : 1;
}
