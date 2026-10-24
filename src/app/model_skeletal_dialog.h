#pragma once

#include "core/model_document.h"
#include <QDialog>
#include <array>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace vibestudio
{
class ModelSkeletalDialog final : public QDialog
{
  public:
	ModelSkeletalDialog(const ModelMesh &mesh, const ModelSelection &selection, const QString &joint, int frame, QWidget *parent = nullptr);
	ModelEdit edit() const;

  private:
	ModelMesh m_mesh;
	ModelSelection m_selection;
	QFormLayout *m_form = nullptr;
	QComboBox *m_operation = nullptr, *m_clip = nullptr, *m_joint = nullptr, *m_sourceClip = nullptr, *m_space = nullptr;
	QLineEdit *m_name = nullptr;
	QSpinBox *m_first = nullptr, *m_last = nullptr, *m_count = nullptr, *m_sourceFrame = nullptr;
	QDoubleSpinBox *m_rate = nullptr;
	QCheckBox *m_loop = nullptr, *m_children = nullptr;
	std::array<QWidget *, 3> m_vectors{};
	std::array<std::array<QDoubleSpinBox *, 3>, 3> m_axes{};
	QLabel *m_summary = nullptr;
	QPushButton *m_apply = nullptr;
	void refresh(bool resetFields);
};
} // namespace vibestudio
