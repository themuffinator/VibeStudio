#pragma once
#include "core/level_component_selection.h"

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

#include "core/level_brush.h"
#include "core/level_scene.h"
#include "core/level_editor_controls.h"
#include "core/level_navigation.h"
#include "core/level_map.h"
#include "core/level_surface.h"
#include "app/level_clip_state.h"
#include "app/level_brush_assemble_state.h"
#include "app/level_component_gizmo.h"
#include "app/level_linked_group_boxes.h"
#include "app/level_vertex_insertion.h"
#include "core/map_geometry.h"
#include "core/map_geometry_cache.h"
#include "app/map_viewport_scene_index.h"
#include "app/map_plan_wires.h"
#include "app/map_plan_render_worker.h"
#include "app/map_viewport_overlay_worker.h"
#include "app/viewport_ring_cache.h"
#include "app/map_grid.h"
#include "core/level_block_grid.h"

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
	void setSelectionSet(const QVector<LevelMapSelectionRef>& selection, bool revealTemporarilyHidden = true);
	[[nodiscard]] QVector<LevelMapSelectionRef> selectionSet() const;
	[[nodiscard]] LevelMapSelectionKind selectionKind() const;
	[[nodiscard]] int selectedObjectId() const;

	// The working grid in map units, 0.125 to 4096.
	void setGridSize(double units);
	[[nodiscard]] double gridSize() const;
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
	// Numbers on the grid lines along the bottom edge and the leading edge
	// (left, or right in right-to-left layouts), read left to right and kept
	// clear of the status tags, as NetRadiant Custom's Show Coordinates
	// prints them (see mapGridCoordinateLabels()). Off by default.
	void setShowGridCoordinates(bool show);
	[[nodiscard]] bool showGridCoordinates() const;
	// The labels as they are painted now, in widget pixels; empty while off.
	[[nodiscard]] MapGridCoordinateLayout gridCoordinateLabels() const;
	// The compiler block grid from worldspawn _blocksize (core/level_block_grid.h),
	// dashed and kept inside the region, as NetRadiant Custom's Show Blocks
	// draws it. Quake-family maps only; off by default.
	void setShowBlockGrid(bool show);
	[[nodiscard]] bool showBlockGrid() const;
	// The open map's block size, and whether block lines are drawn now: shown,
	// a Quake-family map, and an axis of this projection split.
	[[nodiscard]] LevelMapBlockSize blockGridSize() const;
	[[nodiscard]] bool drawsBlockGrid() const;
	// Arrows from each entity's target-style keys to the entities they name.
	// Links that touch the selection are drawn heavier either way.
	void setShowTargetLinks(bool show);
	[[nodiscard]] bool showTargetLinks() const;
	// Every linked group's box, not only the sets of the selected or open
	// groups, which are drawn either way.
	void setShowLinkedGroups(bool show);
	[[nodiscard]] bool showLinkedGroups() const;
	// Each linked group's box as this view solved it, which the 3D camera
	// draws too (see app/level_linked_group_boxes.h).
	[[nodiscard]] const QVector<LevelLinkedGroupBox>& linkedGroupBoxes() const { return m_linkedGroupBoxes; }
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
	// A compiler's vis portals, outlined over the map as TrenchBroom's
	// and Radiant's portal viewers draw them. Empty clears them.
	void setPortals(const QVector<QVector<LevelMapVec3>>& portals);
	[[nodiscard]] int portalCount() const { return static_cast<int>(m_portals.size()); }
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
	[[nodiscard]] LevelMapVec3 worldPositionAt(const QPointF& viewPoint, double hiddenAxisValue, bool snap = true) const;
	// Native local events and semantic callers share this anchor. Leaving the
	// pane resets commands to its centre; no global cursor query is needed.
	void rememberCommandPointer(const QPointF& point) { m_lastPointerPosition = point; m_hasCommandPointer = QRectF(rect()).contains(point); }
	[[nodiscard]] QPointF commandPosition() const {
		return m_hasCommandPointer && QRectF(rect()).contains(m_lastPointerPosition) ? m_lastPointerPosition : QPointF(width()*0.5, height()*0.5);
	}
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
	// The region box, outlined with everything outside it shaded, as
	// Radiant greys out what lies outside its region. Invalid clears it.
	void setRegionBox(const LevelMapVec3& mins = {}, const LevelMapVec3& maxs = {});
	[[nodiscard]] bool hasRegionBox() const { return m_hasRegion; }
	[[nodiscard]] bool hasCameraBrushDraft() const { return m_hasCameraBrushDraft; }
	// Everything under a point of the view, the nearest the viewer first:
	// the stack a Radiant-style click steps down.
	[[nodiscard]] QVector<LevelMapSelectionRef> objectsAt(const QPointF& viewPoint) const;
	void selectSceneObjects(const QVector<LevelMapSelectionRef>& hits, LevelScenePick mode);
	bool openGroupAt(const QPointF& viewPoint);
	[[nodiscard]] const LevelSceneSelectionContext& sceneSelectionContext() const { return m_sceneContext; }

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

	// The Assemble Brush tool (map_viewport_assemble.cpp): the points the plan
	// views and the camera share. A click adds a grid point at the last
	// point's depth, a drag over empty space a rectangle, a drag on a point
	// moves it, and Shift-drag extrudes a flat outline shown side-on. Enter
	// requests the brush; Backspace removes the last point; Escape cancels a
	// drag, then clears the points, then leaves. No document is changed here.
	void setBrushAssembleTool(bool enabled);
	[[nodiscard]] bool brushAssembleTool() const { return m_assembleTool; }
	bool setBrushAssembleState(const LevelBrushAssembleState& state);
	[[nodiscard]] const LevelBrushAssembleState& brushAssembleState() const { return m_assembleState; }
	void setBrushAssemblePreview(const LevelBrushAssemblePreview& preview);
	[[nodiscard]] const LevelBrushAssemblePreview& brushAssemblePreview() const { return m_assemblePreview; }
	[[nodiscard]] QPointF brushAssemblePointPosition(int index) const;
	bool beginBrushAssemble(const QPointF& position, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
	bool updateBrushAssemble(const QPointF& position);
	void finishBrushAssemble(bool commit);
	[[nodiscard]] bool isAssemblingBrush() const { return m_assembleGesture != 0; }
	void removeLastBrushAssemblePoint();
	void clearBrushAssemblePoints();
	void requestBrushAssemble();
	[[nodiscard]] QString brushAssembleSummary(bool full = true) const;

	// Shared world-space clip mode. Clicks place up to three points; an initial
	// drag places two. Existing points can move in this pane's visible axes.
	// Tab cycles kept sides, Enter requests the checked cut, Backspace removes
	// a point, Escape cancels a drag or exits. No document is changed here.
	void setClipMode(bool enabled);
	[[nodiscard]] bool clipMode() const;
	void setClipKeep(LevelMapClipKeep keep);
	[[nodiscard]] LevelMapClipKeep clipKeep() const;
	[[nodiscard]] bool hasClipLine() const;
	// The line's ends in the drawing plane's own units, as a drag would set
	// them.
	void setClipLine(const QPointF& from, const QPointF& to);
	void clearClipLine();
	// Two points extrude toward the originating viewer; a third point replaces
	// that extrusion. False for an incomplete or degenerate plane.
	bool clipPlanePoints(LevelMapVec3* a, LevelMapVec3* b, LevelMapVec3* c) const;
	[[nodiscard]] const LevelClipState& clipState() const { return m_clipState; }
	bool setClipState(const LevelClipState& state);
	void setClipPreview(const LevelClipPreview& preview);
	[[nodiscard]] const LevelClipPreview& clipPreview() const { return m_clipPreview; }
	[[nodiscard]] QPointF clipPointPosition(int index) const;
	[[nodiscard]] int clipPointAt(const QPointF& position) const;
	bool beginClipPoint(const QPointF& position);
	bool moveClipPoint(const QPointF& position);
	void finishClipPoint(bool accept);
	void removeLastClipPoint();
	void requestClip();
	void setClipPreferences(const LevelClipperPreferences& preferences) { m_clipPreferences = preferences; }
	bool beginQuickClipPoint(const QPointF& position, Qt::KeyboardModifiers modifiers);
	bool activateClipDoubleClick();

	// Draw Sector mode, for Doom maps: each click puts a corner down, on an
	// existing vertex near the pointer or else on the grid while snapping is
	// on, and a click on the first corner, or Enter, closes the shape and asks
	// for the sector through sectorDrawRequested(). Backspace or a right-click
	// takes the last corner back, and Escape drops the shape, then leaves the
	// mode. Clicks draw rather than select until then.
	void setDrawMode(bool enabled);
	[[nodiscard]] bool drawMode() const;
	// Shape drawing, for the Shapes tab's sector shapes on Doom maps: in the
	// Top view a drag anywhere, over sectors too, draws the box the shape fills
	// and asks for it through brushDrawRequested(). Escape leaves the mode.
	void setShapeDrawMode(bool enabled);
	[[nodiscard]] bool shapeDrawMode() const;
	// Shear mode, after TrenchBroom's shear tool: the handles in the middle of
	// the selection box's sides slide those sides along themselves, slanting
	// the selection about the opposite side, asked for through
	// shearRequested(). Corner handles do nothing. Escape leaves the mode.
	void setShearMode(bool enabled);
	[[nodiscard]] bool shearMode() const;
	// Make Sector mode, after Doom Builder's Make Sectors mode: in the Top view
	// a click asks for a sector made of the lines around the point, through
	// sectorMakeRequested(). Escape leaves the mode.
	void setMakeSectorMode(bool enabled);
	[[nodiscard]] bool makeSectorMode() const;
	// Vertex editing, after Radiant's vertex, edge and face modes and
	// TrenchBroom's vertex tool: handles on the selected brushes' vertices,
	// edge middles or face centres, and on the selected patches' control
	// points. A click picks the handles under the pointer, every one this view
	// stacks there, Shift or Ctrl (the profile's add and toggle keys) adds or
	// toggles, and a drag over empty space bands them; a click on an unselected
	// brush or patch selects it instead. A drag on a picked handle moves every
	// picked handle, the one under the pointer landing on the grid, and the
	// arrow keys nudge them a grid step (Shift: eight). The view never edits
	// the document: it asks through componentMoveRequested() on release.
	// Escape drops the picks, then leaves the mode.
	enum class ComponentMode { Off, Vertices, Edges, Faces };
	struct ComponentHandle {
		LevelMapSelectionRef object;
		int component = -1;
		LevelMapVec3 position;
		QString geometryKey;
	};
	void setComponentMode(ComponentMode mode);
	bool pickComponentAt(const QPointF& point, LevelComponentPick operation = LevelComponentPick::Replace, bool preservePicked = false);
	bool pickComponentBand(const QRectF& band, LevelComponentPick operation = LevelComponentPick::Replace);
	bool cycleComponentAt(const QPointF& point, int steps = 1);
	bool cycleComponentSelection(int steps = 1);
	// Returns true when the configured gesture is consumed, even on empty space.
	bool componentCycleWheel(const QPointF& point, int delta, Qt::KeyboardModifiers modifiers);
	void selectComponentHandles(LevelComponentPick operation);
	[[nodiscard]] const QVector<ComponentHandle>& componentHandles() const { return m_componentHandles; }
	[[nodiscard]] const QSet<int>& pickedComponentHandles() const { return m_pickedHandles; }
	[[nodiscard]] bool componentHandlesLimited() const { return m_componentHandlesLimited; }
	[[nodiscard]] ComponentMode componentMode() const { return m_componentMode; }
	[[nodiscard]] int componentHandleCount() const { return static_cast<int>(m_componentHandles.size()); }
	[[nodiscard]] int pickedComponentCount() const { return static_cast<int>(m_pickedHandles.size()); }
	// Where handle `index` is drawn, in widget pixels.
	[[nodiscard]] QPointF componentHandlePosition(int index) const;
	[[nodiscard]] QVector<LevelMapComponentSet> pickedComponents() const;
	// The picked handles as objects and positions, so panes can share them.
	[[nodiscard]] QVector<QPair<LevelMapSelectionRef, LevelMapVec3>> pickedComponentPositions() const;
	void setPickedComponentPositions(const QVector<QPair<LevelMapSelectionRef, LevelMapVec3>>& picks);
	// Exact solved IDs for commands and pane synchronisation; equal centres
	// need not identify equal edges/faces. Invalid sets leave picks untouched.
	bool setPickedComponentHandles(const QSet<int>& handles);
	bool setPickedComponents(const QVector<LevelMapComponentSet>& sets);
	// Preflight an additive whole-patch query without rebuilding or losing picks.
	[[nodiscard]] bool canAddComponentPatches(const QVector<int>& patches) const;
	void setTexturedSurfaceFaces(const QVector<LevelSurfaceFace>& faces);
	[[nodiscard]] const QVector<LevelSurfaceFace>& texturedSurfaceFaces() const { return m_texturedFaces; }
	bool queryComponentSelection(const LevelComponentSelectionRequest& request,
		QVector<LevelMapComponentSet>* output, QString* error = nullptr) const;
	void pickAllComponents();
	[[nodiscard]] bool isMovingComponents() const { return m_componentDragging || m_componentGizmo->active() || m_vertexInsertion->active(); }
	// A shell nudge's move of the picked handles by `planeDelta` in this view's
	// plane, as an arrow key makes it: split and extrusion apply, and the edit
	// arrives through componentMoveRequested() as one undo step. False, doing
	// nothing, without picked handles or while a component tool other than
	// Move is chosen; true, doing nothing, during a drag or band.
	bool nudgeComponents(const QPointF& planeDelta);
	// True while the view uses `key` itself ahead of shell commands: its
	// temporary pan key, a profile's arrow pan keys, and any key during a
	// drag, band or resize.
	[[nodiscard]] bool keepsKey(int key, Qt::KeyboardModifiers modifiers) const;
	[[nodiscard]] LevelVertexInsertion* vertexInsertion() const { return m_vertexInsertion; }
	[[nodiscard]] std::shared_ptr<const LevelVertexInsertionData> vertexInsertionData() const { return m_componentInsertionData; }
	bool beginVertexInsertion(const QPointF& point, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
	bool hoverVertexInsertion(const QPointF& point, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
	[[nodiscard]] LevelComponentGizmo* componentGizmo() const { return m_componentGizmo; }
	[[nodiscard]] LevelComponentGizmoFrame componentGizmoFrame() const;
	bool beginComponentTransform(const QPointF& point);
	bool updateComponentTransform(const QPointF& point) { return m_componentGizmo->updateDrag(point); }
	void finishComponentTransform(bool commit) { m_componentGizmo->finish(commit); }
	void setComponentSplit(bool enabled);
	[[nodiscard]] bool componentSplit() const { return m_componentSplit; }
	void setComponentExtrusion(int mode, const LevelMapVec3& normal = {}, const QVector<LevelMapVec3>& vertices = {});
	[[nodiscard]] int componentExtrusion() const { return m_componentExtrusion; }
	[[nodiscard]] double componentExtrusionDistance() const { return m_componentExtrusionDistance; }
	bool armComponentMove(const QPointF& point, Qt::KeyboardModifiers modifiers);
	[[nodiscard]] bool canSnapPickedVertex() const;
	bool requestVertexSnapAt(const QPointF& point);
	[[nodiscard]] bool componentMoveArmed() const { return m_componentNativeArmed; }
	bool beginComponentMove(const QPointF& point, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
	bool updateComponentMove(const QPointF& point) { return updateComponentMove(point,m_componentDragModifiers); }
	bool updateComponentMove(const QPointF& point, Qt::KeyboardModifiers modifiers);
	bool updateComponentModifiers(int key, Qt::KeyboardModifiers modifiers, bool pressed);
	void finishComponentMove(bool commit);
	[[nodiscard]] LevelMapVec3 componentMoveDelta() const;
	[[nodiscard]] bool componentPreviewPending() const { return m_componentPending; }
	void setComponentPreview(const LevelMapVec3& delta, const QVector<QPair<LevelMapVec3,LevelMapVec3>>& edges, const QString& error = {});
	// Region drawing, after Radiant's Region Set XY: a drag over the view,
	// anywhere, draws the box the region keeps across this view, asked for
	// through regionDrawRequested(), and the mode ends. Escape leaves it.
	void setRegionDrawMode(bool enabled);
	[[nodiscard]] bool regionDrawMode() const { return m_regionDrawMode; }
	[[nodiscard]] bool isShearing() const;
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
	[[nodiscard]] int temporaryHiddenCount() const;
	// Objects the display filters hide (core/level_view_filters.h), on top of
	// the ones hidden by hand; Show All leaves them hidden. An entity takes its
	// brushes and patches with it, as when it is hidden by hand.
	void setFilteredObjects(const QVector<LevelMapSelectionRef>& objects);
	[[nodiscard]] int filteredCount() const;
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
	void texturedSelectionClearRequested();
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
	void clipStateChanged();
	void quickClipRequested();
	void brushAssembleToolChanged(bool enabled);
	void brushAssembleStateChanged();
	void brushAssembleRequested();
	void drawModeChanged(bool enabled);
	void shapeDrawModeChanged(bool enabled);
	void shearModeChanged(bool enabled);
	void makeSectorModeChanged(bool enabled);
	// A point in map units, to make a sector of the lines around.
	void sectorMakeRequested(const QPointF& point);
	// `mode` is a ComponentMode cast to an int.
	void componentModeChanged(int mode);
	void componentPicksChanged(int picked);
	// Move the picked components by `delta`, in world units; expected to be
	// one undo command, for example with moveLevelMapComponents().
	void componentMoveRequested(const QVector<vibestudio::LevelMapComponentSet>& sets, const vibestudio::LevelMapVec3& delta);
	void componentMoveActiveChanged(bool active);
	void vertexSnapRequested(const vibestudio::LevelMapSelectionRef& object, int vertex);
	void componentMovePreviewChanged(const vibestudio::LevelMapVec3& delta);
	// An arrow-key nudge in Face mode with no picked handles but faces picked
	// by Select Textured; `delta` is one grid step (Shift: eight) in world units.
	void texturedFacesMoveRequested(const vibestudio::LevelMapVec3& delta);
	void regionDrawModeChanged(bool enabled);
	// The box drawn, in this view's plane units (its drawn axes).
	void regionDrawRequested(const QRectF& planeBox);
	// Slide every point along world axis `axis` (0 x, 1 y, 2 z) by `factor`
	// times its distance along world axis `along` from `anchor`; expected to
	// be one undo command, for example with shearLevelMapSelectionAbout().
	void shearRequested(int axis, int along, double factor, double anchor);
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
	void groupContextRequested(const QString& id);

protected:
	bool event(QEvent* event) override;
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
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
	// Linked groups (core/level_linked_groups.h): each one's box, found when
	// the scene is rebuilt; the groups in use, open or holding the selection
	// whole; and their boxes, names and arrows to the groups they are linked
	// with, as TrenchBroom draws them. The rules are shared with the 3D
	// camera in app/level_linked_group_boxes.h.
	void rebuildLinkedGroupBoxes();
	[[nodiscard]] QSet<QString> linkedGroupsInUse() const;
	void paintLinkedGroups(QPainter& painter, const Palette& palette) const;
	[[nodiscard]] bool tagLinkShown(const LevelMapTagLink& link) const;
	[[nodiscard]] MapViewportHit hitTest(const QPointF& viewPoint) const;
	void paintGrid(QPainter& painter, const Palette& palette) const;
	// The grid as the image path and the labels see it: navigation and grid
	// size only, then with the device, colours, Show > Grid and block grid.
	[[nodiscard]] MapGridView planGridView() const;
	[[nodiscard]] MapGridView gridView(const QTransform& deviceTransform, const Palette& palette) const;
	void paintGridCoordinates(QPainter& painter, const Palette& palette, const ViewportHudLayout& hud) const;
	[[nodiscard]] QString blockGridDescription() const;
	// "Blocks 1024" for the status tag while block lines are drawn.
	[[nodiscard]] QString blockGridTag() const;
	[[nodiscard]] QString gridCoordinatesDescription() const;
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
	void paintRegion(QPainter& painter, const Palette& palette) const;
	void paintPortals(QPainter& painter, const Palette& palette) const;
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
	void paintBrushAssemble(QPainter& painter, const Palette& palette) const;
	[[nodiscard]] QVector<int> brushAssemblePointsAt(const QPointF& position) const;
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
	QVector<LevelSurfaceFace> m_texturedFaces;
	QSet<QPair<int, int>> m_texturedFaceKeys;
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
	bool m_hasCommandPointer = false;
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
	bool m_hasRegion = false;
	std::array<double, 6> m_regionBox {};
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
	double m_gridSize = 64;
	bool m_snapToGrid = true;
	bool m_showGrid = true;
	bool m_showThings = true;
	bool m_showSectorFill = true;
	bool m_showVertices = false;
	bool m_showLabels = false;
	bool m_showGridCoordinates = false;
	bool m_showBlockGrid = false;
	// The source document's block size, read again when the scene changes.
	mutable quint64 m_blockSizeRevision = ~quint64(0);
	mutable LevelMapBlockSize m_blockSize;
	QVector<LevelMapVec3> m_leakTrail;
	QVector<QVector<LevelMapVec3>> m_portals;
	QVector<LevelMapTargetLink> m_targetLinks;
	QVector<LevelMapTagLink> m_tagLinks;
	QVector<LevelLinkedGroupBox> m_linkedGroupBoxes;
	LevelMapDocument m_sourceDocument;
	LevelSceneSelectionContext m_sceneContext;
	// (kind, id) of each hidden object.
	QSet<QPair<int, int>> m_hidden;
	QSet<QString> m_effectiveHidden;
	QSet<QString> m_sceneHidden;
	QSet<QPair<int, int>> m_filtered;
	bool m_showTargetLinks = true;
	bool m_showLinkedGroups = false;
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
	LevelClipperPreferences m_clipPreferences;
	LevelClipState m_clipState;
	LevelClipState m_clipBefore;
	LevelClipPreview m_clipPreview;
	int m_clipGrabbed = -1;
	bool m_clipAdding = false;
	bool m_clipChanging = false;
	bool m_clipDrawing = false;
	bool m_assembleTool = false;
	// 0 none, 1 a click or rectangle, 2 moving points, 3 an extrusion.
	int m_assembleGesture = 0;
	bool m_assembleDragged = false;
	bool m_assembleChanging = false;
	QPointF m_assemblePress;
	QPointF m_assemblePressPlane;
	LevelMapVec3 m_assembleDepth;
	QVector<int> m_assembleGrabbed;
	LevelBrushAssembleState m_assembleState;
	LevelBrushAssembleState m_assembleBefore;
	LevelBrushAssembleOutline m_assembleOutline;
	LevelBrushAssemblePreview m_assemblePreview;
	QString m_assembleMessage;
	// A palette drag over the view, and where it would drop.
	bool m_dropActive = false;
	QPointF m_dropPoint;
	bool m_drawMode = false;
	bool m_shapeDrawMode = false;
	bool m_shearMode = false;
	bool m_makeSectorMode = false;
	bool m_shearing = false;
	int m_shearEdge = 0;
	QRectF m_shearFromPlane;
	QPointF m_shearPressPlane;
	QPointF m_shearCurrentPlane;
	// How far the dragged side has slid, snapped while snapping is on.
	[[nodiscard]] double shearTravel() const;
	void commitShear();
	void paintShearPreview(QPainter& painter, const Palette& palette) const;
	[[nodiscard]] QString shearSummary() const;
	// Vertex editing (map_viewport_components.cpp).
	ComponentMode m_componentMode = ComponentMode::Off;
	LevelComponentGizmo* m_componentGizmo = nullptr;
	LevelVertexInsertion* m_vertexInsertion = nullptr;
	std::shared_ptr<LevelVertexInsertionData> m_componentInsertionData;
	bool m_componentHandlesLimited = false;
	QVector<ComponentHandle> m_componentHandles;
	QVector<LevelComponentSelectionTopology> m_componentSelectionTopology;
	QSet<int> m_pickedHandles;
	LevelComponentCycleCursor m_componentCycle;
	// Picks to find again after the next rebuild, by object and position:
	// where a requested move takes them, else where they were.
	QVector<QPair<LevelMapSelectionRef, LevelMapVec3>> m_componentRepick;
	QVector<QPair<LevelMapSelectionRef, LevelMapVec3>> m_componentRepickFallback;
	bool m_componentArmed = false;
	bool m_componentNativeArmed = false, m_componentWorldDrag = false;
	LevelComponentDrag m_componentAxisDrag;
	LevelMapVec3 m_componentAxisDelta;
	QPointF m_componentLastPoint, m_componentNativePress;
	Qt::KeyboardModifiers m_componentDragModifiers = Qt::NoModifier, m_componentNativeModifiers = Qt::NoModifier;
	bool m_componentDragging = false;
	bool m_componentSplit = false;
	int m_componentExtrusion = 0;
	double m_componentExtrusionDistance = 0;
	LevelMapVec3 m_componentExtrusionNormal;
	QVector<LevelMapVec3> m_componentExtrusionVertices;
	bool m_componentPending = false;
	bool m_componentReleasePending = false;
	QString m_componentError;
	QVector<QPair<LevelMapVec3,LevelMapVec3>> m_componentPreviewEdges;
	bool m_componentBanding = false;
	int m_componentGrabbed = -1;
	QPointF m_componentGrabPlane;
	QPointF m_componentDragPlane;
	QPointF m_componentPressView;
	QPointF m_componentBandView;
	Qt::KeyboardModifiers m_componentModifiers = Qt::NoModifier;
	bool m_regionDrawMode = false;
	void rebuildComponentHandles();
	[[nodiscard]] int componentHandleAt(const QPointF& viewPoint) const;
	[[nodiscard]] QVector<int> componentHandlesStackedAt(int handle) const;
	[[nodiscard]] QPointF componentDragDelta() const;
	bool componentMousePress(QMouseEvent* event);
	bool componentMouseMove(QMouseEvent* event);
	bool componentMouseRelease(QMouseEvent* event);
	bool componentKeyPress(QKeyEvent* event);
	void requestComponentMove(const QPointF& planeDelta);
	void paintComponents(QPainter& painter, const Palette& palette) const;
	[[nodiscard]] QString componentSummary() const;
	[[nodiscard]] QString componentModeName() const;
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
