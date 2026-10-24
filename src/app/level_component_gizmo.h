#pragma once

#include "core/level_brush.h"
#include "core/model_transform.h"
#include <QObject>
#include <QPolygonF>
#include <QFont>
#include <functional>

class QPainter;
namespace vibestudio {
enum class LevelComponentTool { Move, Rotate, Scale, Shear, Pivot };
struct LevelComponentGizmoFrame {
	std::function<QPointF(const LevelMapVec3&)> project;
	std::function<ModelPickRay(const QPointF&)> ray;
	std::function<bool(const LevelMapVec3&)> visible;
	ModelVec3 viewDirection;
	double radius = 64;
	bool perspective = false, worldAxes = false;
	int rotationAxis = -1; // Optional single ring for a tool in an orthographic plan.
};
struct LevelComponentGizmoHandle {
	int id = -1, axis = 0;
	QPolygonF points;
	QPointF label;
	QString text;
};

// Shared, non-document interaction state for the plan and camera. The owner
// supplies projection/rays and a checked asynchronous geometry response.
// Only the latest serial can publish; release waits for that response.
class LevelComponentGizmo final : public QObject {
	Q_OBJECT
public:
	explicit LevelComponentGizmo(QObject* parent = nullptr);
	void setContext(LevelComponentTool tool, const LevelMapVec3& pivot, bool available, double grid,
		double angleStep = 15, double scaleStep = .125, double shearStep = .125);
	[[nodiscard]] LevelComponentTool tool() const { return m_tool; }
	[[nodiscard]] LevelMapVec3 pivot() const { return m_pivot; }
	[[nodiscard]] bool active() const { return m_active; }
	[[nodiscard]] bool pending() const { return m_pending; }
	[[nodiscard]] bool releasePending() const { return m_releasePending; }
	[[nodiscard]] quint64 serial() const { return m_serial; }
	[[nodiscard]] const LevelComponentTransformRequest& request() const { return m_request; }
	[[nodiscard]] QString summary() const;
	[[nodiscard]] QVector<LevelComponentGizmoHandle> handles(const LevelComponentGizmoFrame& frame) const;
	[[nodiscard]] int hitTest(const QPointF& point, const LevelComponentGizmoFrame& frame, const QFont& font = {}) const;
	bool begin(const QPointF& point, const LevelComponentGizmoFrame& frame, const QFont& font = {});
	bool updateDrag(const QPointF& point);
	void finish(bool commit);
	void setPreview(quint64 serial, const QVector<QPair<LevelMapVec3,LevelMapVec3>>& edges, const QString& error);
	[[nodiscard]] LevelMapVec3 transformed(const LevelMapVec3& point) const;
	void paint(QPainter& painter, const LevelComponentGizmoFrame& frame, bool highContrast) const;
Q_SIGNALS:
	void activeChanged(bool active);
	void previewRequested(quint64 serial);
	void commitRequested(quint64 serial);
	void pivotChanged(const vibestudio::LevelMapVec3& pivot);
	void changed();
private:
	void refuse(const QString& error);
	LevelComponentTool m_tool = LevelComponentTool::Move;
	LevelMapVec3 m_pivot;
	bool m_available = false, m_active = false, m_pending = false, m_releasePending = false, m_valid = true, m_travelled = false, m_linearRotation = false;
	double m_grid = 0, m_angleStep = 15, m_scaleStep = .125, m_shearStep = .125;
	int m_handle = -1;
	quint64 m_serial = 0;
	LevelComponentGizmoFrame m_frame;
	LevelComponentTransformRequest m_request;
	ModelMoveDrag m_move;
	ModelRotateDrag m_rotate;
	QPointF m_press, m_tangent;
	QString m_error;
	QVector<QPair<LevelMapVec3,LevelMapVec3>> m_edges;
};
} // namespace vibestudio
