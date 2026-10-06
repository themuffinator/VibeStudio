#pragma once
#include "core/audio_stems.h"
#include <QDialog>
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QLabel;
class QDialogButtonBox;
namespace vibestudio
{
class AudioStemExportDialog final : public QDialog {
	Q_OBJECT
  public:
	AudioStemExportDialog(const AudioSession &session, qint64 first, qint64 end, QWidget *parent = nullptr);
	[[nodiscard]] AudioStemExportRequest request() const;

  private:
	void refresh();
	AudioSession m_session;
	QListWidget *m_strips = nullptr;
	QLineEdit *m_directory = nullptr, *m_prefix = nullptr, *m_seed = nullptr;
	QComboBox *m_tap = nullptr, *m_format = nullptr;
	QCheckBox *m_master = nullptr, *m_solo = nullptr, *m_overwrite = nullptr, *m_dither = nullptr;
	QDoubleSpinBox *m_first = nullptr, *m_end = nullptr;
	QPlainTextEdit *m_plan = nullptr;
	QLabel *m_status = nullptr;
	QDialogButtonBox *m_buttons = nullptr;
};
} // namespace vibestudio
