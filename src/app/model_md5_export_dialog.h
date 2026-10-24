#pragma once

#include "core/model_document.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace vibestudio
{
// Chooses what an md5anim export writes: one skeletal clip, or every clip,
// and the frame rate. An md5anim stores a whole number of frames per second,
// so a clip with a fractional rate needs one chosen here; the review says how
// that changes playback before anything is written.
class ModelMd5ExportDialog final : public QDialog
{
  public:
	// `clip` is preselected; `allClips` writes every clip and hides the chooser.
	ModelMd5ExportDialog(const ModelMesh &mesh, int clip, bool allClips, QWidget *parent = nullptr);
	// The clip (negative for every clip) and the explicit frame rate, zero
	// for each clip's own whole rate.
	[[nodiscard]] ModelExportOptions options() const;

  private:
	struct Clip
	{
		QString name;
		double framesPerSecond = 0;
		int frames = 0;
	};
	QVector<Clip> m_clips;
	bool m_allClips = false;
	QComboBox *m_clip = nullptr;
	QSpinBox *m_rate = nullptr;
	QLabel *m_review = nullptr;
	QPushButton *m_accept = nullptr;
	void refresh();
};
} // namespace vibestudio
