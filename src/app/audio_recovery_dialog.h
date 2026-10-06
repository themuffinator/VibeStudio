#pragma once

#include "core/audio_recovery_store.h"
#include <QDialog>
#include <atomic>
#include <functional>
#include <memory>

class QLabel;
class QPushButton;
class QTableWidget;
class QTextEdit;
class QThread;

namespace vibestudio
{
class AudioRecoveryDialog final : public QDialog {
	Q_OBJECT
  public:
	explicit AudioRecoveryDialog(QString directory, QWidget *parent = nullptr);
	~AudioRecoveryDialog() override;
	std::function<void(const QString &, const QByteArray &, AudioRecoveryKind)> restore;
	void reload();
	[[nodiscard]] bool busy() const { return m_thread != nullptr; }

  private:
	void updateSelection();
	void discardSelected();
	void start(std::function<void()> operation, std::function<void()> complete);
	QString m_directory;
	AudioRecoveryInventory m_inventory;
	QTableWidget *m_table = nullptr;
	QTextEdit *m_details = nullptr;
	QLabel *m_status = nullptr;
	QPushButton *m_restore = nullptr;
	QPushButton *m_discard = nullptr;
	QPushButton *m_refresh = nullptr;
	QThread *m_thread = nullptr;
	std::shared_ptr<std::atomic_bool> m_cancel;
};
} // namespace vibestudio
