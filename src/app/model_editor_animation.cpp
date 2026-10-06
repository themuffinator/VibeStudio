#include "app/model_editor_dialog.h"

#include "app/model_viewport.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QSpinBox>

#include <algorithm>

namespace vibestudio
{
void ModelEditorDialog::addAnimationControls(QFormLayout *animation)
{
	auto *group = new QGroupBox(QCoreApplication::translate("VibeStudioModelEditor", "Animation clips"));
	group->setObjectName(QStringLiteral("meshAnimationClips"));
	auto *form = new QFormLayout(group);
	form->setContentsMargins(4, 6, 4, 6);
	form->setRowWrapPolicy(QFormLayout::WrapAllRows);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	const auto spin = [&](const char *id, const QString &label, const QString &description, int minimum, int maximum)
	{
		auto *field = new QSpinBox;
		field->setObjectName(QString::fromLatin1(id));
		field->setRange(minimum, maximum);
		field->setLayoutDirection(Qt::LeftToRight);
		field->setMinimumWidth(0);
		field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		field->setAccessibleName(label);
		field->setAccessibleDescription(description);
		field->setToolTip(description);
		form->addRow(label, field);
		return field;
	};
	const auto text = [&](const char *id, const QString &label, const QString &description, int maximum)
	{
		auto *field = new QLineEdit;
		field->setObjectName(QString::fromLatin1(id));
		field->setMaxLength(maximum);
		field->setMinimumWidth(0);
		field->setAccessibleName(label);
		field->setAccessibleDescription(description);
		field->setToolTip(description);
		form->addRow(label, field);
		return field;
	};
	const auto button = [&](const char *id, const QString &label, const QString &description, ModelEditKind kind)
	{
		auto *control = new QPushButton(label);
		control->setObjectName(QString::fromLatin1(id));
		control->setAccessibleName(label);
		control->setAccessibleDescription(description);
		control->setToolTip(description);
		form->addRow(control);
		connect(control, &QPushButton::clicked, this, [this, kind] { executeAnimation(kind); });
		return control;
	};
	m_animationClip = new QComboBox;
	m_animationClip->setObjectName(QStringLiteral("meshAnimationClip"));
	m_animationClip->setMinimumContentsLength(12);
	m_animationClip->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_animationClip->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Animation clip"));
	m_animationClip->setToolTip(QCoreApplication::translate("VibeStudioModelEditor",
															"Choose a saved frame range for editing and preview. Indices distinguish "
															"imported clips with the same name; All frames previews the whole model."));
	m_animationClip->setAccessibleDescription(m_animationClip->toolTip());
	m_animationClip->addItem(QCoreApplication::translate("VibeStudioModelEditor", "All frames"), -1);
	form->addRow(QCoreApplication::translate("VibeStudioModelEditor", "Clip"), m_animationClip);
	m_animationSummary = new QLabel;
	m_animationSummary->setObjectName(QStringLiteral("meshAnimationSummary"));
	m_animationSummary->setTextFormat(Qt::PlainText);
	m_animationSummary->setWordWrap(true);
	m_animationSummary->setMinimumWidth(0);
	m_animationSummary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_animationSummary->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Animation preview status"));
	form->addRow(m_animationSummary);
	const auto rate = [&](const char *id, const QString &label, const QString &description, double minimum)
	{
		auto *field = new QDoubleSpinBox;
		field->setObjectName(QString::fromLatin1(id));
		field->setDecimals(6);
		field->setRange(minimum, 1000);
		field->setKeyboardTracking(false);
		field->setLayoutDirection(Qt::LeftToRight);
		field->setMinimumWidth(0);
		field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		field->setAccessibleName(label);
		field->setAccessibleDescription(description);
		field->setToolTip(description);
		form->addRow(label, field);
		return field;
	};
	m_animationRate = rate("meshAnimationRate", QCoreApplication::translate("VibeStudioModelEditor", "Preview FPS"),
		QCoreApplication::translate("VibeStudioModelEditor",
			"Session preview speed. Selecting a clip uses its saved FPS when specified. Apply Clip FPS to save timing; game exports require separate animation configuration. Reduced motion disables automatic playback."), .001);
	m_animationRate->setValue(m_preview->framesPerSecond());
	auto *smooth = new QCheckBox(QCoreApplication::translate("VibeStudioModelEditor", "Smooth preview"));
	smooth->setObjectName(QStringLiteral("meshAnimationSmooth"));
	smooth->setAccessibleName(smooth->text());
	smooth->setToolTip(QCoreApplication::translate(
		"VibeStudioModelEditor",
		"Blend positions, normal directions and rigid attachments between stored frames, including the end-to-start loop. "
		"Pause to return to the current stored pose for editing. This session-only preview does not insert or export poses."));
	smooth->setAccessibleDescription(smooth->toolTip());
	smooth->setChecked(true);
	m_preview->setAnimationInterpolation(true);
	form->addRow(smooth);
	connect(smooth, &QCheckBox::toggled, this,
			[this](bool enabled)
			{
				m_preview->setAnimationInterpolation(enabled);
				refreshAnimationControls();
			});
	m_animationName = text("meshAnimationName", QCoreApplication::translate("VibeStudioModelEditor", "Clip name"),
						   QCoreApplication::translate("VibeStudioModelEditor",
													   "A unique name for a new or renamed clip. Clip names and ranges are saved in the "
													   "editable source; game exports require their own animation configuration."),
						   128);
	m_animationFirst =
		spin("meshAnimationFirst", QCoreApplication::translate("VibeStudioModelEditor", "First frame"),
			 QCoreApplication::translate("VibeStudioModelEditor", "First included frame, using the transport's zero-based frame index."), 0,
			 1023);
	m_animationLast =
		spin("meshAnimationLast", QCoreApplication::translate("VibeStudioModelEditor", "Last frame"),
			 QCoreApplication::translate("VibeStudioModelEditor", "Last included frame. Clips may overlap and may contain a single pose."),
			 0, 1023);
	m_clipRate = rate("meshClipRate", QCoreApplication::translate("VibeStudioModelEditor", "Saved clip FPS"),
		QCoreApplication::translate("VibeStudioModelEditor", "Playback rate saved in the editable source. Zero leaves timing unspecified. Native MDL groups retain their independent timing."), 0);
	m_clipRate->setSpecialValueText(QCoreApplication::translate("VibeStudioModelEditor", "Unspecified"));
	m_addAnimation =
		button("addMeshAnimation", QCoreApplication::translate("VibeStudioModelEditor", "Add Clip"),
			   QCoreApplication::translate("VibeStudioModelEditor", "Save a named range without copying or changing any poses."),
			   ModelEditKind::AddAnimation);
	m_animationSelectionButtons << button("setMeshAnimationRate", QCoreApplication::translate("VibeStudioModelEditor", "Apply Clip FPS"),
		QCoreApplication::translate("VibeStudioModelEditor", "Save the selected clip's playback rate in one undo step without resampling any poses."), ModelEditKind::SetAnimationRate);
	m_animationSelectionButtons << button(
		"setMeshAnimationRange", QCoreApplication::translate("VibeStudioModelEditor", "Apply Clip Range"),
		QCoreApplication::translate("VibeStudioModelEditor", "Replace the selected clip's inclusive frame bounds in one undo step."),
		ModelEditKind::SetAnimationRange);
	m_animationSelectionButtons << button(
		"renameMeshAnimation", QCoreApplication::translate("VibeStudioModelEditor", "Rename Clip"),
		QCoreApplication::translate("VibeStudioModelEditor", "Rename the selected clip without renaming its frames."),
		ModelEditKind::RenameAnimation);
	m_animationSelectionButtons << button(
		"deleteMeshAnimation", QCoreApplication::translate("VibeStudioModelEditor", "Delete Clip"),
		QCoreApplication::translate("VibeStudioModelEditor",
									"Remove only the selected clip's name and range. All poses remain in the model."),
		ModelEditKind::DeleteAnimation);
	m_copyPoseSource =
		spin("meshCopyPoseSource", QCoreApplication::translate("VibeStudioModelEditor", "Copy from frame"),
			 QCoreApplication::translate("VibeStudioModelEditor",
										 "Source pose to copy into the displayed frame, across every surface and attachment."),
			 0, 1023);
	m_copyFramePose = button("copyMeshFramePose", QCoreApplication::translate("VibeStudioModelEditor", "Copy Full Pose"),
							 QCoreApplication::translate("VibeStudioModelEditor",
														 "Replace the displayed pose's geometry, normals, origin and attachments. Keep "
														 "its frame name, UVs, materials, clip ranges and other poses."),
							 ModelEditKind::CopyFramePose);
	m_inbetweenCount =
		spin("meshInbetweenCount", QCoreApplication::translate("VibeStudioModelEditor", "Frames to insert"),
			 QCoreApplication::translate("VibeStudioModelEditor", "Generate evenly spaced poses between the displayed frame and its next "
																  "frame. Clips expand only when they contain both endpoints."),
			 1, 1022);
	m_inbetweenPrefix = text(
		"meshInbetweenPrefix", QCoreApplication::translate("VibeStudioModelEditor", "Frame-name prefix"),
		QCoreApplication::translate("VibeStudioModelEditor",
									"Generated names add _001, _002 and so on. Game formats enforce their shorter name limits at export."),
		123);
	m_inbetweenPrefix->setText(QStringLiteral("blend"));
	m_insertInbetweens = button(
		"insertMeshInbetweens", QCoreApplication::translate("VibeStudioModelEditor", "Insert In-between Frames"),
		QCoreApplication::translate("VibeStudioModelEditor",
									"Blend geometry, normals and rigid attachment orientations across every surface. Existing poses stay "
									"exact. Invalid generated geometry or incompatible tag handedness rejects the complete operation."),
		ModelEditKind::InsertInbetweens);
	animation->addRow(group);
	connect(m_animationClip, &QComboBox::currentIndexChanged, this, [this] { selectAnimationClip(); });
	connect(m_animationRate, &QDoubleSpinBox::valueChanged, this,
			[this](double rate)
			{
				if (!m_refreshing && !m_working)
				{
					m_preview->setFramesPerSecond(rate);
					refreshAnimationControls();
				}
			});
	connect(m_copyPoseSource, &QSpinBox::valueChanged, this,
			[this]
			{
				if (!m_refreshing && !m_working)
				{
					refreshAnimationControls();
				}
			});
	connect(m_preview, &ModelViewport::animationChanged, this,
			[this]
			{
				if (m_refreshing || m_working)
				{
					return;
				}
				const QSignalBlocker blocked(m_animationClip);
				m_animationClip->setCurrentIndex(m_animationClip->findData(m_preview->animationIndex()));
				refreshAnimationControls();
			});
}

void ModelEditorDialog::refreshAnimationControls()
{
	if (!m_animationClip || m_document.mesh().frames.isEmpty())
	{
		return;
	}
	const auto &mesh = m_document.mesh();
	const auto revision = m_document.revisionFingerprint();
	const QSignalBlocker blocked(m_animationClip);
	if (m_animationRevision != revision)
	{
		const int wanted = m_animationClip->currentData().toInt();
		m_animationClip->clear();
		m_animationClip->addItem(QCoreApplication::translate("VibeStudioModelEditor", "All frames"), -1);
		for (int i = 0; i < mesh.animations.size(); ++i)
		{
			const auto &clip = mesh.animations[i];
			m_animationClip->addItem(QCoreApplication::translate("VibeStudioModelEditor", "%1 · %2 (%3–%4)")
										 .arg(i)
										 .arg(clip.name)
										 .arg(clip.firstFrame)
										 .arg(clip.firstFrame + clip.frameCount - 1),
									 i);
		}
		m_animationClip->setCurrentIndex(std::max(0, m_animationClip->findData(wanted)));
		m_animationRevision = revision;
		m_displayedAnimation = -2;
	}
	const int frame = std::clamp(m_frame->currentIndex(), 0, int(mesh.frames.size()) - 1);
	const int index = m_animationClip->currentData().toInt();
	for (auto *field : {m_animationFirst, m_animationLast, m_copyPoseSource})
	{
		field->setMaximum(int(mesh.frames.size()) - 1);
	}
	if (m_displayedAnimation != index)
	{
		if (index >= 0 && index < mesh.animations.size())
		{
			const auto &clip = mesh.animations[index];
			m_animationName->setText(clip.name);
			m_animationFirst->setValue(clip.firstFrame);
			m_animationLast->setValue(clip.firstFrame + clip.frameCount - 1);
			m_clipRate->setValue(clip.framesPerSecond);
		}
		else
		{
			m_animationName->clear();
			m_animationFirst->setValue(frame);
			m_animationLast->setValue(frame);
			m_clipRate->setValue(0);
		}
		m_displayedAnimation = index;
	}
	const bool editable = !m_preview->isPlaying();
	{
		const QSignalBlocker blockedRate(m_animationRate);
		m_animationRate->setValue(m_preview->framesPerSecond());
	}
	m_animationRate->setEnabled(!m_preview->mdlPlaybackActive());
	findChild<QCheckBox *>(QStringLiteral("meshAnimationSmooth"))->setEnabled(!m_preview->mdlPlaybackActive());
	m_addAnimation->setEnabled(editable && mesh.animations.size() < modelDocumentMaxAnimations);
	for (auto *button : m_animationSelectionButtons)
	{
		button->setEnabled(editable && index >= 0);
	}
	m_copyFramePose->setEnabled(editable && mesh.frames.size() > 1 && m_copyPoseSource->value() != frame);
	const int capacity =
		int(std::min<qint64>(modelDocumentMaxFrames, modelDocumentMaxFrameVertices / std::max(1, mesh.vertexCount)) - mesh.frames.size());
	m_inbetweenCount->setMaximum(std::max(1, capacity));
	m_insertInbetweens->setEnabled(editable && frame + 1 < mesh.frames.size() && capacity > 0);
	m_animationSummary->setText(m_preview->playbackSummary());
	m_animationSummary->setAccessibleDescription(m_animationSummary->text());
}

void ModelEditorDialog::restoreAnimationPreview(int frame, bool keepView)
{
	if (!keepView)
	{
		const QSignalBlocker blocked(m_animationClip);
		const auto &clips = m_document.mesh().animations;
		m_animationClip->setCurrentIndex(clips.size() == 1 && clips[0].framesPerSecond > 0 ? m_animationClip->findData(0) : 0);
	}
	refreshAnimationControls();
	int index = m_animationClip->currentData().toInt();
	if (index >= 0)
	{
		const auto &clip = m_document.mesh().animations[index];
		if (frame < clip.firstFrame || frame >= clip.firstFrame + clip.frameCount)
		{
			index = -1;
		}
	}
	if (m_preview->animationIndex() != index)
	{
		m_preview->setAnimationIndex(index);
	}
	if (m_preview->frame() != frame || m_preview->animationSample().fraction != 0)
	{
		m_preview->setFrame(frame);
	}
	{
		const QSignalBlocker blocked(m_animationClip);
		m_animationClip->setCurrentIndex(m_animationClip->findData(index));
	}
	refreshAnimationControls();
}

void ModelEditorDialog::selectAnimationClip()
{
	if (m_refreshing || m_working)
	{
		return;
	}
	{
		QScopedValueRollback<bool> guard(m_refreshing, true);
		m_preview->pause();
		m_preview->setAnimationIndex(m_animationClip->currentData().toInt());
		m_frame->setCurrentIndex(m_preview->frame());
	}
	refresh();
}

void ModelEditorDialog::executeAnimation(ModelEditKind kind)
{
	if (m_working || m_preview->isPlaying())
	{
		return;
	}
	ModelEdit edit;
	edit.kind = kind;
	edit.selection = m_document.selection();
	edit.animationIndex = m_animationClip->currentData().toInt();
	edit.text = m_animationName->text();
	edit.rangeFirst = m_animationFirst->value();
	edit.rangeLast = m_animationLast->value();
	edit.animationRate = m_clipRate->value();
	const int oldClipCount = int(m_document.mesh().animations.size());
	const int frame = m_frame->currentIndex();
	if (kind == ModelEditKind::InsertInbetweens || kind == ModelEditKind::CopyFramePose)
	{
		edit.frame = frame;
		edit.sourceFrame = m_copyPoseSource->value();
		edit.inbetweenCount = m_inbetweenCount->value();
		edit.text = m_inbetweenPrefix->text();
	}
	QString error;
	if (!applyEdit(edit, &error))
	{
		m_status->setText(error);
		return;
	}
	if (kind == ModelEditKind::AddAnimation)
	{
		m_animationClip->setCurrentIndex(m_animationClip->findData(oldClipCount));
	}
	if (kind == ModelEditKind::DeleteAnimation)
	{
		m_animationClip->setCurrentIndex(0);
	}
	if (kind == ModelEditKind::SetAnimationRange)
	{
		{
			const QSignalBlocker blocked(m_animationClip);
			m_animationClip->setCurrentIndex(m_animationClip->findData(edit.animationIndex));
		}
		selectAnimationClip();
	}
	if (kind == ModelEditKind::InsertInbetweens)
	{
		m_frame->setCurrentIndex(frame + 1);
	}
}
} // namespace vibestudio
