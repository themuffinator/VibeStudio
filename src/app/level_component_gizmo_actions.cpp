#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QScrollArea>
#include <QStatusBar>
#include <QVBoxLayout>
#include <algorithm>

namespace vibestudio {
void ApplicationShell::setLevelComponentTool(int tool)
{
	for (auto* view : std::as_const(m_levelPlanViews)) { view->finishComponentMove(false); }
	if (m_levelMap3D) { m_levelMap3D->finishComponentMove(false); }
	m_levelComponentTool = std::clamp(tool,0,4);
	m_levelVertexInsertion = false;
	if (tool) {
		m_levelComponentExtrusion = 0;
		for (auto* view : std::as_const(m_levelPlanViews)) { view->setComponentSplit(false); }
	}
	refreshLevelCameraComponents(); refreshLevelGeometryCommands(); refreshLevelToolShelf();
}
void ApplicationShell::refreshLevelComponentGizmos()
{
	if (!m_levelMapViewport || !m_levelMap3D) { return; }
	if (m_levelComponentPivotLoad != m_levelMapLoadSerial || !levelComponentsActive()) {
		m_levelComponentPivotLoad = m_levelMapLoadSerial; m_levelComponentPivot = {};
		if (!levelComponentsActive()) { m_levelComponentTool = 0; }
	}
	const auto steps = m_settings.levelComponentTransformSteps();
	LevelMapVec3 lo,hi;
	// Faces picked by Select Textured share the manipulator in Face mode.
	const bool available = !levelSweepToolActive() && !m_levelVertexInsertion && levelComponentsActive() && levelMapComponentBounds(m_levelMapDocument,levelComponentTransformSets(),&lo,&hi);
	const auto pivot = m_levelComponentPivot.valid ? m_levelComponentPivot : LevelMapVec3{(lo.x+hi.x)/2,(lo.y+hi.y)/2,(lo.z+hi.z)/2,available};
	const auto tool = static_cast<LevelComponentTool>(m_levelComponentTool);
	for (auto* view : std::as_const(m_levelPlanViews)) {
		view->componentGizmo()->setContext(tool,pivot,available,view->snapToGrid() ? view->gridSize() : 0,steps[0],steps[1],steps[2]);
	}
	m_levelMap3D->componentGizmo()->setContext(tool,pivot,available,m_levelMapViewport->snapToGrid() ? m_levelMapViewport->gridSize() : 0,steps[0],steps[1],steps[2]);
}
void ApplicationShell::editLevelComponentGizmoOptions()
{
	const auto loadSerial = m_levelMapLoadSerial;
	QDialog dialog(this); dialog.setObjectName(QStringLiteral("levelComponentGizmoOptions"));
	dialog.setWindowTitle(tr("Component Manipulator Options")); dialog.setAccessibleName(dialog.windowTitle());
	auto* layout = new QVBoxLayout(&dialog); auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
	auto* contents = new QWidget; auto* form = new QFormLayout(contents); form->setRowWrapPolicy(QFormLayout::WrapLongRows); form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	scroll->setWidget(contents); layout->addWidget(scroll);
	const auto number = [form](const QString& label,const QString& name,double minimum,double maximum,double value) {
		auto* field = new QDoubleSpinBox; field->setRange(minimum,maximum); field->setDecimals(6); field->setValue(value); field->setObjectName(name);
		field->setAccessibleName(label); field->setLayoutDirection(Qt::LeftToRight); field->setKeyboardTracking(false); form->addRow(label,field); return field;
	};
	const auto steps = m_settings.levelComponentTransformSteps();
	auto* angle = number(tr("Rotation step (degrees)"),QStringLiteral("componentAngleStep"),0,360,steps[0]);
	auto* scale = number(tr("Scale factor step"),QStringLiteral("componentScaleStep"),0,1,steps[1]);
	auto* shear = number(tr("Shear factor step"),QStringLiteral("componentShearStep"),0,1,steps[2]);
	for (auto* field : {angle,scale,shear}) { field->setSpecialValueText(tr("No snapping")); }
	auto* custom = new QCheckBox(tr("Use a custom component pivot")); custom->setObjectName(QStringLiteral("componentGizmoCustomPivot"));
	custom->setAccessibleName(custom->text()); custom->setChecked(m_levelComponentPivot.valid); form->addRow(custom);
	LevelMapVec3 lo,hi; levelMapComponentBounds(m_levelMapDocument,levelComponentTransformSets(),&lo,&hi);
	const auto pivot = m_levelComponentPivot.valid ? m_levelComponentPivot : LevelMapVec3{(lo.x+hi.x)/2,(lo.y+hi.y)/2,(lo.z+hi.z)/2,true};
	auto* x = number(tr("Pivot X"),QStringLiteral("componentGizmoPivotX"),-32768,32768,pivot.x);
	auto* y = number(tr("Pivot Y"),QStringLiteral("componentGizmoPivotY"),-32768,32768,pivot.y);
	auto* z = number(tr("Pivot Z"),QStringLiteral("componentGizmoPivotZ"),-32768,32768,pivot.z);
	for (auto* field : {x,y,z}) { field->setEnabled(custom->isChecked()); connect(custom,&QCheckBox::toggled,field,&QWidget::setEnabled); }
	auto* note = new QLabel(tr("The pivot is shared across plan and camera views. Reset Component Pivot restores the picked corners' bounds centre. Pivot moves do not edit the map.")); note->setWordWrap(true); form->addRow(note);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel); layout->addWidget(buttons);
	connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept); connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
	dialog.resize(520,500); if (dialog.exec() != QDialog::Accepted) { return; }
	for (auto* view : std::as_const(m_levelPlanViews)) { view->finishComponentMove(false); }
	if (m_levelMap3D) { m_levelMap3D->finishComponentMove(false); }
	m_settings.setLevelComponentTransformSteps({angle->value(),scale->value(),shear->value()});
	if (loadSerial == m_levelMapLoadSerial) {
		m_levelComponentPivot = custom->isChecked() ? LevelMapVec3{x->value(),y->value(),z->value(),true} : LevelMapVec3{};
	}
	refreshLevelComponentGizmos(); refreshLevelGeometryCommands();
}
} // namespace vibestudio
