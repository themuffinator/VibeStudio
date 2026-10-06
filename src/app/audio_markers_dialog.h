#pragma once

#include "core/audio_markers.h"
#include <QDialog>

class QCheckBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace vibestudio
{
class AudioMarkersDialog final : public QDialog
{
	Q_OBJECT
public:
	AudioMarkersDialog(const AudioMarkers& markers, qint64 frames, qint64 selectionFirst, qint64 selectionEnd,
	                   QWidget* parent = nullptr);
	[[nodiscard]] AudioMarkers markers() const;
	void accept() override;

private:
	void appendCue(const AudioCue& cue);
	void refreshControls();
	qint64 m_frames = 0;
	bool m_canAdd = false;
	QTableWidget* m_cues = nullptr;
	QCheckBox* m_loop = nullptr;
	QSpinBox* m_first = nullptr;
	QSpinBox* m_end = nullptr;
	QPushButton* m_add = nullptr;
	QPushButton* m_remove = nullptr;
	QLabel* m_status = nullptr;
};
} // namespace vibestudio
