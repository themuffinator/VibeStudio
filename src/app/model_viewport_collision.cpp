#include "app/model_viewport.h"
#include "core/model_collision.h"
#include <QPainter>
#include <cmath>

namespace vibestudio
{
void ModelViewport::setShowCollision(bool show)
{
	if (m_showCollision == show)
	{
		return;
	}
	finishEditTransform(false);
	m_showCollision = show;
	invalidateRaster();
	setAccessibleDescription(accessibleSummary());
	update();
}
void ModelViewport::setEditCollision(const QString &name)
{
	const auto valid = findModelCollisionBox(m_mesh, name) ? name : QString();
	if (m_editCollision == valid)
	{
		return;
	}
	finishEditTransform(false);
	m_editCollision = valid;
	if (!valid.isEmpty())
	{
		m_editTag.clear();
		m_editVertices.clear();
		m_editSurfaces.clear();
		invalidateVertexOverlay();
	}
	setAccessibleDescription(accessibleSummary());
	update();
}
void ModelViewport::setCollisionPicking(bool enabled)
{
	if (m_collisionPicking == enabled)
	{
		return;
	}
	finishEditTransform(false);
	m_collisionPicking = enabled;
	if (enabled)
	{
		setShowCollision(true);
	}
	setAccessibleDescription(accessibleSummary());
	update();
}
bool ModelViewport::collisionPose(const QString &name, ModelCollisionBox *result) const
{
	const auto box = findModelCollisionBox(m_mesh, name);
	if (!box || !result)
	{
		return false;
	}
	ModelCollisionBox pose;
	if (!sampleModelCollisionBox(*box, m_frame, m_blendFrame, m_frameBlend, &pose))
		return false;
	if (m_editMoveActive && name == m_editCollision)
	{
		return transformModelCollisionBox(pose, m_editTransform, result);
	}
	*result = std::move(pose);
	return true;
}
QString ModelViewport::collisionAt(const QPointF &point, double tolerance) const
{
	if (!m_collisionPicking || !m_showCollision || !std::isfinite(point.x()) || !std::isfinite(point.y()) || !std::isfinite(tolerance) ||
		tolerance <= 0 || tolerance > 64 || !QRectF(rect()).contains(point) || isRendering() || m_editMoveActive)
	{
		return {};
	}
	QString result;
	double nearest = tolerance * tolerance;
	for (const auto &box : m_rasterCollision)
	{
		for (const auto &line : collisionScreenEdges(box.name))
		{
			const auto delta = line.p2() - line.p1();
			const auto length = QPointF::dotProduct(delta, delta);
			const auto t = length > 1e-12 ? std::clamp(QPointF::dotProduct(point - line.p1(), delta) / length, 0.0, 1.0) : 0;
			const auto offset = point - (line.p1() + delta * t);
			const auto distance = QPointF::dotProduct(offset, offset);
			if (distance < nearest || (distance == nearest && box.name == m_editCollision))
			{
				nearest = distance;
				result = box.name;
			}
		}
	}
	return result;
}
QVector<ModelViewport::CollisionOverlay> ModelViewport::projectCollisionOverlays() const
{
	QVector<CollisionOverlay> result;
	if (!m_showCollision)
	{
		return result;
	}
	for (const auto &box : m_mesh.collisionBoxes)
	{
		if (result.size() >= modelCollisionBoxLimit)
		{
			break;
		}
		CollisionOverlay projected;
		projected.name = box.name;
		ModelCollisionBox pose;
		if (!collisionPose(box.name, &pose))
		{
			continue;
		}
		const auto corners = modelCollisionCorners(pose);
		for (int i = 0; i < 8; ++i)
		{
			for (int axis = 0; axis < 3; ++axis)
			{
				const int j = i ^ (1 << axis);
				QPointF a, b;
				if (i < j && projectSegment(corners[i], corners[j], &a, &b))
				{
					projected.edges << QLineF(a, b);
				}
			}
		}
		result << projected;
	}
	return result;
}
QVector<QLineF> ModelViewport::collisionScreenEdges(const QString &name) const
{
	if (!m_showCollision || m_raster.image.isNull())
	{
		return {};
	}
	for (const auto &box : m_rasterCollision)
	{
		if (box.name != name)
		{
			continue;
		}
		auto lines = box.edges;
		const double sx = double(width()) / std::max(1, m_rasterLogicalSize.width());
		const double sy = double(height()) / std::max(1, m_rasterLogicalSize.height());
		for (auto &line : lines)
		{
			line = {{line.x1() * sx, line.y1() * sy}, {line.x2() * sx, line.y2() * sy}};
		}
		return lines;
	}
	return {};
}
void ModelViewport::paintCollisionOverlays(QPainter &painter) const
{
	if (!m_showCollision)
	{
		return;
	}
	painter.save();
	painter.setClipRect(rect());
	painter.setRenderHint(QPainter::Antialiasing, true);
	for (const auto &box : m_rasterCollision)
	{
		const auto lines = collisionScreenEdges(box.name);
		const bool selected = box.name == m_editCollision;
		const auto style = selected ? Qt::SolidLine : Qt::DashLine;
		const auto colour = selected ? QColor(255, 225, 100) : QColor(100, 245, 230);
		for (int pass = 0; pass < 2; ++pass)
		{
			painter.setPen(QPen(pass == 0 ? QColor(Qt::black) : colour, pass == 0 ? (selected ? 5 : 4) : (selected ? 3 : 1.5), style));
			painter.drawLines(lines);
		}
	}
	painter.restore();
}
} // namespace vibestudio
