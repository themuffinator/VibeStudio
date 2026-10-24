#pragma once
#include "app/live_status_label.h"
#include "core/model_document.h"
#include <QDialog>
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QTableView;
class QTabWidget;
class QPushButton;
class QLabel;
class QLineEdit;
namespace vibestudio
{
class ModelViewport;
class ModelCurveKeysTable;
class ModelCurveKeyReportTable;
class ModelCurveKeysDialog final : public QDialog
{
  public:
	ModelCurveKeysDialog(const ModelMesh &mesh, const QString &joint, int frame, QWidget *parent = nullptr);
	void review();
	void reject() override;
	bool ready() const
	{
		return m_ready;
	}
	bool busy() const
	{
		return m_busy;
	}
	ModelEdit edit() const
	{
		return m_reviewed;
	}
	const ModelMesh &previewMesh() const
	{
		return m_result;
	}
	const ModelCurveKeyReport &report() const
	{
		return m_report;
	}

  private:
	ModelMesh m_source, m_result;
	ModelEdit m_reviewed;
	ModelCurveKeyReport m_report;
	bool m_ready = false, m_busy = false, m_closing = false, m_updating = false;
	std::function<void()> m_cancel;
	QComboBox *m_clip = nullptr, *m_joint = nullptr, *m_channel = nullptr, *m_operation = nullptr, *m_view = nullptr;
	QDoubleSpinBox *m_pivot = nullptr, *m_factor = nullptr, *m_offset = nullptr;
	std::array<QDoubleSpinBox *, 3> m_tolerance{};
	QSpinBox *m_first = nullptr, *m_last = nullptr, *m_sample = nullptr;
	QCheckBox *m_snap = nullptr;
	QTableView *m_keys = nullptr, *m_results = nullptr;
	ModelCurveKeysTable *m_table = nullptr;
	ModelCurveKeyReportTable *m_reportTable = nullptr;
	QTabWidget *m_tabs = nullptr;
	QPushButton *m_review = nullptr, *m_apply = nullptr;
	QLabel *m_count = nullptr;
	LiveStatusLabel *m_status = nullptr;
	QLineEdit *m_detail = nullptr;
	ModelViewport *m_preview = nullptr;
	int clip() const;
	void choose();
	void filter();
	void invalidate();
	void updateControls();
	void showPreview();
};
} // namespace vibestudio
