#pragma once
#include "core/level_map.h"
#include <QCoreApplication>
#include <QDialog>
#include <functional>
#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QThread;
class QTimer;

namespace vibestudio
{
class ModelViewport;

class LevelRotationDialog final : public QDialog
{
	Q_DECLARE_TR_FUNCTIONS(LevelRotationDialog)
  public:
	explicit LevelRotationDialog(const LevelMapDocument &source, int axis = 2, QWidget *parent = nullptr);
	~LevelRotationDialog() override;
	LevelMapRotationRequest request() const;
	void setRequest(const LevelMapRotationRequest &request);
	bool isReady() const { return m_ready; }
	bool previewValid() const { return m_valid; }
	const LevelMapDocument &previewDocument() const { return m_previewDocument; }
	void setApplyHandler(std::function<bool(const LevelMapRotationRequest &, QString *)> handler);
	void accept() override;

  private:
	struct Work;
	void schedule();
	void startPreview();
	LevelMapDocument m_source, m_previewDocument;
	QComboBox *m_axis = nullptr;
	QComboBox *m_pivotMode = nullptr;
	QDoubleSpinBox *m_angle = nullptr;
	std::array<QDoubleSpinBox *, 3> m_pivot{};
	QCheckBox *m_lock = nullptr;
	QCheckBox *m_allowValve = nullptr;
	QLabel *m_status = nullptr;
	QProgressBar *m_progress = nullptr;
	QPushButton *m_apply = nullptr;
	ModelViewport *m_preview = nullptr;
	QTimer *m_debounce = nullptr;
	QThread *m_thread = nullptr;
	std::shared_ptr<Work> m_work;
	std::function<bool(const LevelMapRotationRequest &, QString *)> m_applyHandler;
	quint64 m_generation = 0;
	bool m_ready = false, m_valid = false, m_updating = false;
};
} // namespace vibestudio
