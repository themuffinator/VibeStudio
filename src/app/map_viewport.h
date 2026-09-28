#pragma once

// Interactive 2D map viewport.
//
// Draws real geometry for Doom-family maps (vertices, linedefs, sector fills,
// things) and orthographic brush/patch outlines for Quake-family maps, using
// the shared solver in core/map_geometry so the picture matches map statistics
// and the headless SVG renderer.
//
// Direct manipulation lives here: a rubber band over empty space selects, a
// drag on a selected object moves the whole selection snapped to the grid, and
// arrow keys nudge it by one grid step. The widget never edits the document
// itself - it previews the move locally and emits moveRequested() once on
// release, so the owner decides what becomes a single undo command.

#include "core/level_map.h"
#include "core/map_geometry.h"

#include <QHash>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

namespace vibestudio {

enum class MapViewportProjection {
	TopXY,
	FrontXZ,
	SideZY,
};

struct MapViewportHit {
	LevelMapSelectionKind kind = LevelMapSelectionKind::None;
	int objectId = -1;
	QString label;
	double worldX = 0.0;
	double worldY = 0.0;
};

class MapViewport final : public QWidget {
	Q_OBJECT

public:
	explicit MapViewport(QWidget* parent = nullptr);
	~MapViewport() override;

	// Loads a document and refits the view. Use updateDocument() after an edit
	// so the user keeps their pan and zoom.
	void setDocument(const LevelMapDocument& document);
	// Re-reads geometry and selection from an edited document without touching
	// the camera. Also cancels any drag or rubber band in progress.
	void updateDocument(const LevelMapDocument& document);
	void clearDocument();
	[[nodiscard]] bool hasDocument() const;

	void setProjection(MapViewportProjection projection);
	[[nodiscard]] MapViewportProjection projection() const;

	// Single-selection entry point, kept for existing callers. Setting the
	// object that is already primary leaves a multi-selection intact, so a
	// refresh driven by the document's primary fields cannot silently collapse
	// a set the user built here.
	void setSelection(LevelMapSelectionKind kind, int objectId);
	void setSelectionSet(const QVector<LevelMapSelectionRef>& selection);
	[[nodiscard]] QVector<LevelMapSelectionRef> selectionSet() const;
	[[nodiscard]] LevelMapSelectionKind selectionKind() const;
	[[nodiscard]] int selectedObjectId() const;

	void setGridSize(int units);
	[[nodiscard]] int gridSize() const;
	// Grid snapping for drags and nudges. On by default; off moves by the raw
	// pointer delta instead.
	void setSnapToGrid(bool enabled);
	[[nodiscard]] bool snapToGrid() const;
	void setShowGrid(bool show);
	void setShowThings(bool show);
	void setShowSectorFill(bool show);
	void setShowVertices(bool show);
	void setShowLabels(bool show);
	void setHighContrast(bool enabled);
	void setReducedMotion(bool enabled);

	void zoomToFit();
	void zoomIn();
	void zoomOut();
	void resetView();
	[[nodiscard]] double zoom() const;

	[[nodiscard]] bool isDragging() const;
	// Cancels a drag or rubber band in progress and restores the preview.
	void cancelInteraction();

	[[nodiscard]] QString hoverSummary() const;
	[[nodiscard]] QStringList statusLines() const;
	[[nodiscard]] QString accessibleSummary() const;

	[[nodiscard]] QSize sizeHint() const override;
	[[nodiscard]] QSize minimumSizeHint() const override;

Q_SIGNALS:
	// Primary selection, unchanged: `selectionKind` is a LevelMapSelectionKind
	// cast to int.
	void selectionChanged(int selectionKind, int objectId);
	// The whole selection set, in add order, primary last. Emitted together
	// with selectionChanged() whenever the set changes.
	void selectionSetChanged(const QVector<LevelMapSelectionRef>& selection);
	// A finished drag or an arrow-key nudge. The delta is already snapped to the
	// grid and is never all-zero. The receiver is expected to turn this into one
	// undo command over the current selection, for example with
	// moveLevelMapSelection().
	void moveRequested(double dx, double dy, double dz);
	void hoverChanged(const QString& summary);
	void viewChanged();

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	struct Palette;

	void rebuildGeometry();
	void updateWorldBounds();
	[[nodiscard]] QPointF worldToView(double x, double y) const;
	[[nodiscard]] QPointF viewToWorld(const QPointF& point) const;
	[[nodiscard]] QPointF projectPoint(const LevelMapVec3& point) const;
	[[nodiscard]] MapViewportHit hitTest(const QPointF& viewPoint) const;
	void paintGrid(QPainter& painter, const Palette& palette) const;
	void paintDoom(QPainter& painter, const Palette& palette) const;
	void paintQuake(QPainter& painter, const Palette& palette) const;
	void paintEmptyState(QPainter& painter, const Palette& palette) const;
	void paintOverlay(QPainter& painter, const Palette& palette) const;
	// Corner readouts in the style of idStudio's viewports: view and grid in
	// the top-left, what the map holds in the top-right.
	void paintHud(QPainter& painter, const Palette& palette) const;
	void paintSelectionMarkers(QPainter& painter, const Palette& palette) const;
	void paintDragPreview(QPainter& painter, const Palette& palette) const;
	void paintRubberBand(QPainter& painter, const Palette& palette) const;
	void announceSelection();
	void adoptDocumentSelection();
	void syncPrimaryFromSelection();
	void setSelectionSetInternal(const QVector<LevelMapSelectionRef>& selection);

	[[nodiscard]] bool selectionContains(LevelMapSelectionKind kind, int objectId) const;
	void replaceSelection(LevelMapSelectionKind kind, int objectId);
	void addToSelection(LevelMapSelectionKind kind, int objectId);
	void toggleInSelection(LevelMapSelectionKind kind, int objectId);
	[[nodiscard]] QVector<LevelMapSelectionRef> objectsInWorldRect(const QRectF& rect) const;
	[[nodiscard]] QRectF bandWorldRect() const;

	void beginDrag(const QPointF& viewPoint);
	void updateDrag(const QPointF& viewPoint);
	void commitDrag();
	void cancelDrag();
	[[nodiscard]] QPointF snappedPlaneDelta() const;
	// Maps a delta in the projected drawing plane back to world x/y/z. The axis
	// the projection drops never moves.
	void planeDeltaToWorld(const QPointF& planeDelta, double* dx, double* dy, double* dz) const;
	void requestMove(const QPointF& planeDelta);
	[[nodiscard]] QString dragSummary() const;

	LevelMapDocument m_document;
	bool m_hasDocument = false;
	QVector<MapBrushGeometry> m_brushGeometry;
	QVector<DoomSectorOutline> m_sectorOutlines;
	MapViewportProjection m_projection = MapViewportProjection::TopXY;
	LevelMapSelectionKind m_selectionKind = LevelMapSelectionKind::None;
	int m_selectedObjectId = -1;
	// Multi-selection, primary last. m_selectionKind / m_selectedObjectId always
	// mirror its back().
	QVector<LevelMapSelectionRef> m_selection;
	MapViewportHit m_hover;
	QRectF m_worldBounds;
	QPointF m_worldCenter;
	double m_zoom = 1.0;
	int m_gridSize = 64;
	bool m_snapToGrid = true;
	bool m_showGrid = true;
	bool m_showThings = true;
	bool m_showSectorFill = true;
	bool m_showVertices = false;
	bool m_showLabels = false;
	bool m_highContrast = false;
	bool m_reducedMotion = false;
	bool m_panning = false;
	QPointF m_panAnchorView;
	QPointF m_panAnchorCenter;
	// A left press on a selected object arms a drag; the drag only starts once
	// the pointer has actually moved, so a plain click still selects.
	bool m_pressArmed = false;
	QPointF m_pressViewPoint;
	bool m_dragging = false;
	QPointF m_dragAnchorPlane;
	QPointF m_dragCurrentPlane;
	bool m_banding = false;
	QPointF m_bandAnchorView;
	QPointF m_bandCurrentView;
	Qt::KeyboardModifiers m_bandModifiers = Qt::NoModifier;
};

QString mapViewportProjectionDisplayName(MapViewportProjection projection);

} // namespace vibestudio
