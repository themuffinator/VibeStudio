#include "app/model_editor_dialog.h"
#include "app/model_import_repair_dialog.h"
#include "app/model_viewport.h"
#include <QCoreApplication>
#include <QFileDialog>
#include <QLabel>
#include <QPushButton>
#include <memory>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelImportRepair)
};
} // namespace
void ModelEditorDialog::chooseRepairImport()
{
	const auto path = QFileDialog::getOpenFileName(this, Text::tr("Repair Import"), {},
												   Text::tr("Editable and native models (*.mesh.json *.mdl *.md2 *.md3)"));
	if (path.isEmpty())
		return;
	QString error;
	if (!repairSource(path, &error) && !error.isEmpty())
		m_status->setText(error);
}
bool ModelEditorDialog::repairSource(const QString &path, QString *error)
{
	QString localError;
	if (!error)
		error = &localError;
	error->clear();
	if (m_working || path.isEmpty())
		return false;
	auto plan = std::make_shared<ModelImportRepairPlan>();
	if (!performWork(
			Text::tr("Prepare Import Repair"),
			[path, plan](ModelDocument &, QString *failure, const ModelWorkControl &control) {
				return prepareModelImportRepair(path, plan.get(), failure, control);
			},
			error))
		return false;
	ModelImportRepairDialog dialog(*plan, m_highContrast, this);
	if (dialog.exec() != QDialog::Accepted || !maybeSave())
		return false;
	const auto output = dialog.outputPath();
	if (!performWork(
			Text::tr("Save Repaired Copy"),
			[plan, output](ModelDocument &candidate, QString *failure, const ModelWorkControl &control) {
				return saveModelImportRepair(*plan, output, &candidate, failure, control);
			},
			error, true))
		return false;
	retireRecovery();
	m_recoverySourcePath = m_document.path();
	m_recoverySourceHash = m_document.sourceFingerprint();
	m_previewMaterialSlots.clear();
	m_lastSkinBindings.clear();
	m_skinBindingsDetails->setEnabled(false);
	m_materialLoader->reset();
	m_surfaceImages.clear();
	m_materialAssets = {};
	refresh(false);
	m_status->setText(Text::tr("Saved and opened repaired copy: %1").arg(m_document.path()));
	return true;
}
} // namespace vibestudio
