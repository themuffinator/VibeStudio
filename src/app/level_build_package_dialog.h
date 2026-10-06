#pragma once
#include "core/level_build_deployment.h"
#include <QCoreApplication>
#include <QDialog>
#include <memory>
class QLineEdit;
class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QThread;
class QTimer;
namespace vibestudio {
class LevelBuildPackageDialog final : public QDialog {
	Q_DECLARE_TR_FUNCTIONS(LevelBuildPackageDialog)
  public:
	explicit LevelBuildPackageDialog(LevelBuildWorkspace workspace, QWidget* parent = nullptr);
	~LevelBuildPackageDialog() override;
	void setOutputPath(const QString& path);
	void setPublishedHandler(std::function<void(const LevelBuildPackageResult&)> handler);
	// Configure before the initial asynchronous review starts.
	void configureDeployment(GameInstallationProfile installation, const QString& gameDirectory = {}, bool launchAfter = false);
	void setDeploymentHandler(std::function<void(const LevelBuildDeploymentResult&)> handler);
	[[nodiscard]] const LevelBuildDeploymentResult& lastDeploymentResult() const;
	[[nodiscard]] bool reviewReady() const;
	[[nodiscard]] bool busy() const;
	[[nodiscard]] const LevelBuildPackageResult& lastResult() const;
	void reject() override;

  private:
	struct Work;
	void start(bool publish);
	void refreshRows();
	void updateControls();
	void setDetails(const QString& text, int warningCount);
	LevelBuildWorkspace m_workspace;
	LevelBuildArtifacts m_review;
	LevelBuildPackageResult m_result;
	std::optional<GameInstallationProfile> m_installation;
	LevelBuildDeploymentPlan m_deploymentReview;
	LevelBuildDeploymentResult m_deploymentResult;
	std::function<void(const LevelBuildDeploymentResult&)> m_deployed;
	std::shared_ptr<Work> m_work;
	std::function<void(const LevelBuildPackageResult&)> m_published;
	QThread* m_thread = nullptr;
	QTimer* m_poll = nullptr;
	QLineEdit* m_output = nullptr;
	QWidget* m_deploymentFields = nullptr;
	QLabel* m_installationLabel = nullptr;
	QLineEdit* m_gameDirectory = nullptr;
	QSpinBox* m_pakSlot = nullptr;
	QCheckBox *m_allowDeployment = nullptr, *m_launch = nullptr;
	QCheckBox *m_source = nullptr, *m_overwrite = nullptr;
	QComboBox* m_compression = nullptr;
	QLabel* m_status = nullptr;
	QPlainTextEdit* m_details = nullptr;
	QPushButton* m_detailsButton = nullptr;
	QProgressBar* m_progress = nullptr;
	QTableWidget* m_files = nullptr;
	QSpinBox* m_page = nullptr;
	QPushButton *m_reviewButton = nullptr, *m_publish = nullptr, *m_cancel = nullptr, *m_browse = nullptr;
	bool m_reducedMotion = false;
};
} // namespace vibestudio
