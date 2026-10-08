#include "app/model_viewport.h"
#include <QAccessible>
#include "app/viewport_hud.h"
#include "core/model_collision.h"

#include <QBrush>
#include <QCoreApplication>
#include <QCursor>
#include <QFont>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QTimer>
#include <QThread>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <utility>

namespace vibestudio {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMinScale = 0.002;
constexpr double kMaxScale = 512.0;
constexpr double kMinPitch = -89.0;
constexpr double kMaxPitch = 89.0;
constexpr double kFitFraction = 0.44;
constexpr double kOrbitDegreesPerPixel = 0.4;
constexpr double kKeyOrbitDegrees = 5.0;
constexpr double kPanLimitFactor = 4.0;
// Shading is quantised so the fill brushes can live in a small preallocated
// table; 24 steps is far below what the eye separates on a flat-shaded face,
// and it keeps the paint loop free of allocations.
constexpr int kShadeSteps = 24;
constexpr double kMinGridPixels = 10.0;
constexpr double kMaxGridPixels = 160.0;
constexpr int kMaxGridLines = 81;
// The perspective camera: nothing nearer than this is drawn, a drag travels
// this far before it counts, the pointer turns the view this much per pixel,
// and a held turn key turns it this fast.
constexpr double kNearPlane = 1.0;
constexpr double kDragThreshold = 4.0;
constexpr double kLookDegreesPerPixel = 0.22;
constexpr double kTurnDegreesPerSecond = 110.0;
constexpr double kMinFieldOfView = 15.0;
constexpr double kMaxFieldOfView = 150.0;
constexpr int kFlyIntervalMsecs = 16;

ModelVec3 makeVec(double x, double y, double z)
{
	ModelVec3 value;
	value.x = static_cast<float>(x);
	value.y = static_cast<float>(y);
	value.z = static_cast<float>(z);
	return value;
}

double dotVec(const ModelVec3& a, const ModelVec3& b)
{
	return static_cast<double>(a.x) * b.x + static_cast<double>(a.y) * b.y + static_cast<double>(a.z) * b.z;
}

ModelVec3 crossVec(const ModelVec3& a, const ModelVec3& b)
{
	return makeVec(static_cast<double>(a.y) * b.z - static_cast<double>(a.z) * b.y,
		static_cast<double>(a.z) * b.x - static_cast<double>(a.x) * b.z,
		static_cast<double>(a.x) * b.y - static_cast<double>(a.y) * b.x);
}

double lengthVec(const ModelVec3& v)
{
	return std::sqrt(dotVec(v, v));
}

ModelVec3 normalizeVec(const ModelVec3& v, const ModelVec3& fallback)
{
	const double length = lengthVec(v);
	if (!std::isfinite(length) || length <= 1e-9) {
		return fallback;
	}
	return makeVec(v.x / length, v.y / length, v.z / length);
}

bool vecIsFinite(const ModelVec3& v)
{
	return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

double clampScale(double scale)
{
	if (!std::isfinite(scale) || scale <= 0.0) {
		return 1.0;
	}
	return std::clamp(scale, kMinScale, kMaxScale);
}

double wrapDegrees(double degrees)
{
	if (!std::isfinite(degrees)) {
		return 0.0;
	}
	double value = std::fmod(degrees, 360.0);
	if (value < 0.0) {
		value += 360.0;
	}
	return value;
}

// Grid spacing is a power-of-two multiple of eight world units, which is what
// every idTech editor snaps to, chosen so the lines stay readable at any zoom.
double gridSpacingForScale(double scale, double radius)
{
	double spacing = 8.0;
	while (spacing > 1.0 && spacing * scale > kMaxGridPixels) {
		spacing *= 0.5;
	}
	int guard = 0;
	while (spacing * scale < kMinGridPixels && guard < 24) {
		spacing *= 2.0;
		++guard;
	}
	const double coarse = std::max(radius, 1.0) / 32.0;
	while (spacing < coarse && guard < 48) {
		spacing *= 2.0;
		++guard;
	}
	return spacing;
}

QColor surfaceBaseColor(int surfaceIndex, bool highContrast)
{
	if (highContrast) {
		// Maximally separated hues at full value; surfaces also carry their name
		// in the hover readout, so colour is never the only distinction.
		static const QColor kColors[] = {
			QColor(255, 255, 255),
			QColor(255, 255, 0),
			QColor(0, 255, 255),
			QColor(255, 0, 255),
			QColor(0, 255, 0),
			QColor(255, 160, 0),
		};
		const int count = static_cast<int>(sizeof(kColors) / sizeof(kColors[0]));
		return kColors[((surfaceIndex % count) + count) % count];
	}
	const int hue = ((surfaceIndex * 47 + 205) % 360 + 360) % 360;
	return QColor::fromHsv(hue, 92, 214);
}

QColor scaledColor(const QColor& base, double factor)
{
	const double clamped = std::clamp(factor, 0.0, 1.0);
	return QColor(static_cast<int>(std::lround(base.red() * clamped)),
		static_cast<int>(std::lround(base.green() * clamped)),
		static_cast<int>(std::lround(base.blue() * clamped)));
}

} // namespace

// Colour set for one paint pass. Every distinction the viewport draws is also
// carried by line weight, marker shape or text so nothing depends on colour
// alone.
struct ModelViewport::Palette {
	QColor background;
	QColor grid;
	QColor gridMajor;
	QColor axisX;
	QColor axisY;
	QColor axisZ;
	QColor wire;
	QColor edge;
	QColor hover;
	QColor text;
	QColor subtleText;
	QColor focus;
	QColor highlight;
};

ModelViewport::ModelViewport(QWidget* parent)
	: QWidget(parent)
{
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);
	setAutoFillBackground(false);
	setAttribute(Qt::WA_OpaquePaintEvent, true);
	setAccessibleName(tr("Model viewport"));
	setAccessibleDescription(accessibleSummary());

	m_timer = new QTimer(this);
	m_timer->setObjectName(QStringLiteral("modelPlaybackTimer"));
	m_timer->setTimerType(Qt::PreciseTimer);
	m_timer->setInterval(int(std::ceil(1000.0 / m_fps)));
	connect(m_timer, &QTimer::timeout, this, &ModelViewport::advanceFrame);

	m_flyTimer = new QTimer(this);
	m_flyTimer->setTimerType(Qt::PreciseTimer);
	m_flyTimer->setInterval(kFlyIntervalMsecs);
	connect(m_flyTimer, &QTimer::timeout, this, &ModelViewport::flyStep);

	rebuildFillBrushes();
}

struct ModelViewport::RasterWork {
	std::atomic_bool cancelled {false};
	// Value snapshots only. Workers never read the widget or GUI-owned state.
	ModelMesh mesh;
	QVector<MeshTriangle> meshTriangles;
	QVector<ProjectedTriangle> projected;
	QVector<int> order;
	QVector<QBrush> fillBrushes;
	Camera camera;
	ModelVec3 center, eye, moveOffset;
	bool perspective = false, backfaceCulling = true, textured = false, wireframe = false, highContrast = false;
	bool moving = false, editMoveActive = false, editTagEmpty = true;
	bool resizingSelection = false;
	ResizeBox resizeFrom, resizeTo;
	QHash<int,BoxResizePoint> resizeOrigins;
	int frame = 0, blendFrame = 0, editSurface = -1, edgeSelectionSurface = -1, hover = -1;
	double frameBlend = 0;
	ModelTransform editTransform;
	QSet<int> editVertices, editSurfaces, surfaceStrokeTriangles;
	QVector<bool> highlighted;
	QSet<QPair<int, int>> selectedEdges;
	QImage skin;
	bool skinHasAlpha = false;
	QHash<int, QImage> surfaceSkins;
	QSet<int> surfaceSkinAlpha;
	Palette palette;
	int visibleTriangles = 0, culledTriangles = 0;
	ModelRasterPickIndex pickIndex;
	bool projectionReady = false;
	QVector<TagOverlay> tags;
	QVector<CollisionOverlay> collision;
	QSize logicalSize;
	QVector<ModelRasterTriangle> triangles;
	ModelRasterStyle style;
	QSize size;
	ModelRasterFrame result;
	QImage vertexOverlay;
	std::shared_ptr<const ModelVertexProjection> vertices;
	QColor vertexAccent;
	bool vertexPicking = false, xrayVertices = false, reuseBase = false;
	quint64 revision = 0;
	quint64 baseRevision = 0;
	quint64 contentRevision = 0;
	quint64 projectionRevision = 0;
	bool success = false;

	ModelVec3 vertexPosition(int surface, int vertex) const
	{
		const auto& part = mesh.surfaces[surface];
		const auto& pose = part.frames[std::min(frame, int(part.frames.size()) - 1)];
		auto point = pose.positions[vertex];
		if (frameBlend > 0 && blendFrame < part.frames.size() && part.frames[blendFrame].positions.size() == pose.positions.size()) {
			point = interpolateModelPosition(point, part.frames[blendFrame].positions[vertex], frameBlend);
		}
		return editMoveActive && (editSurfaces.contains(surface) || (surface == editSurface && editVertices.contains(vertex))) ? transformModelPoint(point, editTransform) : point;
	}
	void toView(const ModelVec3& point, double* x, double* y, double* z) const
	{
		const auto relative = makeVec(double(point.x) - camera.position.x, double(point.y) - camera.position.y, double(point.z) - camera.position.z);
		*x = dotVec(relative, camera.right); *y = dotVec(relative, camera.up); *z = dotVec(relative, camera.forward);
	}
	QPointF fromView(double x, double y, double z) const
	{
		const double depth = std::max(z, kNearPlane);
		return {camera.origin.x() + camera.focal * x / depth, camera.origin.y() - camera.focal * y / depth};
	}
	QPointF projectPoint(const ModelVec3& point, double* depthOut) const
	{
		const auto relative = makeVec(double(point.x) - center.x, double(point.y) - center.y, double(point.z) - center.z);
		if (depthOut) { *depthOut = dotVec(relative, camera.eye); }
		return {camera.origin.x() + dotVec(relative, camera.right) * camera.scale, camera.origin.y() - dotVec(relative, camera.up) * camera.scale};
	}
	bool projectSegment(const ModelVec3& a, const ModelVec3& b, QPointF* screenA, QPointF* screenB) const
	{
		if (!perspective) { *screenA = projectPoint(a, nullptr); *screenB = projectPoint(b, nullptr); return true; }
		double ax, ay, az, bx, by, bz;
		toView(a, &ax, &ay, &az); toView(b, &bx, &by, &bz);
		if (az < kNearPlane && bz < kNearPlane) { return false; }
		if (az < kNearPlane) {
			const double t = (kNearPlane - az) / (bz - az);
			ax += t * (bx - ax); ay += t * (by - ay); az = kNearPlane;
		} else if (bz < kNearPlane) {
			const double t = (kNearPlane - bz) / (az - bz);
			bx += t * (ax - bx); by += t * (ay - by); bz = kNearPlane;
		}
		*screenA = fromView(ax, ay, az); *screenB = fromView(bx, by, bz); return true;
	}
};

ModelViewport::~ModelViewport()
{
	if (m_rasterWork) { m_rasterWork->cancelled.store(true); }
	if (m_rasterThread) { m_rasterThread->wait(); }
}

bool ModelViewport::isRendering() const
{
	return m_hasMesh && !m_meshTriangles.isEmpty() && (m_rasterThread || m_projectionDirty || m_rasterDirty);
}

void ModelViewport::invalidateRaster(bool retire)
{
	++m_rasterRevision;
	++m_baseRasterRevision;
	m_rasterDirty = true;
	m_rasterFailed = false;
	if (retire)
	{
		++m_rasterContentRevision;
		m_raster.image = {};
		m_vertexOverlay = {};
		m_rasterVertices.reset();
		m_editPivotCache = {};
		m_rasterTags.clear();
		m_rasterCollision.clear();
		m_presentedRaster.reset();
		if (m_rasterWork)
		{
			m_rasterWork->cancelled.store(true);
		}
	}
}

void ModelViewport::invalidateVertexOverlay()
{
	++m_rasterRevision;
	m_rasterDirty = true;
	m_rasterFailed = false;
}

void ModelViewport::setMesh(const ModelMesh& mesh, bool keepView)
{
	if (!m_materialStrokePreviewChange) { finishMaterialStroke(false); clearMaterialStrokePreview(); }
	endPointerDrive();
	finishBrushDraw(false);
	clearSelectionResizeBox();
	finishSurfaceStroke(false);
	finishEditTransform(false); m_editVertices.clear(); m_editSurfaces.clear(); m_editTag.clear(); m_editCollision.clear(); m_editSurface = -1;
	m_selectedEdges.clear(); m_edgeSelectionSurface = -1;
	invalidateRaster(true);
	const bool keepCamera = keepView && m_hasMesh;
	m_mesh = mesh;
	m_nativeMdl = false;
	m_mdlPlaybackSkins.clear();
	m_mdlSample = {};
	m_hasMesh = true;
	m_playing = false;
	m_animation.clear();
	m_animationIndex = -1;
	m_hover = ModelViewportHit();
	m_orbiting = false;
	m_panning = false;
	unsetCursor();

	// The frame count that can actually be drawn is the smallest thing every
	// surface agrees on; a truncated file can leave a surface short.
	int total = static_cast<int>(m_mesh.frames.size());
	for (const ModelSurface& surface : m_mesh.surfaces) {
		total = std::max(total, static_cast<int>(surface.frames.size()));
	}
	m_frameTotal = std::max(total, 0);
	m_frame = 0;
	m_blendFrame = 0;
	m_frameBlend = 0;
	m_animationClock.invalidate();
	m_rangeFirst = 0;
	m_rangeCount = m_frameTotal;
	m_blendFrame = m_frameTotal > 1 ? 1 : 0;

	const ModelVec3 mins = m_mesh.mins;
	const ModelVec3 maxs = m_mesh.maxs;
	if (keepCamera) {
		// The pivot, scale, and pan stay, so an edit does not move the view.
	} else if (vecIsFinite(mins) && vecIsFinite(maxs) && maxs.x >= mins.x && maxs.y >= mins.y && maxs.z >= mins.z) {
		m_center = makeVec((static_cast<double>(mins.x) + maxs.x) * 0.5,
			(static_cast<double>(mins.y) + maxs.y) * 0.5, (static_cast<double>(mins.z) + maxs.z) * 0.5);
	} else {
		m_center = makeVec(0.0, 0.0, 0.0);
	}
	if (!keepCamera) {
		double radius = static_cast<double>(m_mesh.boundingRadius());
		if (!std::isfinite(radius) || radius <= 0.0) {
			radius = 0.5
				* std::sqrt(std::pow(static_cast<double>(maxs.x) - mins.x, 2.0)
					+ std::pow(static_cast<double>(maxs.y) - mins.y, 2.0)
					+ std::pow(static_cast<double>(maxs.z) - mins.z, 2.0));
		}
		m_radius = (std::isfinite(radius) && radius > 1e-3) ? radius : 64.0;
	}

	rebuildMeshTriangles();
	rebuildFillBrushes();
	updatePlaybackTimer();
	if (keepCamera) {
		invalidateProjection();
	} else {
		m_perspectivePlaced = false;
		frameModel();
	}
	setAccessibleDescription(accessibleSummary());
	Q_EMIT frameChanged(m_frame);
	Q_EMIT animationChanged(m_animation);
	Q_EMIT playbackChanged(m_playing);
	update();
}

void ModelViewport::setHighlightedTriangles(const QVector<int>& triangles)
{
	QVector<bool> highlighted(m_highlighted.size(),false);
	int count = 0;
	for (const int triangle : triangles) {
		if (triangle < 0) {
			continue;
		}
		if (triangle >= highlighted.size()) {
			highlighted.resize(triangle + 1, false);
		}
		if (!highlighted.at(triangle)) {
			highlighted[triangle] = true;
			++count;
		}
	}
	if (highlighted == m_highlighted) { return; }
	finishBrushDraw(false);
	finishSelectionResize(false);
	m_highlighted = std::move(highlighted); m_highlightCount = count;
	invalidateRaster();
	update();
}

int ModelViewport::highlightedTriangleCount() const
{
	return m_highlightCount;
}

void ModelViewport::setHighlightedEdges(int surface, const QSet<QPair<int, int>> &edges)
{
	if (surface == m_edgeSelectionSurface && edges == m_selectedEdges)
	{
		return;
	}
	QSet<QPair<int, int>> valid;
	if (surface >= 0 && surface < m_mesh.surfaces.size())
	{
		const int count = m_mesh.surfaces.at(surface).vertexCount;
		const auto accepted = [count](auto edge) { return edge.first >= 0 && edge.first < edge.second && edge.second < count; };
		if (std::all_of(edges.cbegin(), edges.cend(), accepted))
		{
			valid = edges;
		}
		else
		{
			for (auto edge : edges)
			{
				if (accepted(edge))
				{
					valid.insert(edge);
				}
			}
		}
	}
	if (surface == m_edgeSelectionSurface && valid == m_selectedEdges)
	{
		return;
	}
	m_edgeSelectionSurface = surface;
	m_selectedEdges = std::move(valid);
	setAccessibleDescription(accessibleSummary());
	invalidateRaster();
	update();
}
int ModelViewport::highlightedEdgeCount() const { return m_selectedEdges.size(); }
void ModelViewport::setEdgePicking(bool enabled)
{
	if (m_edgePicking == enabled) { return; }
	m_edgePicking = enabled;
	setAccessibleDescription(accessibleSummary());
}

void ModelViewport::clearMesh()
{
	finishMaterialStroke(false); clearMaterialStrokePreview();
	endPointerDrive();
	setBrushDrawTool(false);
	clearSelectionResizeBox();
	finishSurfaceStroke(false);
	finishEditTransform(false); m_editVertices.clear(); m_editSurfaces.clear(); m_editTag.clear(); m_editCollision.clear(); m_editSurface = -1;
	m_editPivotCache = {};
	m_selectedEdges.clear(); m_edgeSelectionSurface = -1;
	invalidateRaster(true);
	m_mesh = ModelMesh();
	m_nativeMdl = false;
	m_mdlPlaybackSkins.clear();
	m_mdlSample = {};
	m_raster.clear();
	m_hasMesh = false;
	m_meshTriangles.clear();
	m_projected.clear();
	m_order.clear();
	m_frame = 0;
	m_blendFrame = 0;
	m_frameBlend = 0;
	m_animationClock.invalidate();
	m_frameTotal = 0;
	m_rangeFirst = 0;
	m_rangeCount = 0;
	m_animation.clear();
	m_animationIndex = -1;
	m_playing = false;
	m_hover = ModelViewportHit();
	m_orbiting = false;
	m_panning = false;
	m_center = makeVec(0.0, 0.0, 0.0);
	m_radius = 64.0;
	m_pan = QPointF();
	m_visibleTriangles = 0;
	m_culledTriangles = 0;
	unsetCursor();
	updatePlaybackTimer();
	invalidateProjection();
	rebuildFillBrushes();
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT playbackChanged(m_playing);
	Q_EMIT viewChanged();
}

bool ModelViewport::hasMesh() const
{
	return m_hasMesh;
}

const ModelMesh& ModelViewport::mesh() const
{
	return m_mesh;
}

void ModelViewport::setSkin(const QImage& skin)
{
	invalidateRaster(true);
	if (skin.isNull()) {
		clearSkin();
		return;
	}
	// Cache premultiplied pixels and alpha classification once per skin.
	m_skin = skin.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	m_skinHasAlpha = modelTextureHasAlpha(m_skin);
	m_surfaceSkins.clear();
	m_surfaceSkinAlpha.clear();
	invalidateProjection();
	setAccessibleDescription(accessibleSummary());
	update();
}

void ModelViewport::clearSkin()
{
	if (m_skin.isNull() && m_surfaceSkins.isEmpty()) {
		return;
	}
	m_skin = QImage();
	invalidateRaster(true);
	m_skinHasAlpha = false;
	m_surfaceSkins.clear();
	m_surfaceSkinAlpha.clear();
	invalidateProjection();
	setAccessibleDescription(accessibleSummary());
	update();
}

bool ModelViewport::hasSkin() const
{
	return (m_nativeMdl && m_mdlSkinVisible) || !m_skin.isNull() || !m_surfaceSkins.isEmpty();
}

QSize ModelViewport::skinSize() const
{
	return m_skin.size();
}

void ModelViewport::setRenderMode(ModelViewportRenderMode mode)
{
	if (m_renderMode == mode) {
		return;
	}
	m_renderMode = mode;
	invalidateProjection();
	setAccessibleDescription(accessibleSummary());
	update();
}

ModelViewportRenderMode ModelViewport::renderMode() const
{
	return m_renderMode;
}

void ModelViewport::setBackfaceCulling(bool enabled)
{
	if (m_backfaceCulling == enabled) {
		return;
	}
	m_backfaceCulling = enabled;
	invalidateProjection();
	setAccessibleDescription(accessibleSummary());
	update();
}

bool ModelViewport::backfaceCulling() const
{
	return m_backfaceCulling;
}

void ModelViewport::setShowGrid(bool show)
{
	if (m_showGrid == show) {
		return;
	}
	m_showGrid = show;
	update();
}

bool ModelViewport::showGrid() const
{
	return m_showGrid;
}

void ModelViewport::setShowAxes(bool show)
{
	if (m_showAxes == show) {
		return;
	}
	m_showAxes = show;
	update();
}

bool ModelViewport::showAxes() const
{
	return m_showAxes;
}

void ModelViewport::setShowEdges(bool show)
{
	if (m_showEdges == show) {
		return;
	}
	m_showEdges = show;
	invalidateRaster();
	update();
}

bool ModelViewport::showEdges() const
{
	return m_showEdges;
}

void ModelViewport::setHighContrast(bool enabled)
{
	if (m_highContrast == enabled) {
		return;
	}
	m_highContrast = enabled;
	invalidateRaster();
	rebuildFillBrushes();
	update();
}

void ModelViewport::setReducedMotion(bool enabled)
{
	if (m_reducedMotion == enabled) {
		return;
	}
	m_reducedMotion = enabled;
	// Reduced motion never auto-plays. Stepping frames by hand stays available,
	// because that is a deliberate user action rather than motion the widget
	// starts on its own.
	if (m_reducedMotion && m_playing) {
		pause();
	}
	updatePlaybackTimer();
	setAccessibleDescription(accessibleSummary());
	update();
	if (m_nativeMdl) {
		Q_EMIT mdlPlaybackChanged();
	}
}

bool ModelViewport::reducedMotion() const
{
	return m_reducedMotion;
}

void ModelViewport::resetView()
{
	endPointerDrive();
	m_yaw = 30.0;
	m_pitch = 20.0;
	m_pan = QPointF();
	m_orbiting = false;
	m_panning = false;
	unsetCursor();
	if (m_perspective) {
		// Looking down on the scene from its south-west corner.
		m_lookYaw = 45.0;
		m_lookPitch = -30.0;
	}
	frameModel();
}

void ModelViewport::frameModel()
{
	if (m_hasMesh && vecIsFinite(m_mesh.mins) && vecIsFinite(m_mesh.maxs)) {
		auto low = m_mesh.mins, high = m_mesh.maxs;
		if (m_showCollision) {
			for (const auto& box : m_mesh.collisionBoxes) {
				ModelCollisionBox pose;
				if (!sampleModelCollisionBox(box, m_frame, m_blendFrame, m_frameBlend, &pose)) { continue; }
				for (const auto p : modelCollisionCorners(pose)) {
					if (!vecIsFinite(p)) { continue; }
					low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
					high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
				}
			}
		}
		m_center = makeVec((double(low.x) + high.x) * .5, (double(low.y) + high.y) * .5, (double(low.z) + high.z) * .5);
		m_radius = std::max(.001, .5 * std::hypot(double(high.x) - low.x, double(high.y) - low.y, double(high.z) - low.z));
	}
	if (m_perspective) {
		frameModelPerspective();
		return;
	}
	// The widget may still be unsized when a mesh is loaded, so fall back to the
	// size hint rather than producing an absurd scale.
	const double viewWidth = width() > 64 ? static_cast<double>(width()) : 640.0;
	const double viewHeight = height() > 64 ? static_cast<double>(height()) : 480.0;
	const double radius = std::max(m_radius, 1e-3);
	m_scale = clampScale(kFitFraction * std::min(viewWidth, viewHeight) / radius);
	m_pan = QPointF();
	invalidateProjection();
	announceView();
}

void ModelViewport::frameBounds(const ModelVec3& low, const ModelVec3& high)
{
	if (!m_hasMesh || !vecIsFinite(low) || !vecIsFinite(high)) { return; }
	m_center = makeVec((double(low.x) + high.x) * .5, (double(low.y) + high.y) * .5, (double(low.z) + high.z) * .5);
	m_radius = std::max(.5, .5 * std::hypot(double(high.x) - low.x, double(high.y) - low.y, double(high.z) - low.z));
	if (m_perspective) {
		frameModelPerspective();
		return;
	}
	const double viewWidth = width() > 64 ? static_cast<double>(width()) : 640.0;
	const double viewHeight = height() > 64 ? static_cast<double>(height()) : 480.0;
	m_scale = clampScale(kFitFraction * std::min(viewWidth, viewHeight) / std::max(m_radius, 1e-3));
	m_pan = QPointF();
	invalidateProjection();
	announceView();
}

void ModelViewport::setOrbit(double yawDegrees, double pitchDegrees)
{
	if (m_perspective) {
		// The camera on a sphere about the mesh's centre, looking at it.
		const double yawRadians = yawDegrees * kPi / 180.0;
		const double pitchRadians = std::clamp(pitchDegrees, kMinPitch, kMaxPitch) * kPi / 180.0;
		const ModelVec3 out = makeVec(std::cos(pitchRadians) * std::cos(yawRadians), std::cos(pitchRadians) * std::sin(yawRadians), std::sin(pitchRadians));
		const double distance = std::max(m_focusDistance, kNearPlane * 8.0);
		setCameraView(makeVec(m_center.x + out.x * distance, m_center.y + out.y * distance, m_center.z + out.z * distance),
			wrapDegrees(yawDegrees + 180.0), -std::clamp(pitchDegrees, kMinPitch, kMaxPitch));
		return;
	}
	const double yaw = wrapDegrees(yawDegrees);
	const double pitch = std::isfinite(pitchDegrees) ? std::clamp(pitchDegrees, -90.0, 90.0) : 0.0;
	if (qFuzzyCompare(yaw + 1.0, m_yaw + 1.0) && qFuzzyCompare(pitch + 1.0, m_pitch + 1.0)) {
		return;
	}
	m_yaw = yaw;
	m_pitch = pitch;
	invalidateProjection();
	announceView();
}

double ModelViewport::yaw() const
{
	return m_yaw;
}

double ModelViewport::pitch() const
{
	return m_pitch;
}

void ModelViewport::zoomIn()
{
	if (m_perspective) {
		dolly(1.0, QPointF(width() * 0.5, height() * 0.5));
		return;
	}
	applyZoomFactor(1.25, QPointF(width() * 0.5, height() * 0.5));
}

void ModelViewport::zoomOut()
{
	if (m_perspective) {
		dolly(-1.0, QPointF(width() * 0.5, height() * 0.5));
		return;
	}
	applyZoomFactor(1.0 / 1.25, QPointF(width() * 0.5, height() * 0.5));
}

double ModelViewport::zoom() const
{
	return m_scale;
}

QString ModelViewport::hoverSummary() const
{
	if (!m_hasMesh) {
		return tr("No model loaded.");
	}
	if (!m_hover.valid) {
		return tr("Pointer is over empty space.");
	}
	if (m_hover.surfaceName.isEmpty()) {
		return tr("Triangle %1 on surface %2").arg(m_hover.triangle).arg(m_hover.surface);
	}
	return tr("Triangle %1 on surface %2 (%3)").arg(m_hover.triangle).arg(m_hover.surface).arg(m_hover.surfaceName);
}

QString ModelViewport::playbackSummary() const
{
	if (!m_hasMesh) {
		return tr("No model loaded.");
	}
	if (!m_mesh.geometryAvailable || m_meshTriangles.isEmpty()) {
		return tr("Geometry is not decoded for this format, so there is nothing to draw.");
	}
	if (m_nativeMdl) { return mdlPlaybackSummary(); }
	QStringList parts;
	const QString frameName = (m_frame >= 0 && m_frame < m_mesh.frames.size()) ? m_mesh.frames.at(m_frame).name : QString();
	parts << (frameName.isEmpty()
			? tr("Frame %1 of %2").arg(m_frame + 1).arg(m_frameTotal)
			: tr("Frame %1 of %2 (%3)").arg(m_frame + 1).arg(m_frameTotal).arg(frameName));
	parts << (m_animation.isEmpty() ? tr("All frames") : tr("Animation %1").arg(m_animation));
	parts << (m_interpolateAnimation ? tr("Smooth preview") : tr("Stored frames"));
	if (m_reducedMotion) {
		parts << tr("Reduced motion: step with Page Up and Page Down");
	} else if (m_rangeCount > 1) {
		parts << (m_playing ? tr("Playing at %1 fps").arg(m_fps) : tr("Paused"));
	}
	parts << (m_surfaceSkins.isEmpty() ? (m_skin.isNull() ? tr("No skin") : tr("Skin %1 x %2").arg(m_skin.width()).arg(m_skin.height()))
	                                 : tr("Material images: %1 surfaces").arg(m_surfaceSkins.size()));
	const auto warning = interpolationWarning();
	if (!warning.isEmpty()) { parts << warning; }
	return parts.join(QStringLiteral("  %1  ").arg(QChar(0x00b7)));
}

QString ModelViewport::effectiveRenderModeName() const
{
	// Textured mode falls back to flat shading without a skin; say so rather
	// than name a mode the viewport is not drawing.
	if (m_renderMode == ModelViewportRenderMode::Textured && !hasSkin()) {
		return tr("Flat shaded (no skin)");
	}
	return modelViewportRenderModeDisplayName(m_renderMode);
}

QStringList ModelViewport::statusLines() const
{
	QStringList lines;
	lines << tr("Mode: %1").arg(effectiveRenderModeName());
	if (!m_hasMesh) {
		lines << tr("Model: not loaded");
		return lines;
	}

	const QString formatName = modelMeshFormatDisplayName(m_mesh.format);
	lines << (m_mesh.formatId.isEmpty()
			? tr("Format: %1").arg(formatName)
			: tr("Format: %1 (%2 %3)").arg(formatName, m_mesh.formatId).arg(m_mesh.version));
	if (!m_mesh.geometryAvailable || m_meshTriangles.isEmpty()) {
		lines << tr("Geometry: not decoded for this format");
		return lines;
	}

	lines << tr("Surfaces %1, triangles %2 (%3 drawn, %4 culled)")
			 .arg(m_mesh.surfaces.size())
			 .arg(m_meshTriangles.size())
			 .arg(m_visibleTriangles)
			 .arg(m_culledTriangles);
	const QString frameName = (m_frame >= 0 && m_frame < m_mesh.frames.size()) ? m_mesh.frames.at(m_frame).name
										  : QString();
	lines << (frameName.isEmpty()
			? tr("Frame %1 of %2").arg(m_frame + 1).arg(m_frameTotal)
			: tr("Frame %1 of %2 (%3)").arg(m_frame + 1).arg(m_frameTotal).arg(frameName));
	if (!m_nativeMdl) { lines << (m_animation.isEmpty()
			? tr("Animation: all frames")
			: tr("Animation: %1 (frames %2-%3)")
				  .arg(m_animation)
				  .arg(m_rangeFirst + 1)
				  .arg(m_rangeFirst + m_rangeCount)); }
	if (m_nativeMdl) {
		lines << mdlPlaybackSummary();
	} else if (m_reducedMotion) {
		lines << tr("Playback: disabled by reduced motion; step frames with Page Up and Page Down");
	} else if (m_playing) {
		lines << tr("Playback: playing at %1 fps").arg(m_fps);
	} else {
		lines << tr("Playback: paused (%1 fps)").arg(m_fps);
	}
	lines << (!m_nativeMdl && m_interpolateAnimation ? tr("Smooth preview; pausing returns to the stored frame") : tr("Stored-frame preview"));
	const auto warning = interpolationWarning();
	if (!warning.isEmpty()) { lines << warning; }
	lines << (m_skin.isNull() ? tr("Skin: none loaded")
				  : tr("Skin: %1 x %2").arg(m_skin.width()).arg(m_skin.height()));
	if (!m_surfaceSkins.isEmpty()) { lines << tr("Material images: %1 surfaces").arg(m_surfaceSkins.size()); }
	if (m_perspective) {
		lines << tr("Camera: at %1 %2 %3, heading %4 degrees, pitch %5 degrees, %6 degree view")
				 .arg(m_eye.x, 0, 'f', 0)
				 .arg(m_eye.y, 0, 'f', 0)
				 .arg(m_eye.z, 0, 'f', 0)
				 .arg(m_lookYaw, 0, 'f', 0)
				 .arg(m_lookPitch, 0, 'f', 0)
				 .arg(m_fov, 0, 'f', 0);
	} else {
		lines << tr("View: yaw %1 degrees, pitch %2 degrees, %3 pixels per unit")
				 .arg(m_yaw, 0, 'f', 0)
				 .arg(m_pitch, 0, 'f', 0)
				 .arg(m_scale, 0, 'f', 2);
	}
	lines << (m_backfaceCulling ? tr("Backfaces: culled") : tr("Backfaces: drawn (single-sided model)"));
	if (isRendering()) { lines << tr("Rendering model…"); }
	if (m_rasterFailed) { lines << tr("Unable to allocate the model preview. Reduce the viewport size."); }
	return lines;
}

void ModelViewport::setControlsHelp(const QString& text)
{
	m_controlsHelp = text;
	setAccessibleDescription(accessibleSummary());
}

QString ModelViewport::accessibleSummary() const
{
	QString summary = accessibleSummaryWithoutHelp();
	if (m_lookHoldActive) { summary += QLatin1Char(' ') + tr("Temporary mouse look while holding %1.").arg(m_controls.lookHoldKey); }
	if (m_nativeMdl) { summary += QLatin1Char(' ') + mdlPlaybackSummary(); }
	else if (m_interpolateAnimation) { summary += QLatin1Char(' ') + tr("Smooth animation preview. Pause to edit the current stored pose."); }
	const auto interpolation = interpolationWarning();
	if (!interpolation.isEmpty()) { summary += QLatin1Char(' ') + interpolation; }
	if (m_materialStrokeRaster) {
		summary += QLatin1Char(' ') + (materialStrokeActive()
			? tr("Surface transfer stroke: %n surface(s) in preview. Release applies one undo step; Escape cancels.", nullptr, m_surfaceStrokeCount)
			: tr("Finishing surface transfer. Escape cancels before the edit is applied."));
	}
	if (m_surfaceTool != ModelViewportSurfaceTool::None) {
		summary += QLatin1Char(' ') + (m_surfaceTool == ModelViewportSurfaceTool::Paint
			? tr("Paint materials: drag the left button over map surfaces. Release applies one undo step. Escape cancels. Use the material target field for keyboard editing.")
			: tr("Sample materials: click a map surface to choose its material. Escape returns to navigation."));
		if (m_surfaceStrokeActive) { summary += QLatin1Char(' ') + tr("%n surface(s) in the pending stroke.", nullptr, m_surfaceStrokeCount); }
	}
	if (!m_editSurfaces.isEmpty()) { summary += QStringLiteral(" ") + tr("%1 whole surfaces selected. Transforms use one common pivot and include unused vertices.").arg(m_editSurfaces.size()); }
	if (m_vertexPicking) { summary += QStringLiteral(" ") + tr("Vertex selection: choose a visible point on the active surface or use the component table."); }
	if (m_vertexPicking && m_xrayVertices) { summary += QStringLiteral(" ") + tr("X-ray vertices enabled; hidden vertices can be selected."); }
	if (m_showTags) { summary += QStringLiteral(" ") + tr("Attachment tags are drawn through the model as named diamonds with labelled local axes."); }
	if (m_showCollision && !m_mesh.collisionBoxes.isEmpty()) {
		summary += QStringLiteral(" ") + tr("%1 collision boxes are drawn at the preview pose; the selected box has solid thicker edges. Selected collision: %2.")
			.arg(m_mesh.collisionBoxes.size()).arg(m_editCollision.isEmpty() ? tr("None") : m_editCollision);
	}
	if (m_tagPicking) { summary += QStringLiteral(" ") + tr("Tag selection: choose an attachment origin or use the component table. Move and rotate edit its pose; scaling is unavailable."); }
	if (!m_editTag.isEmpty()) { summary += QStringLiteral(" ") + tr("Selected attachment: %1.").arg(m_editTag); }
	if (m_collisionPicking) { summary += QStringLiteral(" ") + tr("Collision selection: choose a box edge or a component-table row. Move and rotate use the chosen transform axes; scale follows the box's local axes. Geometry provides numeric transforms and pivots."); }
	if (m_moveGizmo && (!m_editSurfaces.isEmpty() || !m_editVertices.isEmpty() || !m_editTag.isEmpty() || !m_editCollision.isEmpty())) {
		summary += QStringLiteral(" ") + tr("Transform axes: %1.").arg(m_transformAxesName.isEmpty() ? tr("World") : m_transformAxesName);
		if (!m_transformAxesAvailable) summary += QStringLiteral(" ") + tr("Selection axes are unavailable. Choose a usable face, world axes or custom axes in Geometry.");
		if (m_transformTool == ModelTransformTool::Move) { summary += QStringLiteral(" ") + tr("Move gizmo: X, Y and Z arrows constrain translation; the centre moves in the view plane. Escape cancels. Numeric transforms are available in Geometry."); }
		else if (m_transformTool == ModelTransformTool::Rotate) { summary += QStringLiteral(" ") + tr("Rotate gizmo: X, Y and Z rings turn around the chosen axes. Drag the Free centre or empty space inside the dashed circle for trackball rotation; beyond the circle, the drag follows its rim. Snap keeps the free rotation axis and rounds its angle. Edge-on rings use their projected tangent. Escape cancels. Numeric rotation and pivot controls are in Geometry."); }
		else if (!m_editCollision.isEmpty()) { summary += QStringLiteral(" ") + tr("Scale gizmo: Local X, Y and Z handles resize the collision box; the centre scales uniformly. Factors remain positive; use Geometry for explicit mirroring. Escape cancels."); }
		else { summary += QStringLiteral(" ") + tr("Scale gizmo: labelled boxes scale along the chosen axes; the centre scales uniformly. Factors remain positive; use Geometry for explicit mirroring. Escape cancels."); }
	}
	if (m_editMoveActive) {
		if (m_transformTool == ModelTransformTool::Rotate && m_transformHandle == 3) { summary += QStringLiteral(" ") + tr("Free rotation preview. The starting view, pivot and transform axes remain fixed."); }
		const auto value = m_transformTool == ModelTransformTool::Move ? m_editTransform.translation : m_transformTool == ModelTransformTool::Rotate ? m_editTransform.rotation : m_editTransform.scale;
		summary += QStringLiteral(" ") + tr("Transform preview: X %1, Y %2, Z %3.").arg(value.x).arg(value.y).arg(value.z);
		if (!m_editTransformValid) { summary += QStringLiteral(" ") + tr("This pointer position cannot produce a valid transform. Releasing cancels the gesture."); }
	}
	if (m_edgePicking) { summary += QStringLiteral(" ") + tr("Edge selection mode: choose an edge on a visible face or use the component table. %1 edges selected.").arg(m_selectedEdges.size()); }
	if (m_selectionResizeEnabled && !m_brushDrawTool) { summary += QStringLiteral(" ") + selectionResizeSummary(); }
	if (m_brushDrawTool) { summary += QStringLiteral(" ") + brushDrawSummary(); }
	if (isRendering()) { summary += QStringLiteral(" ") + tr("Rendering model…"); }
	if (m_pointerDriving) { summary += QStringLiteral(" ") + tr("Position steering; release or Escape stops movement."); }
	if (m_rasterFailed) { summary += QStringLiteral(" ") + tr("Unable to allocate the model preview. Reduce the viewport size."); }
	return m_controlsHelp.isEmpty() ? summary : summary + QStringLiteral(" ") + m_controlsHelp;
}

QString ModelViewport::accessibleSummaryWithoutHelp() const
{
	if (!m_hasMesh) {
		return tr("Model viewport: no model is loaded, so there is nothing to draw.");
	}
	const QString formatName = modelMeshFormatDisplayName(m_mesh.format);
	if (!m_mesh.geometryAvailable || m_meshTriangles.isEmpty()) {
		return tr("Model viewport: a %1 header is loaded, but its geometry is not decoded, so nothing is drawn.")
			.arg(formatName);
	}
	if (m_perspective) {
		return tr("Camera view of %1 surfaces and %2 triangles in %3 mode, standing at %4 %5 %6, heading %7 degrees and pitched %8 degrees%9.")
			.arg(m_mesh.surfaces.size())
			.arg(m_meshTriangles.size())
			.arg(effectiveRenderModeName())
			.arg(m_eye.x, 0, 'f', 0)
			.arg(m_eye.y, 0, 'f', 0)
			.arg(m_eye.z, 0, 'f', 0)
			.arg(m_lookYaw, 0, 'f', 0)
			.arg(m_lookPitch, 0, 'f', 0)
			.arg(m_looking ? tr(", in mouse look") : QString());
	}
	const QString animation = m_nativeMdl ? tr("native MDL frame %1").arg(m_mdlPlayback.nativeFrame)
		: m_animation.isEmpty() ? tr("all frames") : m_animation;
	const QString playback = m_reducedMotion ? tr("playback disabled by reduced motion")
						 : (m_playing ? tr("playing") : tr("paused"));
	return tr("Model viewport showing a %1 model with %2 surfaces and %3 triangles in %4 mode, frame %5 of %6, "
		  "animation %7, %8; orbited to yaw %9 and pitch %10 degrees.")
		.arg(formatName)
		.arg(m_mesh.surfaces.size())
		.arg(m_meshTriangles.size())
		.arg(effectiveRenderModeName())
		.arg(m_frame + 1)
		.arg(m_frameTotal)
		.arg(animation, playback)
		.arg(m_yaw, 0, 'f', 0)
		.arg(m_pitch, 0, 'f', 0);
}

QSize ModelViewport::sizeHint() const
{
	return QSize(520, 420);
}

QSize ModelViewport::minimumSizeHint() const
{
	return QSize(220, 180);
}

void ModelViewport::rebuildMeshTriangles()
{
	// Flattening happens here and only here: paintEvent must never rebuild the
	// triangle list, and the list does not depend on the frame or the camera.
	m_meshTriangles.clear();
	m_projected.clear();
	m_order.clear();
	m_visibleTriangles = 0;
	m_culledTriangles = 0;
	if (!m_hasMesh) {
		invalidateProjection();
		return;
	}

	int total = 0;
	for (const ModelSurface& surface : m_mesh.surfaces) {
		total += static_cast<int>(surface.triangles.size());
	}
	m_meshTriangles.reserve(total);
	for (int surfaceIndex = 0; surfaceIndex < m_mesh.surfaces.size(); ++surfaceIndex) {
		const ModelSurface& surface = m_mesh.surfaces.at(surfaceIndex);
		// A surface with no frame geometry cannot be drawn at all; a surface
		// whose indices run past its vertex list is malformed, and those
		// triangles are dropped rather than trusted.
		int vertexLimit = surface.vertexCount;
		for (const ModelFrameGeometry& geometry : surface.frames) {
			vertexLimit = std::max(vertexLimit, static_cast<int>(geometry.positions.size()));
		}
		for (const ModelTriangle& triangle : surface.triangles) {
			if (triangle.a < 0 || triangle.b < 0 || triangle.c < 0) {
				continue;
			}
			if (triangle.a >= vertexLimit || triangle.b >= vertexLimit || triangle.c >= vertexLimit) {
				continue;
			}
			MeshTriangle entry;
			entry.surface = surfaceIndex;
			entry.a = triangle.a;
			entry.b = triangle.b;
			entry.c = triangle.c;
			m_meshTriangles.append(entry);
		}
	}
	invalidateProjection();
}

void ModelViewport::setSurfaceSkins(const QHash<int, QImage>& skins)
{
	invalidateRaster(true);
	m_surfaceSkins.clear(); m_surfaceSkinAlpha.clear();
	for (auto it = skins.cbegin(); it != skins.cend(); ++it) {
		if (it.key() < 0 || it.key() >= m_mesh.surfaces.size() || it.value().isNull()) { continue; }
		const auto converted = it.value().convertToFormat(QImage::Format_ARGB32_Premultiplied);
		m_surfaceSkins.insert(it.key(), converted);
		if (modelTextureHasAlpha(converted)) { m_surfaceSkinAlpha.insert(it.key()); }
	}
	invalidateProjection(); setAccessibleDescription(accessibleSummary()); update();
}

void ModelViewport::rebuildFillBrushes()
{
	const int surfaceCount = std::max(1, static_cast<int>(m_mesh.surfaces.size()));
	m_fillBrushes.resize(surfaceCount * kShadeSteps);
	for (int surfaceIndex = 0; surfaceIndex < surfaceCount; ++surfaceIndex) {
		const QColor base = surfaceBaseColor(surfaceIndex, m_highContrast);
		for (int step = 0; step < kShadeSteps; ++step) {
			const double t = static_cast<double>(step) / static_cast<double>(kShadeSteps - 1);
			m_fillBrushes[surfaceIndex * kShadeSteps + step] = QBrush(scaledColor(base, 0.22 + 0.78 * t));
		}
	}

}

void ModelViewport::invalidateProjection()
{
	if (!m_materialStrokePreviewChange) { finishMaterialStroke(false); clearMaterialStrokePreview(); }
	finishBrushDraw(false);
	finishSelectionResize(false);
	finishSurfaceStroke(false);
	++m_projectionRevision;
	m_projectionDirty = true;
	invalidateRaster();
}

ModelViewport::Camera ModelViewport::buildCamera() const
{
	Camera camera;
	if (m_perspective) {
		// Quake's axes: yaw turns about +Z from +X, pitch lifts the view.
		camera.forward = viewDirection();
		camera.right = normalizeVec(crossVec(camera.forward, makeVec(0.0, 0.0, 1.0)), makeVec(0.0, -1.0, 0.0));
		camera.up = normalizeVec(crossVec(camera.right, camera.forward), makeVec(0.0, 0.0, 1.0));
		// `eye` keeps its meaning for lighting: toward the viewer.
		camera.eye = makeVec(-camera.forward.x, -camera.forward.y, -camera.forward.z);
		camera.light = normalizeVec(makeVec(camera.eye.x + 0.45 * camera.up.x - 0.30 * camera.right.x,
						    camera.eye.y + 0.45 * camera.up.y - 0.30 * camera.right.y,
						    camera.eye.z + 0.45 * camera.up.z - 0.30 * camera.right.z),
			camera.eye);
		camera.origin = QPointF(width() * 0.5, height() * 0.5);
		camera.position = m_eye;
		const double halfHeight = std::max(1.0, height() * 0.5);
		camera.focal = halfHeight / std::tan(std::clamp(m_fov, kMinFieldOfView, kMaxFieldOfView) * kPi / 360.0);
		camera.scale = camera.focal / std::max(m_focusDistance, 1.0);
		return camera;
	}
	const double yawRadians = m_yaw * kPi / 180.0;
	const double pitchRadians = m_pitch * kPi / 180.0;
	const double cosPitch = std::cos(pitchRadians);
	// idTech is Z-up, so the orbit azimuth turns in the X/Y plane and the
	// elevation lifts along +Z. Building the basis this way is what keeps a
	// model standing upright instead of lying on its side.
	camera.eye = normalizeVec(
		makeVec(cosPitch * std::cos(yawRadians), cosPitch * std::sin(yawRadians), std::sin(pitchRadians)),
		makeVec(1.0, 0.0, 0.0));
	// Keep azimuth meaningful at the orthographic poles, including exact XY views.
	camera.right = makeVec(-std::sin(yawRadians), std::cos(yawRadians), 0.0);
	camera.up = normalizeVec(crossVec(camera.eye, camera.right), makeVec(0.0, 0.0, 1.0));
	// A fixed headlight just above and left of the camera. It is attached to the
	// camera rather than the world so a model is never lit from behind and never
	// needs a lighting rig to be legible.
	camera.light = normalizeVec(makeVec(camera.eye.x + 0.45 * camera.up.x - 0.30 * camera.right.x,
					    camera.eye.y + 0.45 * camera.up.y - 0.30 * camera.right.y,
					    camera.eye.z + 0.45 * camera.up.z - 0.30 * camera.right.z),
		camera.eye);
	camera.origin = QPointF(width() * 0.5 + m_pan.x(), height() * 0.5 + m_pan.y());
	camera.scale = m_scale;
	return camera;
}

QPointF ModelViewport::projectPoint(const Camera& camera, const ModelVec3& point, double* depthOut) const
{
	if (m_perspective) {
		double x = 0.0;
		double y = 0.0;
		double z = 0.0;
		toView(point, &x, &y, &z);
		if (depthOut != nullptr) {
			*depthOut = -z;
		}
		if (z < kNearPlane) {
			// Behind the camera or too close: no honest place on screen.
			return QPointF(std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN());
		}
		return fromView(x, y, z);
	}
	const ModelVec3 relative = makeVec(static_cast<double>(point.x) - m_center.x,
		static_cast<double>(point.y) - m_center.y, static_cast<double>(point.z) - m_center.z);
	const double viewX = dotVec(relative, camera.right);
	const double viewY = dotVec(relative, camera.up);
	if (depthOut != nullptr) {
		*depthOut = dotVec(relative, camera.eye);
	}
	// Orthographic: no perspective divide, so a straight world line stays a
	// straight screen line and linear interpolation is exact. Screen Y
	// grows downwards, hence the flip.
	return QPointF(camera.origin.x() + viewX * camera.scale, camera.origin.y() - viewY * camera.scale);
}

void ModelViewport::ensureProjection()
{
	const double pixelRatio = devicePixelRatioF();
	if (!qFuzzyCompare(pixelRatio + 1.0, m_cachedPixelRatio + 1.0)) {
		m_cachedPixelRatio = pixelRatio;
		invalidateProjection();
	}
	if (!m_projectionDirty) { return; }
	m_projectionDirty = false;
	m_camera = buildCamera();
	update();
}

void ModelViewport::projectRaster(RasterWork& work)
{
	const auto& scene = work;
	work.visibleTriangles = 0;
	work.culledTriangles = 0;
	work.order.clear();
	if (work.projected.size() != scene.meshTriangles.size()) {
		work.projected.resize(scene.meshTriangles.size());
	}
	if (scene.meshTriangles.isEmpty()) {
		return;
	}

	const bool textured = scene.textured;
	const int surfaceCount = std::max(1, static_cast<int>(scene.mesh.surfaces.size()));
	int currentSurface = -1;
	const ModelFrameGeometry* geometry = nullptr;

	for (int index = 0; index < scene.meshTriangles.size(); ++index) {
		if (work.cancelled.load()) { return; }
		const MeshTriangle& source = scene.meshTriangles.at(index);
		ProjectedTriangle& target = work.projected[index];
		target.visible = false;
		target.textureValid = false;
		target.source = index;
		if (source.surface != currentSurface) {
			currentSurface = source.surface;
			geometry = nullptr;
			if (currentSurface >= 0 && currentSurface < scene.mesh.surfaces.size()) {
				const ModelSurface& surface = scene.mesh.surfaces.at(currentSurface);
				const int frameIndex = std::min(scene.frame, static_cast<int>(surface.frames.size()) - 1);
				if (frameIndex >= 0) {
					geometry = &surface.frames.at(frameIndex);
				}
			}
		}
		if (geometry == nullptr) {
			continue;
		}
		const QVector<ModelVec3>& positions = geometry->positions;
		if (source.a >= positions.size() || source.b >= positions.size() || source.c >= positions.size()) {
			continue;
		}
		ModelVec3 p0 = positions.at(source.a);
		ModelVec3 p1 = positions.at(source.b);
		ModelVec3 p2 = positions.at(source.c);
		if (scene.frameBlend > 0 || (scene.editMoveActive && (source.surface == scene.editSurface || scene.editSurfaces.contains(source.surface)))) {
			p0 = scene.vertexPosition(source.surface, source.a);
			p1 = scene.vertexPosition(source.surface, source.b);
			p2 = scene.vertexPosition(source.surface, source.c);
		}
		if (!vecIsFinite(p0) || !vecIsFinite(p1) || !vecIsFinite(p2)) {
			continue;
		}
		if (scene.moving && index < scene.highlighted.size() && scene.highlighted.at(index)) {
			for (ModelVec3* corner : {&p0, &p1, &p2}) {
				corner->x += scene.moveOffset.x;
				corner->y += scene.moveOffset.y;
				corner->z += scene.moveOffset.z;
			}
		}
		if (scene.resizingSelection && index < scene.highlighted.size() && scene.highlighted.at(index)) {
			const auto origin = scene.resizeOrigins.constFind(index);
			BoxResizePoint translation{};
			if (origin != scene.resizeOrigins.constEnd()) {
				const auto moved = resizeBoxPoint(scene.resizeFrom,scene.resizeTo,*origin);
				for (int axis = 0; axis < 3; ++axis) { translation[axis] = moved[axis]-(*origin)[axis]; }
			}
			for (auto* point : {&p0,&p1,&p2}) {
				BoxResizePoint target{point->x,point->y,point->z};
				if (origin == scene.resizeOrigins.constEnd()) { target = resizeBoxPoint(scene.resizeFrom,scene.resizeTo,target); }
				else { for (int axis = 0; axis < 3; ++axis) { target[axis] += translation[axis]; } }
				*point = makeVec(target[0],target[1],target[2]);
			}
		}

		ModelVec3 normal = crossVec(makeVec(static_cast<double>(p1.x) - p0.x, static_cast<double>(p1.y) - p0.y,
						 static_cast<double>(p1.z) - p0.z),
			makeVec(static_cast<double>(p2.x) - p0.x, static_cast<double>(p2.y) - p0.y,
				static_cast<double>(p2.z) - p0.z));
		const double normalLength = lengthVec(normal);
		if (!std::isfinite(normalLength) || normalLength <= 1e-12) {
			// A zero-area triangle has no facing and no shade; skipping it also
			// keeps degenerate data out of the rasterizer.
			continue;
		}
		normal = makeVec(normal.x / normalLength, normal.y / normalLength, normal.z / normalLength);
		// MDL, MD2 and MD3 all wind their triangles counter-clockwise when seen
		// from outside, but decoded files in the wild are not always consistent.
		// When the format carries vertex normals, they settle the argument.
		const bool transforming = scene.editMoveActive && scene.editTagEmpty && (source.surface == scene.editSurface || scene.editSurfaces.contains(source.surface));
		if (geometry->normals.size() == positions.size() && (!transforming || scene.editSurfaces.contains(source.surface) || scene.editVertices.size() == positions.size())) {
			const auto normalAt = [&](int vertex) {
				auto normal = geometry->normals.at(vertex);
				const auto& frames = scene.mesh.surfaces[currentSurface].frames;
				if (scene.frameBlend > 0 && scene.blendFrame < frames.size()
					&& frames[scene.blendFrame].positions.size() == positions.size()
					&& frames[scene.blendFrame].normals.size() == positions.size()) {
					// Opposing or invalid normals have no unique direction. Let the
					// geometric face normal determine facing for that transient pose.
					if (!interpolateModelNormal(normal, frames[scene.blendFrame].normals[vertex], scene.frameBlend, &normal)) { normal = {}; }
				}
				return transforming ? transformModelNormal(normal, scene.editTransform) : normal;
			};
			const auto na = normalAt(source.a), nb = normalAt(source.b), nc = normalAt(source.c);
			const ModelVec3 average = makeVec(static_cast<double>(na.x) + nb.x + nc.x,
				static_cast<double>(na.y) + nb.y + nc.y, static_cast<double>(na.z) + nb.z + nc.z);
			if (lengthVec(average) > 1e-6 && dotVec(normal, average) < 0.0) {
				normal = makeVec(-normal.x, -normal.y, -normal.z);
			}
		}

		// In perspective a face turns toward the camera when the camera
		// stands on the side its normal points to.
		const double facing = scene.perspective
			? dotVec(normal, makeVec(static_cast<double>(scene.eye.x) - p0.x, static_cast<double>(scene.eye.y) - p0.y, static_cast<double>(scene.eye.z) - p0.z))
			: dotVec(normal, scene.camera.eye);
		target.frontFacing = facing > 0.0;
		if (scene.backfaceCulling && !target.frontFacing) {
			++work.culledTriangles;
			continue;
		}


		const auto& coords = scene.mesh.surfaces.at(source.surface).texCoords;
		target.textureValid = textured && source.a < coords.size() && source.b < coords.size() && source.c < coords.size();
		QPointF uv[3];
		if (target.textureValid) {
			const int indices[] = {source.a, source.b, source.c};
			for (int corner = 0; corner < 3; ++corner) {
				const auto& value = coords.at(indices[corner]);
				if (!std::isfinite(value.u) || !std::isfinite(value.v)) { target.textureValid = false; break; }
				uv[corner] = QPointF(value.u, value.v);
			}
		}
		if (!target.textureValid) { uv[0] = uv[1] = uv[2] = QPointF(); }
		target.cornerCount = 0;
		const ModelVec3* corners[] = {&p0, &p1, &p2};
		if (scene.perspective) {
			double x[3], y[3], z[3];
			for (int corner = 0; corner < 3; ++corner) { scene.toView(*corners[corner], &x[corner], &y[corner], &z[corner]); }
			const auto append = [&](double px, double py, double pz, const QPointF& texcoord, int edgeMask) {
				const auto screen = scene.fromView(px, py, pz);
				target.corners[target.cornerCount] = {screen, 1.0 / pz, 1.0 / pz, texcoord};
				target.cornerEdges[target.cornerCount] = edgeMask;
				target.screen[target.cornerCount++] = screen;
			};
			for (int corner = 0; corner < 3; ++corner) {
				const int next = (corner + 1) % 3;
				const bool here = z[corner] >= kNearPlane, there = z[next] >= kNearPlane;
				if (here) { append(x[corner], y[corner], z[corner], uv[corner], 7 ^ (1 << corner)); }
				if (here != there) {
					const double t = (kNearPlane - z[corner]) / (z[next] - z[corner]);
					append(x[corner] + t * (x[next]-x[corner]), y[corner] + t * (y[next]-y[corner]),
						kNearPlane, uv[corner] + t * (uv[next]-uv[corner]), (7 ^ (1 << corner)) & (7 ^ (1 << next)));
				}
			}
		} else {
			for (int corner = 0; corner < 3; ++corner) {
				double depth = 0;
				const auto screen = scene.projectPoint(*corners[corner], &depth);
				target.corners[corner] = {screen, depth, 1.0, uv[corner]};
				target.cornerEdges[corner] = 7 ^ (1 << corner);
				target.screen[target.cornerCount++] = screen;
			}
		}
		if (target.cornerCount < 3 || std::any_of(target.screen.cbegin(), target.screen.cbegin() + target.cornerCount, [](const QPointF& point) {
			return !std::isfinite(point.x()) || !std::isfinite(point.y());
		})) { continue; }

		// Two-sided sheets are lit by the absolute value so the back of a flag
		// is not a black hole.
		double lambert = dotVec(normal, scene.camera.light);
		if (!target.frontFacing) {
			lambert = -lambert;
		}
		const double shade = std::clamp(lambert, 0.0, 1.0);
		const int step = std::clamp(static_cast<int>(std::lround(shade * (kShadeSteps - 1))), 0, kShadeSteps - 1);
		const int surfaceSlot = std::clamp(source.surface, 0, surfaceCount - 1);
		target.brushIndex = std::clamp(surfaceSlot * kShadeSteps + step, 0,
			std::max(static_cast<int>(scene.fillBrushes.size()) - 1, 0));
		target.shadowIndex = step;

		target.visible = true;
		work.order.append(index);
		++work.visibleTriangles;
	}


}

ModelViewportHit ModelViewport::hitAt(const QPointF& point)
{
	ensureProjection();
	return hitTest(point);
}

ModelViewportEdgeHit ModelViewport::edgeAt(const QPointF& point, double tolerance)
{
	ensureProjection();
	ModelViewportEdgeHit result;
	if (!std::isfinite(tolerance) || tolerance < 0 || tolerance > 64 || isRendering() || m_editMoveActive) { return result; }
	const auto hit = hitTest(point);
	if (!hit.valid) { return result; }
	const auto triangle = m_meshTriangles[hit.triangle];
	double closest = tolerance * tolerance;
	for (auto edge : {qMakePair(triangle.a, triangle.b), qMakePair(triangle.b, triangle.c), qMakePair(triangle.c, triangle.a)}) {
		QPointF a, b;
		if (!projectSegment(editVertexPosition(triangle.surface, edge.first), editVertexPosition(triangle.surface, edge.second), &a, &b)) { continue; }
		const auto delta = b - a;
		const double length = QPointF::dotProduct(delta, delta);
		if (length <= 1e-12) { continue; }
		const double t = std::clamp(QPointF::dotProduct(point - a, delta) / length, 0.0, 1.0);
		const auto offset = point - (a + t * delta);
		const double distance = QPointF::dotProduct(offset, offset);
		const auto canonical = qMakePair(std::min(edge.first, edge.second), std::max(edge.first, edge.second));
		if (distance < closest || (distance == closest && (!result.valid || canonical < qMakePair(result.a, result.b)))) {
			closest = distance; result = {true, triangle.surface, canonical.first, canonical.second};
		}
	}
	return result;
}

QVector<ModelRasterTriangle> ModelViewport::rasterTriangles(const RasterWork& work)
{
	QVector<ModelRasterTriangle> result;
	result.reserve(work.order.size());
	for (int index : work.order) {
		if (work.cancelled.load()) { return {}; }
		const auto& projected = work.projected.at(index);
		if (!projected.visible) { continue; }
		ModelRasterTriangle triangle;
		triangle.source = index;
		triangle.highlighted = (index < work.highlighted.size() && work.highlighted.at(index)) || work.surfaceStrokeTriangles.contains(index);
		triangle.hovered = work.hover == index;
		triangle.color = work.fillBrushes.at(projected.brushIndex).color().rgba();
		triangle.light = 1.0 - (1.0 - double(projected.shadowIndex) / (kShadeSteps - 1)) * 135.0 / 255.0;
		if (projected.textureValid) {
			const int surface = work.meshTriangles.at(index).surface;
			const auto found = work.surfaceSkins.constFind(surface);
			triangle.texture = found == work.surfaceSkins.cend() ? &work.skin : &found.value();
			triangle.textureHasAlpha = found == work.surfaceSkins.cend() ? work.skinHasAlpha : work.surfaceSkinAlpha.contains(surface);
			if (triangle.texture->isNull()) { triangle.texture = nullptr; }
		}
		int selectedMask = 0;
		const auto source = work.meshTriangles[index];
		if (source.surface == work.edgeSelectionSurface) {
			const QPair<int, int> edges[] = {{source.b, source.c}, {source.c, source.a}, {source.a, source.b}};
			for (int e = 0; e < 3; ++e) {
				if (work.selectedEdges.contains(qMakePair(std::min(edges[e].first, edges[e].second), std::max(edges[e].first, edges[e].second)))) { selectedMask |= 1 << e; }
			}
		}
		for (int corner = 1; corner + 1 < projected.cornerCount; ++corner) {
			triangle.vertices = {projected.corners[0], projected.corners[corner], projected.corners[corner + 1]};
			triangle.edges = {true, corner + 1 == projected.cornerCount - 1, corner == 1};
			const int origin[] = {projected.cornerEdges[0], projected.cornerEdges[corner], projected.cornerEdges[corner + 1]};
			for (int e = 0; e < 3; ++e) { triangle.selectedEdges[e] = (origin[(e + 1) % 3] & origin[(e + 2) % 3] & selectedMask) != 0; }
			result.append(triangle);
		}
	}
	return result;
}

ModelViewportHit ModelViewport::hitTest(const QPointF& viewPoint) const
{
	ModelViewportHit hit;
	if (!std::isfinite(viewPoint.x()) || !std::isfinite(viewPoint.y()) || viewPoint.x() < 0 || viewPoint.y() < 0
		|| viewPoint.x() >= width() || viewPoint.y() >= height()) { return hit; }
	// Never select stale geometry during a camera, pose or material change.
	if (m_projectionDirty || !m_presentedRaster || m_presentedRaster->projectionRevision != m_projectionRevision) { return hit; }
	const int index = pickModelRaster(viewPoint, m_presentedRaster->triangles, m_presentedRaster->pickIndex);
	if (index >= 0 && index < m_meshTriangles.size()) {
		hit.valid = true;
		hit.triangle = index;
		hit.surface = m_meshTriangles.at(index).surface;
		hit.surfaceName = m_mesh.surfaces.at(hit.surface).name;
	}
	return hit;
}

int ModelViewport::materialStrokeTriangleAt(const QPointF& point) const
{
	if (!m_materialStrokeRaster || !std::isfinite(point.x()) || !std::isfinite(point.y())
		|| point.x() < 0 || point.y() < 0 || point.x() >= width() || point.y() >= height()) { return -1; }
	// Material regrouping changes flattened triangle IDs. Picking stays bound
	// to the mouse-down geometry until this transaction ends; previews use their
	// own triangle ordering. No camera or geometry edit can run during a stroke.
	return pickModelRaster(point, m_materialStrokeRaster->triangles, m_materialStrokeRaster->pickIndex);
}

void ModelViewport::clearMaterialStrokePreview()
{
	if (materialStrokeActive()) { finishMaterialStroke(false); return; }
	if (!m_materialStrokeRaster) { return; }
	auto original = std::move(m_materialStrokeRaster);
	const QScopedValueRollback guard(m_materialStrokePreviewChange, true);
	if (m_materialStrokePreviewChanged) { setMesh(original->mesh, true); setSurfaceSkins(m_materialStrokeSkins); }
	setHighlightedTriangles(m_materialStrokeHighlights); setSurfaceStrokePreview({}, 0);
	m_materialStrokeSkins.clear(); m_materialStrokeHighlights.clear(); m_materialStrokePreviewChanged = false;
}

void ModelViewport::applyOrbitDelta(double yawDelta, double pitchDelta)
{
	setOrbit(m_yaw + yawDelta, m_pitch + pitchDelta);
}

void ModelViewport::applyPanDelta(const QPointF& delta)
{
	// Panning is clamped so the model can never be flung so far off screen that
	// the user cannot find it again.
	const double limit = kPanLimitFactor * std::max(width(), height());
	const QPointF panned(std::clamp(m_pan.x() + delta.x(), -limit, limit),
		std::clamp(m_pan.y() + delta.y(), -limit, limit));
	if (qFuzzyCompare(panned.x() + 1.0, m_pan.x() + 1.0) && qFuzzyCompare(panned.y() + 1.0, m_pan.y() + 1.0)) {
		return;
	}
	m_pan = panned;
	invalidateProjection();
	update();
	Q_EMIT viewChanged();
}

void ModelViewport::applyZoomFactor(double factor, const QPointF& anchor)
{
	const double scale = clampScale(m_scale * factor);
	if (qFuzzyCompare(scale + 1.0, m_scale + 1.0)) {
		return;
	}
	// Keep the point under the cursor pinned: the model centre sits at
	// origin = half the widget plus the pan, so scaling about `anchor` is a pure
	// adjustment of the pan.
	const QPointF origin(width() * 0.5 + m_pan.x(), height() * 0.5 + m_pan.y());
	const QPointF offset = origin - anchor;
	const double ratio = scale / m_scale;
	m_scale = scale;
	m_pan += offset * (ratio - 1.0);
	const double limit = kPanLimitFactor * std::max(width(), height());
	m_pan = QPointF(std::clamp(m_pan.x(), -limit, limit), std::clamp(m_pan.y(), -limit, limit));
	invalidateProjection();
	announceView();
}

void ModelViewport::announceView()
{
	finishEditTransform(false);
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT viewChanged();
}

void ModelViewport::paintEmptyState(QPainter& painter, const Palette& palette) const
{
	QFont headingFont = painter.font();
	headingFont.setBold(true);
	painter.setFont(headingFont);
	painter.setPen(QPen(palette.text, 1.0));
	const QRectF area = QRectF(rect()).adjusted(24.0, 24.0, -24.0, -24.0);
	const qreal headingHeight = painter.fontMetrics().height();
	const QRectF headingRect(area.left(), area.center().y() - headingHeight - 8.0, area.width(), headingHeight);
	painter.drawText(headingRect, Qt::AlignHCenter | Qt::AlignVCenter, tr("No model is loaded"));

	headingFont.setBold(false);
	painter.setFont(headingFont);
	painter.setPen(QPen(palette.subtleText, 1.0));
	const QRectF bodyRect(area.left(), area.center().y(), area.width(), area.height() / 2.0);
	painter.drawText(bodyRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
		tr("Select a model to preview it."));
}

void ModelViewport::paintNoGeometryState(QPainter& painter, const Palette& palette) const
{
	QFont headingFont = painter.font();
	headingFont.setBold(true);
	painter.setFont(headingFont);
	painter.setPen(QPen(palette.text, 1.0));
	const QRectF area = QRectF(rect()).adjusted(24.0, 24.0, -24.0, -24.0);
	const qreal headingHeight = painter.fontMetrics().height();
	const QRectF headingRect(area.left(), area.center().y() - headingHeight - 8.0, area.width(), headingHeight);
	painter.drawText(headingRect, Qt::AlignHCenter | Qt::AlignVCenter,
		tr("%1 header read, no drawable geometry").arg(modelMeshFormatDisplayName(m_mesh.format)));

	headingFont.setBold(false);
	painter.setFont(headingFont);
	painter.setPen(QPen(palette.subtleText, 1.0));
	const QRectF bodyRect(area.left(), area.center().y(), area.width(), area.height() / 2.0);
	painter.drawText(bodyRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
		tr("The file parsed, but this format's meshes are not decoded yet. The model inspector still lists "
		   "its header fields, frames and skins."));
}

void ModelViewport::paintGround(QPainter& painter, const Palette& palette) const
{
	// The ground grid is drawn before the model. A painter's algorithm would
	// otherwise have to sort grid segments against triangles for very little
	// gain; instead the grid reads as a floor the model stands on.
	const double spacing = gridSpacingForScale(m_perspective ? m_camera.scale : m_scale, m_radius);
	if (spacing <= 0.0) {
		return;
	}
	double extent = std::max(m_radius * 2.0, spacing * 4.0);
	int lines = static_cast<int>(std::floor(extent / spacing)) * 2 + 1;
	if (lines > kMaxGridLines) {
		lines = kMaxGridLines;
	}
	const int half = std::max((lines - 1) / 2, 1);
	extent = half * spacing;

	QPen minorPen(palette.grid, 1.0);
	QPen majorPen(palette.gridMajor, 1.4);
	for (int step = -half; step <= half; ++step) {
		const double offset = step * spacing;
		const bool major = step == 0 || (std::abs(step) % 8) == 0;
		painter.setPen(major ? majorPen : minorPen);
		QPointF a;
		QPointF b;
		if (projectSegment(makeVec(offset, -extent, 0.0), makeVec(offset, extent, 0.0), &a, &b)) {
			painter.drawLine(a, b);
		}
		if (projectSegment(makeVec(-extent, offset, 0.0), makeVec(extent, offset, 0.0), &a, &b)) {
			painter.drawLine(a, b);
		}
	}
}

void ModelViewport::paintAxes(QPainter& painter, const Palette& palette) const
{
	const double length = std::max(m_radius, 8.0) * 1.15;
	const struct {
		ModelVec3 direction;
		QColor color;
		const char* label;
	} axes[] = {
		{makeVec(length, 0.0, 0.0), palette.axisX, "X"},
		{makeVec(0.0, length, 0.0), palette.axisY, "Y"},
		{makeVec(0.0, 0.0, length), palette.axisZ, "Z"},
	};
	QFont labelFont = painter.font();
	labelFont.setBold(true);
	for (const auto& axis : axes) {
		QPointF origin;
		QPointF tip;
		if (!projectSegment(makeVec(0.0, 0.0, 0.0), axis.direction, &origin, &tip)) {
			continue;
		}
		painter.setPen(QPen(axis.color, 1.8));
		painter.drawLine(origin, tip);
		painter.setFont(labelFont);
		// The axis letter is the non-colour cue: Z always points up the screen
		// for a Z-up model, and the labels say which is which regardless.
		painter.drawText(tip + QPointF(4.0, -4.0), QString::fromLatin1(axis.label));
	}
}

void ModelViewport::renderRaster(RasterWork& work)
{
	if (!work.projectionReady) { projectRaster(work); }
	if (work.cancelled.load()) { return; }
	work.triangles = rasterTriangles(work);
	if (!work.projectionReady && !buildModelRasterPickIndex(work.logicalSize, work.triangles, &work.pickIndex, &work.cancelled)) { return; }
	if (work.reuseBase) { work.success = true; return; }
	if (work.wireframe) {
		QVector<ModelWireSegment> segments;
		segments.reserve(work.order.size() * 2);
		QSet<quint64> baseEdges, highlightedEdges;
		int surface = -1;
		for (int index : std::as_const(work.order)) {
			if (work.cancelled.load()) { return; }
			const auto& source = std::as_const(work.meshTriangles).at(index);
			if (source.surface != surface) {
				surface = source.surface;
				baseEdges.clear(); highlightedEdges.clear();
			}
			const bool highlighted = (index < work.highlighted.size() && work.highlighted.at(index)) || work.surfaceStrokeTriangles.contains(index);
			const auto& triangle = work.projected.at(index);
			for (int corner = 0; corner < triangle.cornerCount; ++corner) {
				const int next = (corner + 1) % triangle.cornerCount;
				const int mask = triangle.cornerEdges[corner] & triangle.cornerEdges[next];
				const int a = mask == 1 ? source.b : mask == 2 ? source.c : source.a;
				const int b = mask == 1 ? source.c : mask == 2 ? source.a : source.b;
				const auto edge = qMakePair(std::min(a, b), std::max(a, b));
				const quint64 key = (quint64(edge.first) << 32) | quint32(edge.second);
				// Clipping boundaries have no source edge. Legacy face moves can
				// separate shared endpoints; retain every segment in that case.
				const bool shared = mask != 0 && !work.moving;
				if (!shared || !baseEdges.contains(key)) {
					segments.append({triangle.screen[corner], triangle.screen[next], false});
					if (shared) { baseEdges.insert(key); }
				}
				const bool explicitEdge = mask != 0 && source.surface == work.edgeSelectionSurface && work.selectedEdges.contains(edge);
				if (highlighted && !explicitEdge && (!shared || !highlightedEdges.contains(key))) {
					segments.append({triangle.screen[corner], triangle.screen[next], true});
					if (shared) { highlightedEdges.insert(key); }
				}
			}
		}
		if (work.edgeSelectionSurface >= 0 && work.edgeSelectionSurface < work.mesh.surfaces.size()) {
			const auto& part = std::as_const(work.mesh).surfaces[work.edgeSelectionSurface];
			const int frame = std::min(work.frame, int(part.frames.size()) - 1);
			if (frame >= 0) {
				for (auto edge : std::as_const(work.selectedEdges)) {
					if (work.cancelled.load()) { return; }
					QPointF a, b;
					if (work.projectSegment(work.vertexPosition(work.edgeSelectionSurface, edge.first), work.vertexPosition(work.edgeSelectionSurface, edge.second), &a, &b)) {
						segments.append({a, b, true});
					}
				}
			}
		}
		ModelWireStyle style;
		style.pixelRatio = work.style.pixelRatio;
		style.width = work.highContrast ? 1.6 : 1.0;
		style.selectionWidth = work.highContrast ? 3.2 : 2.4;
		style.wire = work.palette.wire.rgba();
		style.selection = work.palette.highlight.rgba();
		work.success = renderModelWireframe(work.size, segments, style, &work.result.image, &work.cancelled);
		return;
	}
	// Keep logical-coordinate triangles and their owned texture snapshots for
	// exact subpixel picking. Only this worker allocates the scaled render copy.
	auto scaled = work.triangles;
	for (auto& triangle : scaled) {
		if (work.cancelled.load()) { return; }
		if (triangle.highlighted && !triangle.texture) {
			const double light = double(work.projected.at(triangle.source).shadowIndex) / (kShadeSteps - 1);
			triangle.color = scaledColor(work.palette.highlight, 0.45 + 0.55 * light).rgba();
		}
		for (auto& vertex : triangle.vertices) { vertex.screen *= work.style.pixelRatio; }
	}
	work.success = renderModelRaster(work.size, scaled, work.style, &work.result, &work.cancelled);
}

void ModelViewport::renderVertexOverlay(RasterWork &work)
{
	if (!work.success || work.cancelled.load() || work.editSurface < 0 || work.editSurface >= work.mesh.surfaces.size())
	{
		return;
	}
	const auto &surface = std::as_const(work.mesh).surfaces[work.editSurface];
	if (surface.frames.isEmpty())
	{
		return;
	}
	if (!work.vertices)
	{
		auto vertices = std::make_shared<ModelVertexProjection>();
		vertices->surface = work.editSurface;
		vertices->logicalSize = work.logicalSize;
		const int count = surface.frames[std::min(work.frame, int(surface.frames.size()) - 1)].positions.size();
		vertices->positions.resize(count);
		vertices->visible.resize(count);
		const QRectF bounds = QRectF(QPointF{}, work.logicalSize).adjusted(-3, -3, 3, 3);
		const double ratio = double(work.result.image.width()) / std::max(1, work.logicalSize.width());
		// Use the visible marker's footprint: an acute silhouette can contain no
		// covered pixel within one logical pixel of its exact projected corner.
		const int radius = std::clamp(int(std::ceil(3 * ratio)), 1, 12);
		const auto visible = [&](int vertex, QPointF point)
		{
			if (!std::isfinite(point.x()) || !std::isfinite(point.y()) || !bounds.contains(point))
			{
				return false;
			}
			if (work.wireframe)
			{
				return true;
			}
			if (work.result.source.size() != qint64(work.size.width()) * work.size.height())
			{
				return false;
			}
			const int px = int(std::floor(point.x() * ratio)), py = int(std::floor(point.y() * ratio));
			// A silhouette may lie just beyond half-open pixel coverage. Only an
			// incident face may provide nearby coverage; unrelated faces cannot.
			for (int y = std::max(0, py - radius); y <= std::min(work.size.height() - 1, py + radius); ++y)
			{
				for (int x = std::max(0, px - radius); x <= std::min(work.size.width() - 1, px + radius); ++x)
				{
					const int source = std::as_const(work.result.source)[y * work.size.width() + x];
					if (source < 0 || source >= work.meshTriangles.size())
					{
						continue;
					}
					const auto &face = std::as_const(work.meshTriangles)[source];
					if (face.surface == work.editSurface && (face.a == vertex || face.b == vertex || face.c == vertex))
					{
						return true;
					}
				}
			}
			return false;
		};
		for (int vertex = 0; vertex < count; ++vertex)
		{
			if (work.cancelled.load())
			{
				return;
			}
			const auto position = work.vertexPosition(work.editSurface, vertex);
			QPointF point;
			if (work.perspective)
			{
				double x, y, z;
				work.toView(position, &x, &y, &z);
				const double nan = std::numeric_limits<double>::quiet_NaN();
				point = z < kNearPlane ? QPointF(nan, nan) : work.fromView(x, y, z);
			}
			else
			{
				point = work.projectPoint(position, nullptr);
			}
			vertices->positions[vertex] = point;
			vertices->visible.setBit(vertex, visible(vertex, point));
		}
		if (!indexModelVertices(vertices.get(), &work.cancelled))
		{
			work.success = false;
			return;
		}
		work.vertices = std::move(vertices);
	}
	if (work.vertexPicking)
	{
		work.success = renderModelVertexOverlay(*work.vertices, work.size, work.style.pixelRatio, work.editVertices, work.xrayVertices,
												work.editMoveActive, work.vertexAccent, &work.vertexOverlay, &work.cancelled);
	}
}

void ModelViewport::paintTriangles(QPainter &painter, const Palette &palette)
{
	const int hovered = m_hover.valid ? m_hover.triangle : -1;
	if (m_rasterHover != hovered)
	{
		m_rasterHover = hovered;
		if (m_renderMode != ModelViewportRenderMode::Wireframe)
		{
			invalidateRaster();
		}
	}
	if (m_rasterDirty && !m_rasterThread)
	{
		// High-DPI renders stay sharp up to the bounded buffer budget; extreme
		// window sizes keep the same framing with a uniformly scaled image.
		double ratio = devicePixelRatioF();
		const double pixels = double(width()) * height() * ratio * ratio;
		if (pixels > modelRasterMaxPixels)
		{
			ratio *= std::sqrt(modelRasterMaxPixels / pixels);
		}
		const QSize target(std::max(1, int(std::floor(width() * ratio))), std::max(1, int(std::floor(height() * ratio))));
		ModelRasterStyle style;
		style.pixelRatio = ratio;
		style.showEdges = m_showEdges;
		style.edge = palette.edge.rgba();
		style.hatch =
			QColor(palette.background.red(), palette.background.green(), palette.background.blue(), m_highContrast ? 200 : 110).rgba();
		style.hover = palette.hover.rgba();
		style.selection = palette.highlight.rgba();
		auto work = std::make_shared<RasterWork>();
		work->revision = m_rasterRevision;
		work->baseRevision = m_baseRasterRevision;
		work->contentRevision = m_rasterContentRevision;
		work->size = target;
		work->logicalSize = size();
		work->tags = projectTagOverlays();
		work->collision = projectCollisionOverlays();
		work->style = style;
		work->projectionRevision = m_projectionRevision;
		if (m_presentedRaster && m_presentedRaster->projectionRevision == m_projectionRevision)
		{
			// Selection/hover/style updates share immutable projection/index data;
			// their topology, camera, clipping and texture coverage have not changed.
			work->projectionReady = true;
			work->projected = m_presentedRaster->projected;
			work->order = m_presentedRaster->order;
			work->pickIndex = m_presentedRaster->pickIndex;
			work->visibleTriangles = m_presentedRaster->visibleTriangles;
			work->culledTriangles = m_presentedRaster->culledTriangles;
		}
		work->mesh = m_mesh;
		work->meshTriangles = m_meshTriangles;
		work->fillBrushes = m_fillBrushes;
		work->camera = m_camera;
		work->center = m_center;
		work->eye = m_eye;
		work->moveOffset = m_moveOffset;
		work->perspective = m_perspective;
		work->backfaceCulling = m_backfaceCulling;
		work->textured = m_renderMode == ModelViewportRenderMode::Textured && hasSkin();
		work->wireframe = m_renderMode == ModelViewportRenderMode::Wireframe;
		work->highContrast = m_highContrast;
		work->moving = m_dragStarted && m_dragAction == DragAction::Move;
		work->resizingSelection = isResizingSelection();
		work->resizeFrom = m_selectionResizeBox; work->resizeTo = m_selectionResizePreview;
		work->resizeOrigins = m_selectionResizeOrigins;
		work->editMoveActive = m_editMoveActive;
		work->editTagEmpty = m_editTag.isEmpty();
		work->frame = m_frame;
		work->blendFrame = m_blendFrame;
		work->frameBlend = m_frameBlend;
		work->editSurface = m_editSurface;
		work->editTransform = m_editTransform;
		work->editVertices = m_editVertices; work->editSurfaces = m_editSurfaces;
		work->vertexPicking = m_vertexPicking && !(m_playing && (m_interpolateAnimation || m_nativeMdl));
		work->xrayVertices = m_xrayVertices;
		work->vertexAccent = this->palette().color(QPalette::Highlight);
		work->highlighted = m_highlighted;
		work->surfaceStrokeTriangles = m_surfaceStrokeTriangles;
		work->edgeSelectionSurface = m_edgeSelectionSurface;
		work->selectedEdges = m_selectedEdges;
		work->hover = hovered;
		work->palette = palette;
		work->skin = m_nativeMdl && m_mdlSkinVisible ? m_mdlPlaybackSkins.at(m_mdlSample.skinMember) : m_skin;
		work->skinHasAlpha = !m_nativeMdl && m_skinHasAlpha;
		if (!m_nativeMdl)
		{
			work->surfaceSkins = m_surfaceSkins;
			work->surfaceSkinAlpha = m_surfaceSkinAlpha;
		}
		work->reuseBase = m_presentedRaster && m_presentedRaster->baseRevision == m_baseRasterRevision;
		if (work->reuseBase)
		{
			work->result = m_raster;
			if (m_rasterVertices && m_rasterVertices->surface == m_editSurface)
			{
				work->vertices = m_rasterVertices;
			}
		}
		else
		{
			work->result.depth = std::move(m_raster.depth);
			work->result.source = std::move(m_raster.source);
		}
		m_rasterWork = work;
		auto *thread = QThread::create(
			[work]()
			{
				try
				{
					renderRaster(*work);
					renderVertexOverlay(*work);
				}
				catch (const std::bad_alloc &)
				{
					work->success = false;
					work->result.clear();
					work->vertexOverlay = {};
					work->vertices.reset();
				}
			});
		thread->setParent(this);
		m_rasterThread = thread;
		connect(thread, &QThread::finished, this,
				[this, thread, work]()
				{
					m_rasterThread = nullptr;
					m_rasterWork.reset();
					if (work->contentRevision == m_rasterContentRevision && !work->cancelled.load())
					{
						m_raster = std::move(work->result);
						m_vertexOverlay = std::move(work->vertexOverlay);
						m_rasterVertices = work->success ? work->vertices : nullptr;
						m_projected = work->projected;
						m_order = work->order;
						m_visibleTriangles = work->visibleTriangles;
						m_culledTriangles = work->culledTriangles;
						m_presentedRaster = work->success ? work : nullptr;
						m_rasterTags = std::move(work->tags);
						m_rasterCollision = std::move(work->collision);
						m_rasterLogicalSize = work->logicalSize;
						m_rasterFailed = !work->success;
						// Camera/style updates coalesce behind this completed image. This
						// keeps motion visible even when requests outpace the renderer.
						m_rasterDirty = work->revision != m_rasterRevision;
					}
					thread->deleteLater();
					setAccessibleDescription(accessibleSummary());
					update();
					Q_EMIT renderCompleted();
				});
		thread->start();
		setAccessibleDescription(accessibleSummary());
	}
	painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
	if (!m_raster.image.isNull())
	{
		painter.drawImage(QRectF(rect()), m_raster.image);
	}
	if (isRendering() || m_rasterFailed)
	{
		const QString message =
			m_rasterFailed ? tr("Unable to allocate the model preview. Reduce the viewport size.") : tr("Rendering model…");
		const int textWidth = std::max(1, width() - 24);
		const auto textBounds = painter.fontMetrics().boundingRect(QRect(0, 0, textWidth, height()), Qt::TextWordWrap, message);
		const int statusHeight = std::min(height(), textBounds.height() + 12);
		const QRectF area(12, std::max(0, height() - statusHeight - 10), textWidth, statusHeight);
		painter.fillRect(area, palette.background);
		painter.setPen(palette.text);
		painter.drawText(area, Qt::AlignCenter | Qt::TextWordWrap, message);
	}
}

void ModelViewport::paintOverlay(QPainter& painter, const Palette& palette) const
{
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.resetTransform();
	if (!isRendering() && m_renderMode == ModelViewportRenderMode::Wireframe && m_hover.valid && m_hover.triangle >= 0 && m_hover.triangle < m_projected.size()) {
		const ProjectedTriangle& triangle = m_projected.at(m_hover.triangle);
		if (triangle.visible && triangle.cornerCount >= 3) {
			painter.setBrush(Qt::NoBrush);
			painter.setPen(QPen(palette.hover, 1.8));
			painter.drawPolygon(triangle.screen.data(), triangle.cornerCount);
		}
	}

	if (hasFocus()) {
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(palette.focus, 2.0, Qt::SolidLine));
		painter.drawRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5));
		painter.setPen(QPen(palette.background, 1.0, Qt::DashLine));
		painter.drawRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5));
	}
}

void ModelViewport::paintEvent(QPaintEvent*)
{
	Palette palette;
	if (m_highContrast) {
		palette.background = QColor(0, 0, 0);
		palette.grid = QColor(70, 70, 70);
		palette.gridMajor = QColor(140, 140, 140);
		palette.axisX = QColor(255, 80, 80);
		palette.axisY = QColor(80, 255, 80);
		palette.axisZ = QColor(120, 160, 255);
		palette.wire = QColor(255, 255, 255);
		palette.edge = QColor(0, 0, 0);
		palette.hover = QColor(255, 255, 0);
		palette.text = QColor(255, 255, 255);
		palette.subtleText = QColor(220, 220, 220);
		palette.focus = QColor(255, 255, 0);
		palette.highlight = QColor(255, 0, 255);
	} else {
		palette.background = QColor(20, 22, 27);
		palette.grid = QColor(40, 44, 52);
		palette.gridMajor = QColor(62, 68, 80);
		palette.axisX = QColor(226, 104, 104);
		palette.axisY = QColor(126, 206, 132);
		palette.axisZ = QColor(122, 158, 240);
		palette.wire = QColor(214, 222, 234);
		palette.edge = QColor(24, 26, 32, 150);
		palette.hover = QColor(255, 210, 90);
		palette.text = QColor(232, 236, 244);
		palette.subtleText = QColor(160, 168, 182);
		palette.focus = QColor(120, 180, 250);
		palette.highlight = QColor(255, 170, 60);
	}

	QPainter painter(this);
	painter.fillRect(rect(), palette.background);
	painter.setRenderHint(QPainter::Antialiasing, true);

	if (!m_hasMesh) {
		if (m_brushDrawTool) { paintBrushDraw(painter); paintHud(painter,palette); }
		else { paintEmptyState(painter, palette); }
		if (m_overlayPainter) { m_overlayPainter(painter); }
		paintOverlay(painter, palette);
		return;
	}

	// Rebuilding here is a no-op unless the frame, camera, widget size or device
	// pixel ratio actually changed; it reuses its storage, and it never calls
	// update().
	ensureProjection();

	if (m_showGrid && !m_brushDrawTool) {
		paintGround(painter, palette);
	}
	if (m_showAxes) {
		paintAxes(painter, palette);
	}
	if (m_meshTriangles.isEmpty()) {
		if (!m_brushDrawTool) { paintNoGeometryState(painter, palette); }
	} else {
		paintTriangles(painter, palette);
		paintCollisionOverlays(painter);
		paintEditOverlays(painter);
		paintSelectionResize(painter);
	}
	paintBrushDraw(painter);
	if (!m_meshTriangles.isEmpty() || m_brushDrawTool) { paintHud(painter, palette); }
	if (m_overlayPainter) {
		painter.save();
		m_overlayPainter(painter);
		painter.restore();
	}
	paintOverlay(painter, palette);
}

void ModelViewport::paintHud(QPainter& painter, const Palette& palette) const
{
	const QString separator = QStringLiteral("  %1  ").arg(QChar(0x00b7));
	QStringList left;
	if (!m_viewLabel.isEmpty()) { left << m_viewLabel; }
	if (m_perspective) { left << tr("Camera, %1%2 view").arg(qRound(m_fov)).arg(QChar(0x00b0)); }
	left << effectiveRenderModeName();
	if (isResizingSelection()) { left << selectionResizeSummary(false); }
	if (m_brushDrawTool) { left << brushDrawSummary(false); }
	if (m_surfaceTool != ModelViewportSurfaceTool::None) {
		left << (m_surfaceTool == ModelViewportSurfaceTool::Paint ? tr("Paint materials") : tr("Sample material"));
	}
	if (m_perspective && m_looking) { left << (m_lookHoldActive
		? tr("Mouse look (holding %1)").arg(m_controls.lookHoldKey) : tr("Mouse look (Esc to stop)")); }
	if (m_pointerDriving) { left << tr("Position steering (release or Esc to stop)"); }
	const QStringList right = {
		QStringList {QFileInfo(m_mesh.sourcePath).fileName(), tr("frame %1 / %2").arg(m_frame + 1).arg(std::max(1, m_frameTotal))}.join(separator),
		QStringList {tr("%n surface(s)", nullptr, m_mesh.surfaceCount), tr("%n vert(ex)(ices)", nullptr, m_mesh.vertexCount),
			tr("%n triangle(s)", nullptr, m_mesh.triangleCount)}.join(separator),
	};

	paintViewportHud(painter, rect(), font(), left, right, layoutDirection(),
		m_highContrast ? palette.text : palette.subtleText, palette.background, m_highContrast);
}

void ModelViewport::mousePressEvent(QMouseEvent* event)
{
	if (m_materialGesturesEnabled && cameraMaterialGesture(m_controls, event->button(), event->modifiers()) != CameraMaterialGesture::None) {
		// Consume a reserved chord even while busy; it must not fall through to
		// navigation or cancel another pending edit. Multi-button chords do nothing.
		m_materialGestureButtons.setFlag(event->button());
		if (event->buttons() == event->button()) {
			setFocus(Qt::MouseFocusReason);
			beginMaterialStroke(event->position(), event->button(), event->modifiers());
		}
		event->accept(); return;
	}
	m_materialGestureButtons.setFlag(event->button(), false);
	if (m_pointerDriving) { endPointerDrive(); event->accept(); return; }
	if (isDrawingBrush()) { finishBrushDraw(false); event->accept(); return; }
	if (isResizingSelection()) { finishSelectionResize(false); event->accept(); return; }
	setFocus(Qt::MouseFocusReason);
	if (m_looking && m_controls.lookPanUsesButtons) { event->accept(); return; }
	const Qt::MouseButton button = event->button();
	if (m_brushDrawTool && button == Qt::LeftButton && !m_looking
		&& (event->modifiers() & ~(m_brushSquareModifiers | m_brushCubeModifiers)) == Qt::NoModifier) {
		if (beginBrushDraw(event->position(),event->modifiers())) { m_pressButton = button; setCursor(Qt::CrossCursor); }
		event->accept(); return;
	}
	if (button == Qt::LeftButton && event->modifiers() == Qt::NoModifier && beginSelectionResize(event->position())) {
		m_pressButton = button; setCursor(Qt::SizeAllCursor); event->accept(); return;
	}
	if (m_surfaceStrokeActive) { finishSurfaceStroke(false); }
	if (m_surfaceTool != ModelViewportSurfaceTool::None && button == Qt::LeftButton && event->modifiers() == Qt::NoModifier && !m_looking) {
		m_pressButton = Qt::NoButton;
		if (m_surfaceTool == ModelViewportSurfaceTool::Paint) { beginSurfaceStroke(event->position()); }
		else { sampleSurfaceAt(event->position()); }
		m_surfaceToolPressed = true;
		event->accept(); return;
	}
	if (button == Qt::LeftButton && event->modifiers() == Qt::NoModifier
		&& !(m_playing && (m_interpolateAnimation || m_nativeMdl)) && beginEditTransform(event->position())) {
		m_pressButton = button; setCursor(Qt::SizeAllCursor); event->accept(); return;
	}
	if (button != Qt::LeftButton && button != Qt::RightButton && button != Qt::MiddleButton) {
		QWidget::mousePressEvent(event);
		return;
	}
	// What the press becomes is settled when it moves or lets go: a drag
	// orbits, looks, or pans as the controls say; a click picks, opens the
	// menu, or turns mouse look on or off.
	m_pressButton = button;
	m_pressModifiers = event->modifiers();
	m_pressPoint = event->position();
	m_dragAnchor = event->position();
	m_dragStarted = false;
	m_dragAction = dragActionFor(button, event->modifiers());
	if (m_dragAction == DragAction::Drive) {
		m_dragStarted = beginPointerDrive(event->position());
		if (!m_dragStarted) { m_pressButton = Qt::NoButton; m_dragAction = DragAction::None; }
		event->accept(); return;
	}
	// Hold-to-fly starts on the press, even before the drag threshold, and
	// only for the exact look gesture (Alt+right may instead orbit).
	rebuildKeyMotions();
	// A left drag that starts on the selection moves it, as TrenchBroom's
	// and Radiant's cameras do; Alt moves it up and down.
	const Qt::KeyboardModifiers keys = event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
	if (m_perspective && m_controls.dragMovesSelection && button == Qt::LeftButton && (keys == Qt::NoModifier || keys == Qt::AltModifier)) {
		ensureProjection();
		const ModelViewportHit hit = hitTest(event->position());
		if (!isRendering() && hit.valid && hit.triangle >= 0 && hit.triangle < m_highlighted.size() && m_highlighted.at(hit.triangle)) {
			m_dragAction = DragAction::Move;
			m_moveVertical = keys == Qt::AltModifier;
			m_moveStart = orbitPivotAt(event->position());
			m_moveOffset = makeVec(0.0, 0.0, 0.0);
		}
	}
	event->accept();
}

void ModelViewport::mouseMoveEvent(QMouseEvent* event)
{
	const QPointF position = event->position();
	if (materialStrokeActive()) {
		if (event->buttons() == m_materialStrokeButton) { extendMaterialStroke(position, event->modifiers()); }
		else { finishMaterialStroke(false); }
		event->accept(); return;
	}
	if (m_materialStrokeRaster) { event->accept(); return; }
	if (m_pointerDriving) {
		const auto buttons = event->buttons() & ~m_materialGestureButtons;
		if (buttons != m_controls.driveButton || event->modifiers() != m_controls.driveModifiers) { endPointerDrive(); }
		else { updatePointerDrive(position); }
		event->accept(); return;
	}
	if (isDrawingBrush()) { updateBrushDraw(position,event->modifiers()); event->accept(); return; }
	if (isResizingSelection()) { updateSelectionResize(position); event->accept(); return; }
	if (m_surfaceToolPressed || m_surfaceStrokeActive) { extendSurfaceStroke(position); event->accept(); return; }
	if (m_editMoveActive) { updateEditTransform(position); event->accept(); return; }
	if (m_pressButton != Qt::NoButton) {
		if (!m_dragStarted) {
			const QPointF travel = position - m_pressPoint;
			if (m_dragAction == DragAction::None || std::hypot(travel.x(), travel.y()) < kDragThreshold) {
				event->accept();
				return;
			}
			m_dragStarted = true;
			m_orbiting = m_dragAction == DragAction::Orbit;
			m_panning = m_dragAction == DragAction::Pan;
			if (m_perspective && m_dragAction == DragAction::Orbit) {
				m_pivot = orbitPivotAt(m_pressPoint);
			}
			if (!m_looking) {
				setCursor(m_dragAction == DragAction::Pan ? Qt::ClosedHandCursor : Qt::SizeAllCursor);
			}
		}
		if (m_dragAction == DragAction::Move) {
			updateMove(position);
			event->accept();
			return;
		}
		// The travel before the drag counted is applied too, so nothing the
		// pointer did is lost.
		applyDrag(m_dragAction, position - m_dragAnchor);
		m_dragAnchor = position;
		event->accept();
		return;
	}

	if (m_looking) {
		// Mouse look: the pointer turns the camera; Ctrl strafes and Shift
		// moves along the view, as Radiant's camera does.
		const QPointF delta = position - m_lookAnchor;
		if (!delta.isNull()) {
			const auto motion = cameraFreeLookMotion(m_controls, event->buttons(), event->modifiers());
			if (motion == CameraFreeLookMotion::Pan) {
				panPerspective(delta);
			} else if (motion == CameraFreeLookMotion::Dolly) {
				moveCamera(-delta.y() * flySpeed() / 600.0, delta.x() * flySpeed() / 600.0, 0.0);
			} else {
				turnCamera(-delta.x() * kLookDegreesPerPixel, -delta.y() * kLookDegreesPerPixel);
			}
			recentreLookCursor(position);
		}
		event->accept();
		return;
	}

	const ModelViewportHit hit = hitTest(position);
	const bool changed = hit.valid != m_hover.valid || hit.triangle != m_hover.triangle;
	m_hover = hit;
	if (changed) {
		Q_EMIT hoverChanged(hoverSummary());
		update();
	}
	event->accept();
}

void ModelViewport::mouseReleaseEvent(QMouseEvent* event)
{
	// Modifiers may have changed since the press. Consume its matching release
	// without ending a different button's camera or editing gesture.
	if (m_materialGestureButtons.testFlag(event->button())) {
		if (event->button() == m_materialStrokeButton) { extendMaterialStroke(event->position(), event->modifiers()); finishMaterialStroke(true); }
		m_materialGestureButtons.setFlag(event->button(), false);
		event->accept(); return;
	}
	if (m_pointerDriving) { endPointerDrive(); event->accept(); return; }
	if (isDrawingBrush() && event->button() == Qt::LeftButton) {
		updateBrushDraw(event->position(),event->modifiers()); finishBrushDraw(true); event->accept(); return;
	}
	if (isResizingSelection() && event->button() == Qt::LeftButton) {
		updateSelectionResize(event->position()); finishSelectionResize(true); event->accept(); return;
	}
	if ((m_surfaceToolPressed || m_surfaceStrokeActive) && event->button() == Qt::LeftButton) {
		extendSurfaceStroke(event->position()); finishSurfaceStroke(true); event->accept(); return;
	}
	if (m_editMoveActive && event->button() == Qt::LeftButton) { updateEditTransform(event->position()); finishEditTransform(true); event->accept(); return; }
	if (m_looking && m_controls.lookPanUsesButtons) { event->accept(); return; }
	if (m_pressButton == Qt::NoButton || event->button() != m_pressButton) {
		QWidget::mouseReleaseEvent(event);
		return;
	}
	const bool dragged = m_dragStarted;
	const Qt::MouseButton button = m_pressButton;
	const Qt::KeyboardModifiers modifiers = m_pressModifiers;
	if (dragged && m_dragAction == DragAction::Move) {
		const ModelVec3 offset = m_moveOffset;
		m_pressButton = Qt::NoButton;
		cancelMove();
		if (offset.x != 0.0f || offset.y != 0.0f || offset.z != 0.0f) {
			Q_EMIT moveRequested(offset.x, offset.y, offset.z);
		}
		event->accept();
		return;
	}
	m_pressButton = Qt::NoButton;
	m_dragStarted = false;
	m_dragAction = DragAction::None;
	m_orbiting = false;
	m_panning = false;
	rebuildKeyMotions();
	if (!m_looking) {
		unsetCursor();
	}
	const QPointF travel = event->position() - m_pressPoint;
	if (!dragged && std::hypot(travel.x(), travel.y()) < kDragThreshold) {
		if (button == Qt::LeftButton) {
			// A press that stayed put is a click on whatever lies under it.
			pickAt(event->position(), modifiers);
		} else if (m_perspective && button == m_controls.lookButton && m_controls.lookClickToggles) {
			setLooking(!m_looking);
		} else if (button == Qt::RightButton) {
			Q_EMIT contextMenuRequested(event->position().toPoint());
		}
	}
	event->accept();
}

void ModelViewport::wheelEvent(QWheelEvent* event)
{
	if (isDrawingBrush()) {
		m_brushWheelRemainder += double(event->angleDelta().y() ? event->angleDelta().y() : event->angleDelta().x())/120.0;
		const int steps = int(m_brushWheelRemainder);
		if (steps) { m_brushWheelRemainder -= steps; adjustBrushDrawDepth(steps); }
		event->accept(); return;
	}
	if (isResizingSelection()) { event->accept(); return; }
	if (m_editMoveActive) { event->accept(); return; }
	// Some systems turn Shift+wheel into a sideways scroll.
	int delta = event->angleDelta().y();
	if (delta == 0) {
		delta = event->angleDelta().x();
	}
	if (delta == 0) {
		QWidget::wheelEvent(event);
		return;
	}
	const double notches = static_cast<double>(delta) / 120.0;
	if (!m_perspective || m_controls.wheel == CameraWheel::Zoom) {
		applyZoomFactor(std::pow(1.2, notches), event->position());
		event->accept();
		return;
	}
	const Qt::KeyboardModifiers fieldOfView = m_controls.fieldOfViewWheelModifiers;
	if (fieldOfView != Qt::NoModifier && (event->modifiers() & fieldOfView) == fieldOfView) {
		// TrenchBroom's zoom: a narrower view, the camera where it stands.
		m_fov = std::clamp(m_fov * std::pow(0.93, notches), kMinFieldOfView, kMaxFieldOfView);
		invalidateProjection();
		announceView();
		event->accept();
		return;
	}
	dolly(notches, m_controls.wheel == CameraWheel::DollyToPointer ? event->position() : QPointF(width() * 0.5, height() * 0.5));
	event->accept();
}

void ModelViewport::keyPressEvent(QKeyEvent* event)
{
	if (m_materialStrokeRaster) {
		if (event->key() == Qt::Key_Escape) { finishMaterialStroke(false); }
		event->accept(); return;
	}
	if (m_pointerDriving && event->key() == Qt::Key_Escape) { endPointerDrive(); event->accept(); return; }
	if (m_pointerDriving && cameraNavigationDrag(m_controls, m_controls.driveButton, event->modifiers()) != CameraNavigationDrag::Drive) { endPointerDrive(); }
	if (m_brushDrawTool && !m_looking && event->key() == Qt::Key_Escape) {
		if (isDrawingBrush()) { finishBrushDraw(false); } else { setBrushDrawTool(false); }
		event->accept(); return;
	}
	if (isDrawingBrush()) { updateBrushDraw(m_brushDrawPoint,event->modifiers()); event->accept(); return; }
	if (isResizingSelection()) {
		if (event->key() == Qt::Key_Escape) { finishSelectionResize(false); }
		event->accept(); return;
	}
	if (event->key() == Qt::Key_Escape && m_lookHoldActive) { setLooking(false); event->accept(); return; }
	if (event->key() == Qt::Key_Escape && m_surfaceTool != ModelViewportSurfaceTool::None) {
		if (m_surfaceStrokeActive) { finishSurfaceStroke(false); }
		else { setSurfaceTool(ModelViewportSurfaceTool::None); }
		event->accept(); return;
	}
	if (m_surfaceStrokeActive) { event->accept(); return; }
	if (m_editMoveActive) {
		if (event->key() == Qt::Key_Escape) { finishEditTransform(false); }
		event->accept(); return;
	}
	if (m_perspective) {
		if (navigationHoldKeyMatches(m_controls.lookHoldKey, event->key(), event->modifiers())) {
			if (!event->isAutoRepeat()) { setTemporaryLooking(true); }
			event->accept(); return;
		}
		if (claimsLookToggle(event->key(), event->modifiers())) {
			if (!event->isAutoRepeat()) { setLooking(!m_looking); }
			event->accept(); return;
		}
		if (claimsKey(event->key(), event->modifiers())) {
			if (cameraKeyUsesFixedStep(m_controls, event->key(), m_looking, m_pressButton, m_pressModifiers, event->modifiers())) {
				m_heldKeys.remove(event->key());
				if (m_heldKeys.isEmpty()) { m_flyTimer->stop(); }
				stepCameraDrive(cameraKeyMotions(m_controls, false, m_pressButton, m_pressModifiers, event->modifiers()).value(event->key()));
				event->accept(); return;
			}
			if (!event->isAutoRepeat()) {
				m_heldKeys.insert(event->key());
				if (!m_flyTimer->isActive()) {
					m_flyClock.start();
					m_flyTimer->start();
				}
			}
			event->accept();
			return;
		}
		switch (event->key()) {
		case Qt::Key_Escape:
			if (m_dragStarted && m_dragAction == DragAction::Move) {
				// The selection goes back where it was; nothing was changed.
				m_pressButton = Qt::NoButton;
				cancelMove();
				event->accept();
				return;
			}
			if (m_looking) {
				setLooking(false);
				event->accept();
				return;
			}
			break;
		case Qt::Key_Plus:
		case Qt::Key_Equal:
			zoomIn();
			event->accept();
			return;
		case Qt::Key_Minus:
		case Qt::Key_Underscore:
			zoomOut();
			event->accept();
			return;
		case Qt::Key_Home:
			frameModel();
			event->accept();
			return;
		case Qt::Key_0:
			resetView();
			event->accept();
			return;
		default:
			break;
		}
		QWidget::keyPressEvent(event);
		return;
	}
	const bool pan = (event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) != 0;
	const double panStep = 24.0;
	switch (event->key()) {
	case Qt::Key_Left:
		if (pan) {
			applyPanDelta(QPointF(panStep, 0.0));
		} else {
			applyOrbitDelta(kKeyOrbitDegrees, 0.0);
		}
		event->accept();
		return;
	case Qt::Key_Right:
		if (pan) {
			applyPanDelta(QPointF(-panStep, 0.0));
		} else {
			applyOrbitDelta(-kKeyOrbitDegrees, 0.0);
		}
		event->accept();
		return;
	case Qt::Key_Up:
		if (pan) {
			applyPanDelta(QPointF(0.0, panStep));
		} else {
			applyOrbitDelta(0.0, kKeyOrbitDegrees);
		}
		event->accept();
		return;
	case Qt::Key_Down:
		if (pan) {
			applyPanDelta(QPointF(0.0, -panStep));
		} else {
			applyOrbitDelta(0.0, -kKeyOrbitDegrees);
		}
		event->accept();
		return;
	case Qt::Key_Plus:
	case Qt::Key_Equal:
		zoomIn();
		event->accept();
		return;
	case Qt::Key_Minus:
	case Qt::Key_Underscore:
		zoomOut();
		event->accept();
		return;
	case Qt::Key_Home:
		frameModel();
		event->accept();
		return;
	case Qt::Key_0:
		resetView();
		event->accept();
		return;
	case Qt::Key_Space:
		togglePlayback();
		event->accept();
		return;
	case Qt::Key_PageDown:
		stepFrame(1);
		event->accept();
		return;
	case Qt::Key_PageUp:
		stepFrame(-1);
		event->accept();
		return;
	default:
		break;
	}
	QWidget::keyPressEvent(event);
}

void ModelViewport::resizeEvent(QResizeEvent* event)
{
	finishEditTransform(false);
	QWidget::resizeEvent(event);
	// The projection is centred on the widget, so any resize invalidates it.
	invalidateProjection();
	Q_EMIT viewChanged();
}

void ModelViewport::leaveEvent(QEvent* event)
{
	if (m_hover.valid) {
		m_hover = ModelViewportHit();
		update();
	}
	Q_EMIT hoverChanged(m_hasMesh ? tr("Pointer left the model viewport.") : tr("No model loaded."));
	QWidget::leaveEvent(event);
}

// ---------------------------------------------------------------------------
// Camera controls
// ---------------------------------------------------------------------------

namespace {

// `vector` turned `degrees` about the unit `axis` (Rodrigues' rotation).
ModelVec3 rotateAbout(const ModelVec3& vector, const ModelVec3& axis, double degrees)
{
	const double radians = degrees * kPi / 180.0;
	const double c = std::cos(radians);
	const double s = std::sin(radians);
	const ModelVec3 cross = crossVec(axis, vector);
	const double along = dotVec(axis, vector) * (1.0 - c);
	return makeVec(vector.x * c + cross.x * s + axis.x * along, vector.y * c + cross.y * s + axis.y * along,
		vector.z * c + cross.z * s + axis.z * along);
}

} // namespace

void ModelViewport::setCameraControls(const CameraViewControls& controls, bool preserveView)
{
	finishMaterialStroke(false); clearMaterialStrokePreview();
	const bool wasPerspective = m_perspective;
	// A new profile ends every old gesture, including held-button flight.
	setLooking(false);
	finishSurfaceStroke(false);
	finishEditTransform(false);
	m_pressButton = Qt::NoButton;
	cancelMove();
	m_orbiting = false;
	m_panning = false;
	m_controls = controls;
	if (!preserveView) {
		m_perspective = controls.perspective;
		m_fov = std::clamp(controls.fieldOfViewDegrees, kMinFieldOfView, kMaxFieldOfView);
	}
	m_heldKeys.clear();
	m_flyTimer->stop();
	rebuildKeyMotions();
	if (m_perspective && !wasPerspective && m_hasMesh && !m_perspectivePlaced) {
		// First time as a camera: stand back from the south-west, looking
		// down on the whole scene.
		m_lookYaw = 45.0;
		m_lookPitch = -30.0;
		frameModelPerspective();
		return;
	}
	invalidateProjection();
	announceView();
}

const CameraViewControls& ModelViewport::cameraControls() const
{
	return m_controls;
}

bool ModelViewport::isPerspective() const
{
	return m_perspective;
}

ModelVec3 ModelViewport::cameraPosition() const
{
	return m_eye;
}

double ModelViewport::cameraYaw() const
{
	return m_lookYaw;
}

double ModelViewport::cameraPitch() const
{
	return m_lookPitch;
}

double ModelViewport::fieldOfView() const
{
	return m_fov;
}

CameraViewState ModelViewport::navigationState() const
{
	CameraViewState state;
	state.perspective = m_perspective;
	state.position = {m_eye.x, m_eye.y, m_eye.z};
	state.yaw = m_lookYaw;
	state.pitch = m_lookPitch;
	state.fieldOfView = m_fov;
	state.focusDistance = m_focusDistance;
	state.orbitYaw = m_yaw;
	state.orbitPitch = m_pitch;
	state.orbitScale = m_scale;
	const double y = m_yaw * kPi / 180.0, p = m_pitch * kPi / 180.0;
	const std::array<double, 3> right {-std::sin(y), std::cos(y), 0};
	const std::array<double, 3> up {-std::sin(p) * std::cos(y), -std::sin(p) * std::sin(y), std::cos(p)};
	const std::array<double, 3> center {m_center.x, m_center.y, m_center.z};
	for (int i = 0; i < 3; ++i) {
		state.orbitTarget[i] = center[i] - right[i] * m_pan.x() / m_scale + up[i] * m_pan.y() / m_scale;
	}
	return state;
}

bool ModelViewport::restoreNavigationState(const CameraViewState& state)
{
	if (!validateCameraViewState(state)) { return false; }
	setLooking(false);
	cancelMove();
	m_heldKeys.clear();
	m_flyTimer->stop();
	m_orbiting = m_panning = m_dragStarted = false;
	m_dragAction = DragAction::None;
	m_pressButton = Qt::NoButton;
	m_perspective = state.perspective;
	m_controls.perspective = state.perspective;
	m_controls.fieldOfViewDegrees = state.fieldOfView;
	m_eye = makeVec(state.position[0], state.position[1], state.position[2]);
	m_lookYaw = state.yaw;
	m_lookPitch = state.pitch;
	m_fov = state.fieldOfView;
	m_focusDistance = state.focusDistance;
	m_perspectivePlaced = true;
	m_yaw = state.orbitYaw;
	m_pitch = state.orbitPitch;
	m_scale = state.orbitScale;
	const double y = m_yaw * kPi / 180.0, p = m_pitch * kPi / 180.0;
	const std::array<double, 3> right {-std::sin(y), std::cos(y), 0};
	const std::array<double, 3> up {-std::sin(p) * std::cos(y), -std::sin(p) * std::sin(y), std::cos(p)};
	const std::array<double, 3> center {m_center.x, m_center.y, m_center.z};
	double x = 0, z = 0;
	for (int i = 0; i < 3; ++i) {
		x += (center[i] - state.orbitTarget[i]) * right[i];
		z += (state.orbitTarget[i] - center[i]) * up[i];
	}
	m_pan = QPointF(x * m_scale, z * m_scale);
	rebuildKeyMotions();
	invalidateProjection();
	announceView();
	return true;
}

void ModelViewport::setCameraView(const ModelVec3& position, double yawDegrees, double pitchDegrees)
{
	if (!vecIsFinite(position) || !std::isfinite(yawDegrees) || !std::isfinite(pitchDegrees)) {
		return;
	}
	m_eye = position;
	m_lookYaw = wrapDegrees(yawDegrees);
	m_lookPitch = std::clamp(pitchDegrees, kMinPitch, kMaxPitch);
	m_perspectivePlaced = true;
	invalidateProjection();
	announceView();
}

void ModelViewport::aimCameraAt(const ModelVec3& point, bool keepPitch)
{
	const ModelVec3 toward = makeVec(static_cast<double>(point.x) - m_eye.x, static_cast<double>(point.y) - m_eye.y,
		static_cast<double>(point.z) - m_eye.z);
	const double length = lengthVec(toward);
	if (!std::isfinite(length) || length < 1e-3) {
		return;
	}
	const double yaw = std::atan2(static_cast<double>(toward.y), static_cast<double>(toward.x)) * 180.0 / kPi;
	const double pitch = keepPitch ? m_lookPitch : std::asin(std::clamp(toward.z / length, -1.0, 1.0)) * 180.0 / kPi;
	m_focusDistance = length;
	setCameraView(m_eye, yaw, pitch);
}

void ModelViewport::moveCamera(double forward, double right, double up, bool groundPlane)
{
	if (!m_perspective || !std::isfinite(forward) || !std::isfinite(right) || !std::isfinite(up)) {
		return;
	}
	const ModelVec3 ahead = groundPlane ? makeVec(std::cos(m_lookYaw * kPi / 180.0), std::sin(m_lookYaw * kPi / 180.0), 0.0) : viewDirection();
	const ModelVec3 side = normalizeVec(crossVec(ahead, makeVec(0.0, 0.0, 1.0)), makeVec(0.0, -1.0, 0.0));
	// Up is the world's, as in every Quake-family editor: flying up never
	// drifts sideways because the camera is pitched.
	m_eye = makeVec(m_eye.x + ahead.x * forward + side.x * right, m_eye.y + ahead.y * forward + side.y * right,
		m_eye.z + ahead.z * forward + side.z * right + up);
	m_perspectivePlaced = true;
	invalidateProjection();
	announceView();
}

void ModelViewport::turnCamera(double yawDegrees, double pitchDegrees)
{
	if (!m_perspective) {
		applyOrbitDelta(yawDegrees, pitchDegrees);
		return;
	}
	m_lookYaw = wrapDegrees(m_lookYaw + yawDegrees);
	m_lookPitch = std::clamp(m_lookPitch + pitchDegrees, kMinPitch, kMaxPitch);
	m_perspectivePlaced = true;
	invalidateProjection();
	announceView();
}

void ModelViewport::setLooking(bool looking)
{
	if (!looking) { endPointerDrive(); }
	// An edit keeps ownership through release; toggling navigation cannot
	// swallow a paint/sample release or strand a transform preview.
	if (looking && (m_pointerDriving || m_surfaceToolPressed || m_surfaceStrokeActive || m_editMoveActive || m_pressButton != Qt::NoButton)) { return; }
	if (!looking) { m_lookHoldActive = false; m_lookBeforeHold = false; }
	if (!m_perspective) {
		looking = false;
	}
	if (m_looking == looking) {
		return;
	}
	m_looking = looking;
	finishBrushDraw(false);
	finishSelectionResize(false);
	if (looking) {
		setFocus(Qt::OtherFocusReason);
		setCursor(Qt::BlankCursor);
		m_warpPointer = true;
		recentreLookCursor(QPointF(width() * 0.5, height() * 0.5));
	} else {
		unsetCursor();
	}
	// Radiant's fly keys only fly while looking.
	rebuildKeyMotions();
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT lookingChanged(looking);
}

void ModelViewport::setTemporaryLooking(bool looking)
{
	const bool wasHeld = m_lookHoldActive;
	if (looking) {
		if (m_lookHoldActive || m_pointerDriving || !m_perspective || m_controls.lookHoldKey.isEmpty()
			|| m_pressButton != Qt::NoButton || m_editMoveActive || m_surfaceToolPressed || m_surfaceStrokeActive) { return; }
		const bool previous = m_looking;
		setLooking(true);
		m_lookBeforeHold = previous;
		m_lookHoldActive = true;
	} else if (m_lookHoldActive) {
		const bool previous = m_lookBeforeHold;
		m_lookHoldActive = false;
		m_lookBeforeHold = false;
		setLooking(previous);
	}
	if (wasHeld != m_lookHoldActive) {
		setAccessibleDescription(accessibleSummary()); update();
		QAccessibleEvent change(this, QAccessible::DescriptionChanged); QAccessible::updateAccessibility(&change);
	}
}

bool ModelViewport::isTemporarilyLooking() const { return m_lookHoldActive; }

bool ModelViewport::isLooking() const
{
	return m_looking;
}

void ModelViewport::toView(const ModelVec3& point, double* x, double* y, double* z) const
{
	const ModelVec3 relative = makeVec(static_cast<double>(point.x) - m_camera.position.x,
		static_cast<double>(point.y) - m_camera.position.y, static_cast<double>(point.z) - m_camera.position.z);
	*x = dotVec(relative, m_camera.right);
	*y = dotVec(relative, m_camera.up);
	*z = dotVec(relative, m_camera.forward);
}

QPointF ModelViewport::fromView(double x, double y, double z) const
{
	const double depth = std::max(z, kNearPlane);
	return QPointF(m_camera.origin.x() + m_camera.focal * x / depth, m_camera.origin.y() - m_camera.focal * y / depth);
}

bool ModelViewport::projectSegment(const ModelVec3& a, const ModelVec3& b, QPointF* screenA, QPointF* screenB) const
{
	if (!m_perspective) {
		*screenA = projectPoint(m_camera, a, nullptr);
		*screenB = projectPoint(m_camera, b, nullptr);
		return true;
	}
	double ax = 0.0;
	double ay = 0.0;
	double az = 0.0;
	double bx = 0.0;
	double by = 0.0;
	double bz = 0.0;
	toView(a, &ax, &ay, &az);
	toView(b, &bx, &by, &bz);
	if (az < kNearPlane && bz < kNearPlane) {
		return false;
	}
	// The end behind the near plane moves along the segment onto it.
	if (az < kNearPlane) {
		const double t = (kNearPlane - az) / (bz - az);
		ax += t * (bx - ax);
		ay += t * (by - ay);
		az = kNearPlane;
	} else if (bz < kNearPlane) {
		const double t = (kNearPlane - bz) / (az - bz);
		bx += t * (ax - bx);
		by += t * (ay - by);
		bz = kNearPlane;
	}
	*screenA = fromView(ax, ay, az);
	*screenB = fromView(bx, by, bz);
	return true;
}

ModelVec3 ModelViewport::viewDirection() const
{
	const double yaw = m_lookYaw * kPi / 180.0;
	const double pitch = m_lookPitch * kPi / 180.0;
	return makeVec(std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch));
}

ModelVec3 ModelViewport::pickDirection(const QPointF& viewPoint) const
{
	const Camera camera = buildCamera();
	const double x = viewPoint.x() - camera.origin.x();
	const double y = camera.origin.y() - viewPoint.y();
	return normalizeVec(makeVec(camera.forward.x * camera.focal + camera.right.x * x + camera.up.x * y,
				    camera.forward.y * camera.focal + camera.right.y * x + camera.up.y * y,
				    camera.forward.z * camera.focal + camera.right.z * x + camera.up.z * y),
		camera.forward);
}

ModelVec3 ModelViewport::orbitPivotAt(const QPointF& viewPoint) const
{
	const ModelVec3 direction = pickDirection(viewPoint);
	double distance = m_focusDistance;
	const ModelViewportHit hit = hitTest(viewPoint);
	if (hit.valid && hit.triangle >= 0 && hit.triangle < m_projected.size()) {
		// Perspective reciprocal depth is affine on the projected face. The
		// first three clipped corners define that plane even for a quad.
		const auto& corners = m_projected.at(hit.triangle).corners;
		const QPointF ab = corners[1].screen - corners[0].screen;
		const QPointF ac = corners[2].screen - corners[0].screen;
		const QPointF ap = viewPoint - corners[0].screen;
		const double determinant = ab.x() * ac.y() - ab.y() * ac.x();
		double reciprocal = 0;
		if (std::abs(determinant) > 1e-12) {
			const double b = (ap.x() * ac.y() - ap.y() * ac.x()) / determinant;
			const double c = (ab.x() * ap.y() - ab.y() * ap.x()) / determinant;
			reciprocal = (1 - b - c) * corners[0].reciprocalW + b * corners[1].reciprocalW + c * corners[2].reciprocalW;
		}
		const double viewDepth = reciprocal > 0 ? 1.0 / reciprocal : 0;
		const double along = dotVec(direction, m_camera.forward);
		if (viewDepth > kNearPlane && along > 0.05) {
			distance = viewDepth / along;
		}
	}
	return makeVec(m_eye.x + direction.x * distance, m_eye.y + direction.y * distance, m_eye.z + direction.z * distance);
}

ModelViewport::DragAction ModelViewport::dragActionFor(Qt::MouseButton button, Qt::KeyboardModifiers modifiers) const
{
	auto controls = m_controls;
	controls.perspective = m_perspective;
	switch (cameraNavigationDrag(controls, button, modifiers)) {
	case CameraNavigationDrag::Orbit: return DragAction::Orbit;
	case CameraNavigationDrag::Look: return DragAction::Look;
	case CameraNavigationDrag::Pan: return DragAction::Pan;
	case CameraNavigationDrag::Drive: return DragAction::Drive;
	case CameraNavigationDrag::None: break;
	}
	return DragAction::None;
}

void ModelViewport::applyDrag(DragAction action, const QPointF& delta)
{
	switch (action) {
	case DragAction::Orbit:
		// Dragging right turns the scene to the right, which means the
		// camera's azimuth runs the other way.
		if (m_perspective) {
			orbitPerspective(-delta.x() * kOrbitDegreesPerPixel, -delta.y() * kOrbitDegreesPerPixel);
		} else {
			applyOrbitDelta(-delta.x() * kOrbitDegreesPerPixel, -delta.y() * kOrbitDegreesPerPixel);
		}
		break;
	case DragAction::Look:
		turnCamera(-delta.x() * kLookDegreesPerPixel, -delta.y() * kLookDegreesPerPixel);
		break;
	case DragAction::Pan:
		if (m_perspective) {
			panPerspective(delta);
		} else {
			applyPanDelta(delta);
		}
		break;
	case DragAction::Drive:
	case DragAction::Move:
		// Selection movement uses the absolute pointer position in updateMove().
		break;
	case DragAction::None:
		break;
	}
}

void ModelViewport::orbitPerspective(double yawDelta, double pitchDelta)
{
	// The camera and its view turn together about the pivot, so what is
	// under the pointer stays under it: a turn about the world's Z, then a
	// tilt about the camera's own right-hand axis.
	ModelVec3 offset = makeVec(static_cast<double>(m_eye.x) - m_pivot.x, static_cast<double>(m_eye.y) - m_pivot.y,
		static_cast<double>(m_eye.z) - m_pivot.z);
	ModelVec3 ahead = viewDirection();
	const ModelVec3 up = makeVec(0.0, 0.0, 1.0);
	offset = rotateAbout(offset, up, yawDelta);
	ahead = rotateAbout(ahead, up, yawDelta);
	const ModelVec3 side = normalizeVec(crossVec(ahead, up), makeVec(0.0, -1.0, 0.0));
	const ModelVec3 tiltedAhead = rotateAbout(ahead, side, -pitchDelta);
	const double tiltedPitch = std::asin(std::clamp(static_cast<double>(tiltedAhead.z), -1.0, 1.0)) * 180.0 / kPi;
	// Past straight up or down the view would flip; the tilt stops there.
	if (tiltedPitch > kMinPitch && tiltedPitch < kMaxPitch) {
		offset = rotateAbout(offset, side, -pitchDelta);
		ahead = tiltedAhead;
	}
	m_eye = makeVec(m_pivot.x + offset.x, m_pivot.y + offset.y, m_pivot.z + offset.z);
	m_lookYaw = wrapDegrees(std::atan2(static_cast<double>(ahead.y), static_cast<double>(ahead.x)) * 180.0 / kPi);
	m_lookPitch = std::clamp(std::asin(std::clamp(static_cast<double>(ahead.z), -1.0, 1.0)) * 180.0 / kPi, kMinPitch, kMaxPitch);
	m_focusDistance = std::max(lengthVec(offset), kNearPlane * 8.0);
	m_perspectivePlaced = true;
	invalidateProjection();
	announceView();
}

void ModelViewport::panPerspective(const QPointF& delta)
{
	// The scene under the pointer keeps pace with it at the focus distance.
	const double unitsPerPixel = 2.0 * std::tan(m_fov * kPi / 360.0) * m_focusDistance / std::max(1, height());
	const ModelVec3 ahead = viewDirection();
	const ModelVec3 side = normalizeVec(crossVec(ahead, makeVec(0.0, 0.0, 1.0)), makeVec(0.0, -1.0, 0.0));
	const ModelVec3 up = normalizeVec(crossVec(side, ahead), makeVec(0.0, 0.0, 1.0));
	const double dx = -delta.x() * unitsPerPixel;
	const double dy = delta.y() * unitsPerPixel;
	m_eye = makeVec(m_eye.x + side.x * dx + up.x * dy, m_eye.y + side.y * dx + up.y * dy, m_eye.z + side.z * dx + up.z * dy);
	m_perspectivePlaced = true;
	invalidateProjection();
	announceView();
}

void ModelViewport::dolly(double notches, const QPointF& anchor)
{
	if (!m_perspective) {
		applyZoomFactor(std::pow(1.2, notches), anchor);
		return;
	}
	const double step = std::clamp(m_radius * 0.06, 16.0, 512.0) * notches;
	const ModelVec3 direction = pickDirection(anchor);
	m_eye = makeVec(m_eye.x + direction.x * step, m_eye.y + direction.y * step, m_eye.z + direction.z * step);
	m_focusDistance = std::max(16.0, m_focusDistance - step);
	m_perspectivePlaced = true;
	invalidateProjection();
	announceView();
}

void ModelViewport::pickAt(const QPointF& viewPoint, Qt::KeyboardModifiers modifiers)
{
	// Keep selection unchanged while the image still represents an older view.
	ensureProjection();
	if (isRendering()) { return; }
	const Qt::KeyboardModifiers keys = modifiers & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
	const CameraViewControls& c = m_controls;
	ModelViewportPick pick = ModelViewportPick::Replace;
	if (keys == Qt::NoModifier) {
		if (!c.plainClickSelects) {
			// GtkRadiant's camera selects only with Shift.
			return;
		}
		pick = ModelViewportPick::Replace;
	} else if (c.toggleModifiers != Qt::NoModifier && keys == c.toggleModifiers) {
		pick = ModelViewportPick::Toggle;
	} else if (c.faceModifiers != Qt::NoModifier && keys == c.faceModifiers) {
		pick = ModelViewportPick::Face;
	} else if (c.toggleModifiers != Qt::NoModifier && c.faceModifiers != Qt::NoModifier && keys == (c.toggleModifiers | c.faceModifiers)) {
		pick = ModelViewportPick::FaceToggle;
	} else {
		// Keys that mean nothing to a click here, such as the orbit view's
		// Shift, which pans.
		return;
	}
	ensureProjection();
	if (m_collisionPicking) {
		Q_EMIT collisionPicked(collisionAt(viewPoint), static_cast<int>(pick));
		return;
	}
	if (m_tagPicking) {
		Q_EMIT tagPicked(tagAt(viewPoint), static_cast<int>(pick));
		return;
	}
	if (m_vertexPicking) {
		const auto vertex = vertexAt(viewPoint);
		Q_EMIT vertexPicked(vertex.valid ? vertex.surface : -1, vertex.vertex, static_cast<int>(pick));
		return;
	}
	if (m_edgePicking) {
		const auto edge = edgeAt(viewPoint);
		Q_EMIT edgePicked(edge.valid ? edge.surface : -1, edge.a, edge.b, static_cast<int>(pick));
		return;
	}
	const ModelViewportHit hit = hitTest(viewPoint);
	if (hit.valid && pick == ModelViewportPick::Replace) {
		Q_EMIT triangleClicked(hit.surface, hit.triangle);
	}
	Q_EMIT trianglePicked(hit.valid ? hit.surface : -1, hit.valid ? hit.triangle : -1, static_cast<int>(pick));
}

void ModelViewport::rebuildKeyMotions()
{
	auto controls = m_controls;
	controls.perspective = m_perspective;
	m_keyMotions = cameraKeyMotions(controls, m_looking, m_pressButton, m_pressModifiers);
	m_motionModifiers = Qt::NoModifier;
	for (auto it = m_heldKeys.begin(); it != m_heldKeys.end();) {
		it = m_keyMotions.contains(*it) && !cameraKeyUsesFixedStep(controls, *it, m_looking, m_pressButton, m_pressModifiers)
			? std::next(it) : m_heldKeys.erase(it);
	}
	if (m_heldKeys.isEmpty()) { m_flyTimer->stop(); }
}

bool ModelViewport::claimsLookToggle(int key, Qt::KeyboardModifiers modifiers) const
{
	if (!m_perspective || m_controls.lookToggleKey.isEmpty()) { return false; }
	return cameraLookToggleMatches(m_controls, key, modifiers);
}

bool ModelViewport::claimsKey(int key, Qt::KeyboardModifiers modifiers) const
{
	return m_perspective && cameraKeyMotions(m_controls, m_looking, m_pressButton, m_pressModifiers, modifiers).contains(key);
}

double ModelViewport::flySpeed() const
{
	// Units per second: about a Quake player's run for a room-sized scene,
	// quicker across a large one.
	return std::clamp(m_radius * 0.35, 192.0, 1600.0);
}

void ModelViewport::flyStep()
{
	const double seconds = std::clamp(static_cast<double>(m_flyClock.restart()) / 1000.0, 0.0, 0.1);
	if (m_heldKeys.isEmpty()) {
		m_flyTimer->stop();
		return;
	}
	KeyMotion total;
	const Qt::KeyboardModifiers modifiers = QGuiApplication::keyboardModifiers();
	if (modifiers != m_motionModifiers) {
		m_keyMotions = cameraKeyMotions(m_controls, m_looking, m_pressButton, m_pressModifiers, modifiers);
		m_motionModifiers = modifiers;
	}
	for (const int key : std::as_const(m_heldKeys)) {
		const auto motion = m_keyMotions.constFind(key);
		if (motion == m_keyMotions.constEnd()) {
			continue;
		}
		total.forward += motion->forward;
		total.right += motion->right;
		total.up += motion->up;
		total.turn += motion->turn;
		total.pitch += motion->pitch;
	}
	double speed = flySpeed();
	const Qt::KeyboardModifiers fast = m_controls.fastModifiers;
	const Qt::KeyboardModifiers slow = m_controls.slowModifiers;
	if (fast != Qt::NoModifier && (modifiers & fast) == fast) {
		speed *= 3.0;
	} else if (slow != Qt::NoModifier && (modifiers & slow) == slow) {
		speed *= 0.3;
	}
	if (total.turn != 0.0 || total.pitch != 0.0) {
		turnCamera(total.turn * kTurnDegreesPerSecond * seconds, total.pitch * kTurnDegreesPerSecond * seconds);
	}
	if (total.forward != 0.0 || total.right != 0.0 || total.up != 0.0) {
		moveCamera(total.forward * speed * seconds, total.right * speed * seconds, total.up * speed * seconds);
	}
}

void ModelViewport::frameModelPerspective()
{
	// Far enough back that the scene's bounding sphere fills the height.
	const double half = std::clamp(m_fov, kMinFieldOfView, kMaxFieldOfView) * kPi / 360.0;
	const double distance = std::max(m_radius / std::sin(half) * 1.05, kNearPlane * 16.0);
	const ModelVec3 ahead = viewDirection();
	m_eye = makeVec(m_center.x - ahead.x * distance, m_center.y - ahead.y * distance, m_center.z - ahead.z * distance);
	m_focusDistance = distance;
	m_perspectivePlaced = true;
	invalidateProjection();
	announceView();
}

void ModelViewport::recentreLookCursor(const QPointF& position)
{
	// Offscreen rendering/semantic tests have no user pointer to capture.
	if (QGuiApplication::platformName() == QLatin1String("offscreen") || QGuiApplication::platformName() == QLatin1String("minimal")) {
		m_warpPointer = false; m_lookAnchor = position; return;
	}
	// The pointer goes back to the middle after each move, so looking never
	// runs into the edge of the screen. Where the platform will not move the
	// pointer (Wayland), the look runs on from where it is.
	const QPoint centre(width() / 2, height() / 2);
	if (m_warpPointer) {
		QCursor::setPos(mapToGlobal(centre));
		if (mapFromGlobal(QCursor::pos()) == centre) {
			m_lookAnchor = centre;
			return;
		}
		m_warpPointer = false;
	}
	m_lookAnchor = position;
}

void ModelViewport::setMoveGrid(double units)
{
	const double next = std::isfinite(units) && units > 0.0 ? units : 0.0;
	if (next != m_moveGrid) { finishBrushDraw(false); finishSelectionResize(false); }
	m_moveGrid = next;
}

bool ModelViewport::isMovingSelection() const
{
	return m_dragStarted && m_dragAction == DragAction::Move;
}

bool ModelViewport::movePlanePoint(const QPointF& viewPoint, ModelVec3* point) const
{
	const ModelVec3 direction = pickDirection(viewPoint);
	ModelVec3 normal = makeVec(0.0, 0.0, 1.0);
	if (m_moveVertical) {
		// Upright and square to the view, so a drag up the screen lifts.
		const ModelVec3 ahead = viewDirection();
		normal = normalizeVec(makeVec(ahead.x, ahead.y, 0.0), makeVec(1.0, 0.0, 0.0));
	}
	const double facing = dotVec(direction, normal);
	if (std::abs(facing) < 1e-4) {
		return false;
	}
	const ModelVec3 toStart = makeVec(static_cast<double>(m_moveStart.x) - m_eye.x, static_cast<double>(m_moveStart.y) - m_eye.y,
		static_cast<double>(m_moveStart.z) - m_eye.z);
	const double along = dotVec(toStart, normal) / facing;
	// Behind the camera, or so far off along a grazing ray that one pixel
	// would throw the selection across the map.
	if (along <= 0.0 || along > std::max(m_focusDistance, 1.0) * 64.0) {
		return false;
	}
	*point = makeVec(m_eye.x + direction.x * along, m_eye.y + direction.y * along, m_eye.z + direction.z * along);
	return true;
}

void ModelViewport::updateMove(const QPointF& viewPoint)
{
	ModelVec3 point;
	if (!movePlanePoint(viewPoint, &point)) {
		return;
	}
	double dx = static_cast<double>(point.x) - m_moveStart.x;
	double dy = static_cast<double>(point.y) - m_moveStart.y;
	double dz = static_cast<double>(point.z) - m_moveStart.z;
	if (m_moveVertical) {
		dx = 0.0;
		dy = 0.0;
	} else {
		dz = 0.0;
	}
	if (m_moveGrid > 0.0) {
		dx = std::round(dx / m_moveGrid) * m_moveGrid;
		dy = std::round(dy / m_moveGrid) * m_moveGrid;
		dz = std::round(dz / m_moveGrid) * m_moveGrid;
	}
	const ModelVec3 offset = makeVec(dx, dy, dz);
	if (offset.x == m_moveOffset.x && offset.y == m_moveOffset.y && offset.z == m_moveOffset.z) {
		return;
	}
	m_moveOffset = offset;
	invalidateProjection();
	update();
	Q_EMIT hoverChanged(tr("Moving the selection by %1, %2, %3").arg(dx, 0, 'g', 8).arg(dy, 0, 'g', 8).arg(dz, 0, 'g', 8));
}

void ModelViewport::cancelMove()
{
	m_moveOffset = makeVec(0.0, 0.0, 0.0);
	m_dragStarted = false;
	m_dragAction = DragAction::None;
	if (!m_looking) {
		unsetCursor();
	}
	invalidateProjection();
	update();
}

void ModelViewport::keyReleaseEvent(QKeyEvent* event)
{
	if (m_pointerDriving && cameraNavigationDrag(m_controls, m_controls.driveButton, event->modifiers()) != CameraNavigationDrag::Drive) { endPointerDrive(); }
	if (isDrawingBrush()) { updateBrushDraw(m_brushDrawPoint,event->modifiers()); event->accept(); return; }
	if (!event->isAutoRepeat() && m_lookHoldActive && navigationHoldKeyMatches(m_controls.lookHoldKey, event->key(), Qt::NoModifier)) {
		setTemporaryLooking(false); event->accept(); return;
	}
	if (!event->isAutoRepeat() && m_heldKeys.remove(event->key())) {
		if (m_heldKeys.isEmpty()) {
			m_flyTimer->stop();
		}
		event->accept();
		return;
	}
	QWidget::keyReleaseEvent(event);
}

void ModelViewport::focusOutEvent(QFocusEvent* event)
{
	finishMaterialStroke(false);
	m_materialGestureButtons = {};
	endPointerDrive();
	finishSurfaceStroke(false);
	finishEditTransform(false);
	// A key let go elsewhere never arrives here, so nothing stays held.
	m_heldKeys.clear();
	m_flyTimer->stop();
	m_pressButton = Qt::NoButton;
	cancelMove();
	m_orbiting = false;
	m_panning = false;
	if (m_looking) {
		setLooking(false);
	}
	rebuildKeyMotions();
	QWidget::focusOutEvent(event);
}

bool ModelViewport::event(QEvent* event)
{
	if (event->type() == QEvent::ShortcutOverride && m_materialStrokeRaster) { event->accept(); return true; }
	if (event->type() == QEvent::ShortcutOverride && m_pointerDriving && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
		event->accept(); return true;
	}
	if (event->type() == QEvent::WindowDeactivate || event->type() == QEvent::UngrabMouse
		|| event->type() == QEvent::Hide || (event->type() == QEvent::EnabledChange && !isEnabled())) {
		m_materialGestureButtons = {};
		if (event->type() != QEvent::UngrabMouse || materialStrokeActive()) { finishMaterialStroke(false); }
		endPointerDrive();
	}
	if (event->type() == QEvent::ShortcutOverride && m_brushDrawTool && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
		event->accept(); return true;
	}
	if (event->type() == QEvent::PaletteChange && m_vertexPicking) { invalidateVertexOverlay(); update(); }
	if (event->type() == QEvent::Hide || (event->type() == QEvent::EnabledChange && !isEnabled())) { finishSurfaceStroke(false); }
	if (event->type() == QEvent::Hide || (event->type() == QEvent::EnabledChange && !isEnabled())) {
		m_heldKeys.clear();
		m_flyTimer->stop();
		m_pressButton = Qt::NoButton;
		cancelMove();
		m_orbiting = false;
		m_panning = false;
		setLooking(false);
		rebuildKeyMotions();
	}
	if (event->type() == QEvent::ShortcutOverride && m_surfaceTool != ModelViewportSurfaceTool::None && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
		event->accept(); return true;
	}
	if (event->type() == QEvent::ShortcutOverride && m_editMoveActive && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
		event->accept(); return true;
	}
	if (event->type() == QEvent::ShortcutOverride && isResizingSelection() && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
		event->accept(); return true;
	}
	// The camera's held keys belong to the view while it has focus: W flies
	// here even where it is a command's key elsewhere.
	if (event->type() == QEvent::ShortcutOverride && m_perspective) {
		const auto* key = static_cast<QKeyEvent*>(event);
		if (claimsKey(key->key(), key->modifiers()) || claimsLookToggle(key->key(), key->modifiers())
			|| navigationHoldKeyMatches(m_controls.lookHoldKey, key->key(), key->modifiers()) || (m_looking && key->key() == Qt::Key_Escape)) {
			event->accept();
			return true;
		}
	}
	return QWidget::event(event);
}

QString modelViewportRenderModeDisplayName(ModelViewportRenderMode mode)
{
	switch (mode) {
	case ModelViewportRenderMode::Wireframe:
		return QCoreApplication::translate("VibeStudioModelViewport", "Wireframe");
	case ModelViewportRenderMode::FlatShaded:
		return QCoreApplication::translate("VibeStudioModelViewport", "Flat shaded");
	case ModelViewportRenderMode::Textured:
		return QCoreApplication::translate("VibeStudioModelViewport", "Textured");
	}
	return QCoreApplication::translate("VibeStudioModelViewport", "Flat shaded");
}

} // namespace vibestudio
