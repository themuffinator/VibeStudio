#pragma once

#include "core/package_import_store.h"

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

class PackageImportDialog final : public QDialog {
	Q_DECLARE_TR_FUNCTIONS(PackageImportDialog)
public:
	explicit PackageImportDialog(QString directory, QWidget* parent = nullptr);
	~PackageImportDialog() override;
	[[nodiscard]] bool busy() const;
	void reload(const QString& notice = {});
	void reject() override;
private:
	void start(std::function<void()> operation, std::function<void()> complete);
	void updateUsage();
	void updateSelection();
	void discardSelected();
	void releaseLock();
	QString m_directory;
	PackageImportInventory m_inventory;
	std::shared_ptr<std::atomic_bool> m_cancel;
	QThread* m_thread = nullptr;
	QLabel* m_status = nullptr;
	QLabel* m_usage = nullptr;
	QTableWidget* m_table = nullptr;
	QPlainTextEdit* m_details = nullptr;
	QPushButton* m_discard = nullptr;
	QPushButton* m_refresh = nullptr;
	QPushButton* m_unlock = nullptr;
	QProgressBar* m_progress = nullptr;
	bool m_closeRequested = false;
};

} // namespace vibestudio
