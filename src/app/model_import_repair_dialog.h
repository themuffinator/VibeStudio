#pragma once
#include "core/model_import_repair.h"
#include <QDialog>

class QLineEdit;
namespace vibestudio
{
class ModelImportRepairDialog final : public QDialog
{
  public:
	explicit ModelImportRepairDialog(const ModelImportRepairPlan &plan, bool highContrast, QWidget *parent = nullptr);
	QString outputPath() const;

  private:
	QLineEdit *m_output = nullptr;
};
} // namespace vibestudio
