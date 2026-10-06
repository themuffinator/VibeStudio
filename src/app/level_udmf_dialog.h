#pragma once
#include "core/level_udmf.h"
#include <QCoreApplication>
#include <QDialog>
#include <functional>
#include <memory>

class QLineEdit;
class QPlainTextEdit;
class QTableView;
class QLabel;
class QProgressBar;
class QDialogButtonBox;
namespace vibestudio {
class UdmfPropertyModel;
struct UdmfEditTask;
class LevelUdmfDialog final : public QDialog {
	Q_DECLARE_TR_FUNCTIONS(LevelUdmfDialog)
  public:
	explicit LevelUdmfDialog(const LevelMapDocument& document, QWidget* parent = nullptr);
	void setApplyHandler(std::function<bool(const LevelMapDocument&, QString*)> handler);
	void reject() override;

  private:
	void loadObject();
	void stage(bool remove);
	void apply();
	void setBusy(bool busy);
	LevelMapDocument m_source;
	std::function<bool(const LevelMapDocument&, QString*)> m_apply;
	std::shared_ptr<UdmfEditTask> m_task;
	UdmfPropertyModel* m_model;
	QWidget* m_controls;
	QLineEdit* m_object;
	QLineEdit* m_key;
	QPlainTextEdit* m_literal;
	QTableView* m_table;
	QLabel* m_status;
	QProgressBar* m_progress;
	QDialogButtonBox* m_buttons;
};
} // namespace vibestudio
