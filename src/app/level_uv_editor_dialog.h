#pragma once

#include "core/level_materials.h"
#include "core/level_uv_edit.h"

#include <QCoreApplication>
#include <QDialog>
#include <QImage>

#include <functional>
#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QTableWidget;
class QThread;
class QTimer;

namespace vibestudio {
class LevelUvView;
class ModelViewport;

// The UV Editor: one picked brush face or one selected patch in texture
// space (LevelUvView), a live textured preview of its brush or patch beside
// it, snap and turn steps, the image size, and a point list whose U and V
// cells move points by number. Apply makes every change one undo step
// through the surface service (core/level_uv_edit.h); Cancel leaves the map
// as it was. Workflow reference: NetRadiant Custom's TexTool plugin
// (plugins/textool/TexTool.cpp at 449778b3, GPL-2.0-or-later). Original Qt
// implementation; see docs/CREDITS.md.
class LevelUvEditorDialog final : public QDialog {
	Q_DECLARE_TR_FUNCTIONS(LevelUvEditorDialog)
  public:
	// `material`, when ready, is the target's image as the Levels preview
	// already resolved it; otherwise the dialog reads it from `archive`.
	LevelUvEditorDialog(const LevelMapDocument& document, const LevelMaterialTarget& target,
		std::shared_ptr<const PackageArchiveReader> archive = {}, const QString& palette = {}, const LevelPreviewMaterial* material = nullptr,
		QWidget* parent = nullptr);
	~LevelUvEditorDialog() override;
	[[nodiscard]] LevelUvView* view() const { return m_view; }
	[[nodiscard]] ModelViewport* preview() const { return m_preview; }
	// The image is read (or known to be missing) and the view holds the surface.
	[[nodiscard]] bool isReady() const { return m_ready; }
	// Uses this image size instead of the material's, as the texture size
	// fields do. Allowed while nothing has changed.
	void setTextureSizeOverride(QSize size);
	void setTurnStep(double degrees);
	void setApplyHandler(std::function<bool(const LevelSurfaceEditPlan&, QString*)> handler);
	// The plan Apply commits, against the map the dialog opened with.
	bool preparePlan(LevelSurfaceEditPlan* plan, QString* error = nullptr, LevelUvFit* fit = nullptr) const;
	void accept() override;

  protected:
	void changeEvent(QEvent* event) override;

  private:
	struct Load;
	void startLoad();
	void finishLoad(const LevelPreviewAssets& assets);
	void loadLayout();
	void refreshControls();
	void refreshPoints();
	void rebuildPreview();
	[[nodiscard]] QSize effectiveSize() const;
	void updateControlWidth();

	LevelMapDocument m_source, m_visible;
	LevelMaterialTarget m_target;
	std::shared_ptr<const PackageArchiveReader> m_archive;
	QString m_palette;
	QString m_material;
	LevelPreviewAssets m_assets;
	QImage m_image;
	QSize m_materialSize;
	QString m_materialNote;
	LevelUvView* m_view = nullptr;
	ModelViewport* m_preview = nullptr;
	QWidget* m_controls = nullptr;
	QScrollArea* m_scroll = nullptr;
	QLabel* m_targetLabel = nullptr;
	QLabel* m_materialLabel = nullptr;
	QLabel* m_status = nullptr;
	QProgressBar* m_progress = nullptr;
	QComboBox* m_snap = nullptr;
	QDoubleSpinBox* m_turnStep = nullptr;
	QCheckBox* m_allowValve220 = nullptr;
	QCheckBox* m_override = nullptr;
	QSpinBox* m_width = nullptr;
	QSpinBox* m_height = nullptr;
	QPushButton* m_undo = nullptr;
	QPushButton* m_redo = nullptr;
	QPushButton* m_reset = nullptr;
	QPushButton* m_anchor = nullptr;
	QPushButton* m_apply = nullptr;
	QTableWidget* m_points = nullptr;
	QTimer* m_previewTimer = nullptr;
	QThread* m_thread = nullptr;
	std::shared_ptr<Load> m_load;
	std::function<bool(const LevelSurfaceEditPlan&, QString*)> m_applyHandler;
	bool m_ready = false;
	bool m_refreshing = false;
};

} // namespace vibestudio
