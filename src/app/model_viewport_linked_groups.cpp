#include "app/model_viewport.h"
#include "app/model_viewport_p.h"
#include "app/viewport_hud.h"

#include <QFontMetricsF>
#include <QLineF>
#include <QPainter>
#include <QPen>

#include <algorithm>
#include <array>
#include <cmath>

// Linked groups through the level camera. TrenchBroom draws each linked
// group's bounds and, while one is selected, arrows to the groups it updates,
// in its 3D view as in its 2D ones. This draws what the Levels plan views draw
// (MapViewport::paintLinkedGroups), with the same rules from
// app/level_linked_group_boxes.h, through this view's camera: box edges are
// clipped to the near plane, names keep clear of the heads-up tags and of each
// other, and an arrow needs both of its groups' centres in front of the camera.
namespace vibestudio {

namespace {

bool finite(const QPointF& point)
{
	return std::isfinite(point.x()) && std::isfinite(point.y());
}

// How far a line from a rectangle's centre runs before leaving it.
double exitDistance(const QRectF& rect, const QPointF& direction)
{
	const double x = std::abs(direction.x()) > 1e-9 ? rect.width() / 2.0 / std::abs(direction.x()) : 1e9;
	const double y = std::abs(direction.y()) > 1e-9 ? rect.height() / 2.0 / std::abs(direction.y()) : 1e9;
	return std::min(x, y);
}

} // namespace

void ModelViewport::setLinkedGroups(const LevelLinkedGroupDisplay& display)
{
	if (display == m_linkedGroups) {
		return;
	}
	const bool drew = m_linkedGroups.drawsAnything();
	m_linkedGroups = display;
	// Boxes kept for later, with nothing drawn before or after, change nothing
	// on screen or in the description.
	if (!drew && !m_linkedGroups.drawsAnything()) {
		return;
	}
	setAccessibleDescription(accessibleSummary());
	update();
}

void ModelViewport::paintLinkedGroups(QPainter& painter, const Palette& palette)
{
	m_linkedGroupPaint = {};
	if (!m_linkedGroups.drawsAnything()) {
		return;
	}
	const LevelLinkedGroupDisplay& display = m_linkedGroups;
	const QSet<QString> links = levelLinkedSetsInUse(display.boxes, display.inUse);
	// The plan views' colours: their link colour, and their warning colour
	// for a group out of sync.
	const QColor linkColour = m_highContrast ? QColor(0x6a, 0xb7, 0xff) : QColor(214, 140, 224);
	const QColor warningColour = m_highContrast ? QColor(0xff, 0xd8, 0x00) : QColor(240, 140, 80);
	// A closed group dragged with the selection takes its box and arrows with
	// it; an open one stays, since only part of it moves.
	const bool dragging = isMovingSelection();
	struct Projected {
		const LevelLinkedGroupBox* box = nullptr;
		// The screen bounds of the corners in front of the camera.
		QRectF bounds;
		QPointF centre;
		bool whole = false;
		bool shown = false;
	};
	QVector<Projected> projected;
	projected.reserve(display.boxes.size());
	const QRectF view(rect());
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setBrush(Qt::NoBrush);
	for (const LevelLinkedGroupBox& box : display.boxes) {
		const bool shown = links.contains(box.link);
		if (!shown && !display.showAll) {
			continue;
		}
		const bool own = display.inUse.contains(box.id);
		ModelVec3 offset;
		if (dragging && own && !display.open.contains(box.id)) {
			offset = m_moveOffset;
		}
		const std::array<double, 3> low {box.mins.x + offset.x, box.mins.y + offset.y, box.mins.z + offset.z};
		const std::array<double, 3> high {box.maxs.x + offset.x, box.maxs.y + offset.y, box.maxs.z + offset.z};
		std::array<ModelVec3, 8> corners;
		Projected item;
		item.box = &box;
		item.shown = shown;
		// QRectF::united() skips empty rectangles, so the corners are gathered by hand.
		int front = 0;
		QPointF least;
		QPointF most;
		for (int corner = 0; corner < 8; ++corner) {
			corners[corner] = {static_cast<float>(corner & 1 ? high[0] : low[0]), static_cast<float>(corner & 2 ? high[1] : low[1]),
				static_cast<float>(corner & 4 ? high[2] : low[2])};
			const QPointF point = projectPoint(m_camera, corners[corner], nullptr);
			if (!finite(point)) {
				continue;
			}
			least = front == 0 ? point : QPointF(std::min(least.x(), point.x()), std::min(least.y(), point.y()));
			most = front == 0 ? point : QPointF(std::max(most.x(), point.x()), std::max(most.y(), point.y()));
			++front;
		}
		item.bounds = front > 0 ? QRectF(least, most) : QRectF();
		item.whole = front == 8;
		item.centre = projectPoint(m_camera,
			{static_cast<float>((low[0] + high[0]) / 2.0), static_cast<float>((low[1] + high[1]) / 2.0), static_cast<float>((low[2] + high[2]) / 2.0)},
			nullptr);
		projected.push_back(item);
		QVector<QLineF> edges;
		for (int corner = 0; corner < 8; ++corner) {
			for (int axis = 0; axis < 3; ++axis) {
				if (corner & (1 << axis)) {
					continue;
				}
				QPointF a;
				QPointF b;
				if (projectSegment(corners[corner], corners[corner | (1 << axis)], &a, &b) && finite(a) && finite(b)
					&& QRectF(a, b).normalized().adjusted(-2, -2, 2, 2).intersects(view)) {
					edges.push_back(QLineF(a, b));
				}
			}
		}
		if (edges.isEmpty()) {
			continue;
		}
		QColor colour = box.outOfSync ? warningColour : linkColour;
		QColor halo = palette.background;
		halo.setAlpha(m_highContrast ? 255 : (shown ? 150 : 70));
		if (!shown && !m_highContrast) {
			colour.setAlpha(110);
		}
		const double width = own ? (m_highContrast ? 3.0 : 2.0) : (m_highContrast ? 1.8 : 1.2);
		// A dark band under each edge keeps it legible over bright surfaces;
		// the dash pattern on top still tells the groups apart.
		painter.setPen(QPen(halo, width + 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		painter.drawLines(edges);
		painter.setPen(QPen(colour, width, levelLinkedGroupPenStyle(box, own), Qt::FlatCap, Qt::RoundJoin));
		painter.drawLines(edges);
		++m_linkedGroupPaint.boxes;
	}

	// Arrows from each group in use to the others of its set, edge to edge on
	// screen, or centre to centre when the boxes overlap there.
	const auto anchorOf = [](const Projected& item) { return item.whole ? item.bounds.center() : item.centre; };
	for (const Projected& from : std::as_const(projected)) {
		if (!display.inUse.contains(from.box->id) || !finite(from.centre)) {
			continue;
		}
		for (const Projected& to : std::as_const(projected)) {
			if (to.box == from.box || to.box->link != from.box->link || !finite(to.centre)) {
				continue;
			}
			const QPointF start = anchorOf(from);
			const QPointF end = anchorOf(to);
			const QPointF span = end - start;
			const double length = std::hypot(span.x(), span.y());
			if (length < 24.0 || !QRectF(start, end).normalized().adjusted(-2, -2, 2, 2).intersects(view)) {
				continue;
			}
			const QPointF direction = span / length;
			const double leave = from.whole ? exitDistance(from.bounds, direction) : 0.0;
			const double enter = to.whole ? exitDistance(to.bounds, direction) : 0.0;
			const bool apart = leave + enter + 16.0 < length;
			const QPointF tail = start + direction * (apart ? leave : 0.0);
			const QPointF tip = end - direction * (apart ? enter : 8.0);
			const QColor colour = to.box->outOfSync ? warningColour : linkColour;
			const double width = m_highContrast ? 2.6 : 1.8;
			const QPointF normal(-direction.y(), direction.x());
			const QPolygonF head {tip, tip - direction * 10.0 + normal * 5.0, tip - direction * 10.0 - normal * 5.0};
			QColor halo = palette.background;
			halo.setAlpha(m_highContrast ? 255 : 150);
			painter.setPen(QPen(halo, width + 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.setBrush(Qt::NoBrush);
			painter.drawLine(tail, tip);
			painter.setBrush(halo);
			painter.drawPolygon(head);
			painter.setPen(QPen(colour, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.drawLine(tail, tip);
			painter.setPen(Qt::NoPen);
			painter.setBrush(colour);
			painter.drawPolygon(head);
			++m_linkedGroupPaint.arrows;
		}
	}

	// Names of the sets in use, where one fits: above the box, else below it,
	// inside the view and clear of the heads-up tags, the selection's resize
	// handles and their axis labels, and the other names. A box too small or
	// too far away to read a name against gets none.
	const QFont font = viewportHudFont(this->font());
	painter.setFont(font);
	const QFontMetricsF metrics(font, painter.device());
	const ViewportHudLayout hud = hudLayoutFor(painter.device());
	const QRectF area = view.adjusted(4, 4, -4, -4);
	const bool rtl = layoutDirection() == Qt::RightToLeft;
	QVector<QRectF> taken {hud.leading.bounds, hud.trailing.bounds};
	if (m_selectionResizeEnabled) {
		for (const QPointF& handle : selectionResizeHandles()) {
			if (finite(handle)) {
				taken.push_back(QRectF(handle - QPointF(8, 8), QSizeF(16, 16)));
			}
		}
		for (const QRectF& axis : selectionResizeLabels()) {
			taken.push_back(axis.adjusted(-2, -2, 2, 2));
		}
	}
	for (const Projected& item : std::as_const(projected)) {
		if (!item.shown || std::max(item.bounds.width(), item.bounds.height()) < 8.0 || !item.bounds.adjusted(-1, -1, 1, 1).intersects(view)) {
			continue;
		}
		const QString text = metrics.elidedText(levelLinkedGroupLabel(*item.box), rtl ? Qt::ElideLeft : Qt::ElideRight, area.width() - 8.0);
		if (text.isEmpty()) {
			continue;
		}
		const QSizeF size(std::ceil(metrics.horizontalAdvance(text)) + 8.0, std::ceil(metrics.height()) + 4.0);
		// Level with the box's leading edge first, as the plan views write
		// names, then its trailing edge; then a line or two further out when
		// the selection's handles sit where the name would.
		const auto within = [&area, &size](double x) { return std::clamp(x, area.left(), std::max(area.left(), area.right() - size.width())); };
		const double leading = within(rtl ? item.bounds.right() - size.width() : item.bounds.left());
		const double trailing = within(rtl ? item.bounds.left() : item.bounds.right() - size.width());
		const double step = size.height() + 4.0;
		QRectF placed;
		for (int ring = 0; ring < 3 && placed.isEmpty(); ++ring) {
			for (const double y : {item.bounds.top() - size.height() - 3.0 - ring * step, item.bounds.bottom() + 3.0 + ring * step}) {
				for (const double x : {leading, trailing}) {
					const QRectF candidate(QPointF(x, y), size);
					if (placed.isEmpty() && area.contains(candidate)
						&& std::none_of(taken.cbegin(), taken.cend(), [&candidate](const QRectF& other) {
							   return !other.isEmpty() && other.intersects(candidate);
						   })) {
						placed = candidate;
					}
				}
			}
		}
		if (placed.isEmpty()) {
			continue;
		}
		taken.push_back(placed.adjusted(-2, -2, 2, 2));
		const QColor colour = item.box->outOfSync ? warningColour : linkColour;
		paintViewportLabel(painter, {placed, text}, layoutDirection(), colour, palette.background, m_highContrast);
		++m_linkedGroupPaint.names;
	}
	painter.restore();
}

} // namespace vibestudio
