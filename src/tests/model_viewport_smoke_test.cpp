#include "app/model_viewport.h"
#include "core/model_document.h"
#include "tests/model_viewport_test_helpers.h"

#include <QApplication>
#include <QDir>
#include <QFont>
#include <QImage>
#include <QThread>
#include <QTranslator>

#include <cstdlib>
#include <iostream>

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

class ExpandedStatus final : public QTranslator
{
  public:
	QString translate(const char *context, const char *source, const char *, int) const override
	{
		if (QByteArray(context) == "vibestudio::ModelViewport" && QString::fromUtf8(source) == QStringLiteral("Rendering model…"))
		{
			return QStringLiteral("Rendering the model preview with an expanded translated status message…");
		}
		return {};
	}
};

ModelMesh crossing(bool perspective)
{
	ModelMesh mesh;
	mesh.format = ModelMeshFormat::Quake3Md3;
	mesh.sourcePath = QStringLiteral("tests/crossing.md3");
	mesh.frames.resize(1);
	mesh.frames[0].name = QStringLiteral("test");
	for (int surface = 0; surface < 2; ++surface)
	{
		ModelSurface part;
		part.name = surface == 0 ? QStringLiteral("red") : QStringLiteral("blue");
		part.triangles = {{0, 1, 2}};
		part.texCoords = {{0, 0}, {1, 0}, {0, 1}};
		ModelFrameGeometry frame;
		if (perspective)
		{
			const float depth[] = {surface == 0 ? 8.0f : 4.0f, surface == 0 ? 2.0f : 4.0f, surface == 0 ? 8.0f : 4.0f};
			// All triangles project to (96,96), (416,96), (96,416).
			frame.positions = {{depth[0], depth[0] * 0.625f, depth[0] * 0.625f},
							   {depth[1], -depth[1] * 0.625f, depth[1] * 0.625f},
							   {depth[2], depth[2] * 0.625f, -depth[2] * 0.625f}};
		}
		else
		{
			frame.positions = {
				{surface == 0 ? 0.0f : 4.0f, -40, 40}, {surface == 0 ? 10.0f : 4.0f, 40, 40}, {surface == 0 ? 0.0f : 4.0f, -40, -40}};
		}
		frame.normals = {{1, 0, 0}, {1, 0, 0}, {1, 0, 0}};
		part.frames.append(frame);
		mesh.surfaces.append(part);
	}
	updateEditableModelMetadata(&mesh);
	return mesh;
}

bool red(const QImage &image, const QPoint &point)
{
	const auto color = image.pixelColor(point);
	return color.red() > 50 && color.red() > color.blue() * 2;
}
} // namespace

int main(int argc, char **argv)
{
	// Direct widget calls and in-process QImage rendering; no input injection or
	// desktop/window captures are used by this integration test.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	const auto savePreview = [&](ModelViewport &preview, const QString &name)
	{
		const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
		if (evidence.isEmpty())
		{
			return true;
		}
		const auto ratio = preview.devicePixelRatioF();
		QImage capture(preview.size() * ratio, QImage::Format_ARGB32_Premultiplied);
		capture.setDevicePixelRatio(ratio);
		preview.render(&capture);
		return QDir().mkpath(evidence) && capture.save(QDir(evidence).filePath(name));
	};
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	bool ok = true;
	ModelViewport viewport;
	viewport.resize(512, 512);
	viewport.setBackfaceCulling(false);
	viewport.setShowGrid(false);
	viewport.setShowAxes(false);
	QImage redSkin(4, 4, QImage::Format_RGB32), blueSkin(4, 4, QImage::Format_RGB32);
	redSkin.fill(Qt::red);
	blueSkin.fill(Qt::blue);
	viewport.setRenderMode(ModelViewportRenderMode::Textured);
	for (bool perspective : {false, true})
	{
		viewport.setMesh(crossing(perspective));
		CameraViewControls controls;
		controls.perspective = perspective;
		viewport.setCameraControls(controls);
		if (perspective)
		{
			viewport.setCameraView({0, 0, 0}, 0, 0);
		}
		else
		{
			viewport.setOrbit(0, 0);
		}
		viewport.setSurfaceSkins({{0, redSkin}, {1, blueSkin}});
		viewport.show();
		app.processEvents();
		const QPoint rear = perspective ? QPoint(120, 150) : QPoint(int(256 - 30 * viewport.zoom()), int(256 - 20 * viewport.zoom()));
		const QPoint front = perspective ? QPoint(370, 130) : QPoint(int(256 + 20 * viewport.zoom()), int(256 - 30 * viewport.zoom()));
		QImage image(viewport.size(), QImage::Format_RGB32);
		ok &= expect(tests::settleModelViewport(viewport), "camera preview worker completes");
		viewport.render(&image);
		ok &= expect(!red(image, rear) && red(image, front), "both camera projections draw intersecting surfaces at their actual depth");
		ok &= expect(viewport.hitAt(rear).surface == 1 && viewport.hitAt(front).surface == 0, "widget hit query follows visible depth");
		const QPointF nearEdge = perspective ? QPointF(130, 97) : QPointF(256 - 30 * viewport.zoom(), 256 - 40 * viewport.zoom() + 1);
		const auto edge = viewport.edgeAt(nearEdge);
		ok &= expect(edge.valid && edge.surface == 1 && edge.a == 0 && edge.b == 1 && !viewport.edgeAt(rear, 0.25).valid,
					 "edge picking follows the visible face in both camera projections and respects its distance tolerance");
		viewport.setHighlightedEdges(0, {{0, 1}});
		ok &= expect(viewport.highlightedEdgeCount() == 1 && tests::settleModelViewport(viewport),
					 "selected indexed edge completes its raster update");
		viewport.render(&image);
		ok &= expect(!red(image, rear), "hidden selected edges do not draw through the front surface");
		viewport.setHighlightedEdges(-1, {});
		const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
		if (!evidence.isEmpty())
		{
			QDir().mkpath(evidence);
			ok &= expect(image.save(QDir(evidence).filePath(perspective ? QStringLiteral("intersection-perspective.png")
																		: QStringLiteral("intersection-orthographic.png"))),
						 "save viewport-owned render evidence");
		}
		viewport.setHighlightedTriangles({0});
		ok &= expect(tests::settleModelViewport(viewport), "selection preview worker completes");
		viewport.render(&image);
		ok &= expect(!red(image, rear), "selected mesh remains occluded by foreground geometry");
		viewport.setHighlightedTriangles({});
	}
	auto clipped = crossing(true);
	clipped.surfaces.resize(1);
	clipped.surfaces[0].frames[0].positions = {{0.5f, 0, 1}, {4, 2, -2}, {4, -2, -2}};
	clipped.surfaces[0].texCoords = {{0.5f, 0}, {0, 1}, {1, 1}};
	updateEditableModelMetadata(&clipped);
	viewport.setMesh(clipped);
	viewport.setCameraView({0, 0, 0}, 0, 0);
	viewport.setSkin(redSkin);
	QImage image(viewport.size(), QImage::Format_RGB32);
	ok &= expect(tests::settleModelViewport(viewport), "near-clipped preview worker completes");
	viewport.render(&image);
	ok &= expect(red(image, {256, 250}) && viewport.hitAt({256, 250}).surface == 0,
				 "near-plane clipping retains texture coordinates and picking");
	const auto unselected = image.copy();
	ok &= expect(!viewport.edgeAt({256, 112}).valid && viewport.edgeAt({158, 246}).valid && viewport.edgeAt({158, 246}).a == 0 &&
					 viewport.edgeAt({158, 246}).b == 1,
				 "near-plane cut is not a model edge while clipped original edges remain pickable");
	viewport.setHighlightedEdges(0, {{0, 1}, {0, 2}, {1, 2}});
	ok &= expect(tests::settleModelViewport(viewport), "clipped edge highlighting completes");
	viewport.render(&image);
	const auto differences = [&](QRect region)
	{
		int changed = 0;
		for (int y = region.top(); y <= region.bottom(); ++y)
		{
			for (int x = region.left(); x <= region.right(); ++x)
			{
				changed += image.pixel(x, y) != unselected.pixel(x, y);
			}
		}
		return changed;
	};
	ok &= expect(differences({230, 110, 50, 5}) == 0 && differences({220, 220, 70, 60}) == 0 && differences({149, 239, 18, 12}) > 4,
				 "selected original edges remain visible without highlighting the clipping boundary or internal fan diagonal");
	viewport.setRenderMode(ModelViewportRenderMode::Wireframe);
	ok &= expect(tests::settleModelViewport(viewport), "clipped selected wireframe finishes");
	viewport.render(&image);
	const auto orangePixels = [&](QRect area)
	{
		int count = 0;
		for (int y = area.top(); y <= area.bottom(); ++y)
		{
			for (int x = area.left(); x <= area.right(); ++x)
			{
				const auto color = image.pixelColor(x, y);
				count += color.red() > 100 && color.red() > color.green() * 1.25 && color.red() > color.blue() * 2;
			}
		}
		return count;
	};
	ok &= expect(orangePixels({230, 107, 50, 7}) == 0 && orangePixels({149, 239, 18, 16}) > 3 &&
					 image.pixelColor(256, 250) == QColor(20, 22, 27),
				 "wireframe preserves clipped source edges without a selected clipping cut, fan diagonal or filled interior");
	ok &= expect(savePreview(viewport, QStringLiteral("wireframe-clipped-selection.png")), "save clipped wireframe evidence");
	viewport.setRenderMode(ModelViewportRenderMode::Textured);
	for (auto &point : clipped.surfaces[0].frames[0].positions)
	{
		point.x = 0.5f;
	}
	updateEditableModelMetadata(&clipped);
	viewport.setMesh(clipped);
	viewport.setCameraView({0, 0, 0}, 0, 0);
	ok &= expect(tests::settleModelViewport(viewport), "fully clipped preview retires prior work");
	viewport.render(&image);
	ok &= expect(!viewport.hitAt({256, 250}).valid && !red(image, {256, 250}),
				 "fully clipped triangles leave no stale render or pick result");
	viewport.setMesh(crossing(true));
	viewport.setCameraView({0, 0, 0}, 0, 0);
	viewport.render(&image);
	ok &= expect(viewport.isRendering(), "paint queues raster work and returns without waiting for the worker");
	for (int i = 0; i < 16; ++i)
	{
		viewport.setSkin(i % 2 == 0 ? redSkin : blueSkin);
		viewport.setCameraView({0, 0, 0}, i % 2, 0);
		viewport.render(&image);
	}
	int runningThreads = 0;
	for (auto *thread : viewport.findChildren<QThread *>())
	{
		runningThreads += thread->isRunning() ? 1 : 0;
	}
	ok &= expect(runningThreads <= 1 && viewport.isRendering(), "rapid changes keep at most one render worker active");
	viewport.setCameraView({0, 0, 0}, 0, 0);
	ok &= expect(tests::settleModelViewport(viewport), "latest requested preview eventually completes after cancellation");
	viewport.render(&image);
	ok &= expect(!red(image, {370, 130}), "retired renders cannot overwrite the latest blue material");
	viewport.setSkin(redSkin);
	viewport.render(&image);
	viewport.clearMesh();
	app.processEvents();
	ok &= expect(!viewport.hitAt({256, 250}).valid, "clearing the mesh clears picking data");
	{
		ModelMesh sheet;
		sheet.geometryAvailable = true;
		sheet.format = ModelMeshFormat::Quake3Md3;
		sheet.sourcePath = QStringLiteral("tests/wire-sheet.md3");
		sheet.frames.resize(1);
		sheet.frames[0].name = QStringLiteral("sheet");
		ModelSurface surface;
		surface.name = QStringLiteral("sheet");
		surface.triangles = {{0, 1, 2}, {1, 3, 2}};
		surface.texCoords = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
		surface.frames.append({{{-40, -40, 0}, {40, -40, 0}, {-40, 40, 0}, {40, 40, 0}}, {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}}});
		sheet.surfaces.append(surface);
		updateEditableModelMetadata(&sheet);
		ok &= expect(validateEditableModel(sheet).isEmpty() && sheet.vertexCount == 4,
					 "wireframe selection fixture is a valid editable mesh");
		ModelViewport wire;
		wire.resize(512, 512);
		wire.setShowGrid(false);
		wire.setShowAxes(false);
		wire.setBackfaceCulling(false);
		wire.setRenderMode(ModelViewportRenderMode::Wireframe);
		wire.setMesh(sheet);
		wire.setOrbit(0, 90);
		wire.setHighlightedTriangles({0});
		ok &= expect(tests::settleModelViewport(wire), "selected shared-edge wireframe completes");
		QImage first(wire.size(), QImage::Format_ARGB32_Premultiplied), second = first;
		wire.render(&first);
		std::swap(sheet.surfaces[0].triangles[0], sheet.surfaces[0].triangles[1]);
		wire.setMesh(sheet);
		wire.setOrbit(0, 90);
		wire.setHighlightedTriangles({1});
		ok &= expect(tests::settleModelViewport(wire), "reordered selected wireframe completes");
		wire.render(&second);
		ok &= expect(first.copy(50, 70, 412, 392) == second.copy(50, 70, 412, 392),
					 "unselected neighbouring faces cannot cover the selected wire's shared edge");
		wire.setHighContrast(true);
		ok &= expect(tests::settleModelViewport(wire), "high-visibility wireframe completes");
		wire.render(&second);
		int selectedPixels = 0;
		for (int y = 70; y < 462; ++y)
		{
			for (int x = 50; x < 462; ++x)
			{
				const auto color = second.pixelColor(x, y);
				selectedPixels += color.red() > 180 && color.blue() > 180 && color.green() < 80;
			}
		}
		ok &= expect(selectedPixels > 100 && second.pixelColor(256, 360) == QColor(Qt::black),
					 "high-visibility wireframe retains selected edge cues and an unfilled interior");
		ok &= expect(savePreview(wire, QStringLiteral("wireframe-high-visibility.png")), "save high-visibility wireframe evidence");
		ExpandedStatus expansion;
		app.installTranslator(&expansion);
		QFont expandedFont = wire.font();
		expandedFont.setPointSizeF(expandedFont.pointSizeF() * 2);
		wire.setFont(expandedFont);
		wire.resize(280, 420);
		wire.setLayoutDirection(Qt::RightToLeft);
		wire.setOrbit(15, 75);
		ok &= expect(savePreview(wire, QStringLiteral("wireframe-expanded-pending.png")), "save expanded rendering-status evidence");
		ok &= expect(tests::settleModelViewport(wire), "expanded viewport remains usable after rendering");
		app.removeTranslator(&expansion);
	}
	std::cout << "Viewport device pixel ratio: " << viewport.devicePixelRatioF() << '\n';
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
