#pragma once
#include "core/level_materials.h"
#include "core/level_patch_cap.h"
#include <QCoreApplication>
#include <QDialog>
#include <memory>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QPlainTextEdit;
class QPushButton;
class QProgressBar;
class QScrollArea;
class QTimer;
class QThread;
namespace vibestudio
{
class ModelViewport;
class PatchCapDialog final : public QDialog
{
	Q_DECLARE_TR_FUNCTIONS(PatchCapDialog)
  public:
	PatchCapDialog(const LevelMapDocument &source, std::shared_ptr<const PackageArchiveReader> archive = {}, const QString &palette = {},
				   QWidget *parent = nullptr);
	~PatchCapDialog() override;
	LevelPatchCapRequest request() const;
	int patchId() const;
	bool isReady() const { return m_ready; }
	bool previewValid() const { return m_valid; }
	const LevelPatchCapResult &capResult() const { return m_result; }
	void setApplyHandler(std::function<bool(int, const LevelPatchCapRequest &, QString *)> handler);
	void accept() override;

  private:
	struct Work;
	void schedule();
	void startPreview();
	void updateControlWidth();
	void changeEvent(QEvent *) override;
	LevelMapDocument m_source;
	std::shared_ptr<const PackageArchiveReader> m_archive;
	QString m_palette;
	LevelPatchCapResult m_result;
	LevelPreviewAssets m_assets;
	QComboBox *m_patch = nullptr, *m_boundary = nullptr, *m_texture = nullptr, *m_uv = nullptr, *m_view = nullptr;
	QCheckBox *m_opposite = nullptr, *m_custom = nullptr, *m_invert = nullptr;
	QDoubleSpinBox *m_units = nullptr, *m_x = nullptr, *m_y = nullptr, *m_z = nullptr;
	QPlainTextEdit *m_status = nullptr;
	QPushButton *m_apply = nullptr;
	QProgressBar *m_progress = nullptr;
	QScrollArea *m_scroll = nullptr;
	QWidget *m_controls = nullptr;
	ModelViewport *m_preview = nullptr;
	QTimer *m_debounce = nullptr;
	QThread *m_thread = nullptr;
	std::shared_ptr<Work> m_work;
	std::function<bool(int, const LevelPatchCapRequest &, QString *)> m_handler;
	quint64 m_generation = 0;
	bool m_ready = false, m_valid = false;
};
} // namespace vibestudio
