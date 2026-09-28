#include "app/studio_icons.h"

#include "app/studio_theme.h"

#include <QGuiApplication>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QtMath>

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <utility>

namespace vibestudio {

namespace {

// Glyphs are drawn on a 24x24 unit grid with a 1.75-unit stroke, round caps,
// and round joins, which lands at roughly 1.2px at 16px and 1.75px at 24px.
constexpr qreal kGrid = 24.0;
constexpr qreal kStroke = 1.75;

class GlyphPainter {
public:
	GlyphPainter(QPainter& painter, const QColor& color)
		: m_painter(painter)
		, m_color(color)
	{
		QPen pen(color, kStroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
		m_painter.setPen(pen);
		m_painter.setBrush(Qt::NoBrush);
	}

	void line(qreal x1, qreal y1, qreal x2, qreal y2)
	{
		m_painter.drawLine(QPointF(x1, y1), QPointF(x2, y2));
	}

	void polyline(std::initializer_list<QPointF> points)
	{
		m_painter.drawPolyline(QPolygonF(QVector<QPointF>(points)));
	}

	void polygon(std::initializer_list<QPointF> points, bool filled = false)
	{
		const QPolygonF shape{QVector<QPointF>(points)};
		if (filled) {
			m_painter.setBrush(m_color);
		}
		m_painter.drawPolygon(shape);
		m_painter.setBrush(Qt::NoBrush);
	}

	void circle(qreal cx, qreal cy, qreal radius, bool filled = false)
	{
		if (filled) {
			m_painter.setBrush(m_color);
		}
		m_painter.drawEllipse(QPointF(cx, cy), radius, radius);
		m_painter.setBrush(Qt::NoBrush);
	}

	void dot(qreal cx, qreal cy, qreal radius = 1.1)
	{
		m_painter.save();
		m_painter.setPen(Qt::NoPen);
		m_painter.setBrush(m_color);
		m_painter.drawEllipse(QPointF(cx, cy), radius, radius);
		m_painter.restore();
	}

	void rect(qreal x1, qreal y1, qreal x2, qreal y2, qreal radius = 0.0, bool filled = false)
	{
		if (filled) {
			m_painter.setBrush(m_color);
		}
		m_painter.drawRoundedRect(QRectF(QPointF(x1, y1), QPointF(x2, y2)), radius, radius);
		m_painter.setBrush(Qt::NoBrush);
	}

	void path(const QPainterPath& shape, bool filled = false)
	{
		if (filled) {
			m_painter.setBrush(m_color);
		}
		m_painter.drawPath(shape);
		m_painter.setBrush(Qt::NoBrush);
	}

	void arc(qreal cx, qreal cy, qreal radius, qreal startDegrees, qreal spanDegrees)
	{
		QPainterPath shape;
		const QRectF bounds(cx - radius, cy - radius, radius * 2.0, radius * 2.0);
		shape.arcMoveTo(bounds, startDegrees);
		shape.arcTo(bounds, startDegrees, spanDegrees);
		m_painter.drawPath(shape);
	}

	void widerPen(qreal width)
	{
		QPen pen = m_painter.pen();
		pen.setWidthF(width);
		m_painter.setPen(pen);
	}

private:
	QPainter& m_painter;
	QColor m_color;
};

using GlyphFunction = std::function<void(GlyphPainter&)>;

QPointF polar(qreal cx, qreal cy, qreal radius, qreal degrees)
{
	const qreal radians = qDegreesToRadians(degrees);
	return {cx + radius * qCos(radians), cy - radius * qSin(radians)};
}

const QHash<QString, GlyphFunction>& glyphs()
{
	static const QHash<QString, GlyphFunction> table = [] {
		QHash<QString, GlyphFunction> g;

		g.insert(QStringLiteral("home"), [](GlyphPainter& p) {
			p.polyline({{3.0, 11.0}, {12.0, 3.5}, {21.0, 11.0}});
			p.polyline({{5.5, 9.5}, {5.5, 20.0}, {18.5, 20.0}, {18.5, 9.5}});
			p.polyline({{10.0, 20.0}, {10.0, 14.5}, {14.0, 14.5}, {14.0, 20.0}});
		});
		g.insert(QStringLiteral("map"), [](GlyphPainter& p) {
			p.polygon({{3.0, 6.0}, {9.0, 3.5}, {15.0, 6.0}, {21.0, 3.5}, {21.0, 18.0}, {15.0, 20.5}, {9.0, 18.0}, {3.0, 20.5}});
			p.line(9.0, 3.5, 9.0, 18.0);
			p.line(15.0, 6.0, 15.0, 20.5);
		});
		g.insert(QStringLiteral("cube"), [](GlyphPainter& p) {
			p.polygon({{12.0, 3.0}, {20.0, 7.5}, {20.0, 16.5}, {12.0, 21.0}, {4.0, 16.5}, {4.0, 7.5}});
			p.polyline({{4.0, 7.5}, {12.0, 12.0}, {20.0, 7.5}});
			p.line(12.0, 12.0, 12.0, 21.0);
		});
		g.insert(QStringLiteral("image"), [](GlyphPainter& p) {
			p.rect(3.0, 4.5, 21.0, 19.5, 2.0);
			p.circle(8.5, 9.5, 1.8);
			p.polyline({{3.5, 17.5}, {9.0, 12.5}, {13.0, 16.0}, {16.0, 13.0}, {20.5, 17.5}});
		});
		g.insert(QStringLiteral("waveform"), [](GlyphPainter& p) {
			p.line(4.0, 10.0, 4.0, 14.0);
			p.line(7.2, 7.0, 7.2, 17.0);
			p.line(10.4, 4.0, 10.4, 20.0);
			p.line(13.6, 8.0, 13.6, 16.0);
			p.line(16.8, 5.5, 16.8, 18.5);
			p.line(20.0, 10.0, 20.0, 14.0);
		});
		g.insert(QStringLiteral("package"), [](GlyphPainter& p) {
			p.rect(2.75, 4.0, 21.25, 8.5, 1.0);
			p.polyline({{4.0, 8.5}, {4.0, 20.0}, {20.0, 20.0}, {20.0, 8.5}});
			p.line(10.0, 12.5, 14.0, 12.5);
		});
		g.insert(QStringLiteral("code"), [](GlyphPainter& p) {
			p.polyline({{8.5, 7.0}, {3.5, 12.0}, {8.5, 17.0}});
			p.polyline({{15.5, 7.0}, {20.5, 12.0}, {15.5, 17.0}});
			p.line(13.5, 5.0, 10.5, 19.0);
		});
		g.insert(QStringLiteral("layers"), [](GlyphPainter& p) {
			p.polygon({{12.0, 3.5}, {21.0, 8.0}, {12.0, 12.5}, {3.0, 8.0}});
			p.polyline({{3.0, 12.25}, {12.0, 16.75}, {21.0, 12.25}});
			p.polyline({{3.0, 16.25}, {12.0, 20.75}, {21.0, 16.25}});
		});
		g.insert(QStringLiteral("hammer"), [](GlyphPainter& p) {
			p.polygon({{10.5, 6.5}, {14.5, 2.5}, {21.5, 9.5}, {17.5, 13.5}});
			p.widerPen(2.4);
			p.line(12.5, 11.5, 4.0, 20.0);
		});
		g.insert(QStringLiteral("settings"), [](GlyphPainter& p) {
			QPainterPath gear;
			bool first = true;
			for (int tooth = 0; tooth < 8; ++tooth) {
				const qreal centre = tooth * 45.0;
				for (const auto& [radius, offset] : {std::pair<qreal, qreal>{6.6, -17.0}, {9.0, -9.5}, {9.0, 9.5}, {6.6, 17.0}}) {
					const QPointF point = polar(12.0, 12.0, radius, centre + offset);
					if (first) {
						gear.moveTo(point);
						first = false;
					} else {
						gear.lineTo(point);
					}
				}
			}
			gear.closeSubpath();
			p.path(gear);
			p.circle(12.0, 12.0, 2.8);
		});
		g.insert(QStringLiteral("folder"), [](GlyphPainter& p) {
			QPainterPath shape;
			shape.moveTo(3.0, 6.5);
			shape.quadTo(3.0, 5.0, 4.5, 5.0);
			shape.lineTo(9.5, 5.0);
			shape.lineTo(11.5, 7.5);
			shape.lineTo(19.5, 7.5);
			shape.quadTo(21.0, 7.5, 21.0, 9.0);
			shape.lineTo(21.0, 18.0);
			shape.quadTo(21.0, 19.5, 19.5, 19.5);
			shape.lineTo(4.5, 19.5);
			shape.quadTo(3.0, 19.5, 3.0, 18.0);
			shape.closeSubpath();
			p.path(shape);
		});
		g.insert(QStringLiteral("folder-open"), [](GlyphPainter& p) {
			QPainterPath back;
			back.moveTo(3.0, 18.5);
			back.lineTo(3.0, 6.5);
			back.quadTo(3.0, 5.0, 4.5, 5.0);
			back.lineTo(9.5, 5.0);
			back.lineTo(11.5, 7.5);
			back.lineTo(18.0, 7.5);
			back.quadTo(19.5, 7.5, 19.5, 9.0);
			back.lineTo(19.5, 10.5);
			p.path(back);
			p.polygon({{3.0, 18.5}, {6.0, 10.5}, {22.0, 10.5}, {19.0, 18.5}});
		});
		g.insert(QStringLiteral("file"), [](GlyphPainter& p) {
			p.polygon({{6.0, 3.0}, {14.0, 3.0}, {19.0, 8.0}, {19.0, 21.0}, {6.0, 21.0}});
			p.polyline({{14.0, 3.0}, {14.0, 8.0}, {19.0, 8.0}});
		});
		g.insert(QStringLiteral("save"), [](GlyphPainter& p) {
			p.polygon({{4.0, 4.0}, {16.5, 4.0}, {20.0, 7.5}, {20.0, 20.0}, {4.0, 20.0}});
			p.polyline({{7.5, 4.0}, {7.5, 8.5}, {15.0, 8.5}, {15.0, 4.0}});
			p.rect(7.0, 13.0, 17.0, 20.0, 0.5);
		});
		g.insert(QStringLiteral("export"), [](GlyphPainter& p) {
			p.polyline({{4.0, 14.0}, {4.0, 20.0}, {20.0, 20.0}, {20.0, 14.0}});
			p.line(12.0, 15.0, 12.0, 3.5);
			p.polyline({{7.5, 8.0}, {12.0, 3.5}, {16.5, 8.0}});
		});
		g.insert(QStringLiteral("import"), [](GlyphPainter& p) {
			p.polyline({{4.0, 14.0}, {4.0, 20.0}, {20.0, 20.0}, {20.0, 14.0}});
			p.line(12.0, 3.5, 12.0, 15.0);
			p.polyline({{7.5, 10.5}, {12.0, 15.0}, {16.5, 10.5}});
		});
		g.insert(QStringLiteral("undo"), [](GlyphPainter& p) {
			p.polyline({{9.0, 5.0}, {4.0, 10.0}, {9.0, 15.0}});
			QPainterPath shape;
			shape.moveTo(4.0, 10.0);
			shape.lineTo(14.5, 10.0);
			shape.cubicTo(18.0, 10.0, 20.0, 12.5, 20.0, 15.0);
			shape.cubicTo(20.0, 17.5, 18.0, 20.0, 14.5, 20.0);
			shape.lineTo(11.0, 20.0);
			p.path(shape);
		});
		g.insert(QStringLiteral("redo"), [](GlyphPainter& p) {
			p.polyline({{15.0, 5.0}, {20.0, 10.0}, {15.0, 15.0}});
			QPainterPath shape;
			shape.moveTo(20.0, 10.0);
			shape.lineTo(9.5, 10.0);
			shape.cubicTo(6.0, 10.0, 4.0, 12.5, 4.0, 15.0);
			shape.cubicTo(4.0, 17.5, 6.0, 20.0, 9.5, 20.0);
			shape.lineTo(13.0, 20.0);
			p.path(shape);
		});
		g.insert(QStringLiteral("command"), [](GlyphPainter& p) {
			p.rect(3.0, 5.0, 21.0, 19.0, 2.5);
			p.polyline({{7.0, 9.5}, {10.0, 12.0}, {7.0, 14.5}});
			p.line(12.0, 15.0, 17.0, 15.0);
		});
		g.insert(QStringLiteral("terminal"), [](GlyphPainter& p) {
			p.rect(3.0, 4.5, 21.0, 19.5, 2.0);
			p.line(3.0, 8.0, 21.0, 8.0);
			p.polyline({{7.0, 11.5}, {9.5, 14.0}, {7.0, 16.5}});
			p.line(12.0, 16.5, 16.5, 16.5);
		});
		g.insert(QStringLiteral("play"), [](GlyphPainter& p) {
			p.polygon({{7.5, 4.5}, {19.0, 12.0}, {7.5, 19.5}}, true);
		});
		g.insert(QStringLiteral("stop"), [](GlyphPainter& p) {
			p.rect(6.0, 6.0, 18.0, 18.0, 1.5, true);
		});
		g.insert(QStringLiteral("pause"), [](GlyphPainter& p) {
			p.rect(6.5, 5.0, 10.0, 19.0, 1.0, true);
			p.rect(14.0, 5.0, 17.5, 19.0, 1.0, true);
		});
		g.insert(QStringLiteral("search"), [](GlyphPainter& p) {
			p.circle(10.5, 10.5, 6.0);
			p.line(15.0, 15.0, 20.5, 20.5);
		});
		g.insert(QStringLiteral("info"), [](GlyphPainter& p) {
			p.circle(12.0, 12.0, 9.0);
			p.line(12.0, 11.0, 12.0, 16.5);
			p.dot(12.0, 7.75);
		});
		g.insert(QStringLiteral("warning"), [](GlyphPainter& p) {
			p.polygon({{12.0, 3.5}, {21.5, 20.0}, {2.5, 20.0}});
			p.line(12.0, 9.5, 12.0, 14.0);
			p.dot(12.0, 17.0);
		});
		g.insert(QStringLiteral("error"), [](GlyphPainter& p) {
			p.circle(12.0, 12.0, 9.0);
			p.line(9.0, 9.0, 15.0, 15.0);
			p.line(15.0, 9.0, 9.0, 15.0);
		});
		g.insert(QStringLiteral("success"), [](GlyphPainter& p) {
			p.circle(12.0, 12.0, 9.0);
			p.polyline({{8.0, 12.5}, {11.0, 15.5}, {16.5, 9.0}});
		});
		g.insert(QStringLiteral("check"), [](GlyphPainter& p) {
			p.polyline({{4.5, 12.5}, {9.5, 17.5}, {19.5, 6.5}});
		});
		g.insert(QStringLiteral("close"), [](GlyphPainter& p) {
			p.line(6.0, 6.0, 18.0, 18.0);
			p.line(18.0, 6.0, 6.0, 18.0);
		});
		g.insert(QStringLiteral("plus"), [](GlyphPainter& p) {
			p.line(12.0, 5.0, 12.0, 19.0);
			p.line(5.0, 12.0, 19.0, 12.0);
		});
		g.insert(QStringLiteral("minus"), [](GlyphPainter& p) {
			p.line(5.0, 12.0, 19.0, 12.0);
		});
		g.insert(QStringLiteral("trash"), [](GlyphPainter& p) {
			p.line(4.0, 7.0, 20.0, 7.0);
			p.polyline({{9.5, 7.0}, {9.5, 4.5}, {14.5, 4.5}, {14.5, 7.0}});
			p.polyline({{6.0, 7.0}, {7.0, 20.0}, {17.0, 20.0}, {18.0, 7.0}});
			p.line(10.0, 11.0, 10.0, 16.0);
			p.line(14.0, 11.0, 14.0, 16.0);
		});
		g.insert(QStringLiteral("edit"), [](GlyphPainter& p) {
			p.polygon({{4.0, 20.0}, {5.0, 15.5}, {15.5, 5.0}, {19.0, 8.5}, {8.5, 19.0}});
			p.line(13.0, 7.5, 16.5, 11.0);
		});
		g.insert(QStringLiteral("copy"), [](GlyphPainter& p) {
			p.rect(8.5, 8.5, 20.0, 20.0, 2.0);
			QPainterPath back;
			back.moveTo(15.5, 8.5);
			back.lineTo(15.5, 5.5);
			back.quadTo(15.5, 4.0, 14.0, 4.0);
			back.lineTo(5.5, 4.0);
			back.quadTo(4.0, 4.0, 4.0, 5.5);
			back.lineTo(4.0, 14.0);
			back.quadTo(4.0, 15.5, 5.5, 15.5);
			back.lineTo(8.5, 15.5);
			p.path(back);
		});
		g.insert(QStringLiteral("refresh"), [](GlyphPainter& p) {
			// A 285-degree arc whose open end carries a clockwise arrowhead.
			constexpr qreal start = 60.0;
			p.arc(12.0, 12.0, 7.5, start, 285.0);
			const QPointF tip = polar(12.0, 12.0, 7.5, start);
			const qreal radians = qDegreesToRadians(start);
			const QPointF direction(qSin(radians), qCos(radians));
			auto rotated = [](const QPointF& vector, qreal degrees) {
				const qreal angle = qDegreesToRadians(degrees);
				return QPointF(vector.x() * qCos(angle) - vector.y() * qSin(angle), vector.x() * qSin(angle) + vector.y() * qCos(angle));
			};
			p.polyline({tip - rotated(direction, 40.0) * 4.0, tip, tip - rotated(direction, -40.0) * 4.0});
		});
		g.insert(QStringLiteral("filter"), [](GlyphPainter& p) {
			p.polygon({{3.5, 5.0}, {20.5, 5.0}, {14.0, 12.5}, {14.0, 19.0}, {10.0, 21.0}, {10.0, 12.5}});
		});
		g.insert(QStringLiteral("eye"), [](GlyphPainter& p) {
			QPainterPath shape;
			shape.moveTo(2.5, 12.0);
			shape.quadTo(12.0, 2.5, 21.5, 12.0);
			shape.quadTo(12.0, 21.5, 2.5, 12.0);
			p.path(shape);
			p.circle(12.0, 12.0, 3.0);
		});
		g.insert(QStringLiteral("compare"), [](GlyphPainter& p) {
			p.rect(3.0, 4.0, 10.0, 20.0, 1.5);
			p.rect(14.0, 4.0, 21.0, 20.0, 1.5);
			p.line(5.5, 8.5, 7.5, 8.5);
			p.line(16.5, 8.5, 18.5, 8.5);
			p.line(5.5, 12.0, 7.5, 12.0);
			p.line(16.5, 12.0, 18.5, 12.0);
		});
		g.insert(QStringLiteral("gamepad"), [](GlyphPainter& p) {
			QPainterPath body;
			body.moveTo(6.5, 7.5);
			body.lineTo(17.5, 7.5);
			body.cubicTo(20.5, 7.5, 21.8, 10.5, 21.8, 13.5);
			body.cubicTo(21.8, 16.5, 20.5, 18.0, 18.8, 18.0);
			body.cubicTo(17.2, 18.0, 16.3, 16.8, 15.5, 15.5);
			body.lineTo(8.5, 15.5);
			body.cubicTo(7.7, 16.8, 6.8, 18.0, 5.2, 18.0);
			body.cubicTo(3.5, 18.0, 2.2, 16.5, 2.2, 13.5);
			body.cubicTo(2.2, 10.5, 3.5, 7.5, 6.5, 7.5);
			body.closeSubpath();
			p.path(body);
			p.line(7.0, 10.5, 7.0, 13.5);
			p.line(5.5, 12.0, 8.5, 12.0);
			p.dot(15.5, 11.0, 1.05);
			p.dot(17.8, 13.2, 1.05);
		});
		g.insert(QStringLiteral("plugin"), [](GlyphPainter& p) {
			p.line(9.0, 3.0, 9.0, 7.0);
			p.line(15.0, 3.0, 15.0, 7.0);
			QPainterPath body;
			body.moveTo(6.5, 7.0);
			body.lineTo(17.5, 7.0);
			body.lineTo(17.5, 11.0);
			body.cubicTo(17.5, 14.3, 15.0, 16.0, 12.0, 16.0);
			body.cubicTo(9.0, 16.0, 6.5, 14.3, 6.5, 11.0);
			body.closeSubpath();
			p.path(body);
			p.line(12.0, 16.0, 12.0, 21.0);
		});
		g.insert(QStringLiteral("list"), [](GlyphPainter& p) {
			p.line(8.5, 6.5, 20.0, 6.5);
			p.line(8.5, 12.0, 20.0, 12.0);
			p.line(8.5, 17.5, 20.0, 17.5);
			p.dot(4.5, 6.5, 1.2);
			p.dot(4.5, 12.0, 1.2);
			p.dot(4.5, 17.5, 1.2);
		});
		g.insert(QStringLiteral("tree"), [](GlyphPainter& p) {
			p.rect(3.0, 3.0, 9.0, 8.0, 1.0);
			p.polyline({{6.0, 8.0}, {6.0, 17.0}, {11.0, 17.0}});
			p.line(6.0, 11.5, 11.0, 11.5);
			p.rect(12.0, 9.0, 21.0, 14.0, 1.0);
			p.rect(12.0, 14.75, 21.0, 19.75, 1.0);
		});
		g.insert(QStringLiteral("frame"), [](GlyphPainter& p) {
			p.polyline({{4.0, 9.0}, {4.0, 4.0}, {9.0, 4.0}});
			p.polyline({{15.0, 4.0}, {20.0, 4.0}, {20.0, 9.0}});
			p.polyline({{20.0, 15.0}, {20.0, 20.0}, {15.0, 20.0}});
			p.polyline({{9.0, 20.0}, {4.0, 20.0}, {4.0, 15.0}});
			p.rect(9.0, 9.0, 15.0, 15.0, 1.0);
		});
		g.insert(QStringLiteral("move"), [](GlyphPainter& p) {
			p.line(12.0, 3.0, 12.0, 21.0);
			p.line(3.0, 12.0, 21.0, 12.0);
			p.polyline({{9.5, 5.5}, {12.0, 3.0}, {14.5, 5.5}});
			p.polyline({{9.5, 18.5}, {12.0, 21.0}, {14.5, 18.5}});
			p.polyline({{5.5, 9.5}, {3.0, 12.0}, {5.5, 14.5}});
			p.polyline({{18.5, 9.5}, {21.0, 12.0}, {18.5, 14.5}});
		});
		g.insert(QStringLiteral("key"), [](GlyphPainter& p) {
			p.circle(8.0, 16.0, 4.5);
			p.line(11.2, 12.8, 20.0, 4.0);
			p.line(15.5, 8.5, 18.0, 11.0);
			p.line(17.8, 6.2, 20.3, 8.7);
		});
		g.insert(QStringLiteral("grid"), [](GlyphPainter& p) {
			p.rect(4.0, 4.0, 10.5, 10.5, 1.5);
			p.rect(13.5, 4.0, 20.0, 10.5, 1.5);
			p.rect(4.0, 13.5, 10.5, 20.0, 1.5);
			p.rect(13.5, 13.5, 20.0, 20.0, 1.5);
		});
		g.insert(QStringLiteral("sparkle"), [](GlyphPainter& p) {
			QPainterPath star;
			star.moveTo(10.5, 3.5);
			star.quadTo(11.5, 11.0, 18.5, 12.0);
			star.quadTo(11.5, 13.0, 10.5, 20.5);
			star.quadTo(9.5, 13.0, 2.5, 12.0);
			star.quadTo(9.5, 11.0, 10.5, 3.5);
			star.closeSubpath();
			p.path(star);
			p.line(19.0, 2.5, 19.0, 7.5);
			p.line(16.5, 5.0, 21.5, 5.0);
		});
		g.insert(QStringLiteral("palette"), [](GlyphPainter& p) {
			QPainterPath blob;
			blob.moveTo(12.0, 3.0);
			blob.cubicTo(6.5, 3.0, 3.0, 7.0, 3.0, 12.0);
			blob.cubicTo(3.0, 17.0, 7.0, 21.0, 12.0, 21.0);
			blob.cubicTo(13.4, 21.0, 14.0, 20.2, 14.0, 19.2);
			blob.cubicTo(14.0, 17.8, 13.0, 17.3, 13.0, 16.2);
			blob.cubicTo(13.0, 15.1, 13.9, 14.5, 15.0, 14.5);
			blob.lineTo(17.0, 14.5);
			blob.cubicTo(19.5, 14.5, 21.0, 12.9, 21.0, 10.5);
			blob.cubicTo(21.0, 6.5, 17.0, 3.0, 12.0, 3.0);
			blob.closeSubpath();
			p.path(blob);
			p.dot(7.5, 11.5, 1.3);
			p.dot(9.5, 7.3, 1.3);
			p.dot(14.2, 6.8, 1.3);
			p.dot(17.5, 9.8, 1.3);
		});
		g.insert(QStringLiteral("cancel"), [](GlyphPainter& p) {
			p.circle(12.0, 12.0, 9.0);
			p.line(5.7, 5.7, 18.3, 18.3);
		});
		g.insert(QStringLiteral("clock"), [](GlyphPainter& p) {
			p.circle(12.0, 12.0, 9.0);
			p.polyline({{12.0, 7.0}, {12.0, 12.0}, {15.5, 14.0}});
		});
		g.insert(QStringLiteral("activity"), [](GlyphPainter& p) {
			p.polyline({{3.0, 12.0}, {7.0, 12.0}, {9.5, 5.0}, {14.5, 19.0}, {17.0, 12.0}, {21.0, 12.0}});
		});
		g.insert(QStringLiteral("sidebar-right"), [](GlyphPainter& p) {
			p.rect(3.0, 4.0, 21.0, 20.0, 2.0);
			p.line(15.0, 4.0, 15.0, 20.0);
		});
		g.insert(QStringLiteral("sidebar-left"), [](GlyphPainter& p) {
			p.rect(3.0, 4.0, 21.0, 20.0, 2.0);
			p.line(9.0, 4.0, 9.0, 20.0);
		});
		g.insert(QStringLiteral("panel-bottom"), [](GlyphPainter& p) {
			p.rect(3.0, 4.0, 21.0, 20.0, 2.0);
			p.line(3.0, 15.0, 21.0, 15.0);
		});
		g.insert(QStringLiteral("chevron-left"), [](GlyphPainter& p) {
			p.polyline({{14.5, 6.0}, {8.5, 12.0}, {14.5, 18.0}});
		});
		g.insert(QStringLiteral("chevron-right"), [](GlyphPainter& p) {
			p.polyline({{9.5, 6.0}, {15.5, 12.0}, {9.5, 18.0}});
		});
		g.insert(QStringLiteral("chevron-down"), [](GlyphPainter& p) {
			p.polyline({{6.0, 9.5}, {12.0, 15.5}, {18.0, 9.5}});
		});
		g.insert(QStringLiteral("chevron-up"), [](GlyphPainter& p) {
			p.polyline({{6.0, 14.5}, {12.0, 8.5}, {18.0, 14.5}});
		});
		g.insert(QStringLiteral("dot"), [](GlyphPainter& p) {
			p.dot(12.0, 12.0, 4.0);
		});
		g.insert(QStringLiteral("book"), [](GlyphPainter& p) {
			QPainterPath left;
			left.moveTo(12.0, 6.5);
			left.cubicTo(10.0, 4.8, 7.0, 4.5, 3.0, 5.0);
			left.lineTo(3.0, 19.0);
			left.cubicTo(7.0, 18.5, 10.0, 18.8, 12.0, 20.5);
			p.path(left);
			QPainterPath right;
			right.moveTo(12.0, 6.5);
			right.cubicTo(14.0, 4.8, 17.0, 4.5, 21.0, 5.0);
			right.lineTo(21.0, 19.0);
			right.cubicTo(17.0, 18.5, 14.0, 18.8, 12.0, 20.5);
			p.path(right);
			p.line(12.0, 6.5, 12.0, 20.5);
		});
		g.insert(QStringLiteral("external"), [](GlyphPainter& p) {
			p.polyline({{13.5, 4.0}, {20.0, 4.0}, {20.0, 10.5}});
			p.line(20.0, 4.0, 11.0, 13.0);
			QPainterPath box;
			box.moveTo(17.0, 13.5);
			box.lineTo(17.0, 18.5);
			box.quadTo(17.0, 20.0, 15.5, 20.0);
			box.lineTo(5.5, 20.0);
			box.quadTo(4.0, 20.0, 4.0, 18.5);
			box.lineTo(4.0, 8.5);
			box.quadTo(4.0, 7.0, 5.5, 7.0);
			box.lineTo(10.5, 7.0);
			p.path(box);
		});
		g.insert(QStringLiteral("hash"), [](GlyphPainter& p) {
			p.line(10.0, 4.0, 8.0, 20.0);
			p.line(16.0, 4.0, 14.0, 20.0);
			p.line(4.5, 9.0, 20.0, 9.0);
			p.line(4.0, 15.0, 19.5, 15.0);
		});
		g.insert(QStringLiteral("film"), [](GlyphPainter& p) {
			p.rect(3.0, 4.5, 21.0, 19.5, 2.0);
			p.line(7.5, 4.5, 7.5, 19.5);
			p.line(16.5, 4.5, 16.5, 19.5);
			p.line(3.0, 12.0, 7.5, 12.0);
			p.line(16.5, 12.0, 21.0, 12.0);
		});
		g.insert(QStringLiteral("link"), [](GlyphPainter& p) {
			QPainterPath first;
			first.moveTo(10.0, 14.0);
			first.lineTo(8.0, 16.0);
			first.cubicTo(6.5, 17.5, 4.5, 17.5, 3.5, 16.5);
			first.cubicTo(2.5, 15.5, 2.5, 13.5, 4.0, 12.0);
			first.lineTo(7.5, 8.5);
			first.cubicTo(9.0, 7.0, 11.0, 7.0, 12.0, 8.5);
			p.path(first);
			QPainterPath second;
			second.moveTo(14.0, 10.0);
			second.lineTo(16.0, 8.0);
			second.cubicTo(17.5, 6.5, 19.5, 6.5, 20.5, 7.5);
			second.cubicTo(21.5, 8.5, 21.5, 10.5, 20.0, 12.0);
			second.lineTo(16.5, 15.5);
			second.cubicTo(15.0, 17.0, 13.0, 17.0, 12.0, 15.5);
			p.path(second);
		});
		g.insert(QStringLiteral("crosshair"), [](GlyphPainter& p) {
			p.circle(12.0, 12.0, 7.5);
			p.line(12.0, 2.5, 12.0, 7.0);
			p.line(12.0, 17.0, 12.0, 21.5);
			p.line(2.5, 12.0, 7.0, 12.0);
			p.line(17.0, 12.0, 21.5, 12.0);
		});
		g.insert(QStringLiteral("rocket"), [](GlyphPainter& p) {
			QPainterPath body;
			body.moveTo(12.0, 2.5);
			body.cubicTo(15.5, 5.0, 16.5, 9.0, 16.0, 15.0);
			body.lineTo(8.0, 15.0);
			body.cubicTo(7.5, 9.0, 8.5, 5.0, 12.0, 2.5);
			body.closeSubpath();
			p.path(body);
			p.circle(12.0, 9.0, 1.6);
			p.polyline({{8.0, 12.5}, {5.0, 15.5}, {5.0, 18.5}, {8.3, 16.8}});
			p.polyline({{16.0, 12.5}, {19.0, 15.5}, {19.0, 18.5}, {15.7, 16.8}});
			p.line(10.5, 18.0, 10.5, 21.0);
			p.line(13.5, 18.0, 13.5, 21.0);
		});
		return g;
	}();
	return table;
}

// Symbolic aliases so command registrations can name what an action does
// ("extract", "launch") and still resolve to one glyph.
QString canonicalIconName(const QString& name)
{
	static const QHash<QString, QString> aliases = {
		{QStringLiteral("workspace"), QStringLiteral("home")},
		{QStringLiteral("levels"), QStringLiteral("map")},
		{QStringLiteral("level"), QStringLiteral("map")},
		{QStringLiteral("models"), QStringLiteral("cube")},
		{QStringLiteral("model"), QStringLiteral("cube")},
		{QStringLiteral("textures"), QStringLiteral("image")},
		{QStringLiteral("texture"), QStringLiteral("image")},
		{QStringLiteral("audio"), QStringLiteral("waveform")},
		{QStringLiteral("sound"), QStringLiteral("waveform")},
		{QStringLiteral("packages"), QStringLiteral("package")},
		{QStringLiteral("archive"), QStringLiteral("package")},
		{QStringLiteral("shaders"), QStringLiteral("layers")},
		{QStringLiteral("shader"), QStringLiteral("layers")},
		{QStringLiteral("build"), QStringLiteral("hammer")},
		{QStringLiteral("compile"), QStringLiteral("hammer")},
		{QStringLiteral("preferences"), QStringLiteral("settings")},
		{QStringLiteral("open"), QStringLiteral("folder-open")},
		{QStringLiteral("project"), QStringLiteral("folder")},
		{QStringLiteral("document"), QStringLiteral("file")},
		{QStringLiteral("save-as"), QStringLiteral("save")},
		{QStringLiteral("extract"), QStringLiteral("import")},
		{QStringLiteral("download"), QStringLiteral("import")},
		{QStringLiteral("add"), QStringLiteral("plus")},
		{QStringLiteral("remove"), QStringLiteral("minus")},
		{QStringLiteral("delete"), QStringLiteral("trash")},
		{QStringLiteral("rename"), QStringLiteral("edit")},
		{QStringLiteral("reload"), QStringLiteral("refresh")},
		{QStringLiteral("rescan"), QStringLiteral("refresh")},
		{QStringLiteral("run"), QStringLiteral("play")},
		{QStringLiteral("launch"), QStringLiteral("gamepad")},
		{QStringLiteral("game"), QStringLiteral("gamepad")},
		{QStringLiteral("install"), QStringLiteral("gamepad")},
		{QStringLiteral("find"), QStringLiteral("search")},
		{QStringLiteral("detect"), QStringLiteral("search")},
		{QStringLiteral("about"), QStringLiteral("info")},
		{QStringLiteral("help"), QStringLiteral("book")},
		{QStringLiteral("error"), QStringLiteral("error")},
		{QStringLiteral("critical"), QStringLiteral("error")},
		{QStringLiteral("apply"), QStringLiteral("check")},
		{QStringLiteral("accept"), QStringLiteral("check")},
		{QStringLiteral("validate"), QStringLiteral("success")},
		{QStringLiteral("quit"), QStringLiteral("close")},
		{QStringLiteral("reveal"), QStringLiteral("external")},
		{QStringLiteral("diagnostics"), QStringLiteral("activity")},
		{QStringLiteral("log"), QStringLiteral("list")},
		{QStringLiteral("details"), QStringLiteral("list")},
		{QStringLiteral("report"), QStringLiteral("list")},
		{QStringLiteral("inspector"), QStringLiteral("sidebar-right")},
		{QStringLiteral("ai"), QStringLiteral("sparkle")},
		{QStringLiteral("extension"), QStringLiteral("plugin")},
		{QStringLiteral("zoom-fit"), QStringLiteral("frame")},
		{QStringLiteral("fit"), QStringLiteral("frame")},
		{QStringLiteral("sprite"), QStringLiteral("film")},
		{QStringLiteral("index"), QStringLiteral("hash")},
		{QStringLiteral("history"), QStringLiteral("clock")},
		{QStringLiteral("mode"), QStringLiteral("grid")},
	};
	const QString normalized = name.trimmed().toLower();
	return aliases.value(normalized, normalized);
}

QColor toneColor(StudioIconTone tone, QIcon::Mode mode)
{
	const StudioThemeTokens& theme = currentStudioTheme();
	const StudioThemeColors& c = theme.colors;
	if (mode == QIcon::Disabled) {
		return c.textFaint;
	}
	if (mode == QIcon::Selected && tone != StudioIconTone::OnAccent) {
		return c.selectionText;
	}
	switch (tone) {
	case StudioIconTone::Muted:
		return c.textMuted;
	case StudioIconTone::Accent:
		return c.accent;
	case StudioIconTone::OnAccent:
		return c.accentText;
	case StudioIconTone::Success:
		return c.success;
	case StudioIconTone::Warning:
		return c.warning;
	case StudioIconTone::Danger:
		return c.danger;
	case StudioIconTone::Normal:
		break;
	}
	if (mode == QIcon::Active || theme.highContrast) {
		return c.text;
	}
	// Chrome icons sit a step below body text so labels stay the focal point.
	QColor color = c.text;
	color.setAlphaF(theme.light ? 0.82f : 0.86f);
	return color;
}

void paintGlyph(QPainter& painter, const QString& canonicalName, const QRectF& target, const QColor& color)
{
	const auto it = glyphs().constFind(canonicalName);
	if (it == glyphs().constEnd()) {
		return;
	}
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.translate(target.topLeft());
	painter.scale(target.width() / kGrid, target.height() / kGrid);
	GlyphPainter glyph(painter, color);
	it.value()(glyph);
	painter.restore();
}

class StudioIconEngine final : public QIconEngine {
public:
	StudioIconEngine(QString name, StudioIconTone tone, StudioIconAlignment alignment)
		: m_name(std::move(name))
		, m_tone(tone)
		, m_alignment(alignment)
	{
	}

	void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override
	{
		if (!painter) {
			return;
		}
		const qreal side = std::min(rect.width(), rect.height());
		qreal x = rect.x() + (rect.width() - side) / 2.0;
		if (m_alignment == StudioIconAlignment::Leading) {
			x = QGuiApplication::layoutDirection() == Qt::RightToLeft ? rect.right() + 1 - side : rect.x();
		}
		const QRectF square(x, rect.y() + (rect.height() - side) / 2.0, side, side);
		paintGlyph(*painter, m_name, square, toneColor(m_tone, mode));
	}

	QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
	{
		return scaledPixmap(size, mode, state, 1.0);
	}

	QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override
	{
		const qreal ratio = scale > 0.0 ? scale : 1.0;
		QPixmap pixmap(size * ratio);
		pixmap.setDevicePixelRatio(ratio);
		pixmap.fill(Qt::transparent);
		QPainter painter(&pixmap);
		paint(&painter, QRect(QPoint(0, 0), size), mode, state);
		return pixmap;
	}

	QSize actualSize(const QSize& size, QIcon::Mode, QIcon::State) override
	{
		return size;
	}

	QIconEngine* clone() const override
	{
		return new StudioIconEngine(m_name, m_tone, m_alignment);
	}

	QString key() const override
	{
		return QStringLiteral("vibestudio-glyph");
	}

	QString iconName() override
	{
		return m_name;
	}

	bool isNull() override
	{
		return !glyphs().contains(m_name);
	}

private:
	QString m_name;
	StudioIconTone m_tone = StudioIconTone::Normal;
	StudioIconAlignment m_alignment = StudioIconAlignment::Centre;
};

} // namespace

QIcon studioIcon(const QString& name, StudioIconTone tone, StudioIconAlignment alignment)
{
	const QString canonical = canonicalIconName(name);
	if (!glyphs().contains(canonical)) {
		return {};
	}
	return QIcon(new StudioIconEngine(canonical, tone, alignment));
}

QPixmap studioIconPixmap(const QString& name, int logicalSize, qreal devicePixelRatio, const QColor& color)
{
	const qreal ratio = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;
	QPixmap pixmap(QSize(logicalSize, logicalSize) * ratio);
	pixmap.setDevicePixelRatio(ratio);
	pixmap.fill(Qt::transparent);
	QPainter painter(&pixmap);
	paintGlyph(painter, canonicalIconName(name), QRectF(0, 0, logicalSize, logicalSize), color);
	return pixmap;
}

bool studioIconExists(const QString& name)
{
	return glyphs().contains(canonicalIconName(name));
}

QStringList studioIconNames()
{
	QStringList names = glyphs().keys();
	names.sort();
	return names;
}

} // namespace vibestudio
