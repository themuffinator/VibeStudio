#pragma once

#include "core/package_copy_store.h"
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
class PackageCopySessionsDialog final : public QDialog {
	Q_DECLARE_TR_FUNCTIONS(PackageCopySessionsDialog)
public:
	explicit PackageCopySessionsDialog(QString directory, QWidget* parent = nullptr);
	~PackageCopySessionsDialog() override;
	[[nodiscard]] bool busy() const;
	void reload(const QString& notice = {});
	void reject() override;
private:
	struct Progress { std::atomic_bool cancel = false; std::atomic<qint64> completed = 0, total = 0; };
	void start(std::function<void(const PackageReadControl&)> operation, std::function<void()> complete);
	void updateSelection();
	void updateProgress();
	void discardSelected();
	void editLimits();
	void cancelOperation();
	QString m_directory;
	PackageCopyInventory m_inventory;
	std::shared_ptr<Progress> m_work = std::make_shared<Progress>();
	QThread* m_thread = nullptr;
	QLabel* m_status = nullptr;
	QLabel* m_usage = nullptr;
	QLabel* m_sharedUsage = nullptr;
	QTableWidget* m_table = nullptr;
	QPlainTextEdit* m_details = nullptr;
	QPushButton *m_discard = nullptr, *m_refresh = nullptr, *m_cancel = nullptr, *m_limits = nullptr;
	QProgressBar* m_progress = nullptr;
	bool m_closeRequested = false, m_reducedMotion = false;
};
} // namespace vibestudio
