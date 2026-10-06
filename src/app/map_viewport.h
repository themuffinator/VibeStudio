#pragma once

// Interactive 2D map viewport.
//
// Draws real geometry for Doom-family maps (vertices, linedefs, sector fills,
// things) and orthographic brush/patch outlines for Quake-family maps, using
// the shared solver in core/map_geometry so the picture matches map statistics
// and the headless SVG renderer.
//
// Direct manipulation lives here: a rubber band over empty space selects, a
// drag on a selected object moves the whole selection snapped to the grid,
// arrow keys nudge it by one grid step, and the handles on the selection's
// box resize it. The widget never edits the document itself - it previews
// the change locally and emits moveRequested() or resizeRequested() once on
// release, so the owner decides what becomes a single undo command.

#include "core/level_editor_controls.h"
#include "core/level_navigation.h"
#include "core/level_map.h"
#include "core/map_geometry.h"
#include "core/map_geometry_cache.h"
#include "app/map_viewport_scene_index.h"
#include "app/map_plan_wires.h"
#include "app/map_plan_render_worker.h"
#include "app/map_viewport_overlay_worker.h"
#include "app/viewport_ring_cache.h"
#include "app/map_grid.h"

#include <QHash>
#include <QKeySequence>
#include <QPair>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

namespace vibestudio {

struct ViewportHudLayout;

struct MapViewportHit {
	LevelMapSelectionKind kind = LevelMapSelectionKind::None;
	int objectId = -1;
	QString label;
	double worldX = 0.0;
	double worldY = 0.0;
};

// Optional GUI paint measurements, separate from background image completion.
// The overlay duration includes its marker, handle and HUD child measurements.
struct MapViewportPaintStatistics {
	qint64 totalNs = 0, gridNs = 0, geometryNs = 0, overlayNs = 0;
	qint64 selectionMarkersNs = 0, resizeHandlesNs = 0, hudNs = 0;
};

class MapViewport final : public QWidget {
	Q_OBJECT

public:
	explicit MapViewport(QWidget* parent = nullptr);
	~MapViewport() override;

	// Loads a document and refits the view. Use updateDocument() after an edit
	// so the user keeps their pan and zoom.
	void setDocument(const LevelMapDocument& document, const MapBrushGeometryCache* prepared = nullptr);
	// Re-reads geometry and selection from an edited document without touching
	// the camera. Also cancels any drag or rubber band in progress.
	void updateDocument(const LevelMapDocument& document);
	void clearDocument();
	[[nodiscard]] bool hasDocument() const;
	// Diagnostics for the most recent scene rebuild; retained bytes are an
	// estimate of cache payload, separate from the document and visible scene.
	[[nodiscard]] MapBrushGeometryCacheStatistics geometryCacheStatistics() const { return m_brushGeometryCache.statistics(); }
	[[nodiscard]] MapPlanWireStatistics planWireStatistics() const { return m_planWires.statistics; }
	[[nodiscard]] MapPlanWireStatistics selectionWireStatistics() const { return m_selectionWires.statistics; }
	[[nodiscard]] bool isRendering() const { return m_planRenderPending || m_overlayRenderPending; }
	[[nodiscard]] bool isOverlayRendering() const { return m_overlayRenderPending; }
	void setPaintStatisticsEnabled(bool enabled) { m_measurePainting = enabled; m_paintStatistics = {}; }
	[[nodiscard]] MapViewportPaintStatistics paintStatistics() const { return m_paintStatistics; }
	// Share the already solved scene, visibility and selection with another
	// orthographic pane. Projection, pan, zoom and local tools stay independent.
	// This avoids solving every brush again for each pane of a four-view layout.
	void synchronizeSceneFrom(const MapViewport& source, bool frame = false);
	void setActivePane(bool active);
	[[nodiscard]] bool isActivePane() const { return m_activePane; }

	void setProjection(MapViewportProjection projection);
	[[nodiscard]] MapViewportProjection projection() const;
	[[nodiscard]] PlanViewState navigationState() const;
	bool restoreNavigationState(const PlanViewState& state);
	// Linked navigation changes only pan/zoom. It preserves tools, focus, and
	// world-space editing anchors, and never changes projection or selection.
	bool applyLinkedNavigation(const PlanViewState& state);

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
	[[nodiscard]] bool showGrid() const;
	void setShowThings(bool show);
	[[nodiscard]] bool showThings() const;
	void setShowSectorFill(bool show);
	[[nodiscard]] bool showSectorFill() const;
	void setShowVertices(bool show);
	[[nodiscard]] bool showVertices() const;
	void setShowLabels(bool show);
	[[nodiscard]] bool showLabels() const;
	// Arrows from each entity's target-style keys to the entities they name.
	// Links that touch the selection are drawn heavier either way.
	void setShowTargetLinks(bool show);
	[[nodiscard]] bool showTargetLinks() const;
	[[nodiscard]] int targetLinkCount() const;
	// Doom tag links (see levelMapTagLinks) the view knows, and how many it
	// draws now: those touching the selection, or all while links are shown.
	[[nodiscard]] int tagLinkCount() const;
	[[nodiscard]] int drawnTagLinkCount() const;
	void setHighContrast(bool enabled);
	void setReducedMotion(bool enabled);

	void zoomToFit();
	// Frames the selection with a margin; a single point object is centred at
	// a comfortable zoom rather than magnified without limit. Falls back to
	// the whole map when nothing is selected.
	void zoomToSelection();

	// The leak trail from a compiler's point file (.pts or .lin), drawn over
	// the map in every projection until cleared or another map is loaded.
	// World coordinates, in the order the file lists them.
	void setLeakTrail(const QVector<LevelMapVec3>& points);
	void clearLeakTrail();
	[[nodiscard]] bool hasLeakTrail() const;
	// Frames the whole trail, like zoomToSelection() does for objects.
	void zoomToLeakTrail();
	void zoomIn();
	void zoomOut();
	void resetView();
	[[nodiscard]] double zoom() const;

	// The map position under `viewPoint`, snapped to the grid while snapping
	// is on. The axis the projection hides, z in the top view, takes
	// `hiddenAxisValue`.
	[[nodiscard]] LevelMapVec3 worldPositionAt(const QPointF& viewPoint, double hiddenAxisValue) const;
	// Where a point of the view's plane, in map units, is drawn, in widget
	// pixels.
	[[nodiscard]] QPointF viewPointFor(double x, double y) const;

	// How the mouse and arrow keys work here, per editor profile (see
	// core/level_editor_controls.h). The defaults are the VibeStudio ones.
	void setControls(const PlanViewControls& controls);
	// Installed actions own overlapping selection-cycle keys and chord prefixes.
	void setReservedShortcuts(const QList<QKeySequence>& sequences);
	[[nodiscard]] const PlanViewControls& controls() const;
	// The 3D camera, drawn where it stands with its view opening ahead of it
	// while the 3D view is showing.
	void setCameraMarker(bool visible, const LevelMapVec3& position, double yawDegrees, double pitchDegrees, double fieldOfViewDegrees);
	[[nodiscard]] bool cameraMarkerVisible() const;
	[[nodiscard]] bool isDrawingBrush() const;
	// Remembered selection depth shared by plan and camera brush creation.
	[[nodiscard]] QPair<double,double> brushDepthRange(int hiddenAxis) const;
	// Transient camera construction overlay; it never enters the document,
	// selection, spatial index, undo history or geometry workers.
	void setCameraBrushDraft(const LevelMapVec3& mins = {}, const LevelMapVec3& maxs = {});
	[[nodiscard]] bool hasCameraBrushDraft() const { return m_hasCameraBrushDraft; }
	// Everything under a point of the view, the nearest the viewer first:
	// the stack a Radiant-style click steps down.
	[[nodiscard]] QVector<LevelMapSelectionRef> objectsAt(const QPointF& viewPoint) const;

	[[nodiscard]] bool isDragging() const;
	// Cancels a drag, resize, or rubber band in progress and restores the
	// preview.
	void cancelInteraction();
	void setTemporaryPan(bool active, const QPointF& anchor = {});
	[[nodiscard]] bool isTemporarilyPanning() const;

	// Which edges of the selection's box a resize handle moves, in the view's
	// own axes: horizontal and vertical as drawn. A corner moves two.
	enum ResizeEdge {
		ResizeMinHorizontal = 0x1,
		ResizeMaxHorizontal = 0x2,
		ResizeMinVertical = 0x4,
		ResizeMaxVertical = 0x8,
	};
	// Eight handles sit on the corners and edges of the selection's box when
	// it can be resized in this view: a Quake-family selection, or Doom things
	// alone, spanning both drawn axes, and big enough on screen to grab.
	[[nodiscard]] bool hasResizeHandles() const;
	// Where the handle for `edges` (ResizeEdge flags) is drawn, in widget
	// pixels; null when there are no handles.
	[[nodiscard]] QPointF resizeHandlePosition(int edges) const;
	[[nodiscard]] bool isResizing() const;

	// Clip mode: a drag across the view draws the clip line, on the grid while
	// snapping is on, for a plane square to the view, and the part a cut would
	// take away is hatched. Tab chooses what stays (one side, the other, or
	// both as two brushes), Enter asks for the cut through clipRequested(),
	// and Escape leaves the mode. Clicks draw rather than select until then.
	void setClipMode(bool enabled);
	[[nodiscard]] bool clipMode() const;
	void setClipKeep(LevelMapClipKeep keep);
	[[nodiscard]] LevelMapClipKeep clipKeep() const;
	[[nodiscard]] bool hasClipLine() const;
	// The line's ends in the drawing plane's own units, as a drag would set
	// them.
	void setClipLine(const QPointF& from, const QPointF& to);
	void clearClipLine();
	// The drawn line as the three points clipLevelMapSelection() takes: its
	// ends, and a step from the first toward the viewer, so the plane's front
	// is the side to the right of the line as it was drawn. False with no
	// line.
	bool clipPlanePoints(LevelMapVec3* a, LevelMapVec3* b, LevelMapVec3* c) const;

	// Draw Sector mode, for Doom maps: each click puts a corner down, on an
	// existing vertex near the pointer or else on the grid while snapping is
	// on, and a click on the first corner, or Enter, closes the shape and asks
	// for the sector through sectorDrawRequested(). Backspace or a right-click
	// takes the last corner back, and Escape drops the shape, then leaves the
	// mode. Clicks draw rather than select until then.
	void setDrawMode(bool enabled);
	[[nodiscard]] bool drawMode() const;
	// The corners put down so far, in map units.
	[[nodiscard]] QVector<QPointF> drawCorners() const;
	void clearDrawCorners();

	// Hides the selected entities, brushes, patches, and things from drawing,
	// picking, framing, and Tab until they are shown again; the map itself is
	// not changed. An entity takes its brushes and patches with it. Selecting
	// a hidden object, from the objects list say, shows it again. Returns how
	// many objects were hidden.
	int hideSelection();
	void showAllHidden();
	[[nodiscard]] int hiddenCount() const;
	// The map as this view draws it: the document without the hidden objects,
	// ids unchanged. The 3D view is built from it, so hiding hides there too.
	[[nodiscard]] const LevelMapDocument& displayDocument() const;
	[[nodiscard]] bool isHidden(LevelMapSelectionKind kind, int objectId) const;

	// The selection's width and height in the view plane, in map units; zero
	// when nothing selected has an extent there, such as a single entity.
	[[nodiscard]] QSizeF selectionExtent() const;

	[[nodiscard]] QString hoverSummary() const;
	[[nodiscard]] QStringList statusLines() const;
	[[nodiscard]] QString accessibleSummary() const;
	// How the view answers the mouse and keys under the editor profile, said
	// after the summary so a screen reader learns the controls with the map.
	void setControlsHelp(const QString& text);
	// Full, unelided corner tags: the view's state (leading) and map/selection
	// counts (trailing). Painting wraps/elides these to fit the current pane.
	[[nodiscard]] QStringList hudTags() const;

	[[nodiscard]] QSize sizeHint() const override;
	[[nodiscard]] QSize minimumSizeHint() const override;

Q_SIGNALS:
	void renderCompleted();
	// Primary selectionKind is a LevelMapSelectionKind cast to an int.
	void selectionChanged(int selectionKind, int objectId);
	// The whole selection set, in add order, primary last. Emitted together
	// with selectionChanged() whenever the set changes.
	void selectionSetChanged(const QVector<LevelMapSelectionRef>& selection);
	// A finished drag or an arrow-key nudge. The delta is already snapped to the
	// grid and is never all-zero. The receiver is expected to turn this into one
	// undo command over the current selection, for example with
	// moveLevelMapSelection().
	void moveRequested(double dx, double dy, double dz);
	// A finished drag on a resize handle: the box the selection should fit,
	// the axis the projection hides kept as it was, snapped to the grid while
	// snapping is on, and never the box it already has. The receiver is
	// expected to make it one undo command, for example with
	// resizeLevelMapSelection().
	void resizeRequested(const vibestudio::LevelMapVec3& mins, const vibestudio::LevelMapVec3& maxs);
	void clipModeChanged(bool enabled);
	void drawModeChanged(bool enabled);
	// A class or thing type from the palette dropped on the view, and where.
	void paletteDropped(const QString& payload, const QPointF& viewPoint);
	// A shape closed in Draw Sector mode: its corners, in order, in map units.
	void sectorDrawRequested(const QVector<QPointF>& corners);
	// Enter in clip mode with a line drawn; `keep` is a LevelMapClipKeep.
	void clipRequested(const vibestudio::LevelMapVec3& a, const vibestudio::LevelMapVec3& b, const vibestudio::LevelMapVec3& c, int keep);
	void hoverChanged(const QString& summary);
	void viewChanged();
	// A box dragged out over empty space where the controls draw brushes:
	// the new brush's bounds, snapped to the grid while snapping is on, the
	// hidden axis spanning the last selection there. The receiver makes it
	// one undo command, for example with addLevelMapBoxBrush().
	void brushDrawRequested(const vibestudio::LevelMapVec3& mins, const vibestudio::LevelMapVec3& maxs);
	// Radiant's middle button: turn the 3D camera toward a point of the
	// view's plane, or move it there (the hidden axis is the camera's own).
	void cameraAimRequested(const QPointF& planePoint);
	void cameraPlaceRequested(const QPointF& planePoint);
	// Radiant's arrow keys drive the 3D camera from here: a step forward
	// (negative back) in map units, or a turn in degrees.
	void cameraDriveRequested(double forward, double turnDegrees);

protected:
	bool event(QEvent* event) override;
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void dragEnterEvent(QDragEnterEvent* event) override;
	void dragMoveEvent(QDragMoveEvent* event) override;
	void dragLeaveEvent(QDragLeaveEvent* event) override;
	void dropEvent(QDropEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void keyReleaseEvent(QKeyEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	struct Palette;

	void rebuildGeometry();
	void invalidateSelectionGeometry();
	[[nodiscard]] bool selectionWorldArea(QRectF* area) const;
	[[nodiscard]] bool selectionTransformBounds(LevelMapVec3* mins, LevelMapVec3* maxs) const;
	// m_document is m_sourceDocument without the hidden objects, so every
	// drawing and picking path skips them without checking.
	void applyHiddenFilter();
	// After the hidden set changes: filter, re-solve what is drawn, and
	// measure the map again, so Zoom to Fit frames what can be seen.
	void refilterHidden();
	// Shows any hidden object `selection` names, directly or through its
	// entity. True when something was shown.
	bool revealSelected(const QVector<LevelMapSelectionRef>& selection);
	// Forgets hidden objects an edit took out of the map, so the hidden count
	// and Show All never stand for something that is not there.
	void forgetVanishedHidden();
	void updateWorldBounds();
	[[nodiscard]] QPointF worldToView(double x, double y) const;
	[[nodiscard]] QPointF viewToWorld(const QPointF& point) const;
	[[nodiscard]] QPointF projectPoint(const LevelMapVec3& point) const;
	// Centres `area` (world units in the current projection) with a margin,
	// widening a point-sized area to a few grid steps.
	void frameWorldArea(QRectF area);
	void paintLeakTrail(QPainter& painter, const Palette& palette) const;
	void paintTargetLinks(QPainter& painter, const Palette& palette) const;
	void paintTagLinks(QPainter& painter, const Palette& palette) const;
	[[nodiscard]] bool tagLinkShown(const LevelMapTagLink& link) const;
	[[nodiscard]] MapViewportHit hitTest(const QPointF& viewPoint) const;
	void paintGrid(QPainter& painter, const Palette& palette) const;
	void paintDoom(QPainter& painter, const Palette& palette) const;
	void paintQuake(QPainter& painter, const Palette& palette);
	void paintQuakeGeometry(QPainter& painter, const Palette& palette, int worldspawnId);
	void retirePlanRender();
	void retireOverlayRender();
	void prepareOverlays(QPainter& painter, const Palette& palette);
	[[nodiscard]] MapViewportOverlayRequest overlayRequest(const MapViewportOverlayKey& key) const;
	void paintOverlayImage(QPainter& painter, const QImage& image, const MapGridView& view) const;
	void paintEmptyState(QPainter& painter, const Palette& palette) const;
	void paintOverlay(QPainter& painter, const Palette& palette) const;
	// Corner readouts in the style of idStudio's viewports: view and grid in
	// the top-left, what the map holds in the top-right.
	void paintHud(QPainter& painter, const Palette& palette, const ViewportHudLayout& hud) const;
	void paintSelectionMarkers(QPainter& painter, const Palette& palette, const ViewportHudLayout& hud) const;
	void paintDragPreview(QPainter& painter, const Palette& palette) const;
	void paintRubberBand(QPainter& painter, const Palette& palette) const;
	void announceSelection();
	// Profile gestures: panning from any button, the camera driven from the
	// middle button, a drag over empty space that draws a brush or resizes
	// the selection, and Radiant's click that steps down what is stacked
	// under the pointer.
	enum class CameraDrag {
		None,
		Aim,
		Place,
	};
	[[nodiscard]] static bool modifiersHeld(Qt::KeyboardModifiers held, Qt::KeyboardModifiers wanted);
	void beginPan(const QPointF& viewPoint, Qt::MouseButton button);
	void settleSelectionForMenu(const QPointF& viewPoint);
	void driveCameraTo(const QPointF& viewPoint);
	[[nodiscard]] bool canDrawBrush() const;
	void armEmptyDrag(PlanEmptyDrag action, const QPointF& viewPoint);
	void beginBrushDraw(const QPointF& viewPoint, Qt::KeyboardModifiers modifiers);
	void updateBrushDraw(const QPointF& viewPoint, Qt::KeyboardModifiers modifiers);
	[[nodiscard]] QRectF brushPlaneBox() const;
	void brushHiddenRange(double* low, double* high) const;
	[[nodiscard]] QString brushSummary() const;
	void commitBrushDraw();
	void updateWorkZone();
	void cycleSelectionAt(const QPointF& viewPoint);
	void paintBrushDraw(QPainter& painter, const Palette& palette) const;
	void paintCameraMarker(QPainter& painter, const Palette& palette) const;
	void adoptDocumentSelection();
	void syncPrimaryFromSelection();
	void setSelectionSetInternal(const QVector<LevelMapSelectionRef>& selection);

	[[nodiscard]] bool selectionContains(LevelMapSelectionKind kind, int objectId) const;
	// The selection as (kind, id) keys, built once for paint paths that ask
	// about every link; selectionContains() searches, which suits one hit.
	[[nodiscard]] QSet<QPair<int, int>> selectedKeys() const;
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

	// The selection's box as levelMapObjectsBounds() finds it, whole and in
	// the drawing plane (x and y the drawn axes' minimums, right and bottom
	// their maximums). False when the selection cannot be resized here.
	bool resizeBox(QRectF* plane, LevelMapVec3* mins = nullptr, LevelMapVec3* maxs = nullptr) const;
	// The box the handles sit on, in the drawing plane: the resize in
	// progress, or the selection's box when it gets handles.
	bool handlesBox(QRectF* box) const;
	[[nodiscard]] QPointF handleViewPosition(const QRectF& box, int edges) const;
	// The handle under `viewPoint`, as ResizeEdge flags, or 0.
	[[nodiscard]] int resizeHandleAt(const QPointF& viewPoint) const;
	// The box the drag has made so far, in the drawing plane.
	[[nodiscard]] QRectF resizedPlaneBox() const;
	void beginResize(int edges, const QPointF& viewPoint);
	void updateResize(const QPointF& viewPoint);
	void commitResize();
	void paintResizeHandles(QPainter& painter, const Palette& palette) const;
	void paintResizePreview(QPainter& painter, const Palette& palette) const;
	[[nodiscard]] QString resizeSummary() const;
	[[nodiscard]] QString resizeHandleSummary(int edges) const;
	// Shows the resize cursor for the handle under the pointer, or puts the
	// ordinary one back; the pan hand is left alone.
	void updateResizeCursor(int edges);
	void paintClip(QPainter& painter, const Palette& palette) const;
	[[nodiscard]] QString clipSummary() const;
	void paintDraw(QPainter& painter, const Palette& palette) const;
	[[nodiscard]] QString drawSummary() const;
	// Where a corner clicked at `viewPoint` goes: on a vertex, or the first
	// corner, near the pointer, else on the grid, else on whole units.
	[[nodiscard]] QPointF drawPointAt(const QPointF& viewPoint) const;
	// Asks for the sector the corners make, when there are three or more.
	void finishDraw();
	// The Doom sector a view point lies in, seen from above; -1 when none.
	[[nodiscard]] int sectorAt(const QPointF& viewPoint) const;
	// hitTest(), or else the sector under the point: what the pointer rests on.
	[[nodiscard]] MapViewportHit hitOrSectorAt(const QPointF& viewPoint) const;
	// A point in the drawing plane under `viewPoint`, on the grid while
	// snapping is on.
	[[nodiscard]] QPointF snappedPlanePoint(const QPointF& viewPoint) const;

	LevelMapDocument m_document;
	bool m_hasDocument = false;
	quint64 m_sceneRevision = 0;
	bool m_activePane = false;
	QVector<MapBrushGeometry> m_brushGeometry;
	MapBrushGeometryCache m_brushGeometryCache;
	QVector<DoomSectorOutline> m_sectorOutlines;
	MapViewportSceneIndex m_sourceIndex;
	MapViewportSceneIndex m_sceneIndex;
	mutable MapPlanWires m_planWires;
	mutable MapPlanWireFrame m_planWireFrame;
	mutable bool m_planWiresComputed = false;
	// Selection is a separate layer: changing it never rebuilds the base image.
	mutable MapPlanWires m_selectionWires;
	mutable MapPlanWireFrame m_selectionWireFrame;
	mutable bool m_selectionWiresComputed = false;
	MapPlanRenderWorker* m_planRenderWorker = nullptr;
	bool m_measurePainting = false;
	mutable MapViewportPaintStatistics m_paintStatistics;
	mutable ViewportRingCache m_selectionRingCache;
	mutable MapGridFrame m_gridFrame;
	MapViewportOverlayWorker* m_overlayRenderWorker = nullptr;
	MapViewportOverlayKey m_overlayRequested;
	MapViewportOverlayResult m_overlayFrame;
	bool m_overlayRenderPending = false, m_overlayRenderFailed = false, m_asyncOverlays = false;
	quint64 m_selectionRevision = 0, m_planRequestedScene = 0, m_planRequestedSelection = 0;
	MapPlanWireFrame m_planRequestedView;
	bool m_planRenderPending = false, m_planRenderFailed = false;
	qsizetype m_planRenderWeight = 0;
	MapViewportProjection m_projection = MapViewportProjection::TopXY;
	LevelMapSelectionKind m_selectionKind = LevelMapSelectionKind::None;
	int m_selectedObjectId = -1;
	// Multi-selection, primary last. m_selectionKind / m_selectedObjectId always
	// mirror its back().
	QVector<LevelMapSelectionRef> m_selection;
	mutable bool m_selectionAreaComputed = false;
	mutable bool m_selectionAreaValid = false;
	mutable QRectF m_selectionArea;
	mutable bool m_selectionBoundsComputed = false;
	mutable bool m_selectionBoundsValid = false;
	mutable LevelMapVec3 m_selectionMins;
	mutable LevelMapVec3 m_selectionMaxs;

	PlanViewControls m_controls;
	QString m_controlsHelp;
	// Space's hand: left drags pan until Space again.
	bool m_handMode = false;
	bool m_panHoldActive = false;
	QPointF m_lastPointerPosition;
	Qt::MouseButton m_panButton = Qt::NoButton;
	bool m_rightArmed = false;
	QPointF m_rightPressView;
	bool m_zoomDragging = false;
	QPointF m_zoomAnchorView;
	QPointF m_zoomAnchorWorld;
	double m_zoomDragStart = 1.0;
	CameraDrag m_cameraDrag = CameraDrag::None;
	// A left press whose meaning waits for the pointer: Radiant's click or
	// drag, or a band-keys press that bands when it drags.
	bool m_clickArmed = false;
	// An armed click that selects on release; GtkRadiant's plain click does not.
	bool m_clickSelects = true;
	bool m_clickBands = false;
	Qt::KeyboardModifiers m_clickModifiers;
	bool m_brushArmed = false;
	bool m_brushDrawing = false;
	QPointF m_brushFromPlane;
	QPointF m_brushToPlane;
	Qt::KeyboardModifiers m_brushModifiers;
	int m_pendingResizeEdges = 0;
	// The last selection's box: along the hidden axis, where a new brush goes.
	bool m_hasWorkZone = false;
	bool m_hasCameraBrushDraft = false;
	std::array<double,6> m_cameraBrushDraft{};
	LevelMapVec3 m_workMins;
	LevelMapVec3 m_workMaxs;
	bool m_cameraMarkerVisible = false;
	LevelMapVec3 m_cameraPosition;
	double m_cameraYaw = 0.0;
	double m_cameraPitch = 0.0;
	double m_cameraFov = 90.0;
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
	QVector<LevelMapVec3> m_leakTrail;
	QVector<LevelMapTargetLink> m_targetLinks;
	QVector<LevelMapTagLink> m_tagLinks;
	LevelMapDocument m_sourceDocument;
	// (kind, id) of each hidden object.
	QSet<QPair<int, int>> m_hidden;
	QSet<QString> m_sceneHidden;
	bool m_showTargetLinks = true;
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
	// A press on a resize handle starts a resize at once: the handle is not
	// an object, so there is no click to tell apart.
	bool m_resizing = false;
	int m_resizeEdges = 0;
	QRectF m_resizeFromPlane;
	LevelMapVec3 m_resizeFromMins;
	LevelMapVec3 m_resizeFromMaxs;
	QPointF m_resizePressPlane;
	QPointF m_resizeCurrentPlane;
	// The handle the pointer rests on, so its cursor can be put back.
	int m_hoverHandle = 0;
	// A right press that cancelled a gesture also eats the context menu the
	// same click would open.
	bool m_swallowContextMenu = false;
	bool m_clipMode = false;
	LevelMapClipKeep m_clipKeep = LevelMapClipKeep::Back;
	bool m_clipDrawing = false;
	bool m_hasClipLine = false;
	// The line's ends in the drawing plane.
	QPointF m_clipFrom;
	QPointF m_clipTo;
	// A palette drag over the view, and where it would drop.
	bool m_dropActive = false;
	QPointF m_dropPoint;
	bool m_drawMode = false;
	QVector<QPointF> m_drawCorners;
	// Where the next corner would go, under the pointer.
	QPointF m_drawHover;
	bool m_hasDrawHover = false;
	// The document's doomTopologyRevision last seen: when it moves on, the
	// ids of hidden vertices, linedefs, and sectors no longer mean the same.
	int m_topologyRevision = 0;
	bool m_banding = false;
	// The sector a rubber band's press began in: a click there, without a
	// drag, picks the sector instead of boxing nothing in.
	int m_bandSector = -1;
	QPointF m_bandAnchorView;
	QPointF m_bandCurrentView;
	Qt::KeyboardModifiers m_bandModifiers = Qt::NoModifier;
};

QString mapViewportProjectionDisplayName(MapViewportProjection projection);

// What the Levels palette drags out and the viewport takes: "entity:<class>"
// or "thing:<DoomEd number>", as UTF-8.
inline constexpr char kMapPaletteMimeType[] = "application/x-vibestudio-map-palette";

} // namespace vibestudio
