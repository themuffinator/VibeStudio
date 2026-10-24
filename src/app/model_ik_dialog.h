#pragma once
#include "app/live_status_label.h"
#include "core/model_document.h"
#include <QDialog>
#include <functional>
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QLabel;
class QPushButton;
class QTabWidget;
namespace vibestudio
{
class ModelViewport;
class ModelIkTable;
class ModelIkDialog final : public QDialog
{
  public:
	ModelIkDialog(const ModelMesh &mesh, const QString &joint, int frame, QWidget *parent = nullptr);
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
	ModelEdit m_reviewed;
	bool m_ready = false, m_busy = false, m_closeAfter = false;
	std::function<void()> m_cancel;
	QComboBox *m_clip = nullptr, *m_end = nullptr, *m_reach = nullptr, *m_orientation = nullptr, *m_view = nullptr;
	QSpinBox *m_first = nullptr, *m_last = nullptr, *m_sample = nullptr;
	QDoubleSpinBox *m_target[3]{}, *m_pole[3]{};
	QLabel *m_chain = nullptr;
	LiveStatusLabel *m_status = nullptr;
	QPushButton *m_apply = nullptr, *m_review = nullptr;
	QTabWidget *m_tabs = nullptr;
	ModelViewport *m_preview = nullptr;
	ModelIkTable *m_table = nullptr;
	ModelEdit request() const;
	void changed();
	void clipChanged();
	void usePose();
	void showPreview();
};
} // namespace vibestudio
