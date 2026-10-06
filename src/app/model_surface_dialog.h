#pragma once
#include "core/model_document.h"
#include <QDialog>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;

namespace vibestudio
{
class ModelSurfaceDialog final : public QDialog
{
  public:
	explicit ModelSurfaceDialog(const ModelMesh &mesh, const ModelSelection &selection, QWidget *parent = nullptr);
	ModelEdit edit() const;

  protected:
	void changeEvent(QEvent *event) override;

  private:
	ModelMesh m_mesh;
	ModelSelection m_selection;
	QFormLayout *m_form = nullptr;
	QComboBox *m_operation = nullptr, *m_target = nullptr;
	QLineEdit *m_name = nullptr;
	QTreeWidget *m_sources = nullptr;
	QCheckBox *m_adopt = nullptr;
	QLabel *m_summary = nullptr;
	QPushButton *m_apply = nullptr;
	void refresh(bool operationChanged = false);
	void sizeColumns();
};
} // namespace vibestudio
