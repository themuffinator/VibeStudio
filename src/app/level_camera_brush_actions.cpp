#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "app/studio_layout.h"
#include <QAction>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QToolButton>
#include <algorithm>

namespace vibestudio {

QWidget* ApplicationShell::buildLevelBrushTools()
{
	m_levelBrushTools = new QWidget;
	m_levelBrushTools->setObjectName(QStringLiteral("levelBrushTools"));
	auto* form = new QFormLayout(m_levelBrushTools);
	form->setContentsMargins(0,0,0,0); form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	auto* row = new QHBoxLayout;
	m_levelBrushPlane = new QComboBox;
	m_levelBrushPlane->setObjectName(QStringLiteral("levelBrushPlane"));
	m_levelBrushPlane->setAccessibleName(tr("Brush construction plane"));
	m_levelBrushPlane->addItem(QStringLiteral("XY"),2); m_levelBrushPlane->addItem(QStringLiteral("XZ"),1); m_levelBrushPlane->addItem(QStringLiteral("YZ"),0);
	row->addWidget(m_levelBrushPlane);
	const auto spin = [&](const QString& name, const QString& prefix, const QString& description, double low, double high) {
		auto* control = new QDoubleSpinBox;
		control->setObjectName(name); control->setRange(low,high); control->setDecimals(3); control->setKeyboardTracking(false);
		control->setPrefix(prefix); control->setAccessibleName(description); control->setToolTip(description);
		row->addWidget(control,1); return control;
	};
	m_levelBrushBase = spin(QStringLiteral("levelBrushBase"),tr("Base "),tr("Construction plane coordinate on its hidden axis"),-32768,32767);
	m_levelBrushDepth = spin(QStringLiteral("levelBrushDepth"),tr("Depth "),tr("Brush depth along the plane's hidden axis"),1,32768);
	m_levelBrushDepth->setValue(64);
	form->addRow(tr("Construction"),row);
	m_levelBrushDirection = new QComboBox;
	m_levelBrushDirection->setObjectName(QStringLiteral("levelBrushDirection"));
	m_levelBrushDirection->setAccessibleName(tr("Brush depth direction"));
	m_levelBrushDirection->setToolTip(tr("Extend the brush from the construction plane along the positive or negative hidden axis."));
	m_levelBrushDirection->addItem(tr("Positive (+)"),1); m_levelBrushDirection->addItem(tr("Negative (−)"),-1);
	form->addRow(tr("Direction"),m_levelBrushDirection);
	auto* buttons = new QHBoxLayout;
	const auto button = [&](const QString& name, const QString& text, const QString& tip) {
		auto* control = new QToolButton; control->setObjectName(name); control->setText(text); control->setAccessibleName(text); control->setToolTip(tip);
		buttons->addWidget(control); return control;
	};
	auto* workZone = button(QStringLiteral("levelBrushWorkZone"),tr("Use Work Zone"),tr("Use the last selection's depth on the chosen plane, or 0–64 in a new map."));
	auto* surface = button(QStringLiteral("levelBrushCameraSurface"),tr("Use Camera Surface"),tr("Place the construction plane on the surface at the centre of the camera and extend towards the camera. Sloped faces use their nearest axis plane."));
	buttons->addStretch(); form->addRow(buttons); buttons = new QHBoxLayout;
	auto* numeric = button(QStringLiteral("levelBrushNumeric"),tr("Numeric Brush…"),tr("Create a brush using exact bounds and a primitive preview."));
	buttons->addStretch(); form->addRow(buttons);
	const auto apply = [this] {
		const QSignalBlocker blocker(m_levelBrushDepth),baseBlocker(m_levelBrushBase);
		const int direction = m_levelBrushDirection->currentData().toInt();
		m_levelBrushBase->setRange(direction > 0 ? -32768 : -32767,direction > 0 ? 32767 : 32768);
		m_levelBrushDepth->setMaximum(32768-direction*m_levelBrushBase->value());
		m_levelMap3D->setBrushDrawPlane(m_levelBrushPlane->currentData().toInt(),m_levelBrushBase->value(),m_levelBrushDepth->value(),
			m_levelControls.plan.squareModifiers,m_levelControls.plan.cubeModifiers,direction);
	};
	connect(m_levelBrushPlane,&QComboBox::currentIndexChanged,this,[this] { useLevelBrushWorkZone(); });
	connect(m_levelBrushBase,&QDoubleSpinBox::valueChanged,this,apply);
	connect(m_levelBrushDepth,&QDoubleSpinBox::valueChanged,this,apply);
	connect(m_levelBrushDirection,&QComboBox::currentIndexChanged,this,apply);
	connect(workZone,&QToolButton::clicked,this,&ApplicationShell::useLevelBrushWorkZone);
	connect(surface,&QToolButton::clicked,this,[this] {
		m_levelMap3D->finishBrushDraw(false);
		if (!m_levelMap3D->setBrushDrawPlaneFromSurface(QPointF(m_levelMap3D->width()*0.5,m_levelMap3D->height()*0.5))) { return; }
		const QSignalBlocker plane(m_levelBrushPlane),base(m_levelBrushBase),depth(m_levelBrushDepth),direction(m_levelBrushDirection);
		const int sign = m_levelMap3D->brushDrawDirection();
		m_levelBrushPlane->setCurrentIndex(m_levelBrushPlane->findData(m_levelMap3D->brushDrawAxis()));
		m_levelBrushDirection->setCurrentIndex(m_levelBrushDirection->findData(sign));
		m_levelBrushBase->setRange(sign > 0 ? -32768 : -32767,sign > 0 ? 32767 : 32768);
		m_levelBrushBase->setValue(m_levelMap3D->brushDrawBase());
		m_levelBrushDepth->setMaximum(32768-sign*m_levelMap3D->brushDrawBase());
		m_levelBrushDepth->setValue(m_levelMap3D->brushDrawDepth());
	});
	connect(numeric,&QToolButton::clicked,this,[this] { m_levelMap3D->finishBrushDraw(false); addLevelMapBrushFromUi(); });
	connect(m_levelMap3D,&ModelViewport::brushDrawToolChanged,this,[this] {
		const bool active = m_levelMap3D->brushDrawTool();
		refreshLevelToolShelf();
		m_levelBrushTools->setVisible(active);
		m_levelMaterialStrokeCancel->setText(active ? tr("Cancel Draft") : tr("Cancel Stroke"));
		m_levelMaterialStrokeCancel->setAccessibleName(active ? tr("Cancel brush draft") : tr("Cancel material stroke"));
		m_levelMaterialStrokeCancel->setEnabled(active ? m_levelMap3D->isDrawingBrush() : m_levelMaterialStroke);
		m_levelMaterialPaintStatus->setAccessibleName(active ? tr("Brush drawing status") : tr("Material painting status"));
		if (!active) { showLevelMaterialPaintMessage(tr("Camera navigation follows your editor profile.")); }
		const QSignalBlocker blocker(m_levelMaterialTool);
		m_levelMaterialTool->setCurrentIndex(active ? 3 : int(m_levelMap3D->surfaceTool()));
		if (m_commands) { if (auto* action = m_commands->action(QStringLiteral("map.drawBrush"))) { action->setChecked(active); } }
	});
	connect(m_levelMap3D,&ModelViewport::brushDrawPreviewChanged,this,[this] {
		const QSignalBlocker blocker(m_levelBrushDepth); m_levelBrushDepth->setValue(m_levelMap3D->brushDrawDepth());
	});
	m_levelBrushTools->hide(); return m_levelBrushTools;
}

void ApplicationShell::useLevelBrushWorkZone()
{
	if (!m_levelBrushPlane || !m_levelMapViewport) { return; }
	const auto range = m_levelMapViewport->brushDepthRange(m_levelBrushPlane->currentData().toInt());
	const int direction = m_levelBrushDirection->currentData().toInt();
	const double base = std::clamp(direction > 0 ? range.first : range.second,direction > 0 ? -32768.0 : -32767.0,direction > 0 ? 32767.0 : 32768.0);
	const double depth = std::clamp(range.second-range.first,1.0,32768-direction*base);
	const QSignalBlocker a(m_levelBrushBase),b(m_levelBrushDepth);
	m_levelBrushBase->setRange(direction > 0 ? -32768 : -32767,direction > 0 ? 32767 : 32768);
	m_levelBrushBase->setValue(base); m_levelBrushDepth->setMaximum(32768-direction*base); m_levelBrushDepth->setValue(depth);
	m_levelMap3D->setBrushDrawPlane(m_levelBrushPlane->currentData().toInt(),base,depth,
		m_levelControls.plan.squareModifiers,m_levelControls.plan.cubeModifiers,direction);
}

void ApplicationShell::connectLevelCameraBrush()
{
	connectLevelCameraPlacement();
	struct Source {
		quint64 revision = 0, loadSerial = 0;
		QVector<LevelMapSelectionRef> selection;
		QString destination,material;
	};
	const auto source = std::make_shared<Source>();
	connect(m_levelMap3D,&ModelViewport::brushDrawActiveChanged,this,[this,source](bool active) {
		if (m_levelMaterialStrokeCancel && m_levelMap3D->brushDrawTool()) { m_levelMaterialStrokeCancel->setEnabled(active); }
		if (!active) {
			for (auto* view : m_levelPlanViews) { view->setCameraBrushDraft(); }
			return;
		}
		source->revision = m_levelMapDocument.revision; source->loadSerial = m_levelMapLoadSerial;
		source->selection = m_levelMapDocument.selection; source->destination = m_levelMapDocument.activeSceneNode;
		source->material = m_levelPaintMaterial;
	});
	connect(m_levelMap3D,&ModelViewport::brushDrawPreviewChanged,this,[this] {
		const auto box = m_levelMap3D->brushDrawBox(); const bool valid = m_levelMap3D->isDrawingBrush() && m_levelMap3D->brushDrawValid();
		for (auto* view : m_levelPlanViews) {
			view->setCameraBrushDraft({box.mins[0],box.mins[1],box.mins[2],valid},{box.maxs[0],box.maxs[1],box.maxs[2],valid});
		}
	});
	connect(m_levelMap3D,&ModelViewport::brushDrawRequested,this,[this,source](const ResizeBox& box) {
		if (source->revision != m_levelMapDocument.revision || source->loadSerial != m_levelMapLoadSerial
			|| source->selection != m_levelMapDocument.selection || source->destination != m_levelMapDocument.activeSceneNode
			|| source->material != m_levelPaintMaterial) {
			statusBar()->showMessage(tr("The map, selection, creation layer or material changed. The brush draft was cancelled.")); return;
		}
		drawLevelMapBrushFromViewport({box.mins[0],box.mins[1],box.mins[2],true},{box.maxs[0],box.maxs[1],box.maxs[2],true},m_levelMap3D->brushDrawAxis());
	});
}

void ApplicationShell::refreshLevelCameraBrush()
{
	if (!m_levelBrushTools) { return; }
	const bool editable = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	if (auto* model = qobject_cast<QStandardItemModel*>(m_levelMaterialTool->model())) { model->item(3)->setEnabled(editable); }
	if (!editable || m_levelBrushWorkSerial != m_levelMapLoadSerial) { m_levelMap3D->setBrushDrawTool(false); }
	if (m_levelBrushWorkSerial != m_levelMapLoadSerial || m_levelBrushWorkSelection != m_levelMapDocument.selection) {
		m_levelMap3D->finishBrushDraw(false);
		m_levelBrushWorkSerial = m_levelMapLoadSerial; m_levelBrushWorkSelection = m_levelMapDocument.selection;
		useLevelBrushWorkZone();
	}
	const double step = m_levelMapViewport && m_levelMapViewport->snapToGrid() ? std::max(1,m_levelMapViewport->gridSize()) : 1;
	m_levelBrushBase->setSingleStep(step); m_levelBrushDepth->setSingleStep(step);
	m_levelMap3D->setBrushDrawPlane(m_levelMap3D->brushDrawAxis(),m_levelMap3D->brushDrawBase(),m_levelMap3D->brushDrawDepth(),
		m_levelControls.plan.squareModifiers,m_levelControls.plan.cubeModifiers,m_levelMap3D->brushDrawDirection());
}

} // namespace vibestudio
