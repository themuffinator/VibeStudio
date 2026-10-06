#include "app/model_editor_dialog.h"
#include "app/model_viewport.h"
#include "app/wrapping_action_button.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelCollisionEditor)
};
ModelVec3 vector(QDoubleSpinBox *const *fields)
{
	return {float(fields[0]->value()), float(fields[1]->value()), float(fields[2]->value())};
}
void setVector(QDoubleSpinBox **fields, ModelVec3 v)
{
	fields[0]->setValue(v.x);
	fields[1]->setValue(v.y);
	fields[2]->setValue(v.z);
}
} // namespace
void ModelEditorDialog::addCollisionControls(QFormLayout *form)
{
	const auto combo = [&](const char *name, const QString &label) {
		auto *field = new QComboBox;
		field->setObjectName(QString::fromLatin1(name));
		field->setAccessibleName(label);
		field->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		field->setMinimumContentsLength(12);
		form->addRow(label, field);
		return field;
	};
	m_collisionSummary = new QLabel;
	m_collisionSummary->setObjectName("meshCollisionSummary");
	m_collisionSummary->setAccessibleName(Text::tr("Collision status"));
	m_collisionSummary->setTextFormat(Qt::PlainText);
	m_collisionSummary->setWordWrap(true);
	m_collisionSummary->setMinimumWidth(0);
	m_collisionSummary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	form->addRow(m_collisionSummary);
	m_showCollision = new QCheckBox(Text::tr("Show collision boxes"));
	m_showCollision->setObjectName("meshShowCollision");
	m_showCollision->setAccessibleName(m_showCollision->text());
	m_showCollision->setToolTip(
		Text::tr("Draw collision boxes at the preview pose. The selected box has solid thicker edges; other boxes have dashed edges."));
	m_showCollision->setChecked(true);
	m_preview->setShowCollision(true);
	form->addRow(m_showCollision);
	connect(m_showCollision, &QCheckBox::toggled, m_preview, &ModelViewport::setShowCollision);
	m_collisionBoxes = combo("meshCollisionBoxes", Text::tr("Collision box"));
	m_collisionName = new QLineEdit;
	m_collisionName->setObjectName("meshCollisionName");
	m_collisionName->setAccessibleName(Text::tr("Collision box name"));
	m_collisionName->setMaxLength(128);
	m_collisionName->setText("collision_1");
	form->addRow(Text::tr("Name"), m_collisionName);
	const auto axes = [&](const QString &label, const char *name, QDoubleSpinBox **fields, double low, double high, double initial) {
		for (int axis = 0; axis < 3; ++axis)
		{
			auto *field = new QDoubleSpinBox;
			field->setDecimals(4);
			field->setRange(low, high);
			field->setValue(initial);
			field->setLayoutDirection(Qt::LeftToRight);
			field->setMinimumWidth(0);
			field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
			field->setObjectName(QString::fromLatin1(name) + QString::number(axis));
			const auto title = Text::tr("%1 %2").arg(label, QString(QChar('X' + axis)));
			field->setAccessibleName(title);
			form->addRow(title, field);
			fields[axis] = field;
		}
	};
	axes(Text::tr("Centre"), "meshCollisionCentre", m_collisionCentre, -32768, 32768, 0);
	axes(Text::tr("Size"), "meshCollisionSize", m_collisionSize, 1, 65536, 16);
	axes(Text::tr("Rotation"), "meshCollisionRotation", m_collisionRotation, -36000, 36000, 0);
	const auto button = [&](const char *name, const QString &label, ModelEditKind kind, bool selected) {
		auto *control = new WrappingActionButton(label);
		control->setObjectName(QString::fromLatin1(name));
		control->setAccessibleName(label);
		form->addRow(control);
		connect(control, &QPushButton::clicked, this, [this, kind] { executeCollision(kind); });
		if (selected)
		{
			m_collisionSelectedButtons << control;
		}
		return control;
	};
	button("addMeshCollision", Text::tr("Add Box"), ModelEditKind::AddCollisionBox, false);
	m_collisionPoseScope = combo("meshCollisionPoseScope", Text::tr("Edit poses"));
	m_collisionPoseScope->addItems({Text::tr("All frames"), Text::tr("Current frame")});
	m_collisionPoseScope->setToolTip(Text::tr("Animated boxes: apply absolute values or transforms to the current pose or every pose. "
											  "Shared with Geometry transform scope. Static boxes always use all frames."));
	m_collisionPoseScope->setAccessibleDescription(m_collisionPoseScope->toolTip());
	connect(m_collisionPoseScope, &QComboBox::currentIndexChanged, m_frameScope, &QComboBox::setCurrentIndex);
	connect(m_frameScope, &QComboBox::currentIndexChanged, m_collisionPoseScope, &QComboBox::setCurrentIndex);
	button("updateMeshCollision", Text::tr("Apply Box"), ModelEditKind::UpdateCollisionBox, true);
	m_collisionAnimate = button("animateMeshCollision", Text::tr("Animate Box"), ModelEditKind::AnimateCollisionBox, true);
	m_collisionFreeze = button("freezeMeshCollision", Text::tr("Make Static from Current Frame"), ModelEditKind::FreezeCollisionBox, true);
	m_collisionAnimate->setToolTip(Text::tr("Copy this static box into every stored pose. Collision poses follow playback and frame edits, "
											"and remain independent of later mesh edits."));
	m_collisionAnimate->setAccessibleDescription(m_collisionAnimate->toolTip());
	m_collisionFreeze->setToolTip(Text::tr("Keep the current stored collision pose for every frame. Undo restores the complete track."));
	m_collisionFreeze->setAccessibleDescription(m_collisionFreeze->toolTip());
	button("duplicateMeshCollision", Text::tr("Duplicate Box"), ModelEditKind::DuplicateCollisionBox, true);
	button("deleteMeshCollision", Text::tr("Delete Box"), ModelEditKind::DeleteCollisionBox, true);
	m_collisionFitScope = combo("meshCollisionFitScope", Text::tr("Fit poses"));
	m_collisionFitScope->addItems({Text::tr("All frames"), Text::tr("Current frame")});
	auto *fit = button("fitMeshCollision", Text::tr("Fit New Box"), ModelEditKind::FitCollisionBox, false);
	fit->setToolTip(Text::tr("Fit an axis-aligned static box to selected vertices, edges or faces; without mesh components, fit every "
							 "surface. Thin dimensions expand to one unit. The box does not follow later mesh edits or animation."));
	fit->setAccessibleDescription(fit->toolTip());
	auto *fitAnimated = button("fitAnimatedMeshCollision", Text::tr("Fit New Animated Box"), ModelEditKind::FitAnimatedCollisionBox, false);
	fitAnimated->setToolTip(Text::tr("Fit an independent axis-aligned box in every stored frame, using selected components or the whole "
									 "model. Later mesh edits do not refit these poses."));
	fitAnimated->setAccessibleDescription(fitAnimated->toolTip());
	m_collisionTarget = combo("meshCollisionTarget", Text::tr("Map target"));
	m_collisionTarget->addItem(Text::tr("Quake clip hull"), "quake");
	m_collisionTarget->addItem(Text::tr("Quake II player clip"), "quake2");
	m_collisionTarget->addItem(Text::tr("Quake III clip shader"), "quake3");
	m_collisionMaterial = new QLineEdit;
	m_collisionMaterial->setObjectName("meshCollisionMaterial");
	m_collisionMaterial->setAccessibleName(Text::tr("Quake III clip shader"));
	m_collisionMaterial->setLayoutDirection(Qt::LeftToRight);
	m_collisionMaterial->setPlaceholderText("common/clip");
	m_collisionMaterial->setToolTip(Text::tr("Project shader below textures/, without that prefix. Its compiler definition must include "
											 "surfaceparm playerclip and nodraw. Availability is not verified here."));
	m_collisionMaterial->setAccessibleDescription(m_collisionMaterial->toolTip());
	form->addRow(Text::tr("Clip shader"), m_collisionMaterial);
	axes(Text::tr("Map origin"), "meshCollisionMapOrigin", m_collisionOrigin, -32768, 32768, 0);
	m_collisionExport = new QPushButton(Text::tr("Export Collision Map…"));
	m_collisionExport->setObjectName("exportMeshCollision");
	m_collisionExport->setAccessibleName(m_collisionExport->text());
	form->addRow(m_collisionExport);
	m_collisionPlace = new QPushButton(Text::tr("Place Collision in Level"));
	m_collisionPlace->setObjectName("placeMeshCollision");
	m_collisionPlace->setAccessibleName(m_collisionPlace->text());
	const auto handoffHelp = Text::tr(
		"Export or place every box at the current stored frame as static map brushes. Collision animation remains in the mesh source.");
	for (auto *control : {m_collisionExport, m_collisionPlace})
	{
		control->setToolTip(handoffHelp);
		control->setAccessibleDescription(handoffHelp);
	}
	form->addRow(m_collisionPlace);
	connect(m_collisionExport, &QPushButton::clicked, this, [this] { handoffCollision(false); });
	connect(m_collisionPlace, &QPushButton::clicked, this, [this] { handoffCollision(true); });
	connect(m_collisionTarget, &QComboBox::currentIndexChanged, this, [this] { refreshCollision(); });
	connect(m_collisionBoxes, &QComboBox::currentIndexChanged, this, [this] {
		if (m_refreshing || m_working)
		{
			return;
		}
		ModelSelection selection;
		selection.surface = m_document.selection().surface;
		selection.collision = m_collisionBoxes->currentData().toString();
		m_document.setSelection(selection);
		refresh();
	});
}
void ModelEditorDialog::refreshCollision()
{
	if (!m_collisionBoxes)
	{
		return;
	}
	const auto &mesh = m_document.mesh();
	const auto name = m_document.selection().collision;
	const auto box = findModelCollisionBox(mesh, name);
	const QSignalBlocker block(m_collisionBoxes);
	if (m_collisionRevision != m_document.revisionFingerprint())
	{
		m_collisionBoxes->clear();
		m_collisionBoxes->addItem(Text::tr("No collision selection"), QString());
		for (const auto &entry : mesh.collisionBoxes)
		{
			m_collisionBoxes->addItem(entry.name, entry.name);
		}
	}
	m_collisionBoxes->setCurrentIndex(std::max(0, m_collisionBoxes->findData(name)));
	const int frame = m_frame->currentIndex();
	ModelCollisionBox pose;
	if (box && sampleModelCollisionBox(*box, frame, frame, 0, &pose) &&
		(name != m_displayedCollision || m_collisionRevision != m_document.revisionFingerprint() || frame != m_displayedCollisionFrame))
	{
		m_collisionName->setText(box->name);
		setVector(m_collisionCentre, pose.centre);
		setVector(m_collisionSize, pose.size);
		setVector(m_collisionRotation, pose.rotation);
	}
	m_displayedCollisionFrame = frame;
	m_displayedCollision = name;
	m_collisionRevision = m_document.revisionFingerprint();
	for (auto *button : m_collisionSelectedButtons)
	{
		button->setEnabled(box != nullptr);
	}
	const bool animated = box && !box->framePoses.isEmpty();
	m_collisionAnimate->setEnabled(box && !animated);
	m_collisionFreeze->setEnabled(animated);
	m_collisionPoseScope->setEnabled(animated);
	m_collisionPoseScope->setCurrentIndex(m_frameScope->currentIndex());
	m_preview->setEditCollision(name);
	m_collisionMaterial->setEnabled(m_collisionTarget->currentData() == "quake3");
	m_collisionExport->setEnabled(!mesh.collisionBoxes.isEmpty());
	m_collisionSummary->setText(
		Text::tr("%1 of 64 collision boxes. Source, undo and recovery retain them; model exports omit them.")
			.arg(mesh.collisionBoxes.size()) +
		QStringLiteral("\n") +
		(animated ? Text::tr("Animated box · stored frame %1 of %2. Map handoff samples the current stored frame as static brushes.")
						.arg(frame)
						.arg(mesh.frames.size())
				  : Text::tr("Static box. Animate Box creates a pose for every mesh frame.")) +
		QStringLiteral("\n") +
		(m_collisionTarget->currentData() == "quake3"
			 ? Text::tr("Quake III needs a project clip shader in the compiler assets. Export does not verify its behavior.")
		 : m_collisionTarget->currentData() == "quake2" ? Text::tr("Quake II: player movement only; no monster or projectile collision.")
														: Text::tr("Quake: expanded clip hulls; no point-trace collision.")));
	refreshContext();
}
void ModelEditorDialog::executeCollision(ModelEditKind kind)
{
	if (m_working)
	{
		return;
	}
	m_preview->pause();
	ModelEdit edit;
	edit.kind = kind;
	edit.selection = m_document.selection();
	edit.collisionBox = {m_collisionName->text(), vector(m_collisionCentre), vector(m_collisionSize), vector(m_collisionRotation)};
	if ((kind == ModelEditKind::AddCollisionBox || kind == ModelEditKind::FitCollisionBox ||
		 kind == ModelEditKind::FitAnimatedCollisionBox || kind == ModelEditKind::DuplicateCollisionBox) &&
		findModelCollisionBox(m_document.mesh(), edit.collisionBox.name))
	{
		const auto stem = edit.collisionBox.name.left(115);
		int suffix = 2;
		while (findModelCollisionBox(m_document.mesh(), stem + '_' + QString::number(suffix)))
		{
			++suffix;
		}
		edit.collisionBox.name = stem + '_' + QString::number(suffix);
	}
	edit.frame = kind == ModelEditKind::FitCollisionBox && m_collisionFitScope->currentIndex() == 1 ? m_frame->currentIndex() : -1;
	const auto box = findModelCollisionBox(m_document.mesh(), edit.selection.collision);
	if (kind == ModelEditKind::FreezeCollisionBox ||
		(kind == ModelEditKind::UpdateCollisionBox && box && !box->framePoses.isEmpty() && m_collisionPoseScope->currentIndex() == 1))
		edit.frame = m_frame->currentIndex();
	QString error;
	if (!applyEdit(edit, &error))
	{
		m_status->setText(error);
	}
	else if (kind == ModelEditKind::AnimateCollisionBox || kind == ModelEditKind::FitAnimatedCollisionBox)
		m_frameScope->setCurrentIndex(1);
}
ModelCollisionExport ModelEditorDialog::collisionRequest() const
{
	return {m_collisionTarget->currentData().toString(),
			m_collisionTarget->currentData() == "quake3" ? m_collisionMaterial->text() : QString(), vector(m_collisionOrigin),
			m_frame->currentIndex()};
}
bool ModelEditorDialog::exportCollision(const QString &path, const ModelCollisionExport &request, bool overwrite, QString *error)
{
	QString localError;
	if (!error)
	{
		error = &localError;
	}
	if (m_working || !path.endsWith(".map", Qt::CaseInsensitive))
	{
		*error = Text::tr("Choose a .map output while the modeller is idle.");
		return false;
	}
	const auto recoverySource = m_recoverySourcePath;
	ModelCollisionMap result;
	const bool ok = performWork(
		Text::tr("Export Collision Map"),
		[path, request, overwrite, recoverySource, &result](ModelDocument &candidate, QString *failure, const ModelWorkControl &control) {
			const auto target = inspectModelWriteTarget(path, control);
			if (!target.isValid())
			{
				*failure = target.error;
				return false;
			}
			if ((target.existed && !overwrite) || modelPathsReferToSameFile(path, candidate.path()) ||
				modelPathsReferToSameFile(path, candidate.mesh().sourcePath) || modelPathsReferToSameFile(path, recoverySource))
			{
				*failure = Text::tr("Protect source inputs and approve any existing derivative before replacing it.");
				return false;
			}
			return exportModelCollisionMap(candidate.mesh(), request, &result, failure, control) &&
				   writeModelFile(target, result.bytes, failure, control);
		},
		error, true);
	if (ok)
	{
		m_status->setText(Text::tr("Exported %1 collision brushes to %2. %3").arg(result.brushCount).arg(path, result.notes.join(' ')));
	}
	return ok;
}
bool ModelEditorDialog::placeCollision(const ModelCollisionExport &request, QString *error)
{
	QString localError;
	if (!error)
	{
		error = &localError;
	}
	if (m_working || !collisionDestination)
	{
		*error = Text::tr("Open a Quake-family level before placing collision.");
		return false;
	}
	const auto destination = collisionDestination();
	if (!destination.publish)
	{
		*error = Text::tr("No writable level destination is available.");
		return false;
	}
	LevelPlacementResult placed;
	if (!performWork(
			Text::tr("Place Collision"),
			[request, map = destination.document, &placed](ModelDocument &candidate, QString *failure, const ModelWorkControl &control) {
				placed = prepareModelCollisionPlacement(candidate.mesh(), request, map, control);
				if (!placed.succeeded)
				{
					*failure = placed.error;
				}
				return placed.succeeded;
			},
			error))
	{
		return false;
	}
	if (!destination.publish(placed.document, error))
	{
		return false;
	}
	m_status->setText(Text::tr("Placed static collision brushes as one level undo step. Save and compile the map. Later model changes "
							   "require updating these brushes."));
	return true;
}
void ModelEditorDialog::handoffCollision(bool place)
{
	if (m_working)
	{
		return;
	}
	m_preview->pause();
	QString error;
	if (place)
	{
		if (!placeCollision(collisionRequest(), &error))
		{
			m_status->setText(error);
		}
	}
	else
	{
		const auto path = QFileDialog::getSaveFileName(this, Text::tr("Export Collision Map"), {}, Text::tr("Quake-family map (*.map)"));
		if (!path.isEmpty() && !exportCollision(path, collisionRequest(), true, &error))
		{
			m_status->setText(error);
		}
	}
}
} // namespace vibestudio
