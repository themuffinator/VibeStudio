#pragma once
#include "core/level_map.h"
#include "core/level_materials.h"
#include <QCoreApplication>
#include <QDialog>
#include <array>
#include <functional>
#include <memory>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QThread;
class QTimer;

namespace vibestudio {
class ModelViewport;
enum class LevelPlacementMode { Duplicate, Paste };

class LevelPlacementDialog final : public QDialog {
	Q_DECLARE_TR_FUNCTIONS(LevelPlacementDialog)
  public:
	LevelPlacementDialog(const LevelMapDocument& source, LevelPlacementMode mode, const QString& clipboardText = {},
						 std::shared_ptr<const PackageArchiveReader> archive = {}, const QString& palette = {}, QWidget* parent = nullptr);
	~LevelPlacementDialog() override;
	void setOffset(const LevelMapVec3& offset);
	LevelMapVec3 offset() const;
	void setTextureLock(bool enabled);
	void setCopies(int copies);
	int copies() const;
	bool isReady() const { return m_ready; }
	bool previewValid() const { return m_valid; }
	const LevelMapDocument& previewDocument() const { return m_candidate; }
	const LevelPreviewAssets& previewAssets() const { return m_assets; }
	void setApplyHandler(std::function<bool(const LevelMapDocument&, QString*)> handler) { m_applyHandler = std::move(handler); }
	void accept() override;
	void reject() override;

  private:
	struct Work;
	void schedule();
	void startPreview();
	void updateControlWidth();
	void changeEvent(QEvent* event) override;
	LevelMapDocument m_source, m_candidate;
	LevelPreviewAssets m_assets;
	LevelPlacementMode m_mode;
	QString m_text, m_palette, m_details;
	std::shared_ptr<const PackageArchiveReader> m_archive;
	std::array<QDoubleSpinBox*, 3> m_offset{};
	QCheckBox* m_lock = nullptr;
	QSpinBox* m_copies = nullptr;
	QWidget* m_controls = nullptr;
	QScrollArea* m_scroll = nullptr;
	QLabel* m_status = nullptr;
	QProgressBar* m_progress = nullptr;
	QPushButton* m_apply = nullptr;
	ModelViewport* m_preview = nullptr;
	QTimer* m_debounce = nullptr;
	QTimer* m_progressPoll = nullptr;
	QThread* m_thread = nullptr;
	std::shared_ptr<Work> m_work;
	std::function<bool(const LevelMapDocument&, QString*)> m_applyHandler;
	quint64 m_generation = 0;
	bool m_ready = false, m_valid = false, m_updating = false, m_closed = false;
	bool m_reducedMotion = false;
};
} // namespace vibestudio
