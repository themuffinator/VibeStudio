#include "app/model_assembly_dialog.h"
#include "app/model_q3_animation_dialog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>

namespace vibestudio
{
bool ModelAssemblyDialog::applyQ3Animation(const std::optional<ModelAssemblyQ3Animation> &animation, QString *error)
{
	auto candidate = m_document;
	if (!candidate.setQ3Animation(animation, error) || !validateModelAssemblyQ3Animation(candidate.assembly(), m_resolved, error))
		return false;
	return adopt(std::move(candidate), error);
}
bool ModelAssemblyDialog::exportQ3Animation(const QString &path, bool overwrite, QString *error)
{
	play(false);
	const auto assembly = m_document.assembly();
	const auto resolved = m_resolved;
	const auto source = m_document.recoverySource().isEmpty() ? m_document.path() : m_document.recoverySource();
	const auto archive = m_materialSource.archive;
	if (!performWork(
			QCoreApplication::translate("ModelAssemblyDialog", "Export Quake III Animation"),
			[&](QString *failure, const ModelWorkControl &control) {
				return exportModelAssemblyQ3Animation(assembly, resolved, path, source, overwrite, false, failure, control, archive);
			},
			error, true))
		return false;
	report(QCoreApplication::translate("ModelAssemblyDialog", "Exported native animation configuration: %1").arg(path));
	return true;
}
void ModelAssemblyDialog::chooseQ3Animation()
{
	if (m_working)
		return;
	play(false);
	ModelQ3AnimationDialog dialog(m_document.assembly(), m_resolved, this);
	if (dialog.exec() != QDialog::Accepted)
		return;
	QString error;
	if (!applyQ3Animation(dialog.animation(), &error))
	{
		report(error);
		return;
	}
	if (!dialog.exportRequested())
		return;
	const auto path = QFileDialog::getSaveFileName(this, QCoreApplication::translate("ModelAssemblyDialog", "Export Quake III Animation"),
												   QDir(m_document.directory()).filePath(QStringLiteral("animation.cfg")),
												   QCoreApplication::translate("ModelAssemblyDialog", "Animation configuration (*.cfg)"));
	if (!path.isEmpty() && !exportQ3Animation(path, QFileInfo::exists(path), &error))
		report(error);
}
} // namespace vibestudio
