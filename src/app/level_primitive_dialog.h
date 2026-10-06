#pragma once

#include "core/level_materials.h"
#include "core/level_primitive.h"
#include <QCoreApplication>
#include <QDialog>
#include <memory>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QThread;
class QTimer;

namespace vibestudio
{
class ModelViewport;
class LevelPrimitiveDialog final : public QDialog
{
	Q_DECLARE_TR_FUNCTIONS(LevelPrimitiveDialog)
  public:
	LevelPrimitiveDialog(const LevelMapDocument &source, const LevelBrushPrimitiveRequest &request,
						 std::shared_ptr<const PackageArchiveReader> archive = {}, const QString &palette = {}, QWidget *parent = nullptr);
	~LevelPrimitiveDialog() override;
	LevelBrushPrimitiveRequest request() const;
	void setRequest(const LevelBrushPrimitiveRequest &request);
	bool isReady() const { return m_ready; }
	bool previewValid() const { return m_valid; }
	const LevelMapDocument &previewDocument() const { return m_previewDocument; }
	// Publishes the exact worker-validated preview. The caller must guard the
	// source/selection/asset snapshot before accepting this candidate.
	void setApplyHandler(std::function<bool(const LevelMapDocument &, QString *)> handler);
	void accept() override;
	void reject() override;

  private:
	struct Work;
	void schedule();
	void startPreview();
	void updateControlWidth();
	void changeEvent(QEvent *event) override;
	LevelMapDocument m_source, m_previewDocument;
	std::shared_ptr<const PackageArchiveReader> m_archive;
	QString m_palette, m_cachedMaterial;
	LevelPreviewAssets m_assets;
	QComboBox *m_shape = nullptr, *m_axis = nullptr, *m_texture = nullptr;
	std::array<QDoubleSpinBox *, 3> m_center{}, m_size{};
	QSpinBox *m_sides = nullptr, *m_bands = nullptr;
	QWidget *m_controls = nullptr;
	QScrollArea *m_scroll = nullptr;
	QLabel *m_status = nullptr;
	QProgressBar *m_progress = nullptr;
	QPushButton *m_apply = nullptr;
	ModelViewport *m_preview = nullptr;
	QTimer *m_debounce = nullptr;
	QThread *m_thread = nullptr;
	std::shared_ptr<Work> m_work;
	std::function<bool(const LevelMapDocument &, QString *)> m_applyHandler;
	quint64 m_generation = 0;
	bool m_ready = false, m_valid = false, m_updating = false, m_closed = false;
};
} // namespace vibestudio
