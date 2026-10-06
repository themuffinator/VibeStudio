#include "core/texture_document.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; } return value; }

// Deliberately slow pixel-stamp oracle, independent of the production span union.
QImage referenceStroke(QSize size, QPoint from, QPoint to, const TextureBrush& brush, QRect clip)
{
	QImage result(size, QImage::Format_ARGB32); result.fill(Qt::transparent);
	int x = from.x(), y = from.y();
	const int dx = std::abs(to.x() - x), dy = std::abs(to.y() - y);
	int remainder = dx - dy;
	while (true) {
		for (int row = 0; row < brush.width; ++row) {
			for (int column = 0; column < brush.width; ++column) {
				const int localX = 2 * column - (brush.width - 1), localY = 2 * row - (brush.width - 1);
				if (brush.shape == TextureBrushShape::Round && localX * localX + localY * localY > brush.width * brush.width) { continue; }
				QPoint pixel(x + column - (brush.width - 1) / 2, y + row - (brush.width - 1) / 2);
				if (brush.wrap) { pixel = {(pixel.x() % size.width() + size.width()) % size.width(), (pixel.y() % size.height() + size.height()) % size.height()}; }
				if (clip.contains(pixel)) { result.setPixelColor(pixel, Qt::red); }
			}
		}
		if (QPoint(x, y) == to) { break; }
		const int twice = 2 * remainder;
		if (twice > -dy || twice == -dy) { remainder -= dy; x += from.x() < to.x() ? 1 : -1; }
		if (twice < dx || twice == dx) { remainder += dx; y += from.y() < to.y() ? 1 : -1; }
	}
	return result;
}
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true; QString error;
	const QSize size(17, 13);
	for (bool wrap : {false, true}) {
		for (auto shape : {TextureBrushShape::Square, TextureBrushShape::Round}) {
			for (int width : {1, 2, 5, 9, 16, 32}) {
				for (bool selected : {false, true}) {
					for (int seed = 0; seed < 36; ++seed) {
						const auto point = [&](int value) { return wrap ? QPoint(value % 51 - 17, value * 7 % 39 - 13) : QPoint(value % 17, value * 7 % 13); };
						const QPoint from = point(seed * 13 + 3), to = point(seed * 29 + 11);
						TextureDocument document; document.create(size, Qt::transparent); document.markSaved();
						const QRect clip = selected ? QRect(2, 1, 10, 9) : QRect(QPoint(), size);
						if (selected) { document.setSelection(clip); }
						const TextureBrush brush{width, shape, TexturePaintMode::Replace, wrap};
						ok &= expect(document.paintStroke({from, to}, Qt::red, brush, &error) && document.image() == referenceStroke(size, from, to, brush, clip),
							"optimized square/round spans match pixel stamps across octants, selection, and repeated tiles");
					}
				}
			}
		}
	}
	TextureDocument document;
	document.create({8, 8}, Qt::blue); document.markSaved();
	TextureBrush blend{3, TextureBrushShape::Square, TexturePaintMode::SourceOver};
	ok &= expect(document.paintStroke({{1, 1}, {5, 1}, {1, 1}, {5, 1}}, QColor(255, 0, 0, 128), blend, &error), "blend-over stroke applies");
	for (int x = 0; x <= 6; ++x) { ok &= expect(document.image().pixel( x, 1) == qRgba(128, 0, 127, 255), "overlapping stamps and retraced segments apply stroke alpha exactly once"); }
	ok &= expect(document.undo() && !document.isDirty() && document.redo(), "one gesture remains one saved-state-aware undo unit");
	document.paintStroke({{1, 1}}, QColor(255, 0, 0, 128), blend);
	ok &= expect(document.image().pixel(1, 1) == qRgba(192, 0, 63, 255), "a separate gesture builds up alpha");
	document.create({1, 1}, QColor(0, 0, 255, 128));
	document.paintStroke({{0, 0}}, QColor(255, 0, 0, 128), blend);
	ok &= expect(document.image().pixel(0, 0) == qRgba(170, 0, 85, 192), "straight RGBA blend handles partial destination alpha with integer rounding");
	document.create({1, 1}, QColor(10, 20, 30, 0)); document.markSaved();
	document.paintStroke({{0, 0}}, QColor(220, 100, 80, 0), blend);
	ok &= expect(document.image().pixel(0, 0) == qRgba(10, 20, 30, 0) && !document.isDirty(), "transparent blend is a true no-op preserving hidden RGB");
	document.paintStroke({{0, 0}}, QColor(220, 100, 80, 19), blend);
	ok &= expect(document.image().pixel(0, 0) == qRgba(220, 100, 80, 19), "blend over a transparent destination retains straight color");
	document.create({8, 8}, Qt::transparent);
	TextureBrush solid{1};
	ok &= expect(document.drawShape(TextureShape::Rectangle, {6, 5}, {1, 1}, QColor(20, 40, 60, 90), solid, false, &error), "reversed rectangle endpoints normalize");
	for (int y = 0; y < 8; ++y) { for (int x = 0; x < 8; ++x) {
		const bool border = x >= 1 && x <= 6 && y >= 1 && y <= 5 && (x == 1 || x == 6 || y == 1 || y == 5);
		ok &= expect(document.image().pixel(x, y) == (border ? qRgba(20, 40, 60, 90) : qRgba(0, 0, 0, 0)), "rectangle outline lies inside inclusive endpoint bounds");
	} }
	ok &= expect(document.undoLabel() == QStringLiteral("Draw rectangle") && document.undo(), "shapes carry meaningful history labels");
	solid.width = 2;
	document.drawShape(TextureShape::Ellipse, {0, 0}, {7, 7}, Qt::red, solid, false);
	ok &= expect(document.image().pixelColor(0, 0).alpha() == 0 && document.image().pixelColor(3, 0) == QColor(Qt::red) && document.image().pixelColor(3, 3).alpha() == 0,
		"ellipse outline has rounded corners and an unpainted center");
	for (int y = 0; y < 8; ++y) { for (int x = 0; x < 8; ++x) {
		ok &= expect(document.image().pixel(x, y) == document.image().pixel(7 - x, y) && document.image().pixel(x, y) == document.image().pixel(x, 7 - y), "ellipse pixels are symmetric on both axes");
	} }
	document.drawShape(TextureShape::Ellipse, {0, 0}, {7, 7}, Qt::green, solid, true);
	ok &= expect(document.image().pixelColor(3, 3) == QColor(Qt::green), "filled ellipse covers its interior");
	for (auto shape : {TextureShape::Line, TextureShape::Rectangle, TextureShape::Ellipse}) {
		document.create({3, 3}, Qt::transparent);
		ok &= expect(document.drawShape(shape, {1, 1}, {1, 1}, Qt::red, TextureBrush{}, false) && document.image().pixelColor(1, 1) == QColor(Qt::red), "single-pixel shapes are defined");
		document.create({8, 8}, Qt::blue); document.markSaved(); document.setSelection({6, 0, 2, 2});
		TextureBrush wrapped{1, TextureBrushShape::Square, TexturePaintMode::Replace, true};
		ok &= expect(document.drawShape(shape, {-2, 0}, {1, 1}, Qt::red, wrapped, true), "shapes accept unwrapped repeat endpoints");
		ok &= expect(document.image().pixelColor(0, 0) == QColor(Qt::blue), "selection clips shape pixels after wrapping");
	}
	QImage ramp(5, 1, QImage::Format_ARGB32);
	for (int x = 0; x < 5; ++x) { ramp.setPixelColor(x, 0, QColor(10 + x * 10, 30, 40, 100)); }
	document.reset(ramp);
	ok &= expect(document.floodFill({0, 0}, ramp.pixelColor(0, 0), 15, false, &error) && document.image().pixelColor(1, 0) == ramp.pixelColor(0, 0) && document.image().pixel(2, 0) == ramp.pixel(2, 0),
		"tolerance fill reaches matching neighbors even when replacement equals seed and does not drift along a gradient");
	QImage alpha(4, 1, QImage::Format_ARGB32);
	alpha.setPixel(0, 0, qRgba(10, 20, 30, 0)); alpha.setPixel(1, 0, qRgba(10, 20, 30, 20));
	alpha.setPixel(2, 0, qRgba(10, 20, 30, 21)); alpha.setPixel(3, 0, qRgba(40, 20, 30, 0));
	document.reset(alpha); document.floodFill({0, 0}, Qt::red, 20, false);
	ok &= expect(document.image().pixelColor(1, 0) == QColor(Qt::red) && document.image().pixel(2, 0) == alpha.pixel(2, 0), "tolerance includes alpha and accepts exactly the boundary difference");
	document.reset(alpha); document.floodFill({3, 0}, Qt::green, 0, false);
	ok &= expect(document.image().pixel(0, 0) == alpha.pixel(0, 0), "hidden RGB is compared explicitly even when alpha is zero");
	QImage separated(5, 3, QImage::Format_ARGB32); separated.fill(Qt::blue);
	for (int y = 0; y < 3; ++y) { separated.setPixelColor(0, y, Qt::red); separated.setPixelColor(4, y, Qt::red); }
	document.reset(separated); document.floodFill({0, 0}, Qt::green, 0, false);
	ok &= expect(document.image().pixelColor(4, 2) == QColor(Qt::red), "nonwrapped fill does not cross the seam");
	document.reset(separated); document.floodFill({-1, 0}, Qt::green, 0, true);
	ok &= expect(document.image().pixelColor(0, 2) == QColor(Qt::green) && document.image().pixelColor(4, 2) == QColor(Qt::green) && document.image().pixelColor(2, 2) == QColor(Qt::blue), "wrapped four-connected fill joins regions across canvas edges");
	document.reset(separated); document.setSelection({0, 0, 2, 2}); document.floodFill({0, 0}, Qt::green, 255, true);
	ok &= expect(document.image().pixelColor(1, 1) == QColor(Qt::green) && document.image().pixelColor(4, 0) == QColor(Qt::red), "even full tolerance and wrapping cannot leave the selection");
	QImage indexed(8, 4, QImage::Format_Indexed8);
	QList<QRgb> colors; for (int i = 0; i < 256; ++i) { colors << qRgba(i / 2, i / 2, i / 2, i % 3 == 0 ? 0 : 255); }
	indexed.setColorTable(colors);
	for (int y = 0; y < 4; ++y) { for (int x = 0; x < 8; ++x) { indexed.setPixel(x, y, y * 8 + x); } }
	document.reset(indexed); document.paintStroke({{0, 0}}, QColor(200, 100, 50, 0), blend);
	ok &= expect(!document.isDirty() && document.image().format() == QImage::Format_Indexed8 && document.image() == indexed, "transparent blending preserves untouched indexed data and save state");
	document.reset(indexed); document.setSelection({2, 1, 3, 2});
	ok &= expect(document.offsetPixels({4, -3}, &error) && document.image().format() == QImage::Format_Indexed8 && document.image().colorTable() == colors, "offset preserves indexed representation, duplicate colors, and alpha entries");
	for (int y = 0; y < 4; ++y) { for (int x = 0; x < 8; ++x) {
		const QPoint source = document.selection().contains(x, y) ? QPoint(2 + (x - 2 + 2) % 3, 1 + (y - 1 + 1) % 2) : QPoint(x, y);
		ok &= expect(document.image().pixelIndex(x, y) == indexed.pixelIndex(source), "negative offsets wrap strictly within selection without touching outside pixels");
	} }
	ok &= expect(document.undo() && document.image() == indexed && !document.isDirty() && document.selection() == QRect(2, 1, 3, 2), "offset undo restores exact indices and selection");
	ok &= expect(document.offsetPixels({300, -200}) && !document.isDirty(), "offset by whole selection cycles does not create an edit");
	document.create({512, 512}, Qt::blue); document.markSaved();
	const auto revision = document.revision(); const QImage before = document.image();
	int checkpoints = 0;
	ok &= expect(!document.floodFill({0, 0}, Qt::red, 255, true, &error, [&](qint64, qint64) { return ++checkpoints < 4; }) && document.image() == before && document.revision() == revision && !document.canUndo(), "cancelled tolerant fill preserves pixels, history, and save point");
	checkpoints = 0;
	ok &= expect(!document.drawShape(TextureShape::Ellipse, {0, 0}, {511, 511}, Qt::red, blend, true, &error, [&](qint64, qint64) { return ++checkpoints < 4; }) && document.image() == before && document.revision() == revision, "shape cancellation never commits partial rows");
	checkpoints = 0;
	ok &= expect(!document.offsetPixels({10, -9}, &error, [&](qint64, qint64) { return ++checkpoints < 4; }) && document.image() == before && document.revision() == revision, "offset cancellation retains original pixels and history");
	ok &= expect(document.beginStroke({0, 0}, Qt::red, blend), "begin a cancellable blend stroke"); checkpoints = 0;
	ok &= expect(!document.continueStroke({511, 511}, [&](qint64, qint64) { return ++checkpoints < 4; }) && !document.strokeActive() && document.image() == before && !document.isDirty(), "interactive cancellation restores the entire gesture, including its first stamp");
	for (QJsonObject operation : {
		QJsonObject{{QStringLiteral("op"), QStringLiteral("stroke")}, {QStringLiteral("points"), QJsonArray{QJsonArray{1, 1}}}, {QStringLiteral("color"), QStringLiteral("red")}, {QStringLiteral("mode"), QStringLiteral("multiply")}},
		QJsonObject{{QStringLiteral("op"), QStringLiteral("fill")}, {QStringLiteral("x"), 0}, {QStringLiteral("y"), 0}, {QStringLiteral("color"), QStringLiteral("red")}, {QStringLiteral("tolerance"), 256}},
		QJsonObject{{QStringLiteral("op"), QStringLiteral("rectangle")}, {QStringLiteral("x"), 0}, {QStringLiteral("y"), 0}, {QStringLiteral("x2"), 4}, {QStringLiteral("y2"), 4}, {QStringLiteral("color"), QStringLiteral("red")}, {QStringLiteral("filled"), 1}},
		QJsonObject{{QStringLiteral("op"), QStringLiteral("ellipse")}, {QStringLiteral("x"), -513}, {QStringLiteral("y"), 0}, {QStringLiteral("x2"), 4}, {QStringLiteral("y2"), 4}, {QStringLiteral("color"), QStringLiteral("red")}, {QStringLiteral("wrap"), true}}
	}) {
		ok &= expect(!applyTextureOperations(&document, QJsonArray{operation}, {}, &error) && document.image() == before && document.revision() == revision, "invalid recipe fields and unbounded repeat endpoints reject atomically");
	}
	document.setLayerProperties(QStringLiteral("Locked"), true, true, 100, TextureBlendMode::Normal);
	ok &= expect(!document.drawShape(TextureShape::Line, {0, 0}, {5, 5}, Qt::red, solid, false) && !document.floodFill({0, 0}, Qt::red, 100, true) && !document.paintStroke({{0, 0}}, Qt::red, blend), "all new tools honor layer locks");
	document.create({2048, 2048}, Qt::transparent);
	QElapsedTimer timer; timer.start();
	ok &= expect(document.paintStroke({{0, 0}, {2047, 2047}}, QColor(200, 50, 10, 100), TextureBrush{128, TextureBrushShape::Round, TexturePaintMode::SourceOver}, &error), "representative broad diagonal brush completes");
	std::cout << "2048 x 2048 round 128-pixel blend stroke: " << timer.elapsed() << " ms\n";
	ok &= expect(document.image().pixelColor(1024, 1024) == QColor(200, 50, 10, 100) && document.image().pixelColor(0, 2047).alpha() == 0, "broad brush retains alpha and leaves uncovered pixels intact");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
