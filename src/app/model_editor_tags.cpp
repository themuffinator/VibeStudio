#include "app/model_editor_dialog.h"

#include "app/model_viewport.h"
#include "core/model_tags.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace vibestudio
{
void ModelEditorDialog::addTagControls(QFormLayout *animation)
{
	auto *group = new QGroupBox(QCoreApplication::translate("VibeStudioModelEditor", "Attachment tags"));
	group->setObjectName(QStringLiteral("meshTagControls"));
	group->setAccessibleName(group->title());
	auto *form = new QFormLayout(group);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->setRowWrapPolicy(QFormLayout::WrapAllRows);
	m_tagSummary = new QLabel;
	m_tagSummary->setObjectName(QStringLiteral("meshTagSummary"));
	m_tagSummary->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Selected attachment pose"));
	m_tagSummary->setTextFormat(Qt::PlainText);
	m_tagSummary->setWordWrap(true);
	m_tagSummary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_tagSummary->setMinimumWidth(0);
	form->addRow(m_tagSummary);
	m_tagName = new QLineEdit;
	m_tagName->setObjectName(QStringLiteral("meshTagName"));
	m_tagName->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Attachment tag name"));
	m_tagName->setLayoutDirection(Qt::LeftToRight);
	m_tagName->setPlaceholderText(QStringLiteral("tag_weapon"));
	m_tagName->setToolTip(QCoreApplication::translate(
		"VibeStudioModelEditor",
		"A unique name of 1–63 printable ASCII characters, without surrounding spaces. Tag identities span every animation frame."));
	form->addRow(QCoreApplication::translate("VibeStudioModelEditor", "Name"), m_tagName);
	const auto button = [&](const QString &label, const QString &name, ModelEditKind kind, bool selected)
	{
		auto *control = new QPushButton(label);
		control->setObjectName(name);
		control->setAccessibleName(label);
		connect(control, &QPushButton::clicked, this, [this, kind] { executeTag(kind); });
		form->addRow(control);
		if (selected)
		{
			m_tagSelectionButtons << control;
		}
		return control;
	};
	button(QCoreApplication::translate("VibeStudioModelEditor", "Add Tag"), QStringLiteral("addMeshTag"), ModelEditKind::AddTag, false);
	button(QCoreApplication::translate("VibeStudioModelEditor", "Duplicate Tag"), QStringLiteral("duplicateMeshTag"),
		   ModelEditKind::DuplicateTag, true);
	button(QCoreApplication::translate("VibeStudioModelEditor", "Rename Tag"), QStringLiteral("renameMeshTag"), ModelEditKind::RenameTag,
		   true);
	button(QCoreApplication::translate("VibeStudioModelEditor", "Delete Tag"), QStringLiteral("deleteMeshTag"), ModelEditKind::DeleteTag,
		   true);
	m_tagFrameScope = new QComboBox;
	m_tagFrameScope->setObjectName(QStringLiteral("meshTagFrameScope"));
	m_tagFrameScope->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Attachment pose edit scope"));
	m_tagFrameScope->addItems({QCoreApplication::translate("VibeStudioModelEditor", "All frames"),
							   QCoreApplication::translate("VibeStudioModelEditor", "Current frame")});
	m_tagFrameScope->setToolTip(QCoreApplication::translate("VibeStudioModelEditor",
															"Origin, orientation and pose copying use this scope. It is shared with "
															"Geometry transforms; tag names always apply to every frame."));
	connect(m_tagFrameScope, &QComboBox::currentIndexChanged, m_frameScope, &QComboBox::setCurrentIndex);
	connect(m_frameScope, &QComboBox::currentIndexChanged, m_tagFrameScope, &QComboBox::setCurrentIndex);
	form->addRow(QCoreApplication::translate("VibeStudioModelEditor", "Pose scope"), m_tagFrameScope);
	for (int axis = 0; axis < 3; ++axis)
	{
		auto *field = new QDoubleSpinBox;
		field->setObjectName(QStringLiteral("meshTagOrigin%1").arg(axis));
		field->setDecimals(6);
		field->setRange(-1000000, 1000000);
		field->setMinimumWidth(0);
		field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		field->setLayoutDirection(Qt::LeftToRight);
		const auto label = QCoreApplication::translate("VibeStudioModelEditor", "Origin %1").arg(QChar('X' + axis));
		field->setAccessibleName(label);
		m_tagOrigin[axis] = field;
		form->addRow(label, field);
	}
	auto *centre = new QPushButton(QCoreApplication::translate("VibeStudioModelEditor", "Use Selection Centre"));
	centre->setObjectName(QStringLiteral("meshTagUseSelectionCentre"));
	centre->setAccessibleName(centre->text());
	centre->setToolTip(QCoreApplication::translate(
		"VibeStudioModelEditor", "Fill the origin fields from selected mesh components in the displayed frame, then add a tag."));
	connect(centre, &QPushButton::clicked, this,
			[this]
			{
				const auto &selection = m_document.selection();
				const auto &surface = m_document.mesh().surfaces[selection.surface];
				auto vertices = selection.vertices;
				for (const auto& edge : selection.edges)
				{
					vertices << edge.first << edge.second;
				}
				for (int face : selection.faces)
				{
					const auto triangle = surface.triangles[face];
					vertices << triangle.a << triangle.b << triangle.c;
				}
				ModelVec3 origin;
				if (!modelTransformPivot(surface.frames[m_frame->currentIndex()].positions, vertices, ModelTransformPivot::SelectionCentre,
										 {}, &origin))
				{
					m_status->setText(QCoreApplication::translate("VibeStudioModelEditor",
																  "Select mesh components to use their centre as the tag origin."));
					return;
				}
				const float coordinates[]{origin.x, origin.y, origin.z};
				for (int axis = 0; axis < 3; ++axis)
				{
					m_tagOrigin[axis]->setValue(coordinates[axis]);
				}
			});
	form->addRow(centre);
	button(QCoreApplication::translate("VibeStudioModelEditor", "Set Origin"), QStringLiteral("setMeshTagOrigin"),
		   ModelEditKind::SetTagOrigin, true);
	auto *reset = button(QCoreApplication::translate("VibeStudioModelEditor", "Reset Orientation"),
						 QStringLiteral("resetMeshTagOrientation"), ModelEditKind::ResetTagOrientation, true);
	reset->setToolTip(QCoreApplication::translate(
		"VibeStudioModelEditor", "Align the tag's local axes with model X, Y and Z in the chosen scope. Use Rotate or Geometry for "
								 "rotation deltas; imported orientation is preserved until explicitly edited."));
	m_tagCopyFrame = new QSpinBox;
	m_tagCopyFrame->setObjectName(QStringLiteral("meshTagCopyFrame"));
	m_tagCopyFrame->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Attachment source frame"));
	m_tagCopyFrame->setLayoutDirection(Qt::LeftToRight);
	form->addRow(QCoreApplication::translate("VibeStudioModelEditor", "Copy from frame"), m_tagCopyFrame);
	button(QCoreApplication::translate("VibeStudioModelEditor", "Copy Tag Pose"), QStringLiteral("copyMeshTagPose"),
		   ModelEditKind::CopyTagPose, true);
	animation->addRow(group);
	connect(m_preview, &ModelViewport::playbackChanged, group, [group](bool playing) { group->setEnabled(!playing); });
}
void ModelEditorDialog::refreshTags()
{
	if (!m_tagName)
	{
		return;
	}
	const auto &mesh = m_document.mesh();
	const auto &name = m_document.selection().tag;
	const int frame = m_frame->currentIndex();
	const auto tag = findModelTag(mesh, name, frame);
	for (auto *button : m_tagSelectionButtons)
	{
		button->setEnabled(tag != nullptr);
	}
	m_tagCopyFrame->setRange(0, qMax(0, int(mesh.frames.size()) - 1));
	m_tagCopyFrame->setEnabled(tag != nullptr);
	if (!tag)
	{
		m_displayedTag.clear();
		m_tagSummary->setText(QCoreApplication::translate("VibeStudioModelEditor",
														  "%n attachment tag(s). Select a tag in the component table to edit its pose.",
														  nullptr, mesh.tagCount));
		return;
	}
	if (name != m_displayedTag)
	{
		m_tagName->setText(name);
		m_displayedTag = name;
	}
	const float coordinates[]{tag->origin.x, tag->origin.y, tag->origin.z};
	for (int axis = 0; axis < 3; ++axis)
	{
		m_tagOrigin[axis]->setValue(coordinates[axis]);
	}
	QStringList axes;
	for (int row = 0; row < 3; ++row)
	{
		axes << QStringLiteral("%1: %2, %3, %4")
					.arg(QChar('X' + row))
					.arg(tag->axis[row * 3], 0, 'g', 5)
					.arg(tag->axis[row * 3 + 1], 0, 'g', 5)
					.arg(tag->axis[row * 3 + 2], 0, 'g', 5);
	}
	m_tagSummary->setText(QCoreApplication::translate("VibeStudioModelEditor", "%1 · frame %2\nLocal axes\n%3")
							  .arg(name)
							  .arg(frame)
							  .arg(axes.join(QLatin1Char('\n'))));
}
void ModelEditorDialog::executeTag(ModelEditKind kind)
{
	if (m_working || m_preview->isPlaying())
	{
		return;
	}
	ModelEdit edit;
	edit.kind = kind;
	edit.selection = m_document.selection();
	edit.text = m_tagName->text();
	edit.tagOrigin = {float(m_tagOrigin[0]->value()), float(m_tagOrigin[1]->value()), float(m_tagOrigin[2]->value())};
	edit.sourceFrame = m_tagCopyFrame->value();
	const bool identity = kind == ModelEditKind::AddTag || kind == ModelEditKind::DuplicateTag || kind == ModelEditKind::RenameTag ||
						  kind == ModelEditKind::DeleteTag;
	edit.frame = identity || m_tagFrameScope->currentIndex() == 0 ? -1 : m_frame->currentIndex();
	QString error;
	if (!applyEdit(edit, &error))
	{
		m_status->setText(error);
	}
}
} // namespace vibestudio
