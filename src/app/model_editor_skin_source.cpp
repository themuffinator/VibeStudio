#include "app/model_editor_dialog.h"
#include "app/model_skin_source_dialog.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QLabel>

namespace vibestudio
{
void ModelEditorDialog::chooseMdlPackageSkin()
{
	const auto source = m_materialSource;
	if (!source.archive || !source.archive->isOpen())
	{
		return;
	}
	ModelSkinSourceDialog picker(source.archive->entries(), m_mdlSkin->currentIndex(), m_mdlMember->currentIndex(),
								 m_mdlSkinDuration->value(), this);
	if (picker.exec() != QDialog::Accepted)
	{
		return;
	}
	if (source.archive != m_materialSource.archive || source.revision != m_materialSource.revision ||
		source.paletteId != m_materialSource.paletteId)
	{
		m_status->setText(QCoreApplication::translate("VibeStudioModelEditor",
													  "The package changed while choosing a skin. Open the texture picker again."));
		return;
	}
	QString error;
	if (!importMdlSkinFromPackage(picker.reference(), picker.kind(), &error))
	{
		m_status->setText(error);
	}
}

bool ModelEditorDialog::importMdlSkinFromPackage(const ModelSkinSourceReference &reference, ModelEditKind kind, QString *error)
{
	const auto source = m_materialSource;
	if (!source.archive || !source.archive->isOpen() ||
		(kind != ModelEditKind::AddMdlSkin && kind != ModelEditKind::ReplaceMdlSkinMember && kind != ModelEditKind::AppendMdlSkinMember))
	{
		if (error)
		{
			*error = QCoreApplication::translate("VibeStudioModelEditor", "Choose an open package and a skin import operation.");
		}
		return false;
	}
	ModelEdit edit;
	edit.kind = kind;
	edit.selection = m_document.selection();
	edit.mdlSkinSlot = m_mdlSkin->currentIndex();
	edit.mdlSkinMember = m_mdlMember->currentIndex();
	edit.mdlDuration = m_mdlSkinDuration->value();
	ModelSkinSourceReceipt receipt;
	if (!performWork(
			QCoreApplication::translate("VibeStudioModelEditor", "Import Package Skin"),
			[source, reference, edit, &receipt](ModelDocument &candidate, QString *failure, const ModelWorkControl &control) mutable
			{
				return readModelSkinSource(*source.archive, reference, modelMdlPreviewPalette(candidate.mesh()), source.paletteId,
										   &edit.mdlSkin, &receipt, failure, control) &&
					   candidate.edit(edit, failure, control);
			},
			error))
	{
		return false;
	}
	refresh();
	if (kind == ModelEditKind::AddMdlSkin)
	{
		m_mdlSkin->setCurrentIndex(m_mdlSkin->count() - 1);
	}
	if (kind == ModelEditKind::AppendMdlSkinMember)
	{
		m_mdlMember->setCurrentIndex(m_mdlMember->count() - 1);
	}
	const auto palette =
		receipt.paletteKind == QStringLiteral("embedded")  ? QCoreApplication::translate("VibeStudioModelEditor", "embedded image palette")
		: receipt.paletteKind == QStringLiteral("package") ? receipt.paletteSource
														   : QCoreApplication::translate("VibeStudioModelEditor", "model fallback palette");
	m_status->setText(QCoreApplication::translate("VibeStudioModelEditor", "Imported entry %1: %2 (%3). Undo restores the previous skin.")
						  .arg(receipt.entryIndex)
						  .arg(receipt.path, palette));
	return true;
}
} // namespace vibestudio
