#include "app/model_viewport.h"
#include "core/model_document.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/model_scale_test_helpers.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <cstdlib>
#include <iostream>
#include <numeric>

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

} // namespace

int main(int argc, char **argv)
{
	// Semantic widget calls and QWidget::render only: no OS input or capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	bool ok = true;
	const auto mesh = tests::maximumEditableGrid();
	const auto errors = validateEditableModel(mesh);
	ok &= expect(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
	ok &= expect(mesh.vertexCount == modelDocumentMaxVertices && mesh.triangleCount == 130050 &&
					 qint64(mesh.vertexCount) * mesh.frames.size() == modelDocumentMaxFrameVertices,
				 "fixture reaches authoring limits");
	if (!ok)
	{
		return EXIT_FAILURE;
	}
	ModelViewport viewport;
	viewport.resize(1024, 768);
	viewport.setShowGrid(false);
	viewport.setShowAxes(false);
	viewport.setOrbit(0, 90);
	viewport.show();
	app.processEvents();
	const int requestedScale = qEnvironmentVariableIntValue("QT_SCALE_FACTOR");
	if (requestedScale > 0)
	{
		ok &= expect(qFuzzyCompare(viewport.devicePixelRatioF(), double(requestedScale)), "requested device pixel ratio is active");
	}
	std::cout << QJsonDocument(QJsonObject{{"vertices", mesh.vertexCount},
										   {"triangles", mesh.triangleCount},
										   {"frames", int(mesh.frames.size())},
										   {"devicePixelRatio", viewport.devicePixelRatioF()}})
					 .toJson(QJsonDocument::Compact)
					 .constData()
			  << std::endl;
	QJsonArray timings;
	const auto measure = [&](const char *name, auto operation)
	{
		QElapsedTimer clock;
		clock.start();
		qint64 previous = 0, maximumGap = 0;
		int beats = 0;
		const auto beat = [&]
		{
			const auto now = clock.nsecsElapsed();
			maximumGap = std::max(maximumGap, now - previous);
			previous = now;
			++beats;
		};
		QTimer heartbeat;
		heartbeat.setTimerType(Qt::PreciseTimer);
		QObject::connect(&heartbeat, &QTimer::timeout, &app, beat);
		heartbeat.start(5);
		operation();
		const double callMs = clock.nsecsElapsed() / 1e6;
		ok &= expect(tests::settleModelViewport(viewport, 60000), name);
		beat();
		const double settledMs = clock.nsecsElapsed() / 1e6;
		const QJsonObject record{{"stage", QString::fromLatin1(name)},
								 {"callMs", callMs},
								 {"settledMs", settledMs},
								 {"maxEventGapMs", maximumGap / 1e6},
								 {"heartbeats", beats}};
		const int maximumAllowed = qEnvironmentVariableIntValue("VIBESTUDIO_MODELLER_MAX_EVENT_GAP_MS");
		if (maximumAllowed > 0)
		{
			ok &= expect(maximumGap / 1e6 <= maximumAllowed, "configured event-loop latency budget");
		}
		const int wireframeLimit = qEnvironmentVariableIntValue("VIBESTUDIO_MODELLER_MAX_WIREFRAME_MS");
		if (wireframeLimit > 0 && QByteArray(name).startsWith("wireframe"))
		{
			ok &= expect(settledMs <= wireframeLimit, "configured completed-wireframe latency budget, including all-face selection");
		}
		timings.append(record);
		std::cout << QJsonDocument(record).toJson(QJsonDocument::Compact).constData() << std::endl;
	};
	measure("set-mesh",
			[&]
			{
				viewport.setMesh(mesh);
				viewport.setOrbit(0, 90);
			});
	const QPointF point(512.25, 384.25);
	measure("pick-16",
			[&]
			{
				for (int i = 0; i < 16; ++i)
				{
					ok &= expect(viewport.hitAt(point + QPointF(i, 0)).valid, "maximum model is pickable");
				}
			});
	measure("orbit", [&] { viewport.setOrbit(25, 65); });
	measure("frame", [&] { viewport.setFrame(15); });
	measure("selection", [&] { viewport.setHighlightedTriangles({65024}); });
	QImage skin(8, 8, QImage::Format_ARGB32_Premultiplied);
	skin.fill(Qt::red);
	measure("textured",
			[&]
			{
				viewport.setSkin(skin);
				viewport.setRenderMode(ModelViewportRenderMode::Textured);
			});
	QImage capture(viewport.size(), QImage::Format_ARGB32_Premultiplied);
	viewport.render(&capture);
	ok &= expect(capture.pixelColor(512, 384).red() > capture.pixelColor(512, 384).blue() * 2,
				 "maximum scene retains the selected skin at its centre");
	measure("wireframe", [&] { viewport.setRenderMode(ModelViewportRenderMode::Wireframe); });
	measure("wireframe-orbit", [&] { viewport.setOrbit(0, 90); });
	if (const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE"); !evidence.isEmpty())
	{
		const auto ratio = viewport.devicePixelRatioF();
		QImage wireCapture(viewport.size() * ratio, QImage::Format_ARGB32_Premultiplied);
		wireCapture.setDevicePixelRatio(ratio);
		viewport.render(&wireCapture);
		ok &= expect(QDir().mkpath(evidence) && wireCapture.save(QDir(evidence).filePath(QStringLiteral("maximum-wireframe.png"))),
					 "save dense wireframe evidence");
	}
	QVector<int> everyFace(mesh.triangleCount);
	std::iota(everyFace.begin(), everyFace.end(), 0);
	measure("wireframe-select-all", [&] { viewport.setHighlightedTriangles(everyFace); });
	viewport.setHighlightedTriangles({65024});
	measure("flat", [&] { viewport.setRenderMode(ModelViewportRenderMode::FlatShaded); });
	measure("coalesced-camera-pose",
			[&]
			{
				for (int i = 0; i < 12; ++i)
				{
					viewport.setOrbit(i * 4, 65);
					viewport.setFrame(i);
					viewport.render(&capture);
					ok &= expect(!viewport.hitAt(point).valid, "pending projection cannot select stale faces");
				}
				viewport.setOrbit(0, 90);
				viewport.setFrame(15);
			});
	ok &= expect(viewport.hitAt(point).valid, "latest camera and pose replace coalesced work");
	measure("retire-during-projection",
			[&]
			{
				viewport.setOrbit(25, 65);
				viewport.render(&capture);
				viewport.clearMesh();
				viewport.setMesh(mesh);
				viewport.setOrbit(0, 90);
			});
	ok &= expect(viewport.hitAt(point).valid, "a retired mesh cannot replace its successor");
	viewport.render(&capture);
	const auto a = viewport.vertexScreenPosition(0, 0), b = viewport.vertexScreenPosition(0, 65535);
	const auto interior = QRectF(a, b).normalized().adjusted(2, 2, -2, -2).toRect();
	int holes = 0;
	for (int y = interior.top(); y <= interior.bottom(); ++y)
	{
		for (int x = interior.left(); x <= interior.right(); ++x)
		{
			holes += capture.pixelColor(x, y).red() < 40;
		}
	}
	ok &= expect(holes == 0, "dense shared triangles leave no background gaps in the model interior");
	const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (!evidence.isEmpty())
	{
		viewport.render(&capture);
		ok &= expect(QDir().mkpath(evidence) && capture.save(QDir(evidence).filePath(QStringLiteral("maximum-grid.png"))),
					 "save viewport-owned maximum-scene evidence");
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
