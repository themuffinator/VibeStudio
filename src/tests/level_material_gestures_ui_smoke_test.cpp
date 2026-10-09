#include "app/application_shell.h"
#include "app/level_gestures_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_gestures.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "tests/level_material_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
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
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < 30000) {
		QEventLoop loop; QTimer::singleShot(20, &loop, &QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return ready();
}
bool settle(ModelViewport& view)
{
	return until([&] {
		QImage image(view.size(), QImage::Format_ARGB32_Premultiplied); view.render(&image);
		return view.isEnabled() && !view.isRendering();
	});
}
struct Targets {
	QHash<LevelMaterialTarget, QPointF> surfaces;
	QPointF model{-1, -1};
};
Targets visibleTargets(ModelViewport& view, const LevelMapPreviewMesh& mesh)
{
	Targets result;
	for (int y = 35; y < view.height() - 25; y += 6) {
		for (int x = 25; x < view.width() - 25; x += 6) {
			const auto hit = view.hitAt(QPointF(x, y));
			if (!hit.valid || hit.triangle < 0 || hit.triangle >= mesh.materialTargets.size()) { continue; }
			const auto target = mesh.materialTargets[hit.triangle];
			if (target.kind == LevelMaterialKind::None) { result.model = QPointF(x, y); }
			else if (!result.surfaces.contains(target)) { result.surfaces.insert(target, QPointF(x, y)); }
		}
	}
	return result;
}
class Expansion final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		if (QByteArray(context) != "LevelGestures") { return {}; }
		const auto value = QString::fromUtf8(source);
		if (!value.contains("material", Qt::CaseInsensitive)) { return {}; }
		return QStringLiteral("[%1 %2]").arg(value, QString(value.size() / 2, '~'));
	}
};
bool inspectPreferences(QApplication& app, StudioSettings& settings, int scale)
{
	bool ok = true;
	LevelGesturesDialog dialog(settings, "gtkradiant-1-6");
	dialog.resize(scale == 100 ? 780 : 1000, scale == 100 ? 740 : 1050);
	if (scale == 200) { dialog.setLayoutDirection(Qt::RightToLeft); }
	dialog.show();
	auto* tabs = dialog.findChild<QTabWidget*>("gestureTabs");
	auto* sample = dialog.findChild<QComboBox*>("gesture.camera.materialSampleButton");
	auto* paintModifiers = dialog.findChild<QComboBox*>("gesture.camera.materialPaintModifiers");
	auto* apply = dialog.findChild<QPushButton*>("gestureApply");
	if (!expect(tabs && sample && paintModifiers && apply, "new preferences use real dialog")) { return false; }
	for (int i = 0; i < tabs->count(); ++i) { if (tabs->widget(i)->isAncestorOf(sample)) { tabs->setCurrentIndex(i); break; } }
	paintModifiers->setCurrentIndex(paintModifiers->findData(QStringLiteral("none")));
	ok &= expect(!apply->isEnabled(), "ambiguous sample/paint blocked in live dialog");
	paintModifiers->setCurrentIndex(paintModifiers->findData(QStringLiteral("ctrl+shift")));
	ok &= expect(apply->isEnabled(), "distinct material chord accepted");
	for (const auto& id : {"materialSampleButton", "materialSampleModifiers", "materialPaintButton", "materialPaintModifiers"}) {
		auto* field = dialog.findChild<QComboBox*>(QStringLiteral("gesture.camera.") + QLatin1String(id));
		ok &= expect(field && !field->accessibleName().isEmpty() && field->focusPolicy() != Qt::NoFocus
			&& QAccessible::queryAccessibleInterface(field)->role() == QAccessible::ComboBox, "material preference keyboard and accessible metadata");
	}
	app.processEvents(QEventLoop::ExcludeUserInputEvents);
	for (auto* scroll : dialog.findChildren<QScrollArea*>()) {
		if (scroll->isAncestorOf(paintModifiers)) { scroll->ensureWidgetVisible(paintModifiers); }
	}
	// Wrapped labels post form-layout work after a scroll/resize. Let that
	// finish before sizing a render target or checking the dialog's footer.
	{ QEventLoop loop; QTimer::singleShot(75, &loop, &QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents); }
	ok &= expect(dialog.rect().contains(QRect(apply->mapTo(&dialog, QPoint()), apply->size())), "scaled material preference footer fits the dialog");
	const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
	if (!captures.isEmpty()) {
		QDir().mkpath(captures); QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); dialog.render(&image);
		ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("material-preferences-%1.png").arg(scale))), "render preference widget target");
	}
	apply->click();
	ok &= expect(settings.effectiveLevelEditorControls("gtkradiant-1-6").camera.materialPaintModifiers == (Qt::ControlModifier | Qt::ShiftModifier), "dialog persists material chord");
	paintModifiers->setCurrentIndex(0);
	auto* pan = dialog.findChild<QComboBox*>("gesture.camera.panButtons");
	pan->setCurrentIndex(pan->findData(QStringLiteral("middle")));
	ok &= expect(apply->isEnabled() && dialog.findChild<QLabel*>("gestureStatus")->text().contains("disable"), "navigation priority is explained before applying");
	apply->click();
	ok &= expect(settings.editorGestureOverrides("gtkradiant-1-6").value("camera.materialSampleButton") == "none"
		&& settings.effectiveLevelEditorControls("gtkradiant-1-6").camera.panButtons.contains(Qt::MiddleButton), "dialog preserves navigation and records inherited action as disabled");
	settings.setEditorGestureOverrides("gtkradiant-1-6", {});
	return ok;
}
}

int main(int argc, char** argv)
{
	// Semantic APIs and offscreen QWidget render targets; no injected user input.
	qputenv("QT_QPA_PLATFORM", "offscreen"); qputenv("QT_SCALE_FACTOR", "1");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
	// Draws in 3D: skip where no OpenGL or Vulkan renderer starts.
	if (const int skip = vibestudio::test_support::exitCodeWithoutRenderer("level-material-gestures-ui-smoke"); skip >= 0) {
		return skip;
	}
#ifdef Q_OS_WIN
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini")); StudioSettings settings;
	settings.setRestoreSession(false); settings.setReducedMotion(true);
	QString error, layer; LevelMapDocument fixture;
	bool ok = expect(tests::createMaterialFixture(temp.path(), &fixture, &error), "material fixture", error);
	ok &= expect(createLevelSceneNode(&fixture, LevelSceneNodeKind::Layer, "Protected", {}, &layer, &error)
		&& assignLevelSceneObjects(&fixture, layer, {"brush:1"}, &error) && setLevelSceneLocked(&fixture, layer, true, &error), "locked visible brush fixture", error);
	const auto path = temp.filePath("materials.map");
	LevelDocumentSaveRequest save; save.path = path;
	ok &= expect(writeLevelDocument(fixture, save).succeeded(), "write fixture with scene locks");
	PackageArchive archive; ok &= expect(archive.load(temp.filePath("assets"), &error), "generated fixture package", error);
	const auto assets = resolveLevelPreviewAssets(fixture, archive);
	if (!ok) { return 1; }
	for (int scale : {100, 200}) {
		std::cerr << "Material gestures scale " << scale << std::endl;
		settings.setSelectedEditorProfileId("q3radiant"); settings.setTextScalePercent(scale);
		settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark); settings.sync();
		applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, scale));
		Expansion expansion; if (scale == 200) { app.installTranslator(&expansion); }
		ok &= inspectPreferences(app, settings, scale);
		{
			ApplicationShell shell; shell.resize(scale == 100 ? 1700 : 2500, scale == 100 ? 1150 : 1750);
			if (scale == 200) { shell.setLayoutDirection(Qt::RightToLeft); }
			shell.show();
			for (auto* combo : shell.findChildren<QComboBox*>()) {
				if (combo->accessibleName() == "Level map engine hint") { combo->setCurrentIndex(combo->findData(QStringLiteral("idTech3"))); }
			}
			shell.openPathFromCommandLine(archive.sourcePath()); shell.openPathFromCommandLine(path);
			shell.findChild<QAction*>("shell.mode.levels")->trigger();
			auto* view = shell.findChild<ModelViewport*>("mapPreview3D");
			auto* mode = shell.findChild<QComboBox*>("levelMaterialTool");
			auto* profiles = shell.findChild<QComboBox*>("editorProfileCombo");
			auto* status = shell.findChild<QLabel*>("levelMaterialPaintStatus");
			if (!expect(view && mode && profiles && status, "live material controls")) { return 1; }
			if (!expect(until([&] { return view->isEnabled() && view->hasMesh() && view->hasSkin() && shell.levelDocument().scene.nodes.size() == 1; }), "map and assets loaded")) { return 1; }
			shell.findChild<QAction*>("map.selectAll")->trigger();
			const auto mesh = buildLevelMapPreviewMesh(shell.levelDocument(), {.modelMeshes = assets.models, .textureSizes = levelPreviewTextureSizes(assets)});
			for (const auto& profile : {"q3radiant", "gtkradiant-1-6", "netradiant", "netradiant-custom"}) {
				profiles->setCurrentIndex(profiles->findData(QLatin1String(profile)));
				view->setCameraView({-180, -230, 180}, 52, -28);
				if (!expect(settle(*view), "profile camera settled", QLatin1String(profile))) { return 1; }
				const auto targets = visibleTargets(*view, mesh);
				LevelMaterialTarget face, locked, patch;
				for (const auto& target : targets.surfaces.keys()) {
					if (target.kind == LevelMaterialKind::BrushFace && target.objectId == 0) { face = target; }
					if (target.kind == LevelMaterialKind::BrushFace && target.objectId == 1) { locked = target; }
					if (target.kind == LevelMaterialKind::Patch) { patch = target; }
				}
				if (!expect(face.kind != LevelMaterialKind::None && locked.kind != LevelMaterialKind::None && patch.kind != LevelMaterialKind::None && targets.model.x() >= 0,
					"visible unlocked face, locked face, patch and placed model", QLatin1String(profile))) { return 1; }
				const auto point = targets.surfaces.value(face);
				const auto before = shell.levelDocument(); const auto bytes = serializeLevelMap(before).bytes;
				mode->setCurrentIndex(1); shell.chooseLevelPaintMaterial("studio/editor");
				QString sampled; sampleLevelMaterial(before, face, &sampled);
				ok &= expect(view->materialGesturesEnabled() && view->materialGestureAt(point, Qt::MiddleButton, Qt::NoModifier)
					&& shell.levelPaintMaterial() == sampled && mode->currentIndex() == 1 && view->surfaceTool() == ModelViewportSurfaceTool::Paint
					&& serializeLevelMap(shell.levelDocument()).bytes == bytes && shell.levelDocument().selection == before.selection, "sample shares picker without changing tool, selection or map", QLatin1String(profile));
				ok &= expect(!view->materialGestureAt(point, Qt::MiddleButton, Qt::AltModifier)
					&& !view->materialGestureAt(point, Qt::MiddleButton, Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier), "extra modifiers cannot fall through to sampling");
				ok &= expect(view->beginSurfaceStroke(point) && !view->materialGestureAt(point, Qt::MiddleButton, Qt::NoModifier) && view->surfaceStrokeActive(), "sampling leaves pending paint stroke intact");
				view->finishSurfaceStroke(false); mode->setCurrentIndex(0); ok &= settle(*view);
				mode->setCurrentIndex(3);
				ok &= expect(view->beginBrushDraw(point) && !view->materialGestureAt(point, Qt::MiddleButton, Qt::NoModifier)
					&& view->isDrawingBrush(), "sampling leaves an active brush draft intact");
				view->finishBrushDraw(false); mode->setCurrentIndex(0); ok &= settle(*view);
				view->setCameraView({-180, -230, 180}, 53, -28);
				ok &= expect(!view->materialGestureAt(point, Qt::MiddleButton, Qt::NoModifier) && shell.levelPaintMaterial() == sampled,
					"pending camera render refuses a stale hit");
				view->setCameraView({-180, -230, 180}, 52, -28); ok &= settle(*view);
				view->setLooking(true);
				ok &= expect(!view->materialGestureAt(point, Qt::MiddleButton, Qt::NoModifier) && view->isLooking(), "mouse look cannot trigger material actions");
				view->setLooking(false); ok &= settle(*view);
				if (view->cameraControls().driveButton != Qt::NoButton) {
					ok &= expect(view->beginPointerDrive(point)
						&& !view->materialGestureAt(point, Qt::MiddleButton, Qt::NoModifier) && view->isPointerDriving(),
						"material action leaves pointer steering active");
					view->endPointerDrive(); ok &= settle(*view);
				}
				view->setEnabled(false);
				ok &= expect(!view->materialGestureAt(point, Qt::MiddleButton, Qt::NoModifier), "disabled preview refuses material actions");
				view->setEnabled(true); ok &= settle(*view);
				ok &= expect(!view->materialGestureAt({-100, -100}, Qt::MiddleButton, Qt::NoModifier), "empty hit leaves material unchanged");
				ok &= expect(view->materialGestureAt(targets.model, Qt::MiddleButton, Qt::NoModifier)
					&& shell.levelPaintMaterial() == sampled && status->text().contains("Models"), "model provenance is never interpreted as a map face");
				if (view->cameraControls().materialPaintButton == Qt::NoButton) { continue; }
				shell.chooseLevelPaintMaterial("studio/editor");
				ok &= expect(view->materialGestureAt(targets.surfaces.value(locked), Qt::MiddleButton, Qt::ShiftModifier)
					&& serializeLevelMap(shell.levelDocument()).bytes == bytes && shell.levelDocument().revision == before.revision
					&& status->text().contains("lock", Qt::CaseInsensitive), "instant paint honors scene locks", status->text());
				LevelMapDocument expected = before; LevelMaterialPaintPlan plan;
				ok &= expect(prepareLevelMaterialPaint(expected, {face}, "studio/editor", &plan, &error) && commitLevelMaterialPaint(&expected, plan, &error), "reference material-only transaction", error);
				ok &= expect(view->materialGestureAt(point, Qt::MiddleButton, Qt::ShiftModifier)
					&& serializeLevelMap(shell.levelDocument()).bytes == serializeLevelMap(expected).bytes
					&& shell.levelDocument().undoStack.size() == before.undoStack.size() + 1 && shell.levelDocument().selection == before.selection
					&& mode->currentIndex() == 0, "one exact source face, one undo, unchanged UVs and tool", status->text());
				ok &= expect(until([&] { return view->isEnabled(); }) && settle(*view), "paint preview updates");
				const auto revision = shell.levelDocument().revision;
				ok &= expect(view->materialGestureAt(point, Qt::MiddleButton, Qt::ShiftModifier) && shell.levelDocument().revision == revision, "repeated instant paint creates no empty undo");
				shell.findChild<QAction*>("map.undo")->trigger();
				ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == bytes, "instant paint undo exact");
				ok &= expect(until([&] { return view->isEnabled(); }) && settle(*view), "undo preview updates");
				ok &= expect(view->materialGestureAt(targets.surfaces.value(patch), Qt::MiddleButton, Qt::ShiftModifier), "patch provenance is one paint target");
				QString patchMaterial; sampleLevelMaterial(shell.levelDocument(), patch, &patchMaterial);
				ok &= expect(patchMaterial == "studio/editor" && shell.levelDocument().selection == before.selection, "patch paint preserves scene selection");
				shell.findChild<QAction*>("map.undo")->trigger();
				ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == bytes, "patch undo exact");
				ok &= expect(until([&] { return view->isEnabled(); }) && settle(*view), "patch undo preview updates");
				ok &= expect(status->textFormat() == Qt::PlainText && !status->accessibleDescription().isEmpty(), "material status is accessible plain text");
			}
			profiles->setCurrentIndex(profiles->findData(QStringLiteral("trenchbroom")));
			ok &= expect(cameraMaterialGesture(view->cameraControls(), Qt::MiddleButton, Qt::NoModifier) == CameraMaterialGesture::None
				&& cameraNavigationDrag(view->cameraControls(), Qt::MiddleButton, Qt::NoModifier) == CameraNavigationDrag::Pan, "switching away restores native middle pan");
			ModelViewport modelBrowser;
			modelBrowser.setCameraControls(gtkRadiantLevelControls().camera);
			ok &= expect(!modelBrowser.materialGesturesEnabled() && !modelBrowser.materialGestureAt({0, 0}, Qt::MiddleButton, Qt::NoModifier), "shared Models viewport remains opted out");
		}
		if (scale == 200) { app.removeTranslator(&expansion); }
	}
	std::cout << (ok ? "Material gesture integration checks passed.\n" : "Material gesture integration checks failed.\n");
	return ok ? 0 : 1;
}
