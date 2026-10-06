#pragma once

#include "core/package_selection.h"
#include "core/package_staging.h"
#include <QCoreApplication>
#include <QDialog>
#include <atomic>

class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QSpinBox;
class QPushButton;
class QTableWidget;
class QThread;

namespace vibestudio {
class PackageSubsetDialog final : public QDialog {
	Q_DECLARE_TR_FUNCTIONS(PackageSubsetDialog)
public:
	PackageSubsetDialog(std::shared_ptr<const PackageArchiveReader> archive, PackageSelectionRequest selection, QWidget* parent = nullptr);
	~PackageSubsetDialog() override;
	[[nodiscard]] bool busy() const;
	void reject() override;
	std::function<void(const PackageWriteReport&)> exported;
private:
	struct Work;
	void prepare();
	void exportSubset();
	void start(std::function<void()> work, std::function<void()> complete);
	void updateControls();
	void showPage();
	std::shared_ptr<const PackageArchiveReader> m_archive;
	PackageSelectionRequest m_selection;
	PackageStagingModel m_plan;
	PackageSubsetReview m_review;
	std::shared_ptr<Work> m_work;
	QThread* m_thread = nullptr;
	bool m_ready = false, m_closeRequested = false;
	QLabel* m_status = nullptr;
	QTableWidget* m_files = nullptr;
	QSpinBox* m_page = nullptr;
	QWidget* m_pageControls = nullptr;
	QPlainTextEdit* m_details = nullptr;
	QProgressBar* m_progress = nullptr;
	QPushButton* m_export = nullptr;
	QPushButton* m_cancel = nullptr;
};
} // namespace vibestudio
