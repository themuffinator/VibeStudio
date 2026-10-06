#include "app/map_grid.h"
#include "app/viewport_ring_cache.h"
#include "app/viewport_image.h"
#include <QApplication>
#include <QDir>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; }
	return value;
}
int difference(const QImage& a, const QImage& b)
{
	if (a.size() != b.size()) { return 256; }
	int maximum = 0;
	for (int y = 0; y < a.height(); ++y) {
		for (int x = 0; x < a.width(); ++x) {
			const auto p = a.pixel(x,y), q = b.pixel(x,y);
			maximum = std::max({maximum,std::abs(qRed(p) - qRed(q)),std::abs(qGreen(p) - qGreen(q)),
				std::abs(qBlue(p) - qBlue(q)),std::abs(qAlpha(p) - qAlpha(q))});
		}
	}
	return maximum;
}
void capture(const QImage& actual, const QImage& expected, const QString& name)
{
	if (qEnvironmentVariableIsEmpty("VIBESTUDIO_TEST_CAPTURE_DIR")) { return; }
	QDir output(qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR")); QDir().mkpath(output.path());
	actual.save(output.filePath(name + "-actual.png")); expected.save(output.filePath(name + "-reference.png"));
}
QImage ringImage(ViewportRingCache* cache, qreal ratio, int variant)
{
	QImage image(QSize(int(std::ceil(323 * ratio)),int(std::ceil(217 * ratio))),QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio); image.fill(QColor(23,31,39));
	QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing);
	if (variant == 1) { painter.translate(3.3,-1.7); painter.setClipRect(QRectF(10,9,190,170)); painter.setOpacity(0.6); }
	if (variant == 2) { painter.translate(28,9); painter.rotate(17); }
	if (variant == 3) { painter.scale(1.2,0.8); }
	if (variant == 4) { painter.setRenderHint(QPainter::Antialiasing,false); }
	if (variant == 5) { painter.setCompositionMode(QPainter::CompositionMode_Source); }
	const QColor color = variant == 1 ? QColor(219,171,38,170) : QColor(255,73,198);
	const double width = variant == 1 ? 2.0 : 1.4;
	QVector<QPointF> points {{0,0},{323,217},{16.125,40.375},{47.9,61.13},{51.11,60.51},{320.13,35.17}};
	for (int i = 0; i < 24; ++i) { points.append(QPointF(22 + i * 11.173,113 + (i % 4) * 14.159)); }
	if (cache) { cache->draw(painter,points,color,width,8); }
	else {
		painter.setPen(QPen(color,width,Qt::SolidLine)); painter.setBrush(Qt::NoBrush);
		for (const auto center : points) { painter.drawEllipse(center,8,8); }
	}
	return image;
}
QImage gridReference(const MapGridView& view)
{
	QImage image(QSize(int(std::ceil(view.viewport.width() * view.pixelRatio + view.pixelPhase.x())),int(std::ceil(view.viewport.height() * view.pixelRatio + view.pixelPhase.y()))),
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(view.pixelRatio); image.fill(Qt::transparent);
	QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing); painter.setBrush(Qt::NoBrush);
	painter.translate(view.pixelPhase / view.pixelRatio);
	const auto lines = mapGridLines(view);
	const QRgb colors[] = {view.minor,view.major,view.axis};
	for (int pass = 0; pass < 3; ++pass) {
		painter.setPen(QPen(QColor::fromRgba(colors[pass]),pass == 2 ? 2.0 : 1.0));
		for (const auto& line : lines[pass]) { painter.drawLine(line); }
	}
	return image;
}
}
int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM","offscreen"); qputenv("QT_SCALE_FACTOR","1");
	QApplication app(argc,argv); bool ok = true;
	ViewportRingCache rings;
	int maximumRingDelta = 0;
	for (const qreal ratio : {1.0,1.25,1.5,1.75,2.0}) {
		for (int variant = 0; variant < 6; ++variant) {
			const auto expected = ringImage(nullptr,ratio,variant), actual = ringImage(&rings,ratio,variant);
			const int delta = difference(actual,expected);
			maximumRingDelta = std::max(maximumRingDelta,delta);
			if (delta > 4) {
				std::cerr << "ring reference difference=" << delta << " DPR=" << ratio << " variant=" << variant << '\n';
				capture(actual,expected,QStringLiteral("rings-%1-%2").arg(ratio).arg(variant)); ok = false;
			}
			ok &= expect(actual == ringImage(&rings,ratio,variant),"warm rings exactly match the first cached draw");
		}
	}
	ViewportRingCache tiny(1);
	ok &= expect(ringImage(&tiny,1.75,0) == ringImage(nullptr,1.75,0) && tiny.entries() == 0,
		"insufficient ring budget uses complete ordinary drawing");
	ViewportRingCache oneRing(2304);
	ok &= expect(difference(ringImage(&oneRing,1,0),ringImage(nullptr,1,0)) <= 4 && oneRing.entries() == 1,
		"eviction from a one-ring cache still paints every requested marker once");
	QImage stress(1300,1300,QImage::Format_ARGB32_Premultiplied); stress.fill(Qt::transparent);
	QImage stressReference = stress.copy();
	{
		QPainter painter(&stress); painter.setRenderHint(QPainter::Antialiasing);
		QPainter reference(&stressReference); reference.setRenderHint(QPainter::Antialiasing);
		reference.setPen(QPen(Qt::magenta,1.4)); reference.setBrush(Qt::NoBrush);
		QVector<QPointF> points;
		for (int i = 0; i < 1024; ++i) { points.append(QPointF(20 + i * 0.997123,20 + i * 0.711983)); }
		rings.draw(painter,points,Qt::magenta,1.4,8);
		for (const auto center : points) { reference.drawEllipse(center,8,8); }
	}
	ok &= expect(difference(stress,stressReference) <= 4,"cache eviction preserves dense overlapping marker coverage");
	ok &= expect(rings.entries() > 0 && rings.entries() <= 512 && rings.accountedBytes() <= 8 * 1024 * 1024,
		"ring raster memory and entry count stay bounded across distinct phases");
	rings.clear(); ok &= expect(rings.entries() == 0 && rings.accountedBytes() == 0,"ring cache releases storage");
	MapGridView view {QSize(323,217),QPointF(11.3,-23.7),0.717,1,16,qRgba(58,65,78,213),qRgba(93,102,117,245),qRgb(138,148,166)};
	for (const qreal ratio : {1.0,1.25,1.5,1.75,2.0}) {
		for (const double zoom : {0.031,0.717,1.0,3.13}) {
			view.pixelRatio = ratio; view.zoom = zoom;
			MapGridFrame frame;
			ok &= expect(renderMapGrid(view,&frame),"grid frame builds at all physical scales");
			const auto key = frame.image.cacheKey();
			ok &= expect(renderMapGrid(view,&frame) && key == frame.image.cacheKey(),"unchanged grid reuses its physical image");
			const auto reference = gridReference(view);
			const int delta = difference(frame.image,reference);
			if (delta != 0) {
				std::cerr << "grid reference difference=" << delta << " DPR=" << ratio << " zoom=" << zoom << '\n';
				capture(frame.image,reference,QStringLiteral("grid-%1-%2").arg(ratio).arg(zoom)); ok = false;
			}
		}
	}
	MapGridFrame frame; renderMapGrid(view,&frame);
	const auto change = [&](const MapGridView& changed) {
		const auto previous = frame.image.cacheKey();
		return renderMapGrid(changed,&frame) && previous != frame.image.cacheKey();
	};
	view.center.rx() += 0.125; ok &= expect(change(view),"panning refreshes grid");
	view.zoom *= 1.1; ok &= expect(change(view),"zoom refreshes grid");
	view.units = 32; ok &= expect(change(view),"grid spacing refreshes grid");
	view.viewport.rwidth() += 4; ok &= expect(change(view),"resizing refreshes grid");
	view.pixelRatio = 1.5; ok &= expect(change(view),"physical scale refreshes grid");
	view.minor = qRgb(255,255,255); ok &= expect(change(view),"palette change refreshes grid");
	for (const auto phase : {QPointF(0.125,0.875),QPointF(0.5,0.5),QPointF(0.999,0.001)}) {
		view.pixelPhase = phase;
		ok &= expect(change(view) && frame.image == gridReference(view),"physical-origin phase refreshes complete native grid pixels");
	}
	const auto kept = frame.image;
	view.viewport = QSize(10000,10000);
	ok &= expect(!renderMapGrid(view,&frame) && frame.image == kept,"oversized grid keeps previous bounded frame for caller fallback");
	view.viewport = QSize(100,100); view.zoom = std::numeric_limits<double>::quiet_NaN();
	ok &= expect(!renderMapGrid(view,&frame) && mapGridLines(view)[0].isEmpty(),"invalid coordinates fail safely");
	view = {QSize(128,128),QPointF(),1,1,16,0,0,0};
	const auto lines = mapGridLines(view);
	ok &= expect(lines[2].size() == 2 && lines[2][0] == QLineF(64,0,64,128) && lines[2][1] == QLineF(0,64,128,64),
		"world axes cross at the projected origin");
	ok &= expect(lines[0].contains(QLineF(16,0,16,128)) && lines[0].contains(QLineF(0,16,128,16)),"grid lines retain world spacing");
	view.zoom = 0.01;
	const auto sparse = mapGridLines(view);
	ok &= expect(sparse[0].size() + sparse[1].size() < 50,"adaptive grid avoids dense zoomed-out stripes");
	QTransform transform(1.5,0,0,1.5,-10.25,20.875);
	const auto device = viewportImageDevice(transform);
	ok &= expect(device.pixelRatio == 1.5 && device.pixelPhase == QPointF(0.75,0.875),"negative translations retain positive fractional device phases");
	ok &= expect(viewportImageSize({5,3},1.5,{0.75,0.875}) == QSize(9,6),"image coverage includes leading and trailing fractional pixels");
	ok &= expect(viewportImageSize({4096,2048},1) == QSize(4096,2048) && viewportImageSize({4096,2048},1,{0.1,0}).isEmpty(),
		"phase guard pixels count against the unchanged physical image budget");
	ok &= expect(viewportImageSize({10,10},1,{1,0}).isEmpty() && viewportImageSize({10,10},1,{-0.1,0}).isEmpty()
		&& viewportImageSize({10,10},1,{std::numeric_limits<double>::quiet_NaN(),0}).isEmpty()
		&& viewportImageSize({10,10},std::numeric_limits<double>::denorm_min(),{0.5,0.5}).isEmpty(),"invalid phase/scale requests cannot allocate unusable image coordinates");
	for (const auto& unsupported : {QTransform().rotate(12),QTransform().scale(1.5,2),QTransform().scale(-1,-1),QTransform().shear(0.1,0)}) {
		ok &= expect(viewportImageDevice(unsupported).pixelRatio == 0,"unsupported image transforms retain ordinary painter fallback");
	}
	std::cout << "Grid matches native Qt pixels exactly; maximum ring channel difference=" << maximumRingDelta << "/255.\n";
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
