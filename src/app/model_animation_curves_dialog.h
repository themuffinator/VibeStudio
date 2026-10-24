#pragma once
#include "app/live_status_label.h"
#include "core/model_document.h"
#include <QDialog>
#include <functional>
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableView;
class QTabWidget;
namespace vibestudio
{
class ModelViewport;
class ModelAnimationCurveView;
class ModelCurveKeyTable;
class ModelAnimationCurvesDialog final : public QDialog
{
  public:
	ModelAnimationCurvesDialog(const ModelMesh &mesh, const QString &joint, int frame, QWidget *parent = nullptr);
	ModelEdit edit() const
	{
		return m_reviewed;
	}
	bool ready() const
	{
		return m_ready;
	}
	bool busy() const
	{
		return m_busy;
	}
	const ModelMesh &previewMesh() const
	{
		return m_result;
	}
	void capture();
	void review();
	void reject() override;

  private:
	struct Draft
	{
		ModelPoseCurves group;
		bool bake = false;
		bool operator==(const Draft &) const = default;
	};
	ModelMesh m_source, m_result;
	ModelEdit m_reviewed;
	// Staged drafts for Undo and Redo, at most draftLimit; the oldest go
	// first. m_initial is the clip's stored curve, which marks the draft dirty.
	static constexpr int draftLimit = 256;
	Draft m_initial;
	QVector<Draft> m_history;
	int m_historyIndex = 0;
	bool m_ready = false, m_busy = false, m_closeAfter = false, m_updating = false;
	std::function<void()> m_cancel;
	QComboBox *m_clip = nullptr, *m_joint = nullptr, *m_channel = nullptr, *m_mode = nullptr, *m_tangent = nullptr, *m_view = nullptr;
	QSpinBox *m_frame = nullptr, *m_sample = nullptr;
	QDoubleSpinBox *m_value = nullptr, *m_in = nullptr, *m_out = nullptr;
	QPushButton *m_capture = nullptr, *m_bake = nullptr, *m_set = nullptr, *m_move = nullptr, *m_remove = nullptr, *m_undo = nullptr,
				*m_redo = nullptr, *m_discard = nullptr, *m_review = nullptr, *m_apply = nullptr;
	LiveStatusLabel *m_status = nullptr;
	QTabWidget *m_tabs = nullptr;
	QTableView *m_keys = nullptr;
	ModelCurveKeyTable *m_table = nullptr;
	ModelAnimationCurveView *m_graph = nullptr;
	ModelViewport *m_preview = nullptr;
	int clip() const;
	int frameCount() const;
	const Draft &draft() const;
	void choose();
	void stage(Draft next, int selectedFrame);
	void refresh(int selectedFrame = 0);
	void selectedKey(int row);
	void showPreview();
	void commitKey(int oldFrame, const ModelPoseCurveKey &key);
	void removeKey(int frame);
	ModelPoseCurveKey keyFromControls() const;
};
} // namespace vibestudio
