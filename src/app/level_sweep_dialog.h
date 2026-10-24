#pragma once
#include "core/level_materials.h"
#include "core/level_placement.h"
#include <QCoreApplication>
#include <QDialog>
#include <array>
#include <memory>
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QThread;
class QTimer;
namespace vibestudio
{
class ModelViewport;
class LevelSweepDialog final : public QDialog
{
	Q_OBJECT
  public:
	LevelSweepDialog(const LevelMapDocument& source, const LevelSweepRequest& request,
	                 std::shared_ptr<const PackageArchiveReader> archive = {}, const QString& palette = {}, QWidget* parent = nullptr);
	~LevelSweepDialog() override;
	bool isReady() const
	{
		return m_ready;
	}
	void setApplyHandler(std::function<bool(const LevelMapDocument&, QString*)> handler)
	{
		m_applyHandler = std::move(handler);
	}
	void accept() override;
	LevelSweepRequest request() const;
	void setDestination(const LevelSweepRequest& request);
	void setViewportMode();
  Q_SIGNALS:
	void scaleRequested(int direction);
	void requestChanged(const vibestudio::LevelSweepRequest& request);
	void previewChanged(bool pending, const QVector<QPair<vibestudio::LevelMapVec3, vibestudio::LevelMapVec3>>& edges,
	                    const QString& status);

  private:
	struct Work;
	void schedule();
	void start();
	void showMesh();
	LevelMapDocument m_source;
	LevelSweepRequest m_request;
	std::shared_ptr<const PackageArchiveReader> m_archive;
	QString m_palette;
	std::shared_ptr<Work> m_work;
	QThread* m_thread = nullptr;
	QTimer* m_timer = nullptr;
	std::array<std::array<QDoubleSpinBox*, 3>, 3> m_vectors{};
	QDoubleSpinBox* m_angle = nullptr;
	QSpinBox *m_segments = nullptr, *m_iterations = nullptr;
	QCheckBox* m_integer = nullptr;
	QComboBox *m_path = nullptr, *m_view = nullptr;
	QLineEdit* m_material = nullptr;
	QLabel* m_status = nullptr;
	QPlainTextEdit* m_details = nullptr;
	QProgressBar* m_progress = nullptr;
	QPushButton* m_apply = nullptr;
	ModelViewport* m_preview = nullptr;
	quint64 m_generation = 0;
	bool m_ready = false;
	std::function<bool(const LevelMapDocument&, QString*)> m_applyHandler;
};
} // namespace vibestudio
