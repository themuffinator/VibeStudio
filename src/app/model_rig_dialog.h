#pragma once

#include "core/model_document.h"
#include <QDialog>
#include <array>

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;

namespace vibestudio
{
class ModelRigDialog final : public QDialog
{
  public:
	ModelRigDialog(const ModelMesh &mesh, const ModelSelection &selection, const QString &joint, QWidget *parent = nullptr);
	ModelEdit edit() const;

  private:
	ModelMesh m_mesh;
	ModelSelection m_selection;
	QFormLayout *m_form = nullptr;
	QComboBox *m_operation = nullptr, *m_joint = nullptr, *m_parent = nullptr;
	QLineEdit *m_name = nullptr;
	QWidget *m_origin = nullptr;
	std::array<QDoubleSpinBox *, 3> m_axes{};
	QDoubleSpinBox *m_weight = nullptr;
	QLabel *m_summary = nullptr;
	void refresh();
};
} // namespace vibestudio
