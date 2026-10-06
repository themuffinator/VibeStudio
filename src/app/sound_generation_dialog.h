#pragma once

// The Sound Generator: a description in, game-ready sound effects out
// (core/sound_generation.h), made by the built-in synthesizer on this machine
// or by the configured sound model. Variants are listed with their waveforms
// and played before one is saved where the game reads it, placed in the open
// map (a target_speaker for Quake II and III), or opened in the Audio editor.
// The shell supplies the folder, consent, the other surfaces, and Activity
// through hooks.

#include "core/ai_audio_transport.h"
#include "core/sound_generation.h"

#include <QCoreApplication>
#include <QDialog>
#include <QJsonObject>
#include <QVector>

#include <functional>
#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSpinBox;

namespace vibestudio {

class AudioPlayback;

struct SoundGenerationDialogHooks {
	// The game the open map, package, or project targets, or empty.
	std::function<QString()> defaultGame;
	// The project folder sounds are written into.
	std::function<QString()> outputFolder;
	std::function<bool(const QString& connectorId, const QString& displayName, const QString& endpoint, const AiHttpRequest& request)> confirmSend;
	// Opens a WAV of the working sound in the Audio editor.
	std::function<void(const QByteArray& wav, const QString& name)> openInEditor;
	// Places a saved sound in the open map; false says why.
	std::function<bool(const GeneratedSound& sound, const QString& game, QString* error)> placeInMap;
	std::function<void(const QStringList& paths)> written;
	std::function<QString(const QString& title, const QString& detail)> beginTask;
	std::function<void(const QString& task, bool succeeded, bool cancelled, const QString& summary)> endTask;
};

class SoundGenerationDialog final : public QDialog {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioSoundGenerationDialog)

public:
	SoundGenerationDialog(QWidget* parent, SoundGenerationDialogHooks hooks);
	~SoundGenerationDialog() override;

	void setPrompt(const QString& prompt);
	void generate();
	void cancelGeneration();
	[[nodiscard]] bool busy() const;
	[[nodiscard]] const QVector<GeneratedSound>& sounds() const;
	// Saves the selected variant; with `place`, puts it in the open map too.
	bool saveSelected(bool place);
	// Plays the selected variant, or stops it.
	void togglePlayback();
	void refreshSourceStatus();

protected:
	void reject() override;
	void keyPressEvent(QKeyEvent* event) override;

private:
	struct Work;

	SoundGenerationSpec specFromControls() const;
	AiSoundRequest soundRequest(const SoundGenerationSpec& variant, const AiSoundConnection& connection) const;
	void askModel(const AiSoundConnection& connection, int index);
	void process();
	void showSounds();
	void showSelected();
	void setBusy(bool busy, const QString& status);
	void finishTask(bool succeeded, bool cancelled, const QString& summary);
	void updateGameControls();
	void updatePlayButton();
	void showRequestPreview();

	SoundGenerationDialogHooks m_hooks;
	std::shared_ptr<Work> m_work;
	std::unique_ptr<AiSoundClient> m_client;
	AudioPlayback* m_playback = nullptr;
	QVector<GeneratedSound> m_sounds;
	QVector<SoundGenerationSpec> m_specs;
	// A model's answers, in variant order, before they are processed.
	QVector<QPair<QByteArray, QString>> m_answers;
	QJsonObject m_provenance;
	QString m_task;
	bool m_busy = false;
	bool m_loopTouched = false;

	QPlainTextEdit* m_prompt = nullptr;
	QComboBox* m_game = nullptr;
	QComboBox* m_kind = nullptr;
	QDoubleSpinBox* m_duration = nullptr;
	QCheckBox* m_loop = nullptr;
	QSpinBox* m_variants = nullptr;
	QSpinBox* m_seed = nullptr;
	QLineEdit* m_name = nullptr;
	QLineEdit* m_folder = nullptr;
	QLineEdit* m_wad = nullptr;
	QLabel* m_folderLabel = nullptr;
	QLabel* m_wadLabel = nullptr;
	QRadioButton* m_synthSource = nullptr;
	QRadioButton* m_modelSource = nullptr;
	QLabel* m_sourceStatus = nullptr;
	QPushButton* m_previewRequest = nullptr;
	QPushButton* m_generate = nullptr;
	QPushButton* m_cancel = nullptr;
	QLabel* m_status = nullptr;
	QProgressBar* m_progress = nullptr;
	QListWidget* m_variantList = nullptr;
	QPushButton* m_play = nullptr;
	QPlainTextEdit* m_details = nullptr;
	QPushButton* m_save = nullptr;
	QPushButton* m_savePlace = nullptr;
	QPushButton* m_openEditor = nullptr;
};

} // namespace vibestudio
