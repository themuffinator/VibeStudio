#pragma once
#include "app/live_status_label.h"
#include "core/model_document.h"
#include <QDialog>
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QListWidget;
class QTableView;
class QTabWidget;
class QLabel;
class QPushButton;
class QLineEdit;
namespace vibestudio
{
class ModelViewport;
class ModelCurveKeyReportTable;
class ModelCurveTransferDialog final : public QDialog
{
  public:
	ModelCurveTransferDialog(const ModelMesh &mesh, const QString &joint, int frame, QWidget *parent = nullptr);
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

  private:
	ModelMesh m_source, m_result;
	ModelEdit m_reviewed;
	bool m_ready = false, m_busy = false, m_closing = false, m_updating = false;
	std::function<void()> m_cancel;
	QComboBox *m_from = nullptr, *m_to = nullptr, *m_view = nullptr;
	QSpinBox *m_first = nullptr, *m_last = nullptr, *m_targetFirst = nullptr, *m_sample = nullptr;
	QDoubleSpinBox *m_factor = nullptr;
	QCheckBox *m_reverse = nullptr, *m_matchRate = nullptr;
	QListWidget *m_joints = nullptr, *m_channels = nullptr;
	QTabWidget *m_tabs = nullptr;
	QTableView *m_results = nullptr;
	ModelCurveKeyReportTable *m_report = nullptr;
	QLabel *m_summary = nullptr;
	LiveStatusLabel *m_status = nullptr;
	QLineEdit *m_detail = nullptr;
	QPushButton *m_review = nullptr, *m_apply = nullptr;
	ModelViewport *m_preview = nullptr;
	int from() const;
	int to() const;
	double factor() const;
	void choose(bool sourceChanged);
	void invalidate();
	void controls();
	void showPreview();
};
} // namespace vibestudio
