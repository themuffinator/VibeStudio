#include "core/texture_project.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonObject>

#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; } return value; }
QImage fixture(QSize size, QImage::Format format, int paletteSize = 32, bool transparent = true)
{
	QImage image(size, format);
	if (format == QImage::Format_Indexed8) {
		QList<QRgb> palette;
		for (int i = 0; i < paletteSize; ++i) { palette << qRgba(i * 7 % 256, i * 13 % 256, i * 29 % 256, transparent && i == 3 ? 0 : 255); }
		if (paletteSize > 6) { palette[6] = palette[5]; } // Duplicate colors must retain distinct indices.
		image.setColorTable(palette);
	}
	for (int y = 0; y < size.height(); ++y) {
		for (int x = 0; x < size.width(); ++x) {
			if (format == QImage::Format_Indexed8) { image.setPixel(x, y, (x + y * size.width()) % paletteSize); }
			else { image.setPixel(x, y, qRgba(x * 31 % 256, y * 47 % 256, (x + y) * 19 % 256, format == QImage::Format_RGB32 ? 255 : (x + y) % 5 * 51)); }
		}
	}
	return image;
}
uint storedPixel(const QImage& image, QPoint point) { return image.format() == QImage::Format_Indexed8 ? image.pixelIndex(point) : image.pixel(point); }
bool sameLayers(const TextureDocument& a, const TextureDocument& b)
{
	if (a.size() != b.size() || a.layers().size() != b.layers().size() || a.activeLayerIndex() != b.activeLayerIndex()) { return false; }
	for (int i = 0; i < a.layers().size(); ++i) {
		const auto& x = a.layers()[i]; const auto& y = b.layers()[i];
		if (x.id != y.id || x.name != y.name || x.visible != y.visible || x.locked != y.locked || x.opacity != y.opacity || x.blend != y.blend ||
			x.pixels.format() != y.pixels.format() || x.pixels != y.pixels) { return false; }
	}
	return true;
}
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); bool ok = true; QString error;
	for (auto format : {QImage::Format_Indexed8, QImage::Format_ARGB32, QImage::Format_RGB32}) {
		const QImage source = fixture({7, 5}, format);
		for (const QSize target : {QSize(3, 2), QSize(14, 15), QSize(1, 1), QSize(7, 5)}) {
			const auto resized = resizeTexturePixels(source, target, false, &error);
			bool exact = resized.format() == source.format() && resized.colorTable() == source.colorTable();
			for (int y = 0; y < target.height() && exact; ++y) {
				for (int x = 0; x < target.width(); ++x) {
					const QPoint from(int(std::floor((x + 0.5) * source.width() / target.width())), int(std::floor((y + 0.5) * source.height() / target.height())));
					exact &= storedPixel(resized, {x, y}) == storedPixel(source, from);
				}
			}
			ok &= expect(exact, "nearest resampling preserves exact source samples, duplicate indices and hidden RGB");
		}
		const auto rotated = rotateTexturePixelsClockwise(source, &error);
		bool exact = rotated.size() == QSize(5, 7) && rotated.format() == source.format() && rotated.colorTable() == source.colorTable();
		for (int y = 0; y < rotated.height(); ++y) { for (int x = 0; x < rotated.width(); ++x) { exact &= storedPixel(rotated, {x, y}) == storedPixel(source, {y, source.height() - 1 - x}); } }
		ok &= expect(exact, "quarter turn uses the exact inverse mapping for every source representation");
		TextureDocument document; document.reset(source); document.markSaved();
		const auto workingPixels = document.activeLayer()->pixels;
		for (int turn = 0; turn < 4; ++turn) { ok &= expect(document.rotateClockwise(&error), "whole canvas can rotate repeatedly"); }
		ok &= expect(document.activeLayer()->pixels == workingPixels && document.activeLayer()->pixels.format() == workingPixels.format(), "four quarter turns restore every working pixel after import normalization");
		for (int turn = 0; turn < 4; ++turn) { document.undo(); }
		ok &= expect(!document.isDirty() && document.activeLayer()->pixels == workingPixels, "rotation undo reaches the saved revision");
	}
	const QImage indexed = fixture({6, 5}, QImage::Format_Indexed8);
	for (int index = 0; index < 9; ++index) {
		const auto anchor = static_cast<TextureAnchor>(index); TextureAnchor parsed{};
		ok &= expect(textureAnchorFromId(textureAnchorId(anchor), &parsed) && parsed == anchor, "anchor identifiers round trip");
		const int growX[]{0, 1, 3}, growY[]{0, 2, 4}, shrink[]{0, -2, -3};
		const QPoint grow(growX[index % 3], growY[index / 3]), crop(shrink[index % 3], shrink[index / 3]);
		ok &= expect(textureAnchorOffset(indexed.size(), {9, 9}, anchor) == grow && textureAnchorOffset(indexed.size(), {3, 2}, anchor) == crop, "all nine anchors align correctly for odd positive and negative differences");
		TextureDocument document; document.reset(indexed); document.addLayer(QStringLiteral("Hidden locked"), indexed);
		document.setLayerProperties(QStringLiteral("Hidden locked"), false, true, 47, TextureBlendMode::Multiply); document.markSaved();
		const auto before = document;
		ok &= expect(document.resizeCanvas({9, 9}, grow, &error), "canvas sizing applies to hidden and locked layers");
		bool exact = true;
		for (const auto& layer : document.layers()) {
			exact &= layer.pixels.format() == QImage::Format_Indexed8 && layer.pixels.colorTable() == indexed.colorTable();
			for (int y = 0; y < 9; ++y) { for (int x = 0; x < 9; ++x) {
				const QPoint old = QPoint(x, y) - grow;
				exact &= layer.pixels.pixelIndex(x, y) == (indexed.rect().contains(old) ? indexed.pixelIndex(old) : 3);
			} }
		}
		ok &= expect(exact && document.activeLayer()->locked && !document.activeLayer()->visible && document.activeLayer()->opacity == 47, "canvas padding preserves layer properties and indices with the original transparent entry");
		ok &= expect(document.undo() && sameLayers(document, before) && !document.isDirty(), "canvas size undo restores all layers and save state");
		ok &= expect(document.resizeCanvas({3, 2}, crop, &error) && document.layers()[0].pixels == indexed.copy(QRect(-crop, QSize(3, 2))), "canvas shrink crops exact pixels without resampling");
	}
	for (int count : {32, 256}) {
		const auto opaque = fixture({8, 6}, QImage::Format_Indexed8, count, false);
		const auto padded = resizeTextureCanvas(opaque, {10, 8}, {1, 1}, &error);
		ok &= expect(padded.pixelColor(0, 0).alpha() == 0 && padded.copy(1, 1, 8, 6).convertToFormat(QImage::Format_ARGB32) == opaque.convertToFormat(QImage::Format_ARGB32), "opaque indexed canvas exposes truly transparent padding without changing colors");
		ok &= expect(count == 32 ? padded.format() == QImage::Format_Indexed8 && padded.colorCount() == 33 && padded.pixelIndex(7, 1) == 6 : padded.format() == QImage::Format_ARGB32, "spare palette slots preserve indices; full opaque palettes promote only when transparency is needed");
		const auto cropped = resizeTextureCanvas(opaque, {3, 2}, {-1, -2}, &error);
		ok &= expect(cropped.format() == QImage::Format_Indexed8 && cropped.colorTable() == opaque.colorTable() && cropped == opaque.copy(1, 2, 3, 2), "opaque indexed crop needs no transparent palette entry or conversion");
	}
	for (auto format : {QImage::Format_Indexed8, QImage::Format_ARGB32}) {
		const auto source = fixture({10, 9}, format);
		const QRect area(3, 2, 3, 4);
		for (int anchorIndex = 0; anchorIndex < 9; ++anchorIndex) {
			for (bool rotate : {false, true}) {
				TextureDocument document; document.reset(source); document.addLayer(QStringLiteral("Selected"), source); document.setSelection(area); document.markSaved();
				const auto before = document;
				const QSize target = rotate ? QSize(4, 3) : QSize(4, 2);
				const int dx[]{0, -1, -1}, dyResize[]{0, 1, 2}, dyRotate[]{0, 0, 1};
				const QPoint origin = area.topLeft() + QPoint(dx[anchorIndex % 3], (rotate ? dyRotate : dyResize)[anchorIndex / 3]);
				const QRect destination(origin, target);
				QImage expected = source;
				for (int y = area.top(); y <= area.bottom(); ++y) { for (int x = area.left(); x <= area.right(); ++x) { expected.setPixel(x, y, format == QImage::Format_Indexed8 ? 3u : qRgba(0, 0, 0, 0)); } }
				for (int y = 0; y < target.height(); ++y) { for (int x = 0; x < target.width(); ++x) {
					const QPoint sample = rotate ? QPoint(y, area.height() - 1 - x) : QPoint(int(std::floor((x + 0.5) * area.width() / target.width())), int(std::floor((y + 0.5) * area.height() / target.height())));
					expected.setPixel(origin + QPoint(x, y), storedPixel(source, area.topLeft() + sample));
				} }
				const auto anchor = static_cast<TextureAnchor>(anchorIndex);
				ok &= expect(rotate ? document.rotateSelectedPixelsClockwise(anchor, &error) : document.resizeSelectedPixels(target, false, anchor, &error), "selected transform accepts every fitting anchor");
				ok &= expect(document.activeLayer()->pixels == expected && document.activeLayer()->pixels.format() == source.format() && document.layers()[0].pixels == source && document.selection() == destination, "selected transform replaces its destination, clears vacated pixels and leaves other layers exact");
				TextureDocument restored;
				ok &= expect(decodeTextureProject(encodeTextureProject(document), &restored) && sameLayers(document, restored), "transformed indices and hidden RGBA survive native project persistence");
				ok &= expect(document.undo() && sameLayers(document, before) && document.selection() == area && !document.isDirty() && document.redo() && document.selection() == destination && document.isDirty(), "selected transform undo and redo restore geometry, pixels and saved revision");
			}
		}
		TextureDocument document; document.reset(source); document.setSelection(area); document.markSaved();
		ok &= expect(document.flip(true, &error) && document.activeLayer()->pixels.format() == format, "selected flip retains the working representation");
		for (int y = 0; y < area.height(); ++y) { for (int x = 0; x < area.width(); ++x) { ok &= expect(storedPixel(document.activeLayer()->pixels, area.topLeft() + QPoint(x, y)) == storedPixel(source, area.topRight() + QPoint(-x, y)), "selected flip retains exact source pixels"); } }
		document.undo();
		ok &= expect(document.moveSelectedPixels({2, 1}, &error) && document.activeLayer()->pixels.format() == format && document.copySelection() == source.copy(area), "overlapping move preserves exact selected pixels and palette indices");
		document.undo(); ok &= expect(document.clearSelectedPixels(&error) && document.activeLayer()->pixels.format() == format && document.undo() && document.activeLayer()->pixels == source, "clear uses transparent storage and remains exactly undoable");
	}
	TextureDocument empty; empty.create({10, 10}, Qt::transparent); empty.setSelection({3, 3, 2, 3}); empty.markSaved(); const auto revision = empty.revision();
	ok &= expect(empty.rotateSelectedPixelsClockwise(TextureAnchor::Center, &error) && empty.selection() == QRect(2, 3, 3, 2) && empty.revision() == revision && !empty.isDirty() && empty.canUndo(), "selection-only transform records geometry without dirtying saved content");
	ok &= expect(empty.undo() && empty.selection() == QRect(3, 3, 2, 3) && !empty.isDirty() && empty.redo() && empty.selection() == QRect(2, 3, 3, 2) && !empty.isDirty(), "selection-only history keeps the saved revision in both directions");
	empty.undo(); empty.resizeSelectedPixels({1, 2}, false, TextureAnchor::TopLeft);
	ok &= expect(!empty.canRedo() && !empty.isDirty(), "a new selection-only transform clears the abandoned redo branch");
	TextureDocument document; document.reset(fixture({12, 10}, QImage::Format_ARGB32)); document.setSelection({0, 0, 3, 5}); document.markSaved();
	const auto boundary = document;
	ok &= expect(!document.rotateSelectedPixelsClockwise(TextureAnchor::Center, &error) && !document.resizeSelectedPixels({13, 1}, false, TextureAnchor::TopLeft, &error) &&
		!document.resizeCanvas({10, 10}, {std::numeric_limits<int>::max(), 0}, &error) && !document.resizeCanvas({10, 10}, {std::numeric_limits<int>::min(), 0}, &error) &&
		sameLayers(document, boundary) && document.selection() == boundary.selection() && !document.canUndo() && !document.isDirty(), "out-of-canvas transforms and overflowing offsets fail without partial edits");
	ok &= expect(!document.resizeSelectedPixels({3, 5}, false, static_cast<TextureAnchor>(20), &error), "invalid anchor enum fails");
	document.clearSelection(); ok &= expect(!document.rotateSelectedPixelsClockwise(TextureAnchor::Center, &error) && !document.resizeSelectedPixels({2, 2}, false, TextureAnchor::Center, &error), "selected resize and rotate require an explicit selection");
	document.setSelection({3, 2, 3, 4}); document.setLayerProperties(QStringLiteral("Locked"), true, true, 100, TextureBlendMode::Normal);
	ok &= expect(!document.rotateSelectedPixelsClockwise(TextureAnchor::Center, &error) && !document.resizeSelectedPixels({2, 2}, false, TextureAnchor::Center, &error), "selected transforms cannot modify locked layers");
	document.setLayerProperties(QStringLiteral("Hidden"), false, false, 100, TextureBlendMode::Normal);
	ok &= expect(!document.rotateSelectedPixelsClockwise(TextureAnchor::Center, &error), "selected transforms cannot modify hidden layers");
	TextureDocument large; large.reset(fixture({384, 256}, QImage::Format_ARGB32)); large.addLayer(QStringLiteral("Second"), large.image()); large.setSelection({40, 50, 100, 150}); large.markSaved();
	for (int operation = 0; operation < 6; ++operation) {
		for (int cancelAt : {0, 35, 100}) {
			auto edited = large; int callbacks = 0; double previous = 0; bool monotonic = true;
			const auto cancel = [&](qint64 done, qint64 total) { ++callbacks; const double fraction = total > 0 ? double(done) / total : 0; monotonic &= fraction >= previous; previous = fraction; return fraction * 100 < cancelAt; };
			bool applied = false;
			switch (operation) {
			case 0: applied = edited.resizeCanvas({512, 384}, {32, 16}, &error, cancel); break;
			case 1: applied = edited.resize({128, 512}, false, &error, cancel); break;
			case 2: applied = edited.resize({128, 512}, true, &error, cancel); break;
			case 3: applied = edited.rotateClockwise(&error, cancel); break;
			case 4: applied = edited.resizeSelectedPixels({140, 170}, false, TextureAnchor::Center, &error, cancel); break;
			case 5: applied = edited.rotateSelectedPixelsClockwise(TextureAnchor::Center, &error, cancel); break;
			}
			ok &= expect(!applied && callbacks > 0 && monotonic && sameLayers(edited, large) && edited.selection() == large.selection() && edited.revision() == large.revision() && edited.undoLabel() == large.undoLabel() && !edited.isDirty(), "early, mid-operation and final cancellation retain complete document/history with monotonic progress");
		}
	}
	TextureDocument many; many.create({16, 16}, Qt::red); for (int layer = 1; layer < 32; ++layer) { many.addLayer(QString::number(layer)); }
	const auto manyBefore = many;
	ok &= expect(!many.resizeCanvas({2048, 2048}, {}, &error) && sameLayers(many, manyBefore) && !many.resize({2048, 2048}, false, &error), "aggregate layer pixel budget is checked before allocating canvas transforms");
	TextureDocument recipe; recipe.create({8, 6}, Qt::blue);
	QJsonArray operations{QJsonObject{{"op", "canvas-size"}, {"width", 12}, {"height", 10}, {"anchor", "bottom-right"}},
		QJsonObject{{"op", "select"}, {"x", 5}, {"y", 5}, {"width", 3}, {"height", 2}},
		QJsonObject{{"op", "resize-selection"}, {"width", 2}, {"height", 3}, {"anchor", "top-left"}}, QJsonObject{{"op", "rotate-selection"}, {"anchor", "top-left"}}};
	ok &= expect(applyTextureOperations(&recipe, operations, {}, &error) && recipe.size() == QSize(12, 10) && recipe.selection() == QRect(5, 5, 3, 2) && recipe.image().pixelColor(0, 0).alpha() == 0, "shared recipes provide canvas size and both selected transforms");
	for (const QJsonObject& invalid : {QJsonObject{{"op", "canvas-size"}, {"width", 8}, {"height", 8}, {"x", 0}},
		QJsonObject{{"op", "canvas-size"}, {"width", 8}, {"height", 8}, {"anchor", "center"}, {"x", 0}, {"y", 0}},
		QJsonObject{{"op", "resize-selection"}, {"width", 2}, {"height", 2}, {"smooth", "true"}}, QJsonObject{{"op", "rotate-selection"}, {"anchor", "unknown"}}}) {
		const auto before = recipe;
		ok &= expect(!applyTextureOperations(&recipe, {QJsonObject{{"op", "canvas-size"}, {"width", 20}, {"height", 20}}, invalid}, {}, &error) && sameLayers(recipe, before) && recipe.selection() == before.selection() && recipe.revision() == before.revision(), "invalid transform recipes roll back earlier operations too");
	}
	ok &= expect(applyTextureOperations(&recipe, {QJsonObject{{"op", "canvas-size"}, {"width", 12}, {"height", 10}, {"x", 1}, {"y", 0}}}, {}, &error), "canvas size recipe accepts explicit placement offsets");
	TextureDocument timed; timed.create({2048, 2048}, Qt::red); QElapsedTimer timer; timer.start();
	ok &= expect(timed.rotateClockwise(&error), "representative quarter turn succeeds");
	std::cout << "2048 x 2048 quarter turn: " << timer.elapsed() << " ms\n"; timer.restart();
	ok &= expect(timed.resizeCanvas({2048, 2048}, {1, 1}, &error), "representative canvas offset succeeds");
	std::cout << "2048 x 2048 canvas placement: " << timer.elapsed() << " ms\n";
	TextureDocument bounded; bounded.create({16, 16}, Qt::red); bounded.markSaved();
	for (int cycle = 0; cycle < 8; ++cycle) {
		ok &= expect(bounded.resize({2048, 2048}, false, &error), "grow bounded history fixture");
		if (cycle != 7) { ok &= expect(bounded.resize({16, 16}, false, &error), "shrink bounded history fixture"); }
	}
	const qint64 historyLimit = 128 * 1024 * 1024;
	ok &= expect(bounded.historyBytes() <= historyLimit, "transform commits obey the additional pixel history budget");
	int undoCount = 0;
	while (bounded.canUndo()) { bounded.undo(); ++undoCount; ok &= expect(bounded.historyBytes() <= historyLimit, "undo to a smaller canvas must also enforce the history byte budget"); }
	ok &= expect(undoCount > 0 && bounded.canRedo(), "history pruning keeps the immediate inverse operation");
	while (bounded.canRedo()) { bounded.redo(); ok &= expect(bounded.historyBytes() <= historyLimit, "redo obeys the same history byte budget"); }
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
