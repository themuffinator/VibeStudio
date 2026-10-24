#include "app/level_component_trace.h"
#include "app/viewport_hud.h"
#include <QCoreApplication>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace vibestudio {
QVector<LevelComponentTraceLabel> paintLevelComponentTrace(QPainter& painter,const LevelComponentGizmoFrame& frame,
	LevelMapVec3 origin,LevelMapVec3 position,int constrainedAxis,bool pending,bool highContrast,const QPalette& palette)
{
	QVector<LevelComponentTraceLabel> labels;
	const auto finite = [](LevelMapVec3 p) { return p.valid && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); };
	if (!frame.project || !finite(origin) || !finite(position)) { return labels; }
	const auto visible = [](QPointF p) { return std::isfinite(p.x()) && std::isfinite(p.y()); };
	const QRectF viewport = QRectF(painter.window()).adjusted(6,6,-6,-6);
	if (viewport.width() < 30 || viewport.height() < 30) { return labels; }
	const QColor colours[]{QColor(230,100,85),QColor(70,190,110),QColor(90,145,245)};
	painter.save(); painter.setClipRect(viewport,Qt::IntersectClip);
	const QFontMetricsF metrics(painter.font());
	// Both view types reserve at most two HUD rows. Keep trace labels below
	// that area even when a long translation wraps the status during a drag.
	auto labelArea = viewport;
	const QFontMetricsF hudMetrics(viewportHudFont(painter.font()),painter.device());
	labelArea.setTop(std::max(viewport.top(),painter.window().top()+2*std::ceil(hudMetrics.height())+18));
	for (int axis = 0; axis < 3; ++axis) {
		auto end = origin; const double distance = axis == 0 ? position.x-origin.x : axis == 1 ? position.y-origin.y : position.z-origin.z;
		if (std::abs(distance) < 1e-8) { continue; }
		if (axis == 0) { end.x = position.x; } else if (axis == 1) { end.y = position.y; } else { end.z = position.z; }
		const auto a = frame.project(origin),b = frame.project(end); origin = end;
		if (!visible(a) || !visible(b)) { continue; }
		const bool strong = axis == constrainedAxis;
		painter.setPen(QPen(palette.color(QPalette::Window),strong ? 6 : 4)); painter.drawLine(a,b);
		painter.setPen(QPen(highContrast ? palette.color(QPalette::WindowText) : colours[axis],strong ? 3 : 2,pending ? Qt::DashLine : Qt::SolidLine)); painter.drawLine(a,b);
		if (labelArea.height() < metrics.height()+6) { continue; }
		const auto full = QCoreApplication::translate("LevelComponentTrace","%1: %2").arg(QString(QLatin1Char("XYZ"[axis]))).arg(distance,0,'g',6);
		const auto text = metrics.elidedText(full,Qt::ElideRight,int(viewport.width()-12));
		const QSizeF size(std::min(viewport.width(),metrics.horizontalAdvance(text)+10),std::min(viewport.height(),metrics.height()+6));
		QRectF box((a+b)*.5+QPointF(6,-size.height()-5),size);
		const auto clamp = [&] {
			box.moveLeft(std::clamp(box.left(),viewport.left(),viewport.right()-box.width()));
			box.moveTop(std::clamp(box.top(),labelArea.top(),labelArea.bottom()-box.height()));
		};
		clamp();
		for (int attempt = 0; attempt < 6; ++attempt) {
			bool overlaps = false; for (const auto& label : labels) { overlaps |= label.bounds.adjusted(-2,-2,2,2).intersects(box); }
			if (!overlaps) { break; }
			box.moveTop(attempt < 3 ? box.top()+size.height()+4 : labelArea.top()+(attempt-3)*(size.height()+4)); clamp();
		}
		if (std::any_of(labels.cbegin(),labels.cend(),[&](const auto& label) { return label.bounds.adjusted(-2,-2,2,2).intersects(box); })) { continue; }
		painter.setPen(QPen(palette.color(QPalette::WindowText),strong ? 2 : 1)); painter.setBrush(palette.color(QPalette::Window)); painter.drawRoundedRect(box,2,2);
		painter.setPen(palette.color(QPalette::WindowText)); painter.drawText(box,Qt::AlignCenter|Qt::TextForceLeftToRight,text);
		labels.append({axis,box,full});
	}
	painter.restore(); return labels;
}
} // namespace vibestudio
