#pragma once

#include "core/package_recovery.h"

#include <QCoreApplication>
#include <QDialog>
#include <atomic>

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QProgressBar;
class QTableWidget;
class QThread;

namespace vibestudio {

struct PackageRecoveryReport;

class PackageRecoveryDialog final : public QDialog {
	Q_DECLARE_TR_FUNCTIONS(PackageRecoveryDialog)
public:
	explicit PackageRecoveryDialog(QString directory, QWidget* parent = nullptr, QStringList publicationDirectories = {});
	~PackageRecoveryDialog() override;
	[[nodiscard]] bool busy() const;
	std::function<void(const PackageRecoveryInfo&)> restore;
	std::function<void()> preferencesChanged;
	std::function<void(const QString&)> openPublicationOutput;
	std::function<void(const PackageRecoveryReport&)> publicationFinished;
	void reload();
	void reject() override;

private:
	void start(std::function<void()> operation, std::function<void()> complete);
	void updateSelection();
	void updateUsage();
	void discardSelected();
	QString m_directory;
	PackageRecoveryInventory m_inventory;
	std::shared_ptr<std::atomic_bool> m_cancel;
	QThread* m_thread = nullptr;
	QLabel* m_status = nullptr;
	QLabel* m_usage = nullptr;
	QTableWidget* m_table = nullptr;
	QPlainTextEdit* m_details = nullptr;
	QPushButton* m_restore = nullptr;
	QPushButton* m_discard = nullptr;
	QPushButton* m_refresh = nullptr;
	QProgressBar* m_progress = nullptr;
	bool m_closeRequested = false;
};

} // namespace vibestudio
