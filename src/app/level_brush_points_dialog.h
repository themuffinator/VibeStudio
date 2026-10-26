#pragma once

#include "core/level_brush_points.h"
#include "core/level_materials.h"

#include <QCoreApplication>
#include <QDialog>

#include <functional>
#include <memory>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QThread;
class QTimer;

namespace vibestudio {
class ModelViewport;

// Create Brush from Points: type or paste four or more points, see the convex
// brush that encloses them, and add it as one undo step
// (core/level_brush_points.h). The hull is checked and previewed on a worker
// after each pause in typing; Create Brush publishes exactly that candidate.
// Workflow reference: TB MapWindow::debugCreateBrush at 4c756be1
// (GPL-3.0-or-later). Original Qt implementation; see docs/CREDITS.md.
class LevelBrushPointsDialog final : public QDialog {
	Q_DECLARE_TR_FUNCTIONS(LevelBrushPointsDialog)
public:
	// `selectionPoints` is the selection's corners as levelVertexListingText()
	// writes them, for Add Selected Corners; empty disables that button.
	LevelBrushPointsDialog(const LevelMapDocument& source, const QString& points, const QString& material, const QString& selectionPoints,
		std::shared_ptr<const PackageArchiveReader> archive = {}, const QString& palette = {}, QWidget* parent = nullptr);
	~LevelBrushPointsDialog() override;
	QString pointsText() const;
	void setPointsText(const QString& text);
	// Resolve selected corners only if the user requests them.
	void setSelectionPointsProvider(std::function<QString()> provider, bool available);
	QString material() const;
	// True once the latest text has been checked; valid() says whether it can be added.
	bool isReady() const { return m_ready; }
	bool isValid() const { return m_valid; }
	const LevelBrushPointsReport& report() const { return m_report; }
	QString statusText() const;
	// Publishes the worker-validated candidate. The caller guards the source,
	// selection and destination it was prepared from.
	void setApplyHandler(std::function<bool(const LevelMapDocument&, QString*)> handler) { m_applyHandler = std::move(handler); }
	void accept() override;
	void reject() override;

private:
	struct Work;
	void schedule();
	void start();
	void showStatus(const QString& text);
	LevelMapDocument m_source, m_candidate;
	std::shared_ptr<const PackageArchiveReader> m_archive;
	QString m_palette, m_selectionPoints;
	QPlainTextEdit* m_points = nullptr;
	QComboBox* m_material = nullptr;
	QPushButton* m_addSelection = nullptr;
	QLabel* m_status = nullptr;
	QProgressBar* m_progress = nullptr;
	QPushButton* m_apply = nullptr;
	ModelViewport* m_preview = nullptr;
	QTimer* m_debounce = nullptr;
	QThread* m_thread = nullptr;
	std::shared_ptr<Work> m_work;
	LevelBrushPointsReport m_report;
	std::function<bool(const LevelMapDocument&, QString*)> m_applyHandler;
	std::function<QString()> m_selectionPointsProvider;
	quint64 m_generation = 0;
	bool m_ready = false, m_valid = false, m_closed = false;
};
} // namespace vibestudio
