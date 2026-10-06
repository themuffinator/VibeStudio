#pragma once
#include "app/audio_recording.h"
#include "app/audio_session_playback.h"
#include "core/audio_recording_import.h"
#include "core/audio_recording_review.h"
#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QMessageBox;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QThread;
class QTreeWidget;
namespace vibestudio
{
struct AudioRecordingDialogWork;
class AudioRecordingMeters;
class AudioRecordingDialog final : public QDialog {
	Q_OBJECT
  public:
	explicit AudioRecordingDialog(AudioSession session, QString sessionPath, qint64 first, qint64 end,
	                              QWidget *parent = nullptr, AudioDuplexDeviceFactory device = {},
	                              AudioCaptureStorageFactory storage = {}, AudioRecordingGate permission = {},
	                              AudioRecordingGate beforeStart = {}, AudioStreamDeviceFactory auditionDevice = {});
	~AudioRecordingDialog() override;
	bool inspectRecording(const QString &directory);
	bool openReview(const QString &path);
	bool saveReview(const QString &path, bool overwrite = false);
	[[nodiscard]] bool reviewModified() const;
	[[nodiscard]] const AudioProjectIdentity &reviewIdentity() const { return m_reviewIdentity; }
	[[nodiscard]] bool busy() const;
	[[nodiscard]] const AudioRecordingImportResult &imported() const { return m_imported; }
	[[nodiscard]] AudioRecordingImportRequest importRequest() const;

  protected:
	void closeEvent(QCloseEvent *) override;
	void reject() override;

  private:
	enum class WorkKind { Review, Import, Audition, OpenReview, SaveReview };
	void refreshDevices();
	void refresh();
	void loadArm();
	void saveArm();
	QString recordingProblem() const;
	void loadTake();
	void saveTake();
	void editCompSection(bool replace);
	void loadCompSection();
	void refreshCompSections();
	void record();
	void review(const AudioRecordingInfo &info);
	void restoreReview(const AudioRecordingReviewResult &opened);
	void confirmReviewChange(std::function<void()> proceed);
	void importTakes();
	void launch(std::function<void(AudioRecordingDialogWork &)> operation, WorkKind kind);
	void createAuditionControls(QFormLayout *reviewForm);
	void refreshAudition();
	void audition(bool currentSection);
	AudioRecordingImportRequest auditionRequest(bool currentSection) const;
	void invalidateAudition();
	void finishDialog(int result);
	AudioSession m_session;
	QString m_sessionPath, m_problem;
	AudioRecording *m_recording = nullptr;
	AudioSessionPlayback *m_audition = nullptr;
	AudioRecordingGate m_beforePlayback;
	quint64 m_auditionEpoch = 0;
	bool m_auditionWaiting = false, m_closingOutput = false, m_auditionReady = false, m_auditionSection = false;
	qint64 m_auditionFirst = 0, m_auditionEnd = 0;
	QWidget *m_auditionForm = nullptr;
	QComboBox *m_auditionOutput = nullptr, *m_auditionBuffer = nullptr;
	QCheckBox *m_auditionBacking = nullptr, *m_auditionLoop = nullptr;
	QDoubleSpinBox *m_auditionPosition = nullptr;
	QPushButton *m_auditionPlaySection = nullptr, *m_auditionPlayReview = nullptr;
	QPushButton *m_auditionReplay = nullptr, *m_auditionPause = nullptr;
	QLabel *m_auditionStatus = nullptr;
	AudioRecordingMeters *m_meters = nullptr;
	AudioRecordingInfo m_info;
	AudioRecordingImportResult m_imported;
	AudioProjectIdentity m_reviewIdentity;
	QJsonObject m_savedReview;
	QLabel *m_reviewFileStatus = nullptr;
	QPushButton *m_saveReview = nullptr, *m_saveReviewAs = nullptr;
	QMessageBox *m_reviewGuard = nullptr;
	std::function<void()> m_afterReviewSave;
	bool m_reviewDiscardApproved = false;
	QVector<AudioDuplexArm> m_arms;
	QVector<AudioRecordingSelection> m_selections;
	QVector<AudioRecordingSelection> m_compSections;
	QVector<bool> m_recordedPlacement;
	std::shared_ptr<AudioRecordingDialogWork> m_work;
	QThread *m_thread = nullptr;
	bool m_loading = false, m_closePending = false;
	QTabWidget *m_tabs = nullptr;
	QWidget *m_recordForm = nullptr, *m_reviewForm = nullptr;
	QTreeWidget *m_armList = nullptr, *m_takeList = nullptr;
	QTreeWidget *m_compList = nullptr;
	QWidget *m_compForm = nullptr;
	QCheckBox *m_comp = nullptr, *m_useQueue = nullptr;
	QDoubleSpinBox *m_crossfade = nullptr;
	QLabel *m_compSummary = nullptr;
	QPushButton *m_addSection = nullptr, *m_updateSection = nullptr, *m_removeSection = nullptr;
	QComboBox *m_input = nullptr, *m_output = nullptr, *m_buffer = nullptr, *m_target = nullptr;
	QLineEdit *m_path = nullptr, *m_name = nullptr, *m_channels = nullptr, *m_storedChannels = nullptr;
	QSpinBox *m_inputChannels = nullptr, *m_calibration = nullptr, *m_clockTolerance = nullptr;
	QSpinBox *m_loopPasses = nullptr, *m_takePass = nullptr;
	qint64 m_livePassFrames = 0;
	int m_livePasses = 1;
	QDoubleSpinBox *m_playbackFirst = nullptr, *m_punchFirst = nullptr, *m_punchEnd = nullptr;
	QDoubleSpinBox *m_monitorGain = nullptr, *m_first = nullptr, *m_end = nullptr, *m_position = nullptr;
	QCheckBox *m_monitor = nullptr, *m_replacePlayback = nullptr, *m_replaceExisting = nullptr;
	QCheckBox *m_acceptInterrupted = nullptr, *m_group = nullptr;
	QCheckBox *m_useRecordedPlacement = nullptr;
	QPlainTextEdit *m_details = nullptr;
	QLabel *m_status = nullptr, *m_reviewStatus = nullptr;
	QProgressBar *m_progress = nullptr;
	QPushButton *m_record = nullptr, *m_import = nullptr, *m_stop = nullptr;
};
} // namespace vibestudio
