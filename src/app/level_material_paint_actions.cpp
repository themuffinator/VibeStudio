#include "app/application_shell.h"
#include "app/model_viewport.h"
#include "app/level_surface_tools.h"
#include "app/studio_layout.h"

#include <QAccessible>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace vibestudio {

QWidget* ApplicationShell::buildLevelMaterialTools()
{
	m_levelMaterialTools = new QWidget;
	m_levelMaterialTools->setObjectName(QStringLiteral("levelMaterialTools"));
	auto* layout = new QVBoxLayout(m_levelMaterialTools);
	layout->setContentsMargins(6, 3, 6, 3);
	layout->setSpacing(3);
	auto* row = new QHBoxLayout;
	layout->addLayout(row);
	auto* label = new QLabel(tr("Material"));
	m_levelMaterialPicker = new QComboBox;
	m_levelMaterialPicker->setObjectName(QStringLiteral("levelPaintMaterial"));
	m_levelMaterialPicker->setEditable(true);
	m_levelMaterialPicker->setInsertPolicy(QComboBox::NoInsert);
	m_levelMaterialPicker->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_levelMaterialPicker->setMinimumContentsLength(12);
	m_levelMaterialPicker->setAccessibleName(tr("Material for painting and new brushes"));
	m_levelMaterialPicker->setToolTip(tr("Choose a map material, type its name, or use a material from Textures. Painting preserves surface alignment; new brushes use this material."));
	label->setBuddy(m_levelMaterialPicker);
	row->addWidget(label);
	row->addWidget(m_levelMaterialPicker, 1);
	m_levelMaterialTool = new QComboBox;
	m_levelMaterialTool->setObjectName(QStringLiteral("levelMaterialTool"));
	m_levelMaterialTool->setAccessibleName(tr("Camera tool"));
	m_levelMaterialTool->addItems({tr("Navigate"), tr("Paint"), tr("Sample"), tr("Draw Brush")});
	m_levelMaterialTool->setToolTip(tr("Choose navigation, material painting, material sampling or brush drawing. Escape cancels the current gesture or returns to navigation. Other camera gestures follow your editor profile."));
	row->addWidget(m_levelMaterialTool);
	auto* targets = new QToolButton;
	targets->setObjectName(QStringLiteral("levelMaterialTargetsToggle"));
	targets->setText(tr("Targets"));
	targets->setAccessibleName(tr("Show explicit material targets"));
	targets->setToolTip(tr("Paint or sample exact faces, patches, walls and flats using the keyboard."));
	targets->setCheckable(true);
	row->addWidget(targets);
	auto* explicitTargets = new QWidget;
	auto* explicitLayout = new QHBoxLayout(explicitTargets);
	explicitLayout->setContentsMargins(0, 0, 0, 0);
	m_levelMaterialTargetInput = new QLineEdit;
	m_levelMaterialTargetInput->setObjectName(QStringLiteral("levelMaterialTargets"));
	m_levelMaterialTargetInput->setAccessibleName(tr("Material targets, separated by commas"));
	m_levelMaterialTargetInput->setPlaceholderText(QStringLiteral("face:0:1, patch:0"));
	m_levelMaterialTargetInput->setToolTip(tr("Use face:brushId:faceNumber (one-based), patch:id, side:id:upper|lower|middle or sector:id:floor|ceiling. Separate targets with commas."));
	explicitLayout->addWidget(m_levelMaterialTargetInput, 1);
	const auto button = [&](const QString& id, const QString& text) {
		auto* control = new QToolButton;
		control->setObjectName(id); control->setText(text); control->setAccessibleName(text);
		explicitLayout->addWidget(control); return control;
	};
	auto* paint = button(QStringLiteral("levelPaintTargets"), tr("Paint Targets"));
	auto* sample = button(QStringLiteral("levelSampleTarget"), tr("Sample Target"));
	layout->addWidget(explicitTargets);
	explicitTargets->hide();
	connect(targets, &QToolButton::toggled, explicitTargets, &QWidget::setVisible);
	const auto run = [this](bool sampling) {
		QVector<LevelMaterialTarget> selected;
		QString error;
		for (const auto& value : m_levelMaterialTargetInput->text().split(QLatin1Char(','))) {
			LevelMaterialTarget target;
			if (!parseLevelMaterialTarget(value.trimmed(), &target, &error)) { showLevelMaterialPaintMessage(error); return; }
			selected << target;
		}
		if (sampling && selected.size() != 1) { showLevelMaterialPaintMessage(tr("Choose one target to sample.")); return; }
		const bool ok = sampling ? sampleLevelMaterialTarget(selected.first(), &error)
			: applyLevelMaterialPaint(selected, m_levelPaintMaterial, &error);
		if (!ok) { showLevelMaterialPaintMessage(error); }
	};
	connect(paint, &QToolButton::clicked, this, [run] { run(false); });
	connect(sample, &QToolButton::clicked, this, [run] { run(true); });
	connect(m_levelMaterialTargetInput, &QLineEdit::returnPressed, this, [run] { run(false); });
	auto* statusRow = new QHBoxLayout;
	// A changing wrapped status can resize the camera and cancel a held
	// gesture. The shared fixed-height readout retains full accessible text
	// and a tooltip; detailed surface status remains in the Surfaces panel.
	m_levelMaterialPaintStatus = new ElidedLabel(tr("Choose a material to paint or sample a surface."));
	m_levelMaterialPaintStatus->setObjectName(QStringLiteral("levelMaterialPaintStatus"));
	m_levelMaterialPaintStatus->setAccessibleName(tr("Material painting status"));
	m_levelMaterialPaintStatus->setTextFormat(Qt::PlainText);
	statusRow->addWidget(m_levelMaterialPaintStatus, 1);
	m_levelMaterialStrokeCancel = new QToolButton;
	m_levelMaterialStrokeCancel->setObjectName(QStringLiteral("levelCancelMaterialStroke"));
	m_levelMaterialStrokeCancel->setText(tr("Cancel Stroke"));
	m_levelMaterialStrokeCancel->setAccessibleName(tr("Cancel material stroke"));
	m_levelMaterialStrokeCancel->setEnabled(false);
	statusRow->addWidget(m_levelMaterialStrokeCancel);
	layout->addLayout(statusRow);
	connect(m_levelMaterialStrokeCancel, &QToolButton::clicked, this, [this] {
		if (m_levelSurfaceStroke) { cancelLevelSurfaceEdits(); }
		else if (m_levelMap3D->isDrawingBrush()) { m_levelMap3D->finishBrushDraw(false); }
		else { m_levelMap3D->finishSurfaceStroke(false); }
	});
	connect(m_levelMaterialPicker, &QComboBox::editTextChanged, this, &ApplicationShell::chooseLevelPaintMaterial);
	connect(m_levelMaterialTool, &QComboBox::currentIndexChanged, this, &ApplicationShell::setLevelMaterialTool);
	connect(m_levelMap3D, &ModelViewport::surfaceStrokeBegan, this, &ApplicationShell::beginLevelMaterialStroke);
	connect(m_levelMap3D, &ModelViewport::surfaceTouched, this, &ApplicationShell::touchLevelMaterialSurface);
	connect(m_levelMap3D, &ModelViewport::surfaceStrokeEnded, this, &ApplicationShell::finishLevelMaterialStroke);
	connect(m_levelMap3D, &ModelViewport::materialStrokeBegan, this, &ApplicationShell::beginLevelSurfaceStroke);
	connect(m_levelMap3D, &ModelViewport::materialStrokeTouched, this, &ApplicationShell::touchLevelSurfaceStroke);
	connect(m_levelMap3D, &ModelViewport::materialStrokeEnded, this, &ApplicationShell::finishLevelSurfaceStroke);
	connect(m_levelMap3D, &ModelViewport::surfaceToolChanged, this, [this] {
		const QSignalBlocker blocker(m_levelMaterialTool);
		m_levelMaterialTool->setCurrentIndex(static_cast<int>(m_levelMap3D->surfaceTool()));
	});
	connect(m_levelMap3D, &ModelViewport::surfaceSampleRequested, this, [this](int triangle) {
		if (triangle < 0 || triangle >= m_levelPreviewMaterialTargets.size()) { return; }
		if (m_levelPreviewMaterialTargets[triangle].kind == LevelMaterialKind::None) {
			showLevelMaterialPaintMessage(tr("Placed model materials are edited in Models.")); return;
		}
		QString error;
		if (!sampleLevelMaterialTarget(m_levelPreviewMaterialTargets[triangle], &error)) { showLevelMaterialPaintMessage(error); }
	});
	connect(m_levelMap3D, &ModelViewport::surfacePaintRequested, this, [this](int triangle) {
		if (triangle < 0 || triangle >= m_levelPreviewMaterialTargets.size()) { return; }
		const auto target = m_levelPreviewMaterialTargets[triangle];
		if (target.kind == LevelMaterialKind::None) { showLevelMaterialPaintMessage(tr("Placed model materials are edited in Models.")); return; }
		QString error;
		if (!applyLevelMaterialPaint({target}, m_levelPaintMaterial, &error)) { showLevelMaterialPaintMessage(error); }
		else if (m_levelMaterialTargetInput) { m_levelMaterialTargetInput->setText(levelMaterialTargetId(target)); }
	});
	connect(m_levelMap3D, &ModelViewport::surfaceActionMessage, this, &ApplicationShell::showLevelMaterialPaintMessage);
	connect(m_levelMap3D, &ModelViewport::surfacePasteRequested, this, [this](int triangle, CameraMaterialGesture action) {
		if (triangle < 0 || triangle >= m_levelPreviewMaterialTargets.size()) { return; }
		const auto target = m_levelPreviewMaterialTargets[triangle];
		const bool wholeBrush = action == CameraMaterialGesture::PasteBrush;
		const bool seamless = action == CameraMaterialGesture::WrapFace || action == CameraMaterialGesture::WrapFaceOnly;
		const bool values = action == CameraMaterialGesture::ValuesSelection || action == CameraMaterialGesture::ValuesSelectionOnly;
		const bool project = action == CameraMaterialGesture::ProjectSelection || action == CameraMaterialGesture::ProjectSelectionOnly;
		if (target.kind != LevelMaterialKind::BrushFace && !((values || project) && target.kind == LevelMaterialKind::Patch)) {
			showLevelMaterialPaintMessage(tr("This paste needs a brush face or a patch for Radiant values/projection. Use material painting for Doom surfaces.")); return;
		}
		QVector<LevelMaterialTarget> targets{target};
		if (wholeBrush) {
			targets.clear();
			for (const auto& brush : m_levelMapDocument.brushes) {
				if (brush.id != target.objectId) { continue; }
				for (int i = 0; i < brush.faces.size(); ++i) { targets.append({LevelMaterialKind::BrushFace, brush.id, i}); }
				break;
			}
		}
		QString error;
		LevelSurfacePasteOptions options;
		options.mappingOnly = action == CameraMaterialGesture::ValuesSelectionOnly || action == CameraMaterialGesture::WrapFaceOnly || action == CameraMaterialGesture::ProjectSelectionOnly;
		if (values) { options.mode = LevelSurfacePasteMode::RadiantValues; options.includeSelection = true; }
		if (project) { options.mode = LevelSurfacePasteMode::RadiantProject; options.includeSelection = true; }
		if (seamless) {
			options.mode = LevelSurfacePasteMode::Seamless;
			options.allowValve220 = m_levelSurfaceTools && m_levelSurfaceTools->pasteOptions().allowValve220;
		}
		if (!pasteLevelSurfaceTargets(targets, options, &error)) { showLevelMaterialPaintMessage(error); }
		else { showLevelMaterialPaintMessage(tr("Surface paste queued. Progress and cancellation are in the Surfaces tab.")); }
	});
	layout->addWidget(buildLevelBrushTools());
	m_levelMaterialTools->hide();
	return m_levelMaterialTools;
}

void ApplicationShell::showLevelMaterialPaintMessage(const QString& message)
{
	if (m_levelMaterialPaintStatus) {
		m_levelMaterialPaintStatus->setText(message);
		m_levelMaterialPaintStatus->setAccessibleDescription(message);
		QAccessibleEvent changed(m_levelMaterialPaintStatus, QAccessible::DescriptionChanged);
		QAccessible::updateAccessibility(&changed);
	}
	statusBar()->showMessage(message, 6000);
}

void ApplicationShell::chooseLevelPaintMaterial(const QString& material)
{
	if (material != m_levelPaintMaterial && m_levelMap3D) { m_levelMap3D->finishSurfaceStroke(false); m_levelMap3D->finishBrushDraw(false); }
	m_levelPaintMaterial = material;
	if (m_levelMaterialPicker && m_levelMaterialPicker->currentText() != material) {
		const QSignalBlocker blocker(m_levelMaterialPicker);
		m_levelMaterialPicker->setEditText(material);
	}
}

void ApplicationShell::rememberLevelMaterial(const QString& material)
{
	chooseLevelPaintMaterial(material);
	m_lastAppliedTexture = material;
	m_recentMapTextures.removeIf([&material](const QString& recent) { return recent.compare(material, Qt::CaseInsensitive) == 0; });
	m_recentMapTextures.prepend(material);
	while (m_recentMapTextures.size() > 12) { m_recentMapTextures.removeLast(); }
}

void ApplicationShell::setLevelMaterialTool(int tool)
{
	if (!m_levelMap3D || tool < 0 || tool > 3) { return; }
	if (tool == 3 && m_levelMapDocument.format != LevelMapFormat::QuakeMap && m_levelMapDocument.format != LevelMapFormat::Quake3Map) { return; }
	if (tool != 0 && !levelMap3DShowing()) { setLevelMap3D(true); }
	if (tool == 3) {
		refreshLevelCameraBrush(); m_levelMap3D->setBrushDrawTool(true);
		showLevelMaterialPaintMessage(tr("Draw Brush · Drag a footprint; wheel changes depth. Release creates the brush; Escape cancels.")); return;
	}
	m_levelMap3D->setSurfaceTool(static_cast<ModelViewportSurfaceTool>(tool));
	showLevelMaterialPaintMessage(tool == 1 ? tr("Paint %1 · Drag across surfaces; release to apply. Escape cancels.").arg(m_levelPaintMaterial)
		: tool == 2 ? tr("Sample a surface to choose its material.") : tr("Camera navigation follows your editor profile."));
}

void ApplicationShell::refreshLevelMaterialTools(const QVector<LevelMapTextureUse>* textureUses)
{
	if (!m_levelMaterialTools) { return; }
	const bool editable = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map || levelMapDoomEditable();
	m_levelMaterialTools->setVisible(editable);
	m_levelMap3D->setMaterialGesturesEnabled(editable);
	if (m_levelPaintContextSerial != m_levelMapLoadSerial) {
		m_levelPaintContextSerial = m_levelMapLoadSerial;
		m_levelMap3D->setSurfaceTool(ModelViewportSurfaceTool::None);
		m_levelMap3D->finishSurfaceStroke(false);
		m_levelMaterialTargetInput->clear();
	}
	if (m_levelMaterialStroke && (m_levelStrokeSerial != m_levelMapLoadSerial || m_levelStrokeRevision != m_levelMapDocument.revision)) {
		m_levelMap3D->finishSurfaceStroke(false);
	}
	const QSignalBlocker blocker(m_levelMaterialPicker);
	m_levelMaterialPicker->clear();
	QStringList choices = m_recentMapTextures;
	QSet<QString> listed(choices.cbegin(), choices.cend());
	const auto uses = textureUses ? *textureUses : levelMapTextureUsage(m_levelMapDocument);
	for (const auto& use : uses) {
		if (!listed.contains(use.name)) { listed.insert(use.name); choices << use.name; }
	}
	m_levelMaterialPicker->addItems(choices);
	if (m_levelPaintMaterial.isEmpty() && !choices.isEmpty()) { m_levelPaintMaterial = choices.first(); }
	if (m_levelPaintMaterial.isEmpty() && (m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map)) {
		m_levelPaintMaterial = levelBrushMaterial();
	}
	m_levelMaterialPicker->setEditText(m_levelPaintMaterial);
}

bool ApplicationShell::applyLevelMaterialPaint(const QVector<LevelMaterialTarget>& targets, const QString& material, QString* error)
{
	LevelMaterialPaintPlan plan;
	if (!prepareLevelMaterialPaint(m_levelMapDocument, targets, material, &plan, error) || !commitLevelMaterialPaint(&m_levelMapDocument, plan, error)) { return false; }
	rememberLevelMaterial(plan.material());
	if (plan.changedCount() > 0) {
		recordActivity(tr("Map materials painted"), plan.material(), QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
		refreshLevelMapWorkbench();
	}
	showLevelMaterialPaintMessage(plan.changedCount() > 0 ? tr("Painted %n surface(s) with %1.", nullptr, plan.changedCount()).arg(plan.material())
		: tr("The targets already use %1; no map edit was added.").arg(plan.material()));
	return true;
}

bool ApplicationShell::sampleLevelMaterialTarget(const LevelMaterialTarget& target, QString* error)
{
	if (levelSurfaceEditsPending()) {
		if (error) { *error = tr("Finish or cancel the pending surface edit before sampling."); } return false;
	}
	QString material;
	if (!sampleLevelMaterial(m_levelMapDocument, target, &material, error)) { return false; }
	QString clipboardError;
	if (target.kind == LevelMaterialKind::BrushFace) {
		if (!copyLevelSurfaceSettings({target.objectId, target.faceIndex}, &clipboardError)) { m_levelSurfaceClipboard = {}; }
	} else { m_levelSurfaceClipboard = {}; }
	if (!m_levelSurfaceClipboard.ready()) {
		m_levelSurfaceClipboardArchive.reset(); m_levelSurfaceClipboardStaging.reset();
		m_levelSurfaceClipboardPalette.clear(); m_levelSurfaceClipboardEngineFamily.clear();
		m_levelSurfaceClipboardFormat = LevelMapFormat::Unknown;
	}
	refreshLevelSurfaceTools();
	chooseLevelPaintMaterial(material);
	if (m_levelMaterialTargetInput) { m_levelMaterialTargetInput->setText(levelMaterialTargetId(target)); }
	showLevelMaterialPaintMessage(tr("Sampled %1 from %2.").arg(material, levelMaterialTargetId(target))
		+ (clipboardError.isEmpty() ? QString() : tr(" Surface clipboard unavailable: %1").arg(clipboardError)));
	return true;
}

void ApplicationShell::rebuildLevelMaterialTargets(const QVector<LevelMaterialTarget>& targets)
{
	m_levelMap3D->finishSurfaceStroke(false);
	m_levelPreviewMaterialTargets = targets;
	m_levelMaterialTriangles.clear();
	for (int i = 0; i < targets.size(); ++i) {
		if (targets[i].kind != LevelMaterialKind::None) { m_levelMaterialTriangles[targets[i]].append(i); }
	}
}

void ApplicationShell::beginLevelMaterialStroke()
{
	m_levelStrokeTargets.clear(); m_levelStrokeTriangles.clear();
	m_levelStrokeSerial = m_levelMapLoadSerial;
	m_levelStrokeRevision = m_levelMapDocument.revision;
	m_levelStrokeMaterial = m_levelPaintMaterial;
	m_levelMaterialStroke = true;
	m_levelMaterialStrokeCancel->setEnabled(true);
	showLevelMaterialPaintMessage(tr("Material stroke pending · Release to apply; Escape cancels."));
}

void ApplicationShell::touchLevelMaterialSurface(int triangle)
{
	if (!m_levelMaterialStroke || m_levelStrokeSerial != m_levelMapLoadSerial || m_levelStrokeRevision != m_levelMapDocument.revision) {
		m_levelMap3D->finishSurfaceStroke(false); return;
	}
	if (triangle < 0 || triangle >= m_levelPreviewMaterialTargets.size()) { return; }
	const auto target = m_levelPreviewMaterialTargets[triangle];
	if (target.kind == LevelMaterialKind::None) { showLevelMaterialPaintMessage(tr("Placed model materials are edited in Models. This surface was skipped.")); return; }
	if (m_levelStrokeTargets.contains(target)) { return; }
	if (m_levelStrokeTargets.size() >= 16384) {
		m_levelMap3D->finishSurfaceStroke(false);
		showLevelMaterialPaintMessage(tr("The stroke exceeds 16384 surfaces and was cancelled.")); return;
	}
	m_levelStrokeTargets.insert(target);
	m_levelStrokeTriangles += m_levelMaterialTriangles.value(target);
	m_levelMaterialTargetInput->setText(levelMaterialTargetId(target));
	m_levelMap3D->setSurfaceStrokePreview(m_levelStrokeTriangles, static_cast<int>(m_levelStrokeTargets.size()));
	showLevelMaterialPaintMessage(tr("%n surface(s) pending · %1 · Release to apply; Escape cancels.", nullptr, static_cast<int>(m_levelStrokeTargets.size())).arg(m_levelStrokeMaterial));
}

void ApplicationShell::finishLevelMaterialStroke(bool commit)
{
	const bool current = m_levelMaterialStroke && m_levelStrokeSerial == m_levelMapLoadSerial && m_levelStrokeRevision == m_levelMapDocument.revision;
	m_levelMaterialStroke = false;
	m_levelMaterialStrokeCancel->setEnabled(false);
	const QVector<LevelMaterialTarget> targets(m_levelStrokeTargets.cbegin(), m_levelStrokeTargets.cend());
	m_levelStrokeTargets.clear(); m_levelStrokeTriangles.clear();
	if (!commit || !current) { showLevelMaterialPaintMessage(tr("Material stroke cancelled; the map was not changed.")); return; }
	if (targets.isEmpty()) { showLevelMaterialPaintMessage(tr("No paintable map surface was touched.")); return; }
	QString error;
	if (!applyLevelMaterialPaint(targets, m_levelStrokeMaterial, &error)) { showLevelMaterialPaintMessage(error); }
}

} // namespace vibestudio
