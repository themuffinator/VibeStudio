#include "app/application_shell.h"
#include "app/level_sweep_canvas.h"
#include "app/level_sweep_dialog.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "tests/level_geometry_test_helpers.h"
#include "tests/level_material_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <iostream>
using namespace vibestudio;
namespace
{
bool ok = true;
bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << label << ": " << detail.toStdString() << '\n';
	}
	return value;
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 20000) {
		QEventLoop loop;
		QTimer::singleShot(10, &loop, &QEventLoop::quit);
		loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return ready();
}
QByteArray bytes(const LevelMapDocument& map)
{
	return serializeLevelMap(map).bytes;
}
int topFace(const LevelMapDocument& map)
{
	LevelBrushTopology topology;
	levelBrushTopology(map.brushes.first(), &topology);
	for (int i = 0; i < topology.faces.size(); ++i) {
		if (topology.geometry.faces[i].plane.normalZ > .99) {
			return i;
		}
	}
	return -1;
}
class Expansion final : public QTranslator
{
  public:
	bool isEmpty() const override
	{
		return false;
	}
	QString translate(const char*, const char* source, const char*, int) const override
	{
		const auto s = QString::fromUtf8(source);
		return '[' + s + QString(s.size() / 3, '~') + ']';
	}
};
void dialogChecks(QApplication& app, const QString& evidence)
{
	QString error;
	QTemporaryDir assets;
	expect(assets.isValid() && tests::putMaterialFile(assets.filePath("textures/studio/cache.png"), tests::materialImage(64, 64)),
	       "source material fixture");
	PackageArchive archive;
	PackageStagingModel staging;
	expect(archive.load(assets.path(), &error) && staging.loadBaseArchive(archive, &error) &&
	           staging.addBytes(tests::materialImage(128, 64, true), "textures/studio/sweep.png", &error),
	       "staged sweep material", error);
	const auto materialReader = std::make_shared<PackageStagingArchive>(staging);
	LevelMapDocument source;
	tests::createGeometryFixture(1, &source, &error);
	const auto before = bytes(source);
	LevelSweepRequest request;
	request.texture = "studio/sweep";
	request.faces = {{{LevelMapSelectionKind::QuakeBrush, 0}, LevelBrushComponent::Face, {topFace(source)}}};
	for (const int scale : {100, 200}) {
		StudioSettings settings;
		settings.setTheme(StudioTheme::HighContrastLight);
		settings.setReducedMotion(true);
		settings.sync();
		applyStudioTheme(app, studioThemeTokens(StudioTheme::HighContrastLight, UiDensity::Standard, scale));
		Expansion expansion;
		if (scale == 200) {
			app.installTranslator(&expansion);
		}
		QWidget owner;
		owner.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
		LevelSweepDialog dialog(source, request, materialReader, {}, &owner);
		dialog.show();
		auto* z = dialog.findChild<QDoubleSpinBox*>("sweepDeltaZ");
		auto* x = dialog.findChild<QDoubleSpinBox*>("sweepDeltaX");
		auto* segments = dialog.findChild<QSpinBox*>("sweepSegments");
		auto* iterations = dialog.findChild<QSpinBox*>("sweepIterations");
		auto* material = dialog.findChild<QLineEdit*>("sweepMaterial");
		auto* apply = dialog.findChild<QPushButton*>("sweepApply");
		auto* status = dialog.findChild<QLabel*>("sweepStatus");
		auto* view = dialog.findChild<QComboBox*>("sweepView");
		auto* path = dialog.findChild<QComboBox*>("sweepPath");
		auto* preview = dialog.findChild<ModelViewport*>("sweepPreview");
		if (!expect(z && x && segments && iterations && material && apply && status && view && path && preview, "sweep controls exist")) {
			return;
		}
		expect(!apply->isEnabled() && until([&] { return !status->text().contains("Checking") && !status->text().isEmpty(); }) &&
		           !dialog.isReady(),
		       "identity sweep refused visibly");
		z->setValue(64);
		segments->setValue(4);
		iterations->setValue(2);
		if (!expect(until([&] { return dialog.isReady() && preview->hasMesh(); }), "checked sweep preview", status->text())) {
			continue;
		}
		expect(dialog.layoutDirection() == owner.layoutDirection(), "RTL inheritance");
		for (auto* field : QVector<QWidget*>{z, x, segments, iterations, material, apply, view, path}) {
			auto* a = QAccessible::queryAccessibleInterface(field);
			expect(a && !a->text(QAccessible::Name).isEmpty() && field->focusPolicy() != Qt::NoFocus, "accessible keyboard control",
			       field->objectName());
		}
		view->setCurrentIndex(2);
		expect(preview->hasMesh() && preview->hasSkin(), "source material inspection");
		view->setCurrentIndex(1);
		expect(preview->hasMesh() && preview->hasSkin() && preview->renderMode() == ModelViewportRenderMode::Textured,
		       "staged result material inspection");
		view->setCurrentIndex(0);
		material->setText("invalid material");
		expect(!apply->isEnabled() && !preview->hasMesh(), "invalidate stale preview immediately");
		expect(until([&] { return !status->text().contains("Checking") && !status->text().isEmpty(); }) && !dialog.isReady(),
		       "invalid material refused");
		material->setText("studio/sweep");
		for (int i = 0; i < 20; ++i) {
			z->setValue(65 + i);
		}
		z->setValue(96);
		expect(until([&] { return dialog.isReady(); }), "latest change wins", status->text());
		int applications = 0;
		dialog.setApplyHandler([&](const LevelMapDocument& candidate, QString* issue) {
			auto expected = source;
			auto exact = request;
			exact.translation = {0, 0, 96, true};
			exact.segments = 4;
			exact.iterations = 2;
			if (!sweepLevelMap(&expected, exact, nullptr, issue)) {
				return false;
			}
			expect(bytes(candidate) == bytes(expected) && candidate.undoStack.size() == 1, "GUI matches service exactly with one undo");
			++applications;
			return true;
		});
		for (auto* scroll : dialog.findChildren<QScrollArea*>()) {
			expect(scroll->horizontalScrollBar()->maximum() == 0, "no horizontal control clipping");
		}
		expect(preview->width() >= 240 && preview->height() >= 240, "usable scaled preview");
		if (!evidence.isEmpty()) {
			expect(until([&] {
				       QImage frame(dialog.size(), QImage::Format_ARGB32_Premultiplied);
				       dialog.render(&frame);
				       return preview->hasMesh() && !preview->isRendering();
			       }),
			       "owned preview render settles");
			QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			dialog.render(&image);
			expect(image.save(QDir(evidence).filePath(QString("sweep-%1.png").arg(scale))), "save owned widget render");
		}
		dialog.setViewportMode();
		dialog.show();
		expect(until([&] { return !preview->isVisible(); }), "compact sweep palette leaves the map visible");
		for (auto* scroll : dialog.findChildren<QScrollArea*>()) {
			expect(scroll->horizontalScrollBar()->maximum() == 0, "compact controls avoid horizontal clipping");
		}
		if (!evidence.isEmpty()) {
			QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			dialog.render(&image);
			expect(image.save(QDir(evidence).filePath(QString("sweep-palette-%1.png").arg(scale))), "save native palette owned render");
		}
		auto* previewToggle = dialog.findChild<QPushButton*>("sweepShowPreview");
		expect(previewToggle && !previewToggle->accessibleName().isEmpty(), "keyboard-accessible textured preview toggle");
		if (previewToggle) {
			previewToggle->click();
			expect(preview->isVisible(), "textured preview opens from the compact palette");
		}
		dialog.accept();
		expect(applications == 1 && dialog.result() == QDialog::Accepted && bytes(source) == before,
		       "one publication and source immutability");
		if (scale == 200) {
			app.removeTranslator(&expansion);
		}
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	{
		LevelSweepDialog cancelled(source, request);
		cancelled.show();
		cancelled.findChild<QDoubleSpinBox*>("sweepDeltaZ")->setValue(128);
		cancelled.reject();
	}
	expect(bytes(source) == before && source.undoStack.isEmpty(), "cancel without publication");
}
void shellChecks(const QString& directory)
{
	QString error;
	LevelMapDocument source;
	tests::createGeometryFixture(1, &source, &error);
	const auto path = QDir(directory).filePath("source.map");
	expect(saveLevelMapAs(source, path).succeeded(), "save shell fixture");
	ApplicationShell shell;
	shell.resize(1600, 1000);
	shell.show();
	shell.openPathFromCommandLine(path);
	const auto trigger = [&](const char* name) {
		auto* a = shell.findChild<QAction*>(QString::fromLatin1(name));
		if (expect(a && a->isEnabled(), name)) {
			a->trigger();
		}
	};
	trigger("shell.mode.levels");
	auto* top = shell.findChild<MapViewport*>("mapViewport");
	auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
	if (!expect(top && camera && until([&] { return shell.levelDocument().brushes.size() == 1 && camera->hasMesh(); }), "shell ready")) {
		return;
	}
	trigger("map.selectAll");
	trigger("map.faceTool");
	const auto id = topFace(shell.levelDocument());
	const QVector<LevelMapComponentSet> picks{{{LevelMapSelectionKind::QuakeBrush, 0}, LevelBrushComponent::Face, {id}}};
	top->setPickedComponents(picks);
	top->componentPicksChanged(1);
	const auto before = bytes(shell.levelDocument());
	QTimer::singleShot(0, &shell, [&] {
		auto* d = static_cast<LevelSweepDialog*>(shell.findChild<QDialog*>("levelSweepDialog"));
		if (!expect(d, "sweep dialog opens")) {
			return;
		}
		d->findChild<QDoubleSpinBox*>("sweepDeltaZ")->setValue(64);
		d->findChild<QSpinBox*>("sweepSegments")->setValue(4);
		if (expect(until([&] { return d->isReady(); }), "shell candidate ready")) {
			d->accept();
		} else {
			d->reject();
		}
	});
	trigger("map.sweepFaces");
	expect(shell.levelDocument().brushes.size() == 5 && shell.levelDocument().undoStack.size() == 1 &&
	           shell.levelDocument().selection.size() == 4,
	       "shell creates and selects sweep atomically");
	trigger("map.undo");
	expect(bytes(shell.levelDocument()) == before, "shell exact undo");
	trigger("map.faceTool");
	top->setPickedComponents(picks);
	top->componentPicksChanged(1);
	QTimer::singleShot(0, &shell, [&] {
		auto* d = static_cast<LevelSweepDialog*>(shell.findChild<QDialog*>("levelSweepDialog"));
		if (!expect(d, "stale sweep dialog opens")) {
			return;
		}
		d->findChild<QDoubleSpinBox*>("sweepDeltaZ")->setValue(64);
		if (expect(until([&] { return d->isReady(); }), "stale candidate ready")) {
			top->setPickedComponents({});
			top->componentPicksChanged(0);
			d->accept();
			expect(d->isVisible(), "changed face picks refuse publication");
		}
		d->reject();
	});
	trigger("map.sweepFaces");
	expect(bytes(shell.levelDocument()) == before && shell.levelDocument().undoStack.isEmpty(),
	       "stale sweep preserves document and history");
	const auto choose = [&] {
		if (top->componentMode() != MapViewport::ComponentMode::Faces) {
			trigger("map.faceTool");
		}
		top->setPickedComponents(picks);
		top->componentPicksChanged(1);
	};
	choose();
	trigger("map.sweepTool");
	QPointer<LevelSweepDialog> native = shell.findChild<LevelSweepDialog*>("levelSweepDialog");
	auto* gizmo = shell.findChild<LevelSweepGizmo*>("levelSweepGizmo");
	if (!expect(native && gizmo, "native sweep opens modeless controls and shared destination")) {
		return;
	}
	expect(!native->isModal() && shell.findChildren<LevelSweepCanvas*>("levelSweepCanvas").size() == 4,
	       "all four editor views share the tool");
	int planBindings = 0, cameraBindings = 0;
	for (auto* canvas : shell.findChildren<LevelSweepCanvas*>("levelSweepCanvas")) {
		const auto frame = canvas->frame();
		if (qobject_cast<MapViewport*>(canvas->parentWidget())) {
			++planBindings;
			expect(frame.rotationAxis >= 0 && gizmo->rotationHandles(frame).size() == 1,
			       "native plan binding supplies the perpendicular ring");
		} else if (qobject_cast<ModelViewport*>(canvas->parentWidget())) {
			++cameraBindings;
			expect(frame.rotationAxis == -1 && gizmo->rotationHandles(frame).size() == 3, "native camera binding supplies three rings");
		}
	}
	expect(planBindings == 3 && cameraBindings == 1, "bindings target the actual three plans and level camera");
	expect(top->componentGizmo()->handles(top->componentGizmoFrame()).isEmpty(), "source manipulator is hidden during sweep");
	native->findChild<QDoubleSpinBox*>("sweepDeltaZ")->setValue(64);
	native->findChild<QSpinBox*>("sweepSegments")->setValue(4);
	expect(gizmo->request().translation.z == 64 && gizmo->request().segments == 4, "numeric controls update the shared cap");
	top->setGridSize(16);
	top->setSnapToGrid(true);
	gizmo->setSteps(16, 15);
	const auto viewFrame = top->componentGizmoFrame();
	const auto centre = viewFrame.project(gizmo->centre());
	expect(gizmo->begin(centre, viewFrame) && gizmo->updateDrag(centre + QPointF(40, 0)), "native plan cap drag");
	gizmo->finish(true);
	expect(bytes(shell.levelDocument()) == before && shell.levelDocument().undoStack.isEmpty(),
	       "release keeps source and undo history unchanged");
	expect(native->findChild<QDoubleSpinBox*>("sweepDeltaX")->value() == gizmo->request().translation.x, "cap updates numeric coordinates");
	trigger("map.sweepIncreaseScale");
	expect(native->findChild<QDoubleSpinBox*>("sweepScaleX")->value() > 1, "named scale action updates shared numeric draft");
	expect(until([&] { return native && native->isReady(); }), "native candidate validated asynchronously");
	top->hoverChanged(QStringLiteral("obsolete component hint"));
	const auto readouts = shell.findChildren<QLabel*>("viewportReadout");
	expect(std::any_of(readouts.cbegin(),readouts.cend(),[](const auto* label) {
		return label->isVisible() && label->toolTip().contains("Sweep destination");
	}), "sweep state takes priority over component hover hints");
	auto expected = shell.levelDocument();
	expect(sweepLevelMap(&expected, native->request(), nullptr, &error), "shared native service candidate", error);
	if (!qEnvironmentVariable("VIBESTUDIO_SWEEP_UI_EVIDENCE").isEmpty()) {
		// Frame the destination for the owned-widget evidence without sending
		// pointer or keyboard events, or changing document geometry.
		for (auto* canvas : shell.findChildren<LevelSweepCanvas*>("levelSweepCanvas")) {
			if (auto* plan = qobject_cast<MapViewport*>(canvas->parentWidget())) {
				for (int step = 0; step < 4; ++step) {
					plan->zoomOut();
				}
			}
		}
		camera->frameBounds({-48, -48, -24}, {80, 80, 112});
		expect(until([&] { return !camera->isRendering(); }), "camera evidence frame settles");
		QImage image(shell.size(), QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);
		shell.render(&image);
		expect(image.save(QDir(qEnvironmentVariable("VIBESTUDIO_SWEEP_UI_EVIDENCE")).filePath("sweep-views.png")),
		       "save owned map-view sweep render");
	}
	trigger("map.sweepApply");
	expect(bytes(shell.levelDocument()) == bytes(expected) && shell.levelDocument().undoStack.size() == 1,
	       "explicit native apply matches checked service with one undo");
	expect(until([&] { return native.isNull(); }), "completed tool and its worker are disposed");
	trigger("map.undo");
	expect(bytes(shell.levelDocument()) == before, "native sweep exact undo");
	choose();
	trigger("map.sweepTool");
	native = shell.findChild<LevelSweepDialog*>("levelSweepDialog");
	if (!expect(native, "native stale-context fixture")) {
		return;
	}
	native->findChild<QDoubleSpinBox*>("sweepDeltaZ")->setValue(64);
	expect(until([&] { return native && native->isReady(); }), "native stale draft ready");
	top->setPickedComponents({});
	top->componentPicksChanged(0);
	// The commit guard must reject immediately, before the periodic close runs.
	native->accept();
	expect(bytes(shell.levelDocument()) == before, "stale native apply cannot overwrite changed selection");
	expect(until([&] { return native.isNull(); }), "changed face selection closes native tool");
	expect(until([&] { return shell.findChildren<LevelSweepCanvas*>("levelSweepCanvas").isEmpty(); }),
	       "all view bindings detach when draft closes");
	choose();
	trigger("map.sweepTool");
	native = shell.findChild<LevelSweepDialog*>("levelSweepDialog");
	if (expect(native, "native cancellation fixture")) {
		trigger("map.sweepTool");
		expect(until([&] { return native.isNull(); }), "tool toggle cancels pending work");
	}
	expect(bytes(shell.levelDocument()) == before && shell.levelDocument().undoStack.isEmpty(),
	       "cancelled native drafts preserve source and history");
}
} // namespace
int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	StudioSettings settings;
	settings.setRestoreSession(false);
	settings.setReducedMotion(true);
	settings.setLevelViewLayoutPreference("four-views");
	settings.sync();
	dialogChecks(app, qEnvironmentVariable("VIBESTUDIO_SWEEP_UI_EVIDENCE"));
	shellChecks(temp.path());
	return ok ? 0 : 1;
}
