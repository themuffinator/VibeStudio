// The Skeleton panel: joints, skeletal clips and the files they came from,
// rebuilding the animation from the skeleton after editing the bind pose,
// and the skeletal exports (MD5 and IQM).
#include "app/model_editor_dialog.h"
#include "app/locale_number.h"

#include "app/model_editor_tools.h"
#include "app/model_rig_dialog.h"
#include "app/model_skeletal_dialog.h"
#include "app/model_skeletal_working_set_dialog.h"
#include "app/model_ik_dialog.h"
#include "app/model_animation_curves_dialog.h"
#include "app/model_curve_keys_dialog.h"
#include "app/model_curve_transfer_dialog.h"
#include "app/model_skinning_dialog.h"
#include "app/model_weight_paint_dialog.h"
#include "app/model_weight_transfer_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_sidebar.h"
#include "app/wrapping_action_button.h"
#include "app/wrapping_check_box.h"
#include "core/model_skeleton.h"

#include <QCoreApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLocale>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>

#include <algorithm>

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
	m_skeletonSummary = new LiveStatusLabel;
	m_skeletonSummary->setObjectName(QStringLiteral("meshSkeletonSummary"));
	m_skeletonSummary->setWordWrap(true);
	m_skeletonSummary->setTextFormat(Qt::PlainText);
	m_skeletonSummary->setAccessibleName(Text::tr("Skeleton summary"));
	form->addRow(m_skeletonSummary);
	// The working set first as one line and a meter; the dialog holds the
	// per-clip costs and the full report.
	m_workingSetSummary = new QLabel;
	m_workingSetSummary->setObjectName(QStringLiteral("meshSkeletalWorkingSet"));
	m_workingSetSummary->setWordWrap(true);
	m_workingSetSummary->setTextFormat(Qt::PlainText);
	m_workingSetSummary->setAccessibleName(Text::tr("Skeletal working set"));
	form->addRow(m_workingSetSummary);
	m_workingSetBudget = new QProgressBar;
	m_workingSetBudget->setObjectName(QStringLiteral("meshSkeletalBudget"));
	m_workingSetBudget->setRange(0, 1000);
	m_workingSetBudget->setTextVisible(true);
	m_workingSetBudget->setAccessibleName(Text::tr("Frame-vertex budget"));
	form->addRow(m_workingSetBudget);
	m_workingSetButton = new QPushButton(Text::tr("Working Set…"));
	m_workingSetButton->setObjectName(QStringLiteral("editMeshSkeletalWorkingSet"));
	m_workingSetButton->setAccessibleName(Text::tr("Choose working set"));
	m_workingSetButton->setAccessibleDescription(Text::tr("Choose which skeletal clips are baked into editable frames, within the frame "
															"budget. Clips outside it stay in the skeleton and in exports."));
	m_workingSetButton->setToolTip(m_workingSetButton->accessibleDescription());
	connect(m_workingSetButton, &QPushButton::clicked, this, [this] {
		if (m_working) return;
		m_preview->finishEditTransform(false); m_preview->pause();
		const auto revision = m_document.revisionFingerprint();
		ModelSkeletalWorkingSetDialog dialog(m_document.mesh(), this);
		if (dialog.exec() != QDialog::Accepted) return;
		if (revision != m_document.revisionFingerprint()) {
			m_status->setText(Text::tr("The model changed while choosing the working set. Reopen Working Set.")); return;
		}
		auto edit = dialog.edit();
		edit.selection = m_document.selection();
		QString error;
		const bool noticed = m_historyNoticeShown;
		if (!applyEdit(edit, &error)) { m_status->setText(error); return; }
		// The confirmation replaces the status, so it carries the notice that
		// history discarded steps, which only shows once.
		auto text = Text::tr("Re-baked the frames: %1.").arg(modelSkeletalWorkingSetSummary(modelSkeletalWorkingSet(m_document.mesh())));
		if (!noticed && m_historyNoticeShown) text += QLatin1Char(' ') + historyTrimNotice();
		m_status->setText(text);
	});
	form->addRow(m_workingSetButton);
	m_showJoints = new WrappingCheckBox(Text::tr("Show joints"));
	m_showJoints->setObjectName(QStringLiteral("meshShowJoints"));
	m_showJoints->setAccessibleName(m_showJoints->text());
	m_showJoints->setAccessibleDescription(Text::tr("Display the retained skeletal pose over the model. Joint selection keeps this enabled."));
	m_showJoints->setToolTip(m_showJoints->accessibleDescription());
	m_showJoints->setChecked(true);
	form->addRow(m_showJoints);
	m_preview->setShowJoints(true);
	m_xrayJoints = new WrappingCheckBox(Text::tr("X-ray joints"));
	m_xrayJoints->setObjectName(QStringLiteral("meshXrayJoints"));
	m_xrayJoints->setAccessibleName(m_xrayJoints->text());
	m_xrayJoints->setChecked(true);
	m_xrayJoints->setAccessibleDescription(Text::tr("Show and select joints through the mesh. Wireframe always shows every joint."));
	m_xrayJoints->setToolTip(m_xrayJoints->accessibleDescription());
	form->addRow(m_xrayJoints);
	m_jointDescendants = new WrappingCheckBox(Text::tr("Transform children"));
	m_jointDescendants->setObjectName(QStringLiteral("meshJointDescendants"));
	m_jointDescendants->setAccessibleName(m_jointDescendants->text());
	m_jointDescendants->setChecked(true);
	m_jointDescendants->setAccessibleDescription(Text::tr("Move descendants with the selected joint. Turn off to keep children in model space."));
	m_jointDescendants->setToolTip(m_jointDescendants->accessibleDescription());
	form->addRow(m_jointDescendants);
	connect(m_showJoints, &QCheckBox::toggled, this, [this](bool enabled) { m_preview->setShowJoints(enabled); refreshSelection(); syncPanes(); });
	connect(m_xrayJoints, &QCheckBox::toggled, this, [this](bool enabled) { m_preview->setXrayJoints(enabled); syncPanes(); });
	connect(m_jointDescendants, &QCheckBox::toggled, this, [this](bool enabled) { m_preview->setJointDescendants(enabled); refreshSelection(); syncPanes(); });
	connect(m_preview, &ModelViewport::jointPicked, this, [this](const QString &name, int pick) {
		if (m_selectionMode->currentIndex() == 6) selectJoint(name, pick == int(ModelViewportPick::Toggle) || pick == int(ModelViewportPick::FaceToggle));
	});
	connect(m_preview, &ModelViewport::jointPreviewRejected, this, [this](const QString &message) { m_status->setText(message); });
	m_skeletonTree = new QTreeWidget;
	m_skeletonTree->setObjectName(QStringLiteral("meshSkeletonJoints"));
	m_skeletonTree->setAccessibleName(Text::tr("Joints"));
	m_skeletonTree->setAccessibleDescription(Text::tr("Choose a joint to select it in the viewport. Geometry provides numeric move, rotate, scale, axes and pivot controls. Edits affect the bind pose or the current skeletal sample."));
	m_skeletonTree->setHeaderLabels({Text::tr("Joint"), Text::tr("Influences")});
	m_skeletonTree->header()->setStretchLastSection(false);
	m_skeletonTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_skeletonTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	m_skeletonTree->setMinimumHeight(160);
	form->addRow(m_skeletonTree);
	connect(m_skeletonTree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
		if (item) selectJoint(item->text(0));
	});
	auto *editRig = new WrappingActionButton(Text::tr("Edit Skeleton…"), 2);
	editRig->setObjectName(QStringLiteral("editMeshRig"));
	editRig->setAccessibleName(Text::tr("Edit skeleton"));
	editRig->setAccessibleDescription(Text::tr("Add, rename, reparent or delete unused joints, or assign weights to selected mesh components."));
	editRig->setToolTip(editRig->accessibleDescription());
	connect(editRig, &QPushButton::clicked, this, [this] {
		if (m_working)
			return;
		const auto revision = m_document.revisionFingerprint();
		const auto *item = m_skeletonTree->currentItem();
		ModelRigDialog dialog(m_document.mesh(), m_document.selection(), item ? item->text(0) : QString(), this);
		if (dialog.exec() != QDialog::Accepted)
			return;
		if (revision != m_document.revisionFingerprint())
		{
			m_status->setText(Text::tr("The model changed while reviewing its skeleton. Reopen Edit Skeleton."));
			return;
		}
		QString error;
		if (!applyEdit(dialog.edit(), &error))
			m_status->setText(error);
	});
	form->addRow(editRig);
	auto *skinning = new WrappingActionButton(Text::tr("Skin Weights and Binding…"), 2);
	skinning->setObjectName(QStringLiteral("editMeshSkinning"));
	skinning->setAccessibleName(Text::tr("Skin weights and binding"));
	skinning->setAccessibleDescription(Text::tr("Inspect and select influenced vertices, review rigid or automatic binding, repair offsets, reduce weights or unbind surfaces."));
	skinning->setToolTip(skinning->accessibleDescription());
	connect(skinning, &QPushButton::clicked, this, [this] {
		if (m_working) return;
		m_preview->finishEditTransform(false); m_preview->pause();
		const auto revision = m_document.revisionFingerprint();
		ModelSkinningDialog dialog(m_document.mesh(), m_document.selection(), m_document.selection().joint, m_preview->frame(), this);
		if (dialog.exec() != QDialog::Accepted) return;
		if (revision != m_document.revisionFingerprint()) {
			m_status->setText(Text::tr("The model changed while reviewing skinning. Reopen Skin Weights and Binding.")); return;
		}
		if (dialog.choosingSelection()) {
			m_document.setSelection(dialog.chosenSelection());
			{ QSignalBlocker blocker(m_selectionMode); m_selectionMode->setCurrentIndex(1); }
			refresh(); return;
		}
		QString error;
		if (!applyEdit(dialog.edit(), &error)) m_status->setText(error);
	});
	form->addRow(skinning);
	auto *paintWeights = new WrappingActionButton(Text::tr("Paint Skin Weights…"), 2);
	paintWeights->setObjectName(QStringLiteral("paintMeshSkinWeights"));
	paintWeights->setAccessibleName(Text::tr("Paint skin weights"));
	paintWeights->setAccessibleDescription(Text::tr("Stage weight brush strokes with joint locks, smoothing, a vertex mask and exact numeric controls, then review animation poses before applying."));
	paintWeights->setToolTip(paintWeights->accessibleDescription());
	connect(paintWeights, &QPushButton::clicked, this, [this] {
		if (m_working) return;
		m_preview->finishEditTransform(false); m_preview->pause();
		const auto revision = m_document.revisionFingerprint();
		const auto selection = m_document.selection();
		ModelWeightPaintDialog dialog(m_document.mesh(), m_document.selection(), m_document.selection().joint, m_preview->frame(), this);
		if (auto *preview = dialog.findChild<ModelViewport *>(QStringLiteral("weightPaintPreview")))
			preview->setCameraControls(m_preview->cameraControls());
		if (dialog.exec() != QDialog::Accepted) return;
		if (revision != m_document.revisionFingerprint()) {
			m_status->setText(Text::tr("The model changed while painting weights. Reopen Paint Skin Weights.")); return;
		}
		QString error;
		if (!applyEdit(dialog.edit(), &error)) m_status->setText(error);
		else { m_document.setSelection(selection); refresh(); }
	});
	form->addRow(paintWeights);
	auto *transferWeights = new WrappingActionButton(Text::tr("Transfer Skin Weights…"), 2);
	transferWeights->setObjectName(QStringLiteral("transferMeshSkinWeights"));
	transferWeights->setAccessibleName(Text::tr("Transfer skin weights"));
	transferWeights->setAccessibleDescription(Text::tr("Review mirrored or spatial weight transfer, joint mapping, locks and posed deformation before applying."));
	transferWeights->setToolTip(transferWeights->accessibleDescription());
	connect(transferWeights, &QPushButton::clicked, this, [this] {
		if (m_working) return;
		m_preview->finishEditTransform(false); m_preview->pause();
		const auto revision = m_document.revisionFingerprint();
		const auto selection = m_document.selection();
		ModelWeightTransferDialog dialog(m_document.mesh(), selection, m_preview->frame(), this);
		if (auto *preview = dialog.findChild<ModelViewport *>(QStringLiteral("transferPreview")))
			preview->setCameraControls(m_preview->cameraControls());
		if (dialog.exec() != QDialog::Accepted) return;
		if (revision != m_document.revisionFingerprint()) {
			m_status->setText(Text::tr("The model changed while reviewing weight transfer. Reopen Transfer Skin Weights.")); return;
		}
		QString error;
		if (!applyEdit(dialog.edit(), &error)) m_status->setText(error);
		else { m_document.setSelection(selection); refresh(); }
	});
	form->addRow(transferWeights);
	auto *ik = new WrappingActionButton(Text::tr("Two-Bone Inverse Kinematics…"), 2);
	ik->setObjectName(QStringLiteral("editMeshIk"));
	ik->setAccessibleName(Text::tr("Two-bone inverse kinematics"));
	ik->setAccessibleDescription(Text::tr("Place a hand or foot at a model-space target with an explicit bend pole. Review selected clip samples before applying."));
	ik->setToolTip(ik->accessibleDescription());
	connect(ik, &QPushButton::clicked, this, [this] {
		if (m_working) return;
		m_preview->finishEditTransform(false); m_preview->pause();
		const auto revision = m_document.revisionFingerprint();
		const auto selection = m_document.selection();
		ModelIkDialog dialog(m_document.mesh(), selection.joint, m_preview->frame(), this);
		if (auto *preview = dialog.findChild<ModelViewport *>(QStringLiteral("ikPreview")))
			preview->setCameraControls(m_preview->cameraControls());
		if (dialog.exec() != QDialog::Accepted) return;
		if (revision != m_document.revisionFingerprint()) {
			m_status->setText(Text::tr("The model changed while reviewing inverse kinematics. Reopen Two-Bone Inverse Kinematics.")); return;
		}
		QString error;
		if (!applyEdit(dialog.edit(), &error)) m_status->setText(error);
		else { m_document.setSelection(selection); refresh(); }
	});
	form->addRow(ik);
	auto *curves = new WrappingActionButton(Text::tr("Animation Curves…"), 2);
	curves->setObjectName(QStringLiteral("editMeshAnimationCurves"));
	curves->setAccessibleName(Text::tr("Animation curves"));
	curves->setAccessibleDescription(Text::tr("Capture sampled joint motion, edit translation, rotation and scale keys and tangents, then review before applying."));
	curves->setToolTip(curves->accessibleDescription());
	connect(curves, &QPushButton::clicked, this, [this] {
		if (m_working) return;
		m_preview->finishEditTransform(false); m_preview->pause();
		const auto revision = m_document.revisionFingerprint();
		const auto selection = m_document.selection();
		ModelAnimationCurvesDialog dialog(m_document.mesh(), selection.joint, m_preview->frame(), this);
		if (auto *preview = dialog.findChild<ModelViewport *>(QStringLiteral("curvesPreview")))
			preview->setCameraControls(m_preview->cameraControls());
		if (dialog.exec() != QDialog::Accepted) return;
		if (revision != m_document.revisionFingerprint()) {
			m_status->setText(Text::tr("The model changed while reviewing animation curves. Reopen Animation Curves.")); return;
		}
		QString error;
		if (!applyEdit(dialog.edit(), &error)) m_status->setText(error);
		else { m_document.setSelection(selection); refresh(); }
	});
	form->addRow(curves);
	auto *keys = new WrappingActionButton(Text::tr("Edit Animation Keys…"), 2);
	keys->setObjectName(QStringLiteral("editMeshAnimationKeys"));
	keys->setAccessibleName(Text::tr("Edit animation keys"));
	keys->setAccessibleDescription(Text::tr("Select keys across joint channels, retime, duplicate, remove or reduce them, then review motion before applying."));
	keys->setToolTip(keys->accessibleDescription());
	connect(keys, &QPushButton::clicked, this, [this] {
		if (m_working) return;
		m_preview->finishEditTransform(false); m_preview->pause();
		const auto revision = m_document.revisionFingerprint();
		const auto selection = m_document.selection();
		ModelCurveKeysDialog dialog(m_document.mesh(), selection.joint, m_preview->frame(), this);
		if (auto *preview = dialog.findChild<ModelViewport *>(QStringLiteral("curveKeysPreview")))
			preview->setCameraControls(m_preview->cameraControls());
		if (dialog.exec() != QDialog::Accepted) return;
		if (revision != m_document.revisionFingerprint()) {
			m_status->setText(Text::tr("The model changed while reviewing keys. Reopen Edit Animation Keys.")); return;
		}
		QString error;
		if (!applyEdit(dialog.edit(), &error)) m_status->setText(error);
		else { m_document.setSelection(selection); refresh(); }
	});
	form->addRow(keys);
	auto *transfer = new WrappingActionButton(Text::tr("Transfer Animation Range…"), 2);
	transfer->setObjectName(QStringLiteral("editMeshCurveTransfer"));
	transfer->setAccessibleName(Text::tr("Transfer animation range"));
	transfer->setAccessibleDescription(Text::tr("Copy or reverse a joint-motion range between clips of this rig, then review the destination before applying."));
	transfer->setToolTip(transfer->accessibleDescription());
	connect(transfer, &QPushButton::clicked, this, [this] {
		if (m_working) return;
		m_preview->finishEditTransform(false); m_preview->pause();
		const auto revision = m_document.revisionFingerprint();
		const auto selection = m_document.selection();
		ModelCurveTransferDialog dialog(m_document.mesh(), selection.joint, m_preview->frame(), this);
		if (auto *preview = dialog.findChild<ModelViewport *>(QStringLiteral("curveTransferPreview")))
			preview->setCameraControls(m_preview->cameraControls());
		if (dialog.exec() != QDialog::Accepted) return;
		if (revision != m_document.revisionFingerprint()) {
			m_status->setText(Text::tr("The model changed while reviewing animation transfer. Reopen Transfer Animation Range.")); return;
		}
		QString error;
		if (!applyEdit(dialog.edit(), &error)) m_status->setText(error);
		else { m_document.setSelection(selection); refresh(); }
	});
	form->addRow(transfer);
	auto *editPoses = new WrappingActionButton(Text::tr("Edit Skeletal Animation…"), 2);
	editPoses->setObjectName(QStringLiteral("editMeshSkeletalAnimation"));
	editPoses->setAccessibleName(Text::tr("Edit skeletal animation"));
	editPoses->setAccessibleDescription(Text::tr("Position rest joints, manage skeletal clips, transform poses and interpolate animation."));
	editPoses->setToolTip(editPoses->accessibleDescription());
	connect(editPoses, &QPushButton::clicked, this, [this] {
		if (m_working) return;
		const auto revision = m_document.revisionFingerprint();
		const auto *item = m_skeletonTree->currentItem();
		ModelSkeletalDialog dialog(m_document.mesh(), m_document.selection(), item ? item->text(0) : QString(), m_preview->frame(), this);
		if (dialog.exec() != QDialog::Accepted) return;
		if (revision != m_document.revisionFingerprint())
		{
			m_status->setText(Text::tr("The model changed while reviewing its animation. Reopen Edit Skeletal Animation."));
			return;
		}
		QString error;
		if (!applyEdit(dialog.edit(), &error)) m_status->setText(error);
	});
	form->addRow(editPoses);
	m_exportMd5 = new WrappingActionButton(Text::tr("Export MD5 Mesh…"), 2);
	m_exportMd5->setObjectName(QStringLiteral("exportMeshMd5"));
	m_exportMd5->setToolTip(Text::tr("Write a Doom 3, Quake 4 or Prey md5mesh; a model without joints gets one origin joint."));
	connect(m_exportMd5, &QPushButton::clicked, this, [this] { exportModel(QStringLiteral("md5mesh")); });
	form->addRow(m_exportMd5);
	auto *exportAnim = new WrappingActionButton(Text::tr("Export MD5 Animation of Clip…"), 2);
	exportAnim->setObjectName(QStringLiteral("exportMeshMd5Anim"));
	exportAnim->setToolTip(Text::tr("Choose a skeletal clip and its whole frame rate, then write it as an md5anim."));
	connect(exportAnim, &QPushButton::clicked, this, [this] { exportModel(QStringLiteral("md5anim")); });
	form->addRow(exportAnim);
	auto *exportAnims = new WrappingActionButton(Text::tr("Export All Clips as MD5…"), 2);
	exportAnims->setObjectName(QStringLiteral("exportMeshMd5AnimAll"));
	exportAnims->setToolTip(Text::tr("Write every skeletal clip as its own md5anim, named after the clip, into a folder."));
	connect(exportAnims, &QPushButton::clicked, this, [this] { exportMd5Animation(true); });
	form->addRow(exportAnims);
	m_exportIqm = new WrappingActionButton(Text::tr("Export IQM…"), 2);
	m_exportIqm->setObjectName(QStringLiteral("exportMeshIqm"));
	m_exportIqm->setToolTip(Text::tr("Write an Inter-Quake Model with joints, weights and every skeletal clip, for ioquake3 and its descendants."));
	connect(m_exportIqm, &QPushButton::clicked, this, [this] { exportModel(QStringLiteral("iqm")); });
	form->addRow(m_exportIqm);
}

void ModelEditorDialog::refreshSkeleton()
{
	if (!m_skeletonTree)
		return;
	QSignalBlocker blocker(m_skeletonTree);
	const bool hadItems = m_skeletonTree->topLevelItemCount() > 0;
	QSet<QString> expanded;
	for (QTreeWidgetItemIterator it(m_skeletonTree); *it; ++it)
		if ((*it)->isExpanded()) expanded.insert((*it)->text(0));
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
		for (QWidget *widget : {static_cast<QWidget *>(m_workingSetSummary), static_cast<QWidget *>(m_workingSetBudget), static_cast<QWidget *>(m_workingSetButton)})
			widget->setVisible(false);
		return;
	}
	const auto workingSet = modelSkeletalWorkingSet(mesh);
	m_workingSetSummary->setText(modelSkeletalWorkingSetSummary(workingSet));
	const int permille = workingSet.budget.frameVertices > 0
		? int(std::min<qint64>(1000, workingSet.frameVertices * 1000 / workingSet.budget.frameVertices)) : 0;
	m_workingSetBudget->setValue(permille);
	const auto meter = Text::tr("Frame vertices: %1 of %2").arg(QLocale().toString(workingSet.frameVertices), QLocale().toString(workingSet.budget.frameVertices));
	m_workingSetBudget->setFormat(meter);
	m_workingSetBudget->setAccessibleDescription(meter);
	m_workingSetBudget->setToolTip(meter);
	for (QWidget *widget : {static_cast<QWidget *>(m_workingSetSummary), static_cast<QWidget *>(m_workingSetBudget), static_cast<QWidget *>(m_workingSetButton)})
		widget->setVisible(true);
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
		clips << Text::tr("%1 (%Ln frame(s))", nullptr, int(clip.frames.size())).arg(clip.name);
	QString summary = Text::tr("%Ln joint(s) from %1. Skeletal clips: %2.", nullptr, int(skeleton.joints.size()))
						  .arg(skeleton.sourceFormat.isEmpty() ? mesh.formatName : skeleton.sourceFormat,
							   clips.isEmpty() ? Text::tr("none") : clips.join(QStringLiteral(", ")));
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
		item->setToolTip(0, Text::tr("%1 at %2").arg(joint.name, localeNumberList({origin.x, origin.y, origin.z})));
		item->setData(0, Qt::UserRole, index);
		items[index] = item;
		item->setExpanded(hadItems ? expanded.contains(joint.name) : true);
		if (joint.name == m_document.selection().joint) m_skeletonTree->setCurrentItem(item);
	}
	if (!hadItems) m_skeletonTree->expandToDepth(2);
}

} // namespace vibestudio
