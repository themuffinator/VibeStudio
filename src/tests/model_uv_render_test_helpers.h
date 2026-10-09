#pragma once

#include "app/model_uv_fill.h"
#include "app/wire_lines.h"
#include "app/model_uv_render.h"
#include <QDir>
#include <QElapsedTimer>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace vibestudio::tests::uvRender
{
inline int checks = 0;
inline bool expect(bool value, const char *message)
{
	++checks;
	if (!value)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return value;
}
inline ModelSurface grid(int columns, int rows)
{
	ModelSurface s;
	s.name = QStringLiteral("render_grid");
	s.vertexCount = columns * rows;
	for (int y = 0; y < rows; ++y)
	{
		for (int x = 0; x < columns; ++x)
		{
			s.texCoords.append({float(x) / (columns - 1), float(y) / (rows - 1)});
			if (x + 1 < columns && y + 1 < rows)
			{
				const int a = y * columns + x, b = a + 1, c = a + columns;
				s.triangles << ModelTriangle{a, b, c} << ModelTriangle{b, c + 1, c};
			}
		}
	}
	return s;
}
inline QSet<int> allFaces(const ModelSurface &s)
{
	QSet<int> result;
	for (int i = 0; i < s.triangles.size(); ++i)
	{
		result.insert(i);
	}
	return result;
}
inline bool selectionOracle(const ModelSurface &surface, const QSet<int> &faces, ModelTexCoord offset = {})
{
	const QTransform camera(170, 0, 0, 150, -19, 17);
	const QRectF clip(7, 9, 139, 134);
	QPainterPath path;
	QString error;
	if (!expect(buildModelUvSelectionPath(surface, faces, camera, offset, clip, &path, &error), "selection contours build"))
	{
		return false;
	}
	// Independent point-in-triangle signs in original UV space. No topology,
	// contour traversal, winding fill or production clipping code is reused.
	const auto cross = [](QPointF a, QPointF b) { return a.x() * b.y() - a.y() * b.x(); };
	const auto uv = [&](int i) {
		const auto p = surface.texCoords[i];
		return QPointF(p.u, p.v);
	};
	const auto inverse = camera.inverted();
	bool agrees = true;
	for (int y = 0; y < 73; ++y)
	{
		for (int x = 0; x < 81; ++x)
		{
			const QPointF p(x * 2.017 + .313, y * 2.071 + .719);
			const auto q = inverse.map(p) - QPointF(offset.u, offset.v);
			bool inside = false, boundary = false;
			for (int face : faces)
			{
				if (face < 0 || face >= surface.triangles.size())
				{
					continue;
				}
				const auto t = surface.triangles[face];
				const auto a = uv(t.a), b = uv(t.b), c = uv(t.c);
				if (cross(b - a, c - a) == 0)
				{
					continue;
				}
				const double e0 = cross(b - a, q - a), e1 = cross(c - b, q - b), e2 = cross(a - c, q - c);
				boundary |= std::min({std::abs(e0), std::abs(e1), std::abs(e2)}) < 1e-10;
				inside |= (e0 > 0 && e1 > 0 && e2 > 0) || (e0 < 0 && e1 < 0 && e2 < 0);
			}
			if (!boundary && path.contains(p) != (inside && clip.contains(p)))
			{
				if (agrees)
				{
					std::cerr << "coverage differs at " << p.x() << "," << p.y() << '\n';
				}
				agrees = false;
			}
		}
	}
	return expect(agrees, "selection union agrees with independent triangle coverage inside and outside the clip");
}
inline bool contourChecks()
{
	bool ok = true;
	auto s = grid(7, 7);
	ok &= selectionOracle(s, allFaces(s));
	QSet<int> ring, checker;
	for (int i = 0; i < s.triangles.size(); ++i)
	{
		const int cell = i / 2, x = cell % 6, y = cell / 6;
		if (x == 0 || y == 0 || x == 5 || y == 5)
		{
			ring.insert(i);
		}
		if ((x + y) % 2 == 0)
		{
			checker.insert(i);
		}
	}
	ok &= selectionOracle(s, ring);
	ok &= selectionOracle(s, checker, {.125f, -.0625f});
	for (int i = 0; i < s.triangles.size(); i += 3)
	{
		std::swap(s.triangles[i].b, s.triangles[i].c);
	}
	ok &= selectionOracle(s, allFaces(s));
	ok &= selectionOracle(s, ring);
	for (auto &uv : s.texCoords)
	{
		uv.u = std::abs(uv.u - .5f) * 2;
	}
	ok &= selectionOracle(s, allFaces(s));
	ok &= selectionOracle(s, checker);
	s.vertexCount = 7;
	s.texCoords = {{.1f, .1f}, {.9f, .1f}, {.1f, .9f}, {.1f, .1f}, {.1f, .9f}, {.9f, .1f}, {.7f, .7f}};
	s.triangles = {{0, 1, 2}, {3, 4, 5}, {0, 1, 6}, {1, 0, 4}};
	ok &= selectionOracle(s, allFaces(s)); // duplicate, opposite winding, nonmanifold and partial overlap
	s.texCoords = {{-1000000, -1000000}, {1000000, -1000000}, {0, 1000000}};
	s.triangles = {{0, 1, 2}};
	s.vertexCount = 3;
	ok &= selectionOracle(s, {0});
	s.texCoords = {{0, 0}, {.5f, .5f}, {1, 1}};
	ok &= selectionOracle(s, {0});
	for (const auto dimensions : {QSize(256, 256), QSize(32768, 2)})
	{
		s = grid(dimensions.width(), dimensions.height());
		QPainterPath path;
		QString error;
		ok &= expect(buildModelUvSelectionPath(s, allFaces(s), QTransform(500, 0, 0, 500, 10, 10), {}, {0, 0, 600, 600}, &path, &error) &&
						 path.elementCount() <= 6 && path.contains({250, 250}) && !path.contains({550, 550}),
					 "maximum selected grids retain exact fill coverage with a compact boundary");
		const auto saved = path;
		int polls = 0;
		ModelWorkControl cancel;
		cancel.cancelled = [&] { return ++polls >= 40; };
		ok &= expect(!buildModelUvSelectionPath(s, allFaces(s), {}, {}, {0, 0, 1, 1}, &path, &error, cancel) && path == saved &&
						 !error.isEmpty(),
					 "cancelled contour preparation publishes no partial path");
	}
	return ok;
}
inline ModelUvRenderRequest crossing()
{
	ModelUvRenderRequest r;
	r.surface.vertexCount = 6;
	r.surface.texCoords = {{.05f, .5f}, {.95f, .5f}, {.05f, .9f}, {.5f, .05f}, {.5f, .95f}, {.9f, .05f}};
	r.surface.triangles = {{0, 1, 2}, {3, 4, 5}};
	r.logicalSize = {220, 220};
	r.camera = QTransform(200, 0, 0, 200, 10, 10);
	r.fit = ModelUvFit::None;
	r.background = QColor(35, 40, 50);
	r.foreground = Qt::white;
	r.accent = QColor(255, 170, 60);
	return r;
}
inline bool save(const QImage &image, const QString &name)
{
	const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	return evidence.isEmpty() || image.save(QDir(evidence).filePath(name + QStringLiteral(".png")));
}
inline bool renderChecks()
{
	bool ok = true;
	QString error;
	ModelUvRenderResult result;
	for (int ratio : {1, 2})
	{
		auto ring = crossing();
		ring.surface = grid(7, 7);
		ring.logicalSize = {640, 480};
		ring.fit = ModelUvFit::All;
		ring.pixelRatio = ratio;
		ModelUvRenderResult ordinary;
		ok &= expect(renderModelUv(ring, &ordinary, &error), "ordinary ring fixture renders");
		for (int face = 0; face < ring.surface.triangles.size(); ++face)
		{
			const int cell = face / 2, x = cell % 6, y = cell / 6;
			if (x == 0 || y == 0 || x == 5 || y == 5)
			{
				ring.selection.faces.insert(face);
			}
		}
		ok &= expect(renderModelUv(ring, &result, &error), "selected ring fixture renders");
		const QRect hole = result.camera.mapRect(QRectF(.4, .4, .2, .2)).toAlignedRect();
		bool clear = true;
		for (int y = hole.top() * ratio; y < hole.bottom() * ratio; ++y)
		{
			for (int x = hole.left() * ratio; x < hole.right() * ratio; ++x)
			{
				clear &= result.image.pixel(x, y) == ordinary.image.pixel(x, y);
			}
		}
		ok &= expect(clear && result.image != ordinary.image, "ring hatching preserves the unselected hole");
		ok &= expect(save(result.image, QStringLiteral("uv-selection-ring-%1x").arg(ratio)), "save ring evidence");
	}
	for (double ratio : {1.0, 1.25, 1.5, 2.0})
	{
		auto r = crossing();
		r.pixelRatio = ratio;
		r.surface.uvSeams = {{0, 1}};
		ok &= expect(renderModelUv(r, &result, &error), "crossing seam render succeeds");
		const auto pixel = [&](double x, double y) { return result.image.pixelColor(int(x * ratio), int(y * ratio)); };
		ok &= expect(pixel(110, 110) == QColor(255, 115, 225), "seam remains above later ordinary crossing edges");
		r.selection.edges = {{0, 1}};
		r.surface.uvSeams = {{3, 4}};
		ok &= expect(renderModelUv(r, &result, &error) && pixel(110, 110) == r.accent,
					 "selected edge remains above later seam and ordinary edges");
		ok &=
			expect(pixel(32, 110) != r.accent && pixel(24, 110) == r.accent, "selected edges have visible dash gaps at every device scale");
		ok &= expect(save(result.image, QStringLiteral("uv-overlay-%1x").arg(ratio)), "save overlay evidence");
		r.surface.texCoords[0] = r.surface.texCoords[3] = {.5f, .5f};
		r.selection = {};
		r.selection.vertices = {0};
		r.showVertices = true;
		ok &= expect(renderModelUv(r, &result, &error) && pixel(110, 110) == r.accent,
					 "selected UV marker remains visible above a coincident later unselected marker");
	}
	auto r = crossing();
	r.surface = grid(2, 2);
	r.selection.faces = {0};
	r.moving = true;
	r.moveOffset = {.125f, .0625f};
	ModelUvRenderResult moving, detached;
	ok &= expect(renderModelUv(r, &moving, &error), "selected face movement renders");
	auto manual = r;
	manual.moving = false;
	for (int i = 0; i < 3; ++i)
	{
		auto uv = manual.surface.texCoords[i];
		uv.u += r.moveOffset.u;
		uv.v += r.moveOffset.v;
		manual.surface.texCoords.append(uv);
	}
	manual.surface.triangles[0] = {4, 5, 6};
	manual.surface.vertexCount = 7;
	ok &= expect(renderModelUv(manual, &detached, &error), "explicitly detached face renders");
	int difference = 0;
	for (int y = 0; y < moving.image.height(); ++y)
	{
		for (int x = 0; x < moving.image.width(); ++x)
		{
			const auto a = moving.image.pixel(x, y), b = detached.image.pixel(x, y);
			difference = std::max({difference, std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b)),
								   std::abs(qAlpha(a) - qAlpha(b))});
		}
	}
	// Reindexing changes the composition order at shared antialiased caps; a
	// one-level channel rounding difference is allowed, displaced geometry is not.
	ok &= expect(difference <= 1, "face move preview equals explicit corner isolation and retains the fixed shared edge");
	std::cout << "Move preview maximum channel difference: " << difference << '\n';
	ok &= save(moving.image, QStringLiteral("uv-face-move"));
	ok &= save(detached.image, QStringLiteral("uv-face-isolated"));
	r = crossing();
	r.surface.vertexCount = 3;
	r.surface.texCoords = {{999999.5f, 999999.5f}, {999999.75f, 999999.5f}, {999999.5f, 999999.75f}};
	r.surface.triangles = {{0, 1, 2}};
	r.camera = QTransform(400, 0, 0, 400, -399999790, -399999790);
	r.selection.faces = {0};
	r.moving = true;
	r.moveOffset = {.04f, .04f};
	manual = r;
	manual.moving = false;
	for (auto &uv : manual.surface.texCoords)
	{
		uv.u += r.moveOffset.u;
		uv.v += r.moveOffset.v;
	}
	ok &= expect(renderModelUv(r, &moving, &error) && renderModelUv(manual, &detached, &error) && moving.image == detached.image,
				 "fill and wires use the same representable moved corners at extreme valid UV coordinates");
	r = crossing();
	const auto originalUVs = r.surface.texCoords;
	for (const QSize size : {QSize(1, std::numeric_limits<int>::max()), QSize(std::numeric_limits<int>::max(), 1), QSize(16384, 16384)})
	{
		r.logicalSize = size;
		r.pixelRatio = 4;
		ok &= expect(renderModelUv(r, &result, &error) && qint64(result.image.width()) * result.image.height() <= 4 * 1024 * 1024 &&
						 result.image.width() > 0 && result.image.height() > 0,
					 "UV allocation remains bounded at extreme aspect ratios");
	}
	r = crossing();
	r.selection.faces = {0};
	int polls = 0;
	ModelWorkControl control;
	control.cancelled = [&] {
		++polls;
		return false;
	};
	ok &= expect(renderModelUv(r, &result, &error, control), "count complete cancellation checkpoints");
	const int total = polls;
	const auto saved = result.image.cacheKey();
	for (int stop : {1, total / 2, total})
	{
		polls = 0;
		control.cancelled = [&] { return ++polls >= stop; };
		ok &= expect(!renderModelUv(r, &result, &error, control) && result.image.cacheKey() == saved && !error.isEmpty(),
					 "early, drawing and final cancellation retain the previous complete frame");
	}
	for (double invalid : {0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
	{
		r.pixelRatio = invalid;
		ok &= expect(!renderModelUv(r, &result, &error) && result.image.cacheKey() == saved, "invalid pixel ratio publishes nothing");
	}
	ok &= expect(std::equal(r.surface.texCoords.cbegin(), r.surface.texCoords.cend(), originalUVs.cbegin(), originalUVs.cend(),
							[](const ModelTexCoord &a, const ModelTexCoord &b) { return a.u == b.u && a.v == b.v; }),
				 "preview work preserves original UV coordinates");
	return ok;
}
inline bool tileChecks()
{
	bool ok = true;
	for (double ratio : {1.0, 1.25, 2.0})
	{
		for (double dash : {0.0, 1.0, 4.0})
		{
			WireStyle style;
			style.pixelRatio = ratio;
			style.width = 3;
			style.selectionWidth = 2.5;
			style.wire = qRgb(171, 129, 73);
			style.selection = qRgb(47, 173, 199);
			style.dashLength = dash;
			QVector<WireSegment> lines;
			for (int i = 0; i < 1600; ++i)
			{
				const double x = i * .08 - 10;
				lines.append({{x, -30}, {x + 9.7, 130}, i % 3 == 0});
			}
			QImage batched(QSize(int(96 * ratio), int(96 * ratio)), QImage::Format_ARGB32_Premultiplied);
			batched.fill(qRgba(13, 7, 20, 71));
			QImage scalar = batched;
			ok &= expect(paintWireLines(&batched, lines, style), "dense layered wire paint succeeds");
			for (bool selected : {false, true})
			{
				for (const auto &line : lines)
				{
					if (line.selected == selected)
					{
						ok &= paintWireLines(&scalar, {line}, style);
					}
				}
			}
			ok &= expect(batched == scalar, "opaque tile reuse is pixel-exact against unbatched coverage, dash and alpha composition");
		}
	}
	WireStyle style;
	QImage image(1, 4 * 1024 * 1024, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	int polls = 0;
	QElapsedTimer timer;
	timer.start();
	ok &= expect(!paintWireLines(&image, {{{.5, 0}, {.5, 4 * 1024 * 1024}, false}}, style, nullptr, [&] { return ++polls == 20; }) &&
					 polls == 20 && timer.elapsed() < 1000,
				 "cancellation callback interrupts the interior of one long stroke promptly");
	for (double invalid : {-1.0, 65.0, std::numeric_limits<double>::quiet_NaN()})
	{
		style.dashLength = invalid;
		ok &= expect(!paintWireLines(&image, {}, style), "invalid dash length is rejected before drawing");
	}
	return ok;
}
} // namespace vibestudio::tests::uvRender
