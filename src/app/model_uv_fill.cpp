#include "app/model_uv_fill.h"

#include <QHash>
#include <QPolygonF>
#include <algorithm>
#include <array>

namespace vibestudio
{
namespace
{
double cross(QPointF a, QPointF b)
{
	return a.x() * b.y() - a.y() * b.x();
}
bool clipped(QPolygonF &polygon, const QRectF &rect, ModelWorkProgress &work)
{
	for (int axis = 0; axis < 4 && !polygon.isEmpty(); ++axis)
	{
		const auto inside = [&](QPointF p) {
			return axis == 0   ? p.x() >= rect.left()
				   : axis == 1 ? p.x() <= rect.right()
				   : axis == 2 ? p.y() >= rect.top()
							   : p.y() <= rect.bottom();
		};
		QPolygonF next;
		auto a = polygon.last();
		bool aInside = inside(a);
		for (auto b : polygon)
		{
			if (!work.step())
			{
				return false;
			}
			const bool bInside = inside(b);
			if (aInside != bInside)
			{
				const double edge = axis == 0 ? rect.left() : axis == 1 ? rect.right() : axis == 2 ? rect.top() : rect.bottom();
				const double at = axis < 2 ? (edge - a.x()) / (b.x() - a.x()) : (edge - a.y()) / (b.y() - a.y());
				next << a + (b - a) * at;
			}
			if (bInside)
			{
				next << b;
			}
			a = b;
			aInside = bInside;
		}
		polygon = std::move(next);
	}
	return true;
}
} // namespace

bool buildModelUvSelectionPath(const ModelSurface &surface, const QSet<int> &faces, const QTransform &camera, const ModelTexCoord &offset,
							   const QRectF &clip, QPainterPath *result, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, error);
	if (!result || !work.check())
	{
		return false;
	}
	const auto uv = [&](int vertex) {
		const auto p = surface.texCoords[vertex];
		// Match the float coordinate addition used by the wire preview and the
		// document edit, including coarse representable steps at large UVs.
		return QPointF(p.u + offset.u, p.v + offset.v);
	};
	QHash<ModelEdge, int> balance;
	balance.reserve(std::min(qsizetype(16384), faces.size() * 3));
	auto orderedFaces = faces.values();
	std::sort(orderedFaces.begin(), orderedFaces.end());
	for (int face : orderedFaces)
	{
		if (!work.step())
		{
			return false;
		}
		if (face < 0 || face >= surface.triangles.size())
		{
			continue;
		}
		const auto t = surface.triangles[face];
		std::array<int, 3> corners{t.a, t.b, t.c};
		const double winding = cross(uv(t.b) - uv(t.a), uv(t.c) - uv(t.a));
		if (winding == 0)
		{
			continue;
		}
		if (winding < 0)
		{
			std::swap(corners[1], corners[2]);
		}
		for (int i = 0; i < 3; ++i)
		{
			const int a = corners[i], b = corners[(i + 1) % 3];
			const auto edge = modelEdge(a, b);
			const int delta = a < b ? 1 : -1;
			auto it = balance.find(edge);
			if (it == balance.end())
			{
				balance.insert(edge, delta);
			}
			else
			{
				it.value() += delta;
				if (it.value() == 0)
				{
					balance.erase(it);
				}
			}
		}
	}
	// Every triangle adds one incoming and one outgoing arc per corner. Removing
	// opposite arcs preserves that balance, so the remainder is closed contours.
	// Normalizing each triangle's winding first preserves overlaps and UV folds.
	QVector<QVector<int>> outgoing(surface.vertexCount);
	for (auto it = balance.cbegin(); it != balance.cend(); ++it)
	{
		if (!work.step())
		{
			return false;
		}
		const int a = it.value() > 0 ? it.key().first : it.key().second;
		const int b = it.value() > 0 ? it.key().second : it.key().first;
		for (int n = 0; n < std::abs(it.value()); ++n)
		{
			if (!work.step())
			{
				return false;
			}
			outgoing[a].append(b);
		}
	}
	QPainterPath candidate;
	candidate.setFillRule(Qt::WindingFill);
	for (int start = 0; start < outgoing.size(); ++start)
	{
		if (!work.step())
		{
			return false;
		}
		std::sort(outgoing[start].begin(), outgoing[start].end());
	}
	for (int start = 0; start < outgoing.size(); ++start)
	{
		if (!work.step())
		{
			return false;
		}
		while (!outgoing[start].isEmpty())
		{
			QPolygonF contour;
			int vertex = start;
			do
			{
				if (!work.step())
				{
					return false;
				}
				const auto p = uv(vertex);
				if (contour.size() >= 2 && cross(contour.last() - contour[contour.size() - 2], p - contour.last()) == 0 &&
					QPointF::dotProduct(contour.last() - contour[contour.size() - 2], p - contour.last()) >= 0)
				{
					// Exact collinearity only: this simplifies the fill contour, never
					// the independent indexed wires or interactive picking geometry.
					contour.last() = p;
				}
				else
				{
					contour.append(p);
				}
				vertex = outgoing[vertex].takeLast();
			} while (vertex != start);
			for (auto &p : contour)
			{
				if (!work.step())
				{
					return false;
				}
				p = camera.map(p);
			}
			if (!clipped(contour, clip, work))
			{
				return false;
			}
			candidate.addPolygon(contour);
			candidate.closeSubpath();
		}
	}
	if (!work.check())
	{
		return false;
	}
	*result = std::move(candidate);
	return true;
}
} // namespace vibestudio
