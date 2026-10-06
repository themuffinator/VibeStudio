#pragma once

#include "core/package_draft_storage.h"
#include <QCoreApplication>
#include <QDialog>
#include <atomic>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QThread;

namespace vibestudio {

class PackageDraftStorageDialog final : public QDialog {
	Q_DECLARE_TR_FUNCTIONS(PackageDraftStorageDialog)
public:
	explicit PackageDraftStorageDialog(QString path = {}, QWidget* parent = nullptr);
	~PackageDraftStorageDialog() override;
	[[nodiscard]] bool busy() const;
	void reload(const QString& notice = {});
	void reject() override;
private:
	void start(std::function<void()> work, std::function<void()> complete);
	void updateControls();
	void updateUsage();
	void compact();
	QString m_path;
	PackageDraftStorageReview m_review;
	std::shared_ptr<std::atomic_bool> m_cancel;
	QThread* m_thread = nullptr;
	QLineEdit* m_directory = nullptr;
	QLabel* m_usage = nullptr;
	QLabel* m_status = nullptr;
	QPlainTextEdit* m_details = nullptr;
	QProgressBar* m_progress = nullptr;
	QPushButton* m_choose = nullptr;
	QPushButton* m_compact = nullptr;
	QPushButton* m_refresh = nullptr;
	bool m_closeRequested = false;
};

} // namespace vibestudio
