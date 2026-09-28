#include "app/model_viewport.h"

#include <QBrush>
#include <QCoreApplication>
#include <QFont>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QResizeEvent>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace vibestudio {

namespace {

// Free helpers in this file cannot use tr(); ModelViewport members can, because
// the class carries Q_OBJECT.
QString viewText(const char* source)
{
	return QCoreApplication::translate("VibeStudioModelViewport", source);
}

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
constexpr int kMinFps = 1;
constexpr int kMaxFps = 60;

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
	m_timer->setTimerType(Qt::CoarseTimer);
	m_timer->setInterval(1000 / m_fps);
	connect(m_timer, &QTimer::timeout, this, &ModelViewport::advanceFrame);

	rebuildFillBrushes();
}

ModelViewport::~ModelViewport() = default;

void ModelViewport::setMesh(const ModelMesh& mesh)
{
	m_mesh = mesh;
	m_hasMesh = true;
	m_playing = false;
	m_animation.clear();
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
	m_rangeFirst = 0;
	m_rangeCount = m_frameTotal;

	const ModelVec3 mins = m_mesh.mins;
	const ModelVec3 maxs = m_mesh.maxs;
	if (vecIsFinite(mins) && vecIsFinite(maxs) && maxs.x >= mins.x && maxs.y >= mins.y && maxs.z >= mins.z) {
		m_center = makeVec((static_cast<double>(mins.x) + maxs.x) * 0.5,
			(static_cast<double>(mins.y) + maxs.y) * 0.5, (static_cast<double>(mins.z) + maxs.z) * 0.5);
	} else {
		m_center = makeVec(0.0, 0.0, 0.0);
	}
	double radius = static_cast<double>(m_mesh.boundingRadius());
	if (!std::isfinite(radius) || radius <= 0.0) {
		radius = 0.5
			* std::sqrt(std::pow(static_cast<double>(maxs.x) - mins.x, 2.0)
				+ std::pow(static_cast<double>(maxs.y) - mins.y, 2.0)
				+ std::pow(static_cast<double>(maxs.z) - mins.z, 2.0));
	}
	m_radius = (std::isfinite(radius) && radius > 1e-3) ? radius : 64.0;

	rebuildMeshTriangles();
	rebuildTextureTriangles();
	rebuildFillBrushes();
	updatePlaybackTimer();
	frameModel();
	setAccessibleDescription(accessibleSummary());
	Q_EMIT frameChanged(m_frame);
	Q_EMIT animationChanged(m_animation);
	Q_EMIT playbackChanged(m_playing);
	update();
}

void ModelViewport::clearMesh()
{
	m_mesh = ModelMesh();
	m_hasMesh = false;
	m_meshTriangles.clear();
	m_textureTriangles.clear();
	m_projected.clear();
	m_order.clear();
	m_frame = 0;
	m_frameTotal = 0;
	m_rangeFirst = 0;
	m_rangeCount = 0;
	m_animation.clear();
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
	if (skin.isNull()) {
		clearSkin();
		return;
	}
	// One conversion here keeps every fill on the fast blend path.
	m_skin = skin.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	m_skinBrush = QBrush(m_skin);
	rebuildTextureTriangles();
	invalidateProjection();
	setAccessibleDescription(accessibleSummary());
	update();
}

void ModelViewport::clearSkin()
{
	if (m_skin.isNull()) {
		return;
	}
	m_skin = QImage();
	m_skinBrush = QBrush();
	rebuildTextureTriangles();
	invalidateProjection();
	setAccessibleDescription(accessibleSummary());
	update();
}

bool ModelViewport::hasSkin() const
{
	return !m_skin.isNull();
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
		m_playing = false;
		Q_EMIT playbackChanged(m_playing);
	}
	updatePlaybackTimer();
	setAccessibleDescription(accessibleSummary());
	update();
}

bool ModelViewport::reducedMotion() const
{
	return m_reducedMotion;
}

void ModelViewport::setFrame(int frame)
{
	if (m_frameTotal <= 0) {
		return;
	}
	const int clamped = std::clamp(frame, 0, m_frameTotal - 1);
	if (clamped == m_frame) {
		return;
	}
	m_frame = clamped;
	// Jumping outside the selected animation drops back to the whole model, so
	// the reported animation never contradicts the reported frame.
	if (!m_animation.isEmpty() && (m_frame < m_rangeFirst || m_frame >= m_rangeFirst + m_rangeCount)) {
		m_animation.clear();
		m_rangeFirst = 0;
		m_rangeCount = m_frameTotal;
		Q_EMIT animationChanged(m_animation);
	}
	invalidateProjection();
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT frameChanged(m_frame);
}

int ModelViewport::frame() const
{
	return m_frame;
}

void ModelViewport::stepFrame(int delta)
{
	if (m_frameTotal <= 0 || m_rangeCount <= 0) {
		return;
	}
	const int offset = ((m_frame - m_rangeFirst + delta) % m_rangeCount + m_rangeCount) % m_rangeCount;
	const int target = m_rangeFirst + offset;
	if (target == m_frame) {
		return;
	}
	m_frame = target;
	invalidateProjection();
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT frameChanged(m_frame);
}

int ModelViewport::frameCount() const
{
	return m_frameTotal;
}

bool ModelViewport::setAnimation(const QString& name)
{
	if (name.isEmpty()) {
		m_animation.clear();
		m_rangeFirst = 0;
		m_rangeCount = m_frameTotal;
		clampFrameToRange();
		Q_EMIT animationChanged(m_animation);
		return true;
	}
	for (const ModelAnimation& animation : m_mesh.animations) {
		if (animation.name != name) {
			continue;
		}
		const int first = std::clamp(animation.firstFrame, 0, std::max(m_frameTotal - 1, 0));
		const int count = std::clamp(animation.frameCount, 1, std::max(m_frameTotal - first, 1));
		m_animation = name;
		m_rangeFirst = first;
		m_rangeCount = count;
		m_frame = first;
		invalidateProjection();
		setAccessibleDescription(accessibleSummary());
		update();
		Q_EMIT animationChanged(m_animation);
		Q_EMIT frameChanged(m_frame);
		return true;
	}
	return false;
}

QString ModelViewport::animation() const
{
	return m_animation;
}

QStringList ModelViewport::animationNames() const
{
	QStringList names;
	names.reserve(static_cast<int>(m_mesh.animations.size()));
	for (const ModelAnimation& animation : m_mesh.animations) {
		names << animation.name;
	}
	return names;
}

void ModelViewport::play()
{
	if (m_playing) {
		return;
	}
	if (m_reducedMotion || m_rangeCount <= 1) {
		// Nothing to animate, or the user asked for no motion: stay paused and
		// let statusLines() explain why.
		return;
	}
	m_playing = true;
	updatePlaybackTimer();
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT playbackChanged(m_playing);
}

void ModelViewport::pause()
{
	if (!m_playing) {
		return;
	}
	m_playing = false;
	updatePlaybackTimer();
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT playbackChanged(m_playing);
}

void ModelViewport::togglePlayback()
{
	if (m_playing) {
		pause();
	} else {
		play();
	}
}

bool ModelViewport::isPlaying() const
{
	return m_playing;
}

void ModelViewport::setFramesPerSecond(int fps)
{
	const int clamped = std::clamp(fps, kMinFps, kMaxFps);
	if (clamped == m_fps) {
		return;
	}
	m_fps = clamped;
	updatePlaybackTimer();
	setAccessibleDescription(accessibleSummary());
	update();
}

int ModelViewport::framesPerSecond() const
{
	return m_fps;
}

void ModelViewport::resetView()
{
	m_yaw = 30.0;
	m_pitch = 20.0;
	m_pan = QPointF();
	m_orbiting = false;
	m_panning = false;
	unsetCursor();
	frameModel();
}

void ModelViewport::frameModel()
{
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

void ModelViewport::setOrbit(double yawDegrees, double pitchDegrees)
{
	const double yaw = wrapDegrees(yawDegrees);
	const double pitch = std::isfinite(pitchDegrees) ? std::clamp(pitchDegrees, kMinPitch, kMaxPitch) : 0.0;
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
	applyZoomFactor(1.25, QPointF(width() * 0.5, height() * 0.5));
}

void ModelViewport::zoomOut()
{
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
	QStringList parts;
	const QString frameName = (m_frame >= 0 && m_frame < m_mesh.frames.size()) ? m_mesh.frames.at(m_frame).name : QString();
	parts << (frameName.isEmpty()
			? tr("Frame %1 of %2").arg(m_frame + 1).arg(m_frameTotal)
			: tr("Frame %1 of %2 (%3)").arg(m_frame + 1).arg(m_frameTotal).arg(frameName));
	parts << (m_animation.isEmpty() ? tr("All frames") : tr("Animation %1").arg(m_animation));
	if (m_reducedMotion) {
		parts << tr("Reduced motion: step with Page Up and Page Down");
	} else if (m_rangeCount > 1) {
		parts << (m_playing ? tr("Playing at %1 fps").arg(m_fps) : tr("Paused"));
	}
	parts << (m_skin.isNull() ? tr("No skin") : tr("Skin %1 x %2").arg(m_skin.width()).arg(m_skin.height()));
	return parts.join(QStringLiteral("  %1  ").arg(QChar(0x00b7)));
}

QString ModelViewport::effectiveRenderModeName() const
{
	// Textured mode falls back to flat shading without a skin; say so rather
	// than name a mode the viewport is not drawing.
	if (m_renderMode == ModelViewportRenderMode::Textured && m_skin.isNull()) {
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
	lines << (m_animation.isEmpty()
			? tr("Animation: all frames")
			: tr("Animation: %1 (frames %2-%3)")
				  .arg(m_animation)
				  .arg(m_rangeFirst + 1)
				  .arg(m_rangeFirst + m_rangeCount));
	if (m_reducedMotion) {
		lines << tr("Playback: disabled by reduced motion; step frames with Page Up and Page Down");
	} else if (m_playing) {
		lines << tr("Playback: playing at %1 fps").arg(m_fps);
	} else {
		lines << tr("Playback: paused (%1 fps)").arg(m_fps);
	}
	lines << (m_skin.isNull() ? tr("Skin: none loaded")
				  : tr("Skin: %1 x %2").arg(m_skin.width()).arg(m_skin.height()));
	lines << tr("View: yaw %1 degrees, pitch %2 degrees, %3 pixels per unit")
			 .arg(m_yaw, 0, 'f', 0)
			 .arg(m_pitch, 0, 'f', 0)
			 .arg(m_scale, 0, 'f', 2);
	lines << (m_backfaceCulling ? tr("Backfaces: culled") : tr("Backfaces: drawn (single-sided model)"));
	return lines;
}

QString ModelViewport::accessibleSummary() const
{
	if (!m_hasMesh) {
		return tr("Model viewport: no model is loaded, so there is nothing to draw.");
	}
	const QString formatName = modelMeshFormatDisplayName(m_mesh.format);
	if (!m_mesh.geometryAvailable || m_meshTriangles.isEmpty()) {
		return tr("Model viewport: a %1 header is loaded, but its geometry is not decoded, so nothing is drawn.")
			.arg(formatName);
	}
	const QString animation = m_animation.isEmpty() ? tr("all frames") : m_animation;
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
	m_projected.resize(m_meshTriangles.size());
	m_order.reserve(m_meshTriangles.size());
	invalidateProjection();
}

void ModelViewport::rebuildTextureTriangles()
{
	m_textureTriangles.clear();
	if (m_meshTriangles.isEmpty() || m_skin.isNull()) {
		return;
	}
	const double skinWidth = m_skin.width();
	const double skinHeight = m_skin.height();
	if (skinWidth <= 0.0 || skinHeight <= 0.0) {
		return;
	}

	m_textureTriangles.resize(m_meshTriangles.size());
	for (int index = 0; index < m_meshTriangles.size(); ++index) {
		const MeshTriangle& source = m_meshTriangles.at(index);
		TextureTriangle& target = m_textureTriangles[index];
		target.invertible = false;
		if (source.surface < 0 || source.surface >= m_mesh.surfaces.size()) {
			continue;
		}
		const QVector<ModelTexCoord>& coords = m_mesh.surfaces.at(source.surface).texCoords;
		if (source.a >= coords.size() || source.b >= coords.size() || source.c >= coords.size()) {
			continue;
		}
		// Texture coordinates are normalised by the decoder. Values outside the
		// unit square are left alone: a QBrush tiles its texture, which is the
		// same wrap behaviour idTech engines get from a repeating texture.
		const ModelTexCoord& ta = coords.at(source.a);
		const ModelTexCoord& tb = coords.at(source.b);
		const ModelTexCoord& tc = coords.at(source.c);
		if (!std::isfinite(ta.u) || !std::isfinite(ta.v) || !std::isfinite(tb.u) || !std::isfinite(tb.v)
			|| !std::isfinite(tc.u) || !std::isfinite(tc.v)) {
			continue;
		}
		if (target.polygon.size() != 3) {
			target.polygon.resize(3);
		}
		target.polygon[0] = QPointF(ta.u * skinWidth, ta.v * skinHeight);
		target.polygon[1] = QPointF(tb.u * skinWidth, tb.v * skinHeight);
		target.polygon[2] = QPointF(tc.u * skinWidth, tc.v * skinHeight);

		// Solve the inverse of the texture-space edge matrix once. The affine
		// map from texture space to screen space is then Q * P^-1 for whatever
		// screen triangle the current camera produces, and the only degenerate
		// case - a triangle with zero area in texture space - is caught here.
		const double px1 = target.polygon[1].x() - target.polygon[0].x();
		const double py1 = target.polygon[1].y() - target.polygon[0].y();
		const double px2 = target.polygon[2].x() - target.polygon[0].x();
		const double py2 = target.polygon[2].y() - target.polygon[0].y();
		const double determinant = px1 * py2 - px2 * py1;
		if (!std::isfinite(determinant) || std::fabs(determinant) < 1e-9) {
			continue;
		}
		target.inverse[0] = py2 / determinant;
		target.inverse[1] = -px2 / determinant;
		target.inverse[2] = -py1 / determinant;
		target.inverse[3] = px1 / determinant;
		target.invertible = true;
	}
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
	// Textured triangles are shaded by compositing this ramp over the skin, so
	// the shape still reads even when the skin is flat-lit.
	m_shadowBrushes.resize(kShadeSteps);
	for (int step = 0; step < kShadeSteps; ++step) {
		const double t = static_cast<double>(step) / static_cast<double>(kShadeSteps - 1);
		m_shadowBrushes[step] = QBrush(QColor(0, 0, 0, static_cast<int>(std::lround((1.0 - t) * 135.0))));
	}
}

void ModelViewport::invalidateProjection()
{
	m_projectionDirty = true;
}

ModelViewport::Camera ModelViewport::buildCamera() const
{
	Camera camera;
	const double yawRadians = m_yaw * kPi / 180.0;
	const double pitchRadians = m_pitch * kPi / 180.0;
	const double cosPitch = std::cos(pitchRadians);
	// idTech is Z-up, so the orbit azimuth turns in the X/Y plane and the
	// elevation lifts along +Z. Building the basis this way is what keeps a
	// model standing upright instead of lying on its side.
	camera.eye = normalizeVec(
		makeVec(cosPitch * std::cos(yawRadians), cosPitch * std::sin(yawRadians), std::sin(pitchRadians)),
		makeVec(1.0, 0.0, 0.0));
	camera.right = normalizeVec(crossVec(makeVec(0.0, 0.0, 1.0), camera.eye), makeVec(0.0, 1.0, 0.0));
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
	const ModelVec3 relative = makeVec(static_cast<double>(point.x) - m_center.x,
		static_cast<double>(point.y) - m_center.y, static_cast<double>(point.z) - m_center.z);
	const double viewX = dotVec(relative, camera.right);
	const double viewY = dotVec(relative, camera.up);
	if (depthOut != nullptr) {
		*depthOut = dotVec(relative, camera.eye);
	}
	// Orthographic: no perspective divide, so a straight world line stays a
	// straight screen line and the affine skin mapping below is exact. Screen Y
	// grows downwards, hence the flip.
	return QPointF(camera.origin.x() + viewX * camera.scale, camera.origin.y() - viewY * camera.scale);
}

void ModelViewport::ensureProjection()
{
	const double pixelRatio = devicePixelRatioF();
	if (!qFuzzyCompare(pixelRatio + 1.0, m_cachedPixelRatio + 1.0)) {
		// Moving to a screen with a different device pixel ratio changes what
		// counts as a sharp result, so the cache key includes it.
		m_cachedPixelRatio = pixelRatio;
		m_projectionDirty = true;
	}
	if (!m_projectionDirty) {
		return;
	}
	m_projectionDirty = false;
	m_camera = buildCamera();
	m_visibleTriangles = 0;
	m_culledTriangles = 0;
	m_order.clear();
	if (m_projected.size() != m_meshTriangles.size()) {
		m_projected.resize(m_meshTriangles.size());
	}
	if (m_meshTriangles.isEmpty()) {
		return;
	}

	const bool textured = m_renderMode == ModelViewportRenderMode::Textured && !m_skin.isNull()
		&& m_textureTriangles.size() == m_meshTriangles.size();
	const int surfaceCount = std::max(1, static_cast<int>(m_mesh.surfaces.size()));
	int currentSurface = -1;
	const ModelFrameGeometry* geometry = nullptr;

	for (int index = 0; index < m_meshTriangles.size(); ++index) {
		const MeshTriangle& source = m_meshTriangles.at(index);
		ProjectedTriangle& target = m_projected[index];
		target.visible = false;
		target.textureValid = false;
		target.source = index;
		if (source.surface != currentSurface) {
			currentSurface = source.surface;
			geometry = nullptr;
			if (currentSurface >= 0 && currentSurface < m_mesh.surfaces.size()) {
				const ModelSurface& surface = m_mesh.surfaces.at(currentSurface);
				const int frameIndex = std::min(m_frame, static_cast<int>(surface.frames.size()) - 1);
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
		const ModelVec3& p0 = positions.at(source.a);
		const ModelVec3& p1 = positions.at(source.b);
		const ModelVec3& p2 = positions.at(source.c);
		if (!vecIsFinite(p0) || !vecIsFinite(p1) || !vecIsFinite(p2)) {
			continue;
		}

		ModelVec3 normal = crossVec(makeVec(static_cast<double>(p1.x) - p0.x, static_cast<double>(p1.y) - p0.y,
						 static_cast<double>(p1.z) - p0.z),
			makeVec(static_cast<double>(p2.x) - p0.x, static_cast<double>(p2.y) - p0.y,
				static_cast<double>(p2.z) - p0.z));
		const double normalLength = lengthVec(normal);
		if (!std::isfinite(normalLength) || normalLength <= 1e-12) {
			// A zero-area triangle has no facing and no shade; skipping it also
			// keeps degenerate data out of the depth sort.
			continue;
		}
		normal = makeVec(normal.x / normalLength, normal.y / normalLength, normal.z / normalLength);
		// MDL, MD2 and MD3 all wind their triangles counter-clockwise when seen
		// from outside, but decoded files in the wild are not always consistent.
		// When the format carries vertex normals, they settle the argument.
		if (geometry->normals.size() == positions.size()) {
			const ModelVec3& na = geometry->normals.at(source.a);
			const ModelVec3& nb = geometry->normals.at(source.b);
			const ModelVec3& nc = geometry->normals.at(source.c);
			const ModelVec3 average = makeVec(static_cast<double>(na.x) + nb.x + nc.x,
				static_cast<double>(na.y) + nb.y + nc.y, static_cast<double>(na.z) + nb.z + nc.z);
			if (lengthVec(average) > 1e-6 && dotVec(normal, average) < 0.0) {
				normal = makeVec(-normal.x, -normal.y, -normal.z);
			}
		}

		const double facing = dotVec(normal, m_camera.eye);
		target.frontFacing = facing > 0.0;
		if (m_backfaceCulling && !target.frontFacing) {
			++m_culledTriangles;
			continue;
		}

		double depth0 = 0.0;
		double depth1 = 0.0;
		double depth2 = 0.0;
		const QPointF s0 = projectPoint(m_camera, p0, &depth0);
		const QPointF s1 = projectPoint(m_camera, p1, &depth1);
		const QPointF s2 = projectPoint(m_camera, p2, &depth2);
		if (!std::isfinite(s0.x()) || !std::isfinite(s0.y()) || !std::isfinite(s1.x()) || !std::isfinite(s1.y())
			|| !std::isfinite(s2.x()) || !std::isfinite(s2.y())) {
			continue;
		}
		if (target.screen.size() != 3) {
			target.screen.resize(3);
		}
		target.screen[0] = s0;
		target.screen[1] = s1;
		target.screen[2] = s2;
		target.depth = (depth0 + depth1 + depth2) / 3.0;

		// Two-sided sheets are lit by the absolute value so the back of a flag
		// is not a black hole.
		double lambert = dotVec(normal, m_camera.light);
		if (!target.frontFacing) {
			lambert = -lambert;
		}
		const double shade = std::clamp(lambert, 0.0, 1.0);
		const int step = std::clamp(static_cast<int>(std::lround(shade * (kShadeSteps - 1))), 0, kShadeSteps - 1);
		const int surfaceSlot = std::clamp(source.surface, 0, surfaceCount - 1);
		target.brushIndex = std::clamp(surfaceSlot * kShadeSteps + step, 0,
			std::max(static_cast<int>(m_fillBrushes.size()) - 1, 0));
		target.shadowIndex = step;

		if (textured) {
			const TextureTriangle& texture = m_textureTriangles.at(index);
			if (texture.invertible && texture.polygon.size() == 3) {
				const double qx1 = s1.x() - s0.x();
				const double qy1 = s1.y() - s0.y();
				const double qx2 = s2.x() - s0.x();
				const double qy2 = s2.y() - s0.y();
				const double a11 = qx1 * texture.inverse[0] + qx2 * texture.inverse[2];
				const double a12 = qx1 * texture.inverse[1] + qx2 * texture.inverse[3];
				const double a21 = qy1 * texture.inverse[0] + qy2 * texture.inverse[2];
				const double a22 = qy1 * texture.inverse[1] + qy2 * texture.inverse[3];
				const double tx = s0.x() - (a11 * texture.polygon[0].x() + a12 * texture.polygon[0].y());
				const double ty = s0.y() - (a21 * texture.polygon[0].x() + a22 * texture.polygon[0].y());
				if (std::isfinite(a11) && std::isfinite(a12) && std::isfinite(a21) && std::isfinite(a22)
					&& std::isfinite(tx) && std::isfinite(ty)) {
					// QTransform maps row vectors: x' = m11*x + m21*y + dx.
					target.textureTransform = QTransform(a11, a21, a12, a22, tx, ty);
					target.textureValid = true;
				}
			}
		}

		target.visible = true;
		m_order.append(index);
		++m_visibleTriangles;
	}

	// Painter's algorithm: draw back to front by view-space depth. This is the
	// right trade here - no depth buffer, no per-pixel work, and a few thousand
	// triangles sort in microseconds - but it mis-orders triangles that
	// interpenetrate or that form a long thin cycle, which shows up as a face
	// popping in front of one it should be behind.
	std::sort(m_order.begin(), m_order.end(), [this](int left, int right) {
		return m_projected.at(left).depth < m_projected.at(right).depth;
	});
}

ModelViewportHit ModelViewport::hitTest(const QPointF& viewPoint) const
{
	ModelViewportHit hit;
	// m_order is sorted back to front, so walking it backwards finds the
	// frontmost triangle under the pointer first.
	for (int position = static_cast<int>(m_order.size()) - 1; position >= 0; --position) {
		const int index = m_order.at(position);
		if (index < 0 || index >= m_projected.size()) {
			continue;
		}
		const ProjectedTriangle& triangle = m_projected.at(index);
		if (!triangle.visible || triangle.screen.size() != 3) {
			continue;
		}
		if (!triangle.screen.boundingRect().contains(viewPoint)) {
			continue;
		}
		if (!triangle.screen.containsPoint(viewPoint, Qt::OddEvenFill)) {
			continue;
		}
		hit.valid = true;
		hit.triangle = index;
		hit.surface = m_meshTriangles.at(index).surface;
		if (hit.surface >= 0 && hit.surface < m_mesh.surfaces.size()) {
			hit.surfaceName = m_mesh.surfaces.at(hit.surface).name;
		}
		return hit;
	}
	return hit;
}

void ModelViewport::clampFrameToRange()
{
	if (m_frameTotal <= 0) {
		m_frame = 0;
		m_rangeFirst = 0;
		m_rangeCount = 0;
		return;
	}
	m_rangeFirst = std::clamp(m_rangeFirst, 0, m_frameTotal - 1);
	m_rangeCount = std::clamp(m_rangeCount, 1, m_frameTotal - m_rangeFirst);
	const int clamped = std::clamp(m_frame, m_rangeFirst, m_rangeFirst + m_rangeCount - 1);
	if (clamped != m_frame) {
		m_frame = clamped;
		invalidateProjection();
		update();
		Q_EMIT frameChanged(m_frame);
	}
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
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT viewChanged();
}

void ModelViewport::updatePlaybackTimer()
{
	if (m_timer == nullptr) {
		return;
	}
	if (m_playing && !m_reducedMotion && m_hasMesh && m_rangeCount > 1) {
		m_timer->setInterval(std::max(1000 / std::clamp(m_fps, kMinFps, kMaxFps), 8));
		if (!m_timer->isActive()) {
			m_timer->start();
		}
		return;
	}
	m_timer->stop();
}

void ModelViewport::advanceFrame()
{
	if (!m_playing || m_reducedMotion || m_rangeCount <= 1) {
		return;
	}
	stepFrame(1);
}

void ModelViewport::paintEmptyState(QPainter& painter, const Palette& palette) const
{
	QFont headingFont = painter.font();
	headingFont.setBold(true);
	painter.setFont(headingFont);
	painter.setPen(QPen(palette.text, 1.0));
	const QRectF area = QRectF(rect()).adjusted(24.0, 24.0, -24.0, -24.0);
	const QRectF headingRect(area.left(), area.center().y() - 30.0, area.width(), 24.0);
	painter.drawText(headingRect, Qt::AlignHCenter | Qt::AlignVCenter, tr("No model is loaded"));

	headingFont.setBold(false);
	painter.setFont(headingFont);
	painter.setPen(QPen(palette.subtleText, 1.0));
	const QRectF bodyRect(area.left(), area.center().y(), area.width(), 64.0);
	painter.drawText(bodyRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
		tr("Open a Quake MDL, Quake II MD2 or Quake III MD3 model to preview it here. Orbit with the left "
		   "mouse button, pan with the middle button, and zoom with the wheel."));
}

void ModelViewport::paintNoGeometryState(QPainter& painter, const Palette& palette) const
{
	QFont headingFont = painter.font();
	headingFont.setBold(true);
	painter.setFont(headingFont);
	painter.setPen(QPen(palette.text, 1.0));
	const QRectF area = QRectF(rect()).adjusted(24.0, 24.0, -24.0, -24.0);
	const QRectF headingRect(area.left(), area.center().y() - 30.0, area.width(), 24.0);
	painter.drawText(headingRect, Qt::AlignHCenter | Qt::AlignVCenter,
		tr("%1 header read, no drawable geometry").arg(modelMeshFormatDisplayName(m_mesh.format)));

	headingFont.setBold(false);
	painter.setFont(headingFont);
	painter.setPen(QPen(palette.subtleText, 1.0));
	const QRectF bodyRect(area.left(), area.center().y(), area.width(), 64.0);
	painter.drawText(bodyRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
		tr("The file parsed, but this format's meshes are not decoded yet. The model inspector still lists "
		   "its header fields, frames and skins."));
}

void ModelViewport::paintGround(QPainter& painter, const Palette& palette) const
{
	// The ground grid is drawn before the model. A painter's algorithm would
	// otherwise have to sort grid segments against triangles for very little
	// gain; instead the grid reads as a floor the model stands on.
	const double spacing = gridSpacingForScale(m_scale, m_radius);
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
		const QPointF ax = projectPoint(m_camera, makeVec(offset, -extent, 0.0), nullptr);
		const QPointF bx = projectPoint(m_camera, makeVec(offset, extent, 0.0), nullptr);
		painter.drawLine(ax, bx);
		const QPointF ay = projectPoint(m_camera, makeVec(-extent, offset, 0.0), nullptr);
		const QPointF by = projectPoint(m_camera, makeVec(extent, offset, 0.0), nullptr);
		painter.drawLine(ay, by);
	}
}

void ModelViewport::paintAxes(QPainter& painter, const Palette& palette) const
{
	const double length = std::max(m_radius, 8.0) * 1.15;
	const QPointF origin = projectPoint(m_camera, makeVec(0.0, 0.0, 0.0), nullptr);
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
		const QPointF tip = projectPoint(m_camera, axis.direction, nullptr);
		painter.setPen(QPen(axis.color, 1.8));
		painter.drawLine(origin, tip);
		painter.setFont(labelFont);
		// The axis letter is the non-colour cue: Z always points up the screen
		// for a Z-up model, and the labels say which is which regardless.
		painter.drawText(tip + QPointF(4.0, -4.0), QString::fromLatin1(axis.label));
	}
}

void ModelViewport::paintTriangles(QPainter& painter, const Palette& palette) const
{
	if (m_order.isEmpty()) {
		return;
	}
	if (m_renderMode == ModelViewportRenderMode::Wireframe) {
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(palette.wire, m_highContrast ? 1.6 : 1.0));
		for (const int index : m_order) {
			painter.drawPolygon(m_projected.at(index).screen);
		}
		return;
	}

	// Filled passes run aliased: adjacent triangles then share their edge pixels
	// exactly, where antialiased fills would leave a hairline crack along every
	// shared edge. The paint loop only reads preallocated cache entries and a
	// preallocated brush table, so it allocates nothing per triangle.
	const bool textured = m_renderMode == ModelViewportRenderMode::Textured && !m_skin.isNull();
	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
	painter.setPen(Qt::NoPen);
	for (const int index : m_order) {
		const ProjectedTriangle& triangle = m_projected.at(index);
		if (textured && triangle.textureValid) {
			// Affine texture mapping: the world transform carries texture space
			// onto screen space, so the skin brush - which lives in logical
			// coordinates - lands exactly on the triangle's texels.
			painter.setTransform(triangle.textureTransform);
			painter.setBrush(m_skinBrush);
			painter.drawPolygon(m_textureTriangles.at(triangle.source).polygon);
			painter.resetTransform();
			painter.setBrush(m_shadowBrushes.at(triangle.shadowIndex));
			painter.drawPolygon(triangle.screen);
			continue;
		}
		// Flat shading, which is also the fallback for a triangle whose texture
		// transform could not be inverted.
		painter.setBrush(m_fillBrushes.at(triangle.brushIndex));
		painter.drawPolygon(triangle.screen);
	}
	painter.setBrush(Qt::NoBrush);

	if (m_showEdges) {
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setPen(QPen(palette.edge, 0.9));
		for (const int index : m_order) {
			painter.drawPolygon(m_projected.at(index).screen);
		}
	}
}

void ModelViewport::paintOverlay(QPainter& painter, const Palette& palette) const
{
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.resetTransform();
	if (m_hover.valid && m_hover.triangle >= 0 && m_hover.triangle < m_projected.size()) {
		const ProjectedTriangle& triangle = m_projected.at(m_hover.triangle);
		if (triangle.visible && triangle.screen.size() == 3) {
			painter.setBrush(Qt::NoBrush);
			painter.setPen(QPen(palette.hover, 1.8));
			painter.drawPolygon(triangle.screen);
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
	}

	QPainter painter(this);
	painter.fillRect(rect(), palette.background);
	painter.setRenderHint(QPainter::Antialiasing, true);

	if (!m_hasMesh) {
		paintEmptyState(painter, palette);
		paintOverlay(painter, palette);
		return;
	}

	// Rebuilding here is a no-op unless the frame, camera, widget size or device
	// pixel ratio actually changed; it reuses its storage, and it never calls
	// update().
	ensureProjection();

	if (m_showGrid) {
		paintGround(painter, palette);
	}
	if (m_showAxes) {
		paintAxes(painter, palette);
	}
	if (m_meshTriangles.isEmpty()) {
		paintNoGeometryState(painter, palette);
	} else {
		paintTriangles(painter, palette);
		paintHud(painter, palette);
	}
	paintOverlay(painter, palette);
}

void ModelViewport::paintHud(QPainter& painter, const Palette& palette) const
{
	const QString separator = QStringLiteral("  %1  ").arg(QChar(0x00b7));
	const QString left = effectiveRenderModeName();
	const QStringList right = {
		QStringList {QFileInfo(m_mesh.sourcePath).fileName(), tr("frame %1 / %2").arg(m_frame + 1).arg(std::max(1, m_frameTotal))}.join(separator),
		QStringList {tr("%n surface(s)", nullptr, m_mesh.surfaceCount), tr("%n vert(ex)(ices)", nullptr, m_mesh.vertexCount),
			tr("%n triangle(s)", nullptr, m_mesh.triangleCount)}.join(separator),
	};

	painter.save();
	painter.resetTransform();
	QFont font = painter.font();
	font.setPointSizeF(std::max(7.0, font.pointSizeF() * 0.9));
	painter.setFont(font);
	const QFontMetricsF metrics(font);
	const qreal margin = 8.0;
	const qreal padX = 7.0;
	const qreal padY = 3.0;
	QColor backdrop = palette.background;
	backdrop.setAlpha(m_highContrast ? 255 : 210);
	auto drawTag = [&](const QStringList& lines, bool alignRight) {
		qreal width = 0.0;
		for (const QString& line : lines) {
			width = std::max(width, metrics.horizontalAdvance(line));
		}
		width += padX * 2.0;
		const qreal height = metrics.height() * lines.size() + padY * 2.0;
		const qreal x = alignRight ? rect().right() - margin - width : rect().left() + margin;
		const QRectF tag(x, rect().top() + margin, width, height);
		painter.setPen(m_highContrast ? QPen(palette.text, 1.0) : Qt::NoPen);
		painter.setBrush(backdrop);
		painter.drawRoundedRect(tag, 3.0, 3.0);
		painter.setPen(palette.subtleText);
		for (int index = 0; index < lines.size(); ++index) {
			const QRectF lineRect(tag.left() + padX, tag.top() + padY + metrics.height() * index, tag.width() - padX * 2.0, metrics.height());
			painter.drawText(lineRect, Qt::AlignVCenter | (alignRight ? Qt::AlignRight : Qt::AlignLeft), lines.at(index));
		}
	};
	drawTag({left}, false);
	qreal rightWidth = 0.0;
	for (const QString& line : right) {
		rightWidth = std::max(rightWidth, metrics.horizontalAdvance(line));
	}
	if (width() > metrics.horizontalAdvance(left) + rightWidth + 60.0) {
		drawTag(right, true);
	}
	painter.restore();
}

void ModelViewport::mousePressEvent(QMouseEvent* event)
{
	setFocus(Qt::MouseFocusReason);
	const bool panRequested = event->button() == Qt::MiddleButton
		|| (event->button() == Qt::LeftButton
			&& (event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) != 0);
	if (panRequested) {
		m_panning = true;
		m_dragAnchor = event->position();
		setCursor(Qt::ClosedHandCursor);
		event->accept();
		return;
	}
	if (event->button() == Qt::LeftButton) {
		m_orbiting = true;
		m_dragAnchor = event->position();
		setCursor(Qt::SizeAllCursor);
		event->accept();
		return;
	}
	QWidget::mousePressEvent(event);
}

void ModelViewport::mouseMoveEvent(QMouseEvent* event)
{
	const QPointF position = event->position();
	if (m_panning) {
		applyPanDelta(position - m_dragAnchor);
		m_dragAnchor = position;
		event->accept();
		return;
	}
	if (m_orbiting) {
		const QPointF delta = position - m_dragAnchor;
		m_dragAnchor = position;
		// Dragging right turns the model to the right, which means the camera
		// azimuth runs the other way.
		applyOrbitDelta(-delta.x() * kOrbitDegreesPerPixel, -delta.y() * kOrbitDegreesPerPixel);
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
	if (m_panning || m_orbiting) {
		m_panning = false;
		m_orbiting = false;
		unsetCursor();
		event->accept();
		return;
	}
	QWidget::mouseReleaseEvent(event);
}

void ModelViewport::wheelEvent(QWheelEvent* event)
{
	const int delta = event->angleDelta().y();
	if (delta == 0) {
		QWidget::wheelEvent(event);
		return;
	}
	applyZoomFactor(std::pow(1.2, static_cast<double>(delta) / 120.0), event->position());
	event->accept();
}

void ModelViewport::keyPressEvent(QKeyEvent* event)
{
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

QString modelViewportRenderModeDisplayName(ModelViewportRenderMode mode)
{
	switch (mode) {
	case ModelViewportRenderMode::Wireframe:
		return viewText("Wireframe");
	case ModelViewportRenderMode::FlatShaded:
		return viewText("Flat shaded");
	case ModelViewportRenderMode::Textured:
		return viewText("Textured");
	}
	return viewText("Flat shaded");
}

} // namespace vibestudio
