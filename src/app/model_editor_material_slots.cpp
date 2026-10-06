#include "app/model_editor_dialog.h"
#include "app/model_material_slots_dialog.h"
#include <QComboBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <algorithm>

namespace vibestudio
{
void ModelEditorDialog::addMaterialSlotControls(QFormLayout *surface)
{
	m_materialSlot = new QComboBox;
	m_materialSlot->setObjectName("meshMaterialSlot");
	m_materialSlot->setLayoutDirection(Qt::LeftToRight);
	m_materialSlot->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Material preview slot"));
	m_materialSlot->setAccessibleDescription(
		QCoreApplication::translate("VibeStudioModelEditor", "Choose which external slot this surface previews. This does not change the "
															 "saved material order. Assign Material edits this slot."));
	m_materialSlot->setToolTip(m_materialSlot->accessibleDescription());
	m_materialSlot->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_materialSlot->setMinimumContentsLength(16);
	surface->addRow(QCoreApplication::translate("VibeStudioModelEditor", "Preview slot"), m_materialSlot);
	m_manageMaterialSlots = new QPushButton(QCoreApplication::translate("VibeStudioModelEditor", "Manage Material Slots…"));
	m_manageMaterialSlots->setObjectName("manageMeshMaterialSlots");
	m_manageMaterialSlots->setAccessibleName(m_manageMaterialSlots->text());
	m_manageMaterialSlots->setAccessibleDescription(
		QCoreApplication::translate("VibeStudioModelEditor", "Add, replace, remove or reorder this surface's external material bindings."));
	m_manageMaterialSlots->setToolTip(m_manageMaterialSlots->accessibleDescription());
	surface->addRow(m_manageMaterialSlots);
	connect(m_materialSlot, &QComboBox::currentIndexChanged, this, [this](int slot) {
		if (m_refreshing || m_working || slot < 0 || m_document.mesh().surfaces.isEmpty())
			return;
		const auto &item = m_document.mesh().surfaces[m_document.selection().surface];
		m_previewMaterialSlots.insert(item.name, slot);
		m_material->setText(item.skinPaths.value(slot));
		refreshMaterial();
	});
	connect(m_manageMaterialSlots, &QPushButton::clicked, this, [this] {
		if (m_working || !supportsModelMaterialSlots(m_document.mesh()))
			return;
		const auto revision = m_document.revisionFingerprint();
		ModelMaterialSlotsDialog dialog(m_document.mesh(), m_document.selection(), this);
		if (dialog.exec() != QDialog::Accepted)
			return;
		if (revision != m_document.revisionFingerprint())
		{
			m_status->setText(QCoreApplication::translate(
				"VibeStudioModelEditor", "The model changed while reviewing material slots. Reopen Manage Material Slots."));
			return;
		}
		QString error;
		if (!applyEdit(dialog.edit(), &error))
			m_status->setText(error);
	});
}
QHash<int, int> ModelEditorDialog::materialPreviewSlots() const
{
	QHash<int, int> result;
	if (!supportsModelMaterialSlots(m_document.mesh()))
		return result;
	for (int i = 0; i < m_document.mesh().surfaces.size(); ++i)
	{
		const auto &surface = m_document.mesh().surfaces[i];
		if (!surface.skinPaths.isEmpty())
			result.insert(i, std::clamp(m_previewMaterialSlots.value(surface.name, 0), 0, int(surface.skinPaths.size()) - 1));
	}
	return result;
}
void ModelEditorDialog::refreshMaterialSlots()
{
	const auto &mesh = m_document.mesh();
	const bool external = supportsModelMaterialSlots(mesh);
	QHash<QString, int> retained;
	for (const auto &surface : mesh.surfaces)
		if (external && !surface.skinPaths.isEmpty())
			retained.insert(surface.name, std::clamp(m_previewMaterialSlots.value(surface.name, 0), 0, int(surface.skinPaths.size()) - 1));
	m_previewMaterialSlots = std::move(retained);
	const auto &surface = mesh.surfaces[m_document.selection().surface];
	const QSignalBlocker block(m_materialSlot);
	m_materialSlot->clear();
	if (!external)
		m_materialSlot->addItem(QCoreApplication::translate("VibeStudioModelEditor", "Embedded skins (Animation)"));
	else
		for (int i = 0; i < surface.skinPaths.size(); ++i)
		{
			// QComboBox's style can use the application's bidi paragraph direction
			// even with a left-to-right widget. Keep the technical ordinal first.
			m_materialSlot->addItem(QStringLiteral("\u200e%1: %2").arg(i).arg(surface.skinPaths[i]), i);
			m_materialSlot->setItemData(i, surface.skinPaths[i], Qt::ToolTipRole);
		}
	if (m_materialSlot->count() == 0)
		m_materialSlot->addItem(QCoreApplication::translate("VibeStudioModelEditor", "Unassigned"));
	const int slot = m_previewMaterialSlots.value(surface.name, 0);
	m_materialSlot->setCurrentIndex(slot);
	m_materialSlot->setEnabled(external && !surface.skinPaths.isEmpty());
	m_manageMaterialSlots->setEnabled(external);
	m_manageMaterialSlots->setAccessibleDescription(
		external ? QCoreApplication::translate("VibeStudioModelEditor",
											   "Add, replace, remove or reorder this surface's external material bindings.")
				 : QCoreApplication::translate("VibeStudioModelEditor", "Embedded MDL skins use the Animation inspector's skin controls."));
	m_manageMaterialSlots->setToolTip(m_manageMaterialSlots->accessibleDescription());
	m_material->setText(surface.skinPaths.value(slot));
	m_material->setEnabled(external);
	m_assignMaterial->setEnabled(external);
	m_showMaterial->setEnabled(external && !surface.skinPaths.isEmpty());
}
} // namespace vibestudio
