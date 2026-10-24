#pragma once
#include "app/live_status_label.h"
#include "core/model_document.h"
#include <QDialog>
#include <QImage>
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QTabWidget;
class QLabel;
class QPushButton;
class QTableView;
class QLineEdit;
class QDialogButtonBox;
namespace vibestudio
{
class ModelUvView;
class ModelUvDensityTable;
class ModelUvDensityDialog final : public QDialog
{
  public:
	ModelUvDensityDialog(const ModelMesh &mesh, const ModelSelection &selection, int frame, const QImage &texture, QSize materialSize,
						 const QString &materialName, QWidget *parent = nullptr);
	void inspect();
	void preview();
	void accept() override;
	void reject() override;
	bool ready() const { return m_ready; }
	bool busy() const { return m_busy; }
	ModelEdit edit() const { return m_reviewed; }
	const ModelMesh &previewMesh() const { return m_result; }
	const ModelUvDensityReport &before() const { return m_before; }
	const ModelUvDensityReport &after() const { return m_after; }

  protected:
	void resizeEvent(QResizeEvent *event) override;

  private:
	ModelMesh m_source, m_result;
	ModelSelection m_selection;
	ModelEdit m_reviewed;
	QImage m_texture;
	ModelUvDensityReport m_before, m_after;
	bool m_ready = false, m_busy = false, m_closing = false;
	std::function<void()> m_cancel;
	QComboBox *m_scope = nullptr, *m_mode = nullptr, *m_view = nullptr;
	QSpinBox *m_width = nullptr, *m_height = nullptr, *m_frame = nullptr;
	QDoubleSpinBox *m_target = nullptr;
	QTabWidget *m_tabs = nullptr;
	LiveStatusLabel *m_status = nullptr;
	QLineEdit *m_detail = nullptr;
	QPushButton *m_inspect = nullptr, *m_previewButton = nullptr, *m_apply = nullptr, *m_useMeasured = nullptr;
	QTableView *m_table = nullptr;
	ModelUvDensityTable *m_report = nullptr;
	ModelUvView *m_preview = nullptr;
	QDialogButtonBox *m_buttons = nullptr;
	void review(bool change);
	void invalidate(bool measurement);
	void controls();
	void showPreview();
	void status(const QString &text);
};
} // namespace vibestudio
