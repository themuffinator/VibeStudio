#include "app/model_uv_render.h"
#include "app/wire_lines.h"
#include "app/model_uv_fill.h"

#include <QCoreApplication>
#include <QPainter>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>

namespace vibestudio
{
namespace
{
QSet<int> points(const ModelSurface &surface, const ModelSelection &selection)
{
	auto vertices = selection.vertices;
	for (auto edge : selection.edges)
	{
		vertices.insert(edge.first);
		vertices.insert(edge.second);
	}
	for (int face : selection.faces)
	{
		if (face < 0 || face >= surface.triangles.size())
		{
			continue;
		}
		const auto t = surface.triangles[face];
		vertices.insert(t.a);
		vertices.insert(t.b);
		vertices.insert(t.c);
	}
	return vertices;
}
bool clipLine(const QRectF &r, QPointF *a, QPointF *b)
{
	const QPointF delta = *b - *a;
	const double p[]{-delta.x(), delta.x(), -delta.y(), delta.y()},
		q[]{a->x() - r.left(), r.right() - a->x(), a->y() - r.top(), r.bottom() - a->y()};
	double first = 0, last = 1;
	for (int i = 0; i < 4; ++i)
	{
		if (std::abs(p[i]) < 1e-20)
		{
			if (q[i] < 0)
			{
				return false;
			}
			continue;
		}
		const double at = q[i] / p[i];
		if (p[i] < 0)
		{
			first = std::max(first, at);
		}
		else
		{
			last = std::min(last, at);
		}
		if (first > last)
		{
			return false;
		}
	}
	*b = *a + delta * last;
	*a += delta * first;
	return true;
}
} // namespace

QRectF modelUvSelectionBounds(const ModelSurface &surface, const ModelSelection &selection, bool *available)
{
	bool any = false;
	double minU = 0, maxU = 0, minV = 0, maxV = 0;
	for (int vertex : points(surface, selection))
	{
		if (vertex < 0 || vertex >= surface.texCoords.size())
		{
			continue;
		}
		const auto uv = surface.texCoords[vertex];
		if (!any)
		{
			minU = maxU = uv.u;
			minV = maxV = uv.v;
			any = true;
		}
		else
		{
			minU = std::min(minU, double(uv.u));
			maxU = std::max(maxU, double(uv.u));
			minV = std::min(minV, double(uv.v));
			maxV = std::max(maxV, double(uv.v));
		}
	}
	if (available)
	{
		*available = any;
	}
	return {QPointF(minU, minV), QPointF(maxU, maxV)};
}

bool renderModelUv(const ModelUvRenderRequest &r, ModelUvRenderResult *result, QString *error, const ModelWorkControl &control)
{
	if (error)
	{
		error->clear();
	}
	ModelWorkProgress work(control, ModelWorkPhase::Validating, error);
	if (!work.check())
	{
		return false;
	}
	if (!result || r.logicalSize.width() < 1 || r.logicalSize.height() < 1 || !std::isfinite(r.pixelRatio) || r.pixelRatio <= 0 ||
		!std::isfinite(r.moveOffset.u) || !std::isfinite(r.moveOffset.v) || std::abs(r.moveOffset.u) > 1000000 ||
		std::abs(r.moveOffset.v) > 1000000)
	{
		return false;
	}
	ModelUvRenderResult candidate;
	candidate.topology = r.topology;
	if (!candidate.topology)
	{
		auto topology = std::make_shared<ModelUvTopology>();
		if (!buildModelUvTopology(r.surface, topology.get(), error, control))
		{
			return false;
		}
		candidate.topology = std::move(topology);
	}
	const double aspect = r.texture.isNull() ? 1 : double(r.texture.width()) / r.texture.height();
	candidate.selectionBounds = modelUvSelectionBounds(r.surface, r.selection, &candidate.hasSelection);
	candidate.camera = r.camera;
	if (r.fit != ModelUvFit::None)
	{
		QRectF bounds(0, 0, 1, 1);
		bool selected = false;
		if (r.fit == ModelUvFit::Selection)
		{
			bounds = candidate.selectionBounds;
			selected = candidate.hasSelection;
		}
		if (!selected)
		{
			double loU = 0, hiU = 1, loV = 0, hiV = 1;
			for (const auto uv : r.surface.texCoords)
			{
				if (!work.step())
				{
					return false;
				}
				loU = std::min(loU, double(uv.u));
				hiU = std::max(hiU, double(uv.u));
				loV = std::min(loV, double(uv.v));
				hiV = std::max(hiV, double(uv.v));
			}
			bounds = {QPointF(loU, loV), QPointF(hiU, hiV)};
		}
		const double scale = std::min(std::max(1, r.logicalSize.width() - 64) / (std::max(0.01, bounds.width()) * aspect),
									  std::max(1, r.logicalSize.height() - 64) / std::max(0.01, bounds.height()));
		candidate.camera = QTransform(scale * aspect, 0, 0, scale, r.logicalSize.width() / 2.0 - bounds.center().x() * scale * aspect,
									  r.logicalSize.height() / 2.0 - bounds.center().y() * scale);
	}
	const auto &camera = candidate.camera;
	if (!camera.isInvertible() || !camera.isAffine() || camera.m12() != 0 || camera.m21() != 0 || !std::isfinite(camera.m11()) ||
		!std::isfinite(camera.m22()) || !std::isfinite(camera.dx()) || !std::isfinite(camera.dy()) || camera.m11() <= 0 ||
		camera.m22() <= 0 || camera.m11() > 1e12 || camera.m22() > 1e12 || std::abs(camera.dx()) > 1e15 || std::abs(camera.dy()) > 1e15)
	{
		return false;
	}
	constexpr double maxPixels = 4.0 * 1024 * 1024;
	// Either dimension is at least one physical pixel. Limit the long axis too,
	// otherwise an extreme aspect ratio defeats the area ceiling after rounding.
	const double ratio = std::min({r.pixelRatio, std::sqrt(maxPixels / (double(r.logicalSize.width()) * r.logicalSize.height())),
								   maxPixels / std::max(r.logicalSize.width(), r.logicalSize.height())});
	candidate.image = QImage(std::max(1, int(r.logicalSize.width() * ratio)), std::max(1, int(r.logicalSize.height() * ratio)),
							 QImage::Format_ARGB32_Premultiplied);
	if (candidate.image.isNull())
	{
		if (error)
		{
			*error = QCoreApplication::translate("VibeStudioModelUvView", "Unable to allocate the UV preview. Reduce its size.");
		}
		return false;
	}
	candidate.image.setDevicePixelRatio(ratio);
	candidate.image.fill(r.background);
	QPainter painter(&candidate.image);
	const QRectF area(QPointF(0, 0), QSizeF(r.logicalSize));
	if (!r.texture.isNull())
	{
		QBrush texture(r.texture);
		QTransform placement = candidate.camera;
		placement.scale(1.0 / r.texture.width(), 1.0 / r.texture.height());
		texture.setTransform(placement);
		painter.fillRect(area, texture);
	}
	painter.setRenderHint(QPainter::Antialiasing, true);
	QSet<int> explicitPoints = r.selection.vertices;
	for (auto edge : r.selection.edges)
	{
		explicitPoints.insert(edge.first);
		explicitPoints.insert(edge.second);
	}
	const auto point = [&](int face, int vertex) {
		const auto uv = r.surface.texCoords[vertex];
		const bool moved = r.moving && (r.selection.faces.contains(face) || explicitPoints.contains(vertex));
		return candidate.camera.map(QPointF(uv.u + (moved ? r.moveOffset.u : 0), uv.v + (moved ? r.moveOffset.v : 0)));
	};
	painter.setPen(Qt::NoPen);
	QColor fill = r.accent;
	fill.setAlpha(85);
	painter.setBrush(QBrush(fill, Qt::BDiagPattern));
	if (!r.selection.faces.isEmpty())
	{
		QPainterPath selection;
		if (!buildModelUvSelectionPath(r.surface, r.selection.faces, candidate.camera, r.moving ? r.moveOffset : ModelTexCoord{}, area,
									   &selection, error, control))
		{
			return false;
		}
		painter.drawPath(selection);
	}
	painter.end();
	// Complete indexed geometry, ordered by meaning rather than vertex index.
	// Contrast outlines cannot cover a later seam or selected edge. No nearby
	// edges are merged, snapped, or omitted to reduce overdraw.
	std::array<QVector<WireSegment>, 3> layers;
	const double margin = std::max(4.0, 2.5 + 1.0 / ratio);
	const auto lineArea = area.adjusted(-margin, -margin, margin, margin);
	for (const auto &edge : candidate.topology->edges)
	{
		if (!work.step())
		{
			return false;
		}
		const bool selected = r.selection.edges.contains(edge.vertices);
		const int layer = selected ? 2 : edge.seam ? 1 : 0;
		const auto line = [&](int face) {
			auto a = point(face, edge.vertices.first), b = point(face, edge.vertices.second);
			if (clipLine(lineArea, &a, &b))
			{
				layers[layer].append({a, b, layer > 0});
			}
		};
		line(edge.faces.first());
		// A face move previews the same corner isolation as its document edit.
		// Shared unselected faces keep their own edge instead of stretching.
		if (r.moving && edge.faces.size() > 1)
		{
			const bool moved = r.selection.faces.contains(edge.faces.first());
			for (int face : edge.faces)
			{
				if (r.selection.faces.contains(face) != moved)
				{
					line(face);
					break;
				}
			}
		}
	}
	for (int layer = 0; layer < 3; ++layer)
	{
		auto &strokes = layers[layer];
		int longStrokes = 0;
		for (const auto &stroke : strokes)
		{
			if (!work.step())
			{
				return false;
			}
			const auto delta = (stroke.b - stroke.a) * ratio;
			longStrokes += QPointF::dotProduct(delta, delta) > 64 * 64;
		}
		if (longStrokes >= 1024 && (layer != 2 || r.accent.alpha() == 255))
		{
			// Visit the whole layout before filling adjacent subpixel strokes.
			// This lets the shared line painter prove opaque tiles unchanged early.
			// Every indexed stroke is still submitted once, at its exact position.
			QVector<WireSegment> spread;
			spread.reserve(strokes.size());
			const auto capacity = std::bit_ceil(quint32(strokes.size()));
			quint32 index = 0;
			for (quint32 i = 0; i < capacity; ++i)
			{
				if (!work.step())
				{
					return false;
				}
				if (index < strokes.size())
				{
					spread.append(strokes[index]);
				}
				quint32 bit = capacity >> 1;
				while (bit && (index & bit))
				{
					index ^= bit;
					bit >>= 1;
				}
				index ^= bit;
			}
			strokes.swap(spread);
		}
		WireStyle style;
		style.pixelRatio = ratio;
		style.width = style.selectionWidth = layer == 0 ? 3 : 5;
		style.wire = style.selection = qRgb(0, 0, 0);
		style.dashLength = 0;
		const auto cancelled = [&] { return !work.check(); };
		if (!paintWireLines(&candidate.image, layers[layer], style, nullptr, cancelled))
		{
			return false;
		}
		style.width = 1;
		style.selectionWidth = 2.5;
		style.wire = qRgb(255, 255, 255);
		style.selection = layer == 1 ? qRgb(255, 115, 225) : r.accent.rgba();
		style.dashLength = layer == 1 ? 1 : 4;
		if (!paintWireLines(&candidate.image, layers[layer], style, nullptr, cancelled))
		{
			return false;
		}
	}
	if (r.showVertices)
	{
		painter.begin(&candidate.image);
		painter.setRenderHint(QPainter::Antialiasing, true);
		for (bool selected : {false, true})
		{
			for (int vertex = 0; vertex < r.surface.vertexCount; ++vertex)
			{
				if (!work.step())
				{
					return false;
				}
				if (r.selection.vertices.contains(vertex) != selected)
				{
					continue;
				}
				const auto p = point(-1, vertex);
				if (!area.contains(p))
				{
					continue;
				}
				painter.setPen(QPen(Qt::black, 2));
				painter.setBrush(selected ? r.accent : QColor(Qt::white));
				const double radius = selected ? 4 : 2.5;
				painter.drawRect(QRectF(p.x() - radius, p.y() - radius, radius * 2, radius * 2));
			}
		}
		painter.end();
	}
	if (!work.check())
	{
		return false;
	}
	*result = std::move(candidate);
	return true;
}
} // namespace vibestudio
