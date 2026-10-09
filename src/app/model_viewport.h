#pragma once

// GPU-rendered model viewport.
//
// Orthographic and perspective cameras render through the active OpenGL or
// Vulkan backend (core/render_device.h): depth-tested triangles with skins,
// flat shading, selection hatching and edges, translucent texels peeled and
// composited in depth order, or antialiased wireframe edges. A worker builds
// each frame and the device reads back its image and the triangle under every
// pixel; QPainter presents the image beneath the grid, overlays and the
// accessible HUD. Picking reads the triangle ids of the presented frame. With
// no working backend the view says why and draws no model.
//
// idTech models are Z-up: X forward, Y left, Z up (see the Quake
// Specifications, chapter 5, https://www.gamers.org/dEngine/quake/spec/quake-spec34/qkspec_5.htm).
// The camera basis below is built around that axis convention explicitly so
// models are never silently drawn lying on their side.

#include "core/level_editor_controls.h"
#include "core/level_camera_keys.h"
#include "core/level_navigation.h"
#include "core/box_resize.h"
#include "core/box_draw.h"
#include "core/camera_surface_placement.h"
#include "core/model_mesh.h"
#include "core/model_mdl.h"
#include "core/model_pose.h"
#include "core/model_transform.h"
#include "core/model_trackball.h"
#include "app/model_vertex_overlay.h"

#include <QBrush>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QLineF>
#include <QPointF>
#include <QPolygonF>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>
#include <functional>
#include <memory>

class QTimer;
class QThread;

namespace vibestudio {

// One presented GPU frame: the colour image (premultiplied ARGB32) and the
// triangle drawn at each of its pixels (-1 for none), in physical pixels.
struct ModelRenderFrame {
	QImage image;
	QVector<int> source;
	double pixelRatio = 1.0;
	void clear()
	{
		image = QImage();
		source.clear();
	}
};

enum class ModelViewportRenderMode {
	Wireframe,
	FlatShaded,
	Textured,
};

// What a left click asks for: the object under the pointer instead of the
// selection, in or out of it, or one face of it (see trianglePicked()).
enum class ModelViewportPick {
	Replace,
	Toggle,
	Face,
	FaceToggle,
};

// What the pointer is over, reported through hoverChanged().
struct ModelViewportHit {
	bool valid = false;
	// Index into ModelMesh::surfaces.
	int surface = -1;
	// Index into the viewport's flattened triangle list, which concatenates the
	// surfaces in order and drops triangles with out-of-range vertex indices.
	int triangle = -1;
	QString surfaceName;
};

struct ModelViewportEdgeHit {
	bool valid = false;
	int surface = -1, a = -1, b = -1;
};

enum class ModelViewportSurfaceTool { None, Paint, Sample };

struct ModelViewportVertexHit {
	bool valid = false;
	int surface = -1, vertex = -1;
};

class ModelViewport final : public QWidget {
	Q_OBJECT

public:
	explicit ModelViewport(QWidget* parent = nullptr);
	~ModelViewport() override;

	// Shows `mesh`, framed to fit. With `keepView` the camera stays where it
	// was, for a mesh rebuilt after an edit to the thing it shows.
	void setMesh(const ModelMesh& mesh, bool keepView = false);
	void clearMesh();
	[[nodiscard]] bool hasMesh() const;
	[[nodiscard]] const ModelMesh& mesh() const;

	// The skin is used by the textured mode. Passing a null image drops back to
	// flat shading; the image is converted once, not per frame.
	void setSkin(const QImage& skin);
	// Overrides the shared fallback skin for individual surfaces.
	void setSurfaceSkins(const QHash<int, QImage>& skins);
	void clearSkin();
	[[nodiscard]] bool hasSkin() const;
	[[nodiscard]] QSize skinSize() const;

	void setRenderMode(ModelViewportRenderMode mode);
	[[nodiscard]] ModelViewportRenderMode renderMode() const;

	// Closed models cull backfaces; some idTech models (flags, torch flames,
	// sprites-as-geometry) are single-sided sheets that vanish when culled, so
	// this is a toggle rather than a constant.
	void setBackfaceCulling(bool enabled);
	[[nodiscard]] bool backfaceCulling() const;

	void setShowGrid(bool show);
	[[nodiscard]] bool showGrid() const;
	void setShowAxes(bool show);
	[[nodiscard]] bool showAxes() const;
	void setShowEdges(bool show);
	[[nodiscard]] bool showEdges() const;
	// Triangles drawn in the highlight colour and outlined, such as the
	// selected objects of a map preview; indexes as in ModelViewportHit.
	void setHighlightedTriangles(const QVector<int>& triangles);
	[[nodiscard]] int highlightedTriangleCount() const;
	// Opt-in map material gestures. The viewport reports hits; the owning
	// document service validates and commits one batch on release.
	void setSurfaceTool(ModelViewportSurfaceTool tool);
	[[nodiscard]] ModelViewportSurfaceTool surfaceTool() const { return m_surfaceTool; }
	bool beginSurfaceStroke(const QPointF& point);
	bool extendSurfaceStroke(const QPointF& point);
	void finishSurfaceStroke(bool commit);
	bool sampleSurfaceAt(const QPointF& point);
	[[nodiscard]] bool surfaceStrokeActive() const { return m_surfaceStrokeActive; }
	void setSurfaceStrokePreview(const QVector<int>& triangles, int surfaceCount);
	// Instant, profile-defined material actions are opt-in for the level camera.
	// This semantic entry point is also used by native presses and offscreen QA.
	// A true result dispatches a hit; the owner can still reject a locked target.
	void setMaterialGesturesEnabled(bool enabled);
	[[nodiscard]] bool materialGesturesEnabled() const { return m_materialGesturesEnabled; }
	bool materialGestureAt(const QPointF& point, Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
	bool beginMaterialStroke(const QPointF& point, Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
	bool extendMaterialStroke(const QPointF& point, Qt::KeyboardModifiers modifiers);
	void finishMaterialStroke(bool commit);
	bool materialStrokeActive() const { return m_materialStrokeButton != Qt::NoButton; }
	void setMaterialStrokePreview(const ModelMesh& mesh, const QHash<int, QImage>& skins, const QVector<int>& triangles, int count);
	void clearMaterialStrokePreview();
	void setHighlightedEdges(int surface, const QSet<QPair<int, int>>& edges);
	[[nodiscard]] int highlightedEdgeCount() const;
	void setEdgePicking(bool enabled);
	[[nodiscard]] ModelViewportEdgeHit edgeAt(const QPointF& point, double tolerance = 8.0);
	// Authoring is opt-in. The ordinary model browser and level preview keep
	// their existing picking and drag behaviour. Vertex picks use this surface.
	void setEditSelection(int surface, const QSet<int>& vertices);
	void setEditSurfaces(int active, const QSet<int>& surfaces);
	void setShowTags(bool show);
	void setShowCollision(bool show);
	void setEditCollision(const QString& name);
	void setCollisionPicking(bool enabled);
	[[nodiscard]] QString collisionAt(const QPointF& point, double tolerance = 8) const;
	[[nodiscard]] bool collisionPose(const QString& name, ModelCollisionBox* result) const;
	[[nodiscard]] QVector<QLineF> collisionScreenEdges(const QString& name) const;
	void setTagPicking(bool enabled);
	void setEditTag(const QString& name);
	[[nodiscard]] QString tagAt(const QPointF& point, double tolerance = 9.0);
	[[nodiscard]] QPointF tagScreenPosition(const QString& name);
	[[nodiscard]] bool tagPose(const QString& name, ModelTag* result) const;
	void setVertexPicking(bool enabled);
	void setXrayVertices(bool enabled);
	void setTransformGizmo(bool enabled, ModelTransformTool tool, double moveGrid = 0, double angleGrid = 0, double scaleGrid = 0);
	void setTransformPivot(ModelTransformPivot mode, ModelVec3 custom = {});
	void setTransformAxes(const ModelTransformBasis& basis, const QString& name = {}, bool available = true);
	[[nodiscard]] std::array<QPointF, 4> transformGizmoPoints();
	[[nodiscard]] std::array<QPolygonF, 3> rotationGizmoRings();
	// Screen-space free-rotation sphere. Axis rings take priority except at
	// its centre handle; unoccupied points inside this circle select handle 3.
	[[nodiscard]] QRectF trackballGizmoRect();
	[[nodiscard]] int transformGizmoAt(const QPointF& point);
	bool beginEditTransform(const QPointF& point);
	bool updateEditTransform(const QPointF& point);
	void finishEditTransform(bool commit);
	[[nodiscard]] ModelTransform editTransform() const;
	[[nodiscard]] bool editTransformValid() const;
	// Translation-only compatibility entry points use the same lifecycle.
	void setMoveGizmo(bool enabled, double grid = 0);
	[[nodiscard]] ModelViewportVertexHit vertexAt(const QPointF& point, double tolerance = 8.0);
	[[nodiscard]] QPointF vertexScreenPosition(int surface, int vertex);
	// X/Y/Z endpoints and view-plane centre, in widget coordinates. Unavailable
	// handles are nonfinite. These value APIs also support tests without input.
	[[nodiscard]] std::array<QPointF, 4> moveGizmoPoints();
	[[nodiscard]] int moveGizmoAt(const QPointF& point);
	bool beginEditMove(const QPointF& point);
	bool updateEditMove(const QPointF& point);
	void finishEditMove(bool commit);
	[[nodiscard]] bool editingMove() const;
	[[nodiscard]] ModelVec3 editMoveDelta() const;
	void setHighContrast(bool enabled);
	void setReducedMotion(bool enabled);
	[[nodiscard]] bool reducedMotion() const;

	void setFrame(int frame);
	[[nodiscard]] int frame() const;
	void stepFrame(int delta);
	[[nodiscard]] int frameCount() const;

	// An empty name selects the whole frame range. Returns false when the mesh
	// has no animation with that name, leaving the current range untouched.
	bool setAnimation(const QString& name);
	// Indexed selection preserves distinct imported clips with the same name.
	bool setAnimationIndex(int index);
	[[nodiscard]] int animationIndex() const;
	[[nodiscard]] QString animation() const;
	[[nodiscard]] QStringList animationNames() const;

	void play();
	void pause();
	void togglePlayback();
	[[nodiscard]] bool isPlaying() const;
	void setFramesPerSecond(double fps);
	[[nodiscard]] double framesPerSecond() const;
	// Session-only smooth playback. Pause, step and explicit frame selection
	// return to exact stored poses; transient blends never change mesh().
	void setAnimationInterpolation(bool enabled);
	[[nodiscard]] bool animationInterpolation() const;
	[[nodiscard]] ModelAnimationSample animationSample() const;
	// Seek within the selected clip using seconds at the current preview FPS.
	// Loops within that range; paused seeks always select an exact stored pose.
	bool seekAnimation(double seconds);
	// Selected native MDL frame and skin share the ordinary transport. Images
	// are prepared on a worker before adoption; no pixel work occurs on ticks.
	bool setMdlPlayback(const ModelMdlPlayback& playback, const QVector<QImage>& skins, QString* error = nullptr);
	void clearMdlPlayback();
	[[nodiscard]] bool mdlPlaybackActive() const { return m_nativeMdl; }
	[[nodiscard]] ModelMdlPlayback mdlPlayback() const { return m_mdlPlayback; }
	[[nodiscard]] QVector<QImage> mdlPlaybackSkins() const { return m_mdlPlaybackSkins; }
	[[nodiscard]] ModelMdlPlaybackSample mdlPlaybackSample() const { return m_mdlSample; }
	[[nodiscard]] QImage mdlPlaybackSkin() const;
	void setMdlSkinVisible(bool visible);

	void resetView();
	void frameModel();
	// Frames a model-space box, such as the selection, and orbits about its centre.
	void frameBounds(const ModelVec3& low, const ModelVec3& high);
	void setOrbit(double yawDegrees, double pitchDegrees);
	[[nodiscard]] double yaw() const;
	[[nodiscard]] double pitch() const;
	void zoomIn();
	void zoomOut();
	[[nodiscard]] double zoom() const;

	// How the mouse, wheel, and keys drive the view (see
	// core/level_editor_controls.h). With `perspective` set the view is a
	// first-person camera placed in the scene, as TrenchBroom's and Radiant's
	// are; otherwise it orbits the mesh orthographically, the Models page's
	// view. The defaults are the orthographic orbit view's.
	void setCameraControls(const CameraViewControls& controls, bool preserveView = false);
	[[nodiscard]] const CameraViewControls& cameraControls() const;
	[[nodiscard]] bool isPerspective() const;
	// The perspective camera in mesh units: where it stands, and where it
	// looks (yaw 0 along +X, counter-clockwise; pitch up positive), with its
	// vertical field of view.
	[[nodiscard]] ModelVec3 cameraPosition() const;
	[[nodiscard]] double cameraYaw() const;
	[[nodiscard]] double cameraPitch() const;
	[[nodiscard]] double fieldOfView() const;
	void setCameraView(const ModelVec3& position, double yawDegrees, double pitchDegrees);
	[[nodiscard]] CameraViewState navigationState() const;
	bool restoreNavigationState(const CameraViewState& state);
	// Turns the camera toward `point`; with `keepPitch` only its heading
	// changes (Radiant's middle click in the top view).
	void aimCameraAt(const ModelVec3& point, bool keepPitch);
	// Moves the camera along its view, to its right, and up the world's Z,
	// in mesh units, and turns it; what flying and driving do.
	void moveCamera(double forward, double right, double up, bool groundPlane = false);
	void turnCamera(double yawDegrees, double pitchDegrees);
	// Semantic camera actions shared by native events and offscreen tests.
	void stepCameraDrive(const CameraKeyMotion& motion);
	bool beginPointerDrive(const QPointF& viewPoint);
	void updatePointerDrive(const QPointF& viewPoint);
	void advancePointerDrive(double seconds);
	void endPointerDrive();
	[[nodiscard]] bool isPointerDriving() const;
	// Mouse look: the pointer turns the camera without a button held
	// (Radiant's free look), until a click of the look button or Escape.
	void setLooking(bool looking);
	void setTemporaryLooking(bool looking);
	[[nodiscard]] bool isTemporarilyLooking() const;
	[[nodiscard]] bool isLooking() const;
	// The step a dragged selection snaps to, in mesh units; 0 moves freely.
	void setMoveGrid(double units);
	[[nodiscard]] bool isMovingSelection() const;
	// Externally owned scene selection. This opt-in tool never edits mesh/source
	// state: the owner applies the requested bounds through its document service.
	void setSelectionResizeBox(const ResizeBox& box, const QHash<int,BoxResizePoint>& pointOrigins = {}, int axes = 7);
	void clearSelectionResizeBox();
	[[nodiscard]] std::array<QPointF,6> selectionResizeHandles();
	[[nodiscard]] std::array<QRectF,6> selectionResizeLabels();
	[[nodiscard]] int selectionResizeHandleAt(const QPointF& point);
	bool beginSelectionResize(const QPointF& point);
	bool updateSelectionResize(const QPointF& point);
	void finishSelectionResize(bool commit);
	[[nodiscard]] bool isResizingSelection() const { return m_selectionResizeHandle >= 0; }
	[[nodiscard]] bool selectionResizeValid() const { return m_selectionResizeValid; }
	[[nodiscard]] ResizeBox selectionResizeBox() const { return isResizingSelection() ? m_selectionResizePreview : m_selectionResizeBox; }

	// Opt-in level construction tool. Drafts contain only bounds; the owner
	// creates geometry through its shared, undoable document service.
	void setBrushDrawTool(bool enabled);
	[[nodiscard]] bool brushDrawTool() const { return m_brushDrawTool; }
	bool setBrushDrawPlane(int axis, double base, double depth,
		Qt::KeyboardModifiers square = Qt::NoModifier, Qt::KeyboardModifiers cube = Qt::NoModifier, int direction = 1);
	bool setBrushDrawPlaneFromSurface(const QPointF& point);
	[[nodiscard]] int brushDrawAxis() const { return m_brushDraw.axis; }
	[[nodiscard]] int brushDrawDirection() const { return m_brushDraw.direction; }
	[[nodiscard]] double brushDrawBase() const { return m_brushDraw.base; }
	[[nodiscard]] double brushDrawDepth() const { return m_brushDraw.depth; }
	bool beginBrushDraw(const QPointF& point, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
	bool updateBrushDraw(const QPointF& point, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
	bool adjustBrushDrawDepth(int steps);
	void finishBrushDraw(bool commit);
	[[nodiscard]] bool isDrawingBrush() const { return m_brushDrawing; }
	[[nodiscard]] bool brushDrawValid() const { return m_brushDrawValid; }
	[[nodiscard]] ResizeBox brushDrawBox() const { return m_brushDrawBox; }

	[[nodiscard]] QString hoverSummary() const;
	[[nodiscard]] QStringList statusLines() const;
	// One line for the readout under the viewport: frame, animation, playback,
	// and skin. Counts are left to the in-view overlay.
	[[nodiscard]] QString playbackSummary() const;
	[[nodiscard]] QString accessibleSummary() const;
	// Read-only hit query in widget coordinates, also used by automation/tests.
	// Returns no hit until the worker has presented the current camera/pose.
	// Wireframe preparation is asynchronous too; renderCompleted announces it.
	[[nodiscard]] ModelViewportHit hitAt(const QPointF& point);
	// Exact point and camera-facing normal on a current rendered triangle.
	// Editing drafts, disabled views and stale render buffers cannot be sampled.
	bool surfacePointAt(const QPointF& point, CameraSurfacePoint* surface, int* triangle = nullptr);
	[[nodiscard]] bool isRendering() const;
	// The renderer behind the last frame, such as "Vulkan 1.3.290 · <device>",
	// or empty before the first; and why the last frame could not be drawn.
	[[nodiscard]] QString renderDeviceSummary() const { return m_renderDeviceSummary; }
	[[nodiscard]] QString renderError() const { return m_rasterError; }
	// Draws again on the current backend, after the 3D renderer changed.
	void resetRendering();
	// How the view answers the mouse and keys, said after the summary.
	void setControlsHelp(const QString& text);

	// Modeller interaction helpers: the ray through a widget point, a model
	// point's widget position (false behind a perspective camera or with no
	// mesh), and the direction the camera looks into the scene.
	[[nodiscard]] ModelPickRay viewRay(const QPointF& point);
	bool projectToView(const ModelVec3& point, QPointF* screen);
	[[nodiscard]] ModelVec3 viewForward();
	// The camera's screen-right and screen-up directions in model space.
	void viewAxes(ModelVec3* right, ModelVec3* up);
	// Edit-surface vertex positions and occlusion from the latest completed
	// render; null until one completes. Shared and immutable.
	[[nodiscard]] std::shared_ptr<const ModelVertexProjection> editVertexProjection() const { return m_rasterVertices; }
	// Owner-drawn overlay (selection boxes, the 3D cursor, tool guides), painted
	// last in widget coordinates. An empty function removes it.
	void setOverlayPainter(std::function<void(QPainter&)> painter);
	// Shows what `source` shows (mesh, frame, shading, skins, selection and
	// overlays) through this viewport's own camera: the modeller's mirror
	// panes (model_viewport_mirror.cpp).
	void mirrorDisplayFrom(const ModelViewport& source);
	// A name shown first in the heads-up line, such as "Top" for a four-view
	// pane (model_viewport_mirror.cpp); empty shows none.
	void setViewLabel(const QString& label);
	[[nodiscard]] QString viewLabel() const { return m_viewLabel; }

	[[nodiscard]] QSize sizeHint() const override;
	[[nodiscard]] QSize minimumSizeHint() const override;

Q_SIGNALS:
	void surfaceToolChanged();
	void surfaceStrokeBegan();
	void surfaceTouched(int triangle);
	void surfaceStrokeEnded(bool commit);
	void surfaceSampleRequested(int triangle);
	void surfacePaintRequested(int triangle);
	void surfacePasteRequested(int triangle, CameraMaterialGesture action);
	void materialStrokeBegan();
	void materialStrokeTouched(int triangle, CameraMaterialGesture action);
	void materialStrokeEnded(bool commit);
	void surfaceActionMessage(const QString& message);
	void renderCompleted();
	void frameChanged(int frame);
	void animationChanged(const QString& name);
	void playbackChanged(bool playing);
	void mdlPlaybackChanged();
	void viewChanged();
	void hoverChanged(const QString& summary);
	// A left click that did not turn into an orbit, landing on a triangle;
	// indexes as in ModelViewportHit.
	void triangleClicked(int surface, int triangle);
	// Every left click that did not drag, with what its keys ask for
	// (`pick` is a ModelViewportPick); -1 for the triangle over empty space.
	void trianglePicked(int surface, int triangle, int pick);
	void edgePicked(int surface, int a, int b, int pick);
	void vertexPicked(int surface, int vertex, int pick);
	void tagPicked(const QString& name, int pick);
	void collisionPicked(const QString& name, int pick);
	void editTransformActiveChanged(bool active);
	void editTransformPreviewChanged();
	void editTransformRequested(const vibestudio::ModelTransform& transform);
	void editMoveActiveChanged(bool active);
	void editMovePreviewChanged(double dx, double dy, double dz);
	void editMoveRequested(double dx, double dy, double dz);
	// A right click that did not drag, where the controls leave the right
	// button's click to a menu.
	void contextMenuRequested(const QPoint& position);
	void lookingChanged(bool looking);
	// A drag of the highlighted triangles finished: how far they went, in
	// mesh units, snapped to the move grid. Never all zero.
	void moveRequested(double dx, double dy, double dz);
	void selectionResizeRequested(const vibestudio::ResizeBox& box);
	void selectionResizeActiveChanged(bool active);
	void selectionResizePreviewChanged();
	void brushDrawToolChanged();
	void brushDrawActiveChanged(bool active);
	void brushDrawPreviewChanged();
	void brushDrawRequested(const vibestudio::ResizeBox& box);

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void keyReleaseEvent(QKeyEvent* event) override;
	void focusOutEvent(QFocusEvent* event) override;
	bool event(QEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	void paintBrushDraw(QPainter& painter);
	[[nodiscard]] QString brushDrawSummary(bool describeControls = true) const;
	bool m_brushDrawTool = false, m_brushDrawing = false, m_brushDrawValid = false;
	BoxDrawDrag m_brushDraw;
	ResizeBox m_brushDrawBox;
	QPointF m_brushDrawPoint;
	Qt::KeyboardModifiers m_brushSquareModifiers, m_brushCubeModifiers, m_brushDrawModifiers;
	double m_brushWheelRemainder = 0;
public:
	// Render-worker types, defined in model_viewport_p.h and shared with
	// model_viewport_render.cpp; not part of the widget's interface.
	struct Palette;
	struct RasterWork;
	struct GeometryKey;
	struct GpuGeometry;
	struct FlagsKey;
	struct GpuFlags;
	struct TagOverlay {
		QString name;
		QPointF centre;
		// Three ordinary and three selected axis tips, projected with the mesh.
		std::array<QPointF, 6> tips;
	};
	struct CollisionOverlay {
		QString name;
		QVector<QLineF> edges;
	};
	// Camera basis in model space. `eye` points from the model centre towards
	// the camera, so a face is turned towards the viewer when its normal has a
	// positive dot product with it.
	struct Camera {
		ModelVec3 right;
		ModelVec3 up;
		ModelVec3 eye;
		ModelVec3 light;
		QPointF origin;
		double scale = 1.0;
		// Perspective only: where the camera stands, where it looks, and the
		// distance from the eye to the picture plane, in pixels.
		ModelVec3 position;
		ModelVec3 forward;
		double focal = 1.0;
	};
	// Flattened triangle list for the whole mesh, built once per mesh rather
	// than once per frame or per paint.
	struct MeshTriangle {
		int surface = 0;
		int a = 0;
		int b = 0;
		int c = 0;
	};

private:
	int materialStrokeTriangleAt(const QPointF& point) const;
	// What a drag of the pressed button does, settled once it moves.
	enum class DragAction {
		None,
		Orbit,
		Look,
		Pan,
		Drive,
		Move,
	};
	// One held key's share of a fly or drive step.
	using KeyMotion = CameraKeyMotion;

	void rebuildMeshTriangles();
	void invalidateProjection();
	void invalidateRaster(bool retire = false);
	void invalidateVertexOverlay();
	void ensureProjection();
	[[nodiscard]] Camera buildCamera() const;
	[[nodiscard]] QPointF projectPoint(const Camera& camera, const ModelVec3& point, double* depthOut) const;
	// Perspective helpers: a mesh point in view space (x right, y up, z along
	// the view), a view-space point on screen, and a segment clipped to the
	// near plane. projectSegment() works in both modes.
	void toView(const ModelVec3& point, double* x, double* y, double* z) const;
	[[nodiscard]] QPointF fromView(double x, double y, double z) const;
	bool projectSegment(const ModelVec3& a, const ModelVec3& b, QPointF* screenA, QPointF* screenB) const;
	[[nodiscard]] ModelVec3 viewDirection() const;
	// The world direction through a point of the widget.
	[[nodiscard]] ModelVec3 pickDirection(const QPointF& viewPoint) const;
	// What an orbit started at `viewPoint` turns about: the surface under the
	// pointer, or a point along the pointer's ray at the focus distance.
	[[nodiscard]] ModelVec3 orbitPivotAt(const QPointF& viewPoint) const;
	[[nodiscard]] DragAction dragActionFor(Qt::MouseButton button, Qt::KeyboardModifiers modifiers) const;
	void applyDrag(DragAction action, const QPointF& delta);
	void orbitPerspective(double yawDelta, double pitchDelta);
	void panPerspective(const QPointF& delta);
	void dolly(double notches, const QPointF& anchor);
	void pickAt(const QPointF& viewPoint, Qt::KeyboardModifiers modifiers);
	// Moving the selection: where the pointer's ray meets the plane the drag
	// runs in (level through the grabbed point, or upright facing the camera
	// with Alt), and the offset so far.
	bool movePlanePoint(const QPointF& viewPoint, ModelVec3* point) const;
	void updateMove(const QPointF& viewPoint);
	void cancelMove();
	void rebuildKeyMotions();
	[[nodiscard]] bool claimsKey(int key, Qt::KeyboardModifiers modifiers) const;
	[[nodiscard]] bool claimsLookToggle(int key, Qt::KeyboardModifiers modifiers) const;
	void flyStep();
	[[nodiscard]] double flySpeed() const;
	void frameModelPerspective();
	void recentreLookCursor(const QPointF& position);
	[[nodiscard]] QString accessibleSummaryWithoutHelp() const;
	[[nodiscard]] ModelViewportHit hitTest(const QPointF& viewPoint) const;
	// The presented frame's triangle at a point, exact to the point rather
	// than to the pixel's centre (see the definition).
	[[nodiscard]] int presentedTriangleAt(const QPointF& viewPoint) const;
	void applyOrbitDelta(double yawDelta, double pitchDelta);
	void applyPanDelta(const QPointF& delta);
	void applyZoomFactor(double factor, const QPointF& anchor);
	void announceView();
	void updatePlaybackTimer();
	void resetPlaybackClock();
	void applyAnimationSample(const ModelAnimationSample& sample);
	void applyMdlSample(const ModelMdlPlaybackSample& sample);
	[[nodiscard]] bool canAnimate() const;
	[[nodiscard]] QString mdlPlaybackSummary() const;
	[[nodiscard]] QString interpolationWarning() const;
	[[nodiscard]] QString effectiveRenderModeName() const;
	void advanceFrame();
	void paintEmptyState(QPainter& painter, const Palette& palette) const;
	void paintNoGeometryState(QPainter& painter, const Palette& palette) const;
	void paintGround(QPainter& painter, const Palette& palette) const;
	void paintAxes(QPainter& painter, const Palette& palette) const;
	void paintTriangles(QPainter& painter, const Palette& palette);
	[[nodiscard]] GeometryKey currentGeometryKey(bool textured, bool moving) const;
	[[nodiscard]] FlagsKey currentFlagsKey() const;
	// What the view says when its last frame failed: the renderer's reason
	// and where to choose another.
	[[nodiscard]] QString renderFailureText() const;
	[[nodiscard]] static std::shared_ptr<const GpuGeometry> buildGpuGeometry(const RasterWork& work);
	[[nodiscard]] static std::shared_ptr<const GpuFlags> buildGpuFlags(const RasterWork& work);
	static void renderGpu(RasterWork& work);
	static void renderVertexOverlay(RasterWork& work);
	// The hovered triangle's outline in widget coordinates, clipped to the
	// perspective camera's near plane; empty when it cannot be seen.
	[[nodiscard]] QPolygonF triangleOutline(int triangle) const;
	void paintOverlay(QPainter& painter, const Palette& palette) const;
	// Corner readouts in the style of idStudio's model view: render mode in the
	// top-left; file, frame, and geometry counts in the top-right.
	void paintHud(QPainter& painter, const Palette& palette) const;
	void paintEditOverlays(QPainter& painter);
	void paintSelectionResize(QPainter& painter);
	[[nodiscard]] BoxResizeRay selectionResizeRay(const QPointF& point) const;
	[[nodiscard]] QString selectionResizeSummary(bool describeControls = true) const;
	[[nodiscard]] QVector<TagOverlay> projectTagOverlays() const;
	[[nodiscard]] QVector<CollisionOverlay> projectCollisionOverlays() const;
	void paintCollisionOverlays(QPainter& painter) const;
	[[nodiscard]] bool editPivot(ModelVec3* pivot) const;
	[[nodiscard]] double editGizmoRadius(ModelVec3 pivot) const;
	[[nodiscard]] ModelVec3 editVertexPosition(int surface, int vertex) const;
	[[nodiscard]] ModelPickRay editRay(const QPointF& point) const;
	[[nodiscard]] ModelVec3 editGizmoAxis(int axis) const;
	[[nodiscard]] ModelVec3 editGizmoCoordinates(ModelVec3 vector) const;
	[[nodiscard]] ModelPickRay editGizmoRay(const QPointF& point) const;

	ModelMesh m_mesh;
	ResizeBox m_selectionResizeBox, m_selectionResizePreview;
	BoxResizeDrag m_selectionResizeDrag;
	QHash<int,BoxResizePoint> m_selectionResizeOrigins;
	bool m_selectionResizeEnabled = false, m_selectionResizeValid = false;
	int m_selectionResizeHandle = -1, m_selectionResizeAxes = 7;
	bool m_hasMesh = false;
	QImage m_skin;
	bool m_skinHasAlpha = false;
	QHash<int, QImage> m_surfaceSkins;
	QSet<int> m_surfaceSkinAlpha;

	QVector<MeshTriangle> m_meshTriangles;
	// Bumped whenever m_mesh is replaced, so cached GPU uploads never outlive it.
	quint64 m_meshRevision = 0;
	quint64 m_renderOwner = 0;
	std::shared_ptr<const GpuGeometry> m_gpuGeometry;
	std::shared_ptr<const GpuFlags> m_gpuFlags;
	QString m_rasterError;
	QString m_renderDeviceSummary;
	quint64 m_renderGeneration = 0;
	ModelRenderFrame m_raster;
	QImage m_vertexOverlay;
	std::shared_ptr<const ModelVertexProjection> m_rasterVertices;
	QVector<TagOverlay> m_rasterTags;
	QVector<CollisionOverlay> m_rasterCollision;
	bool m_showCollision = false;
	bool m_collisionPicking = false;
	QString m_editCollision;
	QSize m_rasterLogicalSize;
	bool m_rasterDirty = true;
	int m_rasterHover = -1;
	bool m_rasterFailed = false;
	quint64 m_rasterRevision = 0;
	quint64 m_baseRasterRevision = 0;
	quint64 m_rasterContentRevision = 0;
	QThread* m_rasterThread = nullptr;
	std::shared_ptr<RasterWork> m_rasterWork;
	std::shared_ptr<RasterWork> m_presentedRaster;
	std::shared_ptr<RasterWork> m_materialStrokeRaster;
	QHash<int, QImage> m_materialStrokeSkins;
	QVector<int> m_materialStrokeHighlights;
	Qt::MouseButton m_materialStrokeButton = Qt::NoButton;
	bool m_materialStrokePreviewChange = false;
	bool m_materialStrokePreviewChanged = false;

	Camera m_camera;
	ModelVec3 m_center;
	double m_radius = 64.0;
	double m_yaw = 30.0;
	double m_pitch = 20.0;
	double m_scale = 1.0;
	QPointF m_pan;
	bool m_projectionDirty = true;
	quint64 m_projectionRevision = 0;
	double m_cachedPixelRatio = 1.0;
	int m_visibleTriangles = 0;
	int m_culledTriangles = 0;

	ModelViewportRenderMode m_renderMode = ModelViewportRenderMode::FlatShaded;
	bool m_backfaceCulling = true;
	bool m_showGrid = true;
	bool m_showAxes = true;
	bool m_showEdges = false;
	bool m_highContrast = false;
	bool m_reducedMotion = false;

	int m_frame = 0;
	int m_frameTotal = 0;
	int m_rangeFirst = 0;
	int m_rangeCount = 0;
	QString m_animation;
	int m_animationIndex = -1;
	QTimer* m_timer = nullptr;
	double m_fps = 10;
	bool m_playing = false;
	bool m_interpolateAnimation = false;
	double m_frameBlend = 0;
	int m_blendFrame = 0;
	QElapsedTimer m_animationClock;
	double m_animationStart = 0;
	bool m_nativeMdl = false;
	bool m_mdlSkinVisible = true;
	ModelMdlPlayback m_mdlPlayback;
	ModelMdlPlaybackSample m_mdlSample;
	QVector<QImage> m_mdlPlaybackSkins;

	ModelViewportHit m_hover;
	bool m_orbiting = false;
	bool m_panning = false;
	QPointF m_dragAnchor;

	CameraViewControls m_controls;
	QString m_controlsHelp;
	QString m_viewLabel;
	bool m_perspective = false;
	bool m_perspectivePlaced = false;
	ModelVec3 m_eye;
	double m_lookYaw = 45.0;
	double m_lookPitch = -30.0;
	double m_fov = 90.0;
	// How far ahead the camera's attention is: what panning moves at the
	// pointer's pace, and what an orbit over empty space turns about.
	double m_focusDistance = 256.0;
	ModelVec3 m_pivot;
	Qt::MouseButton m_pressButton = Qt::NoButton;
	Qt::KeyboardModifiers m_pressModifiers;
	DragAction m_dragAction = DragAction::None;
	bool m_dragStarted = false;
	bool m_looking = false;
	bool m_lookHoldActive = false;
	bool m_lookBeforeHold = false;
	QPointF m_lookAnchor;
	bool m_warpPointer = true;
	QHash<int, KeyMotion> m_keyMotions;
	Qt::KeyboardModifiers m_motionModifiers = Qt::NoModifier;
	ModelVec3 m_moveStart;
	ModelVec3 m_moveOffset;
	bool m_moveVertical = false;
	double m_moveGrid = 0.0;
	QSet<int> m_heldKeys;
	QTimer* m_flyTimer = nullptr;
	QElapsedTimer m_flyClock;
	bool m_pointerDriving = false;
	QPointF m_pointerDrivePoint;
	QTimer* m_driveTimer = nullptr;
	QElapsedTimer m_driveClock;
	// Where a left press began, so a release close by counts as a click.
	QPointF m_pressPoint;
	// One flag per triangle in the flattened list.
	QVector<bool> m_highlighted;
	int m_highlightCount = 0;
	ModelViewportSurfaceTool m_surfaceTool = ModelViewportSurfaceTool::None;
	bool m_materialGesturesEnabled = false;
	Qt::MouseButtons m_materialGestureButtons;
	bool m_surfaceStrokeActive = false, m_surfaceToolPressed = false;
	QSet<int> m_surfaceStrokeTriangles;
	int m_surfaceStrokeCount = 0;
	int m_edgeSelectionSurface = -1;
	QSet<QPair<int, int>> m_selectedEdges;
	bool m_edgePicking = false;
	int m_editSurface = -1;
	QString m_editTag;
	bool m_showTags = false, m_tagPicking = false;
	QSet<int> m_editVertices;
	QSet<int> m_editSurfaces;
	struct EditPivotCache
	{
		QVector<ModelVec3> positions;
		QVector<ModelSurface> surfaces;
		QSet<int> wholeSurfaces;
		int frame = -1;
		QSet<int> vertices;
		ModelVec3 centre;
		bool valid = false;
	};
	mutable EditPivotCache m_editPivotCache;
	bool m_vertexPicking = false, m_xrayVertices = false, m_moveGizmo = false;
	double m_editGrid = 0;
	bool m_editMoveActive = false, m_editMoveTravelled = false;
	QPointF m_editMovePress;
	ModelMoveDrag m_editDrag;
	ModelTransform m_editTransform;
	ModelTransformTool m_transformTool = ModelTransformTool::Move;
	ModelTransformPivot m_transformPivot = ModelTransformPivot::SelectionCentre;
	ModelVec3 m_customTransformPivot;
	ModelTransformBasis m_transformBasis;
	QString m_transformAxesName;
	bool m_transformAxesAvailable = true;
	double m_editRotationGrid = 0, m_editScaleGrid = 0;
	int m_transformHandle = -1;
	bool m_editTransformValid = true, m_rotationLinear = false;
	ModelRotateDrag m_rotationDrag;
	ModelTrackballDrag m_trackballDrag;
	QPointF m_trackballCentre, m_trackballPoint;
	QPointF m_rotationTangent;
	double m_scaleReference = 1;
	std::function<void(QPainter&)> m_overlayPainter;
};

QString modelViewportRenderModeDisplayName(ModelViewportRenderMode mode);

} // namespace vibestudio
