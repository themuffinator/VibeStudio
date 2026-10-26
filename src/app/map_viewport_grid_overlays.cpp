// The 2D views' grid overlays: coordinate labels on the grid lines (Show >
// Grid Coordinates) and the compiler block grid (Show > Block Grid). The
// block lines are drawn into the grid image, so the overlay worker renders
// them with the grid on heavy maps; the labels are text, painted over the
// geometry here, clear of the status tags. Neither changes the map.
//
// Workflow reference: NRC XYWnd::XY_DrawGrid (coordinate text),
// XYWnd::XY_DrawBlockGrid and the ShowCoordinates and ShowBlocks toggles in
// radiant/xywindow.cpp at 449778b3 (GPL-2.0-or-later). Original Qt
// implementation; see docs/CREDITS.md.

#include "app/map_viewport.h"
#include "app/map_viewport_p.h"
#include "app/viewport_hud.h"
#include "app/viewport_image.h"

#include <QFontMetricsF>
#include <QPainter>

#include <algorithm>
#include <utility>

namespace vibestudio {

namespace {

// The world axes a projection draws across and up: x/y, x/z or y/z.
std::pair<int, int> planeAxes(MapViewportProjection projection)
{
	switch (projection) {
	case MapViewportProjection::TopXY:
		return {0, 1};
	case MapViewportProjection::FrontXZ:
		return {0, 2};
	case MapViewportProjection::SideZY:
		return {1, 2};
	}
	return {0, 1};
}

} // namespace

void MapViewport::setShowGridCoordinates(bool show)
{
	if (m_showGridCoordinates == show) {
		return;
	}
	m_showGridCoordinates = show;
	setAccessibleDescription(accessibleSummary());
	update();
}

bool MapViewport::showGridCoordinates() const
{
	return m_showGridCoordinates;
}

void MapViewport::setShowBlockGrid(bool show)
{
	if (m_showBlockGrid == show) {
		return;
	}
	m_showBlockGrid = show;
	setAccessibleDescription(accessibleSummary());
	update();
}

bool MapViewport::showBlockGrid() const
{
	return m_showBlockGrid;
}

LevelMapBlockSize MapViewport::blockGridSize() const
{
	if (!m_hasDocument) {
		return {};
	}
	// Scene revisions are unique across views, so a pane that adopts another
	// pane's scene reads the key again too.
	if (m_blockSizeRevision != m_sceneRevision) {
		m_blockSize = levelMapBlockSize(m_sourceDocument);
		m_blockSizeRevision = m_sceneRevision;
	}
	return m_blockSize;
}

bool MapViewport::drawsBlockGrid() const
{
	if (!m_showBlockGrid || !m_hasDocument || !levelMapHasBlockGrid(m_sourceDocument)) {
		return false;
	}
	const auto [across, up] = planeAxes(m_projection);
	const LevelMapBlockSize blocks = blockGridSize();
	return blocks.along(across) > 0 || blocks.along(up) > 0;
}

MapGridView MapViewport::planGridView() const
{
	MapGridView view;
	view.viewport = size();
	view.center = m_worldCenter;
	view.zoom = m_zoom;
	view.units = m_gridSize;
	return view;
}

MapGridView MapViewport::gridView(const QTransform& deviceTransform, const Palette& palette) const
{
	const auto device = viewportImageDevice(deviceTransform);
	MapGridView view = planGridView();
	view.pixelRatio = device.pixelRatio;
	view.pixelPhase = device.pixelPhase;
	view.minor = palette.gridMinor.rgba();
	view.major = palette.gridMajor.rgba();
	view.axis = palette.axis.rgba();
	view.lines = m_showGrid;
	if (drawsBlockGrid()) {
		const auto [across, up] = planeAxes(m_projection);
		const LevelMapBlockSize blocks = blockGridSize();
		view.blockX = blocks.along(across);
		view.blockY = blocks.along(up);
		view.block = palette.block.rgba();
		view.blockWidth = m_highContrast ? 3.0 : 2.0;
		if (m_hasRegion) {
			// NetRadiant Custom keeps the block grid inside the region.
			view.clipBlocks = true;
			const QPointF a = projectPoint({m_regionBox[0], m_regionBox[1], m_regionBox[2], true});
			const QPointF b = projectPoint({m_regionBox[3], m_regionBox[4], m_regionBox[5], true});
			view.blockBounds = QRectF(QPointF(std::min(a.x(), b.x()), std::min(a.y(), b.y())), QPointF(std::max(a.x(), b.x()), std::max(a.y(), b.y())));
		}
	}
	return view;
}

MapGridCoordinateLayout MapViewport::gridCoordinateLabels() const
{
	if (!m_showGridCoordinates || !m_hasDocument) {
		return {};
	}
	const QFontMetricsF metrics(viewportHudFont(font()), static_cast<const QPaintDevice*>(this));
	const QStringList tags = hudTags();
	const ViewportHudLayout hud = layoutViewportHud(rect(), metrics, tags.at(0).split(QStringLiteral("  %1  ").arg(QChar(0x00b7))),
		{tags.at(1)}, layoutDirection());
	return mapGridCoordinateLabels(planGridView(), metrics, layoutDirection(), {hud.leading.bounds, hud.trailing.bounds});
}

void MapViewport::paintGridCoordinates(QPainter& painter, const Palette& palette, const ViewportHudLayout& hud) const
{
	if (!m_showGridCoordinates || !m_hasDocument) {
		return;
	}
	const QFont font = viewportHudFont(this->font());
	const QFontMetricsF metrics(font, painter.device());
	const MapGridCoordinateLayout layout = mapGridCoordinateLabels(planGridView(), metrics, layoutDirection(),
		{hud.leading.bounds, hud.trailing.bounds});
	if (layout.labels.isEmpty()) {
		return;
	}
	painter.save();
	painter.setFont(font);
	QColor backdrop = palette.background;
	backdrop.setAlpha(m_highContrast ? 255 : 190);
	const QColor text = m_highContrast ? palette.text : palette.subtleText;
	// Numbers read left to right in every layout direction, so "-128" never
	// turns into "128-".
	const int flags = Qt::AlignCenter | Qt::AlignAbsolute | Qt::TextSingleLine | Qt::TextForceLeftToRight;
	for (const MapGridCoordinateLabel& label : layout.labels) {
		painter.setPen(m_highContrast ? QPen(text, 1.0) : QPen(Qt::NoPen));
		painter.setBrush(backdrop);
		painter.drawRoundedRect(label.bounds, 2.0, 2.0);
		painter.setPen(text);
		painter.drawText(label.bounds, flags, label.text);
	}
	painter.restore();
}

QString MapViewport::blockGridTag() const
{
	if (!drawsBlockGrid()) {
		return {};
	}
	const auto [across, up] = planeAxes(m_projection);
	const LevelMapBlockSize blocks = blockGridSize();
	const auto part = [](int size) { return size > 0 ? QString::number(size) : tr("off"); };
	if (blocks.along(across) == blocks.along(up)) {
		return tr("Blocks %1").arg(blocks.along(across));
	}
	return tr("Blocks %1 %2 %3").arg(part(blocks.along(across)), QString(QChar(0x00d7)), part(blocks.along(up)));
}

QString MapViewport::blockGridDescription() const
{
	if (!drawsBlockGrid()) {
		return {};
	}
	const auto [acrossAxis, upAxis] = planeAxes(m_projection);
	const LevelMapBlockSize blocks = blockGridSize();
	const int across = blocks.along(acrossAxis);
	const int up = blocks.along(upAxis);
	if (across > 0 && up > 0) {
		return tr("Dashed block grid lines mark where the compiler splits the world: every %1 units across this view and every %2 units up it.")
			.arg(across)
			.arg(up);
	}
	if (across > 0) {
		return tr("Dashed block grid lines mark where the compiler splits the world: every %1 units across this view.").arg(across);
	}
	return tr("Dashed block grid lines mark where the compiler splits the world: every %1 units up this view.").arg(up);
}

QString MapViewport::gridCoordinatesDescription() const
{
	if (!m_showGridCoordinates || !m_hasDocument) {
		return {};
	}
	const MapGridCoordinateLayout layout = gridCoordinateLabels();
	QStringList across;
	QStringList up;
	for (const MapGridCoordinateLabel& label : layout.labels) {
		(label.across ? across : up) << label.text;
	}
	if (!across.isEmpty() && !up.isEmpty()) {
		return tr("Grid coordinates label the lines across from %1 to %2 and up from %3 to %4.")
			.arg(across.first(), across.last(), up.first(), up.last());
	}
	if (!across.isEmpty()) {
		return tr("Grid coordinates label the lines across from %1 to %2.").arg(across.first(), across.last());
	}
	if (!up.isEmpty()) {
		return tr("Grid coordinates label the lines up from %1 to %2.").arg(up.first(), up.last());
	}
	return tr("Grid coordinates are on, but this view is too small to label its lines.");
}

} // namespace vibestudio
