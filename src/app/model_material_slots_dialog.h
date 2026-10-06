#pragma once
#include "core/model_document.h"
#include "core/model_material_slots.h"
#include <QDialog>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace vibestudio
{
class ModelMaterialSlotsDialog final : public QDialog
{
  public:
	explicit ModelMaterialSlotsDialog(const ModelMesh &mesh, const ModelSelection &selection, QWidget *parent = nullptr);
	ModelEdit edit() const;

  private:
	ModelSelection m_selection;
	QStringList m_original, m_materials;
	QListWidget *m_list = nullptr;
	QLineEdit *m_path = nullptr;
	QLabel *m_summary = nullptr;
	QPushButton *m_add = nullptr, *m_replace = nullptr, *m_remove = nullptr, *m_up = nullptr, *m_down = nullptr, *m_clear = nullptr,
				*m_apply = nullptr;
	void change(ModelMaterialSlotAction action);
	void populate(int selected);
	void refresh();
};
} // namespace vibestudio
