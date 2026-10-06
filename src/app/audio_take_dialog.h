#pragma once
#include "app/audio_capture.h"
#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QThread;
namespace vibestudio
{
struct AudioTakeDialogWork;
class AudioTakeDialog final : public QDialog {
	Q_OBJECT
  public:
	explicit AudioTakeDialog(int sampleRate, QString track, QString sessionPath, qint64 position,
	                         QWidget *parent = nullptr, AudioCaptureDeviceFactory factory = {});
	~AudioTakeDialog() override;
	bool inspectTake(const QString &path);
	[[nodiscard]] bool busy() const;
	[[nodiscard]] const AudioProject &importedAudio() const { return m_imported; }
	[[nodiscard]] qint64 importPosition() const { return m_importPosition; }
	std::function<void()> beforeRecord;

  protected:
	void closeEvent(QCloseEvent *) override;
	void reject() override;

  private:
	void refresh();
	void record();
	void review(const AudioTakeInfo &info);
	void importTake();
	void placement();
	void launch(std::function<void(AudioTakeDialogWork &)> perform, bool importing);
	AudioCapture *m_capture = nullptr;
	int m_rate;
	QString m_track, m_sessionPath;
	AudioTakeInfo m_info;
	AudioProject m_imported;
	qint64 m_importPosition = 0;
	std::shared_ptr<AudioTakeDialogWork> m_work;
	QThread *m_thread = nullptr;
	bool m_closePending = false;
	QTabWidget *m_tabs = nullptr;
	QWidget *m_recordForm = nullptr, *m_reviewForm = nullptr;
	QComboBox *m_input = nullptr, *m_buffer = nullptr;
	QSpinBox *m_inputChannels = nullptr, *m_latency = nullptr;
	QLineEdit *m_name = nullptr, *m_path = nullptr, *m_channels = nullptr, *m_storedChannels = nullptr;
	QDoubleSpinBox *m_position = nullptr, *m_first = nullptr, *m_end = nullptr, *m_place = nullptr;
	QCheckBox *m_arm = nullptr, *m_recover = nullptr, *m_compensate = nullptr;
	QLabel *m_status = nullptr, *m_details = nullptr;
	QPlainTextEdit *m_fileDetails = nullptr;
	QProgressBar *m_progress = nullptr;
	QPushButton *m_record = nullptr, *m_stop = nullptr, *m_import = nullptr;
};
} // namespace vibestudio
