#include "app/model_design_dialog.h"

#include "app/model_viewport.h"

#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>

namespace vibestudio
{

ModelDesignDialog::ModelDesignDialog(QWidget* parent) : QDialog(parent)
{
	setObjectName(QStringLiteral("modelDesignDialog"));
	setWindowTitle(QCoreApplication::translate("VibeStudioModelDesignDialog", "Model Design"));
	setAccessibleName(QCoreApplication::translate("VibeStudioModelDesignDialog", "Model design editor"));
	setAttribute(Qt::WA_DeleteOnClose);
	resize(1220, 800);
	auto* layout = new QVBoxLayout(this);
	auto* tools = new QToolBar(QCoreApplication::translate("VibeStudioModelDesignDialog", "Model design actions"));
	tools->setAccessibleName(QCoreApplication::translate("VibeStudioModelDesignDialog", "Model design actions"));
	tools->setToolButtonStyle(Qt::ToolButtonTextOnly);
	const auto action = [&](const QString& text, const QString& name, auto callback) {
		auto* result = tools->addAction(text);
		result->setObjectName(name);
		connect(result, &QAction::triggered, this, callback);
		return result;
	};
	action(QCoreApplication::translate("VibeStudioModelDesignDialog", "Open Design…"), QStringLiteral("openModelDesign"),
	       [this]() { openDesign(); });
	action(QCoreApplication::translate("VibeStudioModelDesignDialog", "Save Design…"), QStringLiteral("saveModelDesign"),
	       [this]() { saveDesign(); });
	tools->addSeparator();
	m_undoAction =
	    action(QCoreApplication::translate("VibeStudioModelDesignDialog", "Undo"), QStringLiteral("undoModelDesign"), [this]() { undo(); });
	m_redoAction =
	    action(QCoreApplication::translate("VibeStudioModelDesignDialog", "Redo"), QStringLiteral("redoModelDesign"), [this]() { redo(); });
	m_undoAction->setShortcut(QKeySequence::Undo);
	m_redoAction->setShortcut(QKeySequence::Redo);
	m_undoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	m_redoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	addAction(m_undoAction);
	addAction(m_redoAction);
	tools->addSeparator();
	m_exportMd3 = action(QCoreApplication::translate("VibeStudioModelDesignDialog", "Export MD3…"), QStringLiteral("exportDesignMd3"),
	                     [this]() { exportDesign(QStringLiteral("md3")); });
	m_exportObj = action(QCoreApplication::translate("VibeStudioModelDesignDialog", "Export OBJ…"), QStringLiteral("exportDesignObj"),
	                     [this]() { exportDesign(QStringLiteral("obj")); });
	m_stageAction = action(QCoreApplication::translate("VibeStudioModelDesignDialog", "Stage in Package"),
	                       QStringLiteral("stageModelDesign"), [this]() { stage(false); });
	m_placeAction = action(QCoreApplication::translate("VibeStudioModelDesignDialog", "Stage and Place"),
	                       QStringLiteral("placeModelDesign"), [this]() { stage(true); });
	m_stageAction->setToolTip(QCoreApplication::translate("VibeStudioModelDesignDialog",
	                                                      "Add the generated MD3 to the package plan. Save the package separately."));
	m_placeAction->setToolTip(QCoreApplication::translate(
	    "VibeStudioModelDesignDialog",
	    "Stage the MD3 and add one undoable misc_model entity to the current Quake III map. Save the map and package separately."));
	layout->addWidget(tools);
	auto* bake = tools->addAction(QCoreApplication::translate("VibeStudioModelDesignDialog", "Edit as Mesh"));
	bake->setObjectName(QStringLiteral("editDesignAsMesh"));
	bake->setToolTip(QCoreApplication::translate("VibeStudioModelDesignDialog", "Open a baked copy in the mesh editor. Primitive parameters stay in this design source."));
	connect(bake, &QAction::triggered, this, [this]() { if (editMesh) { editMesh(buildModelDesignMesh(m_design)); } });
	m_contextLabel = new QLabel;
	m_contextLabel->setWordWrap(true);
	m_contextLabel->setTextFormat(Qt::PlainText);
	m_contextLabel->setAccessibleName(QCoreApplication::translate("VibeStudioModelDesignDialog", "Model design project context"));
	layout->addWidget(m_contextLabel);
	auto* splitter = new QSplitter;
	auto* partPanel = new QWidget;
	auto* partLayout = new QVBoxLayout(partPanel);
	partLayout->setContentsMargins(0, 0, 0, 0);
	m_parts = new QListWidget;
	m_parts->setObjectName(QStringLiteral("designParts"));
	m_parts->setAccessibleName(QCoreApplication::translate("VibeStudioModelDesignDialog", "Model parts"));
	m_parts->setAccessibleDescription(QCoreApplication::translate(
	    "VibeStudioModelDesignDialog", "Select a primitive to edit its dimensions, position, rotation, and material."));
	partLayout->addWidget(m_parts, 1);
	auto* partTools = new QToolBar(QCoreApplication::translate("VibeStudioModelDesignDialog", "Part actions"));
	partTools->setAccessibleName(QCoreApplication::translate("VibeStudioModelDesignDialog", "Part actions"));
	const QStringList primitiveIds{QStringLiteral("box"), QStringLiteral("cylinder"), QStringLiteral("plane")};
	const QStringList primitiveNames{QCoreApplication::translate("VibeStudioModelDesignDialog", "Box"),
	                                 QCoreApplication::translate("VibeStudioModelDesignDialog", "Cylinder"),
	                                 QCoreApplication::translate("VibeStudioModelDesignDialog", "Plane")};
	for (int i = 0; i < primitiveIds.size(); ++i) {
		auto* add = partTools->addAction(primitiveNames[i]);
		add->setToolTip(QCoreApplication::translate("VibeStudioModelDesignDialog", "Add a %1 part").arg(primitiveNames[i]));
		connect(add, &QAction::triggered, this, [this, id = primitiveIds[i]]() { addPart(id); });
	}
	partLayout->addWidget(partTools);
	auto* editTools = new QToolBar(QCoreApplication::translate("VibeStudioModelDesignDialog", "Part management"));
	editTools->setAccessibleName(QCoreApplication::translate("VibeStudioModelDesignDialog", "Part management"));
	connect(editTools->addAction(QCoreApplication::translate("VibeStudioModelDesignDialog", "Duplicate")), &QAction::triggered, this,
	        [this]() { duplicatePart(); });
	connect(editTools->addAction(QCoreApplication::translate("VibeStudioModelDesignDialog", "Remove")), &QAction::triggered, this,
	        [this]() { removePart(); });
	partLayout->addWidget(editTools);
	splitter->addWidget(partPanel);
	auto* previewPanel = new QWidget;
	auto* previewLayout = new QVBoxLayout(previewPanel);
	previewLayout->setContentsMargins(0, 0, 0, 0);
	auto* previewTools = new QToolBar(QCoreApplication::translate("VibeStudioModelDesignDialog", "Preview controls"));
	previewTools->setAccessibleName(previewTools->windowTitle());
	auto* renderMode = new QComboBox;
	renderMode->setObjectName(QStringLiteral("designRenderMode"));
	renderMode->setAccessibleName(QCoreApplication::translate("VibeStudioModelDesignDialog", "Preview rendering"));
	renderMode->addItem(QCoreApplication::translate("VibeStudioModelDesignDialog", "Solid"));
	renderMode->addItem(QCoreApplication::translate("VibeStudioModelDesignDialog", "Wireframe"));
	renderMode->addItem(QCoreApplication::translate("VibeStudioModelDesignDialog", "UV checker"));
	renderMode->setToolTip(QCoreApplication::translate(
	    "VibeStudioModelDesignDialog", "Inspect texture tiling with a generated checker; project materials are not loaded here."));
	previewTools->addWidget(renderMode);
	auto* frame = previewTools->addAction(QCoreApplication::translate("VibeStudioModelDesignDialog", "Frame Design"));
	frame->setToolTip(QCoreApplication::translate("VibeStudioModelDesignDialog", "Fit the model design in the preview"));
	previewLayout->addWidget(previewTools);
	m_preview = new ModelViewport;
	m_preview->setObjectName(QStringLiteral("designPreview"));
	m_preview->setAccessibleName(QCoreApplication::translate("VibeStudioModelDesignDialog", "Model design preview"));
	m_preview->setReducedMotion(true);
	m_preview->setControlsHelp(QCoreApplication::translate(
	    "VibeStudioModelDesignDialog", "Click a part to select it, or use the parts list. Drag to orbit and use the wheel to zoom."));
	m_preview->setMinimumSize(240, 250);
	m_preview->setBackfaceCulling(false);
	QImage checker(64, 64, QImage::Format_RGB32);
	checker.fill(QColor(220, 220, 220));
	{
		QPainter painter(&checker);
		painter.fillRect(0, 0, 32, 32, QColor(65, 65, 65));
		painter.fillRect(32, 32, 32, 32, QColor(65, 65, 65));
		// An asymmetric corner marks the tile's orientation when mirrored.
		painter.fillRect(2, 2, 8, 8, Qt::white);
	}
	m_preview->setSkin(checker);
	m_preview->setRenderMode(ModelViewportRenderMode::FlatShaded);
	previewLayout->addWidget(m_preview, 1);
	m_selectionStatus = new QLabel;
	m_selectionStatus->setObjectName(QStringLiteral("designSelection"));
	m_selectionStatus->setTextFormat(Qt::PlainText);
	m_selectionStatus->setWordWrap(true);
	m_selectionStatus->setAccessibleName(QCoreApplication::translate("VibeStudioModelDesignDialog", "Selected model part"));
	previewLayout->addWidget(m_selectionStatus);
	connect(frame, &QAction::triggered, m_preview, &ModelViewport::frameModel);
	connect(renderMode, &QComboBox::currentIndexChanged, this, [this](int mode) {
		m_preview->setRenderMode(mode == 2   ? ModelViewportRenderMode::Textured
		                         : mode == 1 ? ModelViewportRenderMode::Wireframe
		                                     : ModelViewportRenderMode::FlatShaded);
	});
	connect(m_preview, &ModelViewport::triangleClicked, this, [this](int surface, int) {
		if (surface >= 0 && surface < m_design.parts.size()) {
			m_parts->setCurrentRow(surface);
		}
	});
	splitter->addWidget(previewPanel);
	auto* properties = new QTabWidget;
	properties->setObjectName(QStringLiteral("designProperties"));
	properties->setAccessibleName(QCoreApplication::translate("VibeStudioModelDesignDialog", "Model design properties"));
	properties->setMinimumWidth(310);
	const auto propertyTab = [&](const QString& title) {
		auto* inspector = new QWidget;
		auto* tabForm = new QFormLayout(inspector);
		tabForm->setRowWrapPolicy(QFormLayout::WrapAllRows);
		tabForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
		auto* scroll = new QScrollArea;
		scroll->setWidgetResizable(true);
		scroll->setWidget(inspector);
		scroll->setAccessibleName(title);
		properties->addTab(scroll, title);
		return tabForm;
	};
	auto* form = propertyTab(QCoreApplication::translate("VibeStudioModelDesignDialog", "Part"));
	const auto field = [&](const QString& label, const QString& id) {
		auto* edit = new QLineEdit;
		edit->setObjectName(id);
		edit->setAccessibleName(label);
		form->addRow(label, edit);
		return edit;
	};
	m_name = field(QCoreApplication::translate("VibeStudioModelDesignDialog", "Design name"), QStringLiteral("designName"));
	m_partName = field(QCoreApplication::translate("VibeStudioModelDesignDialog", "Part name"), QStringLiteral("designPartName"));
	m_primitive = new QComboBox;
	m_primitive->setAccessibleName(QCoreApplication::translate("VibeStudioModelDesignDialog", "Primitive type"));
	for (int i = 0; i < primitiveIds.size(); ++i) {
		m_primitive->addItem(primitiveNames[i], primitiveIds[i]);
	}
	form->addRow(QCoreApplication::translate("VibeStudioModelDesignDialog", "Primitive"), m_primitive);
	const auto vector = [&](const QString& label, const QString& id, QDoubleSpinBox** fields, double minimum, double maximum) {
		auto* row = new QWidget;
		auto* rowLayout = new QVBoxLayout(row);
		rowLayout->setContentsMargins(0, 0, 0, 0);
		for (int i = 0; i < 3; ++i) {
			auto* spin = new QDoubleSpinBox;
			fields[i] = spin;
			spin->setDecimals(6);
			spin->setRange(minimum, maximum);
			spin->setSingleStep(8);
			const QString axis = QStringList{QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")}[i];
			spin->setObjectName(id + axis);
			spin->setAccessibleName(label + QLatin1Char(' ') + axis);
			spin->setToolTip(label + QLatin1Char(' ') + axis);
			spin->setPrefix(axis + QLatin1Char(' '));
			spin->setKeyboardTracking(false);
			rowLayout->addWidget(spin);
		}
		form->addRow(label, row);
	};
	vector(QCoreApplication::translate("VibeStudioModelDesignDialog", "Dimensions"), QStringLiteral("designSize"), m_size, 1.0 / 64, 1000);
	vector(QCoreApplication::translate("VibeStudioModelDesignDialog", "Part position"), QStringLiteral("designOrigin"), m_origin, -10000,
	       10000);
	const auto scalar = [&](const QString& label, const QString& id, double minimum, double maximum, double step) {
		auto* spin = new QDoubleSpinBox;
		spin->setObjectName(id);
		spin->setDecimals(6);
		spin->setRange(minimum, maximum);
		spin->setSingleStep(step);
		spin->setKeyboardTracking(false);
		spin->setAccessibleName(label);
		form->addRow(label, spin);
		return spin;
	};
	m_roll = scalar(QCoreApplication::translate("VibeStudioModelDesignDialog", "X rotation (roll)"), QStringLiteral("designRoll"), -36000,
	                36000, 15);
	m_pitch = scalar(QCoreApplication::translate("VibeStudioModelDesignDialog", "Y rotation (pitch)"), QStringLiteral("designPitch"),
	                 -36000, 36000, 15);
	m_yaw = scalar(QCoreApplication::translate("VibeStudioModelDesignDialog", "Z rotation (yaw)"), QStringLiteral("designYaw"), -36000,
	               36000, 15);
	for (auto* angle : {m_roll, m_pitch, m_yaw}) {
		angle->setSuffix(QCoreApplication::translate("VibeStudioModelDesignDialog", "°"));
		angle->setToolTip(
		    QCoreApplication::translate("VibeStudioModelDesignDialog", "Rotate around the part centre, in X, then Y, then Z order."));
	}
	m_segments = new QSpinBox;
	m_segments->setRange(3, 64);
	m_segments->setKeyboardTracking(false);
	m_segments->setAccessibleName(QCoreApplication::translate("VibeStudioModelDesignDialog", "Cylinder segments"));
	form->addRow(QCoreApplication::translate("VibeStudioModelDesignDialog", "Segments"), m_segments);
	form = propertyTab(QCoreApplication::translate("VibeStudioModelDesignDialog", "Surface"));
	m_material = field(QCoreApplication::translate("VibeStudioModelDesignDialog", "Material path"), QStringLiteral("designMaterial"));
	m_material->setToolTip(QCoreApplication::translate(
	    "VibeStudioModelDesignDialog", "Package-relative image or Quake III shader name. Assets are checked by Levels → Dependencies."));
	auto* show = new QPushButton(QCoreApplication::translate("VibeStudioModelDesignDialog", "Show Material"));
	show->setAccessibleName(QCoreApplication::translate("VibeStudioModelDesignDialog", "Show this model part material in Textures"));
	form->addRow(show);
	connect(show, &QPushButton::clicked, this, [this]() {
		if (showMaterial) {
			showMaterial(m_material->text());
		}
	});
	m_uvScale[0] =
	    scalar(QCoreApplication::translate("VibeStudioModelDesignDialog", "U scale"), QStringLiteral("designUvScaleU"), -64, 64, 0.25);
	m_uvScale[1] =
	    scalar(QCoreApplication::translate("VibeStudioModelDesignDialog", "V scale"), QStringLiteral("designUvScaleV"), -64, 64, 0.25);
	m_uvOffset[0] = scalar(QCoreApplication::translate("VibeStudioModelDesignDialog", "U offset"), QStringLiteral("designUvOffsetU"), -1024,
	                       1024, 0.125);
	m_uvOffset[1] = scalar(QCoreApplication::translate("VibeStudioModelDesignDialog", "V offset"), QStringLiteral("designUvOffsetV"), -1024,
	                       1024, 0.125);
	m_uvRotation = scalar(QCoreApplication::translate("VibeStudioModelDesignDialog", "UV rotation"), QStringLiteral("designUvRotation"),
	                      -36000, 36000, 15);
	m_uvRotation->setSuffix(QCoreApplication::translate("VibeStudioModelDesignDialog", "°"));
	m_uvRotation->setToolTip(QCoreApplication::translate(
	    "VibeStudioModelDesignDialog", "Scale UVs, rotate about UV zero, then add offsets. Negative scales mirror the texture."));
	auto* resetUv = new QPushButton(QCoreApplication::translate("VibeStudioModelDesignDialog", "Reset UVs"));
	resetUv->setObjectName(QStringLiteral("resetDesignUv"));
	resetUv->setAccessibleName(QCoreApplication::translate("VibeStudioModelDesignDialog", "Reset the selected part's UV transform"));
	form->addRow(resetUv);
	connect(resetUv, &QPushButton::clicked, this, [this]() {
		const int index = m_parts->currentRow();
		if (index >= 0) {
			auto next = m_design;
			next.parts[index].uvScale = {1, 1};
			next.parts[index].uvOffset = {};
			next.parts[index].uvRotation = 0;
			commit(next, index);
		}
	});
	form = propertyTab(QCoreApplication::translate("VibeStudioModelDesignDialog", "Handoff"));
	m_virtualPath =
	    field(QCoreApplication::translate("VibeStudioModelDesignDialog", "Package model path"), QStringLiteral("designPackagePath"));
	m_virtualPath->setText(QStringLiteral("models/props/prop.md3"));
	m_replace = new QCheckBox(QCoreApplication::translate("VibeStudioModelDesignDialog", "Replace existing entry"));
	m_replace->setAccessibleName(
	    QCoreApplication::translate("VibeStudioModelDesignDialog", "Allow replacement of the model package entry"));
	form->addRow(m_replace);
	vector(QCoreApplication::translate("VibeStudioModelDesignDialog", "Level placement"), QStringLiteral("designPlacement"), m_placement,
	       -65536, 65536);
	properties->ensurePolished();
	properties->setMinimumWidth(std::max(310, properties->tabBar()->sizeHint().width() + 24));
	splitter->addWidget(properties);
	splitter->setStretchFactor(1, 1);
	splitter->setSizes({200, 580, 410});
	layout->addWidget(splitter, 1);
	m_status = new QLabel;
	m_status->setObjectName(QStringLiteral("designStatus"));
	m_status->setWordWrap(true);
	m_status->setTextFormat(Qt::PlainText);
	m_status->setAccessibleName(QCoreApplication::translate("VibeStudioModelDesignDialog", "Model design status"));
	layout->addWidget(m_status);
	connect(m_parts, &QListWidget::currentRowChanged, this, [this]() {
		if (!m_refreshing) {
			loadPart();
		}
	});
	for (auto* edit : {m_partName, m_material, m_name}) {
		connect(edit, &QLineEdit::textChanged, this, [this, edit]() { editPart(edit); });
	}
	connect(m_primitive, &QComboBox::currentIndexChanged, this, [this]() { editPart(m_primitive); });
	connect(m_segments, &QSpinBox::valueChanged, this, [this]() { editPart(m_segments); });
	for (auto* spin : {m_roll, m_pitch, m_yaw, m_uvScale[0], m_uvScale[1], m_uvOffset[0], m_uvOffset[1], m_uvRotation}) {
		connect(spin, &QDoubleSpinBox::valueChanged, this, [this, spin]() { editPart(spin); });
	}
	for (int i = 0; i < 3; ++i) {
		connect(m_size[i], &QDoubleSpinBox::valueChanged, this, [this, i]() { editPart(m_size[i]); });
		connect(m_origin[i], &QDoubleSpinBox::valueChanged, this, [this, i]() { editPart(m_origin[i]); });
	}
	ModelDesign initial;
	ModelDesignPart part;
	part.name = QStringLiteral("body");
	part.origin.z = 32;
	initial.parts << part;
	setDesign(initial);
}

bool ModelDesignDialog::setDesign(const ModelDesign& design, QString* error)
{
	const auto errors = validateModelDesign(design);
	if (!errors.isEmpty()) {
		if (error) {
			*error = errors.join(QLatin1Char('\n'));
		}
		return false;
	}
	m_design = design;
	m_savedDesign = modelDesignJson(design);
	m_path.clear();
	m_undo.clear();
	m_redo.clear();
	m_dirty = false;
	m_preview->clearMesh();
	refreshParts(0);
	return true;
}
QString ModelDesignDialog::uniqueName(const QString& prefix) const
{
	for (int n = 1;; ++n) {
		const QString name = prefix + QString::number(n);
		bool found = false;
		for (const auto& p : m_design.parts) {
			found |= p.name.compare(name, Qt::CaseInsensitive) == 0;
		}
		if (!found) {
			return name;
		}
	}
}
void ModelDesignDialog::commit(ModelDesign next, int selected, bool refreshFields)
{
	if (modelDesignJson(next) == modelDesignJson(m_design)) {
		return;
	}
	m_undo << HistoryEntry{m_design, m_parts->currentRow()};
	if (m_undo.size() > 100) {
		m_undo.removeFirst();
	}
	m_redo.clear();
	m_design = std::move(next);
	m_dirty = true;
	if (refreshFields) {
		refreshParts(selected);
	} else {
		if (m_parts->currentItem()) {
			m_parts->currentItem()->setText(m_design.parts[selected].name);
		}
		refreshPreview();
	}
}
void ModelDesignDialog::addPart(const QString& primitive)
{
	if (m_design.parts.size() >= 32) {
		m_status->setText(QCoreApplication::translate("VibeStudioModelDesignDialog", "A design can contain at most 32 parts."));
		return;
	}
	ModelDesign next = m_design;
	ModelDesignPart part;
	part.primitive = primitive;
	part.name = uniqueName(primitive);
	part.origin.z = primitive == QStringLiteral("plane") ? 0 : 32;
	if (primitive == QStringLiteral("cylinder")) {
		part.size = {32, 32, 64};
		part.origin.z = 96;
	}
	next.parts << part;
	commit(next, next.parts.size() - 1);
}
void ModelDesignDialog::duplicatePart()
{
	const int index = m_parts->currentRow();
	if (index < 0 || m_design.parts.size() >= 32) {
		return;
	}
	ModelDesign next = m_design;
	auto part = next.parts[index];
	part.name = uniqueName(QStringLiteral("copy"));
	part.origin.x += 16;
	next.parts << part;
	commit(next, next.parts.size() - 1);
}
void ModelDesignDialog::removePart()
{
	const int index = m_parts->currentRow();
	if (index < 0 || m_design.parts.size() <= 1) {
		m_status->setText(QCoreApplication::translate("VibeStudioModelDesignDialog", "Keep at least one part in the design."));
		return;
	}
	ModelDesign next = m_design;
	next.parts.removeAt(index);
	commit(next, std::min(index, int(next.parts.size()) - 1));
}
void ModelDesignDialog::undo()
{
	if (!m_undo.isEmpty()) {
		m_redo << HistoryEntry{m_design, m_parts->currentRow()};
		const auto entry = m_undo.takeLast();
		m_design = entry.design;
		refreshParts(entry.selected);
	}
}
void ModelDesignDialog::redo()
{
	if (!m_redo.isEmpty()) {
		m_undo << HistoryEntry{m_design, m_parts->currentRow()};
		const auto entry = m_redo.takeLast();
		m_design = entry.design;
		refreshParts(entry.selected);
	}
}
void ModelDesignDialog::refreshParts(int selected)
{
	m_refreshing = true;
	m_parts->clear();
	for (const auto& part : m_design.parts) {
		m_parts->addItem(part.name);
	}
	m_parts->setCurrentRow(std::clamp(selected, 0, int(m_design.parts.size()) - 1));
	m_refreshing = false;
	loadPart();
}
void ModelDesignDialog::loadPart()
{
	const int index = m_parts->currentRow();
	if (index < 0) {
		return;
	}
	m_refreshing = true;
	const auto& part = m_design.parts[index];
	m_name->setText(m_design.name);
	m_partName->setText(part.name);
	m_material->setText(part.material);
	m_primitive->setCurrentIndex(m_primitive->findData(part.primitive));
	m_yaw->setValue(part.yaw);
	m_roll->setValue(part.roll);
	m_pitch->setValue(part.pitch);
	m_uvScale[0]->setValue(part.uvScale.u);
	m_uvScale[1]->setValue(part.uvScale.v);
	m_uvOffset[0]->setValue(part.uvOffset.u);
	m_uvOffset[1]->setValue(part.uvOffset.v);
	m_uvRotation->setValue(part.uvRotation);
	m_segments->setValue(part.segments);
	const float sizes[] = {part.size.x, part.size.y, part.size.z}, origins[] = {part.origin.x, part.origin.y, part.origin.z};
	for (int i = 0; i < 3; ++i) {
		m_size[i]->setValue(sizes[i]);
		m_origin[i]->setValue(origins[i]);
	}
	m_refreshing = false;
	refreshPreview();
}
void ModelDesignDialog::editPart(QObject* field)
{
	const int index = m_parts->currentRow();
	if (m_refreshing || index < 0) {
		return;
	}
	ModelDesign next = m_design;
	auto& p = next.parts[index];
	// Only change the edited field. Loading a precise source into a rounded
	// spin box must not quantize unrelated values on the next name edit.
	if (field == m_name) {
		next.name = m_name->text();
	}
	if (field == m_partName) {
		p.name = m_partName->text();
	}
	if (field == m_material) {
		p.material = m_material->text();
	}
	if (field == m_primitive) {
		p.primitive = m_primitive->currentData().toString();
	}
	if (field == m_yaw) {
		p.yaw = m_yaw->value();
	}
	if (field == m_roll) {
		p.roll = m_roll->value();
	}
	if (field == m_pitch) {
		p.pitch = m_pitch->value();
	}
	if (field == m_segments) {
		p.segments = m_segments->value();
	}
	if (field == m_uvRotation) {
		p.uvRotation = m_uvRotation->value();
	}
	float* sizes[] = {&p.size.x, &p.size.y, &p.size.z};
	float* origins[] = {&p.origin.x, &p.origin.y, &p.origin.z};
	for (int i = 0; i < 3; ++i) {
		if (field == m_size[i]) {
			*sizes[i] = float(m_size[i]->value());
		}
		if (field == m_origin[i]) {
			*origins[i] = float(m_origin[i]->value());
		}
	}
	if (field == m_uvScale[0]) {
		p.uvScale.u = float(m_uvScale[0]->value());
	}
	if (field == m_uvScale[1]) {
		p.uvScale.v = float(m_uvScale[1]->value());
	}
	if (field == m_uvOffset[0]) {
		p.uvOffset.u = float(m_uvOffset[0]->value());
	}
	if (field == m_uvOffset[1]) {
		p.uvOffset.v = float(m_uvOffset[1]->value());
	}
	commit(next, index, false);
}
void ModelDesignDialog::refreshPreview()
{
	m_dirty = modelDesignJson(m_design) != m_savedDesign;
	setWindowModified(m_dirty);
	const auto mesh = buildModelDesignMesh(m_design);
	QString md3Error;
	const bool md3 = !exportModelDesign(m_design, QStringLiteral("md3"), &md3Error).isEmpty();
	if (mesh.geometryAvailable) {
		m_preview->setMesh(mesh, m_preview->hasMesh());
	} else {
		m_preview->clearMesh();
	}
	refreshSelection();
	m_segments->setEnabled(m_primitive->currentData() == QStringLiteral("cylinder"));
	m_undoAction->setEnabled(!m_undo.isEmpty());
	m_redoAction->setEnabled(!m_redo.isEmpty());
	m_exportObj->setEnabled(mesh.geometryAvailable);
	m_exportMd3->setEnabled(md3);
	m_status->setText(mesh.error.isEmpty()
	                      ? (md3 ? QCoreApplication::translate(
	                                   "VibeStudioModelDesignDialog",
	                                   "Parts: %1 · Vertices: %2 · Triangles: %3 · Static model, Z up; MD3 positions use 1/64-unit steps.")
	                                   .arg(mesh.surfaceCount)
	                                   .arg(mesh.vertexCount)
	                                   .arg(mesh.triangleCount)
	                             : md3Error)
	                      : mesh.error);
	setWindowTitle(QCoreApplication::translate("VibeStudioModelDesignDialog", "Model Design — %1[*]").arg(m_design.name));
	refreshContext();
}
void ModelDesignDialog::refreshSelection()
{
	const int selected = m_parts->currentRow();
	QVector<int> triangles;
	int start = 0;
	const auto& surfaces = m_preview->mesh().surfaces;
	for (int surface = 0; surface < surfaces.size(); ++surface) {
		const int count = surfaces[surface].triangles.size();
		if (surface == selected) {
			for (int i = 0; i < count; ++i) {
				triangles << start + i;
			}
			break;
		}
		start += count;
	}
	m_preview->setHighlightedTriangles(triangles);
	m_selectionStatus->setText(
	    selected >= 0 ? QCoreApplication::translate("VibeStudioModelDesignDialog", "Selected: %1").arg(m_design.parts[selected].name)
	                  : QString());
}
void ModelDesignDialog::refreshContext()
{
	if (!m_contextLabel) {
		return;
	}
	const auto state = context ? context() : ModelDesignContext();
	m_contextKey = state.packagePath + QLatin1Char('\n') + state.mapPath;
	m_contextLabel->setText(
	    QCoreApplication::translate("VibeStudioModelDesignDialog", "Package: %1 · Level: %2")
	        .arg(state.packagePath.isEmpty() ? QCoreApplication::translate("VibeStudioModelDesignDialog", "none") : state.packagePath,
	             state.mapPath.isEmpty() ? QCoreApplication::translate("VibeStudioModelDesignDialog", "none") : state.mapPath));
	m_stageAction->setEnabled(m_exportMd3->isEnabled() && state.canStage && bool(handoff));
	m_placeAction->setEnabled(m_exportMd3->isEnabled() && state.canStage && state.canPlace && bool(handoff));
}
void ModelDesignDialog::changeEvent(QEvent* event)
{
	QDialog::changeEvent(event);
	if (event->type() == QEvent::ActivationChange && isActiveWindow()) {
		refreshContext();
	}
}
bool ModelDesignDialog::saveDesign()
{
	const auto errors = validateModelDesign(m_design);
	if (!errors.isEmpty()) {
		m_status->setText(errors.join(QLatin1Char('\n')));
		return false;
	}
	const QString path =
	    QFileDialog::getSaveFileName(this, QCoreApplication::translate("VibeStudioModelDesignDialog", "Save Model Design"),
	                                 m_path.isEmpty() ? m_design.name + QStringLiteral(".model.json") : m_path,
	                                 QCoreApplication::translate("VibeStudioModelDesignDialog", "Model designs (*.model.json *.json)"));
	if (path.isEmpty()) {
		return false;
	}
	QString error;
	if (!saveModelDesignBytes(path, QJsonDocument(modelDesignJson(m_design)).toJson(), true, &error)) {
		m_status->setText(error);
		return false;
	}
	m_path = path;
	m_savedDesign = modelDesignJson(m_design);
	m_dirty = false;
	refreshPreview();
	m_status->setText(QCoreApplication::translate("VibeStudioModelDesignDialog", "Saved design: %1").arg(path));
	return true;
}
bool ModelDesignDialog::confirmDiscard()
{
	if (!m_dirty) {
		return true;
	}
	const auto answer = QMessageBox::question(
	    this, QCoreApplication::translate("VibeStudioModelDesignDialog", "Unsaved Model Design"),
	    QCoreApplication::translate("VibeStudioModelDesignDialog", "Save the editable model design before closing or opening another?"),
	    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
	return answer == QMessageBox::Discard || (answer == QMessageBox::Save && saveDesign());
}
void ModelDesignDialog::closeEvent(QCloseEvent* event)
{
	if (confirmDiscard()) {
		event->accept();
	} else {
		event->ignore();
	}
}
void ModelDesignDialog::reject() { close(); }
void ModelDesignDialog::openDesign()
{
	if (!confirmDiscard()) {
		return;
	}
	const QString path =
	    QFileDialog::getOpenFileName(this, QCoreApplication::translate("VibeStudioModelDesignDialog", "Open Model Design"), QString(),
	                                 QCoreApplication::translate("VibeStudioModelDesignDialog", "Model designs (*.model.json *.json)"));
	if (path.isEmpty()) {
		return;
	}
	ModelDesign design;
	QString error;
	if (!loadModelDesign(path, &design, &error) || !setDesign(design, &error)) {
		m_status->setText(error);
		return;
	}
	m_path = path;
}
void ModelDesignDialog::exportDesign(const QString& format)
{
	QString error;
	const QByteArray bytes = exportModelDesign(m_design, format, &error);
	if (bytes.isEmpty()) {
		m_status->setText(error);
		return;
	}
	const QString path = QFileDialog::getSaveFileName(
	    this, QCoreApplication::translate("VibeStudioModelDesignDialog", "Export Model Design"), m_design.name + QLatin1Char('.') + format,
	    format == QStringLiteral("md3") ? QCoreApplication::translate("VibeStudioModelDesignDialog", "Quake III models (*.md3)")
	                                    : QCoreApplication::translate("VibeStudioModelDesignDialog", "Wavefront models (*.obj)"));
	if (path.isEmpty()) {
		return;
	}
	if (!saveModelDesignBytes(path, bytes, true, &error, m_path)) {
		m_status->setText(error);
		return;
	}
	m_status->setText(QCoreApplication::translate("VibeStudioModelDesignDialog", "Exported model: %1").arg(path));
}
void ModelDesignDialog::stage(bool place)
{
	const QString previous = m_contextKey;
	refreshContext();
	if (previous != m_contextKey) {
		m_status->setText(QCoreApplication::translate("VibeStudioModelDesignDialog",
		                                              "The package or level changed. Review the current context before staging again."));
		return;
	}
	if (!handoff) {
		return;
	}
	QString error;
	const LevelMapVec3 origin{m_placement[0]->value(), m_placement[1]->value(), m_placement[2]->value(), true};
	if (!handoff(m_design, m_virtualPath->text(), place, origin, m_replace->isChecked(), &error)) {
		m_status->setText(error);
		return;
	}
	m_status->setText(
	    place ? QCoreApplication::translate("VibeStudioModelDesignDialog",
	                                        "Staged %1 and placed it in the level. Save the map and package to keep both changes.")
	                .arg(m_virtualPath->text())
	          : QCoreApplication::translate("VibeStudioModelDesignDialog", "Staged %1. Review and save the package plan in Packages.")
	                .arg(m_virtualPath->text()));
}

} // namespace vibestudio
