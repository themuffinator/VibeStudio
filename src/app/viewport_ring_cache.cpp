#include "app/viewport_ring_cache.h"
#include <QPainter>
#include <QPen>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace vibestudio {
void ViewportRingCache::draw(QPainter& painter, const QVector<QPointF>& centers, QColor color, double width, double radius)
{
	if (!std::isfinite(width) || !std::isfinite(radius) || width <= 0 || radius <= 0 || centers.isEmpty()) { return; }
	painter.setPen(QPen(color,width,Qt::SolidLine)); painter.setBrush(Qt::NoBrush);
	const auto device = painter.deviceTransform();
	const double ratio = device.m11();
	const double reach = std::ceil((radius + width * 0.5) * ratio) + 2;
	const double size = reach * 2 + 2;
	const bool supported = device.isAffine() && device.m12() == 0 && device.m21() == 0 && ratio == device.m22()
		&& std::isfinite(ratio) && ratio > 0 && std::isfinite(size) && size > 0 && size <= 1024
		&& painter.testRenderHint(QPainter::Antialiasing)
		&& painter.compositionMode() == QPainter::CompositionMode_SourceOver
		&& size * size * 4 <= m_images.maxCost();
	if (!supported) {
		for (const auto center : centers) { painter.drawEllipse(center,radius,radius); }
		return;
	}
	if (ratio != m_ratio || width != m_width || radius != m_radius || color.rgba() != m_color) {
		m_images.clear(); m_ratio = ratio; m_width = width; m_radius = radius; m_color = color.rgba();
	}
	const auto inverse = device.inverted();
	const qsizetype bytes = qsizetype(size) * qsizetype(size) * 4;
	const qsizetype cost = std::max(bytes,(m_images.maxCost() + 511) / 512);
	const qsizetype batchSize = std::min<qsizetype>(512,m_images.maxCost() / cost);
	const auto phaseOf = [&](QPointF center) {
		const auto physical = device.map(center);
		return ViewportRingPhase {physical.x() - std::floor(physical.x()),physical.y() - std::floor(physical.y())};
	};
	for (qsizetype begin = 0; begin < centers.size(); begin += batchSize) {
		const qsizetype end = std::min(centers.size(),begin + batchSize);
		QVector<ViewportRingPhase> missing; QSet<ViewportRingPhase> seen;
		for (qsizetype i = begin; i < end; ++i) {
			const auto phase = phaseOf(centers.at(i));
			if (!std::isfinite(phase.x) || !std::isfinite(phase.y) || m_images.object(phase) || seen.contains(phase)) { continue; }
			seen.insert(phase); missing.append(phase);
		}
		if (!missing.isEmpty()) {
			// A bounded atlas shares one painter setup across cache misses. Its
			// payload is at most the cache budget, and no phase is quantized.
			QImage atlas(QSize(int(size),int(size) * int(missing.size())),QImage::Format_ARGB32_Premultiplied);
			if (!atlas.isNull()) {
				atlas.fill(Qt::transparent);
				{
					QPainter raster(&atlas); raster.setRenderHint(QPainter::Antialiasing);
					raster.setBrush(Qt::NoBrush); raster.setPen(QPen(color,width * ratio,Qt::SolidLine));
					for (qsizetype i = 0; i < missing.size(); ++i) {
						raster.drawEllipse(QPointF(reach + missing.at(i).x,i * size + reach + missing.at(i).y),radius * ratio,radius * ratio);
					}
				}
				for (qsizetype i = 0; i < missing.size(); ++i) {
					auto stamp = atlas.copy(QRect(0,int(i * size),int(size),int(size)));
					if (stamp.isNull()) { continue; }
					stamp.setDevicePixelRatio(ratio);
					// Charge at least 1/512 of the budget, also bounding metadata.
					m_images.insert(missing.at(i),new QImage(std::move(stamp)),cost);
				}
			}
		}
		for (qsizetype i = begin; i < end; ++i) {
			const auto center = centers.at(i), physical = device.map(center);
			if (!std::isfinite(physical.x()) || !std::isfinite(physical.y())) { continue; }
			const auto* image = m_images.object(phaseOf(center));
			if (!image) { painter.drawEllipse(center,radius,radius); continue; }
			const QPointF floor(std::floor(physical.x()),std::floor(physical.y()));
			painter.drawImage(inverse.map(floor - QPointF(reach,reach)),*image);
		}
	}
}
} // namespace vibestudio
