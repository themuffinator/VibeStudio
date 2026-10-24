#include "app/model_editor_dialog.h"
#include "app/model_viewport.h"
#include "core/model_joint_tools.h"
#include "core/model_skeleton.h"
#include "core/model_transform_axes.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QLabel>
#include <QSignalBlocker>
#include <QTreeWidget>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelEditor)
};
} // namespace
void ModelEditorDialog::selectJoint(const QString &name, bool toggle)
{
	if (m_refreshing || m_working || (name.isEmpty() && toggle))
		return;
	m_preview->finishEditTransform(false);
	m_preview->pause();
	ModelSelection selected;
	selected.surface = m_document.selection().surface;
	selected.joint = toggle && m_document.selection().joint == name ? QString{} : name;
	{
		QSignalBlocker blocker(m_selectionMode);
		m_selectionMode->setCurrentIndex(6);
	}
	m_document.setSelection(selected);
	refresh();
}
void ModelEditorDialog::refreshJointSelection()
{
	if (!m_skeletonTree || !m_showJoints)
		return;
	const bool mode = m_selectionMode->currentIndex() == 6;
	m_preview->setJointPicking(mode);
	m_preview->setEditJoint(m_document.selection().joint);
	if (mode)
		m_showJoints->setChecked(true);
	m_showJoints->setEnabled(!mode && !m_document.mesh().skeleton.isEmpty());
	m_xrayJoints->setEnabled(m_showJoints->isChecked() && !m_document.mesh().skeleton.isEmpty());
	m_jointDescendants->setEnabled(!m_document.mesh().skeleton.isEmpty());
	m_frameScope->setEnabled(!mode);
	QSignalBlocker blocker(m_skeletonTree);
	QTreeWidgetItem *current = nullptr;
	for (QTreeWidgetItemIterator it(m_skeletonTree); *it; ++it)
		if ((*it)->text(0) == m_document.selection().joint)
		{
			current = *it;
			break;
		}
	m_skeletonTree->setCurrentItem(current);
	if (!current)
		m_skeletonTree->clearSelection();
	if (m_document.selection().joint.isEmpty())
		return;
	ModelJointFrame frame;
	QString error;
	if (!modelJointFrame(m_document.mesh(), m_preview->frame(), &frame, &error))
	{
		m_status->setText(error);
		return;
	}
	const auto scope = frame.clip < 0 ? Text::tr("bind pose · mesh and animation preserved")
									  : Text::tr("clip %1, pose %2 only").arg(frame.clip).arg(frame.local);
	m_status->setText(Text::tr("Joint %1 · %2 · %3")
						  .arg(m_document.selection().joint, scope,
							   m_jointDescendants->isChecked() ? Text::tr("children follow") : Text::tr("children stay")));
}
void ModelEditorDialog::applyJointTransform(const ModelTransform &transform)
{
	if (m_working || m_document.selection().joint.isEmpty())
		return;
	ModelJointFrame frame;
	ModelEdit edit;
	QString error;
	if (!modelJointFrame(m_document.mesh(), m_preview->frame(), &frame, &error) ||
		!modelJointTransformDelta(transform, &edit.skeletal.delta, &error))
	{
		m_status->setText(error);
		return;
	}
	edit.selection = m_document.selection();
	edit.kind = frame.clip < 0 ? ModelEditKind::TransformBindJoint : ModelEditKind::TransformSkeletalPose;
	edit.skeletal.joint = edit.selection.joint;
	edit.skeletal.clip = frame.clip;
	edit.skeletal.first = edit.skeletal.last = frame.local;
	edit.skeletal.descendants = m_jointDescendants->isChecked();
	edit.skeletal.matrixTransform = true;
	if (!applyEdit(edit, &error))
		m_status->setText(error);
}
void ModelEditorDialog::executeJointTransform()
{
	if (m_working)
		return;
	const auto request = operation(ModelEditKind::Transform);
	ModelTransform transform;
	ModelJointFrame frame;
	QString error;
	if (!modelJointFrame(m_document.mesh(), m_preview->frame(), &frame, &error) ||
		!resolveModelTransformAxes(m_document.mesh(), request, &transform.basis, &error))
	{
		m_status->setText(error);
		return;
	}
	const int joint = modelJointIndex(m_document.mesh().skeleton, request.selection.joint, Qt::CaseSensitive);
	if (joint < 0 ||
		!modelTransformPivot({modelJointTranslation(frame.pose[joint])}, {0}, request.pivotMode, request.pivot, &transform.pivot) ||
		!snapModelTranslation(request.translation, request.translationGrid, &transform.translation) ||
		!snapModelRotation(request.rotation, request.rotationGrid, &transform.rotation) ||
		!snapModelScale(request.scale, request.scaleGrid, &transform.scale))
	{
		m_status->setText(Text::tr("Choose finite joint transforms and a valid pivot."));
		return;
	}
	applyJointTransform(transform);
}
} // namespace vibestudio
