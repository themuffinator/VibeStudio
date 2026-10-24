#include "app/model_skeletal_dialog.h"
#include "app/dialog_screen_fit.h"
#include "core/model_skeleton.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelSkeletalDialog)
};
void accessible(QWidget *widget, const QString &label, const QString &description)
{
	widget->setAccessibleName(label);
	widget->setAccessibleDescription(description);
	widget->setToolTip(description);
}
} // namespace
ModelSkeletalDialog::ModelSkeletalDialog(const ModelMesh &mesh, const ModelSelection &selection, const QString &joint, int frame,
										 QWidget *parent)
	: QDialog(parent), m_mesh(mesh), m_selection(selection)
{
	setObjectName("modelSkeletalDialog");
	setWindowTitle(Text::tr("Edit Skeletal Animation"));
	accessible(this, windowTitle(),
			   Text::tr("Edit rest joints, skeletal clips or poses in one "
						"undoable operation."));
	auto *layout = new QVBoxLayout(this);
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	auto *content = new QWidget;
	m_form = new QFormLayout(content);
	m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	m_form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	scroll->setWidget(content);
	layout->addWidget(scroll);
	const auto combo = [&](const char *id, const QString &label, const QString &description, bool fullWidth = false)
	{
		auto *field = new QComboBox;
		field->setObjectName(id);
		field->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		field->setMinimumContentsLength(12);
		accessible(field, label, description);
		if (fullWidth)
		{
			auto *caption = new QLabel(label);
			caption->setBuddy(field);
			m_form->addRow(caption);
			m_form->addRow(field);
		}
		else
			m_form->addRow(label, field);
		return field;
	};
	m_operation = combo("skeletalOperation", Text::tr("&Operation"), Text::tr("Choose a rest-joint, clip or pose operation."), true);
	for (const auto &item : {qMakePair(Text::tr("Transform joint pose"), ModelEditKind::TransformSkeletalPose),
							 qMakePair(Text::tr("Interpolate poses"), ModelEditKind::InterpolateSkeletalPoses),
							 qMakePair(Text::tr("Copy whole pose"), ModelEditKind::CopySkeletalPose),
							 qMakePair(Text::tr("Position bind joint"), ModelEditKind::TransformBindJoint),
							 qMakePair(Text::tr("Add skeletal clip"), ModelEditKind::AddSkeletalClip),
							 qMakePair(Text::tr("Duplicate skeletal clip"), ModelEditKind::DuplicateSkeletalClip),
							 qMakePair(Text::tr("Rename skeletal clip"), ModelEditKind::RenameSkeletalClip),
							 qMakePair(Text::tr("Resize skeletal clip"), ModelEditKind::ResizeSkeletalClip),
							 qMakePair(Text::tr("Set clip timing"), ModelEditKind::SetSkeletalClipTiming),
							 qMakePair(Text::tr("Delete skeletal clip"), ModelEditKind::DeleteSkeletalClip)})
		m_operation->addItem(item.first, int(item.second));
	m_clip = combo("skeletalClip", Text::tr("&Clip"),
				   Text::tr("The retained skeletal clip to edit. Indices "
							"distinguish imported clips with matching names."));
	m_sourceClip =
		combo("skeletalSourceClip", Text::tr("Source clip"), Text::tr("Copy all joints from this clip, or reset them to the bind pose."));
	m_clip->setLayoutDirection(Qt::LeftToRight);
	m_sourceClip->setLayoutDirection(Qt::LeftToRight);
	m_sourceClip->addItem(Text::tr("Bind pose"), -1);
	for (int i = 0; i < mesh.skeleton.clips.size(); ++i)
	{
		// Keep this technical index/name pair in its own left-to-right paragraph;
		// the widget's direction alone does not isolate text from RTL styling.
		const auto label = QStringLiteral("\u2066%1: %2\u2069").arg(i).arg(mesh.skeleton.clips[i].name);
		m_clip->addItem(label, i);
		m_sourceClip->addItem(label, i);
	}
	m_joint = combo("skeletalJoint", Text::tr("&Joint"),
					Text::tr("The joint to transform. Its descendants can follow "
							 "the same movement."));
	m_joint->setLayoutDirection(Qt::LeftToRight);
	for (const auto &item : mesh.skeleton.joints)
		m_joint->addItem(item.name, item.name);
	m_joint->setCurrentIndex(std::max(0, m_joint->findData(joint)));
	m_name = new QLineEdit;
	m_name->setObjectName("skeletalName");
	m_name->setMaxLength(63);
	m_name->setLayoutDirection(Qt::LeftToRight);
	accessible(m_name, Text::tr("Clip name"),
			   Text::tr("A unique name using ASCII letters, digits, underscores, dots "
						"or hyphens, starting with a letter or underscore."));
	m_form->addRow(Text::tr("&Name"), m_name);
	const auto spin = [&](const char *id, const QString &label, const QString &description, int minimum, int maximum)
	{
		auto *field = new QSpinBox;
		field->setObjectName(id);
		field->setRange(minimum, maximum);
		field->setKeyboardTracking(false);
		field->setLayoutDirection(Qt::LeftToRight);
		accessible(field, label, description);
		m_form->addRow(label, field);
		return field;
	};
	m_first = spin("skeletalFirst", Text::tr("&First pose"), Text::tr("First included pose, numbered from zero within this skeletal clip."),
				   0, 1023);
	m_last = spin("skeletalLast", Text::tr("&Last pose"), Text::tr("Last included pose. Interpolation keeps both endpoints unchanged."), 0,
				  1023);
	m_sourceFrame = spin("skeletalSourceFrame", Text::tr("Source pose"),
						 Text::tr("Pose numbered from zero within the source "
								  "clip; the bind pose uses zero."),
						 0, 1023);
	m_count = spin("skeletalCount", Text::tr("Pose count"),
				   Text::tr("New clips repeat the bind pose. Extending a clip "
							"holds its final pose; shrinking removes its tail."),
				   1, 1024);
	m_count->setValue(24);
	m_rate = new QDoubleSpinBox;
	m_rate->setObjectName("skeletalRate");
	m_rate->setRange(.001, 1000);
	m_rate->setDecimals(3);
	m_rate->setValue(24);
	m_rate->setKeyboardTracking(false);
	m_rate->setLayoutDirection(Qt::LeftToRight);
	accessible(m_rate, Text::tr("Clip FPS"),
			   Text::tr("Saved playback rate shared by the preview range and "
						"skeletal exports."));
	m_form->addRow(Text::tr("FPS"), m_rate);
	m_loop = new QCheckBox(Text::tr("Loop in native skeletal formats"));
	m_loop->setObjectName("skeletalLoop");
	m_loop->setChecked(true);
	accessible(m_loop, m_loop->text(),
			   Text::tr("Save the clip loop flag for formats such as IQM. The "
						"editor transport has its own looping preview."));
	m_form->addRow(m_loop);
	m_space = combo("skeletalSpace", Text::tr("Transform space"),
					Text::tr("Model axes rotate around the joint origin. Local "
							 "axes follow the joint's current orientation."));
	m_space->addItem(Text::tr("Model"), false);
	m_space->addItem(Text::tr("Local"), true);
	m_children = new QCheckBox(Text::tr("Move descendant joints"));
	m_children->setObjectName("skeletalChildren");
	m_children->setChecked(true);
	accessible(m_children, m_children->text(),
			   Text::tr("Move the joint's children with it. Clear this to keep "
						"child joints in their current model-space poses."));
	m_form->addRow(m_children);
	const QStringList labels{Text::tr("Translation"), Text::tr("Rotation"), Text::tr("Scale")};
	const QStringList descriptions{Text::tr("Translation in model units."),
								   Text::tr("Rotation in degrees, applied around X, then Y, then Z."),
								   Text::tr("Positive scale factor; one preserves the current scale. Native "
											"export limits still apply.")};
	for (int vector = 0; vector < 3; ++vector)
	{
		m_vectors[vector] = new QWidget;
		auto *axes = new QVBoxLayout(m_vectors[vector]);
		axes->setContentsMargins(0, 0, 0, 0);
		for (int axis = 0; axis < 3; ++axis)
		{
			auto *field = new QDoubleSpinBox;
			field->setObjectName(QStringLiteral("skeletalVector%1Axis%2").arg(vector).arg(axis));
			field->setDecimals(6);
			field->setRange(vector == 2 ? .000001 : -1000000, vector == 2 ? 1000 : 1000000);
			field->setValue(vector == 2 ? 1 : 0);
			field->setKeyboardTracking(false);
			field->setLayoutDirection(Qt::LeftToRight);
			const auto label = QString(QChar('X' + axis));
			field->setPrefix(label + QLatin1Char(' '));
			accessible(field, labels[vector] + QLatin1Char(' ') + label, descriptions[vector]);
			axes->addWidget(field);
			m_axes[vector][axis] = field;
		}
		m_form->addRow(labels[vector], m_vectors[vector]);
	}
	m_summary = new QLabel;
	m_summary->setObjectName("skeletalSummary");
	m_summary->setWordWrap(true);
	m_summary->setTextFormat(Qt::PlainText);
	m_summary->setMinimumWidth(0);
	m_summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_summary->setAccessibleName(Text::tr("Skeletal edit scope"));
	m_form->addRow(m_summary);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
	m_apply = buttons->button(QDialogButtonBox::Apply);
	m_apply->setObjectName("applySkeletalEdit");
	accessible(m_apply, Text::tr("Apply skeletal edit"),
			   Text::tr("Validate and apply one undoable edit through the model worker. "
						"Invalid or cancelled edits leave the model unchanged."));
	connect(m_apply, &QPushButton::clicked, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
	const int clip = mesh.skeleton.bakedClipForFrame.value(frame, -1);
	if (clip >= 0)
		m_clip->setCurrentIndex(m_clip->findData(clip));
	if (mesh.skeleton.clips.isEmpty())
		m_operation->setCurrentIndex(m_operation->findData(int(ModelEditKind::AddSkeletalClip)));
	refresh(true);
	if (clip >= 0)
	{
		const int local = frame - mesh.skeleton.bakedClipForFrame.indexOf(clip);
		m_first->setValue(local);
		m_last->setValue(local);
	}
	connect(m_operation, &QComboBox::currentIndexChanged, this, [this] { refresh(true); });
	connect(m_clip, &QComboBox::currentIndexChanged, this, [this] { refresh(true); });
	connect(m_sourceClip, &QComboBox::currentIndexChanged, this, [this] { refresh(false); });
	connect(m_name, &QLineEdit::textChanged, this, [this] { refresh(false); });
	fitDialogToScreen(this, QSize(std::max(640, fontMetrics().averageCharWidth() * 75), 700));
}
void ModelSkeletalDialog::refresh(bool resetFields)
{
	const auto kind = ModelEditKind(m_operation->currentData().toInt());
	const bool bind = kind == ModelEditKind::TransformBindJoint, add = kind == ModelEditKind::AddSkeletalClip;
	const bool rename = kind == ModelEditKind::RenameSkeletalClip, duplicate = kind == ModelEditKind::DuplicateSkeletalClip;
	const bool transform = kind == ModelEditKind::TransformSkeletalPose, copy = kind == ModelEditKind::CopySkeletalPose;
	const bool interpolate = kind == ModelEditKind::InterpolateSkeletalPoses, timing = kind == ModelEditKind::SetSkeletalClipTiming;
	const bool resize = kind == ModelEditKind::ResizeSkeletalClip, range = transform || copy || interpolate;
	m_form->setRowVisible(m_clip, !bind && !add);
	m_form->setRowVisible(m_joint, bind || transform);
	m_form->setRowVisible(m_name, add || duplicate || rename);
	m_form->setRowVisible(m_first, range);
	m_form->setRowVisible(m_last, range);
	m_form->setRowVisible(m_count, add || resize);
	m_form->setRowVisible(m_rate, add || timing);
	m_form->setRowVisible(m_loop, add || timing);
	m_form->setRowVisible(m_sourceClip, copy);
	m_form->setRowVisible(m_sourceFrame, copy);
	m_form->setRowVisible(m_space, bind || transform);
	m_form->setRowVisible(m_children, bind || transform);
	for (auto *vector : m_vectors)
		m_form->setRowVisible(vector, bind || transform);
	const int clip = m_clip->currentIndex() >= 0 ? m_clip->currentData().toInt() : -1;
	if (clip >= 0)
	{
		const auto &value = m_mesh.skeleton.clips[clip];
		m_first->setMaximum(std::max(0, int(value.frames.size()) - 1));
		m_last->setMaximum(m_first->maximum());
		if (resetFields)
		{
			m_first->setValue(0);
			m_last->setValue(m_last->maximum());
			if (resize)
				m_count->setValue(value.frames.size());
			if (timing)
			{
				m_rate->setValue(std::max(.001, value.framesPerSecond));
				m_loop->setChecked(value.loops);
			}
			if (rename || duplicate)
			{
				const QSignalBlocker blocker(m_name);
				m_name->setText(value.name + (duplicate ? QStringLiteral("_copy") : QString()));
			}
		}
	}
	const int source = m_sourceClip->currentData().toInt();
	m_sourceFrame->setMaximum(source < 0 ? 0 : std::max(0, int(m_mesh.skeleton.clips[source].frames.size()) - 1));
	QString summary = bind			? Text::tr("Move the rest joint while keeping the model and "
														"existing animation in place.")
					  : transform	? Text::tr("Transform the joint in every selected pose. Skin and joint "
												 "attachments follow; collision tracks stay in place.")
					  : interpolate ? Text::tr("Blend all joints between the endpoint poses using "
											   "parent-relative rotation, translation and scale. "
											   "Endpoints stay unchanged.")
					  : copy		? Text::tr("Copy every joint into the selected poses. Independent "
													  "frame and attachment edits are protected.")
					  : add			? Text::tr("Create a clip from the bind pose. It appears as a "
													   "range in the animation preview.")
					  : resize		? Text::tr("Extend by holding the final pose, or remove poses from the "
													"end. Existing frame references follow the new ranges.")
					  : duplicate	? Text::tr("Duplicate the clip and its preview poses, including "
												 "manual attachments and collision tracks.")
					  : rename		? Text::tr("Rename the skeletal clip and its matching preview "
													"range. Existing frame names stay unchanged.")
					  : timing		? Text::tr("Save clip timing without resampling poses. Native loop "
													"flags are separate from transport looping.")
									: Text::tr("Delete this clip and its preview poses. Custom ranges that "
													"would become empty must be removed explicitly first.");
	if (clip >= 0 && !add && !bind && !m_mesh.skeleton.bakedClipForFrame.contains(clip))
	{
		summary += QLatin1Char(' ') + Text::tr("This clip is outside the working set: the edit changes its poses in the skeleton "
											   "without baking frames. Add it in Skeleton > Working Set to preview it.");
		if (duplicate)
			summary += QLatin1Char(' ') + Text::tr("Duplicating it uses the first editable frame for manual "
												   "attachments, unskinned surfaces and collision tracks.");
	}
	if (m_mesh.skeleton.isEmpty())
		summary = Text::tr("Add the model's first joint in Edit Skeleton before "
						   "authoring skeletal animation.");
	m_summary->setText(summary);
	m_apply->setEnabled(!m_mesh.skeleton.isEmpty() && (add || bind || clip >= 0) &&
						(!(add || rename || duplicate) || !m_name->text().isEmpty()));
}
ModelEdit ModelSkeletalDialog::edit() const
{
	ModelEdit result;
	result.selection = m_selection;
	result.kind = ModelEditKind(m_operation->currentData().toInt());
	auto &o = result.skeletal;
	const auto kind = result.kind;
	const bool bind = kind == ModelEditKind::TransformBindJoint, add = kind == ModelEditKind::AddSkeletalClip;
	const bool transform = kind == ModelEditKind::TransformSkeletalPose, copy = kind == ModelEditKind::CopySkeletalPose;
	if (!bind && !add)
		o.clip = m_clip->currentData().toInt();
	if (add || kind == ModelEditKind::DuplicateSkeletalClip || kind == ModelEditKind::RenameSkeletalClip)
		o.name = m_name->text();
	if (add || kind == ModelEditKind::ResizeSkeletalClip)
		o.frameCount = m_count->value();
	if (add || kind == ModelEditKind::SetSkeletalClipTiming)
	{
		o.rate = m_rate->value();
		o.loops = m_loop->isChecked();
	}
	if (transform || copy || kind == ModelEditKind::InterpolateSkeletalPoses)
	{
		o.first = m_first->value();
		o.last = m_last->value();
	}
	if (copy)
	{
		o.sourceClip = m_sourceClip->currentData().toInt();
		o.sourceFrame = m_sourceFrame->value();
	}
	if (bind || transform)
	{
		o.joint = m_joint->currentData().toString();
		o.local = m_space->currentData().toBool();
		o.descendants = m_children->isChecked();
		ModelVec3 *vectors[]{&o.translation, &o.rotation, &o.scale};
		for (int i = 0; i < 3; ++i)
			*vectors[i] = {float(m_axes[i][0]->value()), float(m_axes[i][1]->value()), float(m_axes[i][2]->value())};
	}
	return result;
}
} // namespace vibestudio
