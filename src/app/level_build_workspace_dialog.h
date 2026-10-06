#pragma once
#include "core/level_build_workspace.h"
#include <QCoreApplication>
#include <QDialog>
#include <memory>

class QLineEdit;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QThread;
class QTimer;

namespace vibestudio {
class LevelBuildWorkspaceDialog final : public QDialog {
	Q_DECLARE_TR_FUNCTIONS(LevelBuildWorkspaceDialog)
  public:
	LevelBuildWorkspaceDialog(const LevelMapDocument& document, std::shared_ptr<const PackageArchiveReader> archive,
							  QWidget* parent = nullptr);
	~LevelBuildWorkspaceDialog() override;
	void setDestination(const QString& directory, const QString& mapName);
	void setApplyHandler(std::function<bool(const LevelBuildWorkspace&, QString*)> handler);
	[[nodiscard]] bool isReady() const;
	[[nodiscard]] const LevelBuildWorkspace& workspace() const;
	void accept() override;
	void reject() override;

  private:
	struct Work;
	void prepare();
	void changed();
	LevelMapDocument m_document;
	std::shared_ptr<const PackageArchiveReader> m_archive;
	std::shared_ptr<Work> m_work;
	LevelBuildWorkspace m_result;
	std::function<bool(const LevelBuildWorkspace&, QString*)> m_apply;
	QLineEdit *m_directory = nullptr, *m_name = nullptr;
	QComboBox* m_target = nullptr;
	QSpinBox* m_limit = nullptr;
	QLabel* m_status = nullptr;
	QPlainTextEdit* m_details = nullptr;
	QProgressBar* m_progress = nullptr;
	QPushButton *m_prepare = nullptr, *m_use = nullptr, *m_browse = nullptr;
	QThread* m_thread = nullptr;
	QTimer* m_poll = nullptr;
	bool m_reducedMotion = false;
	bool m_closed = false;
};
} // namespace vibestudio
