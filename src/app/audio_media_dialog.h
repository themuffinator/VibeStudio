#pragma once
#include "core/audio_media.h"
#include <QDialog>
#include <memory>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTabWidget;
class QThread;
class QTreeWidget;

namespace vibestudio
{
class AudioWaveformView;
struct AudioMediaReview;
class AudioMediaDialog final : public QDialog {
	Q_OBJECT
  public:
	AudioMediaDialog(const AudioSession &session, const QString &source = {}, QWidget *parent = nullptr);
	~AudioMediaDialog() override;
	[[nodiscard]] AudioMediaEdit edit() const;
	[[nodiscard]] AudioMediaCandidate candidate() const { return m_candidate; }
	[[nodiscard]] bool isBusy() const { return m_thread != nullptr; }
	[[nodiscard]] bool isReviewed() const { return m_ready; }
	void review();
	void cancelReview();
	void accept() override;
	void reject() override;

  private:
	void start(bool inspect);
	void invalidate();
	void selectionChanged();
	void refresh();
	AudioSession m_session;
	AudioMediaInventory m_inventory;
	AudioMediaCandidate m_candidate;
	QString m_initialSource;
	std::shared_ptr<AudioMediaReview> m_work;
	QThread *m_thread = nullptr;
	bool m_ready = false, m_closing = false;
	QWidget *m_fields = nullptr;
	QTreeWidget *m_sources = nullptr;
	QComboBox *m_operation = nullptr;
	QLineEdit *m_name = nullptr, *m_path = nullptr;
	QPlainTextEdit *m_details = nullptr;
	QCheckBox *m_resample = nullptr;
	QPushButton *m_browse = nullptr, *m_review = nullptr, *m_cancel = nullptr;
	QLabel *m_summary = nullptr;
	QProgressBar *m_progress = nullptr;
	QDialogButtonBox *m_buttons = nullptr;
	QTabWidget *m_waves = nullptr;
	AudioWaveformView *m_before = nullptr, *m_after = nullptr;
};
} // namespace vibestudio
