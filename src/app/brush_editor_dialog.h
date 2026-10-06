#pragma once

#include "core/level_brush.h"
#include "core/map_preview_mesh.h"
#include <QCoreApplication>
#include <QDialog>
#include <QWidget>
#include <functional>

class QAction;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QTableWidget;

namespace vibestudio
{
class ModelViewport;

class BrushComponentView final : public QWidget
{
	Q_OBJECT
  public:
	explicit BrushComponentView(QWidget *parent = nullptr);
	void setTopology(const LevelBrushTopology &topology);
	void setMode(LevelBrushComponent kind);
	void setSelection(const QVector<int> &selection);
	const QVector<int> &selection() const { return m_selected; }
	void setPlane(int plane);
	void setGrid(double grid)
	{
		m_grid = grid;
		update();
	}
	void frameBrush();
	QPointF componentPosition(int id) const;
	int componentAt(const QPointF &p) const;
	QSize minimumSizeHint() const override { return {280, 240}; }
  Q_SIGNALS:
	void selectionChanged(const QVector<int> &selection);
	void moveRequested(const vibestudio::LevelMapVec3 &delta);

  protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void keyPressEvent(QKeyEvent *) override;
	void wheelEvent(QWheelEvent *) override;
	void focusOutEvent(QFocusEvent *) override;

  private:
	QPointF project(const LevelMapVec3 &p) const;
	QPointF screen(const LevelMapVec3 &p) const;
	LevelMapVec3 movement(const QPointF &p) const;
	LevelBrushTopology m_topology;
	LevelBrushComponent m_kind = LevelBrushComponent::Vertex;
	QVector<int> m_selected;
	int m_plane = 0;
	double m_grid = 8, m_scale = 0;
	QPointF m_center, m_press, m_last, m_drag;
	bool m_dragging = false, m_panning = false;
};

class BrushEditorDialog final : public QDialog
{
	Q_DECLARE_TR_FUNCTIONS(BrushEditorDialog)
  public:
	explicit BrushEditorDialog(const LevelMapBrush &brush, double grid, QWidget *parent = nullptr);
	const LevelMapBrush &brush() const { return m_brush; }
	void selectComponents(LevelBrushComponent kind, const QVector<int> &selection);
	bool moveSelected(const LevelMapVec3 &delta, double grid = 0);
	void undo();
	void redo();
	// A failed commit keeps the draft open, including its local undo history.
	void setApplyHandler(std::function<bool(const LevelMapBrush &, QString *)> handler);
	void accept() override;

  protected:
	void showEvent(QShowEvent *) override;

  private:
	struct State {
		LevelMapBrush brush;
		LevelBrushComponent kind;
		QVector<int> selection;
	};
	State state() const;
	void restore(const State &state);
	void refresh();
	void syncSelection(const QVector<int> &selection);
	LevelMapVec3 componentCenter(int id) const;
	LevelMapBrush m_brush;
	LevelBrushTopology m_topology;
	LevelMapPreviewMesh m_mesh;
	LevelBrushComponent m_kind = LevelBrushComponent::Vertex;
	QVector<int> m_selection;
	QVector<State> m_undo, m_redo;
	BrushComponentView *m_view = nullptr;
	ModelViewport *m_preview = nullptr;
	QComboBox *m_mode = nullptr;
	QTableWidget *m_table = nullptr;
	QCheckBox *m_allowCollapse = nullptr;
	QLabel *m_status = nullptr;
	QAction *m_undoAction = nullptr;
	QAction *m_redoAction = nullptr;
	std::function<bool(const LevelMapBrush &, QString *)> m_applyHandler;
	bool m_refreshing = false, m_firstShow = true;
};
} // namespace vibestudio
