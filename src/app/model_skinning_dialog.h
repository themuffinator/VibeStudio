#pragma once

#include "app/live_status_label.h"
#include "core/model_document.h"
#include <QDialog>
#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableView;

namespace vibestudio
{
class ModelViewport;
class ModelSkinTable;
class ModelSkinningDialog final : public QDialog
{
  public:
	ModelSkinningDialog(const ModelMesh &mesh, const ModelSelection &selection, const QString &joint, int frame, QWidget *parent = nullptr);
	ModelEdit edit() const;
	bool choosingSelection() const
	{
		return m_choosingSelection;
	}
	ModelSelection chosenSelection() const
	{
		return m_chosen;
	}
	bool ready() const
	{
		return m_ready;
	}
	void reject() override;

  private:
	ModelMesh m_mesh;
	ModelSelection m_selection, m_chosen;
	ModelSkinReport m_report;
	ModelEdit m_reviewed;
	bool m_ready = false, m_loading = false, m_choosingSelection = false, m_closeAfter = false;
	std::function<void()> m_cancel;
	QFormLayout *m_form = nullptr;
	QWidget *m_content = nullptr;
	QComboBox *m_operation = nullptr, *m_surface = nullptr, *m_scope = nullptr, *m_method = nullptr, *m_joint = nullptr,
			  *m_filter = nullptr, *m_frame = nullptr;
	QCheckBox *m_allJoints = nullptr, *m_replace = nullptr;
	QListWidget *m_pool = nullptr;
	QSpinBox *m_limit = nullptr;
	QDoubleSpinBox *m_falloff = nullptr, *m_threshold = nullptr, *m_minimum = nullptr, *m_maximum = nullptr;
	QLabel *m_contract = nullptr;
	LiveStatusLabel *m_status = nullptr;
	QPushButton *m_apply = nullptr, *m_select = nullptr, *m_review = nullptr;
	QTableView *m_table = nullptr;
	ModelSkinTable *m_model = nullptr;
	ModelViewport *m_preview = nullptr;
	ModelSelection target() const;
	ModelEdit request() const;
	void changed();
	void review();
	void chooseVertices();
};
} // namespace vibestudio
