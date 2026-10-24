#include "app/level_component_transform_dialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace vibestudio {
LevelComponentTransformDialog::LevelComponentTransformDialog(const LevelComponentTransformRequest& initial,
	const LevelMapVec3& centre, QWidget* parent) : QDialog(parent)
{
	setObjectName(QStringLiteral("levelComponentTransformDialog"));
	setWindowTitle(tr("Transform Components")); setAccessibleName(windowTitle());
	setAccessibleDescription(tr("Move, rotate, scale or shear picked vertices, edges, faces and patch control points as one undo step."));
	auto* layout = new QVBoxLayout(this);
	m_scope = new QLabel; m_scope->setObjectName(QStringLiteral("componentTransformScope")); m_scope->setWordWrap(true);
	m_scope->setAccessibleName(tr("Transform scope")); m_scope->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
	m_scope->hide(); layout->addWidget(m_scope);
	auto* scroll = new QScrollArea(this); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
	auto* contents = new QWidget; auto* form = new QFormLayout(contents);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow); form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	form->setFormAlignment(Qt::AlignTop);
	scroll->setWidget(contents); layout->addWidget(scroll);
	const auto number = [](const QString& name, const QString& label, double minimum, double maximum, double value) {
		auto* field = new QDoubleSpinBox; field->setObjectName(name); field->setAccessibleName(label);
		field->setDecimals(6); field->setRange(minimum,maximum); field->setValue(value); field->setLayoutDirection(Qt::LeftToRight);
		field->setKeyboardTracking(false); return field;
	};
	const auto axes = [](const QString& name, const QString& label, int index) {
		auto* field = new QComboBox; field->setObjectName(name); field->setAccessibleName(label);
		field->addItems({QStringLiteral("X"),QStringLiteral("Y"),QStringLiteral("Z")}); field->setCurrentIndex(index); return field;
	};
	m_operation = new QComboBox; m_operation->setObjectName(QStringLiteral("componentOperation")); m_operation->setAccessibleName(tr("Operation"));
	m_operation->addItems({tr("Move"),tr("Rotate"),tr("Scale"),tr("Shear")}); form->addRow(tr("&Operation"),m_operation);
	auto* pages = new QStackedWidget;
	pages->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Fixed);
	const QStringList dimensions{QStringLiteral("X"),QStringLiteral("Y"),QStringLiteral("Z")};
	const std::array<double,3> delta{initial.delta.x,initial.delta.y,initial.delta.z}, scale{initial.scale.x,initial.scale.y,initial.scale.z};
	for (int operation = 0; operation < 4; ++operation) {
		auto* page = new QWidget; auto* fields = new QFormLayout(page); fields->setContentsMargins(0,0,0,0);
		fields->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow); fields->setRowWrapPolicy(QFormLayout::WrapLongRows);
		if (operation == 0 || operation == 2) {
			for (int axis = 0; axis < 3; ++axis) {
				const auto label = operation == 0 ? tr("Move %1").arg(dimensions[axis]) : tr("Scale %1").arg(dimensions[axis]);
				auto* field = number(QStringLiteral("component%1%2").arg(operation == 0 ? "Delta" : "Scale").arg(dimensions[axis]),label,
					operation == 0 ? -65536 : -1024,operation == 0 ? 65536 : 1024,operation == 0 ? delta[axis] : scale[axis]);
				(operation == 0 ? m_delta : m_scale)[axis] = field; fields->addRow(label,field);
			}
		} else if (operation == 1) {
			m_rotationAxis = axes(QStringLiteral("componentRotationAxis"),tr("Rotation axis"),initial.axis);
			m_angle = number(QStringLiteral("componentAngle"),tr("Angle in degrees"),-360000,360000,initial.amount);
			fields->addRow(tr("Rotation axis"),m_rotationAxis); fields->addRow(tr("Angle (degrees)"),m_angle);
		} else {
			m_shearAxis = axes(QStringLiteral("componentShearAxis"),tr("Shear axis"),initial.axis);
			m_shearAlong = axes(QStringLiteral("componentShearAlong"),tr("Along axis"),initial.along);
			m_factor = number(QStringLiteral("componentFactor"),tr("Shear displacement per unit"),-1024,1024,initial.amount);
			fields->addRow(tr("Shear axis"),m_shearAxis); fields->addRow(tr("Along axis"),m_shearAlong); fields->addRow(tr("Displacement per unit"),m_factor);
		}
		pages->addWidget(page);
	}
	form->addRow(pages);
	connect(m_operation,&QComboBox::currentIndexChanged,pages,&QStackedWidget::setCurrentIndex);
	m_operation->setCurrentIndex(static_cast<int>(initial.operation)); pages->setCurrentIndex(m_operation->currentIndex());
	m_customPivot = new QCheckBox(tr("Use a custom pivot")); m_customPivot->setObjectName(QStringLiteral("componentCustomPivot"));
	m_customPivot->setAccessibleName(m_customPivot->text()); m_customPivot->setChecked(initial.pivot.valid);
	m_customPivot->setToolTip(tr("When off, use the centre of the picked components' combined bounds.")); form->addRow(m_customPivot);
	const auto pivot = initial.pivot.valid ? initial.pivot : centre;
	const std::array<double,3> coordinates{pivot.x,pivot.y,pivot.z};
	for (int axis = 0; axis < 3; ++axis) {
		const auto label = tr("Pivot %1").arg(dimensions[axis]);
		m_pivot[axis] = number(QStringLiteral("componentPivot%1").arg(dimensions[axis]),label,-32768,32768,coordinates[axis]);
		m_pivot[axis]->setEnabled(initial.pivot.valid); form->addRow(label,m_pivot[axis]);
		connect(m_customPivot,&QCheckBox::toggled,m_pivot[axis],&QWidget::setEnabled);
	}
	m_grid = number(QStringLiteral("componentGrid"),tr("Grid after transformation"),0,32768,initial.grid);
	m_grid->setSpecialValueText(tr("No snapping")); form->addRow(tr("Grid"),m_grid);
	const auto check = [form](const QString& name, const QString& label, const QString& description, bool value) {
		auto* field = new QCheckBox(label); field->setObjectName(name); field->setAccessibleName(label);
		field->setAccessibleDescription(description); field->setToolTip(description); field->setChecked(value); form->addRow(field); return field;
	};
	m_collapse = check(QStringLiteral("componentAllowCollapse"),tr("Allow coincident vertices to merge"),tr("Every resulting brush must remain a valid three-dimensional convex solid."),initial.allowCollapse);
	m_uvLock = check(QStringLiteral("componentUvLock"),tr("Lock component UVs"),tr("Keep each surviving corner's original texture coordinates."),initial.textureLock);
	m_uvFit = check(QStringLiteral("componentUvFit"),tr("Fit UVs when the lock fails"),
		tr("Where no single projection can keep every corner's texture coordinates, keep three corners, preferring ones that did not move, and let the others drift. Off refuses such edits."),
		initial.fitUvLock);
	m_uvFit->setEnabled(initial.textureLock);
	connect(m_uvLock,&QCheckBox::toggled,m_uvFit,&QWidget::setEnabled);
	m_valve = check(QStringLiteral("componentAllowValve220"),tr("Allow map-wide Valve 220 conversion"),tr("If classic face mapping cannot preserve UVs, convert every classic face in the map. Undo restores the original dialect."),initial.allowValve220);
	m_error = new QLabel; m_error->setObjectName(QStringLiteral("componentTransformError")); m_error->setWordWrap(true);
	m_error->setAccessibleName(tr("Transform result")); m_error->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse); layout->addWidget(m_error);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Transform")); buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("componentTransformApply")); layout->addWidget(buttons);
	connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
	connect(buttons,&QDialogButtonBox::accepted,this,[this] {
		QString error;
		if (m_apply && !m_apply(request(),&error)) { m_error->setText(error); m_error->setAccessibleDescription(error); return; }
		accept();
	});
	resize(560,660); m_operation->setFocus();
}
LevelComponentTransformRequest LevelComponentTransformDialog::request() const
{
	LevelComponentTransformRequest result;
	result.operation = static_cast<LevelComponentTransformKind>(m_operation->currentIndex());
	result.delta = {m_delta[0]->value(),m_delta[1]->value(),m_delta[2]->value(),true};
	result.scale = {m_scale[0]->value(),m_scale[1]->value(),m_scale[2]->value(),true};
	result.axis = result.operation == LevelComponentTransformKind::Rotate ? m_rotationAxis->currentIndex() : m_shearAxis->currentIndex();
	result.along = m_shearAlong->currentIndex();
	result.amount = result.operation == LevelComponentTransformKind::Rotate ? m_angle->value() : m_factor->value();
	if (m_customPivot->isChecked()) { result.pivot = {m_pivot[0]->value(),m_pivot[1]->value(),m_pivot[2]->value(),true}; }
	result.grid = m_grid->value(); result.allowCollapse = m_collapse->isChecked(); result.textureLock = m_uvLock->isChecked(); result.allowValve220 = m_valve->isChecked();
	result.fitUvLock = m_uvLock->isChecked() && m_uvFit->isChecked();
	return result;
}
void LevelComponentTransformDialog::setApplyHandler(std::function<bool(const LevelComponentTransformRequest&, QString*)> handler) { m_apply = std::move(handler); }
void LevelComponentTransformDialog::setScopeSummary(const QString& text)
{
	m_scope->setText(text); m_scope->setAccessibleDescription(text); m_scope->setVisible(!text.isEmpty());
}
} // namespace vibestudio
