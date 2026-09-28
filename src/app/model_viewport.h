#pragma once

// Software-rendered model viewport.
//
// VibeStudio deliberately carries no OpenGL dependency, so the model preview is
// a plain QPainter renderer. That is affordable here: idTech models are a few
// hundred to a few thousand triangles, which is well inside what a painter's
// algorithm can sort and fill at interactive rates.
//
// The renderer is orthographic. Orthographic projection keeps the maths simple,
// keeps every projected triangle an honest affine image of the model triangle
// (so the affine texture mapping used for skins is exact rather than an
// approximation), and matches the way modellers usually inspect a low-polygon
// asset.
//
// idTech models are Z-up: X forward, Y left, Z up (see the Quake
// Specifications, chapter 5, https://www.gamers.org/dEngine/quake/spec/quake-spec34/qkspec_5.htm).
// The camera basis below is built around that axis convention explicitly so
// models are never silently drawn lying on their side.

#include "core/model_mesh.h"

#include <QBrush>
#include <QImage>
#include <QPointF>
#include <QPolygonF>
#include <QString>
#include <QStringList>
#include <QTransform>
#include <QVector>
#include <QWidget>

class QTimer;

namespace vibestudio {

enum class ModelViewportRenderMode {
	Wireframe,
	FlatShaded,
	Textured,
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

class ModelViewport final : public QWidget {
	Q_OBJECT

public:
	explicit ModelViewport(QWidget* parent = nullptr);
	~ModelViewport() override;

	void setMesh(const ModelMesh& mesh);
	void clearMesh();
	[[nodiscard]] bool hasMesh() const;
	[[nodiscard]] const ModelMesh& mesh() const;

	// The skin is used by the textured mode. Passing a null image drops back to
	// flat shading; the image is converted once, not per frame.
	void setSkin(const QImage& skin);
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
	[[nodiscard]] QString animation() const;
	[[nodiscard]] QStringList animationNames() const;

	void play();
	void pause();
	void togglePlayback();
	[[nodiscard]] bool isPlaying() const;
	void setFramesPerSecond(int fps);
	[[nodiscard]] int framesPerSecond() const;

	void resetView();
	void frameModel();
	void setOrbit(double yawDegrees, double pitchDegrees);
	[[nodiscard]] double yaw() const;
	[[nodiscard]] double pitch() const;
	void zoomIn();
	void zoomOut();
	[[nodiscard]] double zoom() const;

	[[nodiscard]] QString hoverSummary() const;
	[[nodiscard]] QStringList statusLines() const;
	// One line for the readout under the viewport: frame, animation, playback,
	// and skin. Counts are left to the in-view overlay.
	[[nodiscard]] QString playbackSummary() const;
	[[nodiscard]] QString accessibleSummary() const;

	[[nodiscard]] QSize sizeHint() const override;
	[[nodiscard]] QSize minimumSizeHint() const override;

Q_SIGNALS:
	void frameChanged(int frame);
	void animationChanged(const QString& name);
	void playbackChanged(bool playing);
	void viewChanged();
	void hoverChanged(const QString& summary);

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
	};

	// Flattened triangle list for the whole mesh, built once per mesh rather
	// than once per frame or per paint.
	struct MeshTriangle {
		int surface = 0;
		int a = 0;
		int b = 0;
		int c = 0;
	};

	// Texture-space companion to MeshTriangle. The polygon is in skin pixels and
	// never changes with the camera, so its inverse edge matrix is solved once
	// and reused for every frame the triangle is drawn in.
	struct TextureTriangle {
		QPolygonF polygon;
		bool invertible = false;
		double inverse[4] = {0.0, 0.0, 0.0, 0.0};
	};

	struct ProjectedTriangle {
		QPolygonF screen;
		QTransform textureTransform;
		bool textureValid = false;
		bool visible = false;
		bool frontFacing = true;
		double depth = 0.0;
		int brushIndex = 0;
		int shadowIndex = 0;
		int source = -1;
	};

	void rebuildMeshTriangles();
	void rebuildTextureTriangles();
	void rebuildFillBrushes();
	void invalidateProjection();
	void ensureProjection();
	[[nodiscard]] Camera buildCamera() const;
	[[nodiscard]] QPointF projectPoint(const Camera& camera, const ModelVec3& point, double* depthOut) const;
	[[nodiscard]] ModelViewportHit hitTest(const QPointF& viewPoint) const;
	void clampFrameToRange();
	void applyOrbitDelta(double yawDelta, double pitchDelta);
	void applyPanDelta(const QPointF& delta);
	void applyZoomFactor(double factor, const QPointF& anchor);
	void announceView();
	void updatePlaybackTimer();
	[[nodiscard]] QString effectiveRenderModeName() const;
	void advanceFrame();
	void paintEmptyState(QPainter& painter, const Palette& palette) const;
	void paintNoGeometryState(QPainter& painter, const Palette& palette) const;
	void paintGround(QPainter& painter, const Palette& palette) const;
	void paintAxes(QPainter& painter, const Palette& palette) const;
	void paintTriangles(QPainter& painter, const Palette& palette) const;
	void paintOverlay(QPainter& painter, const Palette& palette) const;
	// Corner readouts in the style of idStudio's model view: render mode in the
	// top-left; file, frame, and geometry counts in the top-right.
	void paintHud(QPainter& painter, const Palette& palette) const;

	ModelMesh m_mesh;
	bool m_hasMesh = false;
	QImage m_skin;
	QBrush m_skinBrush;

	QVector<MeshTriangle> m_meshTriangles;
	QVector<TextureTriangle> m_textureTriangles;
	QVector<ProjectedTriangle> m_projected;
	QVector<int> m_order;
	QVector<QBrush> m_fillBrushes;
	QVector<QBrush> m_shadowBrushes;

	Camera m_camera;
	ModelVec3 m_center;
	double m_radius = 64.0;
	double m_yaw = 30.0;
	double m_pitch = 20.0;
	double m_scale = 1.0;
	QPointF m_pan;
	bool m_projectionDirty = true;
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
	QTimer* m_timer = nullptr;
	int m_fps = 10;
	bool m_playing = false;

	ModelViewportHit m_hover;
	bool m_orbiting = false;
	bool m_panning = false;
	QPointF m_dragAnchor;
};

QString modelViewportRenderModeDisplayName(ModelViewportRenderMode mode);

} // namespace vibestudio
