#pragma once
#include "app/live_status_label.h"
#include "core/model_document.h"
#include <QDialog>
#include <functional>
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;
class QTableView;
class QTableWidget;
namespace vibestudio
{
class ModelViewport;
class ModelTransferTable;
class ModelWeightTransferDialog final : public QDialog
{
  public:
	ModelWeightTransferDialog(const ModelMesh &mesh, const ModelSelection &selection, int frame, QWidget *parent = nullptr);
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
	void review();
	void reject() override;

  private:
	ModelMesh m_source, m_result;
	ModelSelection m_selection;
	ModelEdit m_reviewed;
	bool m_ready = false, m_busy = false, m_closeAfter = false;
	std::function<void()> m_cancel;
	QComboBox *m_from = nullptr, *m_to = nullptr, *m_scope = nullptr, *m_method = nullptr, *m_axis = nullptr, *m_side = nullptr,
			  *m_unmatched = nullptr, *m_frame = nullptr, *m_view = nullptr;
	QDoubleSpinBox *m_plane = nullptr, *m_centre = nullptr, *m_distance = nullptr, *m_angle = nullptr, *m_strength = nullptr;
	QLineEdit *m_left = nullptr, *m_right = nullptr;
	QTableWidget *m_mapping = nullptr;
	QTableView *m_weights = nullptr;
	ModelTransferTable *m_table = nullptr;
	ModelViewport *m_preview = nullptr;
	LiveStatusLabel *m_status = nullptr;
	QPushButton *m_apply = nullptr, *m_review = nullptr;
	QTabWidget *m_tabs = nullptr;
	ModelEdit request() const;
	void changed();
	void showPreview();
};
} // namespace vibestudio
