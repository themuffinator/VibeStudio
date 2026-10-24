#pragma once

#include "core/model_document.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTreeWidget;

namespace vibestudio
{
// Chooses the skeletal working set: the clips baked into editable frames
// (core/model_skeletal_working_set.h). Each clip shows its frame count and
// what it costs in frame vertices; the meter shows the budget the checked
// clips use, the summary what that leaves for mesh tools, and the details the
// full report. Every clip stays in the skeleton, so the skeletal tools still
// edit and export clips outside the working set.
class ModelSkeletalWorkingSetDialog final : public QDialog
{
  public:
	explicit ModelSkeletalWorkingSetDialog(const ModelMesh &mesh, QWidget *parent = nullptr);
	// The SetSkeletalWorkingSet edit for the checked clips.
	[[nodiscard]] ModelEdit edit() const;
	[[nodiscard]] QVector<int> checkedClips() const;
	// Checks exactly `clips`, as Fit Automatically and Bind Pose Only do.
	void setCheckedClips(const QVector<int> &clips);

  private:
	ModelMesh m_mesh;
	QTreeWidget *m_clips = nullptr;
	QProgressBar *m_budget = nullptr;
	QLabel *m_summary = nullptr, *m_details = nullptr;
	QPushButton *m_showDetails = nullptr, *m_apply = nullptr;
	void refresh();
};

// One line for the Skeleton page: how many clips the working set bakes and
// how much of the frame budget they use.
[[nodiscard]] QString modelSkeletalWorkingSetSummary(const ModelSkeletalWorkingSetPlan &plan);
// Adds clip `clip` to a skeletal tool's clip chooser, with the clip index as
// its data, unless it has no poses. A clip outside the working set says so in
// its text and tooltip: the tool still edits it, and its preview skins one
// sample at a time.
void addModelSkeletalClipItem(QComboBox *combo, const ModelMesh &mesh, int clip);
} // namespace vibestudio
