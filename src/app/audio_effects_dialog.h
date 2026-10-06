#pragma once
#include "core/audio_effect_preset.h"
#include "core/audio_session.h"
#include <QDialog>
class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QListWidget;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QThread;
namespace vibestudio
{
struct AudioEffectPresetWork;
class AudioEffectsDialog final : public QDialog {
	Q_OBJECT
  public:
	// Empty track ID selects master inserts.
	AudioEffectsDialog(const AudioSession &session, const QString &trackId, QWidget *parent = nullptr,
	                   const QStringList &protectedPaths = {});
	~AudioEffectsDialog() override;
	AudioSessionEdit edit() const;
	bool applyPreset(const AudioEffectPreset &preset);
	bool loadPresetFile(const QString &path);
	bool savePresetFile(const QString &path, bool overwrite = false);
	// The curve dialog edits this draft; the outer Apply remains the session undo boundary.
	void editAutomation();
	void setAutomationCursor(qint64 frame) { m_automationCursor = frame; }
	[[nodiscard]] bool busy() const { return bool(m_work); }

  protected:
	void reject() override;
	void closeEvent(QCloseEvent *event) override;

  private:
	AudioEffectChain &chain();
	AudioEffectAutomation &automation();
	void refresh(int selection);
	void select();
	void validate();
	void updateItem();
	void launchPreset(std::function<void(AudioEffectPresetWork &)> perform, bool loading);
	AudioSession m_draft;
	QStringList m_protectedPaths;
	AudioProjectIdentity m_presetIdentity;
	std::shared_ptr<AudioEffectPresetWork> m_work;
	QThread *m_thread = nullptr;
	bool m_closePending = false;
	QWidget *m_body = nullptr;
	QComboBox *m_factory = nullptr;
	QLineEdit *m_presetName = nullptr;
	QProgressBar *m_progress = nullptr;
	QString m_trackId;
	int m_track = -1;
	qint64 m_automationCursor = 0;
	bool m_missing = false, m_updating = false;
	QListWidget *m_list = nullptr;
	QComboBox *m_type = nullptr;
	QCheckBox *m_enabled = nullptr;
	QDoubleSpinBox *m_tail = nullptr;
	QFormLayout *m_parameters = nullptr;
	QPushButton *m_add = nullptr, *m_remove = nullptr, *m_up = nullptr, *m_down = nullptr;
	QPushButton *m_automation = nullptr;
	QLabel *m_status = nullptr;
	QDialogButtonBox *m_buttons = nullptr;
};
} // namespace vibestudio
