#include "app/texture_editor_dialog.h"
#include "core/asset_formats.h"
#include "app/texture_preview_worker.h"

#include "app/asset_views.h"
#include "app/texture_canvas.h"
#include "app/texture_recovery.h"
#include "app/texture_export_panel.h"
#include "core/studio_settings.h"
#include "core/texture_output.h"
#include "core/texture_handoff.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QSplitter>
#include <QTabWidget>
#include <QTabBar>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QUuid>

#include <memory>
#include <algorithm>
#include <atomic>

namespace vibestudio {

struct TextureEditorTask {
	std::atomic_bool cancelled{false};
	std::atomic_int progress{-1};
};

TextureEditorDialog::TextureEditorDialog(QWidget* parent) : QDialog(parent)
{
	setObjectName(QStringLiteral("textureEditorDialog"));
	setWindowTitle(QCoreApplication::translate("VibeStudioTextureEditor", "Texture Editor"));
	setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Texture editor"));
	setAttribute(Qt::WA_DeleteOnClose); resize(1180, 800);
	m_document.create({64, 64}, Qt::transparent); m_document.markSaved();
	m_palette.palette = generatedIdTechPalette(QStringLiteral("quake"));
	auto* layout = new QVBoxLayout(this);
	m_tools = new QToolBar(QCoreApplication::translate("VibeStudioTextureEditor", "Texture actions"));
	m_tools->setAccessibleName(m_tools->windowTitle()); m_tools->setToolButtonStyle(Qt::ToolButtonTextOnly);
	const auto action = [&](const QString& label, const QString& id, auto callback, QKeySequence shortcut = {}) {
		auto* result = m_tools->addAction(label); result->setObjectName(id);
		connect(result, &QAction::triggered, this, callback);
		if (!shortcut.isEmpty()) { result->setShortcut(shortcut); result->setShortcutContext(Qt::WidgetWithChildrenShortcut); addAction(result); }
		return result;
	};
	action(QCoreApplication::translate("VibeStudioTextureEditor", "New"), QStringLiteral("newTexture"), [this]() { newTexture(); }, QKeySequence::New);
	action(QCoreApplication::translate("VibeStudioTextureEditor", "Open…"), QStringLiteral("openTexture"), [this]() { openFile(); }, QKeySequence::Open);
	action(QCoreApplication::translate("VibeStudioTextureEditor", "Save Project"), QStringLiteral("saveTexture"), [this]() {
		if (m_projectIdentity.path.isEmpty()) { saveAs(); } else { saveProjectToPath(m_projectIdentity.path, true); }
	}, QKeySequence::Save);
	auto saveAsShortcut = QKeySequence(QKeySequence::SaveAs);
	// Some platform themes provide no binding for this standard key.
	if (saveAsShortcut.isEmpty()) { saveAsShortcut = QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S); }
	action(QCoreApplication::translate("VibeStudioTextureEditor", "Save Project As…"), QStringLiteral("saveTextureAs"), [this]() { saveAs(); }, saveAsShortcut);
	action(QCoreApplication::translate("VibeStudioTextureEditor", "Export…"), QStringLiteral("exportTexture"), [this]() {
		findChild<QComboBox*>(QStringLiteral("texturePropertySection"))->setCurrentIndex(7); previewExport();
	});
	m_tools->addSeparator();
	m_undo = action(QCoreApplication::translate("VibeStudioTextureEditor", "Undo"), QStringLiteral("undoTexture"), [this]() { undo(); }, QKeySequence::Undo);
	m_redo = action(QCoreApplication::translate("VibeStudioTextureEditor", "Redo"), QStringLiteral("redoTexture"), [this]() { redo(); }, QKeySequence::Redo);
	m_tools->addSeparator();
	m_stage = action(QCoreApplication::translate("VibeStudioTextureEditor", "Stage Export"), QStringLiteral("stageTexture"), [this]() { stage(); });
	m_apply = action(QCoreApplication::translate("VibeStudioTextureEditor", "Stage and Apply"), QStringLiteral("applyTexture"), [this]() { stage(true); });
	m_stage->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Encode the selected export profile into the package plan. Review its preview and warnings. Save the package separately."));
	m_apply->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Stage a PNG or TGA and apply its texture path to the selection in a Quake III map. Map and package saves remain separate."));
	layout->addWidget(m_tools);
	m_contextLabel = new QLabel; m_contextLabel->setTextFormat(Qt::PlainText); m_contextLabel->setWordWrap(true);
	m_contextLabel->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Texture project context")); layout->addWidget(m_contextLabel);
	auto* splitter = new QSplitter;
	auto* canvasPanel = new QWidget; auto* canvasLayout = new QVBoxLayout(canvasPanel); canvasLayout->setContentsMargins(0, 0, 0, 0);
	auto* view = new QToolBar(QCoreApplication::translate("VibeStudioTextureEditor", "Canvas view"));
	view->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Canvas view"));
	m_canvas = new TextureCanvas(&m_document);
	connect(view->addAction(QCoreApplication::translate("VibeStudioTextureEditor", "Fit")), &QAction::triggered, this, [this]() { m_canvas->fit(); });
	connect(view->addAction(QStringLiteral("1:1")), &QAction::triggered, this, [this]() { m_canvas->setZoom(1); });
	connect(view->addAction(QCoreApplication::translate("VibeStudioTextureEditor", "Zoom In")), &QAction::triggered, this, [this]() { m_canvas->setZoom(m_canvas->zoom() * 1.25); });
	connect(view->addAction(QCoreApplication::translate("VibeStudioTextureEditor", "Zoom Out")), &QAction::triggered, this, [this]() { m_canvas->setZoom(m_canvas->zoom() / 1.25); });
	auto* tiles = view->addAction(QCoreApplication::translate("VibeStudioTextureEditor", "Tile Preview")); tiles->setCheckable(true);
	tiles->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Preview seams in a 3 × 3 repeat. Enable Wrap painting to edit across the repeated tiles."));
	connect(tiles, &QAction::toggled, this, [this](bool checked) { m_canvas->setTiled(checked); m_canvas->fit(); });
	auto* grid = view->addAction(QCoreApplication::translate("VibeStudioTextureEditor", "Pixel Grid")); grid->setCheckable(true); grid->setChecked(true);
	connect(grid, &QAction::toggled, this, [this](bool checked) { m_canvas->setGrid(checked); });
	canvasLayout->addWidget(view); canvasLayout->addWidget(m_canvas, 1);
	m_dimensions = new QLabel; m_dimensions->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Texture dimensions and save state"));
	m_pixelLabel = new QLabel; m_pixelLabel->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Pixel cursor and color"));
	canvasLayout->addWidget(m_dimensions); canvasLayout->addWidget(m_pixelLabel); splitter->addWidget(canvasPanel);
	auto* properties = new QTabWidget; m_properties = properties;
	properties->setObjectName(QStringLiteral("textureProperties"));
	properties->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Texture properties"));
	const auto propertyTab = [&](const QString& label) {
		auto* panel = new QWidget; auto* fields = new QFormLayout(panel);
		fields->setRowWrapPolicy(QFormLayout::WrapAllRows); fields->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
		auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setWidget(panel); scroll->setAccessibleName(label);
		properties->addTab(scroll, label); return fields;
	};
	auto* form = propertyTab(QCoreApplication::translate("VibeStudioTextureEditor", "Paint"));
	const auto spin = [&](const QString& label, const QString& name, int minimum, int maximum) {
		auto* field = new QSpinBox; field->setObjectName(name); field->setAccessibleName(label); field->setRange(minimum, maximum); field->setKeyboardTracking(false); form->addRow(label, field); return field;
	};
	const auto button = [&](const QString& label, const QString& id, auto callback) {
		auto* result = new QPushButton(label); result->setObjectName(id); result->setAccessibleName(label); result->setAutoDefault(false);
		connect(result, &QPushButton::clicked, this, callback); form->addRow(result); return result;
	};
	const auto anchorChoice = [&](const QString& label, const QString& id) {
		auto* field = new QComboBox; field->setObjectName(id); field->setAccessibleName(label);
		const QStringList labels{QCoreApplication::translate("VibeStudioTextureEditor", "Top left"), QCoreApplication::translate("VibeStudioTextureEditor", "Top"),
			QCoreApplication::translate("VibeStudioTextureEditor", "Top right"), QCoreApplication::translate("VibeStudioTextureEditor", "Left"),
			QCoreApplication::translate("VibeStudioTextureEditor", "Center"), QCoreApplication::translate("VibeStudioTextureEditor", "Right"),
			QCoreApplication::translate("VibeStudioTextureEditor", "Bottom left"), QCoreApplication::translate("VibeStudioTextureEditor", "Bottom"),
			QCoreApplication::translate("VibeStudioTextureEditor", "Bottom right")};
		for (int index = 0; index < labels.size(); ++index) { field->addItem(labels[index], textureAnchorId(static_cast<TextureAnchor>(index))); }
		field->setCurrentIndex(int(TextureAnchor::Center)); form->addRow(label, field); return field;
	};
	m_tool = new QComboBox; m_tool->setObjectName(QStringLiteral("textureTool"));
	m_tool->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Drawing tool"));
	m_tool->addItems({QCoreApplication::translate("VibeStudioTextureEditor", "Pencil"), QCoreApplication::translate("VibeStudioTextureEditor", "Eraser"),
		QCoreApplication::translate("VibeStudioTextureEditor", "Fill"), QCoreApplication::translate("VibeStudioTextureEditor", "Eyedropper"), QCoreApplication::translate("VibeStudioTextureEditor", "Rectangle Selection"),
		QCoreApplication::translate("VibeStudioTextureEditor", "Brush"), QCoreApplication::translate("VibeStudioTextureEditor", "Line"),
		QCoreApplication::translate("VibeStudioTextureEditor", "Rectangle"), QCoreApplication::translate("VibeStudioTextureEditor", "Ellipse")});
	form->addRow(QCoreApplication::translate("VibeStudioTextureEditor", "Tool"), m_tool);
	connect(m_tool, &QComboBox::currentIndexChanged, this, [this](int index) { m_canvas->setTool(static_cast<TextureCanvas::Tool>(index)); });
	auto* brush = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Brush width"), QStringLiteral("textureBrushWidth"), 1, 128);
	connect(brush, &QSpinBox::valueChanged, this, [this](int width) { m_canvas->setBrushWidth(width); });
	auto* brushShape = new QComboBox; brushShape->setObjectName(QStringLiteral("textureBrushShape"));
	brushShape->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Brush shape"));
	brushShape->addItem(QCoreApplication::translate("VibeStudioTextureEditor", "Square"), QStringLiteral("square"));
	brushShape->addItem(QCoreApplication::translate("VibeStudioTextureEditor", "Round"), QStringLiteral("round")); brushShape->setCurrentIndex(1);
	form->addRow(QCoreApplication::translate("VibeStudioTextureEditor", "Brush shape"), brushShape);
	connect(brushShape, &QComboBox::currentIndexChanged, this, [this, brushShape]() {
		TextureBrushShape shape; if (textureBrushShapeFromId(brushShape->currentData().toString(), &shape)) { m_canvas->setBrushShape(shape); }
	});
	auto* paintMode = new QComboBox; paintMode->setObjectName(QStringLiteral("texturePaintMode"));
	paintMode->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Paint mode"));
	paintMode->addItem(QCoreApplication::translate("VibeStudioTextureEditor", "Replace RGBA"), QStringLiteral("replace"));
	paintMode->addItem(QCoreApplication::translate("VibeStudioTextureEditor", "Blend over"), QStringLiteral("source-over")); paintMode->setCurrentIndex(1);
	paintMode->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Blend over applies color alpha once per pixel in each stroke or shape. Pencil always replaces RGBA; Eraser writes transparent pixels."));
	form->addRow(QCoreApplication::translate("VibeStudioTextureEditor", "Paint mode"), paintMode);
	connect(paintMode, &QComboBox::currentIndexChanged, this, [this, paintMode]() {
		TexturePaintMode mode; if (texturePaintModeFromId(paintMode->currentData().toString(), &mode)) { m_canvas->setPaintMode(mode); }
	});
	auto* filledShapes = new QCheckBox(QCoreApplication::translate("VibeStudioTextureEditor", "Filled shapes")); filledShapes->setObjectName(QStringLiteral("textureFilledShapes"));
	filledShapes->setAccessibleName(filledShapes->text()); form->addRow(filledShapes);
	connect(filledShapes, &QCheckBox::toggled, this, [this](bool filled) { m_canvas->setFilledShapes(filled); });
	auto* tolerance = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Fill tolerance"), QStringLiteral("textureFillTolerance"), 0, 255);
	tolerance->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Maximum difference in each original RGBA channel from the starting pixel, 0–255. Fill always replaces RGBA."));
	auto* wrap = new QCheckBox(QCoreApplication::translate("VibeStudioTextureEditor", "Wrap painting")); wrap->setObjectName(QStringLiteral("textureWrapPainting"));
	wrap->setAccessibleName(wrap->text());
	wrap->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Cross canvas edges while painting, drawing shapes, or filling. The selection still clips edits. Tile Preview also allows editing repeated tiles."));
	form->addRow(wrap); connect(wrap, &QCheckBox::toggled, this, [this](bool enabled) { m_canvas->setWrappedPainting(enabled); });
	const auto updatePaintControls = [this, brush, brushShape, paintMode, filledShapes, tolerance, wrap]() {
		const auto tool = static_cast<TextureCanvas::Tool>(m_tool->currentIndex());
		brush->setEnabled(tool != TextureCanvas::Tool::Fill && tool != TextureCanvas::Tool::Pick && tool != TextureCanvas::Tool::Select);
		brushShape->setEnabled(tool == TextureCanvas::Tool::Brush || tool == TextureCanvas::Tool::Eraser || tool == TextureCanvas::Tool::Line);
		paintMode->setEnabled(tool == TextureCanvas::Tool::Brush || tool == TextureCanvas::Tool::Line || tool == TextureCanvas::Tool::Rectangle || tool == TextureCanvas::Tool::Ellipse);
		filledShapes->setEnabled(tool == TextureCanvas::Tool::Rectangle || tool == TextureCanvas::Tool::Ellipse);
		tolerance->setEnabled(tool == TextureCanvas::Tool::Fill);
		wrap->setEnabled(tool != TextureCanvas::Tool::Pick && tool != TextureCanvas::Tool::Select);
		updateCanvasEnabled();
	};
	connect(m_tool, &QComboBox::currentIndexChanged, this, updatePaintControls); updatePaintControls();
	m_color = new QLineEdit(QStringLiteral("#ffffffff")); m_color->setObjectName(QStringLiteral("textureColor"));
	m_color->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Paint color"));
	m_color->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "A color name, #RRGGBB, or #AARRGGBB. Alpha comes first. Pencil and Fill replace RGBA; Brush and shapes use the Paint mode."));
	form->addRow(QCoreApplication::translate("VibeStudioTextureEditor", "Color"), m_color);
	connect(m_color, &QLineEdit::textChanged, this, [this](const QString& text) {
		const QColor color(text); if (color.isValid()) { m_canvas->setColor(color); }
		updateCanvasEnabled();
		if (!color.isValid()) { setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Enter a valid color before painting.")); }
	});
	updateCanvasEnabled();
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Choose Color…"), QStringLiteral("chooseTextureColor"), [this]() {
		const auto color = QColorDialog::getColor(QColor(m_color->text()), this, QCoreApplication::translate("VibeStudioTextureEditor", "Paint color"), QColorDialog::ShowAlphaChannel);
		if (color.isValid()) { m_color->setText(color.name(QColor::HexArgb)); }
	});
	form = propertyTab(QCoreApplication::translate("VibeStudioTextureEditor", "Transform"));
	m_width = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Width"), QStringLiteral("textureWidth"), 1, TextureDocument::MaximumDimension); m_width->setValue(64);
	m_height = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Height"), QStringLiteral("textureHeight"), 1, TextureDocument::MaximumDimension); m_height->setValue(64);
	m_canvasAnchor = anchorChoice(QCoreApplication::translate("VibeStudioTextureEditor", "Canvas content anchor"), QStringLiteral("textureCanvasAnchor"));
	m_canvasAnchor->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Align existing pixels within the new canvas. Center snaps half-pixel offsets toward the top left."));
	m_canvasSizeHint = new QLabel; m_canvasSizeHint->setObjectName(QStringLiteral("textureCanvasSizeHint")); m_canvasSizeHint->setWordWrap(true);
	m_canvasSizeHint->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Canvas size preview")); form->addRow(m_canvasSizeHint);
	m_canvasSizeButton = button(QCoreApplication::translate("VibeStudioTextureEditor", "Set Canvas Size"), QStringLiteral("setTextureCanvasSize"), [this]() {
		applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("canvas-size")}, {QStringLiteral("width"), m_width->value()}, {QStringLiteral("height"), m_height->value()}, {QStringLiteral("anchor"), m_canvasAnchor->currentData().toString()}}});
	});
	m_canvasSizeButton->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Pad with transparency or crop every layer, including hidden and locked layers. Pixel size stays unchanged. Undo restores cropped pixels."));
	m_smooth = new QCheckBox(QCoreApplication::translate("VibeStudioTextureEditor", "Smooth resampling")); m_smooth->setAccessibleName(m_smooth->text()); form->addRow(m_smooth);
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Resample Canvas"), QStringLiteral("resizeTexture"), [this]() {
		applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("resize")}, {QStringLiteral("width"), m_width->value()}, {QStringLiteral("height"), m_height->value()}, {QStringLiteral("smooth"), m_smooth->isChecked()}}});
	});
	const QStringList labels{QCoreApplication::translate("VibeStudioTextureEditor", "Crop X"), QCoreApplication::translate("VibeStudioTextureEditor", "Crop Y"),
		QCoreApplication::translate("VibeStudioTextureEditor", "Crop width"), QCoreApplication::translate("VibeStudioTextureEditor", "Crop height")};
	for (int i = 0; i < 4; ++i) { m_crop[i] = spin(labels[i], QStringLiteral("textureCrop%1").arg(i), i < 2 ? 0 : 1, TextureDocument::MaximumDimension); }
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Crop Canvas"), QStringLiteral("cropTexture"), [this]() {
		applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("crop")}, {QStringLiteral("x"), m_crop[0]->value()}, {QStringLiteral("y"), m_crop[1]->value()}, {QStringLiteral("width"), m_crop[2]->value()}, {QStringLiteral("height"), m_crop[3]->value()}}});
	});
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Rotate Canvas Clockwise"), QStringLiteral("rotateTexture"), [this]() { applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("rotate")}}}); });
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Flip Horizontal"), QStringLiteral("flipTextureHorizontal"), [this]() { applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("flip")}, {QStringLiteral("axis"), QStringLiteral("horizontal")}}}); });
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Flip Vertical"), QStringLiteral("flipTextureVertical"), [this]() { applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("flip")}, {QStringLiteral("axis"), QStringLiteral("vertical")}}}); });
	auto* offsetX = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Offset X"), QStringLiteral("textureOffsetX"), -TextureDocument::MaximumDimension, TextureDocument::MaximumDimension);
	auto* offsetY = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Offset Y"), QStringLiteral("textureOffsetY"), -TextureDocument::MaximumDimension, TextureDocument::MaximumDimension);
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Offset and Wrap"), QStringLiteral("offsetTexture"), [this, offsetX, offsetY]() {
		applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("offset")}, {QStringLiteral("x"), offsetX->value()}, {QStringLiteral("y"), offsetY->value()}}});
	})->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Move the active layer's pixels, wrapping within the selection or full canvas."));
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Offset Half Size"), QStringLiteral("offsetTextureHalf"), [this]() {
		const auto size = m_document.selection().isEmpty() ? m_document.size() : m_document.selection().size();
		applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("offset")}, {QStringLiteral("x"), size.width() / 2}, {QStringLiteral("y"), size.height() / 2}}});
	})->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Move edge seams into the center for repair. Uses half the selection or canvas size, rounded down."));
	form = propertyTab(QCoreApplication::translate("VibeStudioTextureEditor", "Palette"));
	m_paletteChoice = new QComboBox;
	m_paletteChoice->setObjectName(QStringLiteral("textureEditorPalette"));
	m_paletteChoice->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Game palette"));
	for (const auto& descriptor : idTechPaletteDescriptors()) { m_paletteChoice->addItem(descriptor.displayName, descriptor.id); }
	form->addRow(QCoreApplication::translate("VibeStudioTextureEditor", "Game palette"), m_paletteChoice);
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Refresh Palette Source"), QStringLiteral("refreshTexturePalette"), [this]() {
		refreshPaletteSource();
	})->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Reload this palette from the staged package or selected installation. Existing document pixels keep their colors until you remap or export."));
	connect(m_paletteChoice, &QComboBox::currentIndexChanged, this, [this]() {
		refreshPaletteSource(m_paletteChoice->currentData().toString());
	});
	m_paletteLabel = new QLabel; m_paletteLabel->setTextFormat(Qt::PlainText); m_paletteLabel->setWordWrap(true);
	m_paletteLabel->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Palette provenance")); form->addRow(m_paletteLabel);
	m_swatches = new PaletteSwatchView; m_swatches->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Paint palette")); form->addRow(m_swatches);
	connect(m_swatches, &PaletteSwatchView::indexSelected, this, [this](int index) { m_color->setText(QColor::fromRgba(m_palette.palette.colorAt(index)).name(QColor::HexArgb)); });
	m_dither = new QCheckBox(QCoreApplication::translate("VibeStudioTextureEditor", "Dither palette conversion")); m_dither->setAccessibleName(m_dither->text()); form->addRow(m_dither);
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Remap to Palette"), QStringLiteral("remapTexture"), [this]() {
		applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("palette")}, {QStringLiteral("dither"), m_dither->isChecked()}}});
	})->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Quantize the current pixels. Later painting can introduce colors outside the palette; remap again before saving if needed."));
	form = propertyTab(QCoreApplication::translate("VibeStudioTextureEditor", "Package"));
	m_virtualPath = new QLineEdit(QStringLiteral("textures/custom/texture.png")); m_virtualPath->setObjectName(QStringLiteral("texturePackagePath"));
	m_virtualPath->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Package texture path")); form->addRow(QCoreApplication::translate("VibeStudioTextureEditor", "Package path"), m_virtualPath);
	m_virtualPath->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Package-relative filename with the export extension, or a bare lump name for WAD. Quake miptexture names must match the export name."));
	m_replace = new QCheckBox(QCoreApplication::translate("VibeStudioTextureEditor", "Replace existing entry")); m_replace->setObjectName(QStringLiteral("replaceTextureEntry")); m_replace->setAccessibleName(m_replace->text()); form->addRow(m_replace);
	form = propertyTab(QCoreApplication::translate("VibeStudioTextureEditor", "Layers"));
	m_layers = new QListWidget; m_layers->setObjectName(QStringLiteral("textureLayers"));
	m_layers->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Layers, top to bottom"));
	m_layers->setSelectionMode(QAbstractItemView::SingleSelection); m_layers->setIconSize({40, 40}); m_layers->setMinimumHeight(170);
	form->addRow(m_layers);
	connect(m_layers, &QListWidget::currentRowChanged, this, [this](int row) {
		if (row >= 0 && m_layers->item(row)) { selectLayer(m_layers->item(row)->data(Qt::UserRole).toInt()); }
	});
	m_layerName = new QLineEdit; m_layerName->setObjectName(QStringLiteral("textureLayerName")); m_layerName->setMaxLength(128);
	m_layerName->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Layer name"));
	form->addRow(QCoreApplication::translate("VibeStudioTextureEditor", "Name"), m_layerName);
	m_layerVisible = new QCheckBox(QCoreApplication::translate("VibeStudioTextureEditor", "Visible")); m_layerVisible->setObjectName(QStringLiteral("textureLayerVisible"));
	m_layerVisible->setAccessibleName(m_layerVisible->text()); form->addRow(m_layerVisible);
	m_layerLocked = new QCheckBox(QCoreApplication::translate("VibeStudioTextureEditor", "Lock pixels")); m_layerLocked->setObjectName(QStringLiteral("textureLayerLocked"));
	m_layerLocked->setAccessibleName(m_layerLocked->text()); form->addRow(m_layerLocked);
	m_layerOpacity = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Opacity"), QStringLiteral("textureLayerOpacity"), 0, 100); m_layerOpacity->setSuffix(QStringLiteral("%"));
	m_layerBlend = new QComboBox; m_layerBlend->setObjectName(QStringLiteral("textureLayerBlend"));
	m_layerBlend->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Layer blend mode"));
	m_layerBlend->addItem(QCoreApplication::translate("VibeStudioTextureEditor", "Normal"), QStringLiteral("normal"));
	m_layerBlend->addItem(QCoreApplication::translate("VibeStudioTextureEditor", "Multiply"), QStringLiteral("multiply"));
	m_layerBlend->addItem(QCoreApplication::translate("VibeStudioTextureEditor", "Screen"), QStringLiteral("screen"));
	m_layerBlend->addItem(QCoreApplication::translate("VibeStudioTextureEditor", "Add"), QStringLiteral("add"));
	form->addRow(QCoreApplication::translate("VibeStudioTextureEditor", "Blend"), m_layerBlend);
	connect(m_layerName, &QLineEdit::editingFinished, this, &TextureEditorDialog::updateLayerProperties);
	connect(m_layerVisible, &QCheckBox::toggled, this, &TextureEditorDialog::updateLayerProperties);
	connect(m_layerLocked, &QCheckBox::toggled, this, &TextureEditorDialog::updateLayerProperties);
	connect(m_layerOpacity, &QSpinBox::valueChanged, this, &TextureEditorDialog::updateLayerProperties);
	connect(m_layerBlend, &QComboBox::currentIndexChanged, this, &TextureEditorDialog::updateLayerProperties);
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Add Layer"), QStringLiteral("addTextureLayer"), [this]() {
		applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("layer-add")}, {QStringLiteral("name"), QCoreApplication::translate("VibeStudioTextureEditor", "Layer %1").arg(m_document.layers().size() + 1)}}});
	});
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Import Layer…"), QStringLiteral("importTextureLayer"), [this]() { importLayer(); });
	const auto layerCommand = [&](const QString& label, const QString& id, const QString& operation) {
		button(label, id, [this, operation]() { applyOperations({QJsonObject{{QStringLiteral("op"), operation}}}); });
	};
	layerCommand(QCoreApplication::translate("VibeStudioTextureEditor", "Duplicate Layer"), QStringLiteral("duplicateTextureLayer"), QStringLiteral("layer-duplicate"));
	layerCommand(QCoreApplication::translate("VibeStudioTextureEditor", "Remove Layer"), QStringLiteral("removeTextureLayer"), QStringLiteral("layer-remove"));
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Move Layer Up"), QStringLiteral("raiseTextureLayer"), [this]() { applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("layer-move")}, {QStringLiteral("index"), m_document.activeLayerIndex() + 1}}}); });
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Move Layer Down"), QStringLiteral("lowerTextureLayer"), [this]() { applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("layer-move")}, {QStringLiteral("index"), m_document.activeLayerIndex() - 1}}}); });
	layerCommand(QCoreApplication::translate("VibeStudioTextureEditor", "Merge Down"), QStringLiteral("mergeTextureLayer"), QStringLiteral("merge-down"));
	layerCommand(QCoreApplication::translate("VibeStudioTextureEditor", "Flatten"), QStringLiteral("flattenTextureLayers"), QStringLiteral("flatten"));
	form = propertyTab(QCoreApplication::translate("VibeStudioTextureEditor", "Selection"));
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Select All"), QStringLiteral("selectTextureAll"), [this]() { m_canvas->setSelection(QRect(QPoint(), m_document.size())); });
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Deselect"), QStringLiteral("deselectTexture"), [this]() { m_canvas->setSelection({}); });
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Copy Pixels"), QStringLiteral("copyTexturePixels"), [this]() {
		const auto pixels = m_document.copySelection();
		if (pixels.isNull()) { setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Unable to copy the selected pixels.")); return; }
		QApplication::clipboard()->setImage(pixels);
	});
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Cut Pixels"), QStringLiteral("cutTexturePixels"), [this]() {
		const auto* layer = m_document.activeLayer(); if (!layer || layer->locked || !layer->visible) { setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Select a visible, unlocked layer to cut pixels.")); return; }
		const auto pixels = m_document.copySelection();
		if (pixels.isNull()) { setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Unable to copy the selected pixels.")); return; }
		QApplication::clipboard()->setImage(pixels); applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("clear")}}});
	});
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Paste at Cursor"), QStringLiteral("pasteTexturePixels"), [this]() {
		const QImage pixels = QApplication::clipboard()->image(); const QPoint position = m_canvas->cursorPixel();
		runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Pasting pixels…"), [pixels, position](TextureDocument& document, QString* error, const TextureProgress&) { return document.pastePixels(pixels, position, error); });
	});
	layerCommand(QCoreApplication::translate("VibeStudioTextureEditor", "Clear Pixels"), QStringLiteral("clearTexturePixels"), QStringLiteral("clear"));
	auto* moveX = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Move X"), QStringLiteral("textureMoveX"), -4096, 4096);
	auto* moveY = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Move Y"), QStringLiteral("textureMoveY"), -4096, 4096);
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Move Selected Pixels"), QStringLiteral("moveTexturePixels"), [this, moveX, moveY]() { applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("move-pixels")}, {QStringLiteral("x"), moveX->value()}, {QStringLiteral("y"), moveY->value()}}}); });
	m_selectionHint = new QLabel; m_selectionHint->setObjectName(QStringLiteral("textureSelectionHint")); m_selectionHint->setWordWrap(true);
	m_selectionHint->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Selection transform preview")); form->addRow(m_selectionHint);
	m_selectionWidth = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Selection width"), QStringLiteral("textureSelectionWidth"), 1, TextureDocument::MaximumDimension);
	m_selectionHeight = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Selection height"), QStringLiteral("textureSelectionHeight"), 1, TextureDocument::MaximumDimension);
	m_selectionAnchor = anchorChoice(QCoreApplication::translate("VibeStudioTextureEditor", "Selection anchor"), QStringLiteral("textureSelectionAnchor"));
	m_selectionAnchor->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Keep this edge or corner fixed when resizing or rotating selected pixels. Center snaps half-pixel offsets toward the top left."));
	m_selectionSmooth = new QCheckBox(QCoreApplication::translate("VibeStudioTextureEditor", "Smooth selection resampling")); m_selectionSmooth->setAccessibleName(m_selectionSmooth->text()); form->addRow(m_selectionSmooth);
	m_resizeSelection = button(QCoreApplication::translate("VibeStudioTextureEditor", "Resize Selected Pixels"), QStringLiteral("resizeTextureSelection"), [this]() {
		applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("resize-selection")}, {QStringLiteral("width"), m_selectionWidth->value()}, {QStringLiteral("height"), m_selectionHeight->value()},
			{QStringLiteral("smooth"), m_selectionSmooth->isChecked()}, {QStringLiteral("anchor"), m_selectionAnchor->currentData().toString()}}});
	});
	m_rotateSelection = button(QCoreApplication::translate("VibeStudioTextureEditor", "Rotate Selected Pixels Clockwise"), QStringLiteral("rotateTextureSelection"), [this]() {
		applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("rotate-selection")}, {QStringLiteral("anchor"), m_selectionAnchor->currentData().toString()}}});
	});
	const auto transformTip = QCoreApplication::translate("VibeStudioTextureEditor", "Transform only the active layer. Vacated pixels become transparent; transformed pixels replace their destination. The result must fit within the canvas.");
	m_resizeSelection->setToolTip(transformTip); m_rotateSelection->setToolTip(transformTip);
	for (auto* field : {m_width, m_height, m_selectionWidth, m_selectionHeight}) { connect(field, &QSpinBox::valueChanged, this, &TextureEditorDialog::refreshTransforms); }
	for (auto* field : {m_canvasAnchor, m_selectionAnchor}) { connect(field, &QComboBox::currentIndexChanged, this, &TextureEditorDialog::refreshTransforms); }
	form = propertyTab(QCoreApplication::translate("VibeStudioTextureEditor", "Recovery"));
	m_recoveryEnabled = new QCheckBox(QCoreApplication::translate("VibeStudioTextureEditor", "Automatic recovery"));
	m_recoveryEnabled->setObjectName(QStringLiteral("textureRecoveryEnabled")); m_recoveryEnabled->setAccessibleName(m_recoveryEnabled->text());
	m_recoveryEnabled->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Keep unsaved layers and palette settings in a separate local checkpoint. Turning this off retains existing checkpoints."));
	m_recoveryEnabled->setChecked(StudioSettings().textureRecoveryEnabled()); form->addRow(m_recoveryEnabled);
	auto* interval = spin(QCoreApplication::translate("VibeStudioTextureEditor", "Interval (seconds)"), QStringLiteral("textureRecoveryInterval"), 5, 600);
	interval->setValue(StudioSettings().textureRecoveryIntervalSeconds());
	const QString profile = StudioSettings::overrideFilePath();
	m_recoveryDirectory = QDir(profile.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) : QFileInfo(profile).absolutePath()).filePath(QStringLiteral("texture-recovery"));
	m_recoveryId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	m_recovery = new TextureRecoveryWriter(m_recoveryDirectory, this);
	auto* recoveryDirectory = new QLineEdit(QDir::toNativeSeparators(m_recoveryDirectory)); recoveryDirectory->setReadOnly(true); recoveryDirectory->setToolTip(recoveryDirectory->text());
	recoveryDirectory->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Recovery folder")); form->addRow(recoveryDirectory);
	auto* checkpointNow = button(QCoreApplication::translate("VibeStudioTextureEditor", "Checkpoint Now"), QStringLiteral("checkpointTexture"), [this]() {
		if (!hasUnsavedChanges()) { setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "No unsaved changes to checkpoint.")); return; }
		checkpointRecovery();
	});
	checkpointNow->setEnabled(m_recoveryEnabled->isChecked()); connect(m_recoveryEnabled, &QCheckBox::toggled, checkpointNow, &QPushButton::setEnabled);
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Refresh Recovery List"), QStringLiteral("refreshTextureRecoveries"), [this]() { refreshRecoveries(); });
	m_recoveryList = new QListWidget; m_recoveryList->setObjectName(QStringLiteral("textureRecoveryList"));
	m_recoveryList->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Available texture recovery checkpoints"));
	m_recoveryList->setMinimumHeight(180); m_recoveryList->setWordWrap(true); m_recoveryList->setResizeMode(QListView::Adjust); form->addRow(m_recoveryList);
	auto* restore = button(QCoreApplication::translate("VibeStudioTextureEditor", "Restore Selected as Draft"), QStringLiteral("restoreTextureRecovery"), [this]() {
		const auto* item = m_recoveryList->currentItem();
		if (item && item->data(Qt::UserRole + 2).toBool()) { restoreRecovery(item->data(Qt::UserRole).toString(), item->data(Qt::UserRole + 1).toByteArray()); }
	});
	restore->setEnabled(false);
	connect(m_recoveryList, &QListWidget::currentRowChanged, this, [this, restore]() {
		const auto* item = m_recoveryList->currentItem(); restore->setEnabled(item && item->data(Qt::UserRole + 2).toBool());
	});
	button(QCoreApplication::translate("VibeStudioTextureEditor", "Open Recovery File…"), QStringLiteral("openTextureRecovery"), [this]() {
		const auto path = QFileDialog::getOpenFileName(this, QCoreApplication::translate("VibeStudioTextureEditor", "Restore Texture Recovery"), m_recoveryDirectory,
			QCoreApplication::translate("VibeStudioTextureEditor", "Texture recovery (*.vtrecovery)"));
		if (!path.isEmpty()) { restoreRecovery(path); }
	});
	m_recoveryStatus = new QLabel; m_recoveryStatus->setObjectName(QStringLiteral("textureRecoveryStatus")); m_recoveryStatus->setTextFormat(Qt::PlainText); m_recoveryStatus->setWordWrap(true);
	m_recoveryStatus->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Texture recovery status")); canvasLayout->addWidget(m_recoveryStatus);
	m_recoveryTimer = new QTimer(this); m_recoveryTimer->setInterval(interval->value() * 1000);
	connect(m_recoveryTimer, &QTimer::timeout, this, &TextureEditorDialog::checkpointRecovery);
	connect(interval, &QSpinBox::valueChanged, this, [this](int seconds) {
		StudioSettings settings; settings.setTextureRecoveryIntervalSeconds(seconds); settings.sync(); m_recoveryTimer->setInterval(seconds * 1000);
	});
	connect(m_recoveryEnabled, &QCheckBox::toggled, this, [this](bool enabled) {
		StudioSettings settings; settings.setTextureRecoveryEnabled(enabled); settings.sync();
		if (enabled) { m_recoveryTimer->start(); checkpointRecovery(); }
		else { m_recoveryTimer->stop(); m_recoveryStatus->setText(QCoreApplication::translate("VibeStudioTextureEditor", "Automatic recovery is off. Existing checkpoints are retained.")); }
	});
	m_recoveryProgressTimer = new QTimer(this); m_recoveryProgressTimer->setInterval(200);
	connect(m_recoveryProgressTimer, &QTimer::timeout, this, [this]() {
		if (!m_recovery->busy()) { m_recoveryProgressTimer->stop(); return; }
		m_recoveryStatus->setText(QCoreApplication::translate("VibeStudioTextureEditor", "Updating local checkpoint: %1%").arg(m_recovery->progress() / 10));
	});
	m_recovery->finished = [this](const QString& id, const QString& path, const QString& error) {
		if (!error.isEmpty()) { m_recoveryStatus->setText(QCoreApplication::translate("VibeStudioTextureEditor", "Recovery failed: %1").arg(error)); }
		else if (id == m_recoveryId) { m_recoveryStatus->setText(QCoreApplication::translate("VibeStudioTextureEditor", "Local checkpoint updated at %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate))); m_recoveryStatus->setToolTip(path); }
	};
	if (m_recoveryEnabled->isChecked()) { m_recoveryTimer->start(); }
	m_recoveryStatus->setText(m_recoveryEnabled->isChecked() ? QCoreApplication::translate("VibeStudioTextureEditor", "Local recovery enabled.") : QCoreApplication::translate("VibeStudioTextureEditor", "Automatic recovery is off. Existing checkpoints are retained."));
	m_export = new TextureExportPanel;
	auto* exportScroll = new QScrollArea; exportScroll->setWidgetResizable(true); exportScroll->setWidget(m_export);
	exportScroll->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Export"));
	properties->addTab(exportScroll, exportScroll->accessibleName());
	m_export->changed = [this]() { refresh(); };
	m_export->previewRequested = [this]() { previewExport(); };
	m_export->exportRequested = [this]() { exportFile(); };
	properties->setMinimumWidth(std::max(300, fontMetrics().horizontalAdvance(m_dither->text()) + 80));
	auto* propertyPanel = new QWidget; auto* propertyLayout = new QVBoxLayout(propertyPanel); propertyLayout->setContentsMargins(0, 0, 0, 0);
	auto* section = new QComboBox; section->setObjectName(QStringLiteral("texturePropertySection"));
	section->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Inspector section"));
	for (int i = 0; i < properties->count(); ++i) { section->addItem(properties->tabText(i)); }
	connect(section, &QComboBox::currentIndexChanged, properties, &QTabWidget::setCurrentIndex);
	connect(properties, &QTabWidget::currentChanged, section, &QComboBox::setCurrentIndex);
	connect(section, &QComboBox::currentIndexChanged, this, [this](int index) { if (index == 6 && !m_recoveryScanned && !m_busy) { refreshRecoveries(); } });
	properties->tabBar()->hide(); propertyLayout->addWidget(section); propertyLayout->addWidget(properties, 1); m_properties = propertyPanel;
	splitter->addWidget(propertyPanel); splitter->setStretchFactor(0, 1); splitter->setSizes({800, properties->minimumWidth()}); layout->addWidget(splitter, 1);
	m_progress = new QProgressBar; m_progress->setObjectName(QStringLiteral("textureOperationProgress")); m_progress->setRange(0, 0); m_progress->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Texture operation progress")); m_progress->hide(); layout->addWidget(m_progress);
	m_progressTimer = new QTimer(this); m_progressTimer->setInterval(50);
	connect(m_progressTimer, &QTimer::timeout, this, [this]() {
		if (!m_task) { return; } const int completed = m_task->progress.load();
		if (completed >= 0) { m_progress->setRange(0, 1000); m_progress->setValue(completed); }
	});
	m_cancel = new QPushButton(QCoreApplication::translate("VibeStudioTextureEditor", "Cancel Operation")); m_cancel->setObjectName(QStringLiteral("cancelTextureOperation")); m_cancel->setAccessibleName(m_cancel->text()); m_cancel->setAutoDefault(false); m_cancel->hide();
	connect(m_cancel, &QPushButton::clicked, this, &TextureEditorDialog::cancelPending); layout->addWidget(m_cancel);
	m_status = new QLabel; m_status->setObjectName(QStringLiteral("textureEditorStatus")); m_status->setTextFormat(Qt::PlainText); m_status->setWordWrap(true);
	m_status->setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Texture operation status")); layout->addWidget(m_status);
	m_canvas->changed = [this]() { refresh(); };
	m_canvas->statusChanged = [this](const QString& text) { m_pixelLabel->setText(text); };
	m_canvas->operationFailed = [this](const QString& text) { setStatus(text); };
	m_canvas->colorPicked = [this](QColor color) { m_color->setText(color.name(QColor::HexArgb)); };
	m_canvas->fillRequested = [this, tolerance, wrap](QPoint point) {
		applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("fill")}, {QStringLiteral("x"), point.x()}, {QStringLiteral("y"), point.y()},
			{QStringLiteral("color"), m_color->text()}, {QStringLiteral("tolerance"), tolerance->value()}, {QStringLiteral("wrap"), wrap->isChecked()}}});
	};
	m_canvas->shapeRequested = [this](TextureShape shape, QPoint from, QPoint to, const TextureBrush& brush, bool filled) {
		const QString operation = shape == TextureShape::Line ? QStringLiteral("line") : (shape == TextureShape::Rectangle ? QStringLiteral("rectangle") : QStringLiteral("ellipse"));
		applyOperations({QJsonObject{{QStringLiteral("op"), operation}, {QStringLiteral("x"), from.x()}, {QStringLiteral("y"), from.y()},
			{QStringLiteral("x2"), to.x()}, {QStringLiteral("y2"), to.y()}, {QStringLiteral("color"), m_color->text()}, {QStringLiteral("width"), brush.width},
			{QStringLiteral("brush"), textureBrushShapeId(brush.shape)}, {QStringLiteral("mode"), texturePaintModeId(brush.mode)},
			{QStringLiteral("wrap"), brush.wrap}, {QStringLiteral("filled"), filled}}});
	};
	m_canvas->selectionChanged = [this](QRect rect) { m_crop[0]->setValue(rect.x()); m_crop[1]->setValue(rect.y()); m_crop[2]->setValue(rect.width()); m_crop[3]->setValue(rect.height()); refreshTransforms(); };
	connect(m_virtualPath, &QLineEdit::textChanged, this, [this]() { refresh(); });
	setPaletteResolution(m_palette); m_savedMetadata = projectMetadata(); refresh();
	for (auto* toolbar : {m_tools, view}) {
		for (auto* button : toolbar->findChildren<QToolButton*>()) {
			button->setFocusPolicy(Qt::StrongFocus);
			if (!button->defaultAction()) {
				const auto name = QCoreApplication::translate("VibeStudioTextureEditor", "More %1").arg(toolbar->accessibleName());
				button->setAccessibleName(name); button->setToolTip(name);
			}
		}
	}
	updateInspectorWidth();
	setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Ready. New creates a transparent texture using the width and height fields."));
}

TextureEditorDialog::~TextureEditorDialog()
{
	// Workers own document snapshots only, never widgets. Shutdown waits before
	// destroying the thread object; ordinary Close is blocked while busy.
	if (m_task) { m_task->cancelled.store(true); }
	if (m_worker) { m_worker->disconnect(this); m_worker->wait(); delete m_worker; }
	if (!m_closeApproved) { m_busy = false; m_canvas->finishStroke(); checkpointRecovery(); }
	delete m_recovery; m_recovery = nullptr;
}

void TextureEditorDialog::setStatus(const QString& text) { m_status->setText(text); }
void TextureEditorDialog::updateCanvasEnabled()
{
	const auto tool = static_cast<TextureCanvas::Tool>(m_tool->currentIndex());
	m_canvas->setEnabled(!m_busy && ((m_color && QColor(m_color->text()).isValid()) || tool == TextureCanvas::Tool::Eraser || tool == TextureCanvas::Tool::Pick || tool == TextureCanvas::Tool::Select));
}
void TextureEditorDialog::setPaletteResolution(const IdTechPaletteResolution& resolution, bool markChanged)
{
	m_palette = resolution; m_swatches->setPalette(resolution.palette);
	m_export->invalidatePreview();
	const QSignalBlocker blocker(m_paletteChoice);
	m_paletteChoice->setCurrentIndex(m_paletteChoice->findData(resolution.palette.id));
	m_paletteLabel->setText(idTechPaletteSummaryLines(resolution).join(QLatin1Char('\n')));
	if (!markChanged) { m_savedMetadata = projectMetadata(); }
	refresh();
}

void TextureEditorDialog::refreshPaletteSource(const QString& paletteId, bool markChanged)
{
	if (m_busy) { return; }
	const auto source = paletteSource ? paletteSource() : TexturePreviewSource{};
	QString id = paletteId.isEmpty() ? m_paletteChoice->currentData().toString() : paletteId;
	if (id.isEmpty()) { id = m_palette.requestedPaletteId; }
	if (id.isEmpty()) { id = source.paletteId; }
	auto resolved = std::make_shared<IdTechPaletteResolution>();
	runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Loading palette source…"),
		[source, id, resolved](TextureDocument&, QString* error, const TextureProgress& progress) {
			*resolved = resolveTexturePreviewPalette(source, id, [&progress] { return progress && !progress(0, 0); });
			if (!resolved->palette.isValid()) {
				if (error) { *error = QCoreApplication::translate("VibeStudioTextureEditor", "The palette could not be loaded."); }
				return false;
			}
			return !progress || progress(1, 1);
		}, [this, source, resolved, markChanged] {
			if (paletteSource && paletteSource().revision != source.revision) {
				setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Palette sources changed while loading. Refresh the palette source again."));
				return;
			}
			setPaletteResolution(*resolved, markChanged);
			setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Palette source loaded. Existing pixels keep their colors until you remap or export."));
		});
}

bool TextureEditorDialog::setImage(const QImage& image, const QString& source, QString* error)
{
	if (m_busy) { return false; }
	if (!m_document.reset(image, error)) { return false; }
	retireRecovery();
	m_source = source; m_savedPath.clear(); m_projectIdentity = {}; m_extraMetadata = {}; m_replace->setChecked(false);
	m_export->setOptions({});
	m_recoveredDraft = false;
	const QString base = source.contains(QLatin1Char('/')) ? source.left(source.lastIndexOf(QLatin1Char('/')) + 1) : QStringLiteral("textures/custom/");
	m_virtualPath->setText(base + QFileInfo(source).completeBaseName() + QStringLiteral(".png"));
	m_savedMetadata = projectMetadata(); refresh(); m_canvas->fit();
	setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Editing one decoded image. Export settings control game output; Save Project retains editable layers."));
	return true;
}

bool TextureEditorDialog::editImage(const QImage& image, const QString& source, const IdTechPaletteResolution& palette)
{
	if (!confirmDiscard([this, image, source, palette]() { editImage(image, source, palette); })) { return false; }
	QString error;
	if (!setImage(image, source, &error)) { setStatus(error); return false; }
	setPaletteResolution(palette);
	m_savedMetadata = projectMetadata(); refresh();
	return true;
}

bool TextureEditorDialog::editDecodedImage(const IdTechImageDecodeResult& decoded, const QString& source, const IdTechPaletteResolution& palette)
{
	if (!decoded.decoded) { setStatus(decoded.error); return false; }
	if (!confirmDiscard([this, decoded, source, palette]() { editDecodedImage(decoded, source, palette); })) { return false; }
	QString error;
	if (!setImage(decoded.image, source, &error)) { setStatus(error); return false; }
	setPaletteResolution(palette); const auto options = textureExportOptionsForImage(decoded); m_export->setOptions(options);
	QString target = source;
	if ((!context || context().wadMagic.isEmpty()) && QFileInfo(source).suffix().compare(textureExportSuffix(options.format), Qt::CaseInsensitive) != 0) {
		const auto slash = source.lastIndexOf(QLatin1Char('/'));
		target = source.left(slash + 1) + QFileInfo(source).completeBaseName() + QLatin1Char('.') + textureExportSuffix(options.format);
	}
	m_virtualPath->setText(target); m_savedMetadata = projectMetadata(); refresh(); return true;
}

void TextureEditorDialog::refreshContext()
{
	const auto current = context ? context() : TextureEditorContext{};
	if (current.packagePath != m_contextKey) { m_contextKey = current.packagePath; m_replace->setChecked(false); }
	if (current.packageTargetKey != m_packageTargetKey) { m_packageTargetKey = current.packageTargetKey; m_export->invalidatePreview(); }
	m_contextLabel->setText(current.packagePath.isEmpty() ? QCoreApplication::translate("VibeStudioTextureEditor", "No package open · export to a project asset folder.") :
		QCoreApplication::translate("VibeStudioTextureEditor", "Package: %1 · Map: %2").arg(current.packagePath, current.mapPath.isEmpty() ? QCoreApplication::translate("VibeStudioTextureEditor", "none") : current.mapPath));
	m_stage->setEnabled(!m_busy && current.canStage && bool(handoff));
	TextureExportOptions options;
	QString problem;
	const bool validReference = m_export->options(&options, &problem) &&
		!textureExportMapReference(options, m_virtualPath->text(), current.mapFormat, current.wadMagic, &problem).isEmpty();
	m_apply->setEnabled(!m_busy && current.canStage && current.canApply && bool(handoff) && validReference);
	m_apply->setToolTip(!validReference ? problem : QCoreApplication::translate("VibeStudioTextureEditor", "Stage the texture and apply its name to the selected brushes or patches. Save the package and map separately."));
}

void TextureEditorDialog::refresh()
{
	// A cancelled or stale palette request retains the previous palette choice.
	if (!m_busy) { const QSignalBlocker blocker(m_paletteChoice); m_paletteChoice->setCurrentIndex(m_paletteChoice->findData(m_palette.palette.id)); }
	if (m_previewRevision != m_document.revision()) { m_export->invalidatePreview(); m_previewRevision = m_document.revision(); }
	m_undo->setEnabled(!m_busy && m_document.canUndo()); m_redo->setEnabled(!m_busy && m_document.canRedo());
	m_undo->setText(m_document.canUndo() ? QCoreApplication::translate("VibeStudioTextureEditor", "Undo %1").arg(m_document.undoLabel()) : QCoreApplication::translate("VibeStudioTextureEditor", "Undo"));
	m_redo->setText(m_document.canRedo() ? QCoreApplication::translate("VibeStudioTextureEditor", "Redo %1").arg(m_document.redoLabel()) : QCoreApplication::translate("VibeStudioTextureEditor", "Redo"));
	m_canvas->refresh();
	refreshLayers();
	m_width->setValue(m_document.image().width()); m_height->setValue(m_document.image().height());
	refreshTransforms();
	m_dimensions->setText(QCoreApplication::translate("VibeStudioTextureEditor", "%1 × %2 · %3").arg(m_document.image().width()).arg(m_document.image().height()).arg(
		hasUnsavedChanges() ? QCoreApplication::translate("VibeStudioTextureEditor", "Unsaved changes") : QCoreApplication::translate("VibeStudioTextureEditor", "Unmodified")));
	setWindowModified(hasUnsavedChanges());
	setWindowTitle(QCoreApplication::translate("VibeStudioTextureEditor", "Texture Editor — %1[*]").arg(m_source.isEmpty() ? QCoreApplication::translate("VibeStudioTextureEditor", "Untitled") : m_source));
	refreshContext();
}

QJsonObject TextureEditorDialog::projectMetadata() const
{
	auto metadata = m_extraMetadata;
	metadata.insert(QStringLiteral("palette"), texturePaletteMetadata(m_palette));
	metadata.insert(QStringLiteral("packageTexturePath"), m_virtualPath->text());
	metadata.insert(QStringLiteral("export"), m_export->settings());
	return metadata;
}

bool TextureEditorDialog::hasUnsavedChanges() const { return m_document.isDirty() || projectMetadata() != m_savedMetadata; }

void TextureEditorDialog::refreshTransforms()
{
	const QSize size(m_width->value(), m_height->value());
	const auto offset = textureAnchorOffset(m_document.size(), size, static_cast<TextureAnchor>(m_canvasAnchor->currentIndex()));
	const bool validSize = qint64(size.width()) * size.height() <= TextureDocument::MaximumPixels &&
		qint64(size.width()) * size.height() * m_document.layers().size() <= TextureDocument::MaximumLayerPixels;
	m_canvasSizeButton->setEnabled(!m_busy && validSize);
	m_canvasSizeHint->setText(validSize ? QCoreApplication::translate("VibeStudioTextureEditor", "%1 × %2 → %3 × %4 · content offset %5, %6")
		.arg(m_document.size().width()).arg(m_document.size().height()).arg(size.width()).arg(size.height()).arg(offset.x()).arg(offset.y()) :
		QCoreApplication::translate("VibeStudioTextureEditor", "Canvas dimensions exceed the document pixel limit."));
	const QRect selection = m_document.selection();
	if (selection != m_transformSelection) {
		const QSignalBlocker widthBlock(m_selectionWidth), heightBlock(m_selectionHeight);
		m_selectionWidth->setValue(selection.width()); m_selectionHeight->setValue(selection.height()); m_transformSelection = selection;
	}
	const auto* layer = m_document.activeLayer();
	const bool editable = !m_busy && !selection.isEmpty() && layer && layer->visible && !layer->locked;
	m_selectionWidth->setEnabled(editable); m_selectionHeight->setEnabled(editable); m_selectionAnchor->setEnabled(editable); m_selectionSmooth->setEnabled(editable);
	const QSize requested(m_selectionWidth->value(), m_selectionHeight->value());
	const auto anchor = static_cast<TextureAnchor>(m_selectionAnchor->currentIndex());
	const QRect resized(selection.topLeft() + textureAnchorOffset(requested, selection.size(), anchor), requested);
	const QRect rotated(selection.topLeft() + textureAnchorOffset(selection.size().transposed(), selection.size(), anchor), selection.size().transposed());
	const QRect canvas(QPoint(), m_document.size());
	const bool fits = canvas.contains(resized) && qint64(requested.width()) * requested.height() <= TextureDocument::MaximumPixels;
	m_resizeSelection->setEnabled(editable && fits); m_rotateSelection->setEnabled(editable && canvas.contains(rotated));
	QString status = selection.isEmpty() ? QCoreApplication::translate("VibeStudioTextureEditor", "No selection") :
		QCoreApplication::translate("VibeStudioTextureEditor", "%1 × %2 at %3, %4 → %5 × %6 at %7, %8")
			.arg(selection.width()).arg(selection.height()).arg(selection.x()).arg(selection.y()).arg(requested.width()).arg(requested.height()).arg(resized.x()).arg(resized.y());
	if (!selection.isEmpty() && layer && (!layer->visible || layer->locked)) { status += QLatin1Char('\n') + QCoreApplication::translate("VibeStudioTextureEditor", "Active layer is hidden or locked."); }
	else if (!selection.isEmpty() && !fits) { status += QLatin1Char('\n') + QCoreApplication::translate("VibeStudioTextureEditor", "Resize would exceed the canvas."); }
	if (!selection.isEmpty() && !canvas.contains(rotated)) { status += QLatin1Char('\n') + QCoreApplication::translate("VibeStudioTextureEditor", "Rotation would exceed the canvas."); }
	m_selectionHint->setText(status);
}

void TextureEditorDialog::refreshLayers()
{
	if (!m_layers) { return; }
	const QSignalBlocker listBlock(m_layers), nameBlock(m_layerName), visibleBlock(m_layerVisible), lockedBlock(m_layerLocked), opacityBlock(m_layerOpacity), blendBlock(m_layerBlend);
	m_layers->clear();
	for (int index = int(m_document.layers().size()) - 1; index >= 0; --index) {
		const auto& layer = m_document.layers()[index];
		QStringList state;
		if (!layer.visible) { state << QCoreApplication::translate("VibeStudioTextureEditor", "Hidden"); }
		if (layer.locked) { state << QCoreApplication::translate("VibeStudioTextureEditor", "Locked"); }
		state << QStringLiteral("%1%").arg(layer.opacity);
		auto* item = new QListWidgetItem(QIcon(QPixmap::fromImage(layer.pixels.scaled(40, 40, Qt::KeepAspectRatio))), layer.name + QLatin1Char('\n') + state.join(QStringLiteral(" · ")), m_layers);
		item->setData(Qt::UserRole, index); item->setData(Qt::AccessibleTextRole, item->text());
		if (index == m_document.activeLayerIndex()) { m_layers->setCurrentItem(item); }
	}
	if (const auto* layer = m_document.activeLayer()) {
		m_layerName->setText(layer->name); m_layerVisible->setChecked(layer->visible); m_layerLocked->setChecked(layer->locked);
		m_layerOpacity->setValue(layer->opacity); m_layerBlend->setCurrentIndex(m_layerBlend->findData(textureBlendModeId(layer->blend)));
	}
}

void TextureEditorDialog::selectLayer(int index)
{
	if (m_busy) { return; }
	m_canvas->finishStroke(); if (m_document.selectLayer(index)) { refresh(); }
}

void TextureEditorDialog::updateLayerProperties()
{
	if (m_busy || !m_document.activeLayer()) { return; }
	const auto& layer = *m_document.activeLayer();
	if (layer.name == m_layerName->text().trimmed() && layer.visible == m_layerVisible->isChecked() && layer.locked == m_layerLocked->isChecked() &&
		layer.opacity == m_layerOpacity->value() && textureBlendModeId(layer.blend) == m_layerBlend->currentData().toString()) { return; }
	applyOperations({QJsonObject{{QStringLiteral("op"), QStringLiteral("layer-properties")}, {QStringLiteral("name"), m_layerName->text()},
		{QStringLiteral("visible"), m_layerVisible->isChecked()}, {QStringLiteral("locked"), m_layerLocked->isChecked()},
		{QStringLiteral("opacity"), m_layerOpacity->value()}, {QStringLiteral("blend"), m_layerBlend->currentData().toString()}}});
}

void TextureEditorDialog::importLayer()
{
	const QString path = QFileDialog::getOpenFileName(this, QCoreApplication::translate("VibeStudioTextureEditor", "Import Texture Layer"), {},
		assetImageOpenFilter(false));
	if (path.isEmpty()) { return; }
	const auto palette = m_palette.palette;
	runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Importing layer…"), [path, palette](TextureDocument& document, QString* error, const TextureProgress& progress) {
		TextureDocument imported;
		return loadTextureFile(path, palette, &imported, error, nullptr, progress) && document.addLayer(QFileInfo(path).completeBaseName().left(128), imported.image(), error);
	});
}

void TextureEditorDialog::runJob(const QString& title, std::function<bool(TextureDocument&, QString*, const TextureProgress&)> operation, std::function<void()> completed, bool cancellable)
{
	if (m_busy) { return; }
	m_canvas->finishStroke(); m_busy = true; m_cancelled = false; m_cancellable = cancellable;
	m_tools->setEnabled(false); m_properties->setEnabled(false); m_canvas->setEnabled(false);
	m_task = std::make_shared<TextureEditorTask>(); m_progressTimer->start(); m_progress->setRange(0, 0);
	m_progress->show(); m_cancel->setVisible(cancellable); m_cancel->setEnabled(true); setStatus(title); refreshContext();
	struct Result { TextureDocument document; QString error; bool ok = false; };
	auto result = std::make_shared<Result>(); result->document = m_document;
	m_worker = QThread::create([result, task = m_task, operation = std::move(operation)]() {
		const auto progress = [task](qint64 completed, qint64 total) {
			if (task->cancelled.load()) { return false; }
			if (total > 0) { task->progress.store(int(std::clamp<qint64>(completed * 1000 / total, 0, 1000))); }
			return true;
		};
		result->ok = progress(0, 0) && operation(result->document, &result->error, progress);
		// Compose transformed layers on the worker before publishing to the UI.
		if (result->ok && progress(1, 1) && result->document.image().isNull()) {
			result->ok = false;
			result->error = QCoreApplication::translate("VibeStudioTextureEditor", "Unable to compose the texture preview.");
		}
	});
	connect(m_worker, &QThread::finished, this, [this, result, completed = std::move(completed)]() {
		m_worker->wait(); m_worker->deleteLater(); m_worker = nullptr;
		m_progressTimer->stop(); m_task.reset();
		m_busy = false; m_cancellable = false; m_tools->setEnabled(true); m_properties->setEnabled(true); updateCanvasEnabled();
		m_progress->hide(); m_cancel->hide();
		if (m_cancelled) { setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Cancelled. The texture is unchanged.")); }
		else if (!result->ok) { setStatus(result->error); }
		else { m_document = std::move(result->document); setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Completed.")); if (completed) { completed(); } }
		refresh();
	});
	m_worker->start();
}

void TextureEditorDialog::applyOperations(const QJsonArray& operations)
{
	const auto palette = m_palette.palette;
	runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Applying texture operation…"), [operations, palette](TextureDocument& document, QString* error, const TextureProgress& progress) {
		return applyTextureOperations(&document, operations, palette, error, progress);
	});
}

void TextureEditorDialog::cancelPending()
{
	if (!m_busy || !m_cancellable) { return; }
	m_cancelled = true; m_cancel->setEnabled(false);
	if (m_task) { m_task->cancelled.store(true); }
	setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Cancelling texture operation…"));
}

void TextureEditorDialog::undo()
{
	if (m_busy) { return; }
	m_canvas->finishStroke(); auto previous = m_document;
	if (!previous.undo()) { return; }
	if (qint64(previous.size().width()) * previous.size().height() * previous.layers().size() > TextureDocument::MaximumPixels) {
		runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Undoing texture edit…"), [previous = std::move(previous)](TextureDocument& document, QString*, const TextureProgress&) { document = previous; return true; });
	} else { m_document = std::move(previous); refresh(); }
}
void TextureEditorDialog::redo()
{
	if (m_busy) { return; }
	m_canvas->finishStroke(); auto next = m_document;
	if (!next.redo()) { return; }
	if (qint64(next.size().width()) * next.size().height() * next.layers().size() > TextureDocument::MaximumPixels) {
		runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Redoing texture edit…"), [next = std::move(next)](TextureDocument& document, QString*, const TextureProgress&) { document = next; return true; });
	} else { m_document = std::move(next); refresh(); }
}

bool TextureEditorDialog::confirmDiscard(std::function<void()> afterSave)
{
	if (m_busy) { setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Wait for the current operation to finish.")); return false; }
	m_canvas->finishStroke();
	if (!hasUnsavedChanges()) { return true; }
	const auto choice = QMessageBox::warning(this, QCoreApplication::translate("VibeStudioTextureEditor", "Unsaved texture"),
		QCoreApplication::translate("VibeStudioTextureEditor", "Save the texture project before continuing? The project preserves layers and palette settings. Exported images and staged packages are separate."),
		QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
	if (choice == QMessageBox::Save) {
		if (m_projectIdentity.path.isEmpty()) { saveAs(std::move(afterSave)); }
		else { saveProjectToPath(m_projectIdentity.path, true, std::move(afterSave)); }
		return false;
	}
	return choice == QMessageBox::Discard;
}

void TextureEditorDialog::newTexture()
{
	const QSize size(m_width->value(), m_height->value());
	if (!confirmDiscard([this, size]() { m_width->setValue(size.width()); m_height->setValue(size.height()); newTexture(); })) { return; }
	QString error;
	if (!m_document.create(size, Qt::transparent, &error)) { setStatus(error); return; }
	retireRecovery();
	m_source.clear(); m_savedPath.clear(); m_projectIdentity = {}; m_extraMetadata = {}; m_replace->setChecked(false); m_virtualPath->setText(QStringLiteral("textures/custom/texture.png"));
	m_export->setOptions({});
	m_recoveredDraft = false;
	refresh(); m_canvas->fit();
	setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "New transparent texture. Paint pixels or import an image."));
}

void TextureEditorDialog::openFile()
{
	const QString path = QFileDialog::getOpenFileName(this, QCoreApplication::translate("VibeStudioTextureEditor", "Open Texture"), m_savedPath,
		assetImageOpenFilter(true));
	if (path.isEmpty()) { return; }
	openFromPath(path);
}

void TextureEditorDialog::openFromPath(const QString& path)
{
	if (!confirmDiscard([this, path]() { openFromPath(path); })) { return; }
	const auto palette = m_palette.palette;
	struct Loaded { TextureProjectIdentity identity; QJsonObject metadata; IdTechPaletteResolution palette; bool hasPalette = false; };
	auto loaded = std::make_shared<Loaded>();
	const bool project = QFileInfo(path).suffix().compare(QStringLiteral("vtexture"), Qt::CaseInsensitive) == 0;
	runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Loading texture…"), [path, palette, project, loaded](TextureDocument& document, QString* error, const TextureProgress& progress) {
		if (!project) {
			IdTechImageDecodeResult decoded;
			if (!loadTextureFile(path, palette, &document, error, &decoded, progress)) { return false; }
			loaded->metadata.insert(QStringLiteral("export"), textureExportOptionsJson(textureExportOptionsForImage(decoded))); return true;
		}
		if (!readTextureProject(path, &document, &loaded->identity, &loaded->metadata, error, progress)) { return false; }
		loaded->hasPalette = loaded->metadata.contains(QStringLiteral("palette"));
		return !loaded->hasPalette || texturePaletteFromMetadata(loaded->metadata.value(QStringLiteral("palette")).toObject(), &loaded->palette, error);
	}, [this, path, project, loaded]() {
		retireRecovery();
		m_source = path; m_savedPath.clear(); m_projectIdentity = loaded->identity; m_extraMetadata = loaded->metadata; m_replace->setChecked(false);
		m_recoveredDraft = false;
		if (loaded->hasPalette) { setPaletteResolution(loaded->palette); }
		loadExportSettings(loaded->metadata);
		m_virtualPath->setText(loaded->metadata.value(QStringLiteral("packageTexturePath")).toString(QStringLiteral("textures/custom/") + QFileInfo(path).completeBaseName() + QStringLiteral(".png")));
		m_savedMetadata = projectMetadata(); m_canvas->fit();
		setStatus(project ? QCoreApplication::translate("VibeStudioTextureEditor", "Loaded texture project with editable layers.") :
			QCoreApplication::translate("VibeStudioTextureEditor", "Loaded one image. Supported native metadata is available in Export; mipmaps are regenerated from edited pixels. Sprite frames are not editable here."));
	});
}

void TextureEditorDialog::saveAs(std::function<void()> completed)
{
	if (m_busy) { return; }
	QString path = QFileDialog::getSaveFileName(this, QCoreApplication::translate("VibeStudioTextureEditor", "Save Texture Project"),
		m_projectIdentity.path.isEmpty() ? QStringLiteral("texture.vtexture") : m_projectIdentity.path, QCoreApplication::translate("VibeStudioTextureEditor", "VibeStudio texture (*.vtexture)"));
	if (path.isEmpty()) { return; }
	if (QFileInfo(path).suffix().isEmpty()) { path += QStringLiteral(".vtexture"); }
	saveProjectToPath(path, true, std::move(completed));
}

void TextureEditorDialog::saveProjectToPath(const QString& path, bool overwrite, std::function<void()> completed)
{
	TextureProjectSaveRequest request; request.path = path; request.overwrite = overwrite;
	if (m_recoveredDraft) {
		request.overwrite = false;
		if (QFileInfo::exists(path)) { setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Save the recovered draft to a new project file. Existing files cannot be replaced by its first save.")); return; }
	}
	if (!m_projectIdentity.path.isEmpty() && (QFileInfo(path).absoluteFilePath() == m_projectIdentity.path || QFileInfo(path).canonicalFilePath() == m_projectIdentity.canonicalPath)) {
		request.expectedSha256 = m_projectIdentity.sha256; request.expectedCanonicalPath = m_projectIdentity.canonicalPath;
	}
	const auto metadata = projectMetadata(); auto prepared = std::make_shared<TextureProjectPreparedSave>();
	runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Preparing texture project…"), [request, metadata, prepared](TextureDocument& document, QString* error, const TextureProgress& progress) {
		*prepared = prepareTextureProjectSave(document, request, metadata, progress);
		*error = prepared->report.error; return prepared->report.succeeded;
	}, [this, metadata, prepared, completed = std::move(completed)]() {
		auto result = std::make_shared<TextureProjectSaveReport>();
		runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Saving texture project…"), [prepared, result](TextureDocument& document, QString* error, const TextureProgress&) {
			*result = publishTextureProjectSave(*prepared); if (!result->succeeded) { *error = result->error; return false; }
			document.markSaved(); return true;
		}, [this, metadata, result, completed]() {
			m_projectIdentity = result->identity; m_savedMetadata = metadata; m_source = result->identity.path;
			m_recoveredDraft = false;
			retireRecovery();
			setStatus(result->backupPath.isEmpty() ? QCoreApplication::translate("VibeStudioTextureEditor", "Saved texture project: %1").arg(m_source) :
				QCoreApplication::translate("VibeStudioTextureEditor", "Saved texture project: %1\nPrevious version: %2").arg(m_source, result->backupPath));
			if (completed) { completed(); }
		}, false);
	});
}

void TextureEditorDialog::exportFile()
{
	if (m_busy) { return; }
	TextureExportOptions options; QString error;
	if (!m_export->options(&options, &error)) { setStatus(error); return; }
	const QString suffix = textureExportSuffix(options.format);
	const auto suggested = m_savedPath.isEmpty() ? QStringLiteral("texture.") + suffix : QFileInfo(m_savedPath).absolutePath() + QLatin1Char('/') + QFileInfo(m_savedPath).completeBaseName() + QLatin1Char('.') + suffix;
	QString path = QFileDialog::getSaveFileName(this, QCoreApplication::translate("VibeStudioTextureEditor", "Export Texture"), suggested,
		QCoreApplication::translate("VibeStudioTextureEditor", "Texture output (*.%1)").arg(suffix));
	if (path.isEmpty()) { return; }
	if (QFileInfo(path).suffix().isEmpty()) { path += QLatin1Char('.') + suffix; }
	saveExportToPath(path, true);
}

void TextureEditorDialog::loadExportSettings(const QJsonObject& metadata)
{
	TextureExportOptions options; textureExportOptionsFromJson(metadata.value(QStringLiteral("export")).toObject(), &options);
	m_export->setOptions(options);
}
void TextureEditorDialog::setExportOptions(const TextureExportOptions& options) { if (!m_busy) { m_export->setOptions(options); refresh(); } }
bool TextureEditorDialog::exportOptions(TextureExportOptions* options, QString* error) const { return m_export->options(options, error); }
void TextureEditorDialog::previewExport()
{
	if (m_busy) { return; }
	TextureExportOptions options; QString error;
	if (!m_export->options(&options, &error)) { setStatus(error); return; }
	m_export->invalidatePreview(); const auto palette = m_palette; auto result = std::make_shared<TextureExportResult>();
	runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Preparing export preview…"), [options, palette, result](TextureDocument& document, QString* error, const TextureProgress& progress) {
		*result = encodeTextureExport(document.image(), options, palette, progress); *error = result->error; return result->succeeded;
	}, [this, result, palette]() { m_previewRevision = m_document.revision(); m_export->showResult(*result, palette); setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Export validated. Review the preview and warnings before packaging.")); });
}
void TextureEditorDialog::saveExportToPath(const QString& path, bool overwrite, std::function<void()> completed)
{
	if (m_busy) { return; }
	TextureExportOptions options; QString error;
	if (!m_export->options(&options, &error)) { setStatus(error); return; }
	if (QFileInfo(path).suffix().compare(textureExportSuffix(options.format), Qt::CaseInsensitive) != 0) { setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "The filename extension must match the selected export profile.")); return; }
	const auto palette = m_palette; auto result = std::make_shared<TextureExportResult>(); auto target = std::make_shared<TextureOutputTarget>();
	// Capture before encoding. Cancellation can discard prepared bytes; publication
	// is a separate short, non-cancellable job so a committed file is never reported cancelled.
	runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Encoding texture export…"), [path, overwrite, target, options, palette, result](TextureDocument& document, QString* error, const TextureProgress& progress) {
		if (!inspectTextureOutputTarget(path, overwrite, target.get(), error, [&] { return !progress(0, 1); })) { return false; }
		*result = encodeTextureExport(document.image(), options, palette, progress); *error = result->error; return result->succeeded;
	}, [this, path, target, result, palette, completed = std::move(completed)]() {
		runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Publishing texture export…"), [target, result](TextureDocument&, QString* error, const TextureProgress&) { return writeTextureOutput(*target, result->bytes, false, error); },
			[this, path, result, palette, completed]() {
				m_savedPath = path; m_previewRevision = m_document.revision(); m_export->showResult(*result, palette);
				setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Exported: %1. Save Project preserves editable layers and export settings.").arg(path)); if (completed) { completed(); }
			}, false);
	});
}

void TextureEditorDialog::saveToPath(const QString& path, bool overwrite, std::function<void()> completed)
{
	runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Saving PNG…"), [path, overwrite](TextureDocument& document, QString* error, const TextureProgress&) {
		return document.savePng(path, overwrite, false, error);
	}, [this, path, completed = std::move(completed)]() { m_savedPath = path; setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Exported PNG: %1. Save the project to preserve editable layers.").arg(path)); if (completed) { completed(); } }, false);
}

void TextureEditorDialog::stage(bool applyToMap)
{
	refreshContext();
	if (m_busy || !(applyToMap ? m_apply : m_stage)->isEnabled()) { return; }
	const auto current = context(); const auto path = m_virtualPath->text(); const bool replace = m_replace->isChecked();
	TextureExportOptions options; QString error;
	if (!m_export->options(&options, &error)) { setStatus(error); return; }
	const auto palette = m_palette; auto result = std::make_shared<TextureExportResult>();
	runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Preparing texture for package staging…"), [result, options, palette](TextureDocument& document, QString* error, const TextureProgress& progress) {
		*result = encodeTextureExport(document.image(), options, palette, progress); *error = result->error; return result->succeeded;
	}, [this, result, options, palette, path, replace, applyToMap, current]() {
			const auto now = context();
			if (!now.canStage || now.packagePath != current.packagePath || now.packageTargetKey != current.packageTargetKey || (applyToMap && (!now.canApply || now.mapPath != current.mapPath || now.mapTargetKey != current.mapTargetKey))) { setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "The project context changed. Stage again after reviewing the target.")); return; }
			QString error;
			if (!handoff(*result, options, path, replace, applyToMap, &error)) { setStatus(error); return; }
			m_previewRevision = m_document.revision(); m_export->showResult(*result, palette);
			setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Texture staged. Save the package separately; map edits also remain unsaved."));
		});
}

QString TextureEditorDialog::recoveryPath() const { return textureRecoveryPath(m_recoveryDirectory, m_recoveryId); }
bool TextureEditorDialog::recoveryBusy() const { return m_recovery && m_recovery->busy(); }

void TextureEditorDialog::retireRecovery()
{
	if (m_recoveryStatus) { m_recoveryStatus->clear(); m_recoveryStatus->setToolTip({}); }
	if (m_recovery) { m_recovery->retire(m_recoveryId); }
	m_recoveryId = QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void TextureEditorDialog::checkpointRecovery()
{
	// Wait for a gesture or foreground operation to commit; never split a stroke
	// or serialize a partially edited layer under its previous revision number.
	if (m_busy || m_document.strokeActive() || m_closeApproved || !m_recovery) { return; }
	if (!hasUnsavedChanges()) { retireRecovery(); return; }
	if (!m_recoveryEnabled->isChecked()) { return; }
	TextureRecoverySnapshot snapshot{m_document.storageSnapshot(), projectMetadata(), m_projectIdentity, QFileInfo(m_source).fileName()};
	if (snapshot.displayName.isEmpty()) { snapshot.displayName = QCoreApplication::translate("VibeStudioTextureEditor", "Untitled texture"); }
	if (m_recovery->checkpoint(m_recoveryId, std::move(snapshot))) {
		m_recoveryStatus->setText(QCoreApplication::translate("VibeStudioTextureEditor", "Updating local checkpoint…")); m_recoveryProgressTimer->start();
	}
}

void TextureEditorDialog::refreshRecoveries()
{
	if (m_busy) { return; }
	auto records = std::make_shared<TextureRecoveryList>();
	runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Inspecting texture checkpoints…"), [directory = m_recoveryDirectory, records](TextureDocument&, QString* error, const TextureProgress& progress) {
		*records = listTextureRecoveries(directory, progress);
		if (!records->error.isEmpty()) { *error = records->error; return false; } return !records->cancelled;
	}, [this, records]() {
		m_recoveryScanned = true; m_recoveryList->clear();
		for (const auto& record : records->records) {
			const QString name = record.displayName.isEmpty() ? QFileInfo(record.path).fileName() : record.displayName;
			auto* item = new QListWidgetItem(record.isValid() ? QCoreApplication::translate("VibeStudioTextureEditor", "%1\n%2\n%3 × %4 · Layers: %5").arg(name, record.writtenUtc.toLocalTime().toString(Qt::ISODate)).arg(record.size.width()).arg(record.size.height()).arg(record.layerCount) :
				QCoreApplication::translate("VibeStudioTextureEditor", "%1\nUnavailable: %2").arg(name, record.error), m_recoveryList);
			item->setData(Qt::UserRole, record.path); item->setData(Qt::UserRole + 1, record.recordSha256); item->setData(Qt::UserRole + 2, record.isValid()); item->setData(Qt::AccessibleTextRole, item->text());
			item->setToolTip(QCoreApplication::translate("VibeStudioTextureEditor", "Checkpoint: %1\nRecorded source: %2").arg(record.path, record.sourcePath));
		}
		setStatus(records->truncated ? QCoreApplication::translate("VibeStudioTextureEditor", "Recovery scan is bounded to 8,192 directory entries and 256 files. Use Open Recovery File for an unlisted checkpoint.") :
			QCoreApplication::translate("VibeStudioTextureEditor", "Recovery checkpoints found: %1. Restore creates an unsaved draft; save it as a new project.").arg(records->records.size()));
	});
}

void TextureEditorDialog::restoreRecovery(const QString& path, const QByteArray& expectedRecordSha256)
{
	if (m_busy) { setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Wait for the current operation to finish.")); return; }
	const QFileInfo selected(path), current(recoveryPath());
	const bool ownCheckpoint = selected == current || (!selected.canonicalFilePath().isEmpty() && QFileInfo(selected.canonicalFilePath()) == QFileInfo(current.canonicalFilePath()));
	if (ownCheckpoint && recoveryBusy()) { setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Wait for the local checkpoint write to finish, then restore again.")); return; }
	// Detach our selected checkpoint before prompting: choosing Save must not
	// retire the very file about to be restored. It stays available on Cancel.
	if (ownCheckpoint) { m_recoveryId = QUuid::createUuid().toString(QUuid::WithoutBraces); }
	if (!confirmDiscard([this, path, expectedRecordSha256]() { restoreRecovery(path, expectedRecordSha256); })) { return; }
	struct Restored { QJsonObject metadata; IdTechPaletteResolution palette; bool hasPalette = false; };
	auto restored = std::make_shared<Restored>();
	runJob(QCoreApplication::translate("VibeStudioTextureEditor", "Restoring texture checkpoint…"), [path, expectedRecordSha256, restored](TextureDocument& document, QString* error, const TextureProgress& progress) {
		if (!restoreTextureRecovery(path, &document, &restored->metadata, nullptr, error, expectedRecordSha256, progress)) { return false; }
		restored->hasPalette = restored->metadata.contains(QStringLiteral("palette"));
		return !restored->hasPalette || texturePaletteFromMetadata(restored->metadata.value(QStringLiteral("palette")).toObject(), &restored->palette, error);
	}, [this, restored]() {
		// Restoration never adopts the checkpoint's recorded source identity.
		retireRecovery();
		m_projectIdentity = {}; m_savedPath.clear(); m_extraMetadata = restored->metadata;
		loadExportSettings(restored->metadata);
		m_recoveredDraft = true;
		m_source = QCoreApplication::translate("VibeStudioTextureEditor", "Recovered texture"); m_replace->setChecked(false);
		if (restored->hasPalette) { setPaletteResolution(restored->palette); }
		m_virtualPath->setText(restored->metadata.value(QStringLiteral("packageTexturePath")).toString(QStringLiteral("textures/custom/recovered.png")));
		m_savedMetadata = {}; m_document.markUnsaved(); m_canvas->fit(); checkpointRecovery();
		setStatus(QCoreApplication::translate("VibeStudioTextureEditor", "Checkpoint restored as an unsaved draft. Save Project As chooses a new destination. The original project and selected checkpoint are unchanged."));
	});
}

bool TextureEditorDialog::requestClose(std::function<void()> afterDeferredSave)
{
	return approveClose(std::move(afterDeferredSave)) && close();
}

bool TextureEditorDialog::approveClose(std::function<void()> afterDeferredSave)
{
	if (m_closeApproved) { return true; }
	if (confirmDiscard([this, afterDeferredSave]() {
		// Recheck in case another studio surface changed metadata during Save.
		if (requestClose(afterDeferredSave) && afterDeferredSave) { afterDeferredSave(); }
	})) {
		m_closeApproved = true; retireRecovery(); return true;
	}
	return false;
}

void TextureEditorDialog::closeEvent(QCloseEvent* event)
{
	if (approveClose()) { event->accept(); } else { event->ignore(); }
}
void TextureEditorDialog::reject() { close(); }
void TextureEditorDialog::updateInspectorWidth()
{
	if (!m_properties) { return; }
	auto* properties = m_properties->findChild<QTabWidget*>(QStringLiteral("textureProperties"));
	if (!properties) { return; }
	int minimum = 300;
	for (auto* scroll : properties->findChildren<QScrollArea*>()) {
		if (!scroll->widget()) { continue; }
		scroll->widget()->layout()->invalidate();
		minimum = std::max(minimum, scroll->widget()->minimumSizeHint().width() + scroll->verticalScrollBar()->sizeHint().width() + 2 * scroll->frameWidth() + 8);
	}
	properties->setMinimumWidth(minimum);
}
void TextureEditorDialog::changeEvent(QEvent* event)
{
	QDialog::changeEvent(event);
	if (event->type() == QEvent::ActivationChange && isActiveWindow()) { refreshContext(); }
	if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
		// Inherited fonts/style propagate to children after this event. Size the
		// inspector from the completed control layouts, including translated text.
		QTimer::singleShot(0, this, [this]() { updateInspectorWidth(); });
	}
}

} // namespace vibestudio
