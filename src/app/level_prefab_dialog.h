#pragma once
#include "core/level_materials.h"
#include "core/level_prefab.h"
#include <QCoreApplication>
#include <QDialog>
#include <memory>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QThread;
class QTimer;

namespace vibestudio
{
class ModelViewport;
enum class LevelPrefabDialogMode { Export, Stage, Insert };
class LevelPrefabDialog final : public QDialog
{
	Q_DECLARE_TR_FUNCTIONS(LevelPrefabDialog)
  public:
	LevelPrefabDialog(const LevelMapDocument &source, LevelPrefabDialogMode mode, const QString &input = {}, bool packageEntry = false,
					  std::shared_ptr<const PackageArchiveReader> archive = {}, const QString &palette = {}, QWidget *parent = nullptr);
	~LevelPrefabDialog() override;
	bool isReady() const { return m_ready; }
	bool previewValid() const { return m_valid; }
	const LevelMapDocument &previewDocument() const { return m_candidate; }
	const LevelPrefabReport &report() const { return m_report; }
	const LevelPrefabWriteReport &writeReport() const { return m_written; }
	const LevelPrefab &prefab() const { return m_prefab; }
	void setPlacement(const LevelPrefabPlacement &placement);
	LevelPrefabPlacement placement() const;
	void setGuard(std::function<bool(QString *)> guard) { m_guard = std::move(guard); }
	void setInsertHandler(std::function<bool(const LevelMapDocument &, const LevelPrefabReport &, QString *)> handler)
	{
		m_insert = std::move(handler);
	}
	void setStageHandler(std::function<bool(const QByteArray &, const QString &, bool, QString *)> handler)
	{
		m_stage = std::move(handler);
	}
	void accept() override;
	void reject() override;

  private:
	struct Work;
	void schedule();
	void startPreview();
	void updateControlWidth();
	void changeEvent(QEvent *event) override;
	LevelPrefabDialogMode m_mode;
	LevelMapDocument m_source, m_candidate;
	QString m_input, m_palette, m_details, m_assetKey;
	bool m_packageEntry = false, m_ready = false, m_valid = false, m_updating = false, m_publishing = false, m_closed = false;
	std::shared_ptr<const PackageArchiveReader> m_archive;
	LevelPrefab m_prefab;
	QByteArray m_bytes;
	LevelPrefabReport m_report;
	LevelPrefabWriteReport m_written;
	LevelPreviewAssets m_assets;
	QString m_dependencyDetails;
	QLineEdit *m_name = nullptr, *m_output = nullptr, *m_prefix = nullptr;
	QPlainTextEdit *m_description = nullptr;
	QCheckBox *m_autoAnchor = nullptr, *m_lock = nullptr, *m_overwrite = nullptr;
	std::array<QDoubleSpinBox *, 3> m_position{}, m_rotation{};
	QWidget *m_controls = nullptr;
	QScrollArea *m_scroll = nullptr;
	QLabel *m_status = nullptr;
	QProgressBar *m_progress = nullptr;
	QPushButton *m_apply = nullptr;
	ModelViewport *m_preview = nullptr;
	QTimer *m_debounce = nullptr;
	QThread *m_thread = nullptr;
	std::shared_ptr<Work> m_work;
	quint64 m_generation = 0;
	std::function<bool(QString *)> m_guard;
	std::function<bool(const LevelMapDocument &, const LevelPrefabReport &, QString *)> m_insert;
	std::function<bool(const QByteArray &, const QString &, bool, QString *)> m_stage;
};
} // namespace vibestudio
