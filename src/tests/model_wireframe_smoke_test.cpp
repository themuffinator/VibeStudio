#include "app/wire_lines.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QLineF>
#include <QPainter>
#include <QPen>
#include <QThread>
#include <QVector>

#include <algorithm>
#include <array>
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

bool benchmark(bool compare)
{
	bool ok = true;
	QVector<std::array<QPointF, 3>> faces;
	QVector<QLineF> lines, unique;
	constexpr int side = 256;
	const auto point = [](int x, int y) { return QPointF(512 + (x - 127.5) * 1.85, 384 + (y - 127.5) * 1.85); };
	for (int y = 0; y + 1 < side; ++y)
	{
		for (int x = 0; x + 1 < side; ++x)
		{
			const auto a = point(x, y), b = point(x + 1, y), c = point(x, y + 1), d = point(x + 1, y + 1);
			faces << std::array<QPointF, 3>{a, b, c} << std::array<QPointF, 3>{b, d, c};
			lines << QLineF(a, b) << QLineF(b, c) << QLineF(c, a) << QLineF(b, d) << QLineF(d, c) << QLineF(c, b);
			unique << QLineF(a, b) << QLineF(a, c) << QLineF(b, c);
			if (x + 2 == side)
			{
				unique << QLineF(b, d);
			}
			if (y + 2 == side)
			{
				unique << QLineF(c, d);
			}
		}
	}
	for (int ratio : {1, 2})
	{
		for (int mode = compare ? 0 : 5; mode < 6; ++mode)
		{
			QImage image(1024 * ratio, 768 * ratio, QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			QElapsedTimer clock;
			clock.start();
			if (mode >= 4)
			{
				QVector<vibestudio::WireSegment> segments;
				for (const auto &line : (mode == 4 ? lines : unique))
				{
					segments << vibestudio::WireSegment{line.p1(), line.p2(), false};
				}
				ok &= expect(vibestudio::renderWireLines(image.size(), segments, {.pixelRatio = double(ratio)}, &image),
							 "dense wireframe probe completes");
			}
			else
			{
				QPainter painter(&image);
				painter.scale(ratio, ratio);
				painter.setRenderHint(QPainter::Antialiasing, true);
				painter.setBrush(Qt::NoBrush);
				QPen pen(Qt::white, mode == 3 ? ratio : 1);
				if (mode == 3)
				{
					pen.setCosmetic(true);
				}
				painter.setPen(pen);
				if (mode == 0)
				{
					for (const auto &face : faces)
					{
						painter.drawPolygon(face.data(), 3);
					}
				}
				else
				{
					const auto &input = mode == 1 ? lines : unique;
					painter.drawLines(input.constData(), input.size());
				}
			}
			std::cout << "ratio=" << ratio << " mode=" << mode << " elapsedMs=" << clock.elapsed() << std::endl;
		}
	}
	return ok;
}

} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true;
	QImage image, reverse;
	WireStyle style;
	style.wire = qRgb(255, 0, 0);
	{
		QImage layer(64, 64, QImage::Format_ARGB32_Premultiplied); layer.fill(qRgb(10, 20, 30));
		WireStyle first, second; first.wire = qRgb(220, 10, 10); second.wire = qRgb(10, 220, 10);
		ok &= expect(paintWireLines(&layer, {{{8,32.5},{56,32.5},false}}, first)
			&& paintWireLines(&layer, {{{32.5,8},{32.5,56},false}}, second)
			&& layer.pixel(4,4) == qRgb(10,20,30) && layer.pixel(16,32) == first.wire && layer.pixel(32,32) == second.wire,
			"ordered wire batches preserve existing content and composite the later style last");
		const QImage previous = layer; std::atomic_bool cancelled{true};
		ok &= expect(!paintWireLines(&layer, {{{0,0},{64,64},false}}, first, &cancelled) && layer == previous,
			"pre-cancelled composition leaves the existing frame untouched");
		QImage unsupported(8,8,QImage::Format_RGB32); unsupported.fill(Qt::blue); const auto before = unsupported;
		ok &= expect(!paintWireLines(&unsupported, {}, first) && unsupported == before,
			"composition refuses unsupported pixel storage without changing it");
	}
	for (int ratio : {1, 2})
	{
		style.pixelRatio = ratio;
		ok &= expect(renderWireLines({64 * ratio, 64 * ratio}, {{{8, 16.5}, {56, 16.5}, false}}, style, &image),
					 "render a scaled horizontal edge");
		ok &= expect(image.pixel(32 * ratio, 16 * ratio) == style.wire && qAlpha(image.pixel(32 * ratio, 14 * ratio)) == 0,
					 "wire stroke follows logical coordinates and physical pixel ratio");
		ok &= expect(renderWireLines(image.size(), {{{56, 16.5}, {8, 16.5}, false}}, style, &reverse) && image == reverse,
					 "reversing an edge retains identical coverage");
	}
	style.pixelRatio = 1;
	ok &= expect(renderWireLines({64, 64}, {{{8, 16}, {56, 16}, false}}, style, &image) && qAlpha(image.pixel(32, 15)) == 128 &&
					 qAlpha(image.pixel(32, 16)) == 128 && qAlpha(image.pixel(32, 14)) == 0,
				 "fractional wire positions share coverage across neighbouring pixels");
	style.width = .5;
	ok &= expect(renderWireLines({64, 64}, {{{8, 16.5}, {56, 16.5}, false}}, style, &image) && qAlpha(image.pixel(32, 16)) == 128,
				 "subpixel-width strokes retain their fractional coverage");
	style.width = 1;
	style.wire = qRgba(255, 0, 0, 128);
	ok &= expect(renderWireLines({64, 64}, {{{8, 16.5}, {56, 16.5}, false}}, style, &image) &&
					 image.pixel(32, 16) == qRgba(128, 0, 0, 128),
				 "wire pixels preserve premultiplied alpha");
	style.wire = qRgb(20, 40, 200);
	style.selection = qRgb(255, 210, 20);
	QVector<WireSegment> selected{{{8, 32.5}, {56, 32.5}, true}, {{8, 32.5}, {56, 32.5}, false}};
	ok &= expect(renderWireLines({64, 64}, selected, style, &image) && image.pixel(12, 32) == style.selection &&
					 image.pixel(20, 32) == style.wire,
				 "selected dashes composite last while their gaps retain the ordinary wire cue");
	std::reverse(selected.begin(), selected.end());
	for (auto &segment : selected)
	{
		std::swap(segment.a, segment.b);
	}
	ok &= expect(renderWireLines({64, 64}, selected, style, &reverse) && image == reverse,
				 "selection colour and dash phase are independent of edge submission direction and order");
	// A nearly diagonal edge may choose another scan axis after subpixel
	// translation or rounding. Its dash anchor must still be the same endpoint.
	for (double ratio : {1.0, 1.25, 1.5, 1.75, 2.0})
	{
		WireStyle diagonalStyle; diagonalStyle.pixelRatio = ratio; diagonalStyle.selectionWidth = 3.6;
		const QSize size(int(120 * ratio), int(120 * ratio));
		const QPointF a(10.125, 90.25), b(90.125, 10.25);
		ok &= expect(renderWireLines(size, {{a, b, true}}, diagonalStyle, &image), "render selected diagonal reference");
		for (double epsilon : {-1e-11, 1e-11})
		{
			ok &= expect(renderWireLines(size, {{a, b + QPointF(0, epsilon), true}}, diagonalStyle, &reverse),
				"render selected diagonal across scan-axis boundary");
			int difference = 0;
			for (int y = 0; y < size.height(); ++y) { for (int x = 0; x < size.width(); ++x) {
				const auto p = image.pixel(x, y), q = reverse.pixel(x, y);
				difference = std::max({difference, std::abs(qRed(p)-qRed(q)), std::abs(qGreen(p)-qGreen(q)),
					std::abs(qBlue(p)-qBlue(q)), std::abs(qAlpha(p)-qAlpha(q))});
			} }
			ok &= expect(difference <= 1, "rounding across a diagonal scan-axis boundary cannot reverse selected dashes");
		}
	}
	int comparisons = 0;
	for (double ratio : {1.0, 1.25, 2.0})
	{
		style.pixelRatio = ratio;
		const QSize size(int(64 * ratio), int(64 * ratio));
		for (double width : {.5, 1.0, 1.6, 2.4, 3.2})
		{
			style.width = width;
			for (int angle = 0; angle < 16; ++angle)
			{
				const double radians = angle * 3.14159265358979323846 / 8;
				const QPointF delta(std::cos(radians) * 27.25, std::sin(radians) * 27.25);
				const QPointF a = QPointF(32.25, 31.75) - delta, b = QPointF(32.25, 31.75) + delta;
				for (bool dashed : {false, true})
				{
					ok &= expect(renderWireLines(size, {{a, b, dashed}}, style, &image) &&
									 renderWireLines(size, {{b, a, dashed}}, style, &reverse) && image == reverse,
								 "antialiasing and selected dashes agree in every octant and display scale");
				}
				// An independently constructed stroke polygon avoids Qt's specialised
				// one-pixel line path and checks the actual geometric stroke area.
				ok &= expect(renderWireLines(size, {{a, b, false}}, style, &image), "render stroke for area comparison");
				QImage reference(size, QImage::Format_ARGB32_Premultiplied);
				reference.fill(Qt::transparent);
				{
					const QPointF unit = delta / std::hypot(delta.x(), delta.y()), normal(-unit.y(), unit.x());
					const QPointF cap = unit * (width * .5), side = normal * (width * .5);
					const std::array<QPointF, 4> corners{a - cap - side, b + cap - side, b + cap + side, a - cap + side};
					QPainter painter(&reference);
					painter.scale(ratio, ratio);
					painter.setRenderHint(QPainter::Antialiasing);
					painter.setPen(Qt::NoPen);
					painter.setBrush(QColor::fromRgba(style.wire));
					painter.drawPolygon(corners.data(), 4);
				}
				qint64 nativeAlpha = 0, referenceAlpha = 0, difference = 0;
				for (int y = 0; y < size.height(); ++y)
				{
					for (int x = 0; x < size.width(); ++x)
					{
						const int n = qAlpha(image.pixel(x, y)), r = qAlpha(reference.pixel(x, y));
						nativeAlpha += n;
						referenceAlpha += r;
						difference += std::abs(n - r);
					}
				}
				const bool agrees = std::abs(nativeAlpha - referenceAlpha) < referenceAlpha * .05 && difference < referenceAlpha * .1;
				ok &= expect(agrees, "wire coverage and alignment agree with an independent antialiased polygon reference");
				if (!agrees)
				{
					std::cerr << "angle=" << angle << " width=" << width << " ratio=" << ratio << " native=" << nativeAlpha
							  << " reference=" << referenceAlpha << " difference=" << difference << '\n';
				}
				++comparisons;
			}
		}
	}
	std::cout << "Independent wire coverage comparisons: " << comparisons << '\n';
	style.pixelRatio = 1;
	style.width = 1;
	ok &= expect(renderWireLines({64, 64}, {{{-1e100, -1e100}, {1e100, 1e100}, false}}, style, &image) &&
					 renderWireLines({64, 64}, {{{-100, -100}, {100, 100}, false}}, style, &reverse) && image == reverse,
				 "far-offscreen diagonal endpoints clip to a bounded, accurate visible line");
	const double nan = std::numeric_limits<double>::quiet_NaN();
	ok &= expect(renderWireLines({64, 64}, {{{nan, 0}, {16, 16}, false}, {{8, 16.5}, {56, 16.5}, false}}, style, &image) &&
					 image.pixel(32, 16) == style.wire,
				 "non-finite segments cannot poison valid wire geometry");
	ok &= expect(!renderWireLines({100000, 100000}, {}, style, &image) && image.isNull(),
				 "wire output obeys the shared pixel-allocation ceiling");
	style.pixelRatio = nan;
	ok &= expect(!renderWireLines({64, 64}, {}, style, &image) && image.isNull(), "invalid render scale fails without stale output");
	style.pixelRatio = 1;
	std::atomic_bool cancelled{true};
	ok &= expect(!renderWireLines({64, 64}, {}, style, &image, &cancelled) && image.isNull(),
				 "wire cancellation is checked before allocation");
	cancelled.store(false);
	QVector<WireSegment> busy(50000, {{-100, 512.5}, {2000, 512.5}, false});
	auto *cancel = QThread::create(
		[&]
		{
			QThread::msleep(10);
			cancelled.store(true);
		});
	QElapsedTimer deadline;
	deadline.start();
	cancel->start();
	const bool finished = renderWireLines({1024, 1024}, busy, style, &image, &cancelled);
	cancel->wait();
	delete cancel;
	ok &=
		expect(!finished && image.isNull() && deadline.elapsed() < 3000, "in-progress wire cancellation discards partial pixels promptly");
	ok &= benchmark(app.arguments().contains(QStringLiteral("--compare-qt")));
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
