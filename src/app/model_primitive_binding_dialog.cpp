#include "app/model_primitive_binding_dialog.h"

#include "core/model_skeleton.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include <algorithm>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelPrimitiveBindingDialog)
};
void accessible(QWidget *widget, const QString &name, const QString &description)
{
	widget->setAccessibleName(name);
	widget->setAccessibleDescription(description);
	widget->setToolTip(description);
}
// The joint that weights the surface most, for a sensible rigid default.
QString heaviestJoint(const ModelMesh &mesh, int surface)
{
	QMap<int, double> totals;
	if (surface >= 0 && surface < mesh.surfaces.size())
		for (const auto &influence : mesh.surfaces[surface].skinning.influences)
			if (influence.joint >= 0 && influence.joint < mesh.skeleton.joints.size() && influence.weight > 0)
				totals[influence.joint] += influence.weight;
	int best = mesh.skeleton.joints.isEmpty() ? -1 : 0;
	for (auto i = totals.cbegin(); i != totals.cend(); ++i)
		if (i.value() > totals.value(best))
			best = i.key();
	return best >= 0 ? mesh.skeleton.joints[best].name : QString{};
}
} // namespace

ModelPrimitiveBindingDialog::ModelPrimitiveBindingDialog(const ModelMesh &mesh, int surface, const QString &joint,
	ModelNewGeometryBinding binding, bool newSurface, QWidget *parent)
	: QDialog(parent)
{
	setObjectName(QStringLiteral("modelPrimitiveBindingDialog"));
	setWindowTitle(Text::tr("Bind New Geometry"));
	accessible(this, windowTitle(), Text::tr("Choose how the new primitive's vertices follow the skeleton before it is added."));
	for (const auto &existing : mesh.surfaces)
		m_surfaceNames << existing.name;
	const QString surfaceName = mesh.surfaces.value(surface).name;
	auto *layout = new QVBoxLayout(this);
	auto *intro = new QLabel(Text::tr("Surface %1 follows the skeleton. New vertices have no vertex to take weights from, so choose how "
									  "they follow it.")
								 .arg(surfaceName));
	intro->setObjectName(QStringLiteral("primitiveBindingIntro"));
	intro->setTextFormat(Qt::PlainText);
	intro->setWordWrap(true);
	layout->addWidget(intro);
	auto *choices = new QButtonGroup(this);
	m_rigid = new QRadioButton(Text::tr("Bind rigidly to a &joint"));
	m_rigid->setObjectName(QStringLiteral("primitiveBindJoint"));
	accessible(m_rigid, Text::tr("Bind rigidly to a joint"),
		Text::tr("Every new vertex follows one joint with full weight. Use this for helmets, armour plates and weapons."));
	m_joint = new QComboBox;
	m_joint->setObjectName(QStringLiteral("primitiveBindingJoint"));
	accessible(m_joint, Text::tr("Joint"), Text::tr("The joint the new vertices follow."));
	for (const auto &item : mesh.skeleton.joints)
		m_joint->addItem(item.name);
	const QString preferred = modelJointIndex(mesh.skeleton, joint, Qt::CaseSensitive) >= 0 ? joint : heaviestJoint(mesh, surface);
	m_joint->setCurrentIndex(std::max(0, m_joint->findText(preferred)));
	m_nearest = new QRadioButton(Text::tr("Bind to the &nearest bones"));
	m_nearest->setObjectName(QStringLiteral("primitiveBindNearest"));
	accessible(m_nearest, Text::tr("Bind to the nearest bones"),
		Text::tr("Each new vertex takes up to four of the closest bones, as Skin Weights and Binding does. Check the result in Weight "
				 "Paint."));
	m_separate = new QRadioButton(Text::tr("Add as a new &unskinned surface"));
	m_separate->setObjectName(QStringLiteral("primitiveNewSurface"));
	accessible(m_separate, Text::tr("Add as a new unskinned surface"),
		Text::tr("Add the primitive as its own surface with no skin weights. It keeps its shape in every frame."));
	m_ownSurface = new QCheckBox(Text::tr("Add as a new &surface"));
	m_ownSurface->setObjectName(QStringLiteral("primitiveOwnSurface"));
	accessible(m_ownSurface, Text::tr("Add as a new surface"),
		Text::tr("Add the bound primitive as its own surface, for example for a separate shader, instead of joining it to the selected "
				 "surface."));
	m_name = new QLineEdit;
	m_name->setObjectName(QStringLiteral("primitiveSurfaceName"));
	m_name->setMaxLength(128);
	accessible(m_name, Text::tr("New surface name"), Text::tr("A name no other surface uses."));
	QString name = Text::tr("%1 primitive").arg(surfaceName);
	for (int n = 2; m_surfaceNames.contains(name); ++n)
		name = Text::tr("%1 primitive %2").arg(surfaceName).arg(n);
	m_name->setText(name);
	for (auto *button : {m_rigid, m_nearest, m_separate})
		choices->addButton(button);
	auto *form = new QFormLayout;
	form->addRow(m_rigid);
	form->addRow(Text::tr("Joint"), m_joint);
	form->addRow(m_nearest);
	form->addRow(m_separate);
	form->addRow(m_ownSurface);
	form->addRow(Text::tr("Surface name"), m_name);
	layout->addLayout(form);
	m_status = new LiveStatusLabel;
	m_status->setObjectName(QStringLiteral("primitiveBindingStatus"));
	m_status->setTextFormat(Qt::PlainText);
	m_status->setWordWrap(true);
	accessible(m_status, Text::tr("Binding status"), Text::tr("What the chosen binding does to the new vertices."));
	layout->addWidget(m_status);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	m_apply = buttons->button(QDialogButtonBox::Ok);
	m_apply->setObjectName(QStringLiteral("primitiveBindingApply"));
	m_apply->setText(Text::tr("&Add Primitive"));
	accessible(m_apply, Text::tr("Add Primitive"), Text::tr("Add the primitive with this binding as one undoable edit."));
	layout->addWidget(buttons);
	(binding == ModelNewGeometryBinding::None ? m_separate : binding == ModelNewGeometryBinding::NearestBones ? m_nearest : m_rigid)->setChecked(true);
	m_ownSurface->setChecked(newSurface);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(choices, &QButtonGroup::buttonToggled, this, [this] { refreshState(); });
	connect(m_joint, &QComboBox::currentIndexChanged, this, [this] { refreshState(); });
	connect(m_name, &QLineEdit::textChanged, this, [this] { refreshState(); });
	connect(m_ownSurface, &QCheckBox::toggled, this, [this] { refreshState(); });
	refreshState();
}

ModelNewGeometryBinding ModelPrimitiveBindingDialog::binding() const
{
	return m_rigid->isChecked() ? ModelNewGeometryBinding::Joint
		   : m_nearest->isChecked() ? ModelNewGeometryBinding::NearestBones
									: ModelNewGeometryBinding::None;
}

QString ModelPrimitiveBindingDialog::joint() const
{
	return m_rigid->isChecked() ? m_joint->currentText() : QString{};
}

QString ModelPrimitiveBindingDialog::newSurface() const
{
	return m_separate->isChecked() || m_ownSurface->isChecked() ? m_name->text().trimmed() : QString{};
}

void ModelPrimitiveBindingDialog::refreshState()
{
	// Unskinned vertices cannot join a surface that follows the skeleton, so
	// they always get a surface of their own.
	const bool unskinned = m_separate->isChecked();
	m_joint->setEnabled(m_rigid->isChecked());
	m_ownSurface->setEnabled(!unskinned);
	const bool separate = unskinned || m_ownSurface->isChecked();
	m_name->setEnabled(separate);
	const auto name = m_name->text().trimmed();
	bool ready = true;
	if (separate && name.isEmpty())
	{
		m_status->setText(Text::tr("Enter a name for the new surface."));
		ready = false;
	}
	else if (separate && m_surfaceNames.contains(name))
	{
		m_status->setText(Text::tr("A surface named %1 already exists. Choose another name.").arg(name));
		ready = false;
	}
	else if (unskinned)
		m_status->setText(Text::tr("The primitive becomes surface %1 with no skin weights.").arg(name));
	else if (m_rigid->isChecked())
		m_status->setText(separate ? Text::tr("The primitive becomes surface %1, and every new vertex follows %2 with full weight.")
										 .arg(name, m_joint->currentText())
								   : Text::tr("Every new vertex follows %1 with full weight.").arg(m_joint->currentText()));
	else
		m_status->setText(separate ? Text::tr("The primitive becomes surface %1, and each new vertex follows the bones nearest to it.").arg(name)
								   : Text::tr("Each new vertex follows the bones nearest to it. The status line counts the bound vertices."));
	m_apply->setEnabled(ready);
}
} // namespace vibestudio
