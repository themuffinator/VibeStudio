#pragma once
#include "app/level_component_gizmo.h"
#include "core/level_component_drag.h"
#include "core/level_sweep_controls.h"

namespace vibestudio
{
// One destination draft shared by every editor view. Pointer events are owned
// by the view binding; these semantic methods are also usable without input.
class LevelSweepGizmo final : public QObject
{
	Q_OBJECT
  public:
	explicit LevelSweepGizmo(LevelSweepSource source, LevelSweepRequest request, QObject* parent = nullptr);
	const LevelSweepRequest& request() const
	{
		return m_request;
	}
	const LevelSweepSource& source() const
	{
		return m_source;
	}
	bool dragging() const
	{
		return m_handle >= 0;
	}
	void setRequest(const LevelSweepRequest& request);
	void setSteps(double grid, double angleStep);
	void setPreview(bool pending, const QVector<QPair<LevelMapVec3, LevelMapVec3>>& edges, const QString& status);
	LevelMapVec3 centre() const;
	LevelMapVec3 scaleHandle() const;
	QVector<LevelComponentGizmoHandle> rotationHandles(const LevelComponentGizmoFrame& frame) const;
	// Handle IDs: 0..2 rotation rings, 3 centre, 4 scale arm; -1 misses.
	int hitTest(QPointF point, const LevelComponentGizmoFrame& frame, const QFont& font = {}) const;
	bool begin(QPointF point, const LevelComponentGizmoFrame& frame, Qt::KeyboardModifiers modifiers = Qt::NoModifier,
	           const QFont& font = {});
	bool updateDrag(QPointF point, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
	void finish(bool keep);
	bool cancel(); // Cancel drag, otherwise reset transform; false means leave tool.
	void reset();
	bool scaleStep(int direction);
	// Moves the destination by `delta` world units, as a nudge key does; false
	// during a drag or for an invalid delta.
	bool translateDestination(const LevelMapVec3& delta);
	// Turns the destination by `degrees`, right-handed about the world vector
	// `axis`, as a roll, yaw or pitch key does; false during a drag or for an
	// invalid axis or angle, with the reason in `error`.
	bool rotateDestination(const LevelMapVec3& axis, double degrees, QString* error = nullptr);
	// The rotation step the rings snap to, which the turn keys use too.
	double angleStep() const
	{
		return m_angleStep;
	}
	QString summary() const;
	void paint(QPainter&, const LevelComponentGizmoFrame&, bool highContrast) const;
  Q_SIGNALS:
	void requestChanged(const vibestudio::LevelSweepRequest& request);
	void changed();

  private:
	void publish();
	void refreshRotation();
	LevelSweepSource m_source;
	LevelSweepRequest m_request, m_before;
	LevelComponentGizmo m_rotation;
	LevelComponentDrag m_move;
	LevelComponentGizmoFrame m_frame;
	QPointF m_press;
	int m_handle = -1;
	double m_grid = 16, m_angleStep = 15;
	bool m_pending = true, m_travelled = false;
	QString m_status, m_error;
	QVector<QPair<LevelMapVec3, LevelMapVec3>> m_edges;
};
} // namespace vibestudio
