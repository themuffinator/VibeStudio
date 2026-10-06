#pragma once
#include "core/level_materials.h"
#include "core/level_merge.h"
#include <QCoreApplication>
#include <QDialog>
#include <memory>

class QComboBox;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QThread;
class QTimer;
class QScrollArea;

namespace vibestudio
{
class ModelViewport;
class LevelMergeDialog final : public QDialog
{
	Q_DECLARE_TR_FUNCTIONS(LevelMergeDialog)
  public:
	LevelMergeDialog(const LevelMapDocument &document, std::shared_ptr<const PackageArchiveReader> archive = {},
					 const QString &palette = {}, QWidget *parent = nullptr);
	~LevelMergeDialog() override;
	bool isReady() const { return m_ready; }
	const LevelBrushMergePlan &plan() const { return m_plan; }
	void setRequest(const LevelBrushMergeRequest &request);
	void setApplyHandler(std::function<bool(const LevelBrushMergePlan &, QString *)> handler);
	void accept() override;

  private:
	struct Work;
	void schedule();
	void startPreview();
	void showSources();
	void updateControlWidth();
	void changeEvent(QEvent *event) override;
	LevelMapDocument m_source, m_visibleSource;
	LevelBrushMergeRequest m_request;
	LevelBrushMergePlan m_plan;
	LevelMapPreviewMesh m_mesh;
	std::shared_ptr<const PackageArchiveReader> m_archive;
	QString m_palette;
	LevelPreviewAssets m_assets;
	QListWidget *m_faces = nullptr;
	QComboBox *m_sourceFace = nullptr, *m_view = nullptr;
	QLabel *m_status = nullptr, *m_surface = nullptr;
	QProgressBar *m_progress = nullptr;
	QPushButton *m_apply = nullptr;
	ModelViewport *m_preview = nullptr;
	QWidget *m_controls = nullptr;
	QScrollArea *m_scroll = nullptr;
	QThread *m_thread = nullptr;
	QTimer *m_debounce = nullptr;
	std::shared_ptr<Work> m_work;
	std::function<bool(const LevelBrushMergePlan &, QString *)> m_applyHandler;
	quint64 m_generation = 0;
	bool m_ready = false, m_assetsReady = false, m_sourceView = false;
};
} // namespace vibestudio
