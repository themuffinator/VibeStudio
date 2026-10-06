#include "app/model_editor_dialog.h"
#include "app/model_skin_source_dialog.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace vibestudio
{
void ModelEditorDialog::addSkinBindingControls(QFormLayout *surface)
{
	m_skinBindingsFile = new QPushButton(QCoreApplication::translate("VibeStudioModelEditor", "Apply .skin File…"));
	m_skinBindingsPackage = new QPushButton(QCoreApplication::translate("VibeStudioModelEditor", "Apply Package .skin…"));
	m_skinBindingsDetails = new QPushButton(QCoreApplication::translate("VibeStudioModelEditor", "Last Skin Import Details…"));
	m_skinBindingsFile->setObjectName(QStringLiteral("applyMeshSkinFile"));
	m_skinBindingsPackage->setObjectName(QStringLiteral("applyMeshPackageSkin"));
	m_skinBindingsDetails->setObjectName(QStringLiteral("meshSkinBindingDetails"));
	const auto help = QCoreApplication::translate("VibeStudioModelEditor",
												  "Match Quake III surface names and replace primary shader paths in one undo step. Every "
												  "surface must be covered. Embedded MDL skins use the Quake MDL inspector.");
	for (auto *button : {m_skinBindingsFile, m_skinBindingsPackage})
	{
		button->setAccessibleName(button->text());
		button->setToolTip(help);
		button->setAccessibleDescription(help);
		surface->addRow(button);
	}
	m_skinBindingsDetails->setAccessibleName(m_skinBindingsDetails->text());
	m_skinBindingsDetails->setAccessibleDescription(QCoreApplication::translate(
		"VibeStudioModelEditor", "Inspect the last successful skin import, including unused bindings and ignored attachment markers."));
	m_skinBindingsDetails->setEnabled(false);
	surface->addRow(m_skinBindingsDetails);
	connect(m_skinBindingsFile, &QPushButton::clicked, this, [this] {
		const auto path =
			QFileDialog::getOpenFileName(this, QCoreApplication::translate("VibeStudioModelEditor", "Apply Skin Assignments"), QString(),
										 QCoreApplication::translate("VibeStudioModelEditor", "Quake III skin assignments (*.skin)"));
		if (path.isEmpty())
		{
			return;
		}
		QString error;
		if (!importSkinBindings(path, &error))
		{
			m_status->setText(error);
		}
	});
	connect(m_skinBindingsPackage, &QPushButton::clicked, this, &ModelEditorDialog::choosePackageSkinBindings);
	connect(m_skinBindingsDetails, &QPushButton::clicked, this, [this] {
		QDialog dialog(this);
		dialog.setObjectName(QStringLiteral("meshSkinBindingDetailsDialog"));
		dialog.setWindowTitle(QCoreApplication::translate("VibeStudioModelEditor", "Last Skin Import Details"));
		dialog.setAccessibleName(dialog.windowTitle());
		auto *layout = new QVBoxLayout(&dialog);
		auto *text = new QPlainTextEdit(m_lastSkinBindings.join(QLatin1Char('\n')));
		text->setReadOnly(true);
		text->setAccessibleName(dialog.windowTitle());
		layout->addWidget(text);
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
		connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
		layout->addWidget(buttons);
		dialog.resize(760, 480);
		dialog.exec();
	});
}
bool ModelEditorDialog::importSkinBindingsWork(std::function<bool(ModelSkinBindingInput *, QString *, const ModelWorkControl &)> read,
											   QString *error)
{
	ModelSkinBindingPlan plan;
	ModelSkinBindingInput input;
	if (!performWork(
			QCoreApplication::translate("VibeStudioModelEditor", "Apply Skin Assignments"),
			[read, &plan, &input](ModelDocument &candidate, QString *failure, const ModelWorkControl &control) {
				if (!read(&input, failure, control) || !planModelSkinBindings(candidate.mesh(), input.bytes, &plan, failure, control))
				{
					return false;
				}
				ModelEdit edit;
				edit.kind = ModelEditKind::ApplySkinBindings;
				edit.skinBindings = input.bytes;
				edit.selection = candidate.selection();
				return candidate.edit(edit, failure, control);
			},
			error))
	{
		return false;
	}
	refresh();
	m_lastSkinBindings = {
		input.entryIndex >= 0
			? QCoreApplication::translate("VibeStudioModelEditor", "Package entry %1: %2").arg(input.entryIndex).arg(input.path)
			: input.path};
	m_lastSkinBindings += modelSkinBindingPlanText(plan);
	m_skinBindingsDetails->setEnabled(true);
	if (std::none_of(plan.assignments.cbegin(), plan.assignments.cend(),
					 [](const auto &item) { return item.previousMaterial != item.material; }))
	{
		m_status->setText(QCoreApplication::translate("VibeStudioModelEditor",
													  "Skin assignments already match the model. No edit was added to undo history."));
		return true;
	}
	m_status->setText(
		QCoreApplication::translate(
			"VibeStudioModelEditor",
			"Skin assignments applied. Surfaces: %1; unused bindings: %2; attachment markers: %3. Undo restores the previous materials.")
			.arg(plan.assignments.size())
			.arg(plan.unusedSurfaces.size())
			.arg(plan.ignoredTags.size()));
	return true;
}
bool ModelEditorDialog::importSkinBindings(const QString &path, QString *error)
{
	return importSkinBindingsWork([path](ModelSkinBindingInput *input, QString *failure,
										 const ModelWorkControl &control) { return readModelSkinBindings(path, input, failure, control); },
								  error);
}
bool ModelEditorDialog::importSkinBindingsFromPackage(const ModelSkinSourceReference &reference, QString *error)
{
	const auto source = m_materialSource;
	if (!source.archive || !source.archive->isOpen())
	{
		if (error)
		{
			*error = QCoreApplication::translate("VibeStudioModelEditor", "Choose an open package before applying skin assignments.");
		}
		return false;
	}
	return importSkinBindingsWork(
		[source, reference](ModelSkinBindingInput *input, QString *failure, const ModelWorkControl &control) {
			return readModelSkinBindings(*source.archive, reference, input, failure, control);
		},
		error);
}
void ModelEditorDialog::choosePackageSkinBindings()
{
	const auto source = m_materialSource;
	if (!source.archive || !source.archive->isOpen())
	{
		return;
	}
	ModelSkinSourceDialog picker(source.archive->entries(), -1, -1, 0, this, ModelSkinSourcePurpose::ShaderBindings);
	if (picker.exec() != QDialog::Accepted)
	{
		return;
	}
	if (source.archive != m_materialSource.archive || source.revision != m_materialSource.revision)
	{
		m_status->setText(
			QCoreApplication::translate("VibeStudioModelEditor", "The package changed while choosing a skin. Open the picker again."));
		return;
	}
	QString error;
	if (!importSkinBindingsFromPackage(picker.reference(), &error))
	{
		m_status->setText(error);
	}
}
} // namespace vibestudio
