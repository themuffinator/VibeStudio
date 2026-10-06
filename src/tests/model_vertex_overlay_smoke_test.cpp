#include "app/model_vertex_overlay.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return value;
}
int referencePick(const ModelVertexProjection &projection, QPointF point, double tolerance, bool xray)
{
	int best = -1;
	double closest = tolerance * tolerance;
	for (int vertex = 0; vertex < projection.positions.size(); ++vertex)
	{
		if (!xray && !projection.visible.testBit(vertex))
		{
			continue;
		}
		const auto delta = projection.positions[vertex] - point;
		const double distance = delta.x() * delta.x() + delta.y() * delta.y();
		if (distance <= closest && (distance < closest || best < 0 || vertex < best))
		{
			closest = distance;
			best = vertex;
		}
	}
	return best;
}
} // namespace
int main()
{
	bool ok = true;
	ModelVertexProjection projection;
	projection.surface = 0;
	projection.logicalSize = {701, 503};
	std::mt19937 random(73923);
	const auto coordinate = [&] { return double(int(random() % 120000) - 20000) / 100; };
	for (int i = 0; i < 65530; ++i)
	{
		projection.positions.append({coordinate(), coordinate()});
	}
	const double nan = std::numeric_limits<double>::quiet_NaN();
	projection.positions << QPointF(50.25, 50.25) << QPointF(50.25, 50.25) << QPointF(nan, nan) << QPointF(1e300, -1e300)
						 << QPointF(-1e300, 1e300) << QPointF(701, 503);
	projection.visible.resize(projection.positions.size());
	for (int i = 0; i < projection.visible.size(); ++i)
	{
		projection.visible.setBit(i, i % 3 != 0);
	}
	ok &= expect(indexModelVertices(&projection), "maximum vertex projection is indexed");
	int indexed = 0;
	for (const auto &cell : projection.cells)
	{
		indexed += cell.size();
	}
	ok &= expect(indexed == 65535, "each finite vertex occurs exactly once, including distant X-ray vertices");
	for (int query = 0; query < 400; ++query)
	{
		const QPointF point = query < 100 ? projection.positions[query * 613] : QPointF(coordinate(), coordinate());
		const double tolerance = query % 4 == 0 ? 0 : query % 4 == 1 ? .125 : query % 4 == 2 ? 8 : 64;
		for (const bool xray : {false, true})
		{
			ok &= expect(pickModelVertex(projection, point, tolerance, xray) == referencePick(projection, point, tolerance, xray),
						 "indexed picking equals exhaustive continuous-coordinate search");
		}
	}
	ok &= expect(pickModelVertex(projection, {50.25, 50.25}, 0, true) == 65530, "coincident vertices choose the lowest index");
	ok &= expect(pickModelVertex(projection, {1e300, -1e300}, 0, true) == 65533,
				 "distant finite coordinates do not overflow cell addressing");
	ok &= expect(pickModelVertex(projection, {nan, 0}, 8, true) == -1 && pickModelVertex(projection, {}, -1, true) == -1 &&
					 pickModelVertex(projection, {}, 65, true) == -1 && pickModelVertex(projection, {}, nan, true) == -1,
				 "invalid queries are rejected");
	std::atomic_bool cancelled{true};
	ok &= expect(!indexModelVertices(&projection, &cancelled), "index preparation observes cancellation");

	ModelVertexProjection markers;
	markers.logicalSize = {100, 50};
	markers.positions = {{15, 20}, {40, 20}, {65, 20}, {90, 20}};
	markers.visible = QBitArray(4);
	markers.visible.setBit(0);
	markers.visible.setBit(2);
	const QColor accent(60, 160, 240);
	for (const double ratio : {1.0, 1.25, 1.5, 2.0})
	{
		QImage image;
		ok &= expect(
			renderModelVertexOverlay(markers, QSize(qRound(100 * ratio), qRound(50 * ratio)), ratio, {2, 3}, true, false, accent, &image),
			"marker image renders at integer and fractional scale");
		const auto pixel = [&](int x, int y) { return image.pixel(qRound(x * ratio), qRound(y * ratio)); };
		ok &= expect(pixel(15, 20) == qRgb(255, 255, 255) && pixel(13, 20) == qRgb(0, 0, 0),
					 "visible marker has a white centre and black outline");
		const int ringX = qRound(38 * ratio), ringY = qRound(20 * ratio);
		const bool ring = image.pixel(ringX - 1, ringY) == qRgb(255, 255, 255) || image.pixel(ringX, ringY) == qRgb(255, 255, 255) ||
						  image.pixel(ringX + 1, ringY) == qRgb(255, 255, 255);
		ok &= expect(qAlpha(pixel(40, 20)) == 0 && ring, "hidden X-ray marker has a hollow dotted outline");
		ok &= expect(pixel(65, 20) == accent.rgba() && pixel(62, 20) == qRgb(0, 0, 0) && pixel(90, 20) == accent.rgba(),
					 "selection preserves filled squares and black outlines, including X-ray selection");
		ok &= expect(renderModelVertexOverlay(markers, image.size(), ratio, {2, 3}, false, false, accent, &image) &&
						 qAlpha(pixel(40, 20)) == 0 && qAlpha(pixel(90, 20)) == 0,
					 "hidden markers disappear when X-ray is disabled");
		ok &= expect(renderModelVertexOverlay(markers, image.size(), ratio, {3}, false, true, accent, &image) &&
						 pixel(90, 20) == accent.rgba(),
					 "active transform retains its selected marker");
		const auto previous = image;
		ok &= expect(!renderModelVertexOverlay(markers, image.size(), ratio, {}, true, false, accent, &image, &cancelled) &&
						 image == previous,
					 "cancelled overlay never publishes a partial image");
		ok &= expect(!renderModelVertexOverlay(markers, {100000, 100000}, ratio, {}, false, false, accent, &image) && image == previous,
					 "oversized overlay preserves the image budget and previous output");
		ModelVertexProjection dense;
		dense.logicalSize = markers.logicalSize;
		QSet<int> all;
		for (int y = 0; y < 8; ++y)
		{
			for (int x = 0; x < 8; ++x)
			{
				all.insert(dense.positions.size());
				dense.positions.append(QPointF(30 + x, 20 + y));
			}
		}
		dense.visible = QBitArray(dense.positions.size(), true);
		ok &= expect(renderModelVertexOverlay(dense, image.size(), ratio, all, false, false, accent, &image),
					 "dense selected marker image renders");
		bool centresRetained = true;
		for (const auto point : dense.positions)
		{
			centresRetained &= image.pixel(qRound(point.x() * ratio), qRound(point.y() * ratio)) == accent.rgba();
		}
		ok &= expect(centresRetained, "overlapping selection outlines cannot cover another selected centre");
		const auto ordered = image;
		std::reverse(dense.positions.begin(), dense.positions.end());
		ok &= expect(renderModelVertexOverlay(dense, image.size(), ratio, all, false, false, accent, &image) && image == ordered,
					 "dense selection appearance is independent of vertex ordering");
	}
	std::cout << "800 indexed-pick oracle comparisons; marker visibility, shape, scaling, cancellation and bounds\n";
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
