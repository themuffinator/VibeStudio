#pragma once

#include "core/package_staging.h"
#include "core/package_wad_groups.h"
#include <QCoreApplication>
#include <QDialog>
#include <atomic>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QThread;

namespace vibestudio {
class PackageWadGroupsDialog final : public QDialog {
	Q_DECLARE_TR_FUNCTIONS(PackageWadGroupsDialog)
public:
	explicit PackageWadGroupsDialog(PackageStagingModel model, QWidget* parent = nullptr);
	~PackageWadGroupsDialog() override;
	bool busy() const;
	void reject() override;
	// The owner checks its document identity/revision before adopting a result.
	std::function<bool(PackageStagingModel&&, const PackageWadGroupEditReview&, QString*)> apply;
private:
	void inspect();
	void reviewEdit();
	void invalidate();
	void updateControls();
	void showPage();
	void start(std::function<void()> work, std::function<void()> complete);
	PackageStagingModel m_original, m_candidate;
	PackageWadGroupInventory m_inventory;
	PackageWadGroupEditReview m_review;
	std::shared_ptr<std::atomic_bool> m_cancelled;
	QThread* m_thread = nullptr;
	bool m_ready = false, m_closeRequested = false;
	QComboBox* m_groups = nullptr;
	QComboBox* m_operation = nullptr;
	QLineEdit* m_target = nullptr;
	QLabel* m_status = nullptr;
	QPlainTextEdit* m_details = nullptr;
	QTableWidget* m_changes = nullptr;
	QSpinBox* m_page = nullptr;
	QProgressBar* m_progress = nullptr;
	QPushButton* m_prepare = nullptr;
	QPushButton* m_apply = nullptr;
	QPushButton* m_cancel = nullptr;
};
} // namespace vibestudio
