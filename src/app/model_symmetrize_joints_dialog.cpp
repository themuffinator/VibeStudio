#include "app/model_symmetrize_joints_dialog.h"
#include "app/dialog_screen_fit.h"

#include "core/model_vertex_origins.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelSymmetrizeJointsDialog)
};
void accessible(QWidget *widget, const QString &name, const QString &description)
{
	widget->setAccessibleName(name);
	widget->setAccessibleDescription(description);
	widget->setToolTip(description);
}
} // namespace

ModelSymmetrizeJointsDialog::ModelSymmetrizeJointsDialog(const ModelMesh &mesh, int surface, int axis, const QMap<QString, QString> &joints,
	bool keepNames, double tolerance, QWidget *parent)
	: QDialog(parent), m_axis(std::clamp(axis, 0, 2)), m_tolerance(tolerance)
{
	m_rig.skeleton = mesh.skeleton;
	setObjectName(QStringLiteral("modelSymmetrizeJointsDialog"));
	setWindowTitle(Text::tr("Symmetrize Skin Weights"));
	accessible(this, windowTitle(),
		Text::tr("Choose the mirror joint for each off-centre joint before Symmetrize copies skin weights to the other half."));
	auto *layout = new QVBoxLayout(this);
	const QString axisName = QStringLiteral("XYZ").mid(std::clamp(axis, 0, 2), 1);
	auto *intro = new QLabel(Text::tr("Mirrored vertices take their source's weights through this map. Joints on the %1 = 0 plane keep "
									  "their names; each off-centre joint needs a mirror joint.")
								 .arg(axisName));
	intro->setObjectName(QStringLiteral("symmetrizeJointsIntro"));
	intro->setTextFormat(Qt::PlainText);
	intro->setWordWrap(true);
	layout->addWidget(intro);
	const auto names = modelOffCentreJoints(mesh, surface, axis, tolerance);
	// Remembered partners apply only where they mirror across this plane.
	const auto remembered = modelMirrorJointPairsForAxis(m_rig, joints, m_axis, -1, m_tolerance);
	m_table = new QTableWidget(names.size(), 2);
	m_table->setObjectName(QStringLiteral("symmetrizeJointMap"));
	m_table->setHorizontalHeaderLabels({Text::tr("Joint"), Text::tr("Mirror joint")});
	accessible(m_table, Text::tr("Joint mirror map"),
		Text::tr("Edit the exact name of each joint's mirror joint. An empty mirror joint is refused unless joint names are kept."));
	m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	m_table->verticalHeader()->setVisible(false);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setTabKeyNavigation(false);
	for (int row = 0; row < names.size(); ++row)
	{
		auto *joint = new QTableWidgetItem(names[row]);
		joint->setFlags(joint->flags() & ~Qt::ItemIsEditable);
		m_table->setItem(row, 0, joint);
		auto *mirror = new QTableWidgetItem(remembered.value(names[row]));
		mirror->setData(Qt::AccessibleDescriptionRole, Text::tr("Type the exact name of the joint that mirrors %1.").arg(names[row]));
		m_table->setItem(row, 1, mirror);
	}
	layout->addWidget(m_table, 1);
	m_detect = new QPushButton(Text::tr("&Detect Pairs"));
	m_detect->setObjectName(QStringLiteral("symmetrizeDetectPairs"));
	accessible(m_detect, Text::tr("Detect Pairs"),
		Text::tr("Fill each mirror joint from left and right naming (Left/Right, L/R tokens, a leading L or R). Only existing pairs are "
				 "used; review them before applying."));
	layout->addWidget(m_detect, 0, Qt::AlignLeading);
	m_keep = new QCheckBox(Text::tr("&Keep joint names for unmapped joints"));
	m_keep->setObjectName(QStringLiteral("symmetrizeKeepJointNames"));
	m_keep->setChecked(keepNames);
	accessible(m_keep, m_keep->text().remove(QLatin1Char('&')),
		Text::tr("Bind mirrored vertices to the same off-centre joints as their source. Use this for props or deliberately asymmetric rigs."));
	layout->addWidget(m_keep);
	m_status = new LiveStatusLabel;
	m_status->setObjectName(QStringLiteral("symmetrizeJointsStatus"));
	m_status->setTextFormat(Qt::PlainText);
	m_status->setWordWrap(true);
	accessible(m_status, Text::tr("Joint map status"), Text::tr("How many off-centre joints still need a mirror joint."));
	layout->addWidget(m_status);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	auto *apply = buttons->button(QDialogButtonBox::Ok);
	apply->setObjectName(QStringLiteral("symmetrizeJointsApply"));
	apply->setText(Text::tr("&Symmetrize"));
	accessible(apply, Text::tr("Symmetrize"), Text::tr("Run Symmetrize with this joint map as one undoable edit."));
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(m_detect, &QPushButton::clicked, this, [this] { detectPairs(); });
	connect(m_table, &QTableWidget::itemChanged, this, [this] { refreshStatus(); });
	connect(m_keep, &QCheckBox::toggled, this, [this] { refreshStatus(); });
	fitDialogToScreen(this, QSize(560, 460));
	refreshStatus();
}

QMap<QString, QString> ModelSymmetrizeJointsDialog::mirrorJoints() const
{
	QMap<QString, QString> map;
	for (int row = 0; row < m_table->rowCount(); ++row)
	{
		const auto mirror = m_table->item(row, 1)->text().trimmed();
		if (!mirror.isEmpty())
			map.insert(m_table->item(row, 0)->text(), mirror);
	}
	return map;
}

bool ModelSymmetrizeJointsDialog::keepJointNames() const
{
	return m_keep->isChecked();
}

void ModelSymmetrizeJointsDialog::detectPairs()
{
	// Only pairs on opposite sides of this plane are proposed.
	const auto pairs = modelMirrorJointPairsForAxis(m_rig, detectModelMirrorJointPairs(m_rig.skeleton), m_axis, -1, m_tolerance);
	{
		const QSignalBlocker blocker(m_table);
		for (int row = 0; row < m_table->rowCount(); ++row)
		{
			const auto joint = m_table->item(row, 0)->text();
			if (pairs.contains(joint))
				m_table->item(row, 1)->setText(pairs.value(joint));
		}
	}
	refreshStatus();
}

void ModelSymmetrizeJointsDialog::refreshStatus()
{
	int missing = 0;
	{
		// Screen readers announce each mirror cell by this name, so it carries
		// the cell's value.
		const QSignalBlocker blocker(m_table);
		for (int row = 0; row < m_table->rowCount(); ++row)
		{
			const auto joint = m_table->item(row, 0)->text(), mirror = m_table->item(row, 1)->text().trimmed();
			missing += mirror.isEmpty();
			m_table->item(row, 1)->setData(Qt::AccessibleTextRole, mirror.isEmpty() ? Text::tr("%1 has no mirror joint").arg(joint)
																					: Text::tr("%1 mirrors to %2").arg(joint, mirror));
		}
	}
	if (m_table->rowCount() == 0)
		m_status->setText(Text::tr("Every joint that weights this surface lies on the mirror plane."));
	else if (missing == 0)
		m_status->setText(Text::tr("Every off-centre joint has a mirror joint."));
	else if (m_keep->isChecked())
		m_status->setText(Text::tr("%n off-centre joint(s) keep their names on the mirrored half.", nullptr, missing));
	else
		m_status->setText(Text::tr("%n off-centre joint(s) have no mirror joint. Detect Pairs, enter them, or keep joint names.", nullptr, missing));
}
} // namespace vibestudio
