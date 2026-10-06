#pragma once
#include "app/audio_recording.h"
#include "app/audio_session_playback.h"
#include "core/audio_arrangement.h"
#include "core/audio_media.h"
#include "core/audio_range.h"
#include "core/audio_session_io.h"
#include "core/audio_stems.h"
#include "core/audio_waveform.h"
#include <QDialog>
#include <QPointer>
#include <memory>

class QAction;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QThread;
class QTimer;
class QTreeWidget;

namespace vibestudio
{
class AudioSessionTimeline;
class AudioRecoveryWriter;
class AudioTakeDialog;
class AudioMeterDialog;
class AudioRecordingDialog;
struct AudioRecordingImportResult;
struct AudioSessionWork;
class AudioSessionDialog final : public QDialog {
	Q_OBJECT
  public:
	explicit AudioSessionDialog(QWidget *parent = nullptr, AudioStreamDeviceFactory deviceFactory = {});
	~AudioSessionDialog() override;
	bool newSession(int sampleRate = 48000, double tempo = 120);
	bool openSession(const QString &path);
	bool openTake(const QString &path = {});
	bool openRecording(const QString &directory = {}, AudioDuplexDeviceFactory device = {},
	                   AudioCaptureStorageFactory storage = {}, AudioRecordingGate permission = {},
	                   AudioStreamDeviceFactory auditionDevice = {});
	[[nodiscard]] bool recordingOpen() const { return !m_recording.isNull(); }
	bool restoreRecovery(const QString &path, const QByteArray &expectedSha256);
	void setRecoveryEnabled(bool enabled);
	[[nodiscard]] QString recoveryPath() const;
	[[nodiscard]] bool recoveryBusy() const;
	bool saveSession(const QString &path, bool overwrite = false);
	bool importFile(const QString &path, const QString &track = {}, qint64 position = 0, bool resample = false);
	bool importSource(const AudioProject &source, const QString &track = {}, qint64 position = 0,
	                  bool resample = false);
	bool applyArrangement(const AudioArrangementEdit &edit, const QString &description);
	bool applyRange(const AudioRangeEdit &edit, const QString &description);
	bool applyMedia(const AudioMediaEdit &edit, const AudioMediaCandidate &candidate = {});
	void selectRegions(const QStringList &ids, const QString &primary = {});
	[[nodiscard]] QStringList selectedRegions(bool linkedGroups = true) const;
	bool applyEdit(const AudioSessionEdit &edit, const QString &description);
	bool exportMix(const QString &path, bool overwrite = false, AudioWavFormat format = AudioWavFormat::Float32,
	               bool dither = false, quint64 ditherSeed = 0);
	bool exportStems(AudioStemExportRequest request);
	[[nodiscard]] QJsonObject deliveryReport() const { return m_deliveryReport; }
	void prepareMix(bool audition = false);
	void setSelection(qint64 first, qint64 end);
	void selectRegion(const QString &track, const QString &region);
	void undo();
	void redo();
	void cancelWork();
	void stopPlayback();
	void showMeters();
	void analyzeMeters();
	[[nodiscard]] const AudioSession &session() const { return m_state.session; }
	[[nodiscard]] bool isBusy() const { return bool(m_work) || !m_take.isNull() || !m_recording.isNull(); }
	[[nodiscard]] bool hasChanges() const { return m_state.revision != m_savedRevision; }
	[[nodiscard]] QString sessionPath() const { return m_identity.path; }
	[[nodiscard]] qint64 retainedWaveformBytes() const;
	std::function<void()> beforePlayback;
	AudioRecordingGate beforeRecording;
  Q_SIGNALS:
	void mixReady(const QByteArray &wav, const QString &sourceSession);
	void recoveryPreferenceChanged(bool enabled);
	void waveformRecoveryRequested(const QString &path, const QByteArray &sha256);

  protected:
	void closeEvent(QCloseEvent *) override;
	void reject() override;

  private:
	struct State {
		AudioSession session;
		quint64 revision = 0;
		QString description;
	};
	void work(const QString &title, std::function<void(AudioSessionWork &)> perform,
	          std::function<void(const AudioSessionWork &)> complete, bool audition = false, bool meterJob = false);
	void refreshMeters();
	void adopt(const AudioSession &session, const QString &description);
	void trimHistory();
	void scheduleRecovery();
	void checkpointRecovery();
	void retireRecovery();
	void refreshClipSelection();
	void editSelection();
	void editRange();
	void editMedia();
	QStringList protectedMediaPaths() const;
	void refreshSnap();
	void refreshMusicalPosition();
	void goToMusicalPosition();
	void refreshTransport();
	void refresh();
	void status(const QString &message);
	bool guardChanges(std::function<void()> continuation);
	bool chooseSave(bool separate = false);
	void chooseImport();
	void chooseTake();
	void adoptRecording(const AudioRecordingImportResult &, quint64 revision);
	void editTrack();
	void editRouting();
	void editEffects(bool master);
	void editRegion();
	void editAutomation();
	AudioSessionEdit selectedEdit(bool region) const;
	State m_state;
	QVector<State> m_undo, m_redo;
	quint64 m_nextRevision = 0, m_savedRevision = 0;
	AudioProjectIdentity m_identity;
	QString m_recoveryDirectory, m_recoveryId, m_recoveryInputPath, m_recoverySourcePath;
	AudioRecoveryWriter *m_recovery = nullptr;
	QTimer *m_recoveryTimer = nullptr;
	QCheckBox *m_recoveryEnabled = nullptr;
	QLabel *m_recoveryStatus = nullptr;
	bool m_recoveryActive = false;
	QHash<QString, AudioWaveformData> m_waveforms;
	std::shared_ptr<AudioSessionWork> m_work;
	QThread *m_thread = nullptr;
	QPointer<AudioTakeDialog> m_take;
	QPointer<AudioRecordingDialog> m_recording;
	QPointer<AudioMeterDialog> m_meters;
	std::function<void()> m_afterSave;
	QString m_track, m_region;
	QStringList m_selection;
	QCheckBox *m_linkedGroups = nullptr;
	QLabel *m_selectionStatus = nullptr;
	AudioSessionTimeline *m_timeline = nullptr;
	QTreeWidget *m_tracks = nullptr;
	QLabel *m_summary = nullptr;
	QLabel *m_status = nullptr;
	QLabel *m_transport = nullptr;
	QProgressBar *m_progress = nullptr;
	QPushButton *m_cancel = nullptr;
	QDoubleSpinBox *m_first = nullptr;
	QDoubleSpinBox *m_end = nullptr;
	QDoubleSpinBox *m_cursor = nullptr;
	QComboBox *m_snap = nullptr;
	QComboBox *m_ruler = nullptr;
	QLineEdit *m_musicalPosition = nullptr;
	AudioTempoTimeline m_musicalTime;
	QCheckBox *m_loop = nullptr;
	AudioSessionPlayback *m_playback = nullptr;
	AudioStreamDeviceFactory m_playbackFactory;
	QComboBox *m_output = nullptr;
	QComboBox *m_buffer = nullptr;
	QVector<QAction *> m_actions;
	QAction *m_undoAction = nullptr;
	QAction *m_redoAction = nullptr;
	QAction *m_play = nullptr;
	QAction *m_stop = nullptr;
	QJsonObject m_deliveryReport;
};
} // namespace vibestudio
