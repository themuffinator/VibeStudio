#pragma once

#include "core/package_publication.h"

#include <QCoreApplication>
#include <QDialog>
#include <atomic>

class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QThread;

namespace vibestudio {

class PackagePublicationDialog final : public QDialog {
	Q_DECLARE_TR_FUNCTIONS(PackagePublicationDialog)
public:
	explicit PackagePublicationDialog(QStringList directories, QWidget* parent = nullptr);
	~PackagePublicationDialog() override;
	[[nodiscard]] bool busy() const;
	void reload();
	void reject() override;
	std::function<void(const QString&)> openOutput;
	std::function<void(const PackageRecoveryReport&)> recoveryFinished;
private:
	struct Progress;
	PackageReadControl readControl() const;
	void start(std::function<void()> work, std::function<void()> complete);
	void updateSelection();
	void updateControls();
	void updateDetails();
	void verifySelected(const QString& backup = {});
	void finishSelected();
	const PackagePublicationJournalInfo* selected() const;
	QString stateText() const;
	QStringList m_directories, m_knownDirectories;
	PackagePublicationInventory m_inventory;
	PackageRecoveryReport m_review;
	QString m_confirmedBackup;
	bool m_needsBackup = false, m_closeRequested = false;
	std::shared_ptr<std::atomic_bool> m_cancel;
	std::shared_ptr<Progress> m_progressState;
	QThread* m_thread = nullptr;
	QLabel* m_status = nullptr;
	QPlainTextEdit* m_folders = nullptr;
	QPlainTextEdit* m_details = nullptr;
	QTableWidget* m_table = nullptr;
	QProgressBar* m_progress = nullptr;
	QPushButton* m_choose = nullptr;
	QPushButton* m_known = nullptr;
	QPushButton* m_verify = nullptr;
	QPushButton* m_backup = nullptr;
	QPushButton* m_finish = nullptr;
	QPushButton* m_open = nullptr;
	QPushButton* m_refresh = nullptr;
};

} // namespace vibestudio
