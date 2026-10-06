#pragma once
#include "core/level_materials.h"
#include "core/level_patch_stitch.h"
#include <QCoreApplication>
#include <QDialog>
#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QThread;
class QTimer;
namespace vibestudio
{
class ModelViewport;
class PatchStitchDialog final : public QDialog
{
	Q_DECLARE_TR_FUNCTIONS(PatchStitchDialog)
  public:
	PatchStitchDialog(const LevelMapDocument &source, std::shared_ptr<const PackageArchiveReader> archive = {}, const QString &palette = {},
					  QWidget *parent = nullptr);
	~PatchStitchDialog() override;
	LevelPatchStitchRequest request() const;
	void setRequest(const LevelPatchStitchRequest &request);
	int firstId() const;
	int secondId() const;
	bool isReady() const { return m_ready; }
	bool previewValid() const { return m_valid; }
	const LevelPatchStitchResult &stitchResult() const { return m_result; }
	void setApplyHandler(std::function<bool(int, int, const LevelPatchStitchRequest &, QString *)> handler);
	void accept() override;

  private:
	struct Work;
	void schedule();
	void startPreview();
	void updateControlWidth();
	void changeEvent(QEvent *) override;
	LevelMapDocument m_source;
	LevelPatchStitchResult m_result;
	LevelPreviewAssets m_assets;
	std::shared_ptr<const PackageArchiveReader> m_archive;
	QString m_palette;
	QComboBox *m_first = nullptr, *m_second = nullptr, *m_firstEdge = nullptr, *m_secondEdge = nullptr, *m_order = nullptr,
			  *m_target = nullptr, *m_uv = nullptr, *m_view = nullptr;
	QDoubleSpinBox *m_gap = nullptr;
	QCheckBox *m_tangents = nullptr;
	QPlainTextEdit *m_status = nullptr;
	QProgressBar *m_progress = nullptr;
	QPushButton *m_apply = nullptr;
	QScrollArea *m_scroll = nullptr;
	QWidget *m_controls = nullptr;
	ModelViewport *m_preview = nullptr;
	QTimer *m_debounce = nullptr;
	QThread *m_thread = nullptr;
	std::shared_ptr<Work> m_work;
	std::function<bool(int, int, const LevelPatchStitchRequest &, QString *)> m_handler;
	quint64 m_generation = 0;
	bool m_ready = false, m_valid = false, m_setting = false;
};
} // namespace vibestudio
