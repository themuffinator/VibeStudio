#include "app/model_editor_dialog.h"
#include "app/model_surface_dialog.h"

#include <QCoreApplication>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>

namespace vibestudio
{
void ModelEditorDialog::addSurfaceControls(QFormLayout *surface)
{
	auto *manage = new QPushButton(QCoreApplication::translate("VibeStudioModelEditor", "Manage Surfaces…"));
	manage->setObjectName("manageMeshSurfaces");
	manage->setAccessibleName(manage->text());
	manage->setAccessibleDescription(QCoreApplication::translate(
		"VibeStudioModelEditor", "Rename, separate, move, duplicate, join or delete surfaces across every animation pose."));
	manage->setToolTip(manage->accessibleDescription());
	surface->addRow(manage);
	connect(manage, &QPushButton::clicked, this, [this] {
		if (m_working || m_document.mesh().surfaces.isEmpty())
			return;
		const auto revision = m_document.revisionFingerprint();
		ModelSurfaceDialog dialog(m_document.mesh(), m_document.selection(), this);
		if (dialog.exec() != QDialog::Accepted)
			return;
		if (revision != m_document.revisionFingerprint())
		{
			m_status->setText(QCoreApplication::translate("VibeStudioModelEditor",
														  "The model changed while reviewing surfaces. Reopen Manage Surfaces."));
			return;
		}
		QString error;
		if (!applyEdit(dialog.edit(), &error))
			m_status->setText(error);
	});
}
} // namespace vibestudio
