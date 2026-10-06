#include "app/model_rasterizer.h"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

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

ModelRasterTriangle face(int source, QRgb color, std::array<double, 3> depth)
{
	ModelRasterTriangle result;
	result.source = source;
	result.color = color;
	result.vertices = {{{{8, 8}, depth[0], 1, {0, 0}}, {{120, 8}, depth[1], 1, {1, 0}}, {{8, 120}, depth[2], 1, {0, 1}}}};
	return result;
}

QImage texture(QRgb color)
{
	QImage image(1, 1, QImage::Format_ARGB32_Premultiplied);
	image.setPixel(0, 0, qPremultiply(color));
	return image;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true;
	ModelRasterStyle style;
	ModelRasterFrame frame, reversed;
	auto red = face(0, qRgb(255, 0, 0), {0, 10, 0});
	auto blue = face(1, qRgb(0, 0, 255), {4, 4, 4});
	ok &= expect(renderModelRaster({128, 128}, {red, blue}, style, &frame), "render crossing opaque triangles");
	ok &= expect(frame.image.pixel(20, 20) == qRgb(0, 0, 255) && frame.image.pixel(90, 20) == qRgb(255, 0, 0),
				 "intersection changes visibility inside one triangle, independently of average depth");
	renderModelRaster({128, 128}, {blue, red}, style, &reversed);
	ok &= expect(frame.image == reversed.image && frame.source == reversed.source, "opaque rendering is independent of submission order");
	ok &= expect(pickModelRaster({20.5, 20.5}, {red, blue}) == 1 && pickModelRaster({90.5, 20.5}, {red, blue}) == 0,
				 "picking agrees with frontmost fragments on both sides of an intersection");
	std::swap(red.vertices[1], red.vertices[2]);
	renderModelRaster({128, 128}, {blue, red}, style, &reversed);
	ok &= expect(frame.image == reversed.image, "either triangle winding covers the same pixels");
	std::swap(red.vertices[1], red.vertices[2]);

	const auto halfRed = texture(qRgba(255, 0, 0, 128));
	red.texture = &halfRed;
	red.textureHasAlpha = true;
	renderModelRaster({128, 128}, {red, blue}, style, &frame);
	ok &= expect(frame.image.pixel(20, 20) == qRgb(0, 0, 255) && frame.image.pixel(90, 20) == qRgb(128, 0, 127),
				 "translucent crossing face composites over the nearer opaque depth only where visible");
	const auto halfGreen = texture(qRgba(0, 255, 0, 128));
	auto green = face(2, qRgb(0, 255, 0), {5, 5, 5});
	green.texture = &halfGreen;
	green.textureHasAlpha = true;
	blue.vertices[0].depth = blue.vertices[1].depth = blue.vertices[2].depth = -1;
	renderModelRaster({128, 128}, {red, blue, green}, style, &frame);
	renderModelRaster({128, 128}, {green, blue, red}, style, &reversed);
	ok &=
		expect(frame.image == reversed.image && frame.source == reversed.source, "crossing transparent faces composite in per-pixel order");
	ok &= expect(frame.image.pixel(20, 20) == qRgb(64, 128, 63) && frame.image.pixel(90, 20) == qRgb(128, 64, 63),
				 "transparent layers reverse their blend order at the geometric intersection");
	const auto empty = texture(qRgba(255, 0, 0, 0));
	red.texture = &empty;
	renderModelRaster({128, 128}, {red, blue}, style, &frame);
	ok &= expect(frame.image.pixel(90, 20) == qRgb(0, 0, 255) && pickModelRaster({90.5, 20.5}, {red, blue}) == 1,
				 "fully transparent texels do not hide or pick through the rear mesh");
	ok &= expect(modelTextureHasAlpha(empty) && modelTextureHasAlpha(halfRed) && !modelTextureHasAlpha(texture(qRgb(12, 30, 90))),
				 "alpha classification distinguishes opaque and transparent images");

	QImage gradient(256, 1, QImage::Format_ARGB32_Premultiplied);
	for (int x = 0; x < 256; ++x)
	{
		gradient.setPixel(x, 0, qRgb(x, 255 - x, 0));
	}
	auto perspective = face(3, qRgb(255, 255, 255), {1, 0.25, 1});
	perspective.texture = &gradient;
	perspective.vertices[1].reciprocalW = 0.25;
	renderModelRaster({128, 128}, {perspective}, style, &frame);
	const double barycentric = (64.5 - 8) / 112;
	const double correctU = barycentric * 0.25 / (1 - barycentric * 0.75);
	const int expectedRed = int(std::lround(correctU * 256 - 0.5));
	ok &= expect(std::abs(qRed(frame.image.pixel(64, 24)) - expectedRed) <= 1 && expectedRed < 60,
				 "perspective mapping interpolates u/w and 1/w before dividing");
	for (auto &vertex : perspective.vertices)
	{
		vertex.uv += QPointF(-2, 3);
	}
	renderModelRaster({128, 128}, {perspective}, style, &reversed);
	ok &= expect(frame.image == reversed.image, "negative and repeated UVs preserve bilinear sampling");
	for (auto &vertex : perspective.vertices)
	{
		vertex.uv = QPointF(0.5, 0.5);
	}
	renderModelRaster({128, 128}, {perspective}, style, &frame);
	ok &=
		expect(qRed(frame.image.pixel(64, 24)) == 128, "zero-area UV triangles sample their constant texel instead of losing the texture");

	// A translucent quad has no doubled alpha or unfilled crack on its diagonal.
	auto a = face(0, qRgb(255, 0, 0), {1, 1, 1});
	auto b = a;
	b.source = 1;
	a.texture = b.texture = &halfRed;
	a.textureHasAlpha = b.textureHasAlpha = true;
	b.vertices[0].screen = {120, 120};
	b.vertices[1].screen = {8, 120};
	b.vertices[2].screen = {120, 8};
	renderModelRaster({128, 128}, {a, b}, style, &frame);
	int incorrect = 0;
	for (int y = 8; y < 120; ++y)
	{
		for (int x = 8; x < 120; ++x)
		{
			incorrect += qAlpha(frame.image.pixel(x, y)) != 128;
		}
	}
	ok &= expect(incorrect == 0, "half-open coverage gives a watertight, singly composited shared edge");
	{
		// Fractional, translated tiny faces expose cancellation in independently
		// computed reversed edge equations. Integer-coordinate quads miss it.
		QVector<ModelRasterTriangle> grid;
		constexpr int side = 128;
		constexpr double step = 3.7316257491;
		const QPointF origin(13.371298753, 512 - 13.371298753 - side * step);
		for (int y = 0; y < side; ++y)
		{
			for (int x = 0; x < side; ++x)
			{
				a = face(grid.size(), qRgb(255, 0, 0), {1, 1, 1});
				b = a;
				b.source += 1;
				a.texture = b.texture = &halfRed;
				a.textureHasAlpha = b.textureHasAlpha = true;
				a.vertices[0].screen = origin + QPointF(x * step, y * step);
				a.vertices[1].screen = origin + QPointF((x + 1) * step, y * step);
				a.vertices[2].screen = origin + QPointF(x * step, (y + 1) * step);
				b.vertices[0] = a.vertices[1];
				b.vertices[2] = a.vertices[2];
				b.vertices[1].screen = origin + QPointF((x + 1) * step, (y + 1) * step);
				grid << a << b;
			}
		}
		ok &= expect(renderModelRaster({512, 512}, grid, {}, &frame), "render dense fractional translucent grid");
		int defects = 0;
		for (int y = int(std::ceil(origin.y())); y < int(origin.y() + side * step) - 1; ++y)
		{
			for (int x = int(std::ceil(origin.x())); x < int(origin.x() + side * step) - 1; ++x)
			{
				defects += qAlpha(frame.image.pixel(x, y)) != 128;
			}
		}
		ok &= expect(defects == 0, "fractional shared edges have neither holes nor double-blended pixels");
	}
	blue.vertices[0].depth = blue.vertices[1].depth = blue.vertices[2].depth = 4;
	red.texture = nullptr;
	red.highlighted = true;
	style.showEdges = true;
	renderModelRaster({128, 128}, {red, blue}, style, &frame);
	ok &= expect(frame.image.pixel(20, 20) == qRgb(0, 0, 255), "hidden selection hatches and edges obey mesh depth");
	style.showEdges = false;
	red.highlighted = false;
	red.selectedEdges[2] = true; // Top edge opposite vertex 2.
	renderModelRaster({128, 128}, {red, blue}, style, &frame);
	ok &= expect(frame.image.pixel(22, 9) == qRgb(0, 0, 255) && frame.image.pixel(82, 9) == style.selection &&
					 frame.image.pixel(82, 30) == qRgb(255, 0, 0),
				 "only the selected edge is highlighted and it obeys foreground depth");
	std::swap(red.vertices[1], red.vertices[2]);
	std::swap(red.selectedEdges[1], red.selectedEdges[2]);
	renderModelRaster({128, 128}, {red, blue}, style, &reversed);
	ok &= expect(frame.image == reversed.image, "selected-edge masks follow reversed winding");
	auto invalid = blue;
	invalid.vertices[0].screen.setX(std::numeric_limits<double>::quiet_NaN());
	ok &= expect(renderModelRaster({128, 128}, {invalid, blue}, style, &frame) && frame.image.pixel(20, 20) == qRgb(0, 0, 255),
				 "non-finite geometry does not poison the depth buffer");
	ok &= expect(!renderModelRaster({100000, 100000}, {blue}, style, &frame) && frame.image.isNull() && frame.depth.isEmpty(),
				 "oversized render targets fail without allocating or retaining stale results");
	ok &= expect(pickModelRaster({std::numeric_limits<double>::infinity(), 0}, {blue}) == -1, "non-finite hit requests are rejected");

	// Report reproducible throughput without imposing a machine-specific latency
	// threshold on sanitizer/debug jobs. The Meson timeout is the hard watchdog.
	QVector<ModelRasterTriangle> benchmark;
	for (int y = 0; y < 32; ++y)
	{
		for (int x = 0; x < 32; ++x)
		{
			a = face(int(benchmark.size()), qRgb(100, 150, 200), {1, 1, 1});
			b = a;
			b.source += 1;
			a.texture = b.texture = &gradient;
			a.vertices[0].screen = {double(x * 32), double(y * 32)};
			a.vertices[1].screen = {double((x + 1) * 32), double(y * 32)};
			a.vertices[2].screen = {double(x * 32), double((y + 1) * 32)};
			b.vertices[0] = a.vertices[1];
			b.vertices[1].screen = {double((x + 1) * 32), double((y + 1) * 32)};
			b.vertices[2] = a.vertices[2];
			benchmark << a << b;
		}
	}
	QElapsedTimer clock;
	clock.start();
	ok &= expect(renderModelRaster({1024, 1024}, benchmark, style, &frame), "render 2048 textured triangles at 1024 square");
	std::cout << "Raster benchmark: 2048 opaque textured triangles, 1024x1024, " << clock.elapsed() << " ms\n";
	// Compare indexed continuous picks with the original exact sampler at cell
	// boundaries, across shared edges, and through transparent/perspective faces.
	QVector<ModelRasterTriangle> pickScene = benchmark;
	pickScene << red << blue << green << perspective << invalid;
	auto huge = face(7000, qRgb(20, 30, 40), {-100, -100, -100});
	huge.vertices[0].screen = {-1e100, -1e100};
	huge.vertices[1].screen = {1e100, -1e100};
	huge.vertices[2].screen = {0, 1e100};
	pickScene << huge;
	ModelRasterPickIndex index;
	ok &= expect(buildModelRasterPickIndex({1024, 1024}, pickScene, &index) && !index.broad.isEmpty(),
				 "build bounded picking index with shared large-face candidates");
	bool samePicks = true;
	for (int y = 0; y < 1024; y += 31)
	{
		for (int x = 0; x < 1024; x += 32)
		{
			for (double offset : {0.0, 0.25})
			{
				const QPointF point(x + offset, y + offset);
				samePicks &= pickModelRaster(point, pickScene) == pickModelRaster(point, pickScene, index);
			}
		}
	}
	ok &= expect(samePicks, "indexed picking preserves coverage, shared-edge ownership, depth, perspective UVs and alpha");
	ok &= expect(pickModelRaster({-0.25, 20}, pickScene, index) == -1 && pickModelRaster({1024, 20}, pickScene, index) == -1 &&
					 pickModelRaster({std::numeric_limits<double>::quiet_NaN(), 20}, pickScene, index) == -1,
				 "indexed picking rejects coordinates outside its finite viewport");
	std::atomic_bool cancelled{true};
	const auto before = index.cells;
	ok &= expect(!buildModelRasterPickIndex({1024, 1024}, pickScene, &index, &cancelled) && index.cells == before,
				 "cancelled index preparation does not publish partial candidates");
	{
		auto budgetFace = face(0, qRgb(100, 150, 200), {1, 1, 1});
		budgetFace.vertices[0].screen = {0, 0};
		budgetFace.vertices[1].screen = {96, 0};
		budgetFace.vertices[2].screen = {0, 96};
		QVector<ModelRasterTriangle> crowded(131073, budgetFace);
		crowded.last().source = 9000;
		for (auto &vertex : crowded.last().vertices)
		{
			vertex.depth = 2;
		}
		ModelRasterPickIndex bounded;
		ok &= expect(buildModelRasterPickIndex({1024, 1024}, crowded, &bounded), "prepare a picking index at its reference budget");
		qsizetype references = 0;
		for (const auto &cell : bounded.cells)
		{
			references += cell.size();
		}
		ok &=
			expect(references == 2 * 1024 * 1024 && bounded.broad.size() == 1 && pickModelRaster({16.25, 16.25}, crowded, bounded) == 9000,
				   "reference-budget overflow uses exact broad candidates without dropping selectable faces");
	}
	ok &= expect(!renderModelRaster({1024, 1024}, benchmark, style, &frame, &cancelled) && frame.image.isNull(),
				 "cancelled rendering drops stale output before starting allocation or fragment work");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
