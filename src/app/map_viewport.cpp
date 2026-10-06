#include "app/map_viewport.h"
#include "app/viewport_hud.h"
#include "app/viewport_image.h"
#include "core/level_scene.h"
#include "core/level_camera_keys.h"

#include <QBrush>
#include <QAccessible>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFont>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QFontMetricsF>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QResizeEvent>
#include <QSet>
#include <QShortcut>
#include <QWheelEvent>

#include <algorithm>
#include <utility>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>

#include <cmath>
#include <optional>
#include <limits>

namespace vibestudio {

namespace {

class PaintMeasurement {
	qint64* m_duration;
	QElapsedTimer m_timer;
public:
	PaintMeasurement(bool enabled, qint64* duration) : m_duration(enabled ? duration : nullptr) { if (m_duration) { m_timer.start(); } }
	~PaintMeasurement() { if (m_duration) { *m_duration += m_timer.nsecsElapsed(); } }
};

constexpr double kMinZoom = 0.0025;
constexpr double kMaxZoom = 64.0;
constexpr double kFitMargin = 24.0;
constexpr double kPickRadius = 7.0;
constexpr int kMaxLabels = 160;
constexpr double kPi = 3.14159265358979323846;
// Pixels the pointer must travel before a press on a selected object turns into
// a move; below this a press-and-release is still just a click.
constexpr double kDragThresholdPixels = 4.0;
// Upper bounds on overlay work, so a selection of tens of thousands of objects
// cannot make painting crawl.
constexpr int kMaxPreviewOutlines = 512;
// Shift multiplies the arrow-key nudge.
constexpr int kCoarseNudgeMultiplier = 8;
// Resize handles: drawn size, grab distance, and the smallest on-screen box
// that gets them, below which they would crowd out the object itself.
constexpr double kResizeHandleSize = 7.0;
constexpr double kResizeHandleGrab = 6.0;
constexpr double kResizeHandleMinBoxPixels = 18.0;

// Doom linedef flag bit 2 (0x0004, ML_TWOSIDED) marks a linedef with a back
// side; see https://doomwiki.org/wiki/Linedef.
constexpr int kDoomLinedefTwoSided = 0x0004;

double clampZoom(double zoom)
{
	if (!std::isfinite(zoom) || zoom <= 0.0) {
		return 1.0;
	}
	return std::clamp(zoom, kMinZoom, kMaxZoom);
}

LevelMapVec3 makeVec(double x, double y, double z)
{
	LevelMapVec3 value;
	value.x = x;
	value.y = y;
	value.z = z;
	value.valid = true;
	return value;
}

QRectF minMaxRect(double minX, double minY, double maxX, double maxY)
{
	return QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
}

// QRectF::intersects() is false for zero-area rectangles, which is exactly the
// case for a flat brush or a straight linedef, so overlap is tested manually.
bool rectsOverlap(const QRectF& a, const QRectF& b)
{
	return a.left() <= b.right() && a.right() >= b.left() && a.top() <= b.bottom() && a.bottom() >= b.top();
}

// Doom lump records are normally stored in index order, so the identifier is
// usually the index. The linear fallback keeps lookups correct for documents
// whose identifiers were renumbered.
template <typename T>
const T* findById(const QVector<T>& items, int id)
{
	if (id < 0) {
		return nullptr;
	}
	if (id < items.size() && items.at(id).id == id) {
		return &items.at(id);
	}
	for (const T& item : items) {
		if (item.id == id) {
			return &item;
		}
	}
	return nullptr;
}

double pointSegmentDistance(const QPointF& point, const QPointF& a, const QPointF& b)
{
	const double dx = b.x() - a.x();
	const double dy = b.y() - a.y();
	const double lengthSquared = dx * dx + dy * dy;
	if (lengthSquared <= 1e-12) {
		return std::hypot(point.x() - a.x(), point.y() - a.y());
	}
	double t = ((point.x() - a.x()) * dx + (point.y() - a.y()) * dy) / lengthSquared;
	t = std::clamp(t, 0.0, 1.0);
	return std::hypot(point.x() - (a.x() + dx * t), point.y() - (a.y() + dy * t));
}

double polygonEdgeDistance(const QPointF& point, const QPolygonF& polygon)
{
	if (polygon.size() < 2) {
		return polygon.isEmpty() ? std::numeric_limits<double>::max()
					 : std::hypot(point.x() - polygon.first().x(), point.y() - polygon.first().y());
	}
	double best = std::numeric_limits<double>::max();
	for (int index = 0; index < polygon.size(); ++index) {
		const QPointF& a = polygon.at(index);
		const QPointF& b = polygon.at((index + 1) % polygon.size());
		best = std::min(best, pointSegmentDistance(point, a, b));
	}
	return best;
}

bool documentIsDoom(const LevelMapDocument& document)
{
	return document.format == LevelMapFormat::DoomWad || !document.doomLinedefs.isEmpty()
		|| !document.doomVertices.isEmpty();
}

bool doomLinedefIsTwoSided(const LevelMapDoomLinedef& linedef)
{
	return linedef.backSidedef >= 0 || (linedef.flags & kDoomLinedefTwoSided) != 0;
}

int worldspawnEntityId(const LevelMapDocument& document)
{
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) == 0) {
			return entity.id;
		}
	}
	return -1;
}

QString objectLabelText(const LevelMapDocument& document, LevelMapSelectionKind kind, int id)
{
	switch (kind) {
	case LevelMapSelectionKind::None:
		break;
	case LevelMapSelectionKind::Entity: {
		const LevelMapEntity* entity = findById(document.entities, id);
		const QString className = (entity != nullptr && !entity->className.isEmpty())
			? entity->className
			: QCoreApplication::translate("VibeStudioMapViewport", "unnamed");
		return QCoreApplication::translate("VibeStudioMapViewport", "Entity %1 (%2)").arg(id).arg(className);
	}
	case LevelMapSelectionKind::DoomVertex:
		return QCoreApplication::translate("VibeStudioMapViewport", "Vertex %1").arg(id);
	case LevelMapSelectionKind::DoomLinedef: {
		const LevelMapDoomLinedef* linedef = findById(document.doomLinedefs, id);
		if (linedef == nullptr) {
			return QCoreApplication::translate("VibeStudioMapViewport", "Linedef %1").arg(id);
		}
		return doomLinedefIsTwoSided(*linedef) ? QCoreApplication::translate("VibeStudioMapViewport", "Linedef %1 (two-sided)").arg(id)
						      : QCoreApplication::translate("VibeStudioMapViewport", "Linedef %1 (one-sided)").arg(id);
	}
	case LevelMapSelectionKind::DoomThing: {
		const LevelMapDoomThing* thing = findById(document.doomThings, id);
		if (thing == nullptr) {
			return QCoreApplication::translate("VibeStudioMapViewport", "Thing %1").arg(id);
		}
		return QCoreApplication::translate("VibeStudioMapViewport", "Thing %1 (type %2, angle %3)").arg(id).arg(thing->type).arg(thing->angle);
	}
	case LevelMapSelectionKind::DoomSector: {
		const LevelMapDoomSector* sector = findById(document.doomSectors, id);
		if (sector == nullptr) {
			return QCoreApplication::translate("VibeStudioMapViewport", "Sector %1").arg(id);
		}
		return QCoreApplication::translate("VibeStudioMapViewport", "Sector %1 (light %2, floor %3, ceiling %4)")
			.arg(id)
			.arg(sector->lightLevel)
			.arg(sector->floorHeight)
			.arg(sector->ceilingHeight);
	}
	case LevelMapSelectionKind::QuakeBrush:
		return QCoreApplication::translate("VibeStudioMapViewport", "Brush %1").arg(id);
	case LevelMapSelectionKind::QuakePatch:
		return QCoreApplication::translate("VibeStudioMapViewport", "Patch %1").arg(id);
	}
	return QCoreApplication::translate("VibeStudioMapViewport", "Nothing selected");
}

QVector<LevelMapSelectionKind> selectionTiers(const LevelMapDocument& document)
{
	if (documentIsDoom(document)) {
		return {
			LevelMapSelectionKind::DoomThing,
			LevelMapSelectionKind::DoomVertex,
			LevelMapSelectionKind::DoomLinedef,
		};
	}
	return {
		LevelMapSelectionKind::Entity,
		LevelMapSelectionKind::QuakeBrush,
		LevelMapSelectionKind::QuakePatch,
	};
}

int objectCountForKind(const LevelMapDocument& document, LevelMapSelectionKind kind)
{
	switch (kind) {
	case LevelMapSelectionKind::None:
		return 0;
	case LevelMapSelectionKind::Entity:
		return static_cast<int>(document.entities.size());
	case LevelMapSelectionKind::DoomVertex:
		return static_cast<int>(document.doomVertices.size());
	case LevelMapSelectionKind::DoomLinedef:
		return static_cast<int>(document.doomLinedefs.size());
	case LevelMapSelectionKind::DoomThing:
		return static_cast<int>(document.doomThings.size());
	case LevelMapSelectionKind::DoomSector:
		return static_cast<int>(document.doomSectors.size());
	case LevelMapSelectionKind::QuakeBrush:
		return static_cast<int>(document.brushes.size());
	case LevelMapSelectionKind::QuakePatch:
		return static_cast<int>(document.patches.size());
	}
	return 0;
}

int objectIdAtIndex(const LevelMapDocument& document, LevelMapSelectionKind kind, int index)
{
	if (index < 0 || index >= objectCountForKind(document, kind)) {
		return -1;
	}
	switch (kind) {
	case LevelMapSelectionKind::None:
		return -1;
	case LevelMapSelectionKind::Entity:
		return document.entities.at(index).id;
	case LevelMapSelectionKind::DoomVertex:
		return document.doomVertices.at(index).id;
	case LevelMapSelectionKind::DoomLinedef:
		return document.doomLinedefs.at(index).id;
	case LevelMapSelectionKind::DoomThing:
		return document.doomThings.at(index).id;
	case LevelMapSelectionKind::DoomSector:
		return document.doomSectors.at(index).id;
	case LevelMapSelectionKind::QuakeBrush:
		return document.brushes.at(index).id;
	case LevelMapSelectionKind::QuakePatch:
		return document.patches.at(index).id;
	}
	return -1;
}

int objectIndexOfId(const LevelMapDocument& document, LevelMapSelectionKind kind, int id)
{
	const int count = objectCountForKind(document, kind);
	for (int index = 0; index < count; ++index) {
		if (objectIdAtIndex(document, kind, index) == id) {
			return index;
		}
	}
	return -1;
}

// Projected extent of an object. Point-like objects (things, entities,
// vertices) report a zero-size rectangle at their position, which is exactly
// what rubber-band containment and the drag preview want.
bool objectWorldBounds(const LevelMapDocument& document, const QVector<DoomSectorOutline>& outlines,
	const QVector<MapBrushGeometry>& brushGeometry, const MapViewportSceneIndex& index, MapViewportProjection projection, LevelMapSelectionKind kind,
	int id, QRectF* out)
{
	if (out == nullptr || id < 0) {
		return false;
	}
	switch (kind) {
	case LevelMapSelectionKind::None:
		return false;
	case LevelMapSelectionKind::DoomLinedef: {
		const LevelMapDoomLinedef* linedef = index.object(document.doomLinedefs, kind, id);
		if (linedef == nullptr) {
			return false;
		}
		const LevelMapDoomVertex* start = index.object(document.doomVertices, LevelMapSelectionKind::DoomVertex, linedef->startVertex);
		const LevelMapDoomVertex* end = index.object(document.doomVertices, LevelMapSelectionKind::DoomVertex, linedef->endVertex);
		if (start == nullptr || end == nullptr) {
			return false;
		}
		const double height = projection == MapViewportProjection::TopXY ? 0.0 : mapViewportLineHeight(document, *linedef);
		const QPointF a = mapViewportProjectPoint(projection, makeVec(start->x, start->y, height));
		const QPointF b = mapViewportProjectPoint(projection, makeVec(end->x, end->y, height));
		*out = minMaxRect(std::min(a.x(), b.x()), std::min(a.y(), b.y()), std::max(a.x(), b.x()), std::max(a.y(), b.y()));
		return true;
	}
	case LevelMapSelectionKind::DoomSector: {
		if (const auto* outline = index.sector(outlines, id); outline && !outline->bounds.isNull()) {
			*out = outline->bounds;
			return true;
		}
		return false;
	}
	case LevelMapSelectionKind::QuakeBrush: {
		if (const auto* brush = index.brush(brushGeometry, id); brush && brush->solved) {
			*out = mapViewportBounds(projection, brush->mins, brush->maxs);
			return true;
		}
		const LevelMapBrush* brush = index.object(document.brushes, kind, id);
		if (brush == nullptr || !brush->boundsSolved) {
			return false;
		}
		*out = mapViewportBounds(projection, brush->mins, brush->maxs);
		return true;
	}
	case LevelMapSelectionKind::QuakePatch: {
		const LevelMapPatch* patch = index.object(document.patches, kind, id);
		if (patch == nullptr) {
			return false;
		}
		if (patch->mins.valid && patch->maxs.valid) {
			*out = mapViewportBounds(projection, patch->mins, patch->maxs);
			return true;
		}
		if (patch->controlPoints.isEmpty()) {
			return false;
		}
		QPointF minimum = mapViewportProjectPoint(projection, patch->controlPoints.first());
		QPointF maximum = minimum;
		for (const LevelMapVec3& point : patch->controlPoints) {
			const QPointF projected = mapViewportProjectPoint(projection, point);
			minimum = QPointF(std::min(minimum.x(), projected.x()), std::min(minimum.y(), projected.y()));
			maximum = QPointF(std::max(maximum.x(), projected.x()), std::max(maximum.y(), projected.y()));
		}
		*out = minMaxRect(minimum.x(), minimum.y(), maximum.x(), maximum.y());
		return true;
	}
	case LevelMapSelectionKind::Entity:
	case LevelMapSelectionKind::DoomVertex:
	case LevelMapSelectionKind::DoomThing:
		break;
	}
	QPointF point;
	if (!mapViewportObjectPoint(document, outlines, brushGeometry, index, projection, kind, id, &point)) {
		return false;
	}
	*out = QRectF(point, QSizeF(0.0, 0.0));
	return true;
}

// QRectF::contains() normalizes, which hides the degenerate cases this viewport
// relies on (a flat brush, a straight linedef, a bare point), so containment is
// tested against the raw edges.
bool rectContainsRect(const QRectF& outer, const QRectF& inner)
{
	return inner.left() >= outer.left() && inner.right() <= outer.right() && inner.top() >= outer.top()
		&& inner.bottom() <= outer.bottom();
}

// Axis letters of the two drawn axes. These name coordinate components, so they
// are identifiers rather than translatable prose.
QString planeAxisLetters(MapViewportProjection projection, bool vertical)
{
	switch (projection) {
	case MapViewportProjection::TopXY:
		return vertical ? QStringLiteral("Y") : QStringLiteral("X");
	case MapViewportProjection::FrontXZ:
		return vertical ? QStringLiteral("Z") : QStringLiteral("X");
	case MapViewportProjection::SideZY:
		return vertical ? QStringLiteral("Z") : QStringLiteral("Y");
	}
	return vertical ? QStringLiteral("Y") : QStringLiteral("X");
}

QString planePointText(MapViewportProjection projection, const QPointF& point)
{
	return QStringLiteral("%1 %2, %3 %4")
		.arg(planeAxisLetters(projection, false))
		.arg(point.x(), 0, 'f', 1)
		.arg(planeAxisLetters(projection, true))
		.arg(point.y(), 0, 'f', 1);
}

} // namespace

// Colour set for one paint pass. Every distinction the viewport draws is also
// carried by line weight, dash pattern or marker shape so nothing depends on
// colour alone.
struct MapViewport::Palette {
	QColor background;
	QColor gridMinor;
	QColor gridMajor;
	QColor axis;
	QColor oneSided;
	QColor twoSided;
	QColor vertex;
	QColor thing;
	QColor entity;
	QColor brush;
	QColor brushEntity;
	QColor patch;
	QColor warning;
	QColor selection;
	QColor hover;
	QColor text;
	QColor subtleText;
	QColor focus;
	QColor sectorDark;
	QColor sectorBright;
	QColor leak;
	QColor link;
};

MapViewport::MapViewport(QWidget* parent)
	: QWidget(parent)
{
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);
	setAcceptDrops(true);
	setAutoFillBackground(false);
	setAttribute(Qt::WA_OpaquePaintEvent, true);
	setAccessibleName(tr("Map viewport"));
	setAccessibleDescription(accessibleSummary());
	m_worldBounds = minMaxRect(-512.0, -512.0, 512.0, 512.0);
	m_worldCenter = m_worldBounds.center();

	m_planRenderWorker = new MapPlanRenderWorker(this);
	m_planRenderWorker->completed = [this](const MapPlanRenderResult& result) {
		if (result.sceneRevision != m_sceneRevision || result.projection != m_projection) { return; }
		if (result.wiresComputed) { m_planWires = result.wires; m_planWiresComputed = true; }
		if (m_planRequestedScene == m_sceneRevision && !result.baseFrame.image.isNull()
			&& !sameMapPlanView(m_planWireFrame, m_planRequestedView)) { m_planWireFrame = result.baseFrame; }
		if (result.selectionRevision == m_selectionRevision) {
			if (result.selectionWiresComputed) { m_selectionWires = result.selectionWires; m_selectionWiresComputed = true; }
			if (m_planRequestedScene == m_sceneRevision && !result.selectionFrame.image.isNull()
				&& !sameMapPlanView(m_selectionWireFrame, m_planRequestedView)) { m_selectionWireFrame = result.selectionFrame; }
		}
		if (result.sceneRevision == m_planRequestedScene && result.selectionRevision == m_planRequestedSelection
			&& sameMapPlanView(result.view, m_planRequestedView)) {
			m_planRenderFailed = result.failed;
			m_planRenderPending = !result.failed && (m_planWireFrame.image.isNull() || !sameMapPlanView(m_planWireFrame, m_planRequestedView)
				|| (!m_selection.isEmpty() && (m_selectionWireFrame.image.isNull() || !sameMapPlanView(m_selectionWireFrame, m_planRequestedView))));
		}
		setAccessibleDescription(accessibleSummary()); update(); Q_EMIT renderCompleted();
	};
	m_overlayRenderWorker = new MapViewportOverlayWorker(this);
	m_overlayRenderWorker->completed = [this](const MapViewportOverlayResult& result) {
		if (result.key.sceneRevision != m_sceneRevision || result.key.selectionRevision != m_selectionRevision
			|| result.key.projection != m_projection || !sameMapViewportOverlayKey(result.key,m_overlayRequested)) { return; }
		m_overlayRenderFailed = result.failed; m_overlayRenderPending = false;
		if (result.ready) {
			m_overlayFrame = result;
			if (result.key.showGrid) { m_gridFrame = result.grid; }
		}
		setAccessibleDescription(accessibleSummary()); update(); Q_EMIT renderCompleted();
	};
	// QWidget::event() consumes Tab and Shift+Tab for focus navigation before
	// keyPressEvent() is reached, so selection cycling is bound as widget-local
	// shortcuts and then routed through the normal key handler. Escape moves
	// focus on once the selection is clear, so the viewport is never a keyboard
	// trap.
	auto* cycleForward = new QShortcut(QKeySequence(Qt::Key_Tab), this);
	cycleForward->setContext(Qt::WidgetShortcut);
	connect(cycleForward, &QShortcut::activated, this, [this]() {
		QKeyEvent keyEvent(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
		keyPressEvent(&keyEvent);
	});
	// Shift+Tab steps backwards and extends the selection; Ctrl+Tab extends
	// forwards, so a set can be built in either direction from the keyboard.
	auto* cycleBackward = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Tab), this);
	cycleBackward->setContext(Qt::WidgetShortcut);
	connect(cycleBackward, &QShortcut::activated, this, [this]() {
		QKeyEvent keyEvent(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
		keyPressEvent(&keyEvent);
	});
	auto* extendForward = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab), this);
	extendForward->setContext(Qt::WidgetShortcut);
	connect(extendForward, &QShortcut::activated, this, [this]() {
		QKeyEvent keyEvent(QEvent::KeyPress, Qt::Key_Tab, Qt::ControlModifier);
		keyPressEvent(&keyEvent);
	});
	auto* extendBackward = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), this);
	extendBackward->setContext(Qt::WidgetShortcut);
	connect(extendBackward, &QShortcut::activated, this, [this]() {
		QKeyEvent keyEvent(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier | Qt::ControlModifier);
		keyPressEvent(&keyEvent);
	});
}

MapViewport::~MapViewport() { delete m_overlayRenderWorker; delete m_planRenderWorker; }

void MapViewport::retirePlanRender()
{
	if (m_planRenderWorker) { m_planRenderWorker->cancel(); }
	m_planRequestedScene = 0; m_planRenderPending = false; m_planRenderFailed = false;
}

void MapViewport::retireOverlayRender()
{
	if (m_overlayRenderWorker) { m_overlayRenderWorker->cancel(); }
	m_overlayRequested = {}; m_overlayFrame = {};
	m_overlayRenderPending = false; m_overlayRenderFailed = false; m_asyncOverlays = false;
}

MapViewportOverlayRequest MapViewport::overlayRequest(const MapViewportOverlayKey& key) const
{
	MapViewportOverlayRequest request; request.key = key;
	request.document = m_document; request.brushes = m_brushGeometry; request.sectors = m_sectorOutlines; request.index = m_sceneIndex;
	request.selection = m_selection; request.primary = {m_selectionKind,m_selectedObjectId}; request.previousGrid = m_gridFrame;
	return request;
}

void MapViewport::prepareOverlays(QPainter& painter, const Palette& palette)
{
	const bool wasRendering = isRendering();
	const auto device = viewportImageDevice(painter.deviceTransform());
	MapViewportOverlayKey key; key.sceneRevision = m_sceneRevision; key.selectionRevision = m_selectionRevision; key.projection = m_projection;
	key.view.viewport = size(); key.view.center = m_worldCenter; key.view.zoom = m_zoom; key.view.units = m_gridSize;
	key.view.pixelRatio = device.pixelRatio; key.view.pixelPhase = device.pixelPhase;
	key.view.minor = palette.gridMinor.rgba(); key.view.major = palette.gridMajor.rgba(); key.view.axis = palette.axis.rgba();
	key.showGrid = m_showGrid; key.showMembers = m_selection.size() > 1;
	key.selectionColor = palette.selection.rgba(); key.markerWidth = m_highContrast ? 2.0 : 1.4;
	const bool sameRequest = sameMapViewportOverlayKey(key,m_overlayRequested);
	const bool supported = !mapGridImageSize(key.view).isEmpty();
	m_asyncOverlays = (m_planRenderWeight > 4096 || m_selection.size() > 64) && (key.showGrid || key.showMembers)
		&& supported && !(sameRequest && m_overlayRenderFailed);
	if (!m_asyncOverlays) {
		m_overlayRenderWorker->cancel(); m_overlayRenderPending = false; m_overlayFrame = {};
	} else if (m_overlayFrame.ready && sameMapViewportOverlayKey(key,m_overlayFrame.key)) {
		// A burst can return to the already presented view before its pending
		// replacement finishes. Do not let that retired target evict this frame.
		m_overlayRenderWorker->cancel(); m_overlayRequested = key;
		m_overlayRenderPending = false; m_overlayRenderFailed = false;
	} else {
		// Keep only visually compatible previous images for navigation feedback.
		// Scene/selection changes also retire their member frame at mutation time.
		const auto& previous = m_overlayFrame.key;
		if (previous.sceneRevision != key.sceneRevision || previous.selectionRevision != key.selectionRevision
			|| previous.projection != key.projection || previous.selectionColor != key.selectionColor
			|| previous.markerWidth != key.markerWidth || previous.view.pixelRatio != key.view.pixelRatio) { m_overlayFrame = {}; }
		m_overlayRequested = key; m_overlayRenderPending = true; m_overlayRenderFailed = false;
		m_overlayRenderWorker->request(overlayRequest(key));
	}
	if (wasRendering != isRendering()) { setAccessibleDescription(accessibleSummary()); }
}

void MapViewport::paintOverlayImage(QPainter& painter, const QImage& image, const MapGridView& view) const
{
	if (image.isNull()) { return; }
	const double scale = m_zoom / view.zoom;
	const QSizeF target(view.viewport.width() * scale,view.viewport.height() * scale);
	const QPointF offset(width() * 0.5 - target.width() * 0.5 + (view.center.x() - m_worldCenter.x()) * m_zoom,
		height() * 0.5 - target.height() * 0.5 - (view.center.y() - m_worldCenter.y()) * m_zoom);
	// Preserve the ceil physical extent at fractional DPR, as for base geometry.
	painter.drawImage(QRectF(offset - view.pixelPhase * (scale / view.pixelRatio),image.deviceIndependentSize() * scale),image);
}

void MapViewport::setDocument(const LevelMapDocument& document, const MapBrushGeometryCache* prepared)
{
	// A different map shows everything again.
	if (prepared) { m_brushGeometryCache = *prepared; }
	else { m_brushGeometryCache.clear(); }
	m_sourceDocument = document;
	m_sourceIndex.rebuild(m_sourceDocument);
	m_hasWorkZone = false;
	m_hasCameraBrushDraft = false;
	m_hidden.clear();
	applyHiddenFilter();
	m_hasDocument = true;
	adoptDocumentSelection();
	m_hover = MapViewportHit();
	m_panning = false;
	m_pressArmed = false;
	m_dragging = false;
	m_resizing = false;
	m_hoverHandle = 0;
	m_banding = false;
	m_clipDrawing = false;
	m_hasClipLine = false;
	if (m_clipMode) {
		m_clipMode = false;
		Q_EMIT clipModeChanged(false);
	}
	m_drawCorners.clear();
	m_hasDrawHover = false;
	if (m_drawMode) {
		m_drawMode = false;
		Q_EMIT drawModeChanged(false);
	}
	m_topologyRevision = document.doomTopologyRevision;
	// A trail belongs to the map whose build wrote it.
	m_leakTrail.clear();
	unsetCursor();
	rebuildGeometry();
	updateWorldBounds();
	updateWorkZone();
	zoomToFit();
	setAccessibleDescription(accessibleSummary());
	// The readout under the view spoke of the last map, or a mode it had on.
	Q_EMIT hoverChanged(hoverSummary());
	update();
}

void MapViewport::updateDocument(const LevelMapDocument& document)
{
	if (!m_hasDocument) {
		setDocument(document);
		return;
	}
	// An edit changes geometry, not the camera: keep the pan and zoom the user
	// set, and only re-solve what is drawn. Hidden objects stay hidden unless
	// the document now selects them.
	m_sourceDocument = document;
	m_sourceIndex.rebuild(m_sourceDocument);
	if (document.doomTopologyRevision != m_topologyRevision) {
		m_topologyRevision = document.doomTopologyRevision;
		for (auto it = m_hidden.begin(); it != m_hidden.end();) {
			const auto kind = static_cast<LevelMapSelectionKind>(it->first);
			if (kind == LevelMapSelectionKind::DoomVertex || kind == LevelMapSelectionKind::DoomLinedef || kind == LevelMapSelectionKind::DoomSector) {
				it = m_hidden.erase(it);
			} else {
				++it;
			}
		}
	}
	forgetVanishedHidden();
	revealSelected(document.selection);
	applyHiddenFilter();
	adoptDocumentSelection();
	m_hover = MapViewportHit();
	m_pressArmed = false;
	m_dragging = false;
	m_resizing = false;
	m_banding = false;
	rebuildGeometry();
	updateWorldBounds();
	updateWorkZone();
	setAccessibleDescription(accessibleSummary());
	update();
}

void MapViewport::adoptDocumentSelection()
{
	if (!m_document.selection.isEmpty()) {
		setSelectionSetInternal(m_document.selection);
		return;
	}
	m_selection.clear();
	if (m_document.selectionKind != LevelMapSelectionKind::None && m_document.selectedObjectId >= 0) {
		m_selection.push_back(LevelMapSelectionRef {m_document.selectionKind, m_document.selectedObjectId});
	}
	syncPrimaryFromSelection();
}

void MapViewport::syncPrimaryFromSelection()
{
	invalidateSelectionGeometry();
	if (m_selection.isEmpty()) {
		m_selectionKind = LevelMapSelectionKind::None;
		m_selectedObjectId = -1;
		return;
	}
	m_selectionKind = m_selection.back().kind;
	m_selectedObjectId = m_selection.back().objectId;
}

void MapViewport::setSelectionSetInternal(const QVector<LevelMapSelectionRef>& selection)
{
	m_selection.clear();
	m_selection.reserve(selection.size());
	// Keep the caller's last occurrence so their final entry is primary: walk
	// backwards keeping first sightings, then turn around. Removing earlier
	// occurrences one reference at a time was quadratic in the selection.
	QSet<QPair<int, int>> seen;
	for (auto it = selection.crbegin(); it != selection.crend(); ++it) {
		const LevelMapSelectionRef& ref = *it;
		if (ref.kind == LevelMapSelectionKind::None || ref.objectId < 0 || isHidden(ref.kind, ref.objectId)) {
			continue;
		}
		const QPair<int, int> key(static_cast<int>(ref.kind), ref.objectId);
		if (seen.contains(key)) {
			continue;
		}
		seen.insert(key);
		m_selection.push_back(ref);
	}
	std::reverse(m_selection.begin(), m_selection.end());
	syncPrimaryFromSelection();
}

void MapViewport::clearDocument()
{
	m_selectionRingCache.clear();
	m_gridFrame = {};
	m_brushGeometryCache.clear();
	m_document = LevelMapDocument();
	m_sourceDocument = LevelMapDocument();
	m_sourceIndex = {};
	m_hidden.clear();
	m_sceneHidden.clear();
	m_leakTrail.clear();
	m_targetLinks.clear();
	m_tagLinks.clear();
	m_hasWorkZone = false;
	m_hasCameraBrushDraft = false;
	m_hasDocument = false;
	rebuildGeometry();
	m_brushGeometry.clear();
	m_sectorOutlines.clear();
	m_selection.clear();
	m_selectionKind = LevelMapSelectionKind::None;
	m_selectedObjectId = -1;
	m_hover = MapViewportHit();
	m_panning = false;
	m_pressArmed = false;
	m_dragging = false;
	m_resizing = false;
	m_hoverHandle = 0;
	m_banding = false;
	m_clipDrawing = false;
	m_hasClipLine = false;
	if (m_clipMode) {
		m_clipMode = false;
		Q_EMIT clipModeChanged(false);
	}
	m_drawCorners.clear();
	m_hasDrawHover = false;
	if (m_drawMode) {
		m_drawMode = false;
		Q_EMIT drawModeChanged(false);
	}
	unsetCursor();
	m_worldBounds = minMaxRect(-512.0, -512.0, 512.0, 512.0);
	m_worldCenter = m_worldBounds.center();
	m_zoom = 1.0;
	setAccessibleDescription(accessibleSummary());
	Q_EMIT hoverChanged(hoverSummary());
	update();
	Q_EMIT viewChanged();
}

bool MapViewport::hasDocument() const
{
	return m_hasDocument;
}

void MapViewport::synchronizeSceneFrom(const MapViewport& source, bool frame)
{
	if (&source == this) {
		return;
	}
	if (m_sceneRevision != source.m_sceneRevision) {
		cancelInteraction();
		retirePlanRender();
		m_sceneRevision = source.m_sceneRevision;
		m_sourceDocument = source.m_sourceDocument;
		m_document = source.m_document;
		m_sourceIndex = source.m_sourceIndex;
		m_sceneIndex = source.m_sceneIndex;
		m_planWires = m_projection == source.m_projection ? source.m_planWires : MapPlanWires();
		m_planWireFrame = m_projection == source.m_projection ? source.m_planWireFrame : MapPlanWireFrame();
		m_planWiresComputed = m_projection == source.m_projection && source.m_planWiresComputed;
		m_planRenderWeight = source.m_planRenderWeight;
		m_hasDocument = source.m_hasDocument;
		m_brushGeometry = source.m_brushGeometry;
		m_brushGeometryCache = source.m_brushGeometryCache;
		m_sectorOutlines = source.m_sectorOutlines;
		m_hidden = source.m_hidden;
		m_sceneHidden = source.m_sceneHidden;
		m_targetLinks = source.m_targetLinks;
		m_tagLinks = source.m_tagLinks;
		m_topologyRevision = source.m_topologyRevision;
		m_hover = MapViewportHit();
		updateWorldBounds();
	}
	m_leakTrail = source.m_leakTrail;
	m_selection = source.m_selection;
	syncPrimaryFromSelection();
	if (m_projection == source.m_projection) {
		m_selectionWires = source.m_selectionWires;
		m_selectionWireFrame = source.m_selectionWireFrame;
		m_selectionWiresComputed = source.m_selectionWiresComputed;
	}
	m_hasWorkZone = source.m_hasWorkZone;
	m_workMins = source.m_workMins;
	m_workMaxs = source.m_workMaxs;
	if (frame) {
		setClipMode(false);
		setDrawMode(false);
		zoomToFit();
	}
	setAccessibleDescription(accessibleSummary());
	update();
}

void MapViewport::setActivePane(bool active)
{
	if (m_activePane == active) { return; }
	m_activePane = active;
	setAccessibleDescription(accessibleSummary());
	update();
}

void MapViewport::setProjection(MapViewportProjection projection)
{
	if (m_projection == projection) {
		return;
	}
	// The drag plane, the rubber band, and a clip line are all expressed in
	// the projected axes, so none survives a projection change; sectors are
	// drawn from above only.
	cancelInteraction();
	m_hasClipLine = false;
	if (m_drawMode && projection != MapViewportProjection::TopXY) {
		setDrawMode(false);
	}
	m_projection = projection;
	retirePlanRender();
	m_planWires = {};
	m_planWireFrame = {};
	m_planWiresComputed = false;
	invalidateSelectionGeometry();
	updateWorldBounds();
	zoomToFit();
	setAccessibleDescription(accessibleSummary());
	update();
}

MapViewportProjection MapViewport::projection() const
{
	return m_projection;
}

void MapViewport::setSelection(LevelMapSelectionKind kind, int objectId)
{
	// A caller that hands back the primary member the viewport already has is
	// only echoing the document's single-selection fields, so the multi-selection
	// the user built here must survive it.
	if (m_selectionKind == kind && m_selectedObjectId == objectId) {
		return;
	}
	if (revealSelected({LevelMapSelectionRef {kind, objectId}})) {
		refilterHidden();
	}
	replaceSelection(kind, objectId);
	updateWorkZone();
	setAccessibleDescription(accessibleSummary());
	update();
}

void MapViewport::setSelectionSet(const QVector<LevelMapSelectionRef>& selection)
{
	if (revealSelected(selection)) {
		refilterHidden();
	}
	setSelectionSetInternal(selection);
	updateWorkZone();
	setAccessibleDescription(accessibleSummary());
	update();
}

QVector<LevelMapSelectionRef> MapViewport::selectionSet() const
{
	return m_selection;
}

LevelMapSelectionKind MapViewport::selectionKind() const
{
	return m_selectionKind;
}

int MapViewport::selectedObjectId() const
{
	return m_selectedObjectId;
}

bool MapViewport::selectionContains(LevelMapSelectionKind kind, int objectId) const
{
	return m_selection.contains(LevelMapSelectionRef {kind, objectId});
}

QSet<QPair<int, int>> MapViewport::selectedKeys() const
{
	QSet<QPair<int, int>> keys;
	keys.reserve(m_selection.size());
	for (const LevelMapSelectionRef& ref : m_selection) {
		keys.insert({static_cast<int>(ref.kind), ref.objectId});
	}
	return keys;
}

void MapViewport::replaceSelection(LevelMapSelectionKind kind, int objectId)
{
	m_selection.clear();
	if (kind != LevelMapSelectionKind::None && objectId >= 0) {
		m_selection.push_back(LevelMapSelectionRef {kind, objectId});
	}
	syncPrimaryFromSelection();
}

void MapViewport::addToSelection(LevelMapSelectionKind kind, int objectId)
{
	if (kind == LevelMapSelectionKind::None || objectId < 0) {
		return;
	}
	const LevelMapSelectionRef ref {kind, objectId};
	// Adding a member that is already present promotes it to primary rather than
	// duplicating it.
	m_selection.removeAll(ref);
	m_selection.push_back(ref);
	syncPrimaryFromSelection();
}

void MapViewport::toggleInSelection(LevelMapSelectionKind kind, int objectId)
{
	if (kind == LevelMapSelectionKind::None || objectId < 0) {
		return;
	}
	const LevelMapSelectionRef ref {kind, objectId};
	if (m_selection.removeAll(ref) > 0) {
		syncPrimaryFromSelection();
		return;
	}
	m_selection.push_back(ref);
	syncPrimaryFromSelection();
}

void MapViewport::setGridSize(int units)
{
	const int clamped = std::clamp(units, 1, 4096);
	if (m_gridSize == clamped) {
		return;
	}
	m_gridSize = clamped;
	retireOverlayRender(); m_gridFrame = {};
	update();
}

int MapViewport::gridSize() const
{
	return m_gridSize;
}

void MapViewport::setSnapToGrid(bool enabled)
{
	if (m_snapToGrid == enabled) {
		return;
	}
	m_snapToGrid = enabled;
	if (m_dragging) {
		Q_EMIT hoverChanged(dragSummary());
	}
	update();
}

bool MapViewport::snapToGrid() const
{
	return m_snapToGrid;
}

bool MapViewport::isDragging() const
{
	return m_dragging;
}

void MapViewport::cancelInteraction()
{
	setTemporaryPan(false);
	const bool wasBusy = m_dragging || m_banding || m_pressArmed || m_resizing || m_clipDrawing || m_brushDrawing || m_clickArmed;
	m_brushArmed = false;
	m_brushDrawing = false;
	m_clickArmed = false;
	m_pendingResizeEdges = 0;
	m_rightArmed = false;
	m_zoomDragging = false;
	m_cameraDrag = CameraDrag::None;
	m_pressArmed = false;
	m_dragging = false;
	m_resizing = false;
	m_clipDrawing = false;
	m_resizeEdges = 0;
	m_banding = false;
	m_dragAnchorPlane = QPointF();
	m_dragCurrentPlane = QPointF();
	if (wasBusy) {
		Q_EMIT hoverChanged(hoverSummary());
		update();
	}
}

void MapViewport::setShowGrid(bool show)
{
	if (m_showGrid == show) {
		return;
	}
	m_showGrid = show;
	retireOverlayRender();
	if (!show) { m_gridFrame = {}; }
	update();
}

void MapViewport::setShowThings(bool show)
{
	if (m_showThings == show) {
		return;
	}
	m_showThings = show;
	update();
}

void MapViewport::setShowSectorFill(bool show)
{
	if (m_showSectorFill == show) {
		return;
	}
	m_showSectorFill = show;
	update();
}

void MapViewport::setShowVertices(bool show)
{
	if (m_showVertices == show) {
		return;
	}
	m_showVertices = show;
	update();
}

void MapViewport::setShowLabels(bool show)
{
	if (m_showLabels == show) {
		return;
	}
	m_showLabels = show;
	update();
}

bool MapViewport::showGrid() const
{
	return m_showGrid;
}

bool MapViewport::showThings() const
{
	return m_showThings;
}

bool MapViewport::showSectorFill() const
{
	return m_showSectorFill;
}

bool MapViewport::showVertices() const
{
	return m_showVertices;
}

bool MapViewport::showLabels() const
{
	return m_showLabels;
}

void MapViewport::setShowTargetLinks(bool show)
{
	if (m_showTargetLinks == show) {
		return;
	}
	m_showTargetLinks = show;
	setAccessibleDescription(accessibleSummary());
	update();
}

bool MapViewport::showTargetLinks() const
{
	return m_showTargetLinks;
}

int MapViewport::targetLinkCount() const
{
	return static_cast<int>(m_targetLinks.size());
}

int MapViewport::tagLinkCount() const
{
	return static_cast<int>(m_tagLinks.size());
}

bool MapViewport::tagLinkShown(const LevelMapTagLink& link) const
{
	return m_showTargetLinks || selectionContains(LevelMapSelectionKind::DoomLinedef, link.linedefId)
		|| selectionContains(LevelMapSelectionKind::DoomSector, link.sectorId);
}

int MapViewport::drawnTagLinkCount() const
{
	if (m_projection != MapViewportProjection::TopXY) {
		return 0;
	}
	if (m_showTargetLinks) {
		return static_cast<int>(m_tagLinks.size());
	}
	const QSet<QPair<int, int>> selected = selectedKeys();
	return static_cast<int>(std::count_if(m_tagLinks.cbegin(), m_tagLinks.cend(), [&selected](const LevelMapTagLink& link) {
		return selected.contains({static_cast<int>(LevelMapSelectionKind::DoomLinedef), link.linedefId})
			|| selected.contains({static_cast<int>(LevelMapSelectionKind::DoomSector), link.sectorId});
	}));
}

void MapViewport::setHighContrast(bool enabled)
{
	if (m_highContrast == enabled) {
		return;
	}
	m_highContrast = enabled;
	retireOverlayRender(); m_gridFrame = {};
	update();
}

void MapViewport::setReducedMotion(bool enabled)
{
	if (m_reducedMotion == enabled) {
		return;
	}
	m_reducedMotion = enabled;
	update();
}

void MapViewport::zoomToFit()
{
	m_worldCenter = m_worldBounds.center();
	const double worldWidth = std::max(m_worldBounds.width(), 1.0);
	const double worldHeight = std::max(m_worldBounds.height(), 1.0);
	// The widget may still be unsized when a document is loaded, so fall back to
	// the size hint rather than producing an absurd zoom.
	const double viewWidth = width() > 64 ? std::max(width() - 2.0 * kFitMargin, 32.0) : 640.0;
	const double viewHeight = height() > 64 ? std::max(height() - 2.0 * kFitMargin, 32.0) : 480.0;
	m_zoom = clampZoom(std::min(viewWidth / worldWidth, viewHeight / worldHeight));
	update();
	Q_EMIT viewChanged();
}

int MapViewport::hideSelection()
{
	int hidden = 0;
	for (const LevelMapSelectionRef& ref : std::as_const(m_selection)) {
		if (ref.kind == LevelMapSelectionKind::Entity || ref.kind == LevelMapSelectionKind::QuakeBrush
			|| ref.kind == LevelMapSelectionKind::QuakePatch || ref.kind == LevelMapSelectionKind::DoomThing) {
			m_hidden.insert({static_cast<int>(ref.kind), ref.objectId});
			++hidden;
		}
	}
	if (hidden == 0) {
		return 0;
	}
	m_selection.clear();
	syncPrimaryFromSelection();
	m_hover = MapViewportHit();
	refilterHidden();
	announceSelection();
	return hidden;
}

void MapViewport::showAllHidden()
{
	if (m_hidden.isEmpty()) {
		return;
	}
	m_hidden.clear();
	refilterHidden();
	setAccessibleDescription(accessibleSummary());
	update();
}

const LevelMapDocument& MapViewport::displayDocument() const
{
	return m_document;
}

int MapViewport::hiddenCount() const
{
	int count = static_cast<int>(m_sceneHidden.size());
	for (const auto& object : m_hidden) {
		if (!m_sceneHidden.contains(levelMapSelectionRefId({static_cast<LevelMapSelectionKind>(object.first), object.second}))) { ++count; }
	}
	return count;
}

bool MapViewport::isHidden(LevelMapSelectionKind kind, int objectId) const
{
	if (!m_sceneHidden.isEmpty() && m_sceneHidden.contains(levelSceneCanonicalObject(m_sourceDocument, levelMapSelectionRefId({kind, objectId})))) { return true; }
	if (m_hidden.isEmpty()) { return false; }
	if (m_hidden.contains({static_cast<int>(kind), objectId})) {
		return true;
	}
	// A brush or patch is hidden with its entity.
	const int entity = static_cast<int>(LevelMapSelectionKind::Entity);
	if (kind == LevelMapSelectionKind::QuakeBrush) {
		const LevelMapBrush* brush = m_sourceIndex.object(m_sourceDocument.brushes, kind, objectId);
		return brush && m_hidden.contains({entity, brush->entityId});
	}
	if (kind == LevelMapSelectionKind::QuakePatch) {
		const LevelMapPatch* patch = m_sourceIndex.object(m_sourceDocument.patches, kind, objectId);
		return patch && m_hidden.contains({entity, patch->entityId});
	}
	return false;
}

void MapViewport::forgetVanishedHidden()
{
	for (auto it = m_hidden.begin(); it != m_hidden.end();) {
		if (levelMapObjectExists(m_sourceDocument, {static_cast<LevelMapSelectionKind>(it->first), it->second})) {
			++it;
		} else {
			it = m_hidden.erase(it);
		}
	}
}

bool MapViewport::revealSelected(const QVector<LevelMapSelectionRef>& selection)
{
	if (m_hidden.isEmpty()) {
		return false;
	}
	bool revealed = false;
	const int entity = static_cast<int>(LevelMapSelectionKind::Entity);
	for (const LevelMapSelectionRef& ref : selection) {
		revealed = m_hidden.remove({static_cast<int>(ref.kind), ref.objectId}) || revealed;
		if (ref.kind == LevelMapSelectionKind::QuakeBrush) {
			if (const LevelMapBrush* brush = m_sourceIndex.object(m_sourceDocument.brushes, ref.kind, ref.objectId)) {
				revealed = m_hidden.remove({entity, brush->entityId}) || revealed;
			}
		} else if (ref.kind == LevelMapSelectionKind::QuakePatch) {
			if (const LevelMapPatch* patch = m_sourceIndex.object(m_sourceDocument.patches, ref.kind, ref.objectId)) {
				revealed = m_hidden.remove({entity, patch->entityId}) || revealed;
			}
		}
	}
	return revealed;
}

void MapViewport::applyHiddenFilter()
{
	m_document = m_sourceDocument;
	m_sceneHidden = levelSceneHiddenObjects(m_sourceDocument);
	if (m_hidden.isEmpty() && m_sceneHidden.isEmpty()) {
		return;
	}
	const auto hidden = [this](LevelMapSelectionKind kind, int id) {
		return isHidden(kind, id);
	};
	// Entities go with their brushes and patches; a Doom thing with the entity
	// it is mirrored into.
	QSet<int> hiddenEntities;
	for (const LevelMapEntity& entity : std::as_const(m_document.entities)) {
		if (hidden(LevelMapSelectionKind::Entity, entity.id) || hidden(LevelMapSelectionKind::DoomThing, entity.id)) {
			hiddenEntities.insert(entity.id);
		}
	}
	m_document.entities.removeIf([&hiddenEntities](const LevelMapEntity& entity) { return hiddenEntities.contains(entity.id); });
	m_document.brushes.removeIf([&](const LevelMapBrush& brush) {
		return hidden(LevelMapSelectionKind::QuakeBrush, brush.id) || hiddenEntities.contains(brush.entityId);
	});
	m_document.patches.removeIf([&](const LevelMapPatch& patch) {
		return hidden(LevelMapSelectionKind::QuakePatch, patch.id) || hiddenEntities.contains(patch.entityId);
	});
	m_document.doomThings.removeIf([&](const LevelMapDoomThing& thing) { return hidden(LevelMapSelectionKind::DoomThing, thing.id); });
	// Doom geometry records retain their indices. Painting and picking consult
	// isHidden; the camera uses the same scene filter before producing triangles.
	auto selection = m_document.selection;
	selection.removeIf([&](const LevelMapSelectionRef& ref) { return hidden(ref.kind, ref.objectId); });
	setLevelMapSelection(&m_document, selection);
}

void MapViewport::refilterHidden()
{
	applyHiddenFilter();
	rebuildGeometry();
	// Hiding and showing change what Zoom to Fit frames, never where the
	// camera is: with everything hidden the bounds would otherwise snap the
	// view to the origin.
	const QPointF center = m_worldCenter;
	const double zoom = m_zoom;
	updateWorldBounds();
	m_worldCenter = center;
	m_zoom = zoom;
}

void MapViewport::invalidateSelectionGeometry()
{
	retireOverlayRender();
	++m_selectionRevision;
	m_planRequestedSelection = 0; m_planRenderPending = false; m_planRenderFailed = false;
	m_selectionAreaComputed = false;
	m_selectionBoundsComputed = false;
	m_selectionWires = {};
	m_selectionWireFrame = {};
	m_selectionWiresComputed = false;
}

bool MapViewport::selectionWorldArea(QRectF* area) const
{
	if (!m_selectionAreaComputed) {
		m_selectionAreaValid = false;
		m_selectionArea = {};
		QVector<LevelMapSelectionRef> entities;
		const auto include = [&](QRectF bounds) {
			bounds = bounds.normalized();
			// QRectF::united drops point and straight-line extents. They still
			// count when framing several entities, vertices or flat projections.
			m_selectionArea = m_selectionAreaValid ? minMaxRect(std::min(m_selectionArea.left(), bounds.left()),
				std::min(m_selectionArea.top(), bounds.top()), std::max(m_selectionArea.right(), bounds.right()),
				std::max(m_selectionArea.bottom(), bounds.bottom())) : bounds;
			m_selectionAreaValid = true;
		};
		for (const LevelMapSelectionRef& ref : m_selection) {
			if (ref.kind == LevelMapSelectionKind::Entity && !documentIsDoom(m_document)) { entities.append(ref); }
			QRectF bounds;
			if (!objectWorldBounds(m_document, m_sectorOutlines, m_brushGeometry, m_sceneIndex, m_projection, ref.kind, ref.objectId, &bounds)) { continue; }
			include(bounds);
		}
		// Brush entities may have no explicit origin. Resolve all selected
		// owners in one shared-service pass, against the visible document;
		// resolving each owner separately would reintroduce quadratic scans.
		LevelMapVec3 low, high;
		if (!entities.isEmpty() && levelMapObjectsBounds(m_document, entities, &low, &high)) {
			include(mapViewportBounds(m_projection, low, high));
		}
		m_selectionAreaComputed = true;
	}
	if (m_selectionAreaValid && area) { *area = m_selectionArea; }
	return m_selectionAreaValid;
}

bool MapViewport::selectionTransformBounds(LevelMapVec3* mins, LevelMapVec3* maxs) const
{
	if (!m_selectionBoundsComputed) {
		// Keep the authoring service authoritative: entity-owned hidden geometry
		// participates in resize/work-zone bounds even when it is not drawn.
		m_selectionBoundsValid = m_hasDocument && !m_selection.isEmpty()
			&& levelMapObjectsBounds(m_sourceDocument, m_selection, &m_selectionMins, &m_selectionMaxs);
		m_selectionBoundsComputed = true;
	}
	if (m_selectionBoundsValid) {
		if (mins) { *mins = m_selectionMins; }
		if (maxs) { *maxs = m_selectionMaxs; }
	}
	return m_selectionBoundsValid;
}

QSizeF MapViewport::selectionExtent() const
{
	QRectF area;
	return selectionWorldArea(&area) ? area.size() : QSizeF();
}

void MapViewport::zoomToSelection()
{
	QRectF area;
	if (!selectionWorldArea(&area)) {
		zoomToFit();
		return;
	}
	frameWorldArea(area);
}

void MapViewport::setLeakTrail(const QVector<LevelMapVec3>& points)
{
	m_leakTrail = points;
	setAccessibleDescription(accessibleSummary());
	update();
}

void MapViewport::clearLeakTrail()
{
	if (m_leakTrail.isEmpty()) {
		return;
	}
	m_leakTrail.clear();
	setAccessibleDescription(accessibleSummary());
	update();
}

bool MapViewport::hasLeakTrail() const
{
	return !m_leakTrail.isEmpty();
}

void MapViewport::zoomToLeakTrail()
{
	if (m_leakTrail.isEmpty()) {
		zoomToFit();
		return;
	}
	// Built from coordinates: QRectF::united() ignores the zero-size rectangle
	// a single point makes.
	const QPointF first = projectPoint(m_leakTrail.first());
	double minX = first.x();
	double minY = first.y();
	double maxX = first.x();
	double maxY = first.y();
	for (const LevelMapVec3& point : std::as_const(m_leakTrail)) {
		const QPointF projected = projectPoint(point);
		minX = std::min(minX, projected.x());
		minY = std::min(minY, projected.y());
		maxX = std::max(maxX, projected.x());
		maxY = std::max(maxY, projected.y());
	}
	frameWorldArea(minMaxRect(minX, minY, maxX, maxY));
}

void MapViewport::frameWorldArea(QRectF area)
{
	// A point object has no extent: show a grid-sized neighbourhood around it
	// so it lands in context instead of at maximum magnification.
	const double minimumExtent = std::max(4.0 * m_gridSize, 128.0);
	if (area.width() < minimumExtent) {
		area.adjust(-(minimumExtent - area.width()) / 2.0, 0.0, (minimumExtent - area.width()) / 2.0, 0.0);
	}
	if (area.height() < minimumExtent) {
		area.adjust(0.0, -(minimumExtent - area.height()) / 2.0, 0.0, (minimumExtent - area.height()) / 2.0);
	}
	m_worldCenter = area.center();
	const double viewWidth = width() > 64 ? std::max(width() - 2.0 * kFitMargin, 32.0) : 640.0;
	const double viewHeight = height() > 64 ? std::max(height() - 2.0 * kFitMargin, 32.0) : 480.0;
	m_zoom = clampZoom(std::min(viewWidth / area.width(), viewHeight / area.height()));
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT viewChanged();
}

void MapViewport::zoomIn()
{
	const double zoom = clampZoom(m_zoom * 1.25);
	if (qFuzzyCompare(zoom, m_zoom)) {
		return;
	}
	m_zoom = zoom;
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT viewChanged();
}

void MapViewport::zoomOut()
{
	const double zoom = clampZoom(m_zoom / 1.25);
	if (qFuzzyCompare(zoom, m_zoom)) {
		return;
	}
	m_zoom = zoom;
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT viewChanged();
}

PlanViewState MapViewport::navigationState() const
{
	return {static_cast<int>(m_projection), m_worldCenter, m_zoom};
}

bool MapViewport::restoreNavigationState(const PlanViewState& state)
{
	if (!validatePlanViewState(state)) { return false; }
	cancelInteraction();
	setClipMode(false);
	setDrawMode(false);
	setProjection(static_cast<MapViewportProjection>(state.projection));
	m_worldCenter = state.center;
	m_zoom = state.zoom;
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT viewChanged();
	return true;
}

bool MapViewport::applyLinkedNavigation(const PlanViewState& state)
{
	if (!validatePlanViewState(state) || state.projection != static_cast<int>(m_projection)) { return false; }
	if (state.center == m_worldCenter && state.zoom == m_zoom) { return true; }
	m_worldCenter = state.center;
	m_zoom = state.zoom;
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT viewChanged();
	return true;
}

void MapViewport::resetView()
{
	m_zoom = 1.0;
	m_worldCenter = m_worldBounds.center();
	m_panning = false;
	unsetCursor();
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT viewChanged();
}

double MapViewport::zoom() const
{
	return m_zoom;
}

QString MapViewport::hoverSummary() const
{
	if (!m_hasDocument) {
		return tr("No map loaded.");
	}
	if (m_dragging) {
		return dragSummary();
	}
	if (m_resizing) {
		return resizeSummary();
	}
	if (m_clipMode && (m_clipDrawing || !m_hasClipLine)) {
		return clipSummary();
	}
	if (m_drawMode) {
		return drawSummary();
	}
	if (m_banding) {
		const QRectF band = bandWorldRect();
		return tr("Selecting inside %1 to %2 (%3 objects)")
			.arg(planePointText(m_projection, band.topLeft()),
				planePointText(m_projection, band.bottomRight()))
			.arg(objectsInWorldRect(band).size());
	}
	const QString coordinates = tr("x %1, y %2")
					    .arg(m_hover.worldX, 0, 'f', 1)
					    .arg(m_hover.worldY, 0, 'f', 1);
	if (m_hover.kind == LevelMapSelectionKind::None || m_hover.label.isEmpty()) {
		return tr("%1 - empty space").arg(coordinates);
	}
	return tr("%1 - %2").arg(coordinates, m_hover.label);
}

QStringList MapViewport::statusLines() const
{
	QStringList lines;
	lines << tr("Projection: %1").arg(mapViewportProjectionDisplayName(m_projection));
	lines << tr("Zoom: %1% (%2 units per pixel)")
			 .arg(m_zoom * 100.0, 0, 'f', 1)
			 .arg(m_zoom > 0.0 ? 1.0 / m_zoom : 0.0, 0, 'f', 2);
	lines << (m_showGrid ? tr("Grid: %1 units").arg(m_gridSize) : tr("Grid: hidden (%1 units)").arg(m_gridSize));
	if (!m_hasDocument) {
		lines << tr("Map: not loaded");
		lines << tr("Selection: none");
		return lines;
	}
	if (!m_leakTrail.isEmpty()) {
		lines << tr("Leak trail: %n point(s)", nullptr, static_cast<int>(m_leakTrail.size()));
	}
	if (!m_tagLinks.isEmpty()) {
		lines << (m_showTargetLinks ? tr("Tag links: %1").arg(m_tagLinks.size())
					    : tr("Tag links: %1, shown for the selection only").arg(m_tagLinks.size()));
	}
	if (!m_targetLinks.isEmpty()) {
		lines << (m_showTargetLinks ? tr("Target links: %1").arg(m_targetLinks.size())
					    : tr("Target links: %1, shown for the selection only").arg(m_targetLinks.size()));
	}
	if (documentIsDoom(m_document)) {
		lines << tr("Vertices %1, linedefs %2, sectors %3, things %4")
				 .arg(m_document.doomVertices.size())
				 .arg(m_document.doomLinedefs.size())
				 .arg(m_document.doomSectors.size())
				 .arg(m_document.doomThings.size());
		int openSectors = 0;
		for (const DoomSectorOutline& outline : m_sectorOutlines) {
		if (isHidden(LevelMapSelectionKind::DoomSector, outline.sectorId)) { continue; }
			if (outline.openEdgeCount > 0) {
				++openSectors;
			}
		}
		if (openSectors > 0) {
			lines << tr("Sector outlines with open edges: %1").arg(openSectors);
		}
	} else {
		int solved = 0;
		for (const MapBrushGeometry& brush : m_brushGeometry) {
			if (brush.solved) {
				++solved;
			}
		}
		lines << tr("Brushes %1 (%2 solved), patches %3, entities %4")
				 .arg(m_brushGeometry.size())
				 .arg(solved)
				 .arg(m_document.patches.size())
				 .arg(m_document.entities.size());
		const int unsolved = static_cast<int>(m_brushGeometry.size()) - solved;
		if (unsolved > 0) {
			lines << tr("Unsolved brushes drawn dashed: %1").arg(unsolved);
		}
	}
	lines << (m_selectionKind == LevelMapSelectionKind::None
			? tr("Selection: none")
			: tr("Selection: %1").arg(objectLabelText(m_document, m_selectionKind, m_selectedObjectId)));
	if (m_selection.size() > 1) {
		lines << tr("Selected objects: %1 (primary listed above)").arg(m_selection.size());
	}
	const QSizeF extent = selectionExtent();
	if (extent.width() > 0.0 || extent.height() > 0.0) {
		lines << tr("Selection size: %1 by %2 units").arg(extent.width(), 0, 'g', 6).arg(extent.height(), 0, 'g', 6);
	}
	if (!m_hidden.isEmpty()) {
		lines << tr("Hidden objects: %1 (Show All, Shift+H, brings them back)").arg(m_hidden.size());
	}
	lines << (m_snapToGrid ? tr("Snap: on (%1 units)").arg(m_gridSize) : tr("Snap: off"));
	if (m_dragging) {
		lines << dragSummary();
	}
	if (m_resizing) {
		lines << resizeSummary();
	}
	if (m_clipMode) {
		lines << clipSummary();
	}
	if (m_drawMode) {
		lines << drawSummary();
	}
	return lines;
}

QString MapViewport::dragSummary() const
{
	const QPointF delta = snappedPlaneDelta();
	const QString deltaText = planePointText(m_projection, delta);
	QPointF world;
	if (m_selectionKind != LevelMapSelectionKind::None
		&& mapViewportObjectPoint(m_document, m_sectorOutlines, m_brushGeometry, m_sceneIndex, m_projection, m_selectionKind,
			m_selectedObjectId, &world)) {
		return tr("Moving %n object(s) by %1 - destination %2", nullptr, static_cast<int>(m_selection.size()))
			.arg(deltaText, planePointText(m_projection, world + delta));
	}
	return tr("Moving %n object(s) by %1", nullptr, static_cast<int>(m_selection.size())).arg(deltaText);
}

QString MapViewport::accessibleSummary() const
{
	const QString help = (m_activePane ? QStringLiteral(" ") + tr("Active editing pane.") : QString())
		+ (m_panHoldActive ? QStringLiteral(" ") + tr("Temporary pan active; release %1 to stop.").arg(m_controls.panHoldKey) : QString())
		+ (m_controlsHelp.isEmpty() ? QString() : QStringLiteral(" ") + m_controlsHelp);
	if (!m_hasDocument) {
		return tr("Map viewport: no map is loaded, so there is nothing to draw.") + help;
	}
	const QString name = m_document.mapName.isEmpty() ? tr("an unnamed map") : m_document.mapName;
	QString selection = m_selectionKind == LevelMapSelectionKind::None
		? tr("nothing selected")
		: objectLabelText(m_document, m_selectionKind, m_selectedObjectId);
	if (m_selection.size() > 1) {
		selection = tr("%1, primary of %n selected object(s)", nullptr, static_cast<int>(m_selection.size()))
				    .arg(selection);
	}
	// Said last, so the map's own summary still leads.
	QString overlays = m_leakTrail.isEmpty() ? QString()
		: QStringLiteral(" ") + tr("A compiler leak trail of %n point(s) is drawn over the map.", nullptr, static_cast<int>(m_leakTrail.size()));
	if (const int drawnTags = drawnTagLinkCount(); drawnTags > 0) {
		overlays += QStringLiteral(" ") + tr("Arrows show %n tag link(s) from lines to the sectors they act on.", nullptr, drawnTags);
	}
	if (!m_targetLinks.isEmpty() && m_showTargetLinks) {
		overlays += QStringLiteral(" ") + tr("Arrows show %n target link(s) between entities.", nullptr, static_cast<int>(m_targetLinks.size()));
	}
	if (!m_hidden.isEmpty()) {
		overlays += QStringLiteral(" ") + tr("%n object(s) are hidden from the view.", nullptr, static_cast<int>(m_hidden.size()));
	}
	if (m_drawMode) {
		overlays += QStringLiteral(" ")
			+ tr("Draw Sector is on with %n corner(s) down; Enter closes the shape and Backspace takes a corner back.", nullptr,
				static_cast<int>(m_drawCorners.size()));
	}
	if (m_hasCameraBrushDraft) { overlays += QStringLiteral(" ") + tr("Camera brush draft: %1 × %2 × %3 units. No source edit yet.")
		.arg(m_cameraBrushDraft[3]-m_cameraBrushDraft[0]).arg(m_cameraBrushDraft[4]-m_cameraBrushDraft[1]).arg(m_cameraBrushDraft[5]-m_cameraBrushDraft[2]); }
	if (isRendering()) { overlays += QStringLiteral(" ") + tr("Updating view…"); }
	overlays += help;
	if (documentIsDoom(m_document)) {
		return tr("Map viewport showing %1 in the %2 projection at %3 percent zoom, with %4 linedefs, %5 sectors "
			  "and %6 things; %7.")
			.arg(name)
			.arg(mapViewportProjectionDisplayName(m_projection))
			.arg(m_zoom * 100.0, 0, 'f', 0)
			.arg(m_document.doomLinedefs.size())
			.arg(m_document.doomSectors.size())
			.arg(m_document.doomThings.size())
			.arg(selection)
			+ overlays;
	}
	return tr("Map viewport showing %1 in the %2 projection at %3 percent zoom, with %4 brushes, %5 patches and %6 "
		  "entities; %7.")
		.arg(name)
		.arg(mapViewportProjectionDisplayName(m_projection))
		.arg(m_zoom * 100.0, 0, 'f', 0)
		.arg(m_document.brushes.size())
		.arg(m_document.patches.size())
		.arg(m_document.entities.size())
		.arg(selection)
		+ overlays;
}

QSize MapViewport::sizeHint() const
{
	return QSize(640, 480);
}

QSize MapViewport::minimumSizeHint() const
{
	return QSize(240, 180);
}

void MapViewport::rebuildGeometry()
{
	retirePlanRender();
	m_planRenderWeight = 0;
	// Widgets and their geometry are confined to the GUI thread. A shared
	// generation lets sibling panes reuse Qt's implicitly shared scene arrays.
	static quint64 nextSceneRevision = 0;
	m_sceneRevision = ++nextSceneRevision;
	m_planWires = {};
	m_planWireFrame = {};
	m_planWiresComputed = false;
	invalidateSelectionGeometry();
	m_sceneIndex.rebuild(m_document);
	// Solving happens here and only here: paintEvent must never rebuild.
	m_brushGeometry.clear();
	m_sectorOutlines.clear();
	m_targetLinks.clear();
	if (!m_hasDocument) {
		return;
	}
	m_targetLinks = levelMapTargetLinks(m_document);
	m_tagLinks.clear();
	if (documentIsDoom(m_document)) {
		m_sectorOutlines = buildDoomSectorOutlines(m_document);
		m_tagLinks = levelMapTagLinks(m_document);
	}
	m_brushGeometry = m_brushGeometryCache.build(m_document);
	// Estimate edge/control work once per scene, including complex small maps.
	// Keep these reads const: mutable Qt iterators would detach the document
	// arrays shared with the source and sibling panes.
	for (const auto& brush : std::as_const(m_document.brushes)) { m_planRenderWeight += std::max<qsizetype>(1,brush.faces.size()) * 4; }
	for (const auto& patch : std::as_const(m_document.patches)) { m_planRenderWeight += patch.controlPoints.size() * 12; }
	m_sceneIndex.rebuildGeometry(m_brushGeometry, m_sectorOutlines);
}

void MapViewport::updateWorldBounds()
{
	bool any = false;
	double minX = 0.0;
	double minY = 0.0;
	double maxX = 0.0;
	double maxY = 0.0;
	const auto add = [&](const QPointF& point) {
		if (!std::isfinite(point.x()) || !std::isfinite(point.y())) {
			return;
		}
		if (!any) {
			minX = maxX = point.x();
			minY = maxY = point.y();
			any = true;
			return;
		}
		minX = std::min(minX, point.x());
		maxX = std::max(maxX, point.x());
		minY = std::min(minY, point.y());
		maxY = std::max(maxY, point.y());
	};

	if (m_hasDocument) {
		double floorLow = 0.0;
		double ceilingHigh = 0.0;
		bool heights = false;
		for (const LevelMapDoomSector& sector : std::as_const(m_document.doomSectors)) {
			const double low = static_cast<double>(sector.floorHeight);
			const double high = static_cast<double>(sector.ceilingHeight);
			if (!heights) {
				floorLow = low;
				ceilingHigh = high;
				heights = true;
				continue;
			}
			floorLow = std::min(floorLow, low);
			ceilingHigh = std::max(ceilingHigh, high);
		}
		for (const LevelMapDoomVertex& vertex : std::as_const(m_document.doomVertices)) {
			if (isHidden(LevelMapSelectionKind::DoomVertex, vertex.id)) { continue; }
			add(mapViewportProjectPoint(m_projection, makeVec(vertex.x, vertex.y, 0.0)));
			if (heights && m_projection != MapViewportProjection::TopXY) {
				add(mapViewportProjectPoint(m_projection, makeVec(vertex.x, vertex.y, floorLow)));
				add(mapViewportProjectPoint(m_projection, makeVec(vertex.x, vertex.y, ceilingHigh)));
			}
		}
		for (const LevelMapDoomThing& thing : std::as_const(m_document.doomThings)) {
			add(mapViewportProjectPoint(m_projection, makeVec(thing.x, thing.y, 0.0)));
		}
		for (const MapBrushGeometry& brush : std::as_const(m_brushGeometry)) {
			if (!brush.solved) {
				continue;
			}
			const QRectF bounds = mapViewportBounds(m_projection, brush.mins, brush.maxs);
			add(bounds.topLeft());
			add(bounds.bottomRight());
		}
		for (const LevelMapBrush& brush : std::as_const(m_document.brushes)) {
			if (!brush.boundsSolved) {
				continue;
			}
			const QRectF bounds = mapViewportBounds(m_projection, brush.mins, brush.maxs);
			add(bounds.topLeft());
			add(bounds.bottomRight());
		}
		for (const LevelMapPatch& patch : std::as_const(m_document.patches)) {
			for (const LevelMapVec3& point : patch.controlPoints) {
				add(mapViewportProjectPoint(m_projection, point));
			}
		}
		for (const LevelMapEntity& entity : std::as_const(m_document.entities)) {
			if (entity.origin.valid) {
				add(mapViewportProjectPoint(m_projection, entity.origin));
			}
		}
	}

	if (!any) {
		m_worldBounds = minMaxRect(-512.0, -512.0, 512.0, 512.0);
		m_worldCenter = m_worldBounds.center();
		return;
	}
	const double padX = std::max((maxX - minX) * 0.02, 16.0);
	const double padY = std::max((maxY - minY) * 0.02, 16.0);
	m_worldBounds = minMaxRect(minX - padX, minY - padY, maxX + padX, maxY + padY);
}

QPointF MapViewport::worldToView(double x, double y) const
{
	// Y is flipped: map space grows upward, widget space grows downward.
	return QPointF(width() * 0.5 + (x - m_worldCenter.x()) * m_zoom,
		height() * 0.5 - (y - m_worldCenter.y()) * m_zoom);
}

QPointF MapViewport::viewToWorld(const QPointF& point) const
{
	if (m_zoom <= 0.0) {
		return m_worldCenter;
	}
	return QPointF(m_worldCenter.x() + (point.x() - width() * 0.5) / m_zoom,
		m_worldCenter.y() - (point.y() - height() * 0.5) / m_zoom);
}

LevelMapVec3 MapViewport::worldPositionAt(const QPointF& viewPoint, double hiddenAxisValue) const
{
	QPointF world = viewToWorld(viewPoint);
	if (m_snapToGrid && m_gridSize > 0) {
		world = QPointF(snapLevelMapCoordinate(world.x(), m_gridSize), snapLevelMapCoordinate(world.y(), m_gridSize));
	}
	switch (m_projection) {
	case MapViewportProjection::TopXY:
		return makeVec(world.x(), world.y(), hiddenAxisValue);
	case MapViewportProjection::FrontXZ:
		return makeVec(world.x(), hiddenAxisValue, world.y());
	case MapViewportProjection::SideZY:
		return makeVec(hiddenAxisValue, world.x(), world.y());
	}
	return makeVec(world.x(), world.y(), hiddenAxisValue);
}

QPointF MapViewport::projectPoint(const LevelMapVec3& point) const
{
	return mapViewportProjectPoint(m_projection, point);
}

MapViewportHit MapViewport::hitTest(const QPointF& viewPoint) const
{
	MapViewportHit hit;
	const QPointF world = viewToWorld(viewPoint);
	hit.worldX = world.x();
	hit.worldY = world.y();
	if (!m_hasDocument) {
		return hit;
	}

	const double pickWorld = m_zoom > 0.0 ? kPickRadius / m_zoom : kPickRadius;
	const QRectF pickRect = minMaxRect(world.x() - pickWorld, world.y() - pickWorld, world.x() + pickWorld,
		world.y() + pickWorld);
	double best = kPickRadius;
	const auto consider = [&](LevelMapSelectionKind kind, int id, const QPointF& worldPoint, double distance) {
		if (isHidden(kind, id)) { return; }
		// Outside the pick radius is always rejected; once something is held, a
		// tie keeps it so that the documented tier and iteration order decides.
		if (distance > best || (hit.kind != LevelMapSelectionKind::None && distance >= best)) {
			return;
		}
		best = distance;
		hit.kind = kind;
		hit.objectId = id;
		hit.label = objectLabelText(m_document, kind, id);
		hit.worldX = worldPoint.x();
		hit.worldY = worldPoint.y();
	};

	// Tier 1: things and point entities.
	for (const LevelMapDoomThing& thing : m_document.doomThings) {
		const QPointF point = mapViewportProjectPoint(m_projection, makeVec(thing.x, thing.y, 0.0));
		if (!pickRect.contains(point)) {
			continue;
		}
		const QPointF view = worldToView(point.x(), point.y());
		consider(LevelMapSelectionKind::DoomThing, thing.id, point,
			std::hypot(view.x() - viewPoint.x(), view.y() - viewPoint.y()));
	}
	// parseDoomWad mirrors every THING into document.entities with the same id
	// and origin. Those duplicates are never drawn by paintDoom and Entity is not
	// one of a Doom document's selectionTiers, so picking them would report an
	// Entity hit for a thing marker and disagree with Tab navigation.
	if (!documentIsDoom(m_document)) {
		for (const LevelMapEntity& entity : m_document.entities) {
			if (!entity.origin.valid) {
				continue;
			}
			const QPointF point = mapViewportProjectPoint(m_projection, entity.origin);
			if (!pickRect.contains(point)) {
				continue;
			}
			const QPointF view = worldToView(point.x(), point.y());
			consider(LevelMapSelectionKind::Entity, entity.id, point,
				std::hypot(view.x() - viewPoint.x(), view.y() - viewPoint.y()));
		}
	}
	if (hit.kind != LevelMapSelectionKind::None) {
		return hit;
	}

	// Tier 2: vertices.
	for (const LevelMapDoomVertex& vertex : m_document.doomVertices) {
		if (isHidden(LevelMapSelectionKind::DoomVertex, vertex.id)) { continue; }
		const QPointF point = mapViewportProjectPoint(m_projection, makeVec(vertex.x, vertex.y, 0.0));
		if (!pickRect.contains(point)) {
			continue;
		}
		const QPointF view = worldToView(point.x(), point.y());
		consider(LevelMapSelectionKind::DoomVertex, vertex.id, point,
			std::hypot(view.x() - viewPoint.x(), view.y() - viewPoint.y()));
	}
	if (hit.kind != LevelMapSelectionKind::None) {
		return hit;
	}

	// Tier 3: linedefs, brush edges and patch outlines.
	for (const LevelMapDoomLinedef& linedef : m_document.doomLinedefs) {
		if (isHidden(LevelMapSelectionKind::DoomLinedef, linedef.id)) { continue; }
		const LevelMapDoomVertex* start = findById(m_document.doomVertices, linedef.startVertex);
		const LevelMapDoomVertex* end = findById(m_document.doomVertices, linedef.endVertex);
		if (start == nullptr || end == nullptr) {
			continue;
		}
		const double height = m_projection == MapViewportProjection::TopXY
			? 0.0
			: mapViewportLineHeight(m_document, linedef);
		const QPointF a = mapViewportProjectPoint(m_projection, makeVec(start->x, start->y, height));
		const QPointF b = mapViewportProjectPoint(m_projection, makeVec(end->x, end->y, height));
		const QRectF segment = minMaxRect(std::min(a.x(), b.x()), std::min(a.y(), b.y()), std::max(a.x(), b.x()),
			std::max(a.y(), b.y()));
		if (!rectsOverlap(segment, pickRect)) {
			continue;
		}
		const QPointF viewA = worldToView(a.x(), a.y());
		const QPointF viewB = worldToView(b.x(), b.y());
		consider(LevelMapSelectionKind::DoomLinedef, linedef.id, QPointF((a.x() + b.x()) * 0.5, (a.y() + b.y()) * 0.5),
			pointSegmentDistance(viewPoint, viewA, viewB));
	}
	for (const MapBrushGeometry& brush : m_brushGeometry) {
		if (!brush.solved) {
			continue;
		}
		const QRectF bounds = mapViewportBounds(m_projection, brush.mins, brush.maxs);
		if (!rectsOverlap(bounds, pickRect)) {
			continue;
		}
		double distance = std::numeric_limits<double>::max();
		if (m_projection == MapViewportProjection::TopXY) {
			const QVector<QPolygonF> footprints = brush.footprintPolygons();
			for (const QPolygonF& polygon : footprints) {
				QPolygonF viewPolygon;
				viewPolygon.reserve(polygon.size());
				for (const QPointF& point : polygon) {
					viewPolygon.append(worldToView(point.x(), point.y()));
				}
				distance = std::min(distance, polygonEdgeDistance(viewPoint, viewPolygon));
			}
		} else {
			QPolygonF viewPolygon;
			for (const MapFacePolygon& face : brush.faces) {
				if (face.points.size() < 2) {
					continue;
				}
				viewPolygon.clear();
				for (const LevelMapVec3& point : face.points) {
					const QPointF projected = mapViewportProjectPoint(m_projection, point);
					viewPolygon.append(worldToView(projected.x(), projected.y()));
				}
				distance = std::min(distance, polygonEdgeDistance(viewPoint, viewPolygon));
			}
		}
		consider(LevelMapSelectionKind::QuakeBrush, brush.brushId, bounds.center(), distance);
	}
	for (const LevelMapPatch& patch : m_document.patches) {
		if (!patch.mins.valid || !patch.maxs.valid) {
			continue;
		}
		const QRectF bounds = mapViewportBounds(m_projection, patch.mins, patch.maxs);
		if (!rectsOverlap(bounds, pickRect)) {
			continue;
		}
		const QPointF view = worldToView(bounds.center().x(), bounds.center().y());
		const QPointF corner = worldToView(bounds.right(), bounds.top());
		const double radius = std::max(std::abs(corner.x() - view.x()), std::abs(corner.y() - view.y()));
		const double distance = std::max(0.0,
			std::hypot(viewPoint.x() - view.x(), viewPoint.y() - view.y()) - radius);
		consider(LevelMapSelectionKind::QuakePatch, patch.id, bounds.center(), distance);
	}
	return hit;
}

QRectF MapViewport::bandWorldRect() const
{
	const QPointF a = viewToWorld(m_bandAnchorView);
	const QPointF b = viewToWorld(m_bandCurrentView);
	return minMaxRect(std::min(a.x(), b.x()), std::min(a.y(), b.y()), std::max(a.x(), b.x()), std::max(a.y(), b.y()));
}

QVector<LevelMapSelectionRef> MapViewport::objectsInWorldRect(const QRectF& rect) const
{
	QVector<LevelMapSelectionRef> result;
	if (!m_hasDocument) {
		return result;
	}
	const QRectF area = minMaxRect(std::min(rect.left(), rect.right()), std::min(rect.top(), rect.bottom()),
		std::max(rect.left(), rect.right()), std::max(rect.top(), rect.bottom()));
	// An object counts as selected when its whole projected extent is inside the
	// band, which is what a mapper expects from a lasso: clipping half a brush
	// would be a surprise.
	//
	// Each kind walks its own container to preserve selection tier and storage
	// order without allocating per-object lookups on every pointer move.
	const int worldspawnId = worldspawnEntityId(m_document);
	const auto keep = [&](LevelMapSelectionKind kind, int id, const QRectF& bounds) {
		if (id >= 0 && !isHidden(kind, id) && rectContainsRect(area, bounds)) {
			result.push_back(LevelMapSelectionRef {kind, id});
		}
	};
	const auto pointRect = [](const QPointF& point) { return QRectF(point, QSizeF(0.0, 0.0)); };

	for (const LevelMapSelectionKind kind : selectionTiers(m_document)) {
		switch (kind) {
		case LevelMapSelectionKind::DoomThing:
			for (const LevelMapDoomThing& thing : m_document.doomThings) {
				keep(kind, thing.id, pointRect(mapViewportProjectPoint(m_projection, makeVec(thing.x, thing.y, 0.0))));
			}
			break;
		case LevelMapSelectionKind::DoomVertex:
			for (const LevelMapDoomVertex& vertex : m_document.doomVertices) {
		if (isHidden(LevelMapSelectionKind::DoomVertex, vertex.id)) { continue; }
				keep(kind, vertex.id, pointRect(mapViewportProjectPoint(m_projection, makeVec(vertex.x, vertex.y, 0.0))));
			}
			break;
		case LevelMapSelectionKind::DoomLinedef:
			for (const LevelMapDoomLinedef& linedef : m_document.doomLinedefs) {
		if (isHidden(LevelMapSelectionKind::DoomLinedef, linedef.id)) { continue; }
				const LevelMapDoomVertex* start = findById(m_document.doomVertices, linedef.startVertex);
				const LevelMapDoomVertex* end = findById(m_document.doomVertices, linedef.endVertex);
				if (start == nullptr || end == nullptr) {
					continue;
				}
				const double height = m_projection == MapViewportProjection::TopXY
					? 0.0
					: mapViewportLineHeight(m_document, linedef);
				const QPointF a = mapViewportProjectPoint(m_projection, makeVec(start->x, start->y, height));
				const QPointF b = mapViewportProjectPoint(m_projection, makeVec(end->x, end->y, height));
				keep(kind, linedef.id,
					minMaxRect(std::min(a.x(), b.x()), std::min(a.y(), b.y()), std::max(a.x(), b.x()),
						std::max(a.y(), b.y())));
			}
			break;
		case LevelMapSelectionKind::Entity:
			for (const LevelMapEntity& entity : m_document.entities) {
				// Worldspawn holds the world brushes and is never drawn as a
				// marker, so it is not band-selectable either.
				if (!entity.origin.valid || entity.id == worldspawnId) {
					continue;
				}
				keep(kind, entity.id, pointRect(mapViewportProjectPoint(m_projection, entity.origin)));
			}
			break;
		case LevelMapSelectionKind::QuakeBrush:
			for (int index = 0; index < m_document.brushes.size(); ++index) {
				const LevelMapBrush& brush = m_document.brushes.at(index);
				// buildLevelMapBrushGeometry() preserves order, so the parallel
				// entry is normally at the same index; fall back to the parser
				// bounds when it is not solved or not aligned.
				if (index < m_brushGeometry.size() && m_brushGeometry.at(index).brushId == brush.id
					&& m_brushGeometry.at(index).solved) {
					keep(kind, brush.id,
						mapViewportBounds(m_projection, m_brushGeometry.at(index).mins,
							m_brushGeometry.at(index).maxs));
					continue;
				}
				if (!brush.boundsSolved) {
					continue;
				}
				keep(kind, brush.id, mapViewportBounds(m_projection, brush.mins, brush.maxs));
			}
			break;
		case LevelMapSelectionKind::QuakePatch:
			for (const LevelMapPatch& patch : m_document.patches) {
				if (!patch.mins.valid || !patch.maxs.valid) {
					continue;
				}
				keep(kind, patch.id, mapViewportBounds(m_projection, patch.mins, patch.maxs));
			}
			break;
		case LevelMapSelectionKind::None:
		case LevelMapSelectionKind::DoomSector:
			break;
		}
	}
	return result;
}

QPointF MapViewport::snappedPlaneDelta() const
{
	const QPointF raw = m_dragCurrentPlane - m_dragAnchorPlane;
	if (!m_snapToGrid || m_gridSize <= 0) {
		return raw;
	}
	// Snapping goes through the core helper so a drag, an arrow-key nudge and a
	// CLI move all land on the same coordinates.
	const double grid = static_cast<double>(m_gridSize);
	return QPointF(snapLevelMapCoordinate(raw.x(), grid), snapLevelMapCoordinate(raw.y(), grid));
}

void MapViewport::planeDeltaToWorld(const QPointF& planeDelta, double* dx, double* dy, double* dz) const
{
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;
	switch (m_projection) {
	case MapViewportProjection::TopXY:
		x = planeDelta.x();
		y = planeDelta.y();
		break;
	case MapViewportProjection::FrontXZ:
		x = planeDelta.x();
		z = planeDelta.y();
		break;
	case MapViewportProjection::SideZY:
		y = planeDelta.x();
		z = planeDelta.y();
		break;
	}
	if (dx != nullptr) {
		*dx = x;
	}
	if (dy != nullptr) {
		*dy = y;
	}
	if (dz != nullptr) {
		*dz = z;
	}
}

void MapViewport::requestMove(const QPointF& planeDelta)
{
	double dx = 0.0;
	double dy = 0.0;
	double dz = 0.0;
	planeDeltaToWorld(planeDelta, &dx, &dy, &dz);
	if (dx == 0.0 && dy == 0.0 && dz == 0.0) {
		return;
	}
	Q_EMIT moveRequested(dx, dy, dz);
}

void MapViewport::beginDrag(const QPointF& viewPoint)
{
	m_pressArmed = false;
	if (!m_hasDocument || m_selection.isEmpty()) {
		return;
	}
	m_dragging = true;
	m_dragAnchorPlane = viewToWorld(m_pressViewPoint);
	m_dragCurrentPlane = viewToWorld(viewPoint);
	Q_EMIT hoverChanged(dragSummary());
	update();
}

void MapViewport::updateDrag(const QPointF& viewPoint)
{
	const QPointF previous = snappedPlaneDelta();
	m_dragCurrentPlane = viewToWorld(viewPoint);
	const QPointF current = snappedPlaneDelta();
	Q_EMIT hoverChanged(dragSummary());
	if (current != previous) {
		update();
	}
}

void MapViewport::commitDrag()
{
	const QPointF delta = snappedPlaneDelta();
	m_dragging = false;
	m_dragAnchorPlane = QPointF();
	m_dragCurrentPlane = QPointF();
	// The document belongs to the owner: the widget previewed the move and now
	// asks for exactly one edit, so a multi-object drag is one undo step.
	requestMove(delta);
	Q_EMIT hoverChanged(hoverSummary());
	update();
}

void MapViewport::cancelDrag()
{
	if (!m_dragging && !m_pressArmed) {
		return;
	}
	m_dragging = false;
	m_pressArmed = false;
	m_dragAnchorPlane = QPointF();
	m_dragCurrentPlane = QPointF();
	// Nothing was committed, so the objects are still where they started and
	// dropping the preview is the whole of the undo.
	Q_EMIT hoverChanged(hoverSummary());
	update();
}

bool MapViewport::isResizing() const
{
	return m_resizing;
}

void MapViewport::setClipMode(bool enabled)
{
	if (m_clipMode == enabled) {
		return;
	}
	cancelInteraction();
	m_clipMode = enabled;
	m_clipDrawing = false;
	m_hasClipLine = false;
	m_hoverHandle = 0;
	if (enabled) {
		setCursor(Qt::CrossCursor);
	} else {
		unsetCursor();
	}
	setAccessibleDescription(accessibleSummary());
	Q_EMIT hoverChanged(enabled ? clipSummary() : hoverSummary());
	Q_EMIT clipModeChanged(enabled);
	update();
}

bool MapViewport::clipMode() const
{
	return m_clipMode;
}

void MapViewport::setClipKeep(LevelMapClipKeep keep)
{
	m_clipKeep = keep;
	if (m_clipMode) {
		Q_EMIT hoverChanged(clipSummary());
	}
	update();
}

LevelMapClipKeep MapViewport::clipKeep() const
{
	return m_clipKeep;
}

bool MapViewport::hasClipLine() const
{
	return m_hasClipLine;
}

void MapViewport::setClipLine(const QPointF& from, const QPointF& to)
{
	m_clipFrom = from;
	m_clipTo = to;
	m_clipDrawing = false;
	m_hasClipLine = from != to;
	update();
}

void MapViewport::clearClipLine()
{
	m_clipDrawing = false;
	m_hasClipLine = false;
	update();
}

bool MapViewport::clipPlanePoints(LevelMapVec3* a, LevelMapVec3* b, LevelMapVec3* c) const
{
	if (!m_hasClipLine) {
		return false;
	}
	// The drawing plane's axes put back in the world, and a step toward the
	// viewer: +z from the top, -y from the front, +x from the side. Drawn
	// direction x toward-viewer is then the right of the line on screen.
	const auto world = [this](const QPointF& point) {
		switch (m_projection) {
		case MapViewportProjection::TopXY:
			return LevelMapVec3 {point.x(), point.y(), 0.0, true};
		case MapViewportProjection::FrontXZ:
			return LevelMapVec3 {point.x(), 0.0, point.y(), true};
		case MapViewportProjection::SideZY:
			return LevelMapVec3 {0.0, point.x(), point.y(), true};
		}
		return LevelMapVec3 {point.x(), point.y(), 0.0, true};
	};
	const double step = 64.0;
	LevelMapVec3 toward = world(m_clipFrom);
	switch (m_projection) {
	case MapViewportProjection::TopXY:
		toward.z += step;
		break;
	case MapViewportProjection::FrontXZ:
		toward.y -= step;
		break;
	case MapViewportProjection::SideZY:
		toward.x += step;
		break;
	}
	if (a != nullptr) {
		*a = world(m_clipFrom);
	}
	if (b != nullptr) {
		*b = world(m_clipTo);
	}
	if (c != nullptr) {
		*c = toward;
	}
	return true;
}

void MapViewport::dragEnterEvent(QDragEnterEvent* event)
{
	// A Doom map is placed on in plan: the other views show heights a thing
	// does not have, so they do not take a drop.
	const bool placeable = !documentIsDoom(m_document) || m_projection == MapViewportProjection::TopXY;
	if (m_hasDocument && placeable && event->mimeData()->hasFormat(QString::fromLatin1(kMapPaletteMimeType))) {
		m_dropActive = true;
		m_dropPoint = event->position();
		// Always a copy: the palette keeps what it offers.
		event->setDropAction(Qt::CopyAction);
		event->accept();
		update();
		return;
	}
	if (m_hasDocument && !placeable && event->mimeData()->hasFormat(QString::fromLatin1(kMapPaletteMimeType))) {
		Q_EMIT hoverChanged(tr("Things go onto a Doom map in the Top view."));
	}
	QWidget::dragEnterEvent(event);
}

void MapViewport::dragMoveEvent(QDragMoveEvent* event)
{
	if (!m_dropActive) {
		QWidget::dragMoveEvent(event);
		return;
	}
	m_dropPoint = event->position();
	event->setDropAction(Qt::CopyAction);
	event->accept();
	Q_EMIT hoverChanged(tr("Drop to place it at %1.").arg(planePointText(m_projection, snappedPlanePoint(m_dropPoint))));
	update();
}

void MapViewport::dragLeaveEvent(QDragLeaveEvent* event)
{
	m_dropActive = false;
	update();
	QWidget::dragLeaveEvent(event);
}

void MapViewport::dropEvent(QDropEvent* event)
{
	const QString payload = QString::fromUtf8(event->mimeData()->data(QString::fromLatin1(kMapPaletteMimeType)));
	m_dropActive = false;
	update();
	if (!m_hasDocument || payload.isEmpty()) {
		QWidget::dropEvent(event);
		return;
	}
	event->setDropAction(Qt::CopyAction);
	event->accept();
	Q_EMIT paletteDropped(payload, event->position());
}

QPointF MapViewport::viewPointFor(double x, double y) const
{
	return worldToView(x, y);
}

int MapViewport::sectorAt(const QPointF& viewPoint) const
{
	if (m_projection != MapViewportProjection::TopXY || !documentIsDoom(m_document)) {
		return -1;
	}
	const QPointF world = viewToWorld(viewPoint);
	int found = -1;
	double foundArea = 0.0;
	for (const DoomSectorOutline& outline : m_sectorOutlines) {
		if (isHidden(LevelMapSelectionKind::DoomSector, outline.sectorId)) { continue; }
		if (!outline.bounds.contains(world)) {
			continue;
		}
		bool inside = false;
		for (const QPolygonF& loop : outline.loops) {
			if (loop.containsPoint(world, Qt::OddEvenFill)) {
				inside = !inside;
			}
		}
		// Where outlines overlap, the smaller sector is the one the pointer
		// means: a room drawn inside another.
		const double area = outline.bounds.width() * outline.bounds.height();
		if (inside && (found < 0 || area < foundArea)) {
			found = outline.sectorId;
			foundArea = area;
		}
	}
	return found;
}

MapViewportHit MapViewport::hitOrSectorAt(const QPointF& viewPoint) const
{
	MapViewportHit hit = hitTest(viewPoint);
	if (hit.kind == LevelMapSelectionKind::None) {
		if (const int sector = sectorAt(viewPoint); sector >= 0) {
			hit.kind = LevelMapSelectionKind::DoomSector;
			hit.objectId = sector;
			hit.label = objectLabelText(m_document, hit.kind, sector);
		}
	}
	return hit;
}

void MapViewport::setDrawMode(bool enabled)
{
	if (m_drawMode == enabled) {
		return;
	}
	cancelInteraction();
	m_drawMode = enabled;
	m_drawCorners.clear();
	m_hasDrawHover = false;
	m_hoverHandle = 0;
	// Clicks draw rather than pick, so no object is marked as under the
	// pointer while the mode is on.
	m_hover = MapViewportHit();
	if (enabled) {
		setCursor(Qt::CrossCursor);
	} else {
		unsetCursor();
	}
	setAccessibleDescription(accessibleSummary());
	Q_EMIT hoverChanged(enabled ? drawSummary() : hoverSummary());
	Q_EMIT drawModeChanged(enabled);
	update();
}

bool MapViewport::drawMode() const
{
	return m_drawMode;
}

QVector<QPointF> MapViewport::drawCorners() const
{
	return m_drawCorners;
}

void MapViewport::clearDrawCorners()
{
	m_drawCorners.clear();
	setAccessibleDescription(accessibleSummary());
	if (m_drawMode) {
		Q_EMIT hoverChanged(drawSummary());
	}
	update();
}

QPointF MapViewport::drawPointAt(const QPointF& viewPoint) const
{
	// A vertex near the pointer wins, so a shape joins the map's lines, and
	// the first corner too, so a click there closes the shape.
	constexpr double kSnapPixels = 8.0;
	double nearest = kSnapPixels;
	std::optional<QPointF> snapped;
	const auto consider = [&](const QPointF& world) {
		const QPointF view = worldToView(world.x(), world.y());
		const double distance = std::hypot(view.x() - viewPoint.x(), view.y() - viewPoint.y());
		if (distance <= nearest) {
			nearest = distance;
			snapped = world;
		}
	};
	for (const LevelMapDoomVertex& vertex : m_document.doomVertices) {
		if (isHidden(LevelMapSelectionKind::DoomVertex, vertex.id)) { continue; }
		consider(QPointF(vertex.x, vertex.y));
	}
	if (!m_drawCorners.isEmpty()) {
		consider(m_drawCorners.first());
	}
	if (snapped) {
		return *snapped;
	}
	const QPointF point = snappedPlanePoint(viewPoint);
	return {std::round(point.x()), std::round(point.y())};
}

void MapViewport::finishDraw()
{
	if (m_drawCorners.size() < 3) {
		Q_EMIT hoverChanged(tr("Draw Sector: a sector needs three corners or more."));
		return;
	}
	Q_EMIT sectorDrawRequested(m_drawCorners);
}

QString MapViewport::drawSummary() const
{
	const int corners = static_cast<int>(m_drawCorners.size());
	if (corners == 0) {
		return tr("Draw Sector: click to put the first corner down; Escape leaves.");
	}
	if (m_hasDrawHover && corners >= 3 && m_drawHover == m_drawCorners.first()) {
		return tr("Draw Sector: click the first corner to close the shape of %n corner(s).", nullptr, corners);
	}
	if (m_hasDrawHover) {
		const QPointF last = m_drawCorners.last();
		const double length = std::hypot(m_drawHover.x() - last.x(), m_drawHover.y() - last.y());
		return tr("Draw Sector: %n corner(s); the next edge runs %1 units to %2. Enter closes the shape, Backspace takes a corner back.", nullptr,
			corners)
			.arg(length, 0, 'f', 0)
			.arg(planePointText(m_projection, m_drawHover));
	}
	return tr("Draw Sector: %n corner(s); click the first corner or press Enter to close the shape.", nullptr, corners);
}

void MapViewport::paintDraw(QPainter& painter, const Palette& palette) const
{
	if (!m_drawMode || (m_drawCorners.isEmpty() && !m_hasDrawHover)) {
		return;
	}
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing, true);
	const QColor ink = palette.selection;
	const double width = m_highContrast ? 2.5 : 2.0;
	QPolygonF path;
	for (const QPointF& corner : m_drawCorners) {
		path << worldToView(corner.x(), corner.y());
	}
	const QPointF hover = worldToView(m_drawHover.x(), m_drawHover.y());
	if (path.size() >= 2) {
		// The area the sector would take, faintly, so the shape reads at once.
		QPolygonF area = path;
		if (m_hasDrawHover) {
			area << hover;
		}
		QColor wash = ink;
		wash.setAlpha(m_highContrast ? 60 : 32);
		painter.setPen(Qt::NoPen);
		painter.setBrush(wash);
		painter.drawPolygon(area);
	}
	painter.setBrush(Qt::NoBrush);
	painter.setPen(QPen(ink, width));
	painter.drawPolyline(path);
	if (m_hasDrawHover && !path.isEmpty()) {
		painter.setPen(QPen(ink, width, Qt::DashLine));
		painter.drawLine(path.last(), hover);
		if (path.size() >= 2) {
			// The edge that would close the shape.
			QColor closing = ink;
			closing.setAlpha(m_highContrast ? 200 : 120);
			painter.setPen(QPen(closing, 1.0, Qt::DotLine));
			painter.drawLine(hover, path.first());
		}
	}
	painter.setPen(QPen(ink, 1.5));
	painter.setBrush(ink);
	for (const QPointF& corner : path) {
		painter.drawRect(QRectF(corner - QPointF(3.0, 3.0), QSizeF(6.0, 6.0)));
	}
	painter.setBrush(Qt::NoBrush);
	if (!path.isEmpty()) {
		// The first corner is ringed: a click there closes the shape.
		painter.drawEllipse(path.first(), 7.0, 7.0);
	}
	if (m_hasDrawHover) {
		painter.drawEllipse(hover, 4.0, 4.0);
		if (!m_drawCorners.isEmpty()) {
			const QPointF last = m_drawCorners.last();
			const double length = std::hypot(m_drawHover.x() - last.x(), m_drawHover.y() - last.y());
			painter.setPen(QPen(palette.text, 1.0));
			painter.drawText(hover + QPointF(10.0, -10.0), tr("%1 units").arg(length, 0, 'f', 0));
		}
	}
	painter.restore();
}

QPointF MapViewport::snappedPlanePoint(const QPointF& viewPoint) const
{
	const QPointF point = viewToWorld(viewPoint);
	if (!m_snapToGrid || m_gridSize <= 0) {
		return point;
	}
	const double grid = static_cast<double>(m_gridSize);
	return QPointF(snapLevelMapCoordinate(point.x(), grid), snapLevelMapCoordinate(point.y(), grid));
}

QString MapViewport::clipSummary() const
{
	if (m_clipDrawing) {
		return tr("Clip line from %1 to %2").arg(planePointText(m_projection, m_clipFrom), planePointText(m_projection, m_clipTo));
	}
	if (!m_hasClipLine) {
		return tr("Clip mode: drag a line across the brushes to cut; Escape leaves.");
	}
	return m_clipKeep == LevelMapClipKeep::Both ? tr("Clip: splits the brushes in two; Tab keeps one side; Enter cuts.")
						 : tr("Clip: cuts away the hatched side; Tab switches sides or keeps both; Enter cuts.");
}

void MapViewport::paintClip(QPainter& painter, const Palette& palette) const
{
	if (!m_clipMode || (!m_hasClipLine && !m_clipDrawing)) {
		return;
	}
	const QPointF from = worldToView(m_clipFrom.x(), m_clipFrom.y());
	const QPointF to = worldToView(m_clipTo.x(), m_clipTo.y());
	painter.save();
	painter.setPen(QPen(palette.warning, 1.5));
	painter.setBrush(palette.warning);
	painter.drawEllipse(from, 4.0, 4.0);
	const QPointF travel = to - from;
	const double length = std::hypot(travel.x(), travel.y());
	if (length < 1.0) {
		painter.restore();
		return;
	}
	const QPointF direction = travel / length;
	// Right of the line as drawn, on a screen whose y points down.
	const QPointF right(-direction.y(), direction.x());
	const double reach = std::hypot(static_cast<double>(width()), static_cast<double>(height())) * 2.0;
	const QPointF start = from - direction * reach;
	const QPointF end = from + direction * reach;
	if (m_clipKeep != LevelMapClipKeep::Both) {
		// The front of the plane is the right of the line; keeping the back
		// cuts the right away. Hatching marks it without relying on colour.
		const QPointF away = (m_clipKeep == LevelMapClipKeep::Back ? right : -right) * reach;
		const QPolygonF removed {start, end, end + away, start + away};
		QColor wash = palette.warning;
		wash.setAlpha(m_highContrast ? 70 : 38);
		painter.setPen(Qt::NoPen);
		painter.setBrush(wash);
		painter.drawPolygon(removed);
		QColor hatch = palette.warning;
		hatch.setAlpha(m_highContrast ? 200 : 120);
		painter.setBrush(QBrush(hatch, Qt::BDiagPattern));
		painter.drawPolygon(removed);
	}
	painter.setBrush(Qt::NoBrush);
	painter.setPen(QPen(palette.warning, m_highContrast ? 2.5 : 2.0, m_clipDrawing ? Qt::DashLine : Qt::SolidLine));
	painter.drawLine(start, end);
	painter.setBrush(palette.warning);
	painter.drawEllipse(from, 4.0, 4.0);
	painter.drawEllipse(to, 4.0, 4.0);
	painter.setPen(QPen(palette.text, 1.0));
	painter.drawText(to + QPointF(10.0, -10.0),
		m_clipKeep == LevelMapClipKeep::Both ? tr("Splits in two") : tr("Cuts away the hatched side"));
	painter.restore();
}

bool MapViewport::resizeBox(QRectF* plane, LevelMapVec3* mins, LevelMapVec3* maxs) const
{
	if (!m_hasDocument || m_selection.isEmpty()) {
		return false;
	}
	if (documentIsDoom(m_document) && !m_document.doomUdmf) {
		// Only things take part in a binary Doom resize, so a box drawn around
		// linedefs or sectors would promise what the edit cannot do.
		for (const LevelMapSelectionRef& ref : m_selection) {
			if (ref.kind != LevelMapSelectionKind::DoomThing) {
				return false;
			}
		}
	}
	// The whole document, hidden objects included: a resize moves an
	// entity's hidden brushes too, so the box has to take them in.
	LevelMapVec3 low;
	LevelMapVec3 high;
	if (!selectionTransformBounds(&low, &high)) {
		return false;
	}
	const QRectF box = mapViewportBounds(m_projection, low, high);
	if (box.width() <= 0.0 || box.height() <= 0.0) {
		return false;
	}
	if (plane != nullptr) {
		*plane = box;
	}
	if (mins != nullptr) {
		*mins = low;
	}
	if (maxs != nullptr) {
		*maxs = high;
	}
	return true;
}

bool MapViewport::handlesBox(QRectF* box) const
{
	if (m_resizing) {
		*box = resizedPlaneBox();
		return true;
	}
	if (m_clipMode || m_dragging || m_banding || !resizeBox(box)) {
		return false;
	}
	const QRectF view = QRectF(worldToView(box->left(), box->bottom()), worldToView(box->right(), box->top())).normalized();
	return view.width() >= kResizeHandleMinBoxPixels && view.height() >= kResizeHandleMinBoxPixels;
}

QPointF MapViewport::handleViewPosition(const QRectF& box, int edges) const
{
	const double x = (edges & ResizeMinHorizontal) != 0 ? box.left()
		: (edges & ResizeMaxHorizontal) != 0	  ? box.right()
							  : box.center().x();
	const double y = (edges & ResizeMinVertical) != 0 ? box.top()
		: (edges & ResizeMaxVertical) != 0	? box.bottom()
							: box.center().y();
	return worldToView(x, y);
}

bool MapViewport::hasResizeHandles() const
{
	QRectF box;
	return handlesBox(&box);
}

QPointF MapViewport::resizeHandlePosition(int edges) const
{
	QRectF box;
	return handlesBox(&box) ? handleViewPosition(box, edges) : QPointF();
}

int MapViewport::resizeHandleAt(const QPointF& viewPoint) const
{
	QRectF box;
	if (m_resizing || !handlesBox(&box)) {
		return 0;
	}
	// Corners first: where a corner and an edge handle crowd together, the
	// corner is the one the pointer is aimed at.
	static constexpr int kHandles[] = {
		ResizeMinHorizontal | ResizeMinVertical,
		ResizeMaxHorizontal | ResizeMinVertical,
		ResizeMinHorizontal | ResizeMaxVertical,
		ResizeMaxHorizontal | ResizeMaxVertical,
		ResizeMinHorizontal,
		ResizeMaxHorizontal,
		ResizeMinVertical,
		ResizeMaxVertical,
	};
	for (const int edges : kHandles) {
		const QPointF handle = handleViewPosition(box, edges);
		if (std::abs(handle.x() - viewPoint.x()) <= kResizeHandleGrab && std::abs(handle.y() - viewPoint.y()) <= kResizeHandleGrab) {
			return edges;
		}
	}
	return 0;
}

QRectF MapViewport::resizedPlaneBox() const
{
	const QRectF& from = m_resizeFromPlane;
	const QPointF travel = m_resizeCurrentPlane - m_resizePressPlane;
	if (travel.isNull()) {
		// A press that has not moved changes nothing, even on a box whose
		// edges are off the grid.
		return from;
	}
	double left = from.left();
	double right = from.right();
	double low = from.top();
	double high = from.bottom();
	const double grid = m_gridSize > 0 ? static_cast<double>(m_gridSize) : 1.0;
	const auto snap = [this, grid](double value) {
		return m_snapToGrid ? snapLevelMapCoordinate(value, grid) : value;
	};
	// A moved edge travels with the pointer, landing on the grid while
	// snapping is on, and stops short of the opposite edge, so a box never
	// turns inside out or vanishes. A box already thinner than the grid may
	// keep its size.
	const auto smallest = [this, grid](double size) {
		return m_snapToGrid ? std::min(grid, size) : std::min(1.0, size);
	};
	const double minWidth = smallest(from.width());
	const double minHeight = smallest(from.height());
	if ((m_resizeEdges & ResizeMinHorizontal) != 0) {
		left = std::min(snap(from.left() + travel.x()), right - minWidth);
	} else if ((m_resizeEdges & ResizeMaxHorizontal) != 0) {
		right = std::max(snap(from.right() + travel.x()), left + minWidth);
	}
	if ((m_resizeEdges & ResizeMinVertical) != 0) {
		low = std::min(snap(from.top() + travel.y()), high - minHeight);
	} else if ((m_resizeEdges & ResizeMaxVertical) != 0) {
		high = std::max(snap(from.bottom() + travel.y()), low + minHeight);
	}
	return minMaxRect(left, low, right, high);
}

void MapViewport::beginResize(int edges, const QPointF& viewPoint)
{
	QRectF box;
	if (!resizeBox(&box, &m_resizeFromMins, &m_resizeFromMaxs)) {
		return;
	}
	m_pressArmed = false;
	m_resizing = true;
	m_resizeEdges = edges;
	m_resizeFromPlane = box;
	m_resizePressPlane = viewToWorld(viewPoint);
	m_resizeCurrentPlane = m_resizePressPlane;
	Q_EMIT hoverChanged(resizeSummary());
	update();
}

void MapViewport::updateResize(const QPointF& viewPoint)
{
	const QRectF previous = resizedPlaneBox();
	m_resizeCurrentPlane = viewToWorld(viewPoint);
	Q_EMIT hoverChanged(resizeSummary());
	if (resizedPlaneBox() != previous) {
		update();
	}
}

void MapViewport::commitResize()
{
	const QRectF box = resizedPlaneBox();
	LevelMapVec3 mins = m_resizeFromMins;
	LevelMapVec3 maxs = m_resizeFromMaxs;
	m_resizing = false;
	m_resizeEdges = 0;
	switch (m_projection) {
	case MapViewportProjection::TopXY:
		mins.x = box.left();
		maxs.x = box.right();
		mins.y = box.top();
		maxs.y = box.bottom();
		break;
	case MapViewportProjection::FrontXZ:
		mins.x = box.left();
		maxs.x = box.right();
		mins.z = box.top();
		maxs.z = box.bottom();
		break;
	case MapViewportProjection::SideZY:
		mins.y = box.left();
		maxs.y = box.right();
		mins.z = box.top();
		maxs.z = box.bottom();
		break;
	}
	// The owner makes the edit, as for a move, so the resize is one undo step.
	if (box != m_resizeFromPlane) {
		Q_EMIT resizeRequested(mins, maxs);
	}
	Q_EMIT hoverChanged(hoverSummary());
	update();
}

QString MapViewport::resizeSummary() const
{
	const QRectF box = resizedPlaneBox();
	return tr("Resizing %n object(s) to %1 %2 by %3 %4", nullptr, static_cast<int>(m_selection.size()))
		.arg(planeAxisLetters(m_projection, false))
		.arg(box.width(), 0, 'g', 8)
		.arg(planeAxisLetters(m_projection, true))
		.arg(box.height(), 0, 'g', 8);
}

QString MapViewport::resizeHandleSummary(int edges) const
{
	const bool horizontal = (edges & (ResizeMinHorizontal | ResizeMaxHorizontal)) != 0;
	const bool vertical = (edges & (ResizeMinVertical | ResizeMaxVertical)) != 0;
	if (horizontal && vertical) {
		return tr("Resize handle: drag to resize the selection along %1 and %2")
			.arg(planeAxisLetters(m_projection, false), planeAxisLetters(m_projection, true));
	}
	return tr("Resize handle: drag to resize the selection along %1").arg(planeAxisLetters(m_projection, vertical));
}

void MapViewport::updateResizeCursor(int edges)
{
	if (edges == m_hoverHandle) {
		return;
	}
	const bool handCursor = cursor().shape() == Qt::OpenHandCursor || cursor().shape() == Qt::ClosedHandCursor;
	m_hoverHandle = edges;
	if (handCursor) {
		return;
	}
	if (edges == 0) {
		unsetCursor();
		return;
	}
	const bool horizontal = (edges & (ResizeMinHorizontal | ResizeMaxHorizontal)) != 0;
	const bool vertical = (edges & (ResizeMinVertical | ResizeMaxVertical)) != 0;
	if (horizontal && vertical) {
		// Up the screen is up the vertical axis, so the minimum-horizontal,
		// maximum-vertical corner is the top-left one.
		const bool falling = ((edges & ResizeMinHorizontal) != 0) == ((edges & ResizeMaxVertical) != 0);
		setCursor(falling ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
		return;
	}
	setCursor(horizontal ? Qt::SizeHorCursor : Qt::SizeVerCursor);
}

void MapViewport::paintResizeHandles(QPainter& painter, const Palette& palette) const
{
	PaintMeasurement measured(m_measurePainting,&m_paintStatistics.resizeHandlesNs);
	QRectF box;
	if (!handlesBox(&box)) {
		return;
	}
	static constexpr int kHandles[] = {
		ResizeMinHorizontal | ResizeMinVertical,
		ResizeMinVertical,
		ResizeMaxHorizontal | ResizeMinVertical,
		ResizeMaxHorizontal,
		ResizeMaxHorizontal | ResizeMaxVertical,
		ResizeMaxVertical,
		ResizeMinHorizontal | ResizeMaxVertical,
		ResizeMinHorizontal,
	};
	const double size = m_highContrast ? kResizeHandleSize + 2.0 : kResizeHandleSize;
	// Filled squares with a dark rim read on any background and are shaped
	// unlike the ring that marks the primary object.
	painter.setPen(QPen(palette.background, 1.2));
	painter.setBrush(QBrush(palette.selection));
	for (const int edges : kHandles) {
		const QPointF handle = handleViewPosition(box, edges);
		const bool active = m_resizing ? (edges == m_resizeEdges) : (edges == m_hoverHandle);
		const double side = active ? size + 3.0 : size;
		painter.drawRect(QRectF(handle.x() - side * 0.5, handle.y() - side * 0.5, side, side));
	}
	painter.setBrush(Qt::NoBrush);
}

void MapViewport::paintResizePreview(QPainter& painter, const Palette& palette) const
{
	const QRectF from = m_resizeFromPlane;
	const QRectF box = resizedPlaneBox();
	if (from.width() <= 0.0 || from.height() <= 0.0) {
		return;
	}
	const double scaleX = box.width() / from.width();
	const double scaleY = box.height() / from.height();
	const auto map = [&](const QPointF& point) {
		return worldToView(box.left() + (point.x() - from.left()) * scaleX, box.top() + (point.y() - from.top()) * scaleY);
	};
	QPen ghostPen(palette.hover, m_highContrast ? 2.0 : 1.4, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin);
	ghostPen.setDashPattern({4.0, 3.0});
	painter.setBrush(Qt::NoBrush);
	painter.setPen(ghostPen);
	QRectF bounds;
	int drawn = 0;
	for (const LevelMapSelectionRef& ref : m_selection) {
		if (drawn >= kMaxPreviewOutlines) {
			break;
		}
		if (!objectWorldBounds(m_document, m_sectorOutlines, m_brushGeometry, m_sceneIndex, m_projection, ref.kind, ref.objectId, &bounds)) {
			continue;
		}
		++drawn;
		const QRectF ghost = QRectF(map(bounds.topLeft()), map(bounds.bottomRight())).normalized();
		if (ghost.width() < 3.0 && ghost.height() < 3.0) {
			const QPointF center = ghost.center();
			painter.drawLine(QPointF(center.x() - 6.0, center.y()), QPointF(center.x() + 6.0, center.y()));
			painter.drawLine(QPointF(center.x(), center.y() - 6.0), QPointF(center.x(), center.y() + 6.0));
			continue;
		}
		painter.drawRect(ghost);
	}
	// The new box itself, solid, with its size beside it.
	const QRectF view = QRectF(worldToView(box.left(), box.bottom()), worldToView(box.right(), box.top())).normalized();
	painter.setPen(QPen(palette.selection, m_highContrast ? 2.0 : 1.5, Qt::SolidLine));
	painter.drawRect(view);
	painter.setPen(QPen(palette.text, 1.0));
	painter.drawText(QPointF(view.left(), view.top() - 8.0),
		tr("%1 %2 by %3 %4")
			.arg(planeAxisLetters(m_projection, false))
			.arg(box.width(), 0, 'g', 8)
			.arg(planeAxisLetters(m_projection, true))
			.arg(box.height(), 0, 'g', 8));
}

void MapViewport::paintSelectionMarkers(QPainter& painter, const Palette& palette, const ViewportHudLayout& hud) const
{
	PaintMeasurement measured(m_measurePainting,&m_paintStatistics.selectionMarkersNs);
	if (m_selection.isEmpty()) {
		return;
	}
	painter.setBrush(Qt::NoBrush);
	// The primary, crosshair and label stay current even while member images
	// are pending. Both image and ordinary paths share the exact same member set.
	if (m_asyncOverlays) {
		paintOverlayImage(painter,m_overlayFrame.members,m_overlayFrame.key.view);
	} else {
		MapViewportOverlayKey key; key.projection = m_projection;
		key.view.viewport = size(); key.view.center = m_worldCenter; key.view.zoom = m_zoom;
		QVector<QPointF> centers;
		mapViewportMemberCenters(overlayRequest(key),&centers);
		m_selectionRingCache.draw(painter,centers,palette.selection,m_highContrast ? 2.0 : 1.4,8.0);
	}
	QPointF world;

	if (m_selectionKind == LevelMapSelectionKind::None
		|| !mapViewportObjectPoint(m_document, m_sectorOutlines, m_brushGeometry, m_sceneIndex, m_projection, m_selectionKind,
			m_selectedObjectId, &world)) {
		return;
	}
	const QPointF view = worldToView(world.x(), world.y());
	// Ring plus crosshair: shape, not colour, marks the selection. Reduced
	// motion prefers a solid ring, because fine dashes shimmer while panning.
	painter.setPen(QPen(palette.selection, 2.0, m_reducedMotion ? Qt::SolidLine : Qt::DashLine));
	painter.drawEllipse(view, 11.0, 11.0);
	painter.setPen(QPen(palette.selection, 1.6));
	painter.drawLine(QPointF(view.x() - 16.0, view.y()), QPointF(view.x() - 4.0, view.y()));
	painter.drawLine(QPointF(view.x() + 4.0, view.y()), QPointF(view.x() + 16.0, view.y()));
	painter.drawLine(QPointF(view.x(), view.y() - 16.0), QPointF(view.x(), view.y() - 4.0));
	painter.drawLine(QPointF(view.x(), view.y() + 4.0), QPointF(view.x(), view.y() + 16.0));
	const auto label = layoutViewportLabel(rect(),view,QFontMetricsF(painter.font(),painter.device()),
		objectLabelText(m_document,m_selectionKind,m_selectedObjectId),layoutDirection(),hud);
	paintViewportLabel(painter,label,layoutDirection(),palette.text,palette.background,m_highContrast);
}

void MapViewport::paintDragPreview(QPainter& painter, const Palette& palette) const
{
	const QPointF delta = snappedPlaneDelta();
	QPen ghostPen(palette.hover, m_highContrast ? 2.0 : 1.4, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin);
	ghostPen.setDashPattern({4.0, 3.0});
	painter.setBrush(Qt::NoBrush);
	painter.setPen(ghostPen);

	QRectF bounds;
	int drawn = 0;
	for (const LevelMapSelectionRef& ref : m_selection) {
		if (drawn >= kMaxPreviewOutlines) {
			break;
		}
		if (!objectWorldBounds(m_document, m_sectorOutlines, m_brushGeometry, m_sceneIndex, m_projection, ref.kind,
			    ref.objectId, &bounds)) {
			continue;
		}
		++drawn;
		const QPointF a = worldToView(bounds.left() + delta.x(), bounds.top() + delta.y());
		const QPointF b = worldToView(bounds.right() + delta.x(), bounds.bottom() + delta.y());
		const QRectF ghost = QRectF(a, b).normalized();
		if (ghost.width() < 3.0 && ghost.height() < 3.0) {
			// Point-like objects get a cross instead of an invisible rectangle.
			const QPointF center = ghost.center();
			painter.drawLine(QPointF(center.x() - 6.0, center.y()), QPointF(center.x() + 6.0, center.y()));
			painter.drawLine(QPointF(center.x(), center.y() - 6.0), QPointF(center.x(), center.y() + 6.0));
			continue;
		}
		painter.drawRect(ghost);
	}

	// The move vector itself, drawn from where the drag started.
	const QPointF from = worldToView(m_dragAnchorPlane.x(), m_dragAnchorPlane.y());
	const QPointF to = worldToView(m_dragAnchorPlane.x() + delta.x(), m_dragAnchorPlane.y() + delta.y());
	painter.setPen(QPen(palette.selection, 1.8, Qt::SolidLine, Qt::RoundCap));
	painter.drawLine(from, to);
	painter.drawEllipse(to, 3.0, 3.0);
	painter.setPen(QPen(palette.text, 1.0));
	painter.drawText(QPointF(to.x() + 12.0, to.y() - 10.0), planePointText(m_projection, delta));
}

void MapViewport::paintRubberBand(QPainter& painter, const Palette& palette) const
{
	const QRectF band = QRectF(m_bandAnchorView, m_bandCurrentView).normalized();
	QColor fill = palette.selection;
	fill.setAlpha(m_highContrast ? 72 : 48);
	painter.setBrush(QBrush(fill));
	QPen bandPen(palette.selection, m_highContrast ? 2.0 : 1.4, Qt::DashLine, Qt::FlatCap, Qt::MiterJoin);
	bandPen.setDashPattern({5.0, 4.0});
	painter.setPen(bandPen);
	painter.drawRect(band);
	painter.setBrush(Qt::NoBrush);
}

void MapViewport::paintGrid(QPainter& painter, const Palette& palette) const
{
	const auto device = viewportImageDevice(painter.deviceTransform());
	MapGridView view; view.viewport = size(); view.center = m_worldCenter;
	view.zoom = m_zoom; view.units = m_gridSize; view.pixelRatio = device.pixelRatio; view.pixelPhase = device.pixelPhase;
	view.minor = palette.gridMinor.rgba(); view.major = palette.gridMajor.rgba(); view.axis = palette.axis.rgba();
	if (m_asyncOverlays) {
		const auto& previous = m_gridFrame.view;
		if (previous.units == view.units && previous.minor == view.minor && previous.major == view.major
			&& previous.axis == view.axis && previous.pixelRatio == view.pixelRatio) { paintOverlayImage(painter,m_gridFrame.image,previous); }
		return;
	}
	if (!m_overlayRenderFailed && renderMapGrid(view,&m_gridFrame)) {
		paintOverlayImage(painter,m_gridFrame.image,view); return;
	}
	const auto lines = mapGridLines(view);
	const QColor colors[] = {palette.gridMinor,palette.gridMajor,palette.axis};
	painter.setBrush(Qt::NoBrush);
	for (int pass = 0; pass < 3; ++pass) {
		painter.setPen(QPen(colors[pass],pass == 2 ? 2.0 : 1.0));
		painter.drawLines(lines[pass]);
	}
}

void MapViewport::paintDoom(QPainter& painter, const Palette& palette) const
{
	const QPointF topLeft = viewToWorld(QPointF(0.0, 0.0));
	const QPointF bottomRight = viewToWorld(QPointF(width(), height()));
	const QRectF visible = minMaxRect(std::min(topLeft.x(), bottomRight.x()), std::min(topLeft.y(), bottomRight.y()),
		std::max(topLeft.x(), bottomRight.x()), std::max(topLeft.y(), bottomRight.y()));
	QPolygonF viewPolygon;
	int labelsDrawn = 0;

	// Sector fills only make sense in the plan view; the traced loops are XY
	// polygons (https://doomwiki.org/wiki/Sector).
	if (m_showSectorFill && m_projection == MapViewportProjection::TopXY) {
		painter.setPen(Qt::NoPen);
		for (const DoomSectorOutline& outline : m_sectorOutlines) {
		if (isHidden(LevelMapSelectionKind::DoomSector, outline.sectorId)) { continue; }
			if (!outline.bounds.isNull() && !rectsOverlap(outline.bounds, visible)) {
				continue;
			}
			const LevelMapDoomSector* sector = findById(m_document.doomSectors, outline.sectorId);
			const int light = sector != nullptr ? std::clamp(sector->lightLevel, 0, 255) : 128;
			const double mix = static_cast<double>(light) / 255.0;
			QColor fill(
				static_cast<int>(std::lround(palette.sectorDark.red() + (palette.sectorBright.red() - palette.sectorDark.red()) * mix)),
				static_cast<int>(std::lround(palette.sectorDark.green() + (palette.sectorBright.green() - palette.sectorDark.green()) * mix)),
				static_cast<int>(std::lround(palette.sectorDark.blue() + (palette.sectorBright.blue() - palette.sectorDark.blue()) * mix)));
			fill.setAlpha(m_highContrast ? 110 : 80);
			// An outline with open edges is hatched as well as tinted so the
			// problem is visible without relying on colour.
			painter.setBrush(outline.openEdgeCount > 0 ? QBrush(fill, Qt::FDiagPattern) : QBrush(fill));
			for (const QPolygonF& loop : outline.loops) {
				if (loop.size() < 3) {
					continue;
				}
				viewPolygon.clear();
				for (const QPointF& point : loop) {
					viewPolygon.append(worldToView(point.x(), point.y()));
				}
				painter.drawPolygon(viewPolygon);
			}
		}
		painter.setBrush(Qt::NoBrush);
	}

	// One-sided lines are solid and heavy (they are the map's walls); two-sided
	// lines are thin, so the distinction survives a monochrome print.
	const QPen solidPen(palette.oneSided, m_highContrast ? 2.4 : 1.8, Qt::SolidLine, Qt::RoundCap);
	const QPen twoSidedPen(palette.twoSided, 0.9, Qt::SolidLine, Qt::RoundCap);
	for (const LevelMapDoomLinedef& linedef : m_document.doomLinedefs) {
		if (isHidden(LevelMapSelectionKind::DoomLinedef, linedef.id)) { continue; }
		const LevelMapDoomVertex* start = findById(m_document.doomVertices, linedef.startVertex);
		const LevelMapDoomVertex* end = findById(m_document.doomVertices, linedef.endVertex);
		if (start == nullptr || end == nullptr) {
			continue;
		}
		const double height = m_projection == MapViewportProjection::TopXY
			? 0.0
			: mapViewportLineHeight(m_document, linedef);
		const QPointF a = mapViewportProjectPoint(m_projection, makeVec(start->x, start->y, height));
		const QPointF b = mapViewportProjectPoint(m_projection, makeVec(end->x, end->y, height));
		const QRectF segment = minMaxRect(std::min(a.x(), b.x()), std::min(a.y(), b.y()), std::max(a.x(), b.x()),
			std::max(a.y(), b.y()));
		if (!rectsOverlap(segment, visible)) {
			continue;
		}
		painter.setPen(doomLinedefIsTwoSided(linedef) ? twoSidedPen : solidPen);
		painter.drawLine(worldToView(a.x(), a.y()), worldToView(b.x(), b.y()));
	}

	if (m_showVertices) {
		painter.setPen(QPen(palette.vertex, 1.0));
		painter.setBrush(QBrush(palette.vertex));
		for (const LevelMapDoomVertex& vertex : m_document.doomVertices) {
		if (isHidden(LevelMapSelectionKind::DoomVertex, vertex.id)) { continue; }
			const QPointF point = mapViewportProjectPoint(m_projection, makeVec(vertex.x, vertex.y, 0.0));
			if (!visible.contains(point)) {
				continue;
			}
			const QPointF view = worldToView(point.x(), point.y());
			// Square marker: vertices never share a shape with things.
			painter.drawRect(QRectF(view.x() - 2.0, view.y() - 2.0, 4.0, 4.0));
		}
		painter.setBrush(Qt::NoBrush);
	}

	if (m_showThings) {
		const QPen thingPen(palette.thing, 1.4);
		painter.setPen(thingPen);
		painter.setBrush(Qt::NoBrush);
		for (const LevelMapDoomThing& thing : m_document.doomThings) {
			const QPointF point = mapViewportProjectPoint(m_projection, makeVec(thing.x, thing.y, 0.0));
			if (!visible.contains(point)) {
				continue;
			}
			const QPointF view = worldToView(point.x(), point.y());
			painter.drawEllipse(view, 4.0, 4.0);
			if (m_projection == MapViewportProjection::TopXY) {
				// Doom thing angles are degrees counter-clockwise from east
				// (https://doomwiki.org/wiki/Thing). The view Y axis is
				// flipped, hence the negated sine.
				const double radians = static_cast<double>(thing.angle) * kPi / 180.0;
				painter.drawLine(view,
					QPointF(view.x() + std::cos(radians) * 9.0, view.y() - std::sin(radians) * 9.0));
			}
			if (m_showLabels && labelsDrawn < kMaxLabels) {
				painter.setPen(QPen(palette.subtleText, 1.0));
				painter.drawText(QPointF(view.x() + 7.0, view.y() - 5.0), QString::number(thing.type));
				painter.setPen(thingPen);
				++labelsDrawn;
			}
		}
	}
}

void MapViewport::paintQuakeGeometry(QPainter& painter, const Palette& palette, int worldspawnId)
{
	const bool wasRendering = m_planRenderPending;
	const auto updateAccessibleState = [&] {
		if (wasRendering != m_planRenderPending) { setAccessibleDescription(accessibleSummary()); }
	};
	MapPlanRenderRequest request;
	request.sceneRevision = m_sceneRevision; request.selectionRevision = m_selectionRevision; request.projection = m_projection;
	request.worldspawnId = worldspawnId; request.document = m_document; request.brushes = m_brushGeometry; request.index = m_sceneIndex;
	request.selection = m_selection;
	auto& view = request.view;
	view.viewport = size(); view.center = m_worldCenter; view.zoom = m_zoom; view.highContrast = m_highContrast;
	const auto device = viewportImageDevice(painter.deviceTransform());
	view.pixelRatio = device.pixelRatio; view.pixelPhase = device.pixelPhase;
	view.world = palette.brush.rgba(); view.entity = palette.brushEntity.rgba(); view.invalid = palette.warning.rgba();
	view.selection = palette.selection.rgba(); view.patch = palette.patch.rgba();
	const bool sameRequest = m_planRequestedScene == m_sceneRevision && m_planRequestedSelection == m_selectionRevision
		&& sameMapPlanView(m_planRequestedView, view);
	const bool baseReady = !m_planWireFrame.image.isNull() && sameMapPlanView(m_planWireFrame, view);
	const bool selectionReady = m_selection.isEmpty() || (!m_selectionWireFrame.image.isNull() && sameMapPlanView(m_selectionWireFrame, view));
	if (baseReady && selectionReady) {
		m_planRenderPending = false;
	} else if (m_planRenderWeight > 4096 && supportedMapPlanFrame(view) && !(sameRequest && m_planRenderFailed)) {
		request.wires = m_planWires; request.wiresComputed = m_planWiresComputed;
		request.selectionWires = m_selectionWires; request.selectionWiresComputed = m_selectionWiresComputed;
		request.baseFrame = m_planWireFrame; request.selectionFrame = m_selectionWireFrame;
		m_planRequestedScene = m_sceneRevision; m_planRequestedSelection = m_selectionRevision; m_planRequestedView = view;
		m_planRenderPending = true; m_planRenderFailed = false;
		m_planRenderWorker->request(std::move(request));
	} else {
		// Small scenes draw immediately. Oversized/failed image allocations keep
		// the full ordinary path rather than lowering resolution or losing objects.
		if (supportedMapPlanFrame(view) && !m_planRenderFailed) {
			if (!m_planWiresComputed) {
				m_planWires = buildMapPlanWires(m_document,m_brushGeometry,m_sceneIndex,m_projection,worldspawnId); m_planWiresComputed = true;
			}
			if (!m_selectionWiresComputed && !m_selection.isEmpty()) {
				m_selectionWires = buildMapPlanSelectionWires(m_document,m_brushGeometry,m_sceneIndex,m_projection,m_selection); m_selectionWiresComputed = true;
			}
			const bool base = renderMapPlanFrame(request,false,m_planWires,&m_planWireFrame);
			const bool selected = m_selection.isEmpty() || renderMapPlanFrame(request,true,m_selectionWires,&m_selectionWireFrame);
			if (base && selected) { m_planRenderPending = false; }
			else {
				retirePlanRender(); paintMapPlanFallback(painter,request,false);
				if (!m_selection.isEmpty()) { paintMapPlanFallback(painter,request,true); }
				updateAccessibleState();
				return;
			}
		} else {
			// Keep the failed request identity so incidental repaints do not
			// repeatedly allocate; a new view/source retries the background path.
			m_planRenderWorker->cancel(); m_planRenderPending = false;
			paintMapPlanFallback(painter,request,false);
			if (!m_selection.isEmpty()) { paintMapPlanFallback(painter,request,true); }
			updateAccessibleState();
			return;
		}
	}
	// Only frames from this scene/projection survive invalidation. A previous
	// navigation image follows the new world transform while the exact view is
	// prepared. Source/visibility changes clear it; selection changes clear the
	// highlight immediately, so retired objects cannot remain on screen.
	const auto draw = [&](const MapPlanWireFrame& frame) {
		if (frame.image.isNull()) { return; }
		const double scale = m_zoom / frame.zoom;
		const QSizeF target(frame.viewport.width() * scale, frame.viewport.height() * scale);
		const QPointF offset(width() * 0.5 - target.width() * 0.5 + (frame.center.x() - m_worldCenter.x()) * m_zoom,
			height() * 0.5 - target.height() * 0.5 - (frame.center.y() - m_worldCenter.y()) * m_zoom);
		// Fractional device scales round physical image dimensions upward. Keep
		// that extra coverage rather than squeezing it into the logical extent,
		// which would move edges by a fraction of a pixel in odd-sized panes.
		painter.drawImage(QRectF(offset - frame.pixelPhase * (scale / frame.pixelRatio),frame.image.deviceIndependentSize() * scale),frame.image);
	};
	draw(m_planWireFrame); if (!m_selection.isEmpty()) { draw(m_selectionWireFrame); }
	updateAccessibleState();
}

void MapViewport::paintQuake(QPainter& painter, const Palette& palette)
{
	const QPointF topLeft = viewToWorld(QPointF(0.0, 0.0));
	const QPointF bottomRight = viewToWorld(QPointF(width(), height()));
	const QRectF visible = minMaxRect(std::min(topLeft.x(), bottomRight.x()), std::min(topLeft.y(), bottomRight.y()),
		std::max(topLeft.x(), bottomRight.x()), std::max(topLeft.y(), bottomRight.y()));
	const int worldspawnId = worldspawnEntityId(m_document);
	painter.setBrush(Qt::NoBrush);
	paintQuakeGeometry(painter,palette,worldspawnId);

	// Point entities: diamond markers so they cannot be mistaken for brushes.
	const QPen entityPen(palette.entity, 1.5);
	painter.setPen(entityPen);
	int labelsDrawn = 0;
	QPolygonF diamond;
	for (const LevelMapEntity& entity : m_document.entities) {
		if (!entity.origin.valid || entity.id == worldspawnId) {
			continue;
		}
		const QPointF point = mapViewportProjectPoint(m_projection, entity.origin);
		if (!visible.contains(point)) {
			continue;
		}
		const QPointF view = worldToView(point.x(), point.y());
		diamond.clear();
		diamond.append(QPointF(view.x(), view.y() - 5.0));
		diamond.append(QPointF(view.x() + 5.0, view.y()));
		diamond.append(QPointF(view.x(), view.y() + 5.0));
		diamond.append(QPointF(view.x() - 5.0, view.y()));
		painter.drawPolygon(diamond);
		if (m_showLabels && labelsDrawn < kMaxLabels && !entity.className.isEmpty()) {
			painter.setPen(QPen(palette.subtleText, 1.0));
			painter.drawText(QPointF(view.x() + 8.0, view.y() - 6.0), entity.className);
			painter.setPen(entityPen);
			++labelsDrawn;
		}
	}
}

void MapViewport::paintEmptyState(QPainter& painter, const Palette& palette) const
{
	painter.setPen(QPen(palette.text, 1.0));
	QFont headingFont = painter.font();
	headingFont.setBold(true);
	painter.setFont(headingFont);
	const QRectF area = QRectF(rect()).adjusted(24.0, 24.0, -24.0, -24.0);
	const QRectF headingRect(area.left(), area.center().y() - 28.0, area.width(), 24.0);
	painter.drawText(headingRect, Qt::AlignHCenter | Qt::AlignVCenter, tr("No map is loaded"));

	headingFont.setBold(false);
	painter.setFont(headingFont);
	painter.setPen(QPen(palette.subtleText, 1.0));
	const QRectF bodyRect(area.left(), area.center().y(), area.width(), 48.0);
	painter.drawText(bodyRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
		tr("Open a Doom WAD or a Quake-family .map file to draw its geometry here."));
}

void MapViewport::paintOverlay(QPainter& painter, const Palette& palette) const
{
	PaintMeasurement measured(m_measurePainting,&m_paintStatistics.overlayNs);
	ViewportHudLayout hud;
	if (m_hasDocument) {
		PaintMeasurement hudMeasured(m_measurePainting,&m_paintStatistics.hudNs);
		const auto tags = hudTags();
		hud = layoutViewportHud(rect(),QFontMetricsF(viewportHudFont(font()),painter.device()),
			tags.at(0).split(QStringLiteral("  %1  ").arg(QChar(0x00b7))),{tags.at(1)},layoutDirection());
	}
	if (m_hasDocument && !m_dragging && !m_resizing && !m_banding && m_hover.kind == LevelMapSelectionKind::DoomSector) {
		// The sector under the pointer is traced, dotted, as the room a click
		// would pick.
		painter.setPen(QPen(palette.hover, m_highContrast ? 2.0 : 1.5, Qt::DotLine));
		painter.setBrush(Qt::NoBrush);
		for (const DoomSectorOutline& outline : m_sectorOutlines) {
		if (isHidden(LevelMapSelectionKind::DoomSector, outline.sectorId)) { continue; }
			if (outline.sectorId != m_hover.objectId) {
				continue;
			}
			for (const QPolygonF& loop : outline.loops) {
				QPolygonF view;
				for (const QPointF& point : loop) {
					view.append(worldToView(point.x(), point.y()));
				}
				painter.drawPolygon(view);
			}
		}
	} else if (m_hasDocument && !m_dragging && !m_resizing && !m_banding && m_hover.kind != LevelMapSelectionKind::None) {
		const QPointF view = worldToView(m_hover.worldX, m_hover.worldY);
		painter.setPen(QPen(palette.hover, 1.0, Qt::DotLine));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(QRectF(view.x() - 7.0, view.y() - 7.0, 14.0, 14.0));
	}

	if (m_hasDocument && m_dropActive) {
		// Where a palette drop would land, on the grid while snapping is on.
		const QPointF snapped = snappedPlanePoint(m_dropPoint);
		const QPointF at = worldToView(snapped.x(), snapped.y());
		painter.setPen(QPen(palette.selection, m_highContrast ? 2.5 : 2.0));
		painter.setBrush(Qt::NoBrush);
		painter.drawEllipse(at, 8.0, 8.0);
		painter.drawLine(at - QPointF(12.0, 0.0), at + QPointF(12.0, 0.0));
		painter.drawLine(at - QPointF(0.0, 12.0), at + QPointF(0.0, 12.0));
	}

	if (m_hasDocument) {
		paintSelectionMarkers(painter, palette, hud);
		if (m_dragging) {
			paintDragPreview(painter, palette);
		}
		if (m_resizing) {
			paintResizePreview(painter, palette);
		}
		paintResizeHandles(painter, palette);
		paintClip(painter, palette);
		paintDraw(painter, palette);
		paintBrushDraw(painter, palette);
		paintCameraMarker(painter, palette);
	}
	if (m_banding) {
		paintRubberBand(painter, palette);
	}
	if (m_hasDocument) {
		paintHud(painter, palette, hud);
	}

	if (hasFocus() || m_activePane) {
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(palette.focus, 2.0, Qt::SolidLine));
		painter.drawRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5));
		painter.setPen(QPen(palette.background, 1.0, Qt::DashLine));
		painter.drawRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5));
	}
}

QStringList MapViewport::hudTags() const
{
	const QString separator = QStringLiteral("  %1  ").arg(QChar(0x00b7));
	QStringList leftParts {
		mapViewportProjectionDisplayName(m_projection),
		tr("Grid %1").arg(m_gridSize),
		m_snapToGrid ? tr("Snap on") : tr("Snap off"),
	};
	if (m_activePane) { leftParts.insert(1, tr("Active")); }
	if (m_hasCameraBrushDraft) { leftParts << tr("Camera brush draft"); }
	if (isRendering()) { leftParts.insert(1, tr("Updating view…")); }
	if (m_clipMode) {
		leftParts << tr("Clip");
	}
	if (m_drawMode) {
		leftParts << tr("Draw Sector");
	}
	if (!m_leakTrail.isEmpty()) {
		leftParts << tr("Leak trail");
	}
	if (!m_hidden.isEmpty()) {
		leftParts << tr("%n hidden", nullptr, static_cast<int>(m_hidden.size()));
	}
	// How big the selection is, the first thing a mapper checks after a drag.
	const QSizeF extent = selectionExtent();
	if (extent.width() > 0.0 || extent.height() > 0.0) {
		leftParts << tr("Selection %1 %2 %3").arg(extent.width(), 0, 'g', 6).arg(QChar(0x00d7)).arg(extent.height(), 0, 'g', 6);
	}
	const QString left = leftParts.join(separator);
	QStringList rightParts = documentIsDoom(m_document)
		? QStringList {tr("%n thing(s)", nullptr, static_cast<int>(m_document.doomThings.size())),
			  tr("%n linedef(s)", nullptr, static_cast<int>(m_document.doomLinedefs.size())),
			  tr("%n sector(s)", nullptr, static_cast<int>(m_document.doomSectors.size()))}
		: QStringList {tr("%n entit(y)(ies)", nullptr, static_cast<int>(m_document.entities.size())),
			  tr("%n brush(es)", nullptr, static_cast<int>(m_document.brushes.size()))};
	// The selection count stays in view, where a status message would go
	// stale the moment the selection changed.
	if (!m_selection.isEmpty()) {
		rightParts.prepend(tr("%n selected", nullptr, static_cast<int>(m_selection.size())));
	}
	return {left, rightParts.join(separator)};
}

void MapViewport::paintHud(QPainter& painter, const Palette& palette, const ViewportHudLayout& hud) const
{
	PaintMeasurement measured(m_measurePainting,&m_paintStatistics.hudNs);
	paintViewportHud(painter,rect(),font(),hud,layoutDirection(),
		m_highContrast ? palette.text : palette.subtleText,palette.background,m_highContrast);
}

void MapViewport::announceSelection()
{
	updateWorkZone();
	setAccessibleDescription(accessibleSummary());
	update();
	// The set goes out first so a receiver that rebuilds the document's
	// selection from it has already done so by the time the primary arrives.
	Q_EMIT selectionSetChanged(m_selection);
	Q_EMIT selectionChanged(static_cast<int>(m_selectionKind), m_selectedObjectId);
}

bool MapViewport::event(QEvent* event)
{
	if (event->type() == QEvent::FocusOut || event->type() == QEvent::Hide || (event->type() == QEvent::EnabledChange && !isEnabled())) {
		setTemporaryPan(false);
	}
	if (event->type() == QEvent::ShortcutOverride) {
		const auto* key = static_cast<QKeyEvent*>(event);
		if (navigationHoldKeyMatches(m_controls.panHoldKey, key->key(), key->modifiers())
			|| !planArrowPanDirection(m_controls, key->key(), key->modifiers()).isNull()) { event->accept(); return true; }
	}
	if (event->type() == QEvent::ShortcutOverride && m_drawMode && !m_drawCorners.isEmpty()) {
		const int key = static_cast<QKeyEvent*>(event)->key();
		if (key == Qt::Key_Delete || key == Qt::Key_Backspace) {
			event->accept();
			return true;
		}
	}
	if (event->type() == QEvent::ShortcutOverride && m_controls.arrowsDriveCamera) {
		const auto* key = static_cast<QKeyEvent*>(event);
		const bool arrow = key->key() == Qt::Key_Left || key->key() == Qt::Key_Right || key->key() == Qt::Key_Up || key->key() == Qt::Key_Down;
		if (arrow && (key->modifiers() & Qt::AltModifier) != 0) {
			event->accept();
			return true;
		}
	}
	if (event->type() == QEvent::ContextMenu && static_cast<QContextMenuEvent*>(event)->reason() == QContextMenuEvent::Mouse
		&& (m_swallowContextMenu || m_controls.panButtons.contains(Qt::RightButton))) {
		// Where the right button pans, the menu comes from a click that did
		// not (mouseReleaseEvent), whenever the platform sends its own.
		m_swallowContextMenu = false;
		event->accept();
		return true;
	}
	return QWidget::event(event);
}

void MapViewport::paintEvent(QPaintEvent*)
{
	if (m_measurePainting) { m_paintStatistics = {}; }
	PaintMeasurement measured(m_measurePainting,&m_paintStatistics.totalNs);
	Palette palette;
	if (m_highContrast) {
		palette.background = QColor(0, 0, 0);
		palette.gridMinor = QColor(70, 70, 70);
		palette.gridMajor = QColor(120, 120, 120);
		palette.axis = QColor(255, 255, 255);
		palette.oneSided = QColor(255, 255, 255);
		palette.twoSided = QColor(190, 190, 190);
		palette.vertex = QColor(255, 255, 0);
		palette.thing = QColor(0, 255, 255);
		palette.entity = QColor(0, 255, 255);
		palette.brush = QColor(255, 255, 255);
		palette.brushEntity = QColor(255, 255, 0);
		palette.patch = QColor(0, 255, 0);
		palette.warning = QColor(255, 96, 0);
		palette.selection = QColor(255, 0, 255);
		palette.hover = QColor(255, 255, 255);
		palette.text = QColor(255, 255, 255);
		palette.subtleText = QColor(220, 220, 220);
		palette.focus = QColor(255, 255, 0);
		palette.sectorDark = QColor(24, 24, 24);
		palette.sectorBright = QColor(200, 200, 200);
		palette.leak = QColor(255, 48, 48);
		palette.link = QColor(140, 180, 255);
	} else {
		palette.background = QColor(22, 24, 28);
		palette.gridMinor = QColor(40, 44, 52);
		palette.gridMajor = QColor(58, 64, 76);
		palette.axis = QColor(96, 108, 128);
		palette.oneSided = QColor(226, 232, 240);
		palette.twoSided = QColor(128, 140, 160);
		palette.vertex = QColor(240, 200, 96);
		palette.thing = QColor(120, 200, 240);
		palette.entity = QColor(120, 200, 240);
		palette.brush = QColor(200, 210, 225);
		palette.brushEntity = QColor(140, 220, 170);
		palette.patch = QColor(210, 160, 240);
		palette.warning = QColor(240, 140, 80);
		palette.selection = QColor(255, 210, 90);
		palette.hover = QColor(170, 180, 200);
		palette.text = QColor(232, 236, 244);
		palette.subtleText = QColor(160, 168, 182);
		palette.focus = QColor(120, 180, 250);
		palette.sectorDark = QColor(34, 44, 58);
		palette.sectorBright = QColor(150, 170, 200);
		palette.leak = QColor(255, 86, 86);
		palette.link = QColor(214, 140, 224);
	}

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.fillRect(rect(), palette.background);

	if (!m_hasDocument) {
		paintEmptyState(painter, palette);
		paintOverlay(painter, palette);
		return;
	}

	prepareOverlays(painter,palette);

	if (m_showGrid) {
		PaintMeasurement grid(m_measurePainting,&m_paintStatistics.gridNs);
		paintGrid(painter, palette);
	}
	{
		PaintMeasurement geometry(m_measurePainting,&m_paintStatistics.geometryNs);
		if (documentIsDoom(m_document)) {
			paintDoom(painter, palette);
			paintTagLinks(painter, palette);
		} else {
			paintTargetLinks(painter, palette);
			paintQuake(painter, palette);
		}
	}
	paintLeakTrail(painter, palette);
	paintOverlay(painter, palette);
}

void MapViewport::paintTargetLinks(QPainter& painter, const Palette& palette) const
{
	if (m_targetLinks.isEmpty()) {
		return;
	}
	// A dragged entity takes its links with it.
	const QPointF dragDelta = m_dragging ? snappedPlaneDelta() : QPointF();
	const QSet<QPair<int, int>> selected = selectedKeys();
	const auto entitySelected = [&selected](int entityId) {
		return selected.contains({static_cast<int>(LevelMapSelectionKind::Entity), entityId});
	};
	const auto endPoint = [this, &dragDelta, &entitySelected](int entityId, const LevelMapVec3& anchor) {
		QPointF world = projectPoint(anchor);
		if (m_dragging && entitySelected(entityId)) {
			world += dragDelta;
		}
		return worldToView(world.x(), world.y());
	};
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing, true);
	for (const LevelMapTargetLink& link : m_targetLinks) {
		const bool touched = entitySelected(link.sourceEntityId) || entitySelected(link.targetEntityId);
		if (!m_showTargetLinks && !touched) {
			continue;
		}
		const QPointF from = endPoint(link.sourceEntityId, link.from);
		const QPointF to = endPoint(link.targetEntityId, link.to);
		const QPointF span = to - from;
		const double length = std::hypot(span.x(), span.y());
		if (length < 12.0) {
			// Stacked in this projection: an arrow would be a smudge.
			continue;
		}
		QColor colour = palette.link;
		if (!touched && !m_highContrast) {
			colour.setAlpha(150);
		}
		// Shape carries the meaning as well as colour: a solid line fires the
		// target, a dashed one removes it.
		QPen pen(colour, touched ? (m_highContrast ? 3.0 : 2.2) : (m_highContrast ? 1.8 : 1.2),
			link.key == QStringLiteral("killtarget") ? Qt::DashLine : Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
		// Stop short of the target's marker so the arrowhead stays visible.
		const QPointF direction = span / length;
		const QPointF tip = to - direction * 7.0;
		painter.setPen(pen);
		painter.setBrush(Qt::NoBrush);
		painter.drawLine(from, tip);
		const QPointF normal(-direction.y(), direction.x());
		const double headLength = touched ? 10.0 : 8.0;
		const double headWidth = touched ? 5.0 : 4.0;
		const QPolygonF head {tip, tip - direction * headLength + normal * headWidth, tip - direction * headLength - normal * headWidth};
		painter.setPen(Qt::NoPen);
		painter.setBrush(colour);
		painter.drawPolygon(head);
	}
	painter.restore();
}

// Doom Builder's associations: each shown tag link is an arrow from the
// middle of the line that acts to the middle of the sector it acts on, and
// the sector's outline is traced dashed, with the tag beside the arrowhead so
// the link reads without colour.
void MapViewport::paintTagLinks(QPainter& painter, const Palette& palette) const
{
	if (m_tagLinks.isEmpty() || m_projection != MapViewportProjection::TopXY) {
		return;
	}
	if (drawnTagLinkCount() == 0) {
		return;
	}
	QHash<int, const DoomSectorOutline*> outlines;
	for (const DoomSectorOutline& outline : m_sectorOutlines) {
		if (isHidden(LevelMapSelectionKind::DoomSector, outline.sectorId)) { continue; }
		outlines.insert(outline.sectorId, &outline);
	}
	// The view's document can leave hidden lines out, so ids are looked up.
	QHash<int, int> lineIndexes;
	for (int index = 0; index < m_document.doomLinedefs.size(); ++index) {
		lineIndexes.insert(m_document.doomLinedefs.at(index).id, index);
	}
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing, true);
	QSet<int> traced;
	const QFontMetricsF metrics(painter.font());
	const QSet<QPair<int, int>> selected = selectedKeys();
	for (const LevelMapTagLink& link : m_tagLinks) {
		const bool touched = selected.contains({static_cast<int>(LevelMapSelectionKind::DoomLinedef), link.linedefId})
			|| selected.contains({static_cast<int>(LevelMapSelectionKind::DoomSector), link.sectorId});
		if (!m_showTargetLinks && !touched) {
			continue;
		}
		const int lineIndex = lineIndexes.value(link.linedefId, -1);
		const DoomSectorOutline* outline = outlines.value(link.sectorId, nullptr);
		if (lineIndex < 0 || !outline || outline->bounds.isNull()) {
			continue;
		}
		const LevelMapDoomLinedef& line = m_document.doomLinedefs.at(lineIndex);
		if (line.startVertex < 0 || line.endVertex < 0 || line.startVertex >= m_document.doomVertices.size()
			|| line.endVertex >= m_document.doomVertices.size()) {
			continue;
		}
		const LevelMapDoomVertex& start = m_document.doomVertices.at(line.startVertex);
		const LevelMapDoomVertex& end = m_document.doomVertices.at(line.endVertex);
		const QPointF from = worldToView((start.x + end.x) / 2.0, (start.y + end.y) / 2.0);
		const QPointF to = worldToView(outline->bounds.center().x(), outline->bounds.center().y());
		QColor colour = palette.link;
		if (!touched && !m_highContrast) {
			colour.setAlpha(150);
		}
		if (touched && !traced.contains(link.sectorId)) {
			traced.insert(link.sectorId);
			painter.setBrush(Qt::NoBrush);
			painter.setPen(QPen(colour, m_highContrast ? 3.0 : 2.0, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin));
			for (const QPolygonF& loop : outline->loops) {
				QPolygonF view;
				for (const QPointF& point : loop) {
					view << worldToView(point.x(), point.y());
				}
				painter.drawPolygon(view);
			}
		}
		const QPointF span = to - from;
		const double length = std::hypot(span.x(), span.y());
		if (length < 12.0) {
			continue;
		}
		const QPointF direction = span / length;
		const QPointF tip = to - direction * 6.0;
		painter.setPen(QPen(colour, touched ? (m_highContrast ? 3.0 : 2.0) : 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		painter.setBrush(Qt::NoBrush);
		painter.drawLine(from, tip);
		const QPointF normal(-direction.y(), direction.x());
		const QPolygonF head {tip, tip - direction * 9.0 + normal * 4.5, tip - direction * 9.0 - normal * 4.5};
		painter.setPen(Qt::NoPen);
		painter.setBrush(colour);
		painter.drawPolygon(head);
		if (touched) {
			const QString label = tr("tag %1").arg(link.tag);
			painter.setPen(palette.text);
			painter.drawText(tip + normal * 8.0 - QPointF(0.0, metrics.descent()), label);
		}
	}
	painter.restore();
}

void MapViewport::paintLeakTrail(QPainter& painter, const Palette& palette) const
{
	if (m_leakTrail.isEmpty()) {
		return;
	}
	QPolygonF line;
	for (const LevelMapVec3& point : m_leakTrail) {
		const QPointF world = projectPoint(point);
		line << worldToView(world.x(), world.y());
	}
	painter.save();
	const qreal width = m_highContrast ? 4.0 : 2.5;
	// A casing in the background colour keeps the line readable over brushes
	// of any colour.
	painter.setBrush(Qt::NoBrush);
	painter.setPen(QPen(palette.background, width + 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	painter.drawPolyline(line);
	painter.setPen(QPen(palette.leak, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	painter.drawPolyline(line);
	// Shape as well as colour marks the ends: a filled circle where the file
	// starts and a hollow square where it ends.
	painter.setBrush(palette.leak);
	painter.drawEllipse(line.first(), 5.0, 5.0);
	painter.setBrush(Qt::NoBrush);
	painter.drawRect(QRectF(line.last().x() - 5.0, line.last().y() - 5.0, 10.0, 10.0));
	// Object labels sit to the right of their marker, so this one goes left.
	const QString label = tr("Leak");
	const QFontMetricsF metrics(painter.font());
	painter.setPen(palette.text);
	painter.drawText(line.first() + QPointF(-9.0 - metrics.horizontalAdvance(label), -9.0), label);
	painter.restore();
}

void MapViewport::mousePressEvent(QMouseEvent* event)
{
	setFocus(Qt::MouseFocusReason);
	m_lastPointerPosition = event->position();
	if (m_panHoldActive) { event->accept(); return; }
	const Qt::MouseButton button = event->button();
	const Qt::KeyboardModifiers keys = event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
	// Space's hand turns the left button into a pan handle, in every profile.
	if (button == Qt::LeftButton && m_handMode) {
		beginPan(event->position(), button);
		event->accept();
		return;
	}
	// Radiant's middle button drives the 3D camera instead of panning: a
	// plain press aims it at the point, Ctrl moves it there, and a drag
	// keeps doing so.
	if (button == Qt::MiddleButton && m_controls.middleButtonDrivesCamera && m_hasDocument) {
		m_cameraDrag = keys.testFlag(Qt::ControlModifier) ? CameraDrag::Place : CameraDrag::Aim;
		driveCameraTo(event->position());
		event->accept();
		return;
	}
	if (button != Qt::NoButton && button == m_controls.zoomDragButton
		&& keys == m_controls.zoomDragModifiers) {
		m_zoomDragging = true;
		m_zoomAnchorView = event->position();
		m_zoomAnchorWorld = viewToWorld(event->position());
		m_zoomDragStart = m_zoom;
		m_swallowContextMenu = true;
		setCursor(Qt::SizeVerCursor);
		event->accept();
		return;
	}
	// Buttons that pan at once. The right button pans only once it moves
	// (below), so a right click still opens the menu.
	if (button != Qt::RightButton && button != Qt::LeftButton && m_controls.panButtons.contains(button)) {
		beginPan(event->position(), button);
		event->accept();
		return;
	}
	m_swallowContextMenu = false;
	if (button == Qt::RightButton && m_drawMode && !m_drawCorners.isEmpty()) {
		// A right-click while drawing takes the last corner back, and the menu
		// it would open stays shut.
		m_drawCorners.removeLast();
		m_swallowContextMenu = true;
		Q_EMIT hoverChanged(drawSummary());
		update();
		event->accept();
		return;
	}
	if (button == Qt::RightButton
		&& (m_dragging || m_banding || m_pressArmed || m_resizing || m_clipDrawing || m_brushArmed || m_brushDrawing || m_clickArmed
			|| m_pendingResizeEdges != 0)) {
		// A right press in the middle of a drag, resize, or rubber band calls it
		// off, as Escape does, rather than changing what the gesture acts on;
		// the menu this click would open stays shut.
		cancelInteraction();
		m_swallowContextMenu = true;
		event->accept();
		return;
	}
	if (button == Qt::RightButton) {
		if (m_controls.panButtons.contains(Qt::RightButton) && keys == Qt::NoModifier) {
			// Pans once it moves; a click settles the selection and opens the
			// menu when it lets go (event() holds the platform's menu back).
			m_rightArmed = true;
			m_rightPressView = event->position();
			event->accept();
			return;
		}
		settleSelectionForMenu(event->position());
	}
	if (button != Qt::LeftButton) {
		QWidget::mousePressEvent(event);
		return;
	}

	// In Draw Sector mode a press puts a corner down, or closes the shape on
	// its first corner.
	if (m_drawMode) {
		const QPointF point = drawPointAt(event->position());
		if (m_drawCorners.size() >= 3 && point == m_drawCorners.first()) {
			finishDraw();
		} else if (m_drawCorners.isEmpty() || point != m_drawCorners.last()) {
			m_drawCorners.push_back(point);
			setAccessibleDescription(accessibleSummary());
		}
		m_drawHover = point;
		m_hasDrawHover = true;
		Q_EMIT hoverChanged(drawSummary());
		update();
		event->accept();
		return;
	}

	// In clip mode a press starts the clip line where it lands.
	if (m_clipMode) {
		m_clipFrom = snappedPlanePoint(event->position());
		m_clipTo = m_clipFrom;
		m_clipDrawing = true;
		m_hasClipLine = false;
		Q_EMIT hoverChanged(clipSummary());
		update();
		event->accept();
		return;
	}

	// A handle outranks whatever lies under it; with Shift or Ctrl held the
	// press edits the selection instead.
	if (keys == Qt::NoModifier) {
		if (const int edges = resizeHandleAt(event->position()); edges != 0) {
			beginResize(edges, event->position());
			event->accept();
			return;
		}
	}

	const MapViewportHit hit = hitTest(event->position());
	m_pressViewPoint = event->position();
	const bool onSelection = hit.kind != LevelMapSelectionKind::None && selectionContains(hit.kind, hit.objectId);
	const PlanEmptyDrag emptyDrag = m_selection.isEmpty() ? m_controls.emptyDrag : m_controls.emptyDragWithSelection;

	// Radiant's area selection: with the band keys held, a drag draws a
	// rubber band wherever it starts, and a click toggles the object under
	// the pointer.
	if (m_controls.bandModifiers != Qt::NoModifier && keys == m_controls.bandModifiers) {
		m_clickArmed = true;
		m_clickBands = true;
		m_clickModifiers = event->modifiers();
		event->accept();
		return;
	}

	// GtkRadiant's drill: with these keys a click steps down the stack under
	// the pointer, and a drag does nothing.
	if (m_controls.cycleModifiers != Qt::NoModifier && keys == m_controls.cycleModifiers) {
		m_clickArmed = true;
		m_clickBands = false;
		m_clickSelects = true;
		m_clickModifiers = event->modifiers();
		event->accept();
		return;
	}

	// Radiant: nothing is decided until the pointer moves or lets go. A drag
	// on the selection moves it, any other drag draws a brush or resizes the
	// selection toward the pointer, and a click selects what is under the
	// pointer, the next one down on each click. GtkRadiant's plain click
	// selects nothing at all: Shift+click does.
	if (keys == Qt::NoModifier && (m_controls.clickCyclesStack || !m_controls.plainClickSelects)) {
		m_clickArmed = true;
		m_clickBands = false;
		m_clickSelects = m_controls.plainClickSelects;
		m_clickModifiers = event->modifiers();
		if (onSelection) {
			m_pressArmed = true;
		} else {
			armEmptyDrag(emptyDrag, event->position());
		}
		event->accept();
		return;
	}

	if (hit.kind == LevelMapSelectionKind::None) {
		// Empty space starts a rubber band. The selection is only replaced on
		// release, so a plain click that selects nothing still clears it. A
		// profile that draws brushes there turns the band into a brush once
		// the drag moves.
		m_banding = true;
		m_bandAnchorView = event->position();
		m_bandCurrentView = event->position();
		m_bandModifiers = event->modifiers();
		m_bandSector = sectorAt(event->position());
		if (keys == Qt::NoModifier) {
			armEmptyDrag(emptyDrag, event->position());
		}
		update();
		event->accept();
		return;
	}

	const bool toggle = modifiersHeld(keys, m_controls.toggleModifiers);
	const bool extend = modifiersHeld(keys, m_controls.addModifiers);
	if (toggle) {
		toggleInSelection(hit.kind, hit.objectId);
		announceSelection();
	} else if (extend) {
		addToSelection(hit.kind, hit.objectId);
		announceSelection();
	} else if (!onSelection) {
		replaceSelection(hit.kind, hit.objectId);
		announceSelection();
	} else if (hit.kind != m_selectionKind || hit.objectId != m_selectedObjectId) {
		// Pressing another member of the set promotes it without dropping the
		// rest, so a multi-object drag can start anywhere in the selection.
		addToSelection(hit.kind, hit.objectId);
		announceSelection();
	}

	// Arm, but do not start, a move: a press that never travels is a click.
	m_pressArmed = !m_selection.isEmpty() && selectionContains(hit.kind, hit.objectId);
	event->accept();
}

void MapViewport::mouseMoveEvent(QMouseEvent* event)
{
	const QPointF position = event->position();
	m_lastPointerPosition = position;
	if (m_cameraDrag != CameraDrag::None) {
		driveCameraTo(position);
		event->accept();
		return;
	}

	if (m_zoomDragging) {
		// Up zooms in, about the point the drag began on.
		const double factor = std::pow(1.01, m_zoomAnchorView.y() - position.y());
		const double zoom = clampZoom(m_zoomDragStart * factor);
		if (!qFuzzyCompare(zoom, m_zoom)) {
			m_zoom = zoom;
			m_worldCenter += m_zoomAnchorWorld - viewToWorld(m_zoomAnchorView);
			setAccessibleDescription(accessibleSummary());
			update();
			Q_EMIT viewChanged();
		}
		event->accept();
		return;
	}

	if (m_rightArmed) {
		const QPointF travel = position - m_rightPressView;
		if (std::hypot(travel.x(), travel.y()) >= kDragThresholdPixels) {
			m_rightArmed = false;
			beginPan(m_rightPressView, Qt::RightButton);
		} else {
			event->accept();
			return;
		}
	}

	if (m_panning) {
		const QPointF delta = position - m_panAnchorView;
		if (m_zoom > 0.0) {
			m_worldCenter = QPointF(m_panAnchorCenter.x() - delta.x() / m_zoom,
				m_panAnchorCenter.y() + delta.y() / m_zoom);
		}
		update();
		Q_EMIT viewChanged();
		event->accept();
		return;
	}

	if (m_clickArmed) {
		const QPointF travel = position - m_pressViewPoint;
		if (std::hypot(travel.x(), travel.y()) >= kDragThresholdPixels) {
			m_clickArmed = false;
			if (m_clickBands) {
				m_banding = true;
				m_bandAnchorView = m_pressViewPoint;
				m_bandCurrentView = position;
				m_bandModifiers = m_clickModifiers;
				m_bandSector = -1;
				update();
			} else if (m_pressArmed) {
				beginDrag(position);
			} else if (m_brushArmed) {
				beginBrushDraw(position, event->modifiers());
			} else if (m_pendingResizeEdges != 0) {
				const int edges = m_pendingResizeEdges;
				m_pendingResizeEdges = 0;
				beginResize(edges, m_pressViewPoint);
				updateResize(position);
			}
		}
		event->accept();
		return;
	}

	if (m_brushDrawing) {
		updateBrushDraw(position, event->modifiers());
		event->accept();
		return;
	}

	if (m_banding) {
		const QPointF travel = position - m_bandAnchorView;
		if ((m_brushArmed || m_pendingResizeEdges != 0) && std::hypot(travel.x(), travel.y()) >= kDragThresholdPixels) {
			// A drag over empty space that draws or resizes instead of banding.
			m_banding = false;
			m_bandSector = -1;
			if (m_brushArmed) {
				beginBrushDraw(position, event->modifiers());
			} else {
				const int edges = m_pendingResizeEdges;
				m_pendingResizeEdges = 0;
				beginResize(edges, m_bandAnchorView);
				updateResize(position);
			}
			event->accept();
			return;
		}
		m_bandCurrentView = position;
		Q_EMIT hoverChanged(hoverSummary());
		update();
		event->accept();
		return;
	}

	if (m_drawMode) {
		const QPointF point = drawPointAt(position);
		if (!m_hasDrawHover || point != m_drawHover) {
			m_drawHover = point;
			m_hasDrawHover = true;
			Q_EMIT hoverChanged(drawSummary());
			update();
		}
		event->accept();
		return;
	}

	if (m_resizing) {
		updateResize(position);
		event->accept();
		return;
	}

	if (m_clipDrawing) {
		const QPointF to = snappedPlanePoint(position);
		if (to != m_clipTo) {
			m_clipTo = to;
			update();
		}
		Q_EMIT hoverChanged(clipSummary());
		event->accept();
		return;
	}

	if (m_pressArmed) {
		const QPointF travel = position - m_pressViewPoint;
		if (std::hypot(travel.x(), travel.y()) >= kDragThresholdPixels) {
			beginDrag(position);
		}
		event->accept();
		return;
	}

	if (m_dragging) {
		updateDrag(position);
		event->accept();
		return;
	}

	const int handle = resizeHandleAt(position);
	const bool handleChanged = handle != m_hoverHandle;
	updateResizeCursor(handle);
	const MapViewportHit hit = hitOrSectorAt(position);
	const bool sameObject = hit.kind == m_hover.kind && hit.objectId == m_hover.objectId;
	m_hover = hit;
	Q_EMIT hoverChanged(handle != 0 ? resizeHandleSummary(handle) : hoverSummary());
	if (!sameObject || handleChanged) {
		update();
	}
	event->accept();
}

void MapViewport::mouseReleaseEvent(QMouseEvent* event)
{
	if (m_panHoldActive) { event->accept(); return; }
	const Qt::MouseButton button = event->button();
	if (m_cameraDrag != CameraDrag::None && button == Qt::MiddleButton) {
		m_cameraDrag = CameraDrag::None;
		event->accept();
		return;
	}
	if (m_zoomDragging && button == m_controls.zoomDragButton) {
		m_zoomDragging = false;
		unsetCursor();
		event->accept();
		return;
	}
	if (m_rightArmed && button == Qt::RightButton) {
		// A right click that did not pan: the menu, for what is under it.
		m_rightArmed = false;
		settleSelectionForMenu(event->position());
		m_swallowContextMenu = false;
		Q_EMIT customContextMenuRequested(event->position().toPoint());
		event->accept();
		return;
	}
	if (m_panning && button == m_panButton) {
		m_panning = false;
		m_panButton = Qt::NoButton;
		// Space's hand stays until Space again; any other pan leaves the
		// ordinary pointer behind.
		if (m_handMode) {
			setCursor(Qt::OpenHandCursor);
		} else {
			unsetCursor();
		}
		// A right drag that panned opens no menu.
		if (button == Qt::RightButton) {
			m_swallowContextMenu = true;
		}
		event->accept();
		return;
	}
	if (button != Qt::LeftButton) {
		QWidget::mouseReleaseEvent(event);
		return;
	}
	if (m_brushDrawing) {
		commitBrushDraw();
		event->accept();
		return;
	}
	if (m_clickArmed) {
		// A click that never travelled.
		m_clickArmed = false;
		m_pressArmed = false;
		m_brushArmed = false;
		m_pendingResizeEdges = 0;
		const MapViewportHit hit = hitTest(event->position());
		if (m_clickBands) {
			if (hit.kind != LevelMapSelectionKind::None) {
				const Qt::KeyboardModifiers keys = m_clickModifiers & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
				if (modifiersHeld(keys, m_controls.toggleModifiers)) {
					toggleInSelection(hit.kind, hit.objectId);
				} else {
					addToSelection(hit.kind, hit.objectId);
				}
				announceSelection();
			}
		} else if (m_clickSelects) {
			cycleSelectionAt(event->position());
		}
		Q_EMIT hoverChanged(hoverSummary());
		event->accept();
		return;
	}
	if (m_resizing) {
		commitResize();
		event->accept();
		return;
	}
	if (m_clipDrawing) {
		m_clipDrawing = false;
		m_hasClipLine = m_clipTo != m_clipFrom;
		Q_EMIT hoverChanged(clipSummary());
		update();
		event->accept();
		return;
	}
	if (m_dragging) {
		commitDrag();
		event->accept();
		return;
	}
	if (m_pressArmed) {
		// A click on an already-selected object: the selection was settled on
		// press, so there is nothing left to do.
		m_pressArmed = false;
		event->accept();
		return;
	}
	if (m_banding) {
		m_brushArmed = false;
		m_pendingResizeEdges = 0;
		QVector<LevelMapSelectionRef> inside = objectsInWorldRect(bandWorldRect());
		m_banding = false;
		// A click, not a drag, inside a Doom sector picks the sector, as Doom
		// Builder's sectors mode does; a drag from there still boxes objects.
		const QPointF travel = m_bandCurrentView - m_bandAnchorView;
		if (m_bandSector >= 0 && std::hypot(travel.x(), travel.y()) < kDragThresholdPixels) {
			inside = {LevelMapSelectionRef {LevelMapSelectionKind::DoomSector, m_bandSector}};
		}
		m_bandSector = -1;
		const Qt::KeyboardModifiers keys = m_bandModifiers & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
		if (modifiersHeld(keys, m_controls.toggleModifiers)) {
			for (const LevelMapSelectionRef& ref : inside) {
				toggleInSelection(ref.kind, ref.objectId);
			}
		} else if (modifiersHeld(keys, m_controls.addModifiers) || modifiersHeld(keys, m_controls.bandModifiers)) {
			for (const LevelMapSelectionRef& ref : inside) {
				addToSelection(ref.kind, ref.objectId);
			}
		} else {
			m_selection = inside;
			syncPrimaryFromSelection();
		}
		m_bandModifiers = Qt::NoModifier;
		announceSelection();
		Q_EMIT hoverChanged(hoverSummary());
		event->accept();
		return;
	}
	QWidget::mouseReleaseEvent(event);
}

void MapViewport::wheelEvent(QWheelEvent* event)
{
	const int delta = event->angleDelta().y();
	if (delta == 0) {
		QWidget::wheelEvent(event);
		return;
	}
	const QPointF anchorView = event->position();
	const QPointF anchorWorld = viewToWorld(anchorView);
	const double factor = std::pow(1.2, static_cast<double>(delta) / 120.0);
	const double zoom = clampZoom(m_zoom * factor);
	if (!qFuzzyCompare(zoom, m_zoom)) {
		m_zoom = zoom;
		// Keep the world point under the cursor pinned to the cursor.
		const QPointF afterWorld = viewToWorld(anchorView);
		m_worldCenter += anchorWorld - afterWorld;
		setAccessibleDescription(accessibleSummary());
		update();
		Q_EMIT viewChanged();
	}
	event->accept();
}

void MapViewport::keyPressEvent(QKeyEvent* event)
{
	if (navigationHoldKeyMatches(m_controls.panHoldKey, event->key(), event->modifiers())) {
		if (!event->isAutoRepeat()) { setTemporaryPan(true, m_lastPointerPosition); }
		event->accept(); return;
	}
	const auto direction = planArrowPanDirection(m_controls, event->key(), event->modifiers());
	if (!direction.isNull()) {
		if (m_zoom > 0) { m_worldCenter += QPointF(direction.x() * width(), direction.y() * height()) / (4.0 * m_zoom); }
		setAccessibleDescription(accessibleSummary()); update(); Q_EMIT viewChanged(); event->accept(); return;
	}
	const double panStep = m_zoom > 0.0 ? 48.0 / m_zoom : 48.0;
	switch (event->key()) {
	case Qt::Key_Left:
	case Qt::Key_Right:
	case Qt::Key_Up:
	case Qt::Key_Down: {
		const double sign = (event->key() == Qt::Key_Left || event->key() == Qt::Key_Down) ? -1.0 : 1.0;
		const bool horizontal = event->key() == Qt::Key_Left || event->key() == Qt::Key_Right;
		const auto arrowModifiers = event->modifiers() & ~Qt::KeypadModifier;
		if (m_controls.arrowsDriveCamera && m_controls.fixedCameraSteps && arrowModifiers != Qt::NoModifier && arrowModifiers != Qt::AltModifier) {
			// Classic Radiant reserves Shift/Ctrl arrows for texture tools. An
			// unsupported texture chord must never move the selected geometry.
			QWidget::keyPressEvent(event); return;
		}
		const auto drive = planCameraStep(m_controls, event->key(), event->modifiers(), m_gridSize);
		if (!drive.isNull()) {
			// Radiant: the arrows drive the 3D camera from any view; Up and
			// Down move it, Left and Right turn it.
			Q_EMIT cameraDriveRequested(drive.x(), drive.y());
			event->accept();
			return;
		}
		// Arrows nudge the selection, which is the whole point of having one.
		// Ctrl always pans, and so do bare arrows when nothing is selected, so
		// the old keyboard panning is still reachable.
		const bool pan = m_selection.isEmpty() || (event->modifiers() & Qt::ControlModifier) != 0;
		// Radiant nudges with Alt held; the step is the same.
		if (pan) {
			if (horizontal) {
				m_worldCenter.rx() += sign * panStep;
			} else {
				m_worldCenter.ry() += sign * panStep;
			}
			update();
			Q_EMIT viewChanged();
			event->accept();
			return;
		}
		if (m_dragging || m_banding || m_resizing) {
			event->accept();
			return;
		}
		const double grid = m_gridSize > 0 ? static_cast<double>(m_gridSize) : 1.0;
		const double step = sign * grid * ((event->modifiers() & Qt::ShiftModifier) != 0 ? kCoarseNudgeMultiplier : 1);
		requestMove(horizontal ? QPointF(step, 0.0) : QPointF(0.0, step));
		event->accept();
		return;
	}
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
		updateWorldBounds();
		zoomToFit();
		event->accept();
		return;
	case Qt::Key_F:
		// F frames the selection, as in most level and 3D editors.
		if (event->modifiers() == Qt::NoModifier) {
			zoomToSelection();
			event->accept();
			return;
		}
		break;
	case Qt::Key_Space:
		// Toggle the pan handle; the cursor shape shows it.
		if (event->isAutoRepeat() || event->modifiers() != Qt::NoModifier) { event->accept(); return; }
		m_handMode = !m_handMode;
		if (m_handMode) {
			setCursor(Qt::OpenHandCursor);
		} else {
			unsetCursor();
		}
		event->accept();
		return;
	case Qt::Key_Backspace:
	case Qt::Key_Delete:
		if (m_drawMode && !m_drawCorners.isEmpty()) {
			m_drawCorners.removeLast();
			setAccessibleDescription(accessibleSummary());
			Q_EMIT hoverChanged(drawSummary());
			update();
			event->accept();
			return;
		}
		QWidget::keyPressEvent(event);
		return;
	case Qt::Key_Escape:
		if (m_panHoldActive) { setTemporaryPan(false); event->accept(); return; }
		if (m_drawMode) {
			// Escape drops the shape being drawn, then leaves the mode.
			if (!m_drawCorners.isEmpty()) {
				clearDrawCorners();
			} else {
				setDrawMode(false);
			}
			event->accept();
			return;
		}
		if (m_clipMode) {
			// Escape drops a line being drawn, then leaves the mode.
			if (m_clipDrawing) {
				cancelInteraction();
			} else {
				setClipMode(false);
			}
			event->accept();
			return;
		}
		m_panning = false;
		m_handMode = false;
		unsetCursor();
		if (m_dragging || m_banding || m_pressArmed || m_resizing || m_brushDrawing || m_clickArmed) {
			// Escape during a drag throws the preview away; nothing was
			// committed, so the objects never left their original positions.
			cancelInteraction();
			event->accept();
			return;
		}
		if (m_selectionKind != LevelMapSelectionKind::None) {
			m_selection.clear();
			syncPrimaryFromSelection();
			announceSelection();
		} else {
			// Tab is bound to selection cycling, so Escape is the documented
			// way out of the viewport for keyboard users.
			focusNextChild();
		}
		event->accept();
		return;
	case Qt::Key_Return:
	case Qt::Key_Enter:
		if (m_drawMode) {
			finishDraw();
			event->accept();
			return;
		}
		if (m_clipMode) {
			// In clip mode Enter cuts along the drawn line; Ctrl+Enter flips
			// which side stays and Shift+Enter keeps both, as TrenchBroom and
			// Radiant have them.
			if ((event->modifiers() & Qt::ControlModifier) != 0) {
				setClipKeep(m_clipKeep == LevelMapClipKeep::Back ? LevelMapClipKeep::Front : LevelMapClipKeep::Back);
				event->accept();
				return;
			}
			LevelMapVec3 a;
			LevelMapVec3 b;
			LevelMapVec3 c;
			if (clipPlanePoints(&a, &b, &c)) {
				const LevelMapClipKeep keep = (event->modifiers() & Qt::ShiftModifier) != 0 ? LevelMapClipKeep::Both : m_clipKeep;
				Q_EMIT clipRequested(a, b, c, static_cast<int>(keep));
			}
			event->accept();
			return;
		}
		announceSelection();
		event->accept();
		return;
	case Qt::Key_Tab:
	case Qt::Key_Backtab:
		break;
	default:
		QWidget::keyPressEvent(event);
		return;
	}

	if (!m_hasDocument) {
		event->accept();
		return;
	}

	if (m_clipMode) {
		// In clip mode Tab chooses what a cut keeps: one side, the other, or
		// both as two brushes; Shift+Tab goes the other way round.
		const int step = (event->key() == Qt::Key_Backtab || (event->modifiers() & Qt::ShiftModifier) != 0) ? 2 : 1;
		setClipKeep(static_cast<LevelMapClipKeep>((static_cast<int>(m_clipKeep) + step) % 3));
		event->accept();
		return;
	}

	// Tab / Shift+Tab cycle the selection through things, vertices and lines
	// (or entities, brushes and patches), so the viewport is usable without a
	// mouse. Holding Shift or Ctrl extends the set instead of replacing it:
	// Shift+Tab extends backwards, Ctrl+Tab extends forwards.
	const int step = (event->key() == Qt::Key_Backtab || (event->modifiers() & Qt::ShiftModifier) != 0) ? -1 : 1;
	const bool extend = (event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) != 0
		|| event->key() == Qt::Key_Backtab;
	const QVector<LevelMapSelectionKind> tiers = selectionTiers(m_document);
	int total = 0;
	for (const LevelMapSelectionKind kind : tiers) {
		total += objectCountForKind(m_document, kind);
	}
	if (total <= 0) {
		event->accept();
		return;
	}
	int current = -1;
	int offset = 0;
	for (const LevelMapSelectionKind kind : tiers) {
		const int count = objectCountForKind(m_document, kind);
		if (kind == m_selectionKind) {
			const int index = objectIndexOfId(m_document, kind, m_selectedObjectId);
			if (index >= 0) {
				current = offset + index;
			}
		}
		offset += count;
	}
	int next = current < 0 ? (step > 0 ? 0 : total - 1) : ((current + step) % total + total) % total;
	bool found = false;
	for (int attempt = 0; attempt < total && !found; ++attempt) {
		offset = 0;
		for (const auto kind : tiers) {
			const int count = objectCountForKind(m_document, kind);
			if (next < offset + count) {
				const int id = objectIdAtIndex(m_document, kind, next - offset);
				if (!isHidden(kind, id)) {
					if (extend) { addToSelection(kind, id); }
					else { replaceSelection(kind, id); }
					found = true;
				}
				break;
			}
			offset += count;
		}
		next = ((next + step) % total + total) % total;
	}

	// Keep the newly selected object on screen; the jump is instantaneous, so
	// reduced motion needs no special case here.
	QPointF world;
	if (mapViewportObjectPoint(m_document, m_sectorOutlines, m_brushGeometry, m_sceneIndex, m_projection, m_selectionKind,
		    m_selectedObjectId, &world)) {
		const QPointF view = worldToView(world.x(), world.y());
		if (!QRectF(rect()).adjusted(24.0, 24.0, -24.0, -24.0).contains(view)) {
			m_worldCenter = world;
			Q_EMIT viewChanged();
		}
	}
	announceSelection();
	event->accept();
}

void MapViewport::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	Q_EMIT viewChanged();
}

void MapViewport::leaveEvent(QEvent* event)
{
	if (m_dragging || m_banding || m_resizing || m_clipDrawing) {
		// Qt keeps delivering moves to the grabbing widget, so a gesture that
		// wanders outside the viewport is still live: leave it alone.
		QWidget::leaveEvent(event);
		return;
	}
	if (m_hover.kind != LevelMapSelectionKind::None || m_hoverHandle != 0 || m_hasDrawHover) {
		update();
	}
	// The next edge follows the pointer, so it goes when the pointer does.
	m_hasDrawHover = false;
	updateResizeCursor(0);
	m_hover = MapViewportHit();
	Q_EMIT hoverChanged(m_hasDocument ? tr("Pointer left the map viewport.") : tr("No map loaded."));
	QWidget::leaveEvent(event);
}

QString mapViewportProjectionDisplayName(MapViewportProjection projection)
{
	switch (projection) {
	case MapViewportProjection::TopXY:
		return QCoreApplication::translate("VibeStudioMapViewport", "Top (X/Y)");
	case MapViewportProjection::FrontXZ:
		return QCoreApplication::translate("VibeStudioMapViewport", "Front (X/Z)");
	case MapViewportProjection::SideZY:
		return QCoreApplication::translate("VibeStudioMapViewport", "Side (Y/Z)");
	}
	return QCoreApplication::translate("VibeStudioMapViewport", "Top (X/Y)");
}

// ---------------------------------------------------------------------------
// Profile controls
// ---------------------------------------------------------------------------

void MapViewport::setControls(const PlanViewControls& controls)
{
	cancelInteraction();
	m_panning = false; m_handMode = false; m_panButton = Qt::NoButton; unsetCursor();
	m_controls = controls;
	update();
}

const PlanViewControls& MapViewport::controls() const
{
	return m_controls;
}

void MapViewport::setReservedShortcuts(const QList<QKeySequence>& sequences)
{
	for (auto* shortcut : findChildren<QShortcut*>(QString(), Qt::FindDirectChildrenOnly)) {
		const auto local = shortcut->key();
		const bool reserved = std::any_of(sequences.cbegin(), sequences.cend(), [&local](const QKeySequence& command) {
			return !command.isEmpty() && (local.matches(command) != QKeySequence::NoMatch || command.matches(local) != QKeySequence::NoMatch);
		});
		shortcut->setEnabled(!reserved);
	}
}

void MapViewport::setControlsHelp(const QString& text)
{
	m_controlsHelp = text;
	setAccessibleDescription(accessibleSummary());
}

void MapViewport::setCameraMarker(bool visible, const LevelMapVec3& position, double yawDegrees, double pitchDegrees, double fieldOfViewDegrees)
{
	const bool changed = visible != m_cameraMarkerVisible
		|| (visible
			&& (!qFuzzyCompare(position.x + 1.0, m_cameraPosition.x + 1.0) || !qFuzzyCompare(position.y + 1.0, m_cameraPosition.y + 1.0)
				|| !qFuzzyCompare(position.z + 1.0, m_cameraPosition.z + 1.0) || !qFuzzyCompare(yawDegrees + 1.0, m_cameraYaw + 1.0)
				|| !qFuzzyCompare(pitchDegrees + 1.0, m_cameraPitch + 1.0) || !qFuzzyCompare(fieldOfViewDegrees + 1.0, m_cameraFov + 1.0)));
	m_cameraMarkerVisible = visible;
	m_cameraPosition = position;
	m_cameraYaw = yawDegrees;
	m_cameraPitch = pitchDegrees;
	m_cameraFov = fieldOfViewDegrees;
	if (changed) {
		update();
	}
}

bool MapViewport::cameraMarkerVisible() const
{
	return m_cameraMarkerVisible;
}

bool MapViewport::isDrawingBrush() const
{
	return m_brushDrawing;
}

bool MapViewport::modifiersHeld(Qt::KeyboardModifiers held, Qt::KeyboardModifiers wanted)
{
	return wanted != Qt::NoModifier && (held & wanted) == wanted;
}

void MapViewport::beginPan(const QPointF& viewPoint, Qt::MouseButton button)
{
	m_panning = true;
	m_panButton = button;
	m_panAnchorView = viewPoint;
	m_panAnchorCenter = m_worldCenter;
	setCursor(Qt::ClosedHandCursor);
}

void MapViewport::setTemporaryPan(bool active, const QPointF& anchor)
{
	const bool previous = m_panHoldActive;
	if (active) {
		if (m_panHoldActive || m_controls.panHoldKey.isEmpty() || m_panning || m_dragging || m_banding
			|| m_pressArmed || m_resizing || m_clipDrawing || m_brushArmed || m_brushDrawing || m_clickArmed
			|| m_zoomDragging || m_rightArmed || m_cameraDrag != CameraDrag::None) { return; }
		m_panHoldActive = true;
		beginPan(anchor, Qt::NoButton);
	} else if (m_panHoldActive) {
		m_panHoldActive = false; m_panning = false; m_panButton = Qt::NoButton;
		if (m_handMode) { setCursor(Qt::OpenHandCursor); } else { unsetCursor(); }
	}
	if (previous != m_panHoldActive) {
		setAccessibleDescription(accessibleSummary());
		QAccessibleEvent change(this, QAccessible::DescriptionChanged); QAccessible::updateAccessibility(&change);
	}
}

bool MapViewport::isTemporarilyPanning() const { return m_panHoldActive; }

void MapViewport::keyReleaseEvent(QKeyEvent* event)
{
	if (!event->isAutoRepeat() && m_panHoldActive && navigationHoldKeyMatches(m_controls.panHoldKey, event->key(), Qt::NoModifier)) {
		setTemporaryPan(false); event->accept(); return;
	}
	QWidget::keyReleaseEvent(event);
}

void MapViewport::settleSelectionForMenu(const QPointF& viewPoint)
{
	// A right press settles the selection the way a plain left press does,
	// so a context menu acts on what is under the pointer: a member of the
	// set is promoted to primary without dropping the rest. Inside a Doom
	// sector with nothing else under the pointer, that is the sector, as
	// the hover outline shows.
	const MapViewportHit hit = hitOrSectorAt(viewPoint);
	if (hit.kind == LevelMapSelectionKind::None) {
		return;
	}
	if (!selectionContains(hit.kind, hit.objectId)) {
		replaceSelection(hit.kind, hit.objectId);
		announceSelection();
	} else if (hit.kind != m_selectionKind || hit.objectId != m_selectedObjectId) {
		addToSelection(hit.kind, hit.objectId);
		announceSelection();
	}
}

void MapViewport::driveCameraTo(const QPointF& viewPoint)
{
	const QPointF plane = viewToWorld(viewPoint);
	if (m_cameraDrag == CameraDrag::Place) {
		Q_EMIT cameraPlaceRequested(plane);
	} else if (m_cameraDrag == CameraDrag::Aim) {
		Q_EMIT cameraAimRequested(plane);
	}
}

bool MapViewport::canDrawBrush() const
{
	return m_hasDocument && !documentIsDoom(m_document) && !m_clipMode && !m_drawMode;
}

void MapViewport::armEmptyDrag(PlanEmptyDrag action, const QPointF& viewPoint)
{
	switch (action) {
	case PlanEmptyDrag::DrawBrush:
		if (canDrawBrush()) {
			m_brushArmed = true;
			m_brushFromPlane = snappedPlanePoint(viewPoint);
			m_brushToPlane = m_brushFromPlane;
		}
		break;
	case PlanEmptyDrag::ResizeSelection: {
		// The sides of the selection's box that face the press point follow
		// the pointer, as Radiant drags the faces a press is outside of.
		QRectF box;
		if (!resizeBox(&box, nullptr, nullptr)) {
			break;
		}
		const QPointF point = viewToWorld(viewPoint);
		int edges = 0;
		if (point.x() < box.left()) {
			edges |= ResizeMinHorizontal;
		} else if (point.x() > box.right()) {
			edges |= ResizeMaxHorizontal;
		}
		if (point.y() < box.top()) {
			edges |= ResizeMinVertical;
		} else if (point.y() > box.bottom()) {
			edges |= ResizeMaxVertical;
		}
		m_pendingResizeEdges = edges;
		break;
	}
	case PlanEmptyDrag::BoxSelect:
		break;
	}
}

void MapViewport::beginBrushDraw(const QPointF& viewPoint, Qt::KeyboardModifiers modifiers)
{
	m_brushArmed = false;
	m_brushDrawing = true;
	updateBrushDraw(viewPoint, modifiers);
}

void MapViewport::updateBrushDraw(const QPointF& viewPoint, Qt::KeyboardModifiers modifiers)
{
	m_brushToPlane = snappedPlanePoint(viewPoint);
	m_brushModifiers = modifiers;
	Q_EMIT hoverChanged(brushSummary());
	update();
}

QRectF MapViewport::brushPlaneBox() const
{
	QPointF to = m_brushToPlane;
	const Qt::KeyboardModifiers keys = m_brushModifiers & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
	if (modifiersHeld(keys, m_controls.squareModifiers) || modifiersHeld(keys, m_controls.cubeModifiers)) {
		// Square: the longer side both ways, growing the way the pointer went.
		const double dx = to.x() - m_brushFromPlane.x();
		const double dy = to.y() - m_brushFromPlane.y();
		const double side = std::max(std::abs(dx), std::abs(dy));
		to = QPointF(m_brushFromPlane.x() + std::copysign(side, dx == 0.0 ? 1.0 : dx),
			m_brushFromPlane.y() + std::copysign(side, dy == 0.0 ? 1.0 : dy));
	}
	return minMaxRect(std::min(m_brushFromPlane.x(), to.x()), std::min(m_brushFromPlane.y(), to.y()), std::max(m_brushFromPlane.x(), to.x()),
		std::max(m_brushFromPlane.y(), to.y()));
}

void MapViewport::brushHiddenRange(double* low, double* high) const
{
	const auto range = brushDepthRange(m_projection == MapViewportProjection::TopXY ? 2
		: m_projection == MapViewportProjection::FrontXZ ? 1 : 0);
	*low = range.first; *high = range.second;
}

QPair<double,double> MapViewport::brushDepthRange(int hiddenAxis) const
{
	// Along the axis the view hides, a new brush spans the last selection
	// there (Radiant's work zone), else 0 to 64.
	double from = 0.0;
	double to = 64.0;
	if (m_hasWorkZone) {
		switch (hiddenAxis) {
		case 2:
			from = m_workMins.z;
			to = m_workMaxs.z;
			break;
		case 1:
			from = m_workMins.y;
			to = m_workMaxs.y;
			break;
		case 0:
			from = m_workMins.x;
			to = m_workMaxs.x;
			break;
		}
	}
	const double grid = m_gridSize > 0 ? static_cast<double>(m_gridSize) : 1.0;
	if (to - from < 1.0) {
		to = from + std::max(64.0, grid);
	}
	return {from,to};
}

QString MapViewport::brushSummary() const
{
	const QRectF box = brushPlaneBox();
	return tr("New brush %1 %2 by %3 %4").arg(planeAxisLetters(m_projection, false)).arg(box.width(), 0, 'g', 8)
		.arg(planeAxisLetters(m_projection, true))
		.arg(box.height(), 0, 'g', 8);
}

void MapViewport::commitBrushDraw()
{
	m_brushDrawing = false;
	const QRectF box = brushPlaneBox();
	const Qt::KeyboardModifiers keys = m_brushModifiers & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
	m_brushModifiers = Qt::NoModifier;
	update();
	Q_EMIT hoverChanged(hoverSummary());
	// A line or a point draws nothing.
	if (box.width() <= 0.0 || box.height() <= 0.0) {
		return;
	}
	double low = 0.0;
	double high = 0.0;
	brushHiddenRange(&low, &high);
	if (modifiersHeld(keys, m_controls.cubeModifiers)) {
		high = low + std::max(box.width(), box.height());
	}
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	switch (m_projection) {
	case MapViewportProjection::TopXY:
		mins = makeVec(box.left(), box.top(), low);
		maxs = makeVec(box.right(), box.bottom(), high);
		break;
	case MapViewportProjection::FrontXZ:
		mins = makeVec(box.left(), low, box.top());
		maxs = makeVec(box.right(), high, box.bottom());
		break;
	case MapViewportProjection::SideZY:
		mins = makeVec(low, box.left(), box.top());
		maxs = makeVec(high, box.right(), box.bottom());
		break;
	}
	Q_EMIT brushDrawRequested(mins, maxs);
}

void MapViewport::updateWorkZone()
{
	LevelMapVec3 low;
	LevelMapVec3 high;
	if (selectionTransformBounds(&low, &high)) {
		m_workMins = low;
		m_workMaxs = high;
		m_hasWorkZone = true;
	}
}

QVector<LevelMapSelectionRef> MapViewport::objectsAt(const QPointF& viewPoint) const
{
	// Everything under the pointer, the nearest the viewer first: markers
	// and lines near it, then the brushes and patches whose outline holds
	// it, topmost first and the smaller of two at one height before the
	// larger. Radiant's tunnel selection steps down this stack.
	QVector<LevelMapSelectionRef> stack;
	if (!m_hasDocument) {
		return stack;
	}
	if (documentIsDoom(m_document)) {
		const MapViewportHit nearest = hitOrSectorAt(viewPoint);
		if (nearest.kind != LevelMapSelectionKind::None) {
			stack.push_back({nearest.kind, nearest.objectId});
		}
		return stack;
	}
	const QPointF world = viewToWorld(viewPoint);
	struct Candidate {
		LevelMapSelectionRef ref;
		double top = 0.0;
		double area = 0.0;
	};
	// Every entity marker within reach, the topmost first: a light over a
	// player start is two clicks, not one hidden under the other.
	QVector<Candidate> markers;
	for (const LevelMapEntity& entity : m_document.entities) {
		if (!entity.origin.valid) {
			continue;
		}
		const QPointF point = mapViewportProjectPoint(m_projection, entity.origin);
		const QPointF view = worldToView(point.x(), point.y());
		const double distance = std::hypot(view.x() - viewPoint.x(), view.y() - viewPoint.y());
		if (distance <= kPickRadius) {
			const double top = m_projection == MapViewportProjection::TopXY ? entity.origin.z
				: m_projection == MapViewportProjection::FrontXZ	? -entity.origin.y
											: entity.origin.x;
			markers.push_back({{LevelMapSelectionKind::Entity, entity.id}, top, distance});
		}
	}
	std::stable_sort(markers.begin(), markers.end(), [](const Candidate& left, const Candidate& right) {
		if (!qFuzzyCompare(left.top + 1.0, right.top + 1.0)) {
			return left.top > right.top;
		}
		return left.area < right.area;
	});
	for (const Candidate& marker : markers) {
		stack.push_back(marker.ref);
	}
	QVector<Candidate> containing;
	const auto hiddenTop = [this](const LevelMapVec3& maxs) {
		switch (m_projection) {
		case MapViewportProjection::TopXY:
			return maxs.z;
		case MapViewportProjection::FrontXZ:
			return -maxs.y;
		case MapViewportProjection::SideZY:
			return maxs.x;
		}
		return maxs.z;
	};
	for (const MapBrushGeometry& brush : m_brushGeometry) {
		if (!brush.solved) {
			continue;
		}
		const QRectF bounds = mapViewportBounds(m_projection, brush.mins, brush.maxs);
		if (!bounds.contains(world)) {
			continue;
		}
		bool inside = false;
		if (m_projection == MapViewportProjection::TopXY) {
			for (const QPolygonF& polygon : brush.footprintPolygons()) {
				inside = inside || polygon.containsPoint(world, Qt::OddEvenFill);
			}
		} else {
			for (const MapFacePolygon& face : brush.faces) {
				QPolygonF polygon;
				for (const LevelMapVec3& point : face.points) {
					polygon.append(mapViewportProjectPoint(m_projection, point));
				}
				inside = inside || (polygon.size() >= 3 && polygon.containsPoint(world, Qt::OddEvenFill));
			}
		}
		if (inside) {
			containing.push_back({{LevelMapSelectionKind::QuakeBrush, brush.brushId}, hiddenTop(brush.maxs), bounds.width() * bounds.height()});
		}
	}
	for (const LevelMapPatch& patch : m_document.patches) {
		if (!patch.mins.valid || !patch.maxs.valid) {
			continue;
		}
		const QRectF bounds = mapViewportBounds(m_projection, patch.mins, patch.maxs);
		if (bounds.contains(world)) {
			containing.push_back({{LevelMapSelectionKind::QuakePatch, patch.id}, hiddenTop(patch.maxs), bounds.width() * bounds.height()});
		}
	}
	std::stable_sort(containing.begin(), containing.end(), [](const Candidate& left, const Candidate& right) {
		if (!qFuzzyCompare(left.top + 1.0, right.top + 1.0)) {
			return left.top > right.top;
		}
		return left.area < right.area;
	});
	for (const Candidate& candidate : containing) {
		const bool listed = std::any_of(stack.cbegin(), stack.cend(), [&candidate](const LevelMapSelectionRef& ref) {
			return ref.kind == candidate.ref.kind && ref.objectId == candidate.ref.objectId;
		});
		if (!listed) {
			stack.push_back(candidate.ref);
		}
	}
	if (stack.isEmpty()) {
		// Just outside every outline: the nearest edge within reach.
		const MapViewportHit nearest = hitTest(viewPoint);
		if (nearest.kind != LevelMapSelectionKind::None) {
			stack.push_back({nearest.kind, nearest.objectId});
		}
	}
	return stack;
}

void MapViewport::cycleSelectionAt(const QPointF& viewPoint)
{
	const QVector<LevelMapSelectionRef> stack = objectsAt(viewPoint);
	if (stack.isEmpty()) {
		if (!m_selection.isEmpty()) {
			m_selection.clear();
			syncPrimaryFromSelection();
			announceSelection();
		}
		return;
	}
	// Clicking again on what is selected steps to the next one down.
	int next = 0;
	if (m_selection.size() == 1) {
		for (int index = 0; index < stack.size(); ++index) {
			if (stack.at(index).kind == m_selection.first().kind && stack.at(index).objectId == m_selection.first().objectId) {
				next = (index + 1) % static_cast<int>(stack.size());
				break;
			}
		}
	}
	replaceSelection(stack.at(next).kind, stack.at(next).objectId);
	announceSelection();
}

void MapViewport::setCameraBrushDraft(const LevelMapVec3& mins, const LevelMapVec3& maxs)
{
	const std::array<double,6> box{mins.x,mins.y,mins.z,maxs.x,maxs.y,maxs.z};
	const bool valid = mins.valid && maxs.valid && m_hasDocument && !documentIsDoom(m_document)
		&& std::all_of(box.begin(),box.end(),[](double value) { return std::isfinite(value); })
		&& maxs.x > mins.x && maxs.y > mins.y && maxs.z > mins.z;
	if (valid == m_hasCameraBrushDraft && (!valid || box == m_cameraBrushDraft)) { return; }
	m_hasCameraBrushDraft = valid; m_cameraBrushDraft = box;
	setAccessibleDescription(accessibleSummary()); update();
}

void MapViewport::paintBrushDraw(QPainter& painter, const Palette& palette) const
{
	if (!m_brushDrawing && !m_hasCameraBrushDraft) {
		return;
	}
	const QRectF box = m_brushDrawing ? brushPlaneBox() : QRectF(
		projectPoint({m_cameraBrushDraft[0],m_cameraBrushDraft[1],m_cameraBrushDraft[2],true}),
		projectPoint({m_cameraBrushDraft[3],m_cameraBrushDraft[4],m_cameraBrushDraft[5],true})).normalized();
	const QRectF view = QRectF(worldToView(box.left(), box.bottom()), worldToView(box.right(), box.top())).normalized();
	const QColor color = !m_brushDrawing && !m_highContrast ? QColor(95,220,240) : palette.selection;
	QColor fill = color;
	fill.setAlpha(m_highContrast ? 80 : 56);
	painter.setBrush(fill);
	painter.setPen(QPen(color, m_highContrast ? 2.5 : 1.8, m_brushDrawing ? Qt::SolidLine : Qt::DashLine));
	painter.drawRect(view);
	painter.setBrush(Qt::NoBrush);
	// The size beside the box, as Radiant and TrenchBroom show it.
	painter.setPen(palette.text);
	painter.drawText(view.bottomLeft() + QPointF(0.0, painter.fontMetrics().height()),
		QStringLiteral("%1 %2 %3").arg(box.width(), 0, 'g', 8).arg(QChar(0x00d7)).arg(box.height(), 0, 'g', 8));
}

void MapViewport::paintCameraMarker(QPainter& painter, const Palette& palette) const
{
	if (!m_cameraMarkerVisible) {
		return;
	}
	// Where the 3D camera stands and which way it looks, drawn as Radiant
	// draws it: a dot with its field of view opening ahead of it.
	const QPointF at = mapViewportProjectPoint(m_projection, m_cameraPosition);
	const QPointF view = worldToView(at.x(), at.y());
	const double yaw = m_cameraYaw * kPi / 180.0;
	const double pitch = m_cameraPitch * kPi / 180.0;
	const LevelMapVec3 ahead = makeVec(m_cameraPosition.x + std::cos(pitch) * std::cos(yaw), m_cameraPosition.y + std::cos(pitch) * std::sin(yaw),
		m_cameraPosition.z + std::sin(pitch));
	const QPointF aheadPlane = mapViewportProjectPoint(m_projection, ahead);
	QPointF direction = worldToView(aheadPlane.x(), aheadPlane.y()) - view;
	const double length = std::hypot(direction.x(), direction.y());
	const QColor colour = m_highContrast ? QColor(0, 255, 255) : QColor(96, 196, 255);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setPen(QPen(colour, m_highContrast ? 2.2 : 1.6));
	painter.setBrush(Qt::NoBrush);
	painter.drawEllipse(view, 5.0, 5.0);
	if (length > 1e-6) {
		direction /= length;
		// The wedge opens by the field of view in the top view; seen from the
		// side the heading is an arrow.
		const double half = m_projection == MapViewportProjection::TopXY ? m_cameraFov * kPi / 360.0 : 0.18;
		const double reach = 34.0;
		const auto turned = [&direction](double angle) {
			return QPointF(direction.x() * std::cos(angle) - direction.y() * std::sin(angle), direction.x() * std::sin(angle) + direction.y() * std::cos(angle));
		};
		painter.drawLine(view, view + turned(half) * reach);
		painter.drawLine(view, view + turned(-half) * reach);
		painter.setPen(QPen(colour, m_highContrast ? 2.6 : 2.0));
		painter.drawLine(view, view + direction * (reach * 0.7));
	}
	painter.setPen(palette.subtleText);
	painter.drawText(view + QPointF(8.0, -8.0), tr("Camera"));
}

} // namespace vibestudio
