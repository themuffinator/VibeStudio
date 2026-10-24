#pragma once
#include "core/level_materials.h"
#include "core/level_placement.h"
#include <QDialog>
#include <QCoreApplication>
#include <array>
#include <memory>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QThread;
class QTimer;

namespace vibestudio {
class ModelViewport;
class LevelWrapDialog final : public QDialog {
	Q_DECLARE_TR_FUNCTIONS(LevelWrapDialog)
public:
	LevelWrapDialog(const LevelMapDocument& source, const LevelWrapRequest& request,
		std::shared_ptr<const PackageArchiveReader> archive = {}, const QString& palette = {}, QWidget* parent = nullptr);
	~LevelWrapDialog() override;
	bool isReady() const { return m_ready; }
	void setApplyHandler(std::function<bool(const LevelMapDocument&,QString*)> handler) { m_applyHandler = std::move(handler); }
	void accept() override;
private:
	struct Work;
	void schedule();
	void start();
	void showMesh();
	LevelMapDocument m_source;
	LevelWrapRequest m_request;
	std::shared_ptr<const PackageArchiveReader> m_archive;
	QString m_palette;
	std::shared_ptr<Work> m_work;
	QThread* m_thread = nullptr;
	QTimer* m_timer = nullptr;
	QSpinBox* m_count = nullptr;
	QLineEdit* m_material = nullptr;
	QComboBox* m_view = nullptr;
	std::array<std::array<QDoubleSpinBox*,3>,3> m_points{};
	QLabel* m_status = nullptr;
	QProgressBar* m_progress = nullptr;
	QPushButton* m_apply = nullptr;
	ModelViewport* m_preview = nullptr;
	quint64 m_generation = 0;
	bool m_ready = false;
	std::function<bool(const LevelMapDocument&,QString*)> m_applyHandler;
};
} // namespace vibestudio
