#pragma once

#include "app/audio_playback.h"
#include "core/audio_clip.h"
#include "core/audio_analysis.h"
#include "core/audio_delivery.h"
#include "core/audio_export.h"
#include "core/audio_level.h"
#include "core/audio_project.h"
#include "core/audio_waveform.h"

#include <QDialog>
#include <memory>

class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSlider;
class QScrollBar;
class QSpinBox;
class QToolBar;

namespace vibestudio
{
class AudioWaveformView;
class AudioRecoveryWriter;
struct AudioEditorWork;

struct AudioEditorContext {
	QString packagePath;
	bool canStage = false;
	bool doomWad = false;
	QString packageToken{};
	QString levelToken{};
	QString levelName{};
	LevelSoundTarget levelTarget{};
	LevelMapVec3 levelOrigin{};
};

class AudioEditorDialog final : public QDialog
{
	Q_OBJECT
public:
	explicit AudioEditorDialog(QWidget* parent = nullptr, std::unique_ptr<AudioPlaybackBackend> playbackBackend = {});
	~AudioEditorDialog() override;
	std::function<AudioEditorContext()> context;
	std::function<bool(const QByteArray&, const QString&, bool, QString*)> handoff;
	std::function<bool(const QByteArray&, const LevelSoundRequest&, bool, QString*)> levelHandoff;
	std::function<void()> beforePlayback;
	std::function<bool()> playbackAllowed;
	using Reader = std::function<QByteArray(QString*)>;
	bool loadSource(const QString& name, const QString& protectedPath, Reader reader);
	bool openFile(const QString& path);
	bool createNew(int sampleRate, int channels, qint64 frames = 0);
	void copySelection(bool cut = false);
	void pasteSelection(bool mix = false);
	void insertSilence(qint64 frames);
	void resample(int targetRate);
	void analyze(const AudioAnalysisOptions& options = {});
	void reviewAnalysisChannels();
	bool setMarkers(const AudioMarkers& markers);
	bool openProject(const QString& path, bool recoverAsDraft = false, const QByteArray& expectedSha256 = {});
	bool saveProjectTo(const QString& path, bool overwrite = false);
	[[nodiscard]] QString projectPath() const { return m_projectIdentity.path; }
	[[nodiscard]] QString recoveryPath() const;
	[[nodiscard]] bool recoveryBusy() const;
	void applyEdit(const QString& operation, double decibels = 0);
	void setSelection(qint64 firstFrame, qint64 endFrame);
	void undo();
	void redo();
	void refreshContext();
	void setHighContrast(bool enabled);
	void setRecoveryEnabled(bool enabled);
	void cancelWork();
	void stopPlayback();
	void stopPlaybackAndWait(QObject* context, std::function<void(bool)> complete);
	[[nodiscard]] const AudioClip& clip() const { return m_state.clip; }
	[[nodiscard]] bool isBusy() const { return bool(m_work); }
	[[nodiscard]] bool hasChanges() const { return m_state.revision != m_savedRevision; }
	bool exportTo(const QString& path, bool overwrite = false, const AudioWavOptions& options = {});
	bool exportDeliveryTo(const QString& path, bool overwrite, const AudioDeliveryOptions& options);

Q_SIGNALS:
	void sessionRequested(const AudioProject& source);
	void sessionRecoveryRequested(const QString& path, const QByteArray& sha256);
	void recoveryPreferenceChanged(bool enabled);

protected:
	void closeEvent(QCloseEvent* event) override;
	void reject() override;
	void changeEvent(QEvent* event) override;

private:
	struct State {
		AudioClip clip;
		AssetAudioPeaks peaks;
		qint64 first = 0, end = 0;
		quint64 revision = 0;
		QString editLabel;
		AudioWaveformData waveform;
	};
	void runWork(const QString& title, std::function<void(AudioEditorWork&)> work,
	             std::function<void(const AudioEditorWork&)> complete, const AudioClip& comparison = {});
	void refresh();
	void refreshSelection();
	void refreshView();
	void showStatus(const QString& text);
	void trimHistory();
	bool confirmDiscard(std::function<void()> afterSave = {});
	void chooseFile();
	void chooseNew();
	void chooseSampleRate();
	void commitEdit(const State& previous, const AudioEditorWork& work, const QString& label, qint64 first, qint64 end);
	void refreshClipboardActions();
	void chooseExport();
	bool chooseProjectSave(bool saveAs = false);
	void chooseRecovery();
	void chooseMarkers();
	AudioProject projectSnapshot() const;
	void checkpointRecovery();
	void retireRecovery();
	void stage(bool place = false);
	void play();
	void stop();
	void refreshPlayback();
	State m_state;
	QVector<State> m_undo, m_redo;
	quint64 m_nextRevision = 0, m_savedRevision = 0;
	QString m_sourceName, m_protectedPath, m_contextKey;
	QString m_contextToken;
	QString m_recoveryInputPath;
	bool m_contextDoomWad = false;
	AudioProjectIdentity m_projectIdentity;
	QJsonObject m_projectMetadata;
	QString m_recoveryDirectory, m_recoveryId;
	bool m_recoveryActive = false;
	AudioRecoveryWriter* m_recovery = nullptr;
	QCheckBox* m_recoveryEnabled = nullptr;
	QLabel* m_recoveryStatus = nullptr;
	QAction* m_save = nullptr;
	QAction* m_saveAs = nullptr;
	QAction* m_recover = nullptr;
	bool m_closeAfterSave = false;
	std::function<void()> m_afterSave;
	quint64 m_waveformRevision = 0;
	quint64 m_recoverySerial = 0;
	std::shared_ptr<AudioEditorWork> m_work;
	AudioWaveformView* m_waveform = nullptr;
	QToolBar* m_viewControls = nullptr;
	QScrollBar* m_pan = nullptr;
	QLabel* m_viewRange = nullptr;
	QAction* m_zoomSelection = nullptr;
	QLabel* m_source = nullptr;
	QLabel* m_summary = nullptr;
	QLabel* m_status = nullptr;
	QLabel* m_contextLabel = nullptr;
	QProgressBar* m_progress = nullptr;
	QPushButton* m_cancel = nullptr;
	QSpinBox* m_first = nullptr;
	QSpinBox* m_end = nullptr;
	QLineEdit* m_packagePath = nullptr;
	QComboBox* m_stagePreset = nullptr;
	QCheckBox* m_stageDither = nullptr;
	QLabel* m_stageFormat = nullptr;
	QCheckBox* m_replace = nullptr;
	QCheckBox* m_loop = nullptr;
	QSlider* m_volume = nullptr;
	QSlider* m_seek = nullptr;
	QSpinBox* m_seekFrame = nullptr;
	QLabel* m_clock = nullptr;
	QAction* m_open = nullptr;
	QAction* m_new = nullptr;
	QAction* m_copy = nullptr;
	QAction* m_cut = nullptr;
	QAction* m_paste = nullptr;
	QAction* m_mix = nullptr;
	QAction* m_insertSilence = nullptr;
	QAction* m_resample = nullptr;
	QAction* m_analyze = nullptr;
	QAction* m_markers = nullptr;
	QAction* m_selectLoop = nullptr;
	QAction* m_export = nullptr;
	QAction* m_toSession = nullptr;
	QAction* m_stage = nullptr;
	QAction* m_stageAndPlace = nullptr;
	QAction* m_undoAction = nullptr;
	QAction* m_redoAction = nullptr;
	QAction* m_play = nullptr;
	QAction* m_stop = nullptr;
	QVector<QAction*> m_edits;
	AudioPlayback* m_playback = nullptr;
};
} // namespace vibestudio
