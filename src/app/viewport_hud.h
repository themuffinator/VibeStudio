#pragma once

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QRectF>
#include <QStringList>

class QPainter;

namespace vibestudio {

struct ViewportHudTag {
	QRectF bounds;
	QStringList lines;
};

struct ViewportHudLayout {
	ViewportHudTag leading;
	ViewportHudTag trailing;
};

struct ViewportLabelLayout { QRectF bounds; QString text; };
// Anchored labels stay inside the pane and avoid the HUD and primary marker.
// Offscreen anchors and panes without room for a line produce no label.
[[nodiscard]] ViewportLabelLayout layoutViewportLabel(const QRectF& viewport, QPointF anchor, const QFontMetricsF& metrics,
	const QString& text, Qt::LayoutDirection direction, const ViewportHudLayout& hud = {});
void paintViewportLabel(QPainter& painter, const ViewportLabelLayout& label, Qt::LayoutDirection direction,
	const QColor& text, const QColor& background, bool highContrast);
[[nodiscard]] QFont viewportHudFont(const QFont& baseFont);

// Put the view identity first in leadingParts. Narrow panes retain that identity
// and use at most two lines; map/model counts appear only when both tags fit.
[[nodiscard]] ViewportHudLayout layoutViewportHud(const QRectF& viewport, const QFontMetricsF& metrics,
	const QStringList& leadingParts, const QStringList& trailingLines, Qt::LayoutDirection direction);

void paintViewportHud(QPainter& painter, const QRectF& viewport, const QFont& font,
	const QStringList& leadingParts, const QStringList& trailingLines, Qt::LayoutDirection direction,
	const QColor& text, const QColor& background, bool highContrast);
void paintViewportHud(QPainter& painter, const QRectF& viewport, const QFont& font,
	const ViewportHudLayout& layout, Qt::LayoutDirection direction,
	const QColor& text, const QColor& background, bool highContrast);

} // namespace vibestudio
