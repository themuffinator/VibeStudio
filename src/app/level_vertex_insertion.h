#pragma once
#include "app/level_component_gizmo.h"
#include "core/level_component_drag.h"
#include <memory>

namespace vibestudio {
struct LevelVertexInsertionBrush {
	int id = -1;
	LevelBrushTopology topology;
};
using LevelVertexInsertionData = QVector<LevelVertexInsertionBrush>;
struct LevelVertexInsertionHit {
	LevelMapVec3 position;
	LevelBrushComponent kind = LevelBrushComponent::Edge;
	QVector<int> brushes;
	QString geometryKey;
};

// Original implementation of edge/face grid-point insertion, informed by
// TrenchBroom v2026.2 VertexTool.cpp / VertexToolController.cpp (GPL-3.0-or-later).
// https://github.com/TrenchBroom/TrenchBroom/tree/v2026.2/lib/TbUiLib/src
// Cached topology only: no hull rebuild on pointer events. Both view types
// share the controller, preview serials and one atomic core transaction.
class LevelVertexInsertion final : public QObject {
	Q_OBJECT
public:
	explicit LevelVertexInsertion(QObject* parent = nullptr) : QObject(parent) {}
	void setContext(std::shared_ptr<const LevelVertexInsertionData> data, bool available, bool tool, double grid);
	[[nodiscard]] bool tool() const { return m_tool; }
	[[nodiscard]] bool active() const { return m_active; }
	[[nodiscard]] bool pending() const { return m_pending; }
	[[nodiscard]] bool releasePending() const { return m_releasePending; }
	[[nodiscard]] quint64 serial() const { return m_serial; }
	[[nodiscard]] const LevelVertexInsertionHit& candidate() const { return m_hit; }
	[[nodiscard]] LevelMapVec3 position() const { return m_position; }
	[[nodiscard]] QVector<LevelMapVertexInsertion> request() const;
	[[nodiscard]] LevelVertexInsertionHit pick(const QPointF& point, const LevelComponentGizmoFrame& frame) const;
	bool hover(const QPointF& point, const LevelComponentGizmoFrame& frame, bool show);
	void clearHover();
	bool begin(const QPointF& point, const LevelComponentGizmoFrame& frame, ModelMoveConstraint constraint = ModelMoveConstraint::ViewPlane, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
	bool updateDrag(const QPointF& point) { return updateDrag(point,m_modifiers); }
	bool updateDrag(const QPointF& point, Qt::KeyboardModifiers modifiers);
	bool updateModifiers(Qt::KeyboardModifiers modifiers) { return updateDrag(m_lastPoint,modifiers); }
	void finish(bool commit);
	void setPreview(quint64 serial, const QVector<QPair<LevelMapVec3,LevelMapVec3>>& edges, const QString& error);
	[[nodiscard]] QString summary() const;
	void paint(QPainter& painter, const LevelComponentGizmoFrame& frame, bool highContrast) const;
Q_SIGNALS:
	void activeChanged(bool active);
	void previewRequested(quint64 serial);
	void commitRequested(quint64 serial);
	void changed();
private:
	std::shared_ptr<const LevelVertexInsertionData> m_data;
	LevelVertexInsertionHit m_hit;
	LevelMapVec3 m_position;
	LevelComponentGizmoFrame m_frame;
	ModelMoveDrag m_drag;
	LevelComponentDrag m_axisDrag;
	QPointF m_press, m_lastPoint;
	Qt::KeyboardModifiers m_modifiers = Qt::NoModifier;
	bool m_available = false, m_tool = false, m_active = false, m_pending = false, m_releasePending = false, m_valid = false;
	double m_grid = 0;
	quint64 m_serial = 0;
	QString m_error;
	QVector<QPair<LevelMapVec3,LevelMapVec3>> m_edges;
};
} // namespace vibestudio
