#include "tests/level_object_test_helpers.h"
using vibestudio::tests::objectIndex;
using vibestudio::tests::setObjectCurrentRow;
using vibestudio::tests::setObjectSelected;
#include "app/texture_editor_dialog.h"
#include "app/texture_canvas.h"
#include "app/asset_views.h"
#include "app/application_shell.h"
#include "app/studio_theme.h"
#include "core/level_patch.h"

#include <QAction>
#include <QAbstractButton>
#include <QAccessible>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFrame>
#include <QJsonObject>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QLineF>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QTimer>
#include <QTranslator>

#include <iostream>

using namespace vibestudio;
namespace {
class ExpandedTranslator final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		if (QByteArray(context) != "VibeStudioTextureEditor") { return {}; }
		return QStringLiteral("[%1 expanded]").arg(QString::fromUtf8(source));
	}
};
bool expect(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; } return value; }
bool settled(TextureEditorDialog* editor)
{
	QEventLoop loop; QTimer poll, timeout; timeout.setSingleShot(true);
	QObject::connect(&poll, &QTimer::timeout, &loop, [&]() { if (!editor->isBusy()) { loop.quit(); } });
	QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	poll.start(10); timeout.start(15000); if (editor->isBusy()) { loop.exec(); } return !editor->isBusy();
}
}

int main(int argc, char** argv)
{
	// Exercise commands and widget state, without synthetic keyboard/mouse input
	// or OS captures. Optional images are rendered by the widgets themselves.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QTemporaryDir temporary; if (!temporary.isValid()) { return EXIT_FAILURE; }
	QDir root(temporary.path()); bool ok = true; QString error;
	QElapsedTimer timer; timer.start();
	QJsonObject performance;
	const auto milestone = [&](const char* label) { std::cerr << label << ": " << timer.elapsed() << " ms\n"; };
	StudioSettings::setOverrideFilePath(root.filePath(QStringLiteral("texture-profile.ini")));
	PackageArchive archive; PackageStagingModel staging;
	ok &= expect(archive.load(root.path(), &error) && staging.loadBaseArchive(archive, &error), "open authoring package");
	for (int scale : {100, 200}) {
		ExpandedTranslator expanded;
		if (scale == 200) { app.installTranslator(&expanded); }
		applyStudioTheme(app, studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark, UiDensity::Standard, scale));
		auto* editor = new TextureEditorDialog;
		if (scale == 200) { editor->setLayoutDirection(Qt::RightToLeft); editor->resize(1450, 1000); }
		QString package = root.path();
		editor->context = [&]() { return TextureEditorContext{package, {}, true, false, {}, {}, {}}; };
		editor->handoff = [&](const TextureExportResult& result, const TextureExportOptions& options, const QString& path, bool replace, bool, QString* problem) { return stageTextureExport(result, options, path, &staging, replace, problem); };
		QImage image(16, 8, QImage::Format_ARGB32); image.fill(Qt::transparent);
		ok &= expect(editor->setImage(image, QStringLiteral("textures/test.png"), &error), "decoded pixels open for editing");
		editor->show(); editor->refreshContext(); app.processEvents();
		auto* canvas = editor->findChild<TextureCanvas*>(QStringLiteral("textureCanvas")); canvas->setHighContrast(scale == 200);
		ok &= expect(canvas && canvas->focusPolicy() == Qt::StrongFocus && !canvas->accessibleDescription().isEmpty(), "canvas exposes focus and keyboard operations");
		ok &= expect(QAccessible::queryAccessibleInterface(canvas)->role() == QAccessible::Graphic, "screen readers receive the canvas graphic role");
		auto* tool = editor->findChild<QComboBox*>(QStringLiteral("textureTool"));
		auto* brushWidth = editor->findChild<QSpinBox*>(QStringLiteral("textureBrushWidth"));
		auto* paintMode = editor->findChild<QComboBox*>(QStringLiteral("texturePaintMode"));
		auto* wrap = editor->findChild<QCheckBox*>(QStringLiteral("textureWrapPainting"));
		auto* filled = editor->findChild<QCheckBox*>(QStringLiteral("textureFilledShapes"));
		auto* tolerance = editor->findChild<QSpinBox*>(QStringLiteral("textureFillTolerance"));
		auto* color = editor->findChild<QLineEdit*>(QStringLiteral("textureColor"));
		ok &= expect(tool->count() == 9 && !paintMode->isEnabled() && !tolerance->isEnabled(), "tool inspector exposes new tools and only relevant settings");
		tool->setCurrentIndex(int(TextureCanvas::Tool::Brush)); brushWidth->setValue(3); color->setText(QStringLiteral("#80ff0000"));
		canvas->beginGesture({1, 1}); canvas->continueGesture({4, 1}); canvas->continueGesture({1, 1}); canvas->endGesture();
		ok &= expect(paintMode->isEnabled() && editor->document().image().pixelColor(3, 1) == QColor(255, 0, 0, 128), "UI blend brush applies opacity once across direct gesture commands");
		brushWidth->setValue(1); wrap->setChecked(true); canvas->setTiled(true);
		canvas->beginGesture({15, 7}); canvas->continueGesture({16, 7}); canvas->endGesture();
		ok &= expect(editor->document().image().pixelColor(15, 7) == QColor(255, 0, 0, 128) && editor->document().image().pixelColor(0, 7) == QColor(255, 0, 0, 128) && editor->document().image().pixelColor(8, 7).alpha() == 0,
			"canvas preserves unwrapped gesture coordinates without drawing an erroneous line across the middle");
		const QPointF anchor(canvas->width() * 0.27, canvas->height() * 0.37);
		const auto anchoredPixel = canvas->imagePositionAt(anchor);
		canvas->setZoomAt(canvas->zoom() * 1.25, anchor); canvas->setZoomAt(canvas->zoom() * 0.8, anchor);
		ok &= expect(QLineF(anchoredPixel, canvas->imagePositionAt(anchor)).length() < 0.000001, "zoom retains the exact fractional image coordinate beneath its anchor");
		wrap->setChecked(false); tool->setCurrentIndex(int(TextureCanvas::Tool::Rectangle)); filled->setChecked(true); color->setText(QStringLiteral("#ffffffff"));
		const auto shapeRevision = editor->document().revision();
		canvas->beginGesture({5, 2}); canvas->continueGesture({8, 5});
		ok &= expect(canvas->hasPendingShape() && editor->document().revision() == shapeRevision, "shape preview leaves the document unchanged until commit");
		canvas->endGesture();
		ok &= expect(editor->isBusy() && settled(editor) && editor->document().image().pixelColor(6, 3) == QColor(Qt::white), "shape release runs the shared recipe worker and commits filled pixels");
		editor->undo(); ok &= expect(editor->document().revision() == shapeRevision, "one shape is one undo command");
		tool->setCurrentIndex(int(TextureCanvas::Tool::Line)); canvas->setCursorPixel({5, 2}); canvas->applyAtCursor(); canvas->setCursorPixel({8, 5});
		ok &= expect(canvas->hasPendingShape() && editor->document().revision() == shapeRevision, "Space-style shape command begins without editing pixels");
		if (!qEnvironmentVariableIsEmpty("VIBESTUDIO_TEST_CAPTURE_ROOT")) {
			app.processEvents(); QImage capture(editor->size(), QImage::Format_ARGB32_Premultiplied); capture.fill(Qt::transparent); editor->render(&capture);
			ok &= expect(capture.save(QDir(qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT")).filePath(QStringLiteral("texture-tools-%1.png").arg(scale))), "render paint controls and a pending shape directly from widgets");
		}
		canvas->applyAtCursor();
		ok &= expect(settled(editor) && editor->document().image().pixelColor(6, 3) == QColor(Qt::white), "second Space-style shape command commits the endpoint");
		editor->undo(); canvas->setCursorPixel({0, 0}); canvas->applyAtCursor(); canvas->setCursorPixel({7, 7}); canvas->cancelGesture();
		ok &= expect(!canvas->hasPendingShape() && editor->document().revision() == shapeRevision, "cancel abandons the shape preview without a history entry");
		wrap->setChecked(true); canvas->setCursorPixel({0, 0}); canvas->applyAtCursor(); canvas->setCursorPixel({100, 100});
		ok &= expect(canvas->cursorPixel() == QPoint(15, 7), "shape cursor and pending endpoint agree at the bounded repeat edge");
		tool->setCurrentIndex(int(TextureCanvas::Tool::Eraser));
		ok &= expect(!canvas->hasPendingShape() && !canvas->accessibleDescription().contains(QStringLiteral("Space commits")) && editor->document().revision() == shapeRevision, "changing tools cancels preview and refreshes its accessible readout");
		wrap->setChecked(false);
		QImage fillFixture(16, 8, QImage::Format_ARGB32); fillFixture.fill(QColor(200, 20, 20));
		fillFixture.setPixelColor(0, 0, QColor(40, 20, 20)); fillFixture.setPixelColor(1, 0, QColor(45, 20, 20));
		editor->setImage(fillFixture, QStringLiteral("fill.png")); tool->setCurrentIndex(int(TextureCanvas::Tool::Fill)); tolerance->setValue(5); color->setText(QStringLiteral("blue"));
		canvas->setCursorPixel({0, 0}); canvas->applyAtCursor();
		ok &= expect(settled(editor) && tolerance->isEnabled() && editor->document().image().pixelColor(1, 0) == QColor(Qt::blue) && editor->document().image().pixelColor(2, 0) == QColor(200, 20, 20), "fill control forwards tolerance through the asynchronous recipe");
		editor->findChild<QSpinBox*>(QStringLiteral("textureOffsetX"))->setValue(1);
		editor->findChild<QPushButton*>(QStringLiteral("offsetTexture"))->click();
		ok &= expect(settled(editor) && editor->document().image().pixelColor(0, 0) == QColor(200, 20, 20) && editor->document().image().pixelColor(2, 0) == QColor(Qt::blue), "offset control wraps active layer pixels");
		editor->undo(); editor->findChild<QPushButton*>(QStringLiteral("offsetTextureHalf"))->click();
		ok &= expect(settled(editor) && editor->document().image().pixelColor(8, 4) == QColor(Qt::blue), "half-size offset moves a corner seam into the canvas center");
		editor->setImage(image, QStringLiteral("textures/test.png")); tool->setCurrentIndex(int(TextureCanvas::Tool::Pencil)); brushWidth->setValue(1); canvas->setTiled(false); canvas->fit();
		color->setText(QStringLiteral("invalid color")); tool->setCurrentIndex(int(TextureCanvas::Tool::Pick));
		ok &= expect(canvas->isEnabled(), "eyedropper remains available to replace an invalid typed paint color");
		canvas->setCursorPixel({0, 0}); canvas->applyAtCursor();
		ok &= expect(QColor(color->text()).isValid(), "eyedropper repairs the color through the shared field");
		tool->setCurrentIndex(int(TextureCanvas::Tool::Select)); canvas->beginGesture({8, 5}); canvas->continueGesture({5, 2}); canvas->endGesture();
		ok &= expect(editor->document().selection() == QRect(5, 2, 4, 4), "reverse selection drag retains both endpoint pixels");
		QImage transformImage = image; transformImage.setPixelColor(5, 2, QColor(210, 40, 80, 64)); transformImage.setPixelColor(6, 4, Qt::green);
		editor->setImage(transformImage, QStringLiteral("transform.png")); canvas->setSelection({5, 2, 2, 3});
		auto* selectedWidth = editor->findChild<QSpinBox*>(QStringLiteral("textureSelectionWidth"));
		auto* selectedHeight = editor->findChild<QSpinBox*>(QStringLiteral("textureSelectionHeight"));
		auto* selectedAnchor = editor->findChild<QComboBox*>(QStringLiteral("textureSelectionAnchor"));
		auto* resizeSelection = editor->findChild<QPushButton*>(QStringLiteral("resizeTextureSelection"));
		auto* rotateSelection = editor->findChild<QPushButton*>(QStringLiteral("rotateTextureSelection"));
		ok &= expect(selectedWidth->value() == 2 && selectedHeight->value() == 3 && selectedAnchor->count() == 9 && !selectedAnchor->accessibleName().isEmpty(), "selection controls follow canvas geometry and expose named anchors");
		selectedAnchor->setCurrentIndex(int(TextureAnchor::TopLeft)); selectedWidth->setValue(4); selectedHeight->setValue(2); resizeSelection->click();
		ok &= expect(settled(editor) && editor->document().selection() == QRect(5, 2, 4, 2) && editor->document().activeLayer()->pixels.pixelColor(6, 2) == QColor(210, 40, 80, 64) && editor->document().activeLayer()->pixels.pixelColor(8, 3) == QColor(Qt::green), "selection resize controls share exact nearest sampling and alpha semantics");
		editor->undo();
		ok &= expect(editor->document().activeLayer()->pixels == transformImage && selectedWidth->value() == 2 && selectedHeight->value() == 3 && !editor->document().isDirty(), "transform undo refreshes selection fields and saved pixels");
		rotateSelection->click();
		ok &= expect(settled(editor) && editor->document().selection() == QRect(5, 2, 3, 2) && editor->document().activeLayer()->pixels.pixelColor(7, 2) == QColor(210, 40, 80, 64), "selected rotation is a separate asynchronous command with the chosen anchor");
		editor->undo(); selectedWidth->setValue(4096);
		ok &= expect(!resizeSelection->isEnabled() && !editor->findChild<QLabel*>(QStringLiteral("textureSelectionHint"))->text().isEmpty(), "out-of-canvas resize has a disabled action and visible explanation");
		selectedWidth->setValue(4); selectedHeight->setValue(2);
		editor->applyOperations({QJsonObject{{"op", "layer-properties"}, {"locked", true}}});
		ok &= expect(settled(editor) && !resizeSelection->isEnabled() && !rotateSelection->isEnabled() && !selectedWidth->isEnabled(), "selected pixel controls respect locked layers");
		editor->undo();
		auto* width = editor->findChild<QSpinBox*>(QStringLiteral("textureWidth"));
		auto* height = editor->findChild<QSpinBox*>(QStringLiteral("textureHeight"));
		auto* canvasAnchor = editor->findChild<QComboBox*>(QStringLiteral("textureCanvasAnchor"));
		canvasAnchor->setCurrentIndex(int(TextureAnchor::BottomRight)); width->setValue(20); height->setValue(10);
		if (!qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT").isEmpty()) {
			auto* tabs = editor->findChild<QTabWidget*>(QStringLiteral("textureProperties"));
			for (int section : {1, 5}) {
				tabs->setCurrentIndex(section); app.processEvents();
				auto* scroll = qobject_cast<QScrollArea*>(tabs->currentWidget());
				for (bool bottom : {false, true}) {
					scroll->verticalScrollBar()->setValue(bottom ? scroll->verticalScrollBar()->maximum() : 0); app.processEvents();
					QImage capture(editor->size(), QImage::Format_ARGB32_Premultiplied); capture.fill(Qt::transparent); editor->render(&capture);
					ok &= expect(capture.save(QDir(qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT")).filePath(QStringLiteral("texture-transform-%1-%2-%3.png").arg(scale).arg(section).arg(bottom ? "bottom" : "top"))), "render transform controls directly at each layout scale");
				}
				ok &= expect(scroll->horizontalScrollBar()->maximum() == 0, "new transform controls fit expanded translations and scaling");
			}
			tabs->setCurrentIndex(0);
		}
		editor->findChild<QPushButton*>(QStringLiteral("setTextureCanvasSize"))->click();
		ok &= expect(settled(editor) && editor->document().size() == QSize(20, 10) && editor->document().selection().isEmpty() && editor->document().activeLayer()->pixels.pixelColor(9, 4) == QColor(210, 40, 80, 64) && editor->document().activeLayer()->pixels.pixelColor(0, 0).alpha() == 0, "canvas size button pads without resampling at the selected anchor");
		editor->undo(); ok &= expect(editor->document().activeLayer()->pixels == transformImage && editor->document().selection() == QRect(5, 2, 2, 3), "canvas sizing undo restores both canvas and selection");
		canvas->setSelection({}); ok &= expect(!resizeSelection->isEnabled() && !rotateSelection->isEnabled(), "selected transforms are disabled without a selection");
		editor->setImage(image, QStringLiteral("textures/test.png"));
		canvas->setSelection({});
		tool->setCurrentIndex(int(TextureCanvas::Tool::Pencil));
		auto* paletteChoice = editor->findChild<QComboBox*>(QStringLiteral("textureEditorPalette"));
		paletteChoice->setCurrentIndex(paletteChoice->findData(QStringLiteral("doom")));
		ok &= expect(settled(editor) && paletteChoice->currentData().toString() == QStringLiteral("doom"), "artist can choose the game palette without reopening the editor");
		canvas->setColor(QColor(240, 50, 80)); canvas->setCursorPixel({3, 4}); canvas->applyAtCursor();
		ok &= expect(editor->document().image().pixelColor(3, 4) == QColor(240, 50, 80) && editor->document().isDirty(), "cursor command paints the document");
		editor->undo(); ok &= expect(!editor->document().isDirty(), "UI undo returns to clean imported surface");
		editor->redo();
		editor->applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("resize")}, {QStringLiteral("width"), 32}, {QStringLiteral("height"), 16}}});
		ok &= expect(editor->isBusy() && !canvas->isEnabled() && settled(editor) && editor->document().image().size() == QSize(32, 16), "asynchronous operation blocks edits and commits results");
		editor->applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("rotate")}}}); editor->cancelPending();
		ok &= expect(settled(editor) && editor->document().image().size() == QSize(32, 16), "cancel discards pending transformation");
		const QString saved = root.filePath(QStringLiteral("ui-%1.png").arg(scale));
		bool continued = false;
		editor->saveToPath(saved, false, [&]() { continued = true; });
		ok &= expect(settled(editor) && continued && editor->document().isDirty() && QImage(saved).pixelColor(6, 8) == QColor(240, 50, 80), "PNG export preserves pixels and cannot mark the authoring project clean");
		editor->findChild<QPushButton*>(QStringLiteral("addTextureLayer"))->click();
		ok &= expect(settled(editor) && editor->document().layers().size() == 2 && editor->document().activeLayerIndex() == 1, "GUI adds and selects a new editable layer");
		canvas->setSelection({2, 2, 2, 2}); canvas->setColor(Qt::blue); canvas->setCursorPixel({1, 1}); canvas->applyAtCursor();
		ok &= expect(editor->document().activeLayer()->pixels.pixelColor(1, 1).alpha() == 0, "canvas selection clips cursor painting");
		canvas->setCursorPixel({2, 2}); canvas->applyAtCursor();
		ok &= expect(editor->document().activeLayer()->pixels.pixelColor(2, 2) == QColor(Qt::blue), "cursor painting targets the selected layer");
		editor->findChild<QCheckBox*>(QStringLiteral("textureLayerLocked"))->setChecked(true);
		ok &= expect(settled(editor) && editor->document().activeLayer()->locked, "layer lock control commits shared document properties");
		canvas->setColor(Qt::green); canvas->applyAtCursor();
		ok &= expect(editor->document().activeLayer()->pixels.pixelColor(2, 2) == QColor(Qt::blue), "canvas cannot paint locked layer pixels");
		editor->undo();
		const QString project = root.filePath(QStringLiteral("ui-%1.vtexture").arg(scale));
		editor->saveProjectToPath(project, false);
		ok &= expect(settled(editor) && !editor->hasUnsavedChanges() && !editor->document().isDirty(), "native project save establishes a clean authoring revision");
		editor->openFromPath(project);
		ok &= expect(settled(editor) && editor->document().layers().size() == 2 && editor->document().activeLayerIndex() == 1 && !editor->hasUnsavedChanges() && paletteChoice->currentData().toString() == QStringLiteral("doom"), "GUI reopens layers and palette without marking restored metadata dirty");
		editor->selectLayer(0);
		ok &= expect(editor->document().activeLayerIndex() == 0 && !editor->hasUnsavedChanges(), "switching active layer does not dirty pixel content");
		canvas->setSelection({}); canvas->setCursorPixel({0, 0}); canvas->applyAtCursor();
		QFile external(project); ok &= expect(external.open(QIODevice::WriteOnly) && external.write("external change") > 0, "simulate a source project conflict"); external.close();
		continued = false; editor->saveProjectToPath(project, true, [&]() { continued = true; });
		ok &= expect(settled(editor) && editor->hasUnsavedChanges() && !continued, "conflicting native saves retain edits and never run close/open continuations");
		editor->saveProjectToPath(root.filePath(QStringLiteral("ui-copy-%1.vtexture").arg(scale)), false);
		ok &= expect(settled(editor) && !editor->hasUnsavedChanges(), "save a copy recovers from an externally modified source");
		editor->findChild<QLineEdit*>(QStringLiteral("texturePackagePath"))->setText(QStringLiteral("textures/ui-%1.png").arg(scale));
		editor->stage(); ok &= expect(settled(editor), "staging completes asynchronously");
		PackageStagingArchive planned(staging); QByteArray payload;
		ok &= expect(planned.readEntryBytes(QStringLiteral("textures/ui-%1.png").arg(scale), &payload, &error) && !QImage::fromData(payload).isNull(), "UI hands generated PNG to normal package staging");
		const auto operationCount = staging.operations().size();
		editor->findChild<QLineEdit*>(QStringLiteral("texturePackagePath"))->setText(QStringLiteral("textures/stale.png"));
		editor->stage(); package += QStringLiteral("-changed");
		ok &= expect(settled(editor) && staging.operations().size() == operationCount, "changing the package during encoding prevents stale handoff");
		package = root.path(); editor->refreshContext();
		canvas->setTiled(true); canvas->fit();
		const QString captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
		if (!captures.isEmpty()) {
			app.processEvents(); QImage capture(editor->size(), QImage::Format_ARGB32_Premultiplied); capture.fill(Qt::transparent); editor->render(&capture);
			ok &= expect(capture.save(QDir(captures).filePath(QStringLiteral("texture-editor-%1.png").arg(scale))), "save widget-rendered layout evidence");
			auto* tabs = editor->findChild<QTabWidget*>(QStringLiteral("textureProperties")); tabs->setCurrentIndex(2);
			app.processEvents(); editor->render(&capture);
			ok &= expect(capture.save(QDir(captures).filePath(QStringLiteral("texture-editor-%1-properties.png").arg(scale))), "render lower palette and staging controls");
			tabs->setCurrentIndex(4); app.processEvents(); editor->render(&capture);
			ok &= expect(capture.save(QDir(captures).filePath(QStringLiteral("texture-editor-%1-layers.png").arg(scale))), "render layered authoring controls");
		}
		// Hidden pages can retain pre-layout scrollbar ranges on Qt 6.4. Audit
		// each actual displayed section regardless of whether captures are enabled.
		auto* properties = editor->findChild<QTabWidget*>(QStringLiteral("textureProperties"));
		for (int section = 0; section < properties->count(); ++section) {
			properties->setCurrentIndex(section); app.processEvents();
			auto* scroll = qobject_cast<QScrollArea*>(properties->currentWidget());
			const bool fits = scroll && scroll->horizontalScrollBar()->maximum() == 0;
			if (!fits && scroll) {
				std::cerr << "Inspector section " << section << " at " << scale << "%: viewport="
				          << scroll->viewport()->width() << ", content minimum=" << scroll->widget()->minimumSizeHint().width()
				          << ", horizontal range=" << scroll->horizontalScrollBar()->maximum() << '\n';
			}
			ok &= expect(fits, "properties fit at large scale and with expanded translations");
		}
		delete editor;
		milestone(scale == 100 ? "100 percent editing complete" : "200 percent editing complete");
		if (scale == 200) { app.removeTranslator(&expanded); }
	}
	StudioSettings::setOverrideFilePath(root.filePath(QStringLiteral("shell.ini")));
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	milestone("Constructing shell");
	const auto constructionStart = timer.elapsed();
	auto* shell = new ApplicationShell; milestone("Shell constructed");
	performance.insert(QStringLiteral("shellConstructionMs"), timer.elapsed() - constructionStart);
	shell->openPathFromCommandLine(root.path());
	LevelMapDocument map;
	ok &= expect(loadLevelMapBytes({QStringLiteral("fixture.map"), {}, QStringLiteral("idTech3")}, "// Q3Radiant\n{\n\"classname\" \"worldspawn\"\n}\n", &map, &error), "create a generated map fixture");
	int brush = -1;
	ok &= expect(addLevelMapBoxBrush(&map, {0, 0, 0, true}, {64, 64, 64, true}, QStringLiteral("textures/old"), &brush, &error), "add a brush for the texture handoff");
	const QString mapPath = root.filePath(QStringLiteral("fixture.map"));
	ok &= expect(saveLevelMapAs(map, mapPath).succeeded(), "save generated map fixture");
	shell->openPathFromCommandLine(mapPath);
	TextureDocument projectFixture; projectFixture.create({8, 8}, Qt::red); projectFixture.addLayer(QStringLiteral("Shell ink"));
	TextureProjectSaveRequest projectSave; projectSave.path = root.filePath(QStringLiteral("shell.vtexture"));
	ok &= expect(writeTextureProject(projectFixture, projectSave).succeeded, "write a project for shell file routing");
	shell->openPathFromCommandLine(projectSave.path);
	auto* routed = shell->findChild<TextureEditorDialog*>(QStringLiteral("textureEditorDialog"));
	ok &= expect(routed && settled(routed) && routed->document().layers().size() == 2, "shell routes native projects into the texture editor without changing the package context");
	auto* objects = shell->findChild<LevelObjectList*>(QStringLiteral("levelMapObjects"));
	for (int i = 0; objects && i < objects->model()->rowCount(); ++i) {
		if (objectIndex(objects, i).data(Qt::UserRole).toString().startsWith(QStringLiteral("brush:"))) { setObjectCurrentRow(objects, i); break; }
	}
	auto* open = shell->findChild<QPushButton*>(QStringLiteral("openTextureEditor"));
	ok &= expect(open, "Textures page exposes authoring even without a selected image"); if (open) { open->click(); }
	auto* editor = shell->findChild<TextureEditorDialog*>(QStringLiteral("textureEditorDialog"));
	ok &= expect(editor && editor->findChild<QAction*>(QStringLiteral("stageTexture"))->isEnabled(), "shell provides package context to authoring");
	if (editor) {
		editor->findChild<QLineEdit*>(QStringLiteral("texturePackagePath"))->setText(QStringLiteral("textures/shell.png")); editor->stage();
		ok &= expect(settled(editor), "shell texture handoff completes");
		auto* entries = shell->findChild<QListWidget*>(QStringLiteral("textureEntries"));
		bool present = false;
		for (int i = 0; entries && i < entries->count(); ++i) { if (entries->item(i)->data(Qt::UserRole).toString() == QStringLiteral("textures/shell.png")) { present = true; } }
		ok &= expect(present, "Textures browser immediately includes generated staged images");
		editor->findChild<QLineEdit*>(QStringLiteral("texturePackagePath"))->setText(QStringLiteral("textures/applied.png"));
		editor->refreshContext();
		ok &= expect(editor->findChild<QAction*>(QStringLiteral("applyTexture"))->isEnabled(), "selected Quake III geometry enables stage-and-apply");
		editor->stage(true); ok &= expect(settled(editor), "stage-and-apply completes");
		auto* textures = shell->findChild<QListWidget*>(QStringLiteral("levelMapTextures"));
		bool applied = false;
		for (int i = 0; textures && i < textures->count(); ++i) { if (textures->item(i)->data(Qt::UserRole).toString() == QStringLiteral("applied")) { applied = true; } }
		ok &= expect(applied, "authored PNG becomes the map brush texture through the shell callback");
		const auto appliedRevision = shell->levelDocument().revision;
		editor->findChild<QLineEdit*>(QStringLiteral("texturePackagePath"))->setText(QStringLiteral("models/wrong-root.png"));
		auto* apply = editor->findChild<QAction*>(QStringLiteral("applyTexture"));
		ok &= expect(!apply->isEnabled() && apply->toolTip().contains(QStringLiteral("textures/")), "invalid compiler paths disable Apply with an actionable reason");
		editor->stage(true);
		ok &= expect(!editor->isBusy() && shell->levelDocument().revision == appliedRevision, "disabled handoff cannot mutate the map");
		editor->findChild<QLineEdit*>(QStringLiteral("texturePackagePath"))->setText(QStringLiteral("textures/applied.png"));
		auto* replace = editor->findChild<QCheckBox*>(QStringLiteral("replaceTextureEntry"));
		replace->setChecked(true);
		editor->applyOperations({QJsonObject{{"op", "fill"}, {"x", 0}, {"y", 0}, {"color", "#204080"}}});
		ok &= expect(settled(editor), "prepare updated pixels for the same texture reference");
		editor->stage(true);
		ok &= expect(settled(editor) && shell->levelDocument().revision == appliedRevision &&
			editor->findChild<QLabel*>(QStringLiteral("textureEditorStatus"))->text().contains(QStringLiteral("Texture staged")),
			"shell can restage an already applied texture without creating a map edit");
		replace->setChecked(false);
		LevelMapPatch curve;
		ok &= expect(createLevelPatch({}, &curve, &error) && shell->applyLevelPatch(curve, -1, &error), "add a patch to the same authoring context");
		editor->findChild<QLineEdit*>(QStringLiteral("texturePackagePath"))->setText(QStringLiteral("textures/patchcheck.png"));
		editor->refreshContext(); editor->stage(true);
		ok &= expect(settled(editor) && shell->levelDocument().patches.first().textureName == QStringLiteral("patchcheck"),
			"texture handoff uses compiler-compatible patch tokens while staging the full package path");
		// Exercise native staging through the real shell callback and browser.
		// Undo package edits first so opening the next test package needs no prompt.
		milestone("Native shell handoff starting");
		QTimer unexpectedPrompt; unexpectedPrompt.setInterval(100);
		QObject::connect(&unexpectedPrompt, &QTimer::timeout, [&]() {
			for (auto* widget : QApplication::topLevelWidgets()) {
				if (auto* prompt = qobject_cast<QMessageBox*>(widget); prompt && prompt->isVisible()) {
					std::cerr << "Unexpected native handoff prompt: " << prompt->windowTitle().toStdString() << ": " << prompt->text().toStdString() << '\n';
					ok = false; prompt->done(QMessageBox::Cancel);
				}
			}
		}); unexpectedPrompt.start();
		auto* packageUndo = shell->findChild<QAbstractButton*>(QStringLiteral("packageUndo"));
		ok &= expect(packageUndo, "package undo control is available through its shared button interface");
		for (int i = 0; packageUndo && packageUndo->isEnabled() && i < 8; ++i) { packageUndo->click(); }
		milestone("Native package undo complete");
		IdTechPaletteResolution nativePalette; nativePalette.palette.id = QStringLiteral("quake");
		for (int i = 0; i < 256; ++i) { nativePalette.palette.colors << qRgb(i, i, i); }
		QImage nativePixels(16, 16, QImage::Format_Indexed8); nativePixels.setColorTable(nativePalette.palette.colors); nativePixels.fill(80);
		TextureExportOptions nativeOptions; nativeOptions.format = TextureExportFormat::QuakeMiptex; nativeOptions.name = QStringLiteral("native");
		const auto nativeExport = encodeTextureExport(nativePixels, nativeOptions, nativePalette); const auto nativePath = root.filePath(QStringLiteral("native.wad"));
		PackageStagingModel nativeFixture;
		ok &= expect(nativeFixture.createEmpty(PackageArchiveFormat::Wad, QStringLiteral("WAD2"), &error) &&
			stageTextureExport(nativeExport, nativeOptions, QStringLiteral("native"), &nativeFixture, false, &error), "prepare native WAD shell fixture");
		for (int i = 0; i < 65; ++i) {
			auto fixtureOptions = nativeOptions; fixtureOptions.name = i == 64 ? QStringLiteral("zz_search") : QStringLiteral("tile_%1").arg(i, 2, 10, QLatin1Char('0'));
			const auto pixels = i == 64 ? nativePixels.scaled(32, 32) : nativePixels;
			ok &= expect(stageTextureExport(encodeTextureExport(pixels, fixtureOptions, nativePalette), fixtureOptions, fixtureOptions.name, &nativeFixture, false, &error),
				"add generated offscreen textures for metadata search coverage");
		}
		PackageWriteRequest writeNative; writeNative.format = PackageArchiveFormat::Wad; writeNative.destinationPath = nativePath;
		ok &= expect(nativeFixture.writeArchive(writeNative).succeeded(), "write native WAD shell fixture");
		editor->saveProjectToPath(projectSave.path, true); ok &= expect(settled(editor) && !editor->hasUnsavedChanges(), "save editor metadata before native browser handoff");
		milestone("Native editor project saved");
		shell->openPathFromCommandLine(nativePath);
		milestone("Native WAD opened");
		auto* packagePreviews = shell->findChild<QStackedWidget*>(QStringLiteral("packagePreviewStack"));
		QElapsedTimer packageWait; packageWait.start();
		while (packagePreviews && packagePreviews->currentIndex() != 1 && packageWait.elapsed() < 10000) { app.processEvents(); }
		auto* packageImage = packagePreviews ? qobject_cast<ImagePreviewView*>(packagePreviews->currentWidget()) : nullptr;
		ok &= expect(packageImage && packageImage->hasImage() && packageImage->image().size() == QSize(16, 16),
			"Packages uses the background decoder for the selected native occurrence");
		for (int i = 0; entries && i < entries->count(); ++i) { if (entries->item(i)->data(Qt::UserRole).toString() == QStringLiteral("native")) { entries->setCurrentRow(i); break; } }
		auto* cancelPreview = shell->findChild<QAbstractButton*>(QStringLiteral("cancelTexturePreview"));
		auto* reloadPreviews = shell->findChild<QAbstractButton*>(QStringLiteral("reloadTexturePreviews"));
		auto* browserPreview = shell->findChild<ImagePreviewView*>(QStringLiteral("texturePreview"));
		ok &= expect(cancelPreview && reloadPreviews && browserPreview, "browser exposes accessible preview cancellation and reload controls");
		if (cancelPreview && reloadPreviews && browserPreview) {
			reloadPreviews->click();
			cancelPreview->click(); app.processEvents();
			ok &= expect(!browserPreview->hasImage() && !cancelPreview->isEnabled(), "cancelling a queued preview clears old pixels and stops publication");
			reloadPreviews->click();
		}
		auto* editSelected = shell->findChild<QPushButton*>(QStringLiteral("editSelectedTexture"));
		QElapsedTimer previewWait; previewWait.start();
		while (editSelected && !editSelected->isEnabled() && previewWait.elapsed() < 15000) { app.processEvents(); }
		ok &= expect(editSelected && editSelected->isEnabled(), "native browser preview becomes editable after asynchronous decoding");
		if (editSelected) { editSelected->click(); }
		milestone("Native image imported");
		TextureExportOptions imported;
		ok &= expect(editor->exportOptions(&imported) && imported.format == TextureExportFormat::QuakeMiptex && imported.name == QStringLiteral("native")
			&& editor->findChild<QLineEdit*>(QStringLiteral("texturePackagePath"))->text() == QStringLiteral("native"), "real native browser handoff retains lump name and metadata");
		imported.name = QStringLiteral("newnative"); editor->setExportOptions(imported); editor->setPaletteResolution(nativePalette);
		editor->findChild<QLineEdit*>(QStringLiteral("texturePackagePath"))->setText(QStringLiteral("newnative")); editor->stage();
		ok &= expect(settled(editor), "native WAD shell staging completes"); present = false;
		for (int i = 0; entries && i < entries->count(); ++i) { if (entries->item(i)->data(Qt::UserRole).toString() == QStringLiteral("newnative")) { present = true; } }
		ok &= expect(present, "native staged WAD lump immediately appears in the texture browser");
		PackageArchive unchanged; unchanged.load(nativePath); ok &= expect(unchanged.entries().size() == 66, "GUI native staging leaves the on-disk WAD unchanged");
		auto* texturesMode = shell->findChild<QAction*>(QStringLiteral("shell.mode.textures"));
		ok &= expect(texturesMode && texturesMode->isEnabled(), "Textures mode action is available");
		if (texturesMode) { texturesMode->trigger(); }
		shell->resize(1600, 1000); shell->show(); app.processEvents();
		QElapsedTimer completedPreviewWait; completedPreviewWait.start();
		while (cancelPreview && cancelPreview->isEnabled() && completedPreviewWait.elapsed() < 15000) { app.processEvents(); }
		auto* completedPreviewState = shell->findChild<QFrame*>(QStringLiteral("textureBrowserState"));
		ok &= expect(completedPreviewState && completedPreviewState->parentWidget()->isHidden(), "completed previews collapse their status area in the normal regression");
		auto* textureFilter = shell->findChild<QLineEdit*>(QStringLiteral("textureFilter"));
		QListWidgetItem* distant = nullptr;
		for (int i = 0; entries && i < entries->count(); ++i) { if (entries->item(i)->data(Qt::UserRole).toString() == QStringLiteral("zz_search")) { distant = entries->item(i); } }
		ok &= expect(textureFilter && distant, "offscreen search fixture is present");
		if (textureFilter && distant) {
			ok &= expect(entries->isVisible(), "dimension query runs in the visible browser");
			QElapsedTimer heartbeat; heartbeat.start(); qint64 lastBeat = 0, maximumGap = 0; int beats = 0;
			QTimer pulse; pulse.setInterval(5);
			QObject::connect(&pulse, &QTimer::timeout, [&] { const auto now = heartbeat.elapsed(); maximumGap = std::max(maximumGap, now - lastBeat); lastBeat = now; ++beats; });
			pulse.start();
			textureFilter->setText(QStringLiteral("w=32"));
			QElapsedTimer searchWait; searchWait.start();
			while (distant->isHidden() && searchWait.elapsed() < 15000) { app.processEvents(); }
			pulse.stop(); maximumGap = std::max(maximumGap, heartbeat.elapsed() - lastBeat);
			performance.insert(QStringLiteral("metadataSearchMs"), heartbeat.elapsed());
			performance.insert(QStringLiteral("metadataSearchMaximumUiGapMs"), maximumGap);
			performance.insert(QStringLiteral("metadataSearchHeartbeats"), beats);
			performance.insert(QStringLiteral("metadataSearchSucceeded"), !distant->isHidden());
			if (distant->isHidden()) {
				std::cerr << "Dimension query diagnostic: visible=" << entries->isVisible() << " loading=" << (cancelPreview && cancelPreview->isEnabled()) << '\n';
				if (completedPreviewState) { for (auto* label : completedPreviewState->findChildren<QLabel*>()) { std::cerr << label->text().toStdString() << '\n'; } }
			}
			ok &= expect(!distant->isHidden(), "dimension queries decode metadata for offscreen rows without requiring an eager image cache");
			textureFilter->clear();
		}
		const auto browserThemes = qEnvironmentVariableIntValue("VIBESTUDIO_TEST_BROWSER_LAYOUT") != 0
			? QVector<StudioTheme>{StudioTheme::Dark, StudioTheme::HighContrastDark, StudioTheme::HighContrastLight} : QVector<StudioTheme>{};
		for (const auto theme : browserThemes) {
			milestone("Starting browser theme render");
			const int scale = theme == StudioTheme::Dark ? 100 : 200;
			applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, scale));
			shell->setLayoutDirection(theme == StudioTheme::HighContrastDark ? Qt::RightToLeft : Qt::LeftToRight);
			shell->resize(1600, 1000); shell->show(); app.processEvents();
			QElapsedTimer browserLayoutWait; browserLayoutWait.start();
			while (browserPreview && (!browserPreview->hasImage() || cancelPreview->isEnabled()) && browserLayoutWait.elapsed() < 15000) { app.processEvents(); }
			ok &= expect(browserPreview && browserPreview->hasImage(), "browser layout capture waits for decoded pixels");
			auto* previewState = shell->findChild<QFrame*>(QStringLiteral("textureBrowserState"));
			ok &= expect(previewState && previewState->parentWidget()->isHidden(), "completed texture loading releases the status strip's layout space");
			ok &= expect(reloadPreviews && reloadPreviews->focusPolicy() != Qt::NoFocus && !reloadPreviews->accessibleName().isEmpty(),
				"preview reload remains focusable and named across themes and scaling");
			const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
			if (!captures.isEmpty()) {
				QImage capture(shell->size(), QImage::Format_ARGB32_Premultiplied); capture.fill(Qt::transparent); shell->render(&capture);
				ok &= expect(capture.save(QDir(captures).filePath(QStringLiteral("texture-browser-%1.png").arg(static_cast<int>(theme)))), "render preview controls from Qt widgets");
			}
			milestone("Browser theme render complete");
		}
	}
	delete shell; app.processEvents(); StudioSettings::setOverrideFilePath({});
	milestone("Shell handoff complete");
	performance.insert(QStringLiteral("totalMs"), timer.elapsed());
	const QString performanceRoot = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
	if (!performanceRoot.isEmpty()) {
		QFile report(QDir(performanceRoot).filePath(QStringLiteral("texture-ui-timings.json")));
		const auto bytes = QJsonDocument(performance).toJson();
		ok &= expect(report.open(QIODevice::WriteOnly) && report.write(bytes) == bytes.size(), "save texture UI timing evidence");
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
