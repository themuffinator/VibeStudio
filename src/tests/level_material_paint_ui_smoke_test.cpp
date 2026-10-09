#include "app/application_shell.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "tests/level_material_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
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
	QElapsedTimer elapsed; elapsed.start();
	while (!ready() && elapsed.elapsed() < 45000) {
		QEventLoop events; QTimer::singleShot(20, &events, &QEventLoop::quit); events.exec();
	}
	return ready();
}
class Expansion final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		if (!QByteArray(context).endsWith("ApplicationShell")) { return {}; }
		const auto value = QString::fromUtf8(source);
		if (QStringList{"Material", "Navigate", "Paint", "Sample", "Targets", "Paint Targets", "Sample Target", "Cancel Stroke"}.contains(value)) {
			return QStringLiteral("[%1 %2]").arg(value, QString(value.size() / 2, '~'));
		}
		return {};
	}
};
} // namespace

int main(int argc, char** argv)
{
	// Semantic Qt APIs and widget render targets only. No injected input.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	// Draws in 3D: skip where no OpenGL or Vulkan renderer starts.
	if (const int skip = vibestudio::test_support::exitCodeWithoutRenderer("level-material-paint-ui-smoke"); skip >= 0) {
		return skip;
	}
	QTemporaryDir temp;
	bool ok = temp.isValid();
	QString error;
	StudioSettings::setOverrideFilePath(QDir(temp.path()).filePath(QStringLiteral("settings.ini")));
	StudioSettings settings; settings.setReducedMotion(true);
	LevelMapDocument fixture;
	ok &= expect(tests::createMaterialFixture(temp.path(), &fixture, &error), "fixture", error);
	LevelDocumentSaveRequest save; save.path = QDir(temp.path()).filePath(QStringLiteral("fixture.map"));
	ok &= expect(writeLevelDocument(fixture, save).succeeded(), "write fixture");
	PackageArchive archive;
	ok &= archive.load(QDir(temp.path()).filePath(QStringLiteral("assets")), &error);
	const auto assets = resolveLevelPreviewAssets(fixture, archive);
	for (int scale : {100, 200}) {
		std::cerr << "Paint UI scale " << scale << std::endl;
		const auto theme = scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark;
		settings.setTheme(theme); settings.setTextScalePercent(scale); settings.sync();
		applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, scale));
		Expansion expansion;
		if (scale == 200) { app.installTranslator(&expansion); }
		ApplicationShell shell;
		const int requestedWidth = scale == 100 ? 1600 : 2400;
		shell.resize(requestedWidth, scale == 100 ? 1000 : 1600);
		if (scale == 200) { shell.setLayoutDirection(Qt::RightToLeft); }
		shell.show();
		shell.openPathFromCommandLine(archive.sourcePath());
		shell.openPathFromCommandLine(save.path);
		auto* view = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
		auto* mode = shell.findChild<QComboBox*>(QStringLiteral("levelMaterialTool"));
		auto* material = shell.findChild<QComboBox*>(QStringLiteral("levelPaintMaterial"));
		auto* target = shell.findChild<QLineEdit*>(QStringLiteral("levelMaterialTargets"));
		auto* status = shell.findChild<QLabel*>(QStringLiteral("levelMaterialPaintStatus"));
		auto* panel = shell.findChild<QWidget*>(QStringLiteral("levelMaterialTools"));
		if (!expect(view && mode && material && target && status && panel, "material controls exist")) { return 1; }
		if (scale == 200) { ok &= expect(mode->itemText(1).startsWith('['), "expanded translation is actually installed"); }
		shell.findChild<QAction*>(QStringLiteral("map.paintMaterial"))->trigger();
		ok &= expect(until([&] { return view->isEnabled() && view->hasMesh() && view->hasSkin(); }), "live shell preview loaded");
		auto controls = view->cameraControls(); controls.perspective = true;
		controls.lookHoldKey = QStringLiteral("Space"); controls.lookPanUsesButtons = true;
		view->setCameraControls(controls); view->setCameraView({-180, -230, 180}, 52, -28);
		ok &= expect(tests::settleModelViewport(*view), "settled camera");
		const auto mesh = buildLevelMapPreviewMesh(shell.levelDocument(), {.modelMeshes = assets.models, .textureSizes = levelPreviewTextureSizes(assets)});
		QHash<LevelMaterialTarget, QPointF> points;
		QPointF modelPoint(-1, -1);
		for (int y = 40; y < view->height() - 20; y += 12) {
			for (int x = 20; x < view->width() - 20; x += 12) {
				const auto hit = view->hitAt(QPointF(x, y));
				if (!hit.valid || hit.triangle >= mesh.materialTargets.size()) { continue; }
				const auto paintTarget = mesh.materialTargets[hit.triangle];
				if (paintTarget.kind == LevelMaterialKind::None) { modelPoint = {double(x), double(y)}; }
				else if (!points.contains(paintTarget)) { points.insert(paintTarget, QPointF(x, y)); }
			}
		}
		if (!expect(points.size() >= 2, "two distinct visible source surfaces")) { return 1; }
		auto hit = points.cbegin(); const auto first = hit.key(); const auto firstPoint = hit.value(); ++hit;
		const auto second = hit.key(); const auto secondPoint = hit.value();
		shell.chooseLevelPaintMaterial(QStringLiteral("studio/editor"));
		const auto before = shell.levelDocument(); const auto beforeBytes = serializeLevelMap(before).bytes;
		ok &= expect(view->beginSurfaceStroke(firstPoint) && view->extendSurfaceStroke(firstPoint) && view->extendSurfaceStroke(secondPoint), "semantic drag hits real surfaces");
		view->setLooking(true); view->setTemporaryLooking(true);
		ok &= expect(view->surfaceStrokeActive() && !view->isLooking() && !view->isTemporarilyLooking(), "toggle and hold navigation cannot take over a pending material stroke");
		ok &= expect(shell.levelDocument().revision == before.revision && serializeLevelMap(shell.levelDocument()).bytes == beforeBytes, "stroke is staged until release");
		// Real event-loop/layout/raster work must not cancel an otherwise stable stroke.
		app.processEvents();
		ok &= expect(tests::settleModelViewport(*view) && view->surfaceStrokeActive(), "stroke survives paint and layout events", status->text());
		const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
		if (!captures.isEmpty()) {
			QDir().mkpath(captures);
			QImage image(shell.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); shell.render(&image);
			ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("paint-stroke-%1.png").arg(scale))), "capture real pending stroke");
		}
		view->finishSurfaceStroke(true);
		ok &= expect(shell.levelDocument().undoStack.size() == before.undoStack.size() + 1 && shell.levelDocument().selection == before.selection, "single command preserves selection", status->text());
		for (const auto& selected : {first, second}) {
			QString sampled;
			ok &= expect(sampleLevelMaterial(shell.levelDocument(), selected, &sampled, &error) && sampled == QStringLiteral("studio/editor"), "painted exact camera source", error);
		}
		ok &= expect(until([&] { return view->isEnabled() && view->hasSkin(); }) && tests::settleModelViewport(*view), "package images reload after stroke");
		const auto edited = serializeLevelMap(shell.levelDocument()).bytes;
		shell.findChild<QAction*>(QStringLiteral("map.undo"))->trigger();
		ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == beforeBytes, "shell undo exact");
		ok &= expect(until([&] { return view->isEnabled(); }) && tests::settleModelViewport(*view), "undo preview settles");
		shell.findChild<QAction*>(QStringLiteral("map.redo"))->trigger();
		ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == edited, "shell redo exact");
		ok &= expect(until([&] { return view->isEnabled(); }) && tests::settleModelViewport(*view), "redo preview settles");
		const auto revision = shell.levelDocument().revision;
		ok &= expect(view->beginSurfaceStroke(firstPoint), "begin no-op stroke"); view->finishSurfaceStroke(true);
		ok &= expect(shell.levelDocument().revision == revision, "no-op leaves revision unchanged");
		ok &= tests::settleModelViewport(*view);
		shell.chooseLevelPaintMaterial(QStringLiteral("studio/grid"));
		ok &= view->beginSurfaceStroke(firstPoint); view->finishSurfaceStroke(false);
		ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == edited, "cancel leaves map unchanged");
		ok &= tests::settleModelViewport(*view);
		ok &= view->beginSurfaceStroke(firstPoint); view->setEnabled(false); view->finishSurfaceStroke(true); view->setEnabled(true);
		ok &= expect(!view->surfaceStrokeActive() && shell.levelDocument().revision == revision, "preview invalidation cancels pending edit");
		ok &= tests::settleModelViewport(*view);
		ok &= view->beginSurfaceStroke(firstPoint); view->setCameraView({-180, -230, 180}, 53, -28); view->finishSurfaceStroke(true);
		ok &= expect(shell.levelDocument().revision == revision, "camera mutation cancels stroke");
		view->setCameraView({-180, -230, 180}, 52, -28); ok &= tests::settleModelViewport(*view);
		mode->setCurrentIndex(2);
		ok &= expect(view->sampleSurfaceAt(firstPoint) && shell.levelPaintMaterial() == QStringLiteral("studio/editor") && shell.levelDocument().revision == revision, "sample uses provenance without editing");
		mode->setCurrentIndex(1); shell.chooseLevelPaintMaterial(QStringLiteral("studio/grid"));
		ok &= tests::settleModelViewport(*view);
		if (modelPoint.x() >= 0) {
			ok &= view->beginSurfaceStroke(modelPoint); view->finishSurfaceStroke(true);
			ok &= expect(shell.levelDocument().revision == revision, "placed model skipped without source mutation");
		}
		ok &= tests::settleModelViewport(*view);
		ok &= expect(view->beginSurfaceStroke(firstPoint), "begin before external map edit");
		ok &= expect(shell.applyLevelMaterialPaint({second}, QStringLiteral("studio/shader"), &error), "other map edit while stroke pending", error);
		view->finishSurfaceStroke(true);
		ok &= expect(!view->surfaceStrokeActive() && shell.levelDocument().revision == revision + 1, "map edit cancels stale pending stroke");
		shell.chooseLevelPaintMaterial(QStringLiteral("studio/grid"));
		shell.findChild<QToolButton*>(QStringLiteral("levelMaterialTargetsToggle"))->setChecked(true);
		target->setText(levelMaterialTargetId(first) + ',' + levelMaterialTargetId(second));
		shell.findChild<QToolButton*>(QStringLiteral("levelPaintTargets"))->click();
		ok &= expect(shell.levelDocument().revision == revision + 2, "native target control uses same atomic transaction", status->text());
		target->setText(levelMaterialTargetId(first)); shell.findChild<QToolButton*>(QStringLiteral("levelSampleTarget"))->click();
		ok &= expect(shell.levelPaintMaterial() == QStringLiteral("studio/grid"), "native target sample");
		ok &= expect(QAccessible::queryAccessibleInterface(mode)->role() == QAccessible::ComboBox && mode->focusPolicy() != Qt::NoFocus && !target->accessibleName().isEmpty(), "keyboard focus and accessibility metadata");
		ok &= expect(shell.width() <= requestedWidth && panel->width() <= shell.width(), "expanded controls fit shell width");
		ok &= expect(until([&] { return view->isEnabled(); }) && tests::settleModelViewport(*view), "final preview ready");
		if (!captures.isEmpty()) {
			QImage image(shell.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); shell.render(&image);
			ok &= image.save(QDir(captures).filePath(QStringLiteral("paint-targets-%1.png").arg(scale)));
		}
		const auto output = QDir(temp.path()).filePath(QStringLiteral("painted-%1.map").arg(scale));
		ok &= expect(shell.saveLevelDocument(output, false, &error), "shell painted map save", error);
		LevelMapDocument reopened;
		ok &= expect(loadLevelMap({output, {}, {}}, &reopened, &error) && serializeLevelMap(reopened).bytes == serializeLevelMap(shell.levelDocument()).bytes, "saved map roundtrip", error);
		ok &= expect(status->textFormat() == Qt::PlainText, "material names and errors remain plain text");
		ok &= expect(until([&] { return view->isEnabled(); }) && tests::settleModelViewport(*view), "save preview settles");
		mode->setCurrentIndex(1);
		ok &= expect(view->beginSurfaceStroke(firstPoint), "pending stroke at shell teardown");
		if (scale == 200) { app.removeTranslator(&expansion); }
	}
	std::cout << (ok ? "Material paint UI checks passed.\n" : "Material paint UI checks failed.\n");
	return ok ? 0 : 1;
}
