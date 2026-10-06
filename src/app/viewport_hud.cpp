#include "app/viewport_hud.h"

#include <QPainter>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio {
namespace {
constexpr qreal margin = 8.0;
constexpr qreal padX = 7.0;
constexpr qreal padY = 3.0;
const QString separator = QStringLiteral("  %1  ").arg(QChar(0x00b7));

qreal textWidth(const QStringList& lines, const QFontMetricsF& metrics)
{
	qreal width = 0.0;
	for (const auto& line : lines) { width = std::max(width, metrics.horizontalAdvance(line)); }
	return std::ceil(width);
}
}

ViewportHudLayout layoutViewportHud(const QRectF& viewport, const QFontMetricsF& metrics,
	const QStringList& leadingParts, const QStringList& trailingLines, Qt::LayoutDirection direction)
{
	ViewportHudLayout result;
	const QRectF area = viewport.adjusted(margin, margin, -margin, -margin);
	const qreal available = area.width() - padX * 2.0;
	const qreal lineHeight = std::ceil(metrics.height());
	if (leadingParts.isEmpty() || available <= 0.0 || lineHeight <= 0.0 || area.height() < lineHeight + padY * 2.0) { return result; }
	const bool twoRows = area.height() >= lineHeight * 2.0 + padY * 2.0;
	const QString whole = leadingParts.join(separator);
	if (metrics.horizontalAdvance(whole) <= available || !twoRows) {
		result.leading.lines = {metrics.elidedText(whole, Qt::ElideRight, available)};
	} else {
		// Wrap at field boundaries so a translated identity is not pushed off
		// the pane by selection dimensions, grid state or a material tool.
		QString first = leadingParts.first();
		qsizetype next = 1;
		while (next < leadingParts.size() && metrics.horizontalAdvance(first + separator + leadingParts.at(next)) <= available) {
			first += separator + leadingParts.at(next++);
		}
		result.leading.lines = {metrics.elidedText(first, Qt::ElideRight, available)};
		if (next < leadingParts.size()) {
			result.leading.lines << metrics.elidedText(leadingParts.mid(next).join(separator), Qt::ElideRight, available);
		}
	}
	const auto place = [&](ViewportHudTag& tag, bool trailing) {
		const qreal width = std::min(area.width(), textWidth(tag.lines, metrics) + padX * 2.0);
		const bool right = (direction == Qt::RightToLeft) != trailing;
		tag.bounds = QRectF(right ? area.right() - width : area.left(), area.top(), width, lineHeight * tag.lines.size() + padY * 2.0);
	};
	place(result.leading, false);
	const qreal trailingWidth = textWidth(trailingLines, metrics) + padX * 2.0;
	if (!trailingLines.isEmpty() && trailingLines.size() <= (twoRows ? 2 : 1)
		&& result.leading.bounds.width() + margin + trailingWidth <= area.width()) {
		result.trailing.lines = trailingLines;
		place(result.trailing, true);
	}
	return result;
}

QFont viewportHudFont(const QFont& baseFont)
{
	QFont font = baseFont;
	if (font.pointSizeF() > 0.0) { font.setPointSizeF(std::max(7.0, font.pointSizeF() * 0.9)); }
	else if (font.pixelSize() > 0) { font.setPixelSize(std::max(9, qRound(font.pixelSize() * 0.9))); }
	return font;
}

void paintViewportHud(QPainter& painter, const QRectF& viewport, const QFont& baseFont,
	const ViewportHudLayout& layout, Qt::LayoutDirection direction,
	const QColor& text, const QColor& background, bool highContrast)
{
	painter.save();
	painter.resetTransform();
	painter.setClipRect(viewport, Qt::IntersectClip);
	const QFont font = viewportHudFont(baseFont);
	painter.setFont(font);
	const QFontMetricsF metrics(font, painter.device());
	const qreal lineHeight = std::ceil(metrics.height());
	QColor backdrop = background;
	backdrop.setAlpha(highContrast ? 255 : 210);
	auto draw = [&](const ViewportHudTag& tag, bool trailing) {
		if (tag.lines.isEmpty()) { return; }
		painter.setPen(highContrast ? QPen(text, 1.0) : Qt::NoPen);
		painter.setBrush(backdrop);
		painter.drawRoundedRect(tag.bounds, 3.0, 3.0);
		painter.setPen(text);
		const bool right = (direction == Qt::RightToLeft) != trailing;
		const int flags = Qt::AlignAbsolute | Qt::AlignVCenter | Qt::TextSingleLine
			| (right ? Qt::AlignRight : Qt::AlignLeft)
			| (direction == Qt::RightToLeft ? Qt::TextForceRightToLeft : Qt::TextForceLeftToRight);
		for (qsizetype i = 0; i < tag.lines.size(); ++i) {
			const QRectF line(tag.bounds.left() + padX, tag.bounds.top() + padY + lineHeight * i,
				tag.bounds.width() - padX * 2.0, lineHeight);
			painter.drawText(line, flags, tag.lines.at(i));
		}
	};
	draw(layout.leading, false);
	draw(layout.trailing, true);
	painter.restore();
}

void paintViewportHud(QPainter& painter, const QRectF& viewport, const QFont& baseFont,
	const QStringList& leadingParts, const QStringList& trailingLines, Qt::LayoutDirection direction,
	const QColor& text, const QColor& background, bool highContrast)
{
	const QFontMetricsF metrics(viewportHudFont(baseFont),painter.device());
	paintViewportHud(painter,viewport,baseFont,layoutViewportHud(viewport,metrics,leadingParts,trailingLines,direction),direction,text,background,highContrast);
}

ViewportLabelLayout layoutViewportLabel(const QRectF& viewport, QPointF anchor, const QFontMetricsF& metrics,
	const QString& text, Qt::LayoutDirection direction, const ViewportHudLayout& hud)
{
	const QRectF area = viewport.adjusted(6,6,-6,-6);
	const qreal height = std::ceil(metrics.height()) + 4;
	if (!std::isfinite(anchor.x()) || !std::isfinite(anchor.y()) || !viewport.contains(anchor) || text.isEmpty()
		|| area.width() <= 8 || area.height() < height) { return {}; }
	const QString shown = metrics.elidedText(text.simplified(),direction == Qt::RightToLeft ? Qt::ElideLeft : Qt::ElideRight,area.width() - 8);
	if (shown.isEmpty()) { return {}; }
	const qreal width = std::min(area.width(),std::ceil(metrics.horizontalAdvance(shown)) + 8);
	const qreal before = anchor.x() - 20 - width, after = anchor.x() + 20;
	const qreal above = anchor.y() - 14 - height, below = anchor.y() + 20;
	const bool rtl = direction == Qt::RightToLeft;
	QVector<QPointF> positions {{rtl ? before : after,above},{rtl ? after : before,above},
		{rtl ? before : after,below},{rtl ? after : before,below}};
	const QRectF obstacles[] = {hud.leading.bounds,hud.trailing.bounds,QRectF(anchor - QPointF(18,18),QSizeF(36,36))};
	const auto fits = [&](const QRectF& candidate) {
		if (!area.contains(candidate)) { return false; }
		for (const auto& obstacle : obstacles) { if (!obstacle.isEmpty() && candidate.intersects(obstacle)) { return false; } }
		return true;
	};
	for (const auto position : positions) {
		const QRectF candidate(position,QSizeF(width,height));
		if (fits(candidate)) { return {candidate,shown}; }
	}
	// Clamping keeps edge labels legible. Try either side of a status tag as
	// well, so a top-corner selection does not disappear underneath the HUD.
	ViewportLabelLayout best;
	qreal bestDistance = std::numeric_limits<qreal>::infinity();
	const auto consider = [&](const QRectF& candidate) {
		if (!fits(candidate)) { return; }
		const qreal dx = anchor.x() - std::clamp(anchor.x(),candidate.left(),candidate.right());
		const qreal dy = anchor.y() - std::clamp(anchor.y(),candidate.top(),candidate.bottom());
		const qreal distance = dx * dx + dy * dy;
		if (distance < bestDistance) { bestDistance = distance; best = {candidate,shown}; }
	};
	for (const auto position : positions) {
		const QPointF clamped(std::clamp(position.x(),area.left(),area.right() - width),
			std::clamp(position.y(),area.top(),area.bottom() - height));
		const QRectF candidate(clamped,QSizeF(width,height));
		consider(candidate);
		for (const auto& obstacle : obstacles) {
			if (obstacle.isEmpty()) { continue; }
			for (const qreal top : {obstacle.bottom() + 4,obstacle.top() - height - 4}) {
				const QRectF shifted(QPointF(clamped.x(),top),QSizeF(width,height));
				consider(shifted);
			}
		}
	}
	return best;
}

void paintViewportLabel(QPainter& painter, const ViewportLabelLayout& label, Qt::LayoutDirection direction,
	const QColor& text, const QColor& background, bool highContrast)
{
	if (label.text.isEmpty()) { return; }
	painter.save(); painter.setClipRect(label.bounds,Qt::IntersectClip);
	QColor backdrop = background; backdrop.setAlpha(highContrast ? 255 : 230);
	painter.setPen(highContrast ? QPen(text,1.0) : Qt::NoPen); painter.setBrush(backdrop);
	painter.drawRoundedRect(label.bounds,3.0,3.0); painter.setPen(text);
	const int flags = Qt::AlignAbsolute | Qt::AlignVCenter | Qt::TextSingleLine
		| (direction == Qt::RightToLeft ? Qt::AlignRight | Qt::TextForceRightToLeft : Qt::AlignLeft | Qt::TextForceLeftToRight);
	painter.drawText(label.bounds.adjusted(4,2,-4,-2),flags,label.text); painter.restore();
}

} // namespace vibestudio
