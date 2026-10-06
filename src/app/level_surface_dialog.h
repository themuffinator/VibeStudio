#pragma once

#include "core/level_materials.h"
#include "core/level_surface.h"
#include <QCoreApplication>
#include <QDialog>
#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QThread;
class QTimer;

namespace vibestudio
{
class ModelViewport;
class LevelSurfaceDialog final : public QDialog
{
	Q_DECLARE_TR_FUNCTIONS(LevelSurfaceDialog)
  public:
	LevelSurfaceDialog(const LevelMapDocument &document, const QVector<LevelSurfaceFace> &faces,
					   std::shared_ptr<const PackageArchiveReader> archive = {}, const QString &palette = {}, QWidget *parent = nullptr);
	~LevelSurfaceDialog() override;
	void setRequest(const LevelSurfaceRequest &request);
	LevelSurfaceRequest request() const;
	void setSelectedFaces(const QVector<LevelSurfaceFace> &faces);
	QVector<LevelSurfaceFace> selectedFaces() const;
	void setTextureSizeOverride(QSize size);
	void setApplyHandler(std::function<bool(const LevelSurfaceEditPlan &, QString *)> handler);
	bool isReady() const { return m_ready; }
	bool previewValid() const { return m_valid; }
	const LevelMapDocument &previewDocument() const { return m_previewDocument; }
	void accept() override;

  private:
	struct Work;
	void schedule();
	void startPreview();
	void updateControls();
	void updateControlWidth();
	void changeEvent(QEvent *event) override;
	LevelMapDocument m_source, m_previewDocument, m_visibleSource;
	QVector<LevelSurfaceFace> m_faces;
	std::shared_ptr<const PackageArchiveReader> m_archive;
	QString m_palette;
	LevelPreviewAssets m_assets;
	LevelSurfaceEditPlan m_plan;
	LevelMapPreviewMesh m_previewMesh;
	QWidget *m_controls = nullptr;
	QScrollArea *m_scroll = nullptr;
	QListWidget *m_faceList = nullptr;
	QComboBox *m_operation = nullptr;
	QDoubleSpinBox *m_x = nullptr, *m_y = nullptr, *m_degrees = nullptr;
	QLabel *m_xLabel = nullptr, *m_yLabel = nullptr;
	QComboBox *m_alignU = nullptr, *m_alignV = nullptr;
	QCheckBox *m_override = nullptr;
	QSpinBox *m_width = nullptr, *m_height = nullptr;
	QLabel *m_status = nullptr, *m_materialStatus = nullptr;
	QProgressBar *m_progress = nullptr;
	QPushButton *m_apply = nullptr;
	ModelViewport *m_preview = nullptr;
	QTimer *m_debounce = nullptr;
	QThread *m_thread = nullptr;
	std::shared_ptr<Work> m_work;
	std::function<bool(const LevelSurfaceEditPlan &, QString *)> m_applyHandler;
	quint64 m_generation = 0;
	bool m_ready = false, m_valid = false, m_updating = false, m_assetsReady = false;
};
} // namespace vibestudio
