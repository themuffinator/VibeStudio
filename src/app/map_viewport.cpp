#include "app/map_viewport.h"

#include <QBrush>
#include <QCoreApplication>
#include <QFont>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QFontMetricsF>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QResizeEvent>
#include <QShortcut>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio {

namespace {

// Free helpers in this file cannot use tr(); MapViewport members can, because
// the class carries Q_OBJECT.
QString viewText(const char* source)
{
	return QCoreApplication::translate("VibeStudioMapViewport", source);
}

constexpr double kMinZoom = 0.0025;
constexpr double kMaxZoom = 64.0;
constexpr double kFitMargin = 24.0;
constexpr double kPickRadius = 7.0;
constexpr double kMinGridSpacingPixels = 6.0;
constexpr int kMajorGridInterval = 8;
constexpr int kMaxGridLines = 1024;
constexpr int kMaxLabels = 160;
constexpr int kPatchSubdivisions = 3;
constexpr double kPi = 3.14159265358979323846;
// Pixels the pointer must travel before a press on a selected object turns into
// a move; below this a press-and-release is still just a click.
constexpr double kDragThresholdPixels = 4.0;
// Upper bounds on overlay work, so a selection of tens of thousands of objects
// cannot make painting crawl.
constexpr int kMaxSelectionMarkers = 512;
constexpr int kMaxPreviewOutlines = 512;
// Shift multiplies the arrow-key nudge.
constexpr int kCoarseNudgeMultiplier = 8;

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

QPointF projectVec(MapViewportProjection projection, const LevelMapVec3& point)
{
	switch (projection) {
	case MapViewportProjection::TopXY:
		return QPointF(point.x, point.y);
	case MapViewportProjection::FrontXZ:
		return QPointF(point.x, point.z);
	case MapViewportProjection::SideZY:
		return QPointF(point.y, point.z);
	}
	return QPointF(point.x, point.y);
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

QRectF projectedBoundsRect(MapViewportProjection projection, const LevelMapVec3& mins, const LevelMapVec3& maxs)
{
	const QPointF a = projectVec(projection, mins);
	const QPointF b = projectVec(projection, maxs);
	return minMaxRect(std::min(a.x(), b.x()), std::min(a.y(), b.y()), std::max(a.x(), b.x()), std::max(a.y(), b.y()));
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

const LevelMapDoomSector* doomSectorForLinedef(const LevelMapDocument& document, const LevelMapDoomLinedef& linedef)
{
	const LevelMapDoomSidedef* side = findById(document.doomSidedefs, linedef.frontSidedef);
	if (side == nullptr) {
		side = findById(document.doomSidedefs, linedef.backSidedef);
	}
	if (side == nullptr) {
		return nullptr;
	}
	return findById(document.doomSectors, side->sector);
}

// Doom vertices are two-dimensional; elevation views therefore place a linedef
// at the floor height of the sector it fronts (see
// https://doomwiki.org/wiki/Sector). Things carry no height in the vanilla
// format, so they stay at zero.
double doomLinedefHeight(const LevelMapDocument& document, const LevelMapDoomLinedef& linedef)
{
	const LevelMapDoomSector* sector = doomSectorForLinedef(document, linedef);
	return sector != nullptr ? static_cast<double>(sector->floorHeight) : 0.0;
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
			: viewText("unnamed");
		return viewText("Entity %1 (%2)").arg(id).arg(className);
	}
	case LevelMapSelectionKind::DoomVertex:
		return viewText("Vertex %1").arg(id);
	case LevelMapSelectionKind::DoomLinedef: {
		const LevelMapDoomLinedef* linedef = findById(document.doomLinedefs, id);
		if (linedef == nullptr) {
			return viewText("Linedef %1").arg(id);
		}
		return doomLinedefIsTwoSided(*linedef) ? viewText("Linedef %1 (two-sided)").arg(id)
						      : viewText("Linedef %1 (one-sided)").arg(id);
	}
	case LevelMapSelectionKind::DoomThing: {
		const LevelMapDoomThing* thing = findById(document.doomThings, id);
		if (thing == nullptr) {
			return viewText("Thing %1").arg(id);
		}
		return viewText("Thing %1 (type %2, angle %3)").arg(id).arg(thing->type).arg(thing->angle);
	}
	case LevelMapSelectionKind::DoomSector: {
		const LevelMapDoomSector* sector = findById(document.doomSectors, id);
		if (sector == nullptr) {
			return viewText("Sector %1").arg(id);
		}
		return viewText("Sector %1 (light %2, floor %3, ceiling %4)")
			.arg(id)
			.arg(sector->lightLevel)
			.arg(sector->floorHeight)
			.arg(sector->ceilingHeight);
	}
	case LevelMapSelectionKind::QuakeBrush:
		return viewText("Brush %1").arg(id);
	case LevelMapSelectionKind::QuakePatch:
		return viewText("Patch %1").arg(id);
	}
	return viewText("Nothing selected");
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

bool objectWorldPoint(const LevelMapDocument& document, const QVector<DoomSectorOutline>& outlines,
	const QVector<MapBrushGeometry>& brushGeometry, MapViewportProjection projection, LevelMapSelectionKind kind,
	int id, QPointF* out)
{
	if (out == nullptr || id < 0) {
		return false;
	}
	switch (kind) {
	case LevelMapSelectionKind::None:
		return false;
	case LevelMapSelectionKind::Entity: {
		const LevelMapEntity* entity = findById(document.entities, id);
		if (entity == nullptr || !entity->origin.valid) {
			return false;
		}
		*out = projectVec(projection, entity->origin);
		return true;
	}
	case LevelMapSelectionKind::DoomVertex: {
		const LevelMapDoomVertex* vertex = findById(document.doomVertices, id);
		if (vertex == nullptr) {
			return false;
		}
		*out = projectVec(projection, makeVec(vertex->x, vertex->y, 0.0));
		return true;
	}
	case LevelMapSelectionKind::DoomLinedef: {
		const LevelMapDoomLinedef* linedef = findById(document.doomLinedefs, id);
		if (linedef == nullptr) {
			return false;
		}
		const LevelMapDoomVertex* start = findById(document.doomVertices, linedef->startVertex);
		const LevelMapDoomVertex* end = findById(document.doomVertices, linedef->endVertex);
		if (start == nullptr || end == nullptr) {
			return false;
		}
		const double height = projection == MapViewportProjection::TopXY ? 0.0 : doomLinedefHeight(document, *linedef);
		const QPointF a = projectVec(projection, makeVec(start->x, start->y, height));
		const QPointF b = projectVec(projection, makeVec(end->x, end->y, height));
		*out = QPointF((a.x() + b.x()) * 0.5, (a.y() + b.y()) * 0.5);
		return true;
	}
	case LevelMapSelectionKind::DoomThing: {
		const LevelMapDoomThing* thing = findById(document.doomThings, id);
		if (thing == nullptr) {
			return false;
		}
		*out = projectVec(projection, makeVec(thing->x, thing->y, 0.0));
		return true;
	}
	case LevelMapSelectionKind::DoomSector: {
		for (const DoomSectorOutline& outline : outlines) {
			if (outline.sectorId == id && !outline.bounds.isNull()) {
				*out = outline.bounds.center();
				return true;
			}
		}
		return false;
	}
	case LevelMapSelectionKind::QuakeBrush: {
		for (const MapBrushGeometry& brush : brushGeometry) {
			if (brush.brushId != id) {
				continue;
			}
			if (!brush.solved) {
				break;
			}
			*out = projectedBoundsRect(projection, brush.mins, brush.maxs).center();
			return true;
		}
		const LevelMapBrush* brush = findById(document.brushes, id);
		if (brush == nullptr || !brush->boundsSolved) {
			return false;
		}
		*out = projectedBoundsRect(projection, brush->mins, brush->maxs).center();
		return true;
	}
	case LevelMapSelectionKind::QuakePatch: {
		const LevelMapPatch* patch = findById(document.patches, id);
		if (patch == nullptr || patch->controlPoints.isEmpty()) {
			return false;
		}
		if (patch->mins.valid && patch->maxs.valid) {
			*out = projectedBoundsRect(projection, patch->mins, patch->maxs).center();
			return true;
		}
		QPointF sum(0.0, 0.0);
		for (const LevelMapVec3& point : patch->controlPoints) {
			sum += projectVec(projection, point);
		}
		*out = sum / static_cast<double>(patch->controlPoints.size());
		return true;
	}
	}
	return false;
}

// Projected extent of an object. Point-like objects (things, entities,
// vertices) report a zero-size rectangle at their position, which is exactly
// what rubber-band containment and the drag preview want.
bool objectWorldBounds(const LevelMapDocument& document, const QVector<DoomSectorOutline>& outlines,
	const QVector<MapBrushGeometry>& brushGeometry, MapViewportProjection projection, LevelMapSelectionKind kind,
	int id, QRectF* out)
{
	if (out == nullptr || id < 0) {
		return false;
	}
	switch (kind) {
	case LevelMapSelectionKind::None:
		return false;
	case LevelMapSelectionKind::DoomLinedef: {
		const LevelMapDoomLinedef* linedef = findById(document.doomLinedefs, id);
		if (linedef == nullptr) {
			return false;
		}
		const LevelMapDoomVertex* start = findById(document.doomVertices, linedef->startVertex);
		const LevelMapDoomVertex* end = findById(document.doomVertices, linedef->endVertex);
		if (start == nullptr || end == nullptr) {
			return false;
		}
		const double height = projection == MapViewportProjection::TopXY ? 0.0 : doomLinedefHeight(document, *linedef);
		const QPointF a = projectVec(projection, makeVec(start->x, start->y, height));
		const QPointF b = projectVec(projection, makeVec(end->x, end->y, height));
		*out = minMaxRect(std::min(a.x(), b.x()), std::min(a.y(), b.y()), std::max(a.x(), b.x()), std::max(a.y(), b.y()));
		return true;
	}
	case LevelMapSelectionKind::DoomSector: {
		for (const DoomSectorOutline& outline : outlines) {
			if (outline.sectorId == id) {
				if (outline.bounds.isNull()) {
					return false;
				}
				*out = outline.bounds;
				return true;
			}
		}
		return false;
	}
	case LevelMapSelectionKind::QuakeBrush: {
		for (const MapBrushGeometry& brush : brushGeometry) {
			if (brush.brushId != id) {
				continue;
			}
			if (brush.solved) {
				*out = projectedBoundsRect(projection, brush.mins, brush.maxs);
				return true;
			}
			break;
		}
		const LevelMapBrush* brush = findById(document.brushes, id);
		if (brush == nullptr || !brush->boundsSolved) {
			return false;
		}
		*out = projectedBoundsRect(projection, brush->mins, brush->maxs);
		return true;
	}
	case LevelMapSelectionKind::QuakePatch: {
		const LevelMapPatch* patch = findById(document.patches, id);
		if (patch == nullptr) {
			return false;
		}
		if (patch->mins.valid && patch->maxs.valid) {
			*out = projectedBoundsRect(projection, patch->mins, patch->maxs);
			return true;
		}
		if (patch->controlPoints.isEmpty()) {
			return false;
		}
		QPointF minimum = projectVec(projection, patch->controlPoints.first());
		QPointF maximum = minimum;
		for (const LevelMapVec3& point : patch->controlPoints) {
			const QPointF projected = projectVec(projection, point);
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
	if (!objectWorldPoint(document, outlines, brushGeometry, projection, kind, id, &point)) {
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
};

MapViewport::MapViewport(QWidget* parent)
	: QWidget(parent)
{
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);
	setAutoFillBackground(false);
	setAttribute(Qt::WA_OpaquePaintEvent, true);
	setAccessibleName(tr("Map viewport"));
	setAccessibleDescription(accessibleSummary());
	m_worldBounds = minMaxRect(-512.0, -512.0, 512.0, 512.0);
	m_worldCenter = m_worldBounds.center();

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

MapViewport::~MapViewport() = default;

void MapViewport::setDocument(const LevelMapDocument& document)
{
	m_document = document;
	m_hasDocument = true;
	adoptDocumentSelection();
	m_hover = MapViewportHit();
	m_panning = false;
	m_pressArmed = false;
	m_dragging = false;
	m_banding = false;
	unsetCursor();
	rebuildGeometry();
	updateWorldBounds();
	zoomToFit();
	setAccessibleDescription(accessibleSummary());
	update();
}

void MapViewport::updateDocument(const LevelMapDocument& document)
{
	if (!m_hasDocument) {
		setDocument(document);
		return;
	}
	// An edit changes geometry, not the camera: keep the pan and zoom the user
	// set, and only re-solve what is drawn.
	m_document = document;
	adoptDocumentSelection();
	m_hover = MapViewportHit();
	m_pressArmed = false;
	m_dragging = false;
	m_banding = false;
	rebuildGeometry();
	updateWorldBounds();
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
	for (const LevelMapSelectionRef& ref : selection) {
		if (ref.kind == LevelMapSelectionKind::None || ref.objectId < 0) {
			continue;
		}
		// Keep the caller's last occurrence so their final entry is primary.
		m_selection.removeAll(ref);
		m_selection.push_back(ref);
	}
	syncPrimaryFromSelection();
}

void MapViewport::clearDocument()
{
	m_document = LevelMapDocument();
	m_hasDocument = false;
	m_brushGeometry.clear();
	m_sectorOutlines.clear();
	m_selection.clear();
	m_selectionKind = LevelMapSelectionKind::None;
	m_selectedObjectId = -1;
	m_hover = MapViewportHit();
	m_panning = false;
	m_pressArmed = false;
	m_dragging = false;
	m_banding = false;
	unsetCursor();
	m_worldBounds = minMaxRect(-512.0, -512.0, 512.0, 512.0);
	m_worldCenter = m_worldBounds.center();
	m_zoom = 1.0;
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT viewChanged();
}

bool MapViewport::hasDocument() const
{
	return m_hasDocument;
}

void MapViewport::setProjection(MapViewportProjection projection)
{
	if (m_projection == projection) {
		return;
	}
	// The drag plane and the rubber band are both expressed in the projected
	// axes, so neither survives a projection change.
	cancelInteraction();
	m_projection = projection;
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
	replaceSelection(kind, objectId);
	setAccessibleDescription(accessibleSummary());
	update();
}

void MapViewport::setSelectionSet(const QVector<LevelMapSelectionRef>& selection)
{
	setSelectionSetInternal(selection);
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
	const bool wasBusy = m_dragging || m_banding || m_pressArmed;
	m_pressArmed = false;
	m_dragging = false;
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

void MapViewport::setHighContrast(bool enabled)
{
	if (m_highContrast == enabled) {
		return;
	}
	m_highContrast = enabled;
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
	if (documentIsDoom(m_document)) {
		lines << tr("Vertices %1, linedefs %2, sectors %3, things %4")
				 .arg(m_document.doomVertices.size())
				 .arg(m_document.doomLinedefs.size())
				 .arg(m_document.doomSectors.size())
				 .arg(m_document.doomThings.size());
		int openSectors = 0;
		for (const DoomSectorOutline& outline : m_sectorOutlines) {
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
	lines << (m_snapToGrid ? tr("Snap: on (%1 units)").arg(m_gridSize) : tr("Snap: off"));
	if (m_dragging) {
		lines << dragSummary();
	}
	return lines;
}

QString MapViewport::dragSummary() const
{
	const QPointF delta = snappedPlaneDelta();
	const QString deltaText = planePointText(m_projection, delta);
	QPointF world;
	if (m_selectionKind != LevelMapSelectionKind::None
		&& objectWorldPoint(m_document, m_sectorOutlines, m_brushGeometry, m_projection, m_selectionKind,
			m_selectedObjectId, &world)) {
		return tr("Moving %n object(s) by %1 - destination %2", nullptr, static_cast<int>(m_selection.size()))
			.arg(deltaText, planePointText(m_projection, world + delta));
	}
	return tr("Moving %n object(s) by %1", nullptr, static_cast<int>(m_selection.size())).arg(deltaText);
}

QString MapViewport::accessibleSummary() const
{
	if (!m_hasDocument) {
		return tr("Map viewport: no map is loaded, so there is nothing to draw.");
	}
	const QString name = m_document.mapName.isEmpty() ? tr("an unnamed map") : m_document.mapName;
	QString selection = m_selectionKind == LevelMapSelectionKind::None
		? tr("nothing selected")
		: objectLabelText(m_document, m_selectionKind, m_selectedObjectId);
	if (m_selection.size() > 1) {
		selection = tr("%1, primary of %n selected object(s)", nullptr, static_cast<int>(m_selection.size()))
				    .arg(selection);
	}
	if (documentIsDoom(m_document)) {
		return tr("Map viewport showing %1 in the %2 projection at %3 percent zoom, with %4 linedefs, %5 sectors "
			  "and %6 things; %7.")
			.arg(name)
			.arg(mapViewportProjectionDisplayName(m_projection))
			.arg(m_zoom * 100.0, 0, 'f', 0)
			.arg(m_document.doomLinedefs.size())
			.arg(m_document.doomSectors.size())
			.arg(m_document.doomThings.size())
			.arg(selection);
	}
	return tr("Map viewport showing %1 in the %2 projection at %3 percent zoom, with %4 brushes, %5 patches and %6 "
		  "entities; %7.")
		.arg(name)
		.arg(mapViewportProjectionDisplayName(m_projection))
		.arg(m_zoom * 100.0, 0, 'f', 0)
		.arg(m_document.brushes.size())
		.arg(m_document.patches.size())
		.arg(m_document.entities.size())
		.arg(selection);
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
	// Solving happens here and only here: paintEvent must never rebuild.
	m_brushGeometry.clear();
	m_sectorOutlines.clear();
	if (!m_hasDocument) {
		return;
	}
	if (documentIsDoom(m_document)) {
		m_sectorOutlines = buildDoomSectorOutlines(m_document);
	}
	if (!m_document.brushes.isEmpty()) {
		m_brushGeometry = buildLevelMapBrushGeometry(m_document);
	}
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
		for (const LevelMapDoomSector& sector : m_document.doomSectors) {
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
		for (const LevelMapDoomVertex& vertex : m_document.doomVertices) {
			add(projectVec(m_projection, makeVec(vertex.x, vertex.y, 0.0)));
			if (heights && m_projection != MapViewportProjection::TopXY) {
				add(projectVec(m_projection, makeVec(vertex.x, vertex.y, floorLow)));
				add(projectVec(m_projection, makeVec(vertex.x, vertex.y, ceilingHigh)));
			}
		}
		for (const LevelMapDoomThing& thing : m_document.doomThings) {
			add(projectVec(m_projection, makeVec(thing.x, thing.y, 0.0)));
		}
		for (const MapBrushGeometry& brush : m_brushGeometry) {
			if (!brush.solved) {
				continue;
			}
			const QRectF bounds = projectedBoundsRect(m_projection, brush.mins, brush.maxs);
			add(bounds.topLeft());
			add(bounds.bottomRight());
		}
		for (const LevelMapBrush& brush : m_document.brushes) {
			if (!brush.boundsSolved) {
				continue;
			}
			const QRectF bounds = projectedBoundsRect(m_projection, brush.mins, brush.maxs);
			add(bounds.topLeft());
			add(bounds.bottomRight());
		}
		for (const LevelMapPatch& patch : m_document.patches) {
			for (const LevelMapVec3& point : patch.controlPoints) {
				add(projectVec(m_projection, point));
			}
		}
		for (const LevelMapEntity& entity : m_document.entities) {
			if (entity.origin.valid) {
				add(projectVec(m_projection, entity.origin));
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

QPointF MapViewport::projectPoint(const LevelMapVec3& point) const
{
	return projectVec(m_projection, point);
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
		const QPointF point = projectVec(m_projection, makeVec(thing.x, thing.y, 0.0));
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
			const QPointF point = projectVec(m_projection, entity.origin);
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
		const QPointF point = projectVec(m_projection, makeVec(vertex.x, vertex.y, 0.0));
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
		const LevelMapDoomVertex* start = findById(m_document.doomVertices, linedef.startVertex);
		const LevelMapDoomVertex* end = findById(m_document.doomVertices, linedef.endVertex);
		if (start == nullptr || end == nullptr) {
			continue;
		}
		const double height = m_projection == MapViewportProjection::TopXY
			? 0.0
			: doomLinedefHeight(m_document, linedef);
		const QPointF a = projectVec(m_projection, makeVec(start->x, start->y, height));
		const QPointF b = projectVec(m_projection, makeVec(end->x, end->y, height));
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
		const QRectF bounds = projectedBoundsRect(m_projection, brush.mins, brush.maxs);
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
					const QPointF projected = projectVec(m_projection, point);
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
		const QRectF bounds = projectedBoundsRect(m_projection, patch.mins, patch.maxs);
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
	// Each kind walks its own container rather than going through
	// objectWorldBounds() by id, because that helper scans the solved brush list
	// per lookup and this runs on every pointer move while the band is open.
	const int worldspawnId = worldspawnEntityId(m_document);
	const auto keep = [&](LevelMapSelectionKind kind, int id, const QRectF& bounds) {
		if (id >= 0 && rectContainsRect(area, bounds)) {
			result.push_back(LevelMapSelectionRef {kind, id});
		}
	};
	const auto pointRect = [](const QPointF& point) { return QRectF(point, QSizeF(0.0, 0.0)); };

	for (const LevelMapSelectionKind kind : selectionTiers(m_document)) {
		switch (kind) {
		case LevelMapSelectionKind::DoomThing:
			for (const LevelMapDoomThing& thing : m_document.doomThings) {
				keep(kind, thing.id, pointRect(projectVec(m_projection, makeVec(thing.x, thing.y, 0.0))));
			}
			break;
		case LevelMapSelectionKind::DoomVertex:
			for (const LevelMapDoomVertex& vertex : m_document.doomVertices) {
				keep(kind, vertex.id, pointRect(projectVec(m_projection, makeVec(vertex.x, vertex.y, 0.0))));
			}
			break;
		case LevelMapSelectionKind::DoomLinedef:
			for (const LevelMapDoomLinedef& linedef : m_document.doomLinedefs) {
				const LevelMapDoomVertex* start = findById(m_document.doomVertices, linedef.startVertex);
				const LevelMapDoomVertex* end = findById(m_document.doomVertices, linedef.endVertex);
				if (start == nullptr || end == nullptr) {
					continue;
				}
				const double height = m_projection == MapViewportProjection::TopXY
					? 0.0
					: doomLinedefHeight(m_document, linedef);
				const QPointF a = projectVec(m_projection, makeVec(start->x, start->y, height));
				const QPointF b = projectVec(m_projection, makeVec(end->x, end->y, height));
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
				keep(kind, entity.id, pointRect(projectVec(m_projection, entity.origin)));
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
						projectedBoundsRect(m_projection, m_brushGeometry.at(index).mins,
							m_brushGeometry.at(index).maxs));
					continue;
				}
				if (!brush.boundsSolved) {
					continue;
				}
				keep(kind, brush.id, projectedBoundsRect(m_projection, brush.mins, brush.maxs));
			}
			break;
		case LevelMapSelectionKind::QuakePatch:
			for (const LevelMapPatch& patch : m_document.patches) {
				if (!patch.mins.valid || !patch.maxs.valid) {
					continue;
				}
				keep(kind, patch.id, projectedBoundsRect(m_projection, patch.mins, patch.maxs));
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

void MapViewport::paintSelectionMarkers(QPainter& painter, const Palette& palette) const
{
	if (m_selection.isEmpty()) {
		return;
	}
	painter.setBrush(Qt::NoBrush);
	// Every selected object is ringed; only the primary gets the crosshair and
	// the label, so the two roles stay distinguishable without colour.
	QPen memberPen(palette.selection, m_highContrast ? 2.0 : 1.4, Qt::SolidLine);
	QPointF world;
	int drawn = 0;
	for (const LevelMapSelectionRef& ref : m_selection) {
		if (drawn >= kMaxSelectionMarkers) {
			break;
		}
		if (ref.kind == m_selectionKind && ref.objectId == m_selectedObjectId) {
			continue;
		}
		if (!objectWorldPoint(m_document, m_sectorOutlines, m_brushGeometry, m_projection, ref.kind,
			    ref.objectId, &world)) {
			continue;
		}
		const QPointF view = worldToView(world.x(), world.y());
		painter.setPen(memberPen);
		painter.drawEllipse(view, 8.0, 8.0);
		++drawn;
	}

	if (m_selectionKind == LevelMapSelectionKind::None
		|| !objectWorldPoint(m_document, m_sectorOutlines, m_brushGeometry, m_projection, m_selectionKind,
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
	painter.setPen(QPen(palette.text, 1.0));
	painter.drawText(QPointF(view.x() + 14.0, view.y() - 12.0),
		objectLabelText(m_document, m_selectionKind, m_selectedObjectId));
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
		if (!objectWorldBounds(m_document, m_sectorOutlines, m_brushGeometry, m_projection, ref.kind,
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
	double step = m_gridSize > 0 ? static_cast<double>(m_gridSize) : 64.0;
	// Adaptive grid: step up in powers of two until lines are legible, so a
	// zoomed-out map never fills with noise.
	for (int guard = 0; guard < 40 && step * m_zoom < kMinGridSpacingPixels; ++guard) {
		step *= 2.0;
	}
	if (step * m_zoom < 1.0) {
		return;
	}

	const QPointF topLeft = viewToWorld(QPointF(0.0, 0.0));
	const QPointF bottomRight = viewToWorld(QPointF(width(), height()));
	const double left = std::min(topLeft.x(), bottomRight.x());
	const double right = std::max(topLeft.x(), bottomRight.x());
	const double bottom = std::min(topLeft.y(), bottomRight.y());
	const double top = std::max(topLeft.y(), bottomRight.y());

	const double firstX = std::floor(left / step) * step;
	const double firstY = std::floor(bottom / step) * step;
	const int columns = std::min(static_cast<int>((right - firstX) / step) + 2, kMaxGridLines);
	const int rows = std::min(static_cast<int>((top - firstY) / step) + 2, kMaxGridLines);

	const QPen minorPen(palette.gridMinor, 1.0);
	const QPen majorPen(palette.gridMajor, 1.0);
	const QPen axisPen(palette.axis, 2.0);

	painter.setBrush(Qt::NoBrush);
	for (int pass = 0; pass < 2; ++pass) {
		painter.setPen(pass == 0 ? minorPen : majorPen);
		for (int index = 0; index < columns; ++index) {
			const double x = firstX + index * step;
			const long long line = std::llround(x / step);
			if (line == 0) {
				continue;
			}
			const bool major = (line % kMajorGridInterval) == 0;
			if (major != (pass == 1)) {
				continue;
			}
			const double viewX = worldToView(x, 0.0).x();
			painter.drawLine(QPointF(viewX, 0.0), QPointF(viewX, height()));
		}
		for (int index = 0; index < rows; ++index) {
			const double y = firstY + index * step;
			const long long line = std::llround(y / step);
			if (line == 0) {
				continue;
			}
			const bool major = (line % kMajorGridInterval) == 0;
			if (major != (pass == 1)) {
				continue;
			}
			const double viewY = worldToView(0.0, y).y();
			painter.drawLine(QPointF(0.0, viewY), QPointF(width(), viewY));
		}
	}

	// World axes are emphasised so the origin is always identifiable.
	painter.setPen(axisPen);
	if (left <= 0.0 && right >= 0.0) {
		const double viewX = worldToView(0.0, 0.0).x();
		painter.drawLine(QPointF(viewX, 0.0), QPointF(viewX, height()));
	}
	if (bottom <= 0.0 && top >= 0.0) {
		const double viewY = worldToView(0.0, 0.0).y();
		painter.drawLine(QPointF(0.0, viewY), QPointF(width(), viewY));
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
		const LevelMapDoomVertex* start = findById(m_document.doomVertices, linedef.startVertex);
		const LevelMapDoomVertex* end = findById(m_document.doomVertices, linedef.endVertex);
		if (start == nullptr || end == nullptr) {
			continue;
		}
		const double height = m_projection == MapViewportProjection::TopXY
			? 0.0
			: doomLinedefHeight(m_document, linedef);
		const QPointF a = projectVec(m_projection, makeVec(start->x, start->y, height));
		const QPointF b = projectVec(m_projection, makeVec(end->x, end->y, height));
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
			const QPointF point = projectVec(m_projection, makeVec(vertex.x, vertex.y, 0.0));
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
			const QPointF point = projectVec(m_projection, makeVec(thing.x, thing.y, 0.0));
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

void MapViewport::paintQuake(QPainter& painter, const Palette& palette) const
{
	const QPointF topLeft = viewToWorld(QPointF(0.0, 0.0));
	const QPointF bottomRight = viewToWorld(QPointF(width(), height()));
	const QRectF visible = minMaxRect(std::min(topLeft.x(), bottomRight.x()), std::min(topLeft.y(), bottomRight.y()),
		std::max(topLeft.x(), bottomRight.x()), std::max(topLeft.y(), bottomRight.y()));
	const int worldspawnId = worldspawnEntityId(m_document);

	// Three distinct encodings: world geometry thin and solid, brush entities
	// heavy and solid, unsolved brushes dashed with a cross.
	const QPen worldPen(palette.brush, m_highContrast ? 1.6 : 1.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
	const QPen entityBrushPen(palette.brushEntity, m_highContrast ? 2.8 : 2.2, Qt::SolidLine, Qt::RoundCap,
		Qt::RoundJoin);
	QPen degeneratePen(palette.warning, 1.6, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin);
	degeneratePen.setDashPattern({4.0, 3.0});

	QPolygonF viewPolygon;
	painter.setBrush(Qt::NoBrush);
	for (const MapBrushGeometry& brush : m_brushGeometry) {
		const bool worldBrush = brush.entityId < 0 || brush.entityId == worldspawnId;
		if (!brush.solved) {
			// Unsolved brushes are never dropped silently: fall back to the
			// parser bounds so the user can see where the problem is.
			const LevelMapBrush* source = findById(m_document.brushes, brush.brushId);
			if (source == nullptr || !source->boundsSolved) {
				continue;
			}
			const QRectF bounds = projectedBoundsRect(m_projection, source->mins, source->maxs);
			if (!rectsOverlap(bounds, visible)) {
				continue;
			}
			const QPointF a = worldToView(bounds.left(), bounds.top());
			const QPointF b = worldToView(bounds.right(), bounds.bottom());
			const QRectF viewRect = QRectF(a, b).normalized();
			painter.setPen(degeneratePen);
			painter.drawRect(viewRect);
			painter.drawLine(viewRect.topLeft(), viewRect.bottomRight());
			painter.drawLine(viewRect.topRight(), viewRect.bottomLeft());
			continue;
		}

		const QRectF bounds = projectedBoundsRect(m_projection, brush.mins, brush.maxs);
		if (!rectsOverlap(bounds, visible)) {
			continue;
		}
		painter.setPen(worldBrush ? worldPen : entityBrushPen);
		if (m_projection == MapViewportProjection::TopXY) {
			const QVector<QPolygonF> footprints = brush.footprintPolygons();
			for (const QPolygonF& polygon : footprints) {
				if (polygon.size() < 2) {
					continue;
				}
				viewPolygon.clear();
				for (const QPointF& point : polygon) {
					viewPolygon.append(worldToView(point.x(), point.y()));
				}
				painter.drawPolygon(viewPolygon);
			}
		} else {
			for (const MapFacePolygon& face : brush.faces) {
				if (face.points.size() < 3) {
					continue;
				}
				viewPolygon.clear();
				for (const LevelMapVec3& point : face.points) {
					const QPointF projected = projectVec(m_projection, point);
					viewPolygon.append(worldToView(projected.x(), projected.y()));
				}
				painter.drawPolygon(viewPolygon);
			}
		}
	}

	// Patch outlines: the tessellated grid border, which is enough to show the
	// curve without drowning the view in mesh lines.
	painter.setPen(QPen(palette.patch, 1.4, Qt::DashDotLine, Qt::RoundCap, Qt::RoundJoin));
	for (const LevelMapPatch& patch : m_document.patches) {
		if (patch.width < 2 || patch.height < 2) {
			continue;
		}
		if (patch.mins.valid && patch.maxs.valid
			&& !rectsOverlap(projectedBoundsRect(m_projection, patch.mins, patch.maxs), visible)) {
			continue;
		}
		const QVector<QVector<LevelMapVec3>> mesh = tessellatePatchMesh(patch, kPatchSubdivisions);
		if (mesh.isEmpty() || mesh.first().isEmpty()) {
			continue;
		}
		const int rows = static_cast<int>(mesh.size());
		const int columns = static_cast<int>(mesh.first().size());
		viewPolygon.clear();
		const auto appendPoint = [&](const LevelMapVec3& point) {
			const QPointF projected = projectVec(m_projection, point);
			viewPolygon.append(worldToView(projected.x(), projected.y()));
		};
		for (int column = 0; column < columns; ++column) {
			appendPoint(mesh.first().at(column));
		}
		for (int row = 1; row < rows; ++row) {
			if (mesh.at(row).size() == columns) {
				appendPoint(mesh.at(row).at(columns - 1));
			}
		}
		for (int column = columns - 2; column >= 0; --column) {
			if (mesh.last().size() == columns) {
				appendPoint(mesh.last().at(column));
			}
		}
		for (int row = rows - 2; row >= 1; --row) {
			if (!mesh.at(row).isEmpty()) {
				appendPoint(mesh.at(row).first());
			}
		}
		if (viewPolygon.size() >= 3) {
			painter.drawPolygon(viewPolygon);
		}
	}

	// Point entities: diamond markers so they cannot be mistaken for brushes.
	const QPen entityPen(palette.entity, 1.5);
	painter.setPen(entityPen);
	int labelsDrawn = 0;
	QPolygonF diamond;
	for (const LevelMapEntity& entity : m_document.entities) {
		if (!entity.origin.valid || entity.id == worldspawnId) {
			continue;
		}
		const QPointF point = projectVec(m_projection, entity.origin);
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
	if (m_hasDocument && !m_dragging && !m_banding && m_hover.kind != LevelMapSelectionKind::None) {
		const QPointF view = worldToView(m_hover.worldX, m_hover.worldY);
		painter.setPen(QPen(palette.hover, 1.0, Qt::DotLine));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(QRectF(view.x() - 7.0, view.y() - 7.0, 14.0, 14.0));
	}

	if (m_hasDocument) {
		paintSelectionMarkers(painter, palette);
		if (m_dragging) {
			paintDragPreview(painter, palette);
		}
	}
	if (m_banding) {
		paintRubberBand(painter, palette);
	}
	if (m_hasDocument) {
		paintHud(painter, palette);
	}

	if (hasFocus()) {
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(palette.focus, 2.0, Qt::SolidLine));
		painter.drawRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5));
		painter.setPen(QPen(palette.background, 1.0, Qt::DashLine));
		painter.drawRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5));
	}
}

void MapViewport::paintHud(QPainter& painter, const Palette& palette) const
{
	const QString separator = QStringLiteral("  %1  ").arg(QChar(0x00b7));
	const QString left = QStringList {
		mapViewportProjectionDisplayName(m_projection),
		tr("Grid %1").arg(m_gridSize),
		m_snapToGrid ? tr("Snap on") : tr("Snap off"),
	}.join(separator);
	const QString right = documentIsDoom(m_document)
		? QStringList {tr("%n thing(s)", nullptr, static_cast<int>(m_document.doomThings.size())),
			  tr("%n linedef(s)", nullptr, static_cast<int>(m_document.doomLinedefs.size())),
			  tr("%n sector(s)", nullptr, static_cast<int>(m_document.doomSectors.size()))}.join(separator)
		: QStringList {tr("%n entit(y)(ies)", nullptr, static_cast<int>(m_document.entities.size())),
			  tr("%n brush(es)", nullptr, static_cast<int>(m_document.brushes.size()))}.join(separator);

	painter.save();
	QFont font = painter.font();
	font.setPointSizeF(std::max(7.0, font.pointSizeF() * 0.9));
	painter.setFont(font);
	const QFontMetricsF metrics(font);
	const qreal margin = 8.0;
	const qreal padX = 7.0;
	const qreal padY = 3.0;
	QColor backdrop = palette.background;
	backdrop.setAlpha(m_highContrast ? 255 : 210);
	auto drawTag = [&](const QString& text, bool alignRight) {
		const qreal width = metrics.horizontalAdvance(text) + padX * 2.0;
		const qreal height = metrics.height() + padY * 2.0;
		const qreal x = alignRight ? rect().right() - margin - width : rect().left() + margin;
		const QRectF tag(x, rect().top() + margin, width, height);
		painter.setPen(m_highContrast ? QPen(palette.text, 1.0) : Qt::NoPen);
		painter.setBrush(backdrop);
		painter.drawRoundedRect(tag, 3.0, 3.0);
		painter.setPen(palette.subtleText);
		painter.drawText(tag.adjusted(padX, 0.0, -padX, 0.0), Qt::AlignVCenter | Qt::AlignLeft, text);
	};
	drawTag(left, false);
	if (width() > metrics.horizontalAdvance(left) + metrics.horizontalAdvance(right) + 60.0) {
		drawTag(right, true);
	}
	painter.restore();
}

void MapViewport::announceSelection()
{
	setAccessibleDescription(accessibleSummary());
	update();
	// The set goes out first so a receiver that rebuilds the document's
	// selection from it has already done so by the time the primary arrives.
	Q_EMIT selectionSetChanged(m_selection);
	Q_EMIT selectionChanged(static_cast<int>(m_selectionKind), m_selectedObjectId);
}

void MapViewport::paintEvent(QPaintEvent*)
{
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
	}

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.fillRect(rect(), palette.background);

	if (!m_hasDocument) {
		paintEmptyState(painter, palette);
		paintOverlay(painter, palette);
		return;
	}

	if (m_showGrid) {
		paintGrid(painter, palette);
	}
	if (documentIsDoom(m_document)) {
		paintDoom(painter, palette);
	} else {
		paintQuake(painter, palette);
	}
	paintOverlay(painter, palette);
}

void MapViewport::mousePressEvent(QMouseEvent* event)
{
	setFocus(Qt::MouseFocusReason);
	// Space toggles a hand cursor that turns the left button into a pan handle;
	// the middle button always pans.
	const bool panRequested = event->button() == Qt::MiddleButton
		|| (event->button() == Qt::LeftButton && cursor().shape() == Qt::OpenHandCursor);
	if (panRequested) {
		m_panning = true;
		m_panAnchorView = event->position();
		m_panAnchorCenter = m_worldCenter;
		setCursor(Qt::ClosedHandCursor);
		event->accept();
		return;
	}
	if (event->button() != Qt::LeftButton) {
		QWidget::mousePressEvent(event);
		return;
	}

	const bool extend = (event->modifiers() & Qt::ShiftModifier) != 0;
	const bool toggle = (event->modifiers() & Qt::ControlModifier) != 0;
	const MapViewportHit hit = hitTest(event->position());
	m_pressViewPoint = event->position();

	if (hit.kind == LevelMapSelectionKind::None) {
		// Empty space starts a rubber band. The selection is only replaced on
		// release, so a plain click that selects nothing still clears it.
		m_banding = true;
		m_bandAnchorView = event->position();
		m_bandCurrentView = event->position();
		m_bandModifiers = event->modifiers();
		update();
		event->accept();
		return;
	}

	const bool alreadySelected = selectionContains(hit.kind, hit.objectId);
	if (toggle) {
		toggleInSelection(hit.kind, hit.objectId);
		announceSelection();
	} else if (extend) {
		addToSelection(hit.kind, hit.objectId);
		announceSelection();
	} else if (!alreadySelected) {
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
	if (m_panning) {
		const QPointF delta = event->position() - m_panAnchorView;
		if (m_zoom > 0.0) {
			m_worldCenter = QPointF(m_panAnchorCenter.x() - delta.x() / m_zoom,
				m_panAnchorCenter.y() + delta.y() / m_zoom);
		}
		update();
		Q_EMIT viewChanged();
		event->accept();
		return;
	}

	if (m_banding) {
		m_bandCurrentView = event->position();
		Q_EMIT hoverChanged(hoverSummary());
		update();
		event->accept();
		return;
	}

	if (m_pressArmed) {
		const QPointF travel = event->position() - m_pressViewPoint;
		if (std::hypot(travel.x(), travel.y()) >= kDragThresholdPixels) {
			beginDrag(event->position());
		}
		event->accept();
		return;
	}

	if (m_dragging) {
		updateDrag(event->position());
		event->accept();
		return;
	}

	const MapViewportHit hit = hitTest(event->position());
	const bool sameObject = hit.kind == m_hover.kind && hit.objectId == m_hover.objectId;
	m_hover = hit;
	Q_EMIT hoverChanged(hoverSummary());
	if (!sameObject) {
		update();
	}
	event->accept();
}

void MapViewport::mouseReleaseEvent(QMouseEvent* event)
{
	if (m_panning && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
		m_panning = false;
		setCursor(Qt::OpenHandCursor);
		event->accept();
		return;
	}
	if (event->button() != Qt::LeftButton) {
		QWidget::mouseReleaseEvent(event);
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
		const QVector<LevelMapSelectionRef> inside = objectsInWorldRect(bandWorldRect());
		m_banding = false;
		if ((m_bandModifiers & Qt::ControlModifier) != 0) {
			for (const LevelMapSelectionRef& ref : inside) {
				toggleInSelection(ref.kind, ref.objectId);
			}
		} else if ((m_bandModifiers & Qt::ShiftModifier) != 0) {
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
	const double panStep = m_zoom > 0.0 ? 48.0 / m_zoom : 48.0;
	switch (event->key()) {
	case Qt::Key_Left:
	case Qt::Key_Right:
	case Qt::Key_Up:
	case Qt::Key_Down: {
		const double sign = (event->key() == Qt::Key_Left || event->key() == Qt::Key_Down) ? -1.0 : 1.0;
		const bool horizontal = event->key() == Qt::Key_Left || event->key() == Qt::Key_Right;
		// Arrows nudge the selection, which is the whole point of having one.
		// Ctrl always pans, and so do bare arrows when nothing is selected, so
		// the old keyboard panning is still reachable.
		const bool pan = m_selection.isEmpty() || (event->modifiers() & Qt::ControlModifier) != 0;
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
		if (m_dragging || m_banding) {
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
	case Qt::Key_Space:
		// Toggle the pan handle. The cursor shape is the visible state.
		if (cursor().shape() == Qt::OpenHandCursor) {
			unsetCursor();
		} else {
			setCursor(Qt::OpenHandCursor);
		}
		event->accept();
		return;
	case Qt::Key_Escape:
		m_panning = false;
		unsetCursor();
		if (m_dragging || m_banding || m_pressArmed) {
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
	offset = 0;
	for (const LevelMapSelectionKind kind : tiers) {
		const int count = objectCountForKind(m_document, kind);
		if (next < offset + count) {
			const int id = objectIdAtIndex(m_document, kind, next - offset);
			if (extend) {
				addToSelection(kind, id);
			} else {
				replaceSelection(kind, id);
			}
			break;
		}
		offset += count;
	}

	// Keep the newly selected object on screen; the jump is instantaneous, so
	// reduced motion needs no special case here.
	QPointF world;
	if (objectWorldPoint(m_document, m_sectorOutlines, m_brushGeometry, m_projection, m_selectionKind,
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
	if (m_dragging || m_banding) {
		// Qt keeps delivering moves to the grabbing widget, so a gesture that
		// wanders outside the viewport is still live: leave it alone.
		QWidget::leaveEvent(event);
		return;
	}
	if (m_hover.kind != LevelMapSelectionKind::None) {
		update();
	}
	m_hover = MapViewportHit();
	Q_EMIT hoverChanged(m_hasDocument ? tr("Pointer left the map viewport.") : tr("No map loaded."));
	QWidget::leaveEvent(event);
}

QString mapViewportProjectionDisplayName(MapViewportProjection projection)
{
	switch (projection) {
	case MapViewportProjection::TopXY:
		return viewText("Top (X/Y)");
	case MapViewportProjection::FrontXZ:
		return viewText("Front (X/Z)");
	case MapViewportProjection::SideZY:
		return viewText("Side (Y/Z)");
	}
	return viewText("Top (X/Y)");
}

} // namespace vibestudio
