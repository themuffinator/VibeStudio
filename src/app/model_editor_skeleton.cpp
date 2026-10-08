// The Skeleton panel: joints, skeletal clips and the files they came from,
// rebuilding the animation from the skeleton after editing the bind pose,
// and the skeletal exports (MD5 and IQM).
#include "app/model_editor_dialog.h"

#include "app/model_editor_tools.h"
#include "app/model_viewport.h"
#include "app/studio_sidebar.h"
#include "core/model_skeleton.h"

#include <QCoreApplication>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
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

void ModelEditorDialog::addSkeletonControls(QFormLayout *form)
{
	m_skeletonSummary = new QLabel;
	m_skeletonSummary->setObjectName(QStringLiteral("meshSkeletonSummary"));
	m_skeletonSummary->setWordWrap(true);
	m_skeletonSummary->setTextFormat(Qt::PlainText);
	m_skeletonSummary->setAccessibleName(Text::tr("Skeleton summary"));
	form->addRow(m_skeletonSummary);
	m_skeletonTree = new QTreeWidget;
	m_skeletonTree->setObjectName(QStringLiteral("meshSkeletonJoints"));
	m_skeletonTree->setAccessibleName(Text::tr("Joints"));
	m_skeletonTree->setAccessibleDescription(Text::tr("The joint hierarchy; choose a joint to see where it sits in the bind pose."));
	m_skeletonTree->setHeaderLabels({Text::tr("Joint"), Text::tr("Influences")});
	m_skeletonTree->header()->setStretchLastSection(false);
	m_skeletonTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_skeletonTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	m_skeletonTree->setMinimumHeight(160);
	form->addRow(m_skeletonTree);
	m_exportMd5 = new QPushButton(Text::tr("Export MD5 Mesh…"));
	m_exportMd5->setObjectName(QStringLiteral("exportMeshMd5"));
	m_exportMd5->setToolTip(Text::tr("Write a Doom 3, Quake 4 or Prey md5mesh; a model without joints gets one origin joint."));
	connect(m_exportMd5, &QPushButton::clicked, this, [this] { exportModel(QStringLiteral("md5mesh")); });
	form->addRow(m_exportMd5);
	auto *exportAnim = new QPushButton(Text::tr("Export MD5 Animation of Clip…"));
	exportAnim->setObjectName(QStringLiteral("exportMeshMd5Anim"));
	exportAnim->setToolTip(Text::tr("Write the skeletal clip the current frame belongs to as an md5anim."));
	connect(exportAnim, &QPushButton::clicked, this, [this] { exportModel(QStringLiteral("md5anim")); });
	form->addRow(exportAnim);
	m_exportIqm = new QPushButton(Text::tr("Export IQM…"));
	m_exportIqm->setObjectName(QStringLiteral("exportMeshIqm"));
	m_exportIqm->setToolTip(Text::tr("Write an Inter-Quake Model with joints, weights and every skeletal clip, for ioquake3 and its descendants."));
	connect(m_exportIqm, &QPushButton::clicked, this, [this] { exportModel(QStringLiteral("iqm")); });
	form->addRow(m_exportIqm);
}

void ModelEditorDialog::refreshSkeleton()
{
	if (!m_skeletonTree)
		return;
	const auto &mesh = m_document.mesh();
	const auto &skeleton = mesh.skeleton;
	if (auto *page = m_sidebarPages.value(QStringLiteral("skeleton")))
		page->setBadge(skeleton.isEmpty() ? QString() : QString::number(skeleton.joints.size()));
	if (skeleton.isEmpty())
	{
		m_skeletonSummary->setText(Text::tr("This model animates by frames and has no joints. MD5 export gives it one origin joint; "
											"IQM export writes it as a static mesh."));
		m_skeletonTree->clear();
		m_skeletonTree->setEnabled(false);
		return;
	}
	QVector<int> influences(skeleton.joints.size(), 0);
	for (const auto &surface : mesh.surfaces)
	{
		for (const auto &influence : surface.skinning.influences)
		{
			if (influence.joint >= 0 && influence.joint < influences.size())
				++influences[influence.joint];
		}
	}
	QStringList clips;
	for (const auto &clip : skeleton.clips)
		clips << Text::tr("%1 (%2 frames)").arg(clip.name).arg(clip.frames.size());
	QString summary = Text::tr("%1 joints from %2. Skeletal clips: %3.")
						  .arg(skeleton.joints.size())
						  .arg(skeleton.sourceFormat.isEmpty() ? mesh.formatName : skeleton.sourceFormat)
						  .arg(clips.isEmpty() ? Text::tr("none") : clips.join(QStringLiteral(", ")));
	if (!mesh.companionPaths.isEmpty())
		summary += QLatin1Char(' ') + Text::tr("Read with: %1.").arg(mesh.companionPaths.join(QStringLiteral(", ")));
	m_skeletonSummary->setText(summary);
	m_skeletonTree->setEnabled(true);
	m_skeletonTree->clear();
	QVector<QTreeWidgetItem *> items(skeleton.joints.size(), nullptr);
	for (int index = 0; index < skeleton.joints.size(); ++index)
	{
		const auto &joint = skeleton.joints[index];
		auto *item = joint.parent >= 0 && joint.parent < index && items[joint.parent] ? new QTreeWidgetItem(items[joint.parent])
																					 : new QTreeWidgetItem(m_skeletonTree);
		item->setText(0, joint.name);
		item->setText(1, QString::number(influences[index]));
		const auto origin = modelJointTranslation(joint.bind);
		item->setToolTip(0, Text::tr("%1 at %2, %3, %4").arg(joint.name).arg(origin.x, 0, 'g', 6).arg(origin.y, 0, 'g', 6).arg(origin.z, 0, 'g', 6));
		item->setData(0, Qt::UserRole, index);
		items[index] = item;
	}
	m_skeletonTree->expandToDepth(2);
}

} // namespace vibestudio
