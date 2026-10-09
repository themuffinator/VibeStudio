#include "app/model_viewport.h"
#include "core/model_document.h"
#include "tests/model_mdl_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/render_test_support.h"

#include <QApplication>
#include <QDir>
#include <QFont>
#include <QJsonDocument>
#include <QLineF>
#include <QSignalBlocker>
#include <QThread>
#include <QTimer>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

using namespace vibestudio;
namespace
{
bool expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return condition;
}
QByteArray source(const ModelMesh &mesh) { return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact); }
bool coreChecks(const ModelMesh &mesh)
{
	bool ok = true;
	QString error;
	ModelMdlPlayback request;
	ModelMdlPlaybackSample sample;
	const auto check = [&](double time, int pose, int skin)
	{
		request.seconds = time;
		return sampleModelMdl(mesh, request, &sample, &error) && sample.frame == pose && sample.skinMember == skin;
	};
	const double first = mesh.mdl.frameGroups[0].intervals[0], end = mesh.mdl.frameGroups[0].intervals[1];
	ok &= expect(check(0, 0, 0) && check(std::nextafter(first, 0.), 0, 0) && check(first, 1, 1) && check(.25, 1, 0) && check(end, 0, 0),
				 "stored intervals use strict end boundaries and independent skin/pose loops");
	request.seconds = first;
	request.syncPhase = .16;
	ok &= expect(sampleModelMdl(mesh, request, &sample) && sample.frame == 1 && sample.skinMember == 0,
				 "software random-sync phase affects both independent cycles");
	auto synchronized = mesh;
	synchronized.mdl.syncType = 0;
	ok &= expect(sampleModelMdl(synchronized, request, &sample) && sample.frame == 1 && sample.skinMember == 1,
				 "synchronized software models ignore the supplied entity phase");
	request.timing = ModelMdlTiming::GlQuake;
	request.seconds = first * 2;
	ok &= expect(sampleModelMdl(mesh, request, &sample) && sample.frame == 0 && sample.skinMember == 0 && sample.frameCycle == first * 2,
				 "GLQuake loops using the first interval and ignores entity phase");
	const int expected[7][4]{{0, 0, 0, 0}, {0, 1, 0, 1}, {0, 1, 2, 0}, {0, 1, 2, 3}, {4, 1, 2, 3}, {4, 5, 2, 3}, {4, 5, 6, 3}};
	for (int count = 1; count <= 7; ++count)
	{
		auto native = mesh;
		auto &skin = native.embeddedSkins[0];
		skin.indexedFrames.fill(skin.indexedFrames[0], count);
		skin.intervals.clear();
		for (int i = 0; i < count; ++i)
		{
			skin.intervals.append(float(i + 1) * .35f);
		}
		for (int slot = 0; slot < 4; ++slot)
		{
			request.seconds = slot * .1 + .01;
			ok &= expect(sampleModelMdl(native, request, &sample) && sample.skinMember == expected[count - 1][slot],
						 "GLQuake's four slots repeat short groups and retain last modulo-four members of long groups");
		}
		for (int tick : {0, 1, 2, 3, 4, 6, 7, 8, 12, 46, 99, 12345})
		{
			request.seconds = tick / 10.;
			ok &= expect(sampleModelMdl(native, request, &sample) && sample.skinMember == expected[count - 1][tick & 3],
						 "GLQuake exact tick boundaries stay correct after repeated cycles");
		}
	}
	request.nativeFrame = 1;
	request.seconds = .11;
	ok &= expect(sampleModelMdl(mesh, request, &sample) && sample.frame == 2 && sample.skinMember == 1,
				 "a single native pose still samples animated skins");
	for (auto timing : {ModelMdlTiming::Stored, ModelMdlTiming::GlQuake})
	{
		request.timing = timing;
		request.seconds = std::numeric_limits<double>::max();
		ok &= expect(sampleModelMdl(mesh, request, &sample) && sample.frame == 2 && sample.skinMember >= 0 && sample.skinMember < 2,
					 "huge finite time is reduced without loop iteration or integer overflow");
	}
	const auto unchanged = sample;
	for (double invalid : {-1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
	{
		request.seconds = invalid;
		ok &= expect(!sampleModelMdl(mesh, request, &sample, &error) && !error.isEmpty() && sample.frame == unchanged.frame &&
						 sample.skinMember == unchanged.skinMember,
					 "invalid times refuse atomically");
	}
	request = {};
	auto damaged = mesh;
	damaged.mdl.frameGroups[0].intervals[1] = 0;
	ok &= expect(!sampleModelMdl(damaged, request, &sample), "malformed group times cannot drive playback");
	request.syncPhase = 1.1;
	ok &= expect(!sampleModelMdl(mesh, request, &sample), "out-of-range sync phase is refused");
	request = {};
	request.skin = 99;
	ok &= expect(!sampleModelMdl(mesh, request, &sample), "out-of-range native selection is refused");
	QVector<QImage> images;
	ok &= expect(prepareModelMdlPlaybackSkins(mesh, 0, &images, &error) && images.size() == 2,
				 "selected skin members prepare once for playback");
	if (images.size() != 2)
	{
		return false;
	}
	for (int member = 0; member < images.size(); ++member)
	{
		ok &= expect(images[member].format() == QImage::Format_ARGB32_Premultiplied, "prepared images need no playback conversion");
		for (int y = 0; y < images[member].height(); ++y)
		{
			for (int x = 0; x < images[member].width(); ++x)
			{
				const int index = quint8(mesh.embeddedSkins[0].indexedFrames[member][y * images[member].width() + x]);
				const auto &pal = mesh.mdl.palette;
				ok &= expect(images[member].pixel(x, y) ==
								 qRgb(quint8(pal[index * 3]), quint8(pal[index * 3 + 1]), quint8(pal[index * 3 + 2])),
							 "every prepared pixel retains exact palette colour and opaque index 255");
			}
		}
	}
	const auto key = images[0].cacheKey();
	ok &= expect(!prepareModelMdlPlaybackSkins(mesh, 0, &images, &error, {[] { return true; }, {}}) && images[0].cacheKey() == key,
				 "cancelled preparation retains prior output");
	damaged = mesh;
	damaged.mdl.skinSize = {8192, 8192};
	ok &= expect(!prepareModelMdlPlaybackSkins(damaged, 0, &images, &error) && images[0].cacheKey() == key,
				 "excessive image allocation is refused before decoding");
	return ok;
}
} // namespace

int main(int argc, char **argv)
{
#if defined(_MSC_VER) && defined(_DEBUG)
	for (int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT})
	{
		_CrtSetReportMode(type, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
	}
#endif
	// Semantic APIs and QWidget rendering only; no input injection or OS capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
	// Draws in 3D: skip where no OpenGL or Vulkan renderer starts.
	if (const int skip = vibestudio::test_support::exitCodeWithoutRenderer("model-mdl-playback-smoke"); skip >= 0) {
		return skip;
	}
#ifdef Q_OS_WIN
	app.setFont(QFont("Segoe UI", 10));
#endif
	const auto mesh = decodeModelMesh("timing.mdl", tests::groupedMdlFixture().bytes);
	const auto original = source(mesh);
	bool ok = coreChecks(mesh);
	QVector<QImage> skins;
	QString error;
	if (!expect(prepareModelMdlPlaybackSkins(mesh, 0, &skins, &error), "prepare viewport skins"))
	{
		return EXIT_FAILURE;
	}
	ModelViewport viewport, reference;
	for (auto *view : {&viewport, &reference})
	{
		view->resize(576, 480);
		view->setMesh(mesh);
		view->setShowAxes(false);
		view->setShowGrid(false);
		view->setBackfaceCulling(false);
		view->setRenderMode(ModelViewportRenderMode::Textured);
		view->show();
	}
	ok &= expect(qEnvironmentVariableIntValue("QT_SCALE_FACTOR") < 2 || viewport.devicePixelRatioF() >= 1.99,
				 "actual 2x device scale is enforced");
	ModelMdlPlayback request;
	request.seconds = .23;
	ok &= expect(viewport.setMdlPlayback(request, skins, &error) && viewport.frame() == 1 && viewport.mdlPlaybackSample().skinMember == 1,
				 "native transport adopts both pose and skin at the requested time");
	auto *timer = viewport.findChild<QTimer *>("modelPlaybackTimer");
	if (!expect(timer, "transport timer exists"))
	{
		return EXIT_FAILURE;
	}
	QSignalBlocker blocked(timer);
	viewport.setAnimationInterpolation(true);
	viewport.setFramesPerSecond(60);
	viewport.play();
	for (bool perspective : {false, true})
	{
		QImage otherSkin;
		for (auto *view : {&viewport, &reference})
		{
			CameraViewControls controls;
			controls.perspective = perspective;
			view->setCameraControls(controls);
			if (perspective)
			{
				view->setCameraView({22, 15, 18}, -145, -28);
			}
			else
			{
				view->setOrbit(35, 25);
			}
		}
		for (double time : {.03, .13, .23, .26})
		{
			ok &= expect(viewport.seekAnimation(time) && viewport.animationSample().fraction == 0,
						 "native playback ignores FPS and smoothing");
			const auto sample = viewport.mdlPlaybackSample();
			reference.setFrame(sample.frame);
			reference.setSkin(skins[sample.skinMember]);
			ok &= expect(tests::settleModelViewport(viewport) && tests::settleModelViewport(reference),
						 "native raster and independent stored reference finish");
			QImage actual(viewport.size(), QImage::Format_RGB32), expectedImage(reference.size(), QImage::Format_RGB32);
			viewport.render(&actual);
			reference.render(&expectedImage);
			ok &= expect(actual.copy(QRect(24, 95, 528, 310)) == expectedImage.copy(QRect(24, 95, 528, 310)),
						 "native pose and selected indexed texture match an independently bound reference in both cameras");
			for (int vertex = 0; vertex < mesh.surfaces[0].vertexCount; ++vertex)
			{
				ok &= expect(QLineF(viewport.vertexScreenPosition(0, vertex), reference.vertexScreenPosition(0, vertex)).length() < .001,
							 "native geometry projects the exact stored pose");
			}
			if (time == .13)
			{
				otherSkin = actual.copy(QRect(24, 95, 528, 310));
			}
			if (time == .26)
			{
				ok &= expect(actual.copy(QRect(24, 95, 528, 310)) != otherSkin,
							 "skin-only timing visibly changes pixels on an unchanged pose");
			}
			ok &= expect(viewport.mdlPlaybackSkin().cacheKey() == skins[sample.skinMember].cacheKey(),
						 "playback shares prepared image storage");
		}
	}
	viewport.setAnimationInterpolation(false);
	viewport.setEditSelection(0, {0, 1});
	viewport.setMoveGizmo(true);
	ok &= expect(!std::isfinite(viewport.moveGizmoPoints()[0].x()),
				 "native playback hides edit handles even when clip smoothing is disabled");
	viewport.pause();
	const double paused = viewport.mdlPlayback().seconds;
	viewport.play();
	QThread::msleep(35);
	blocked.unblock();
	QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection);
	blocked.reblock();
	ok &= expect(viewport.mdlPlayback().seconds >= paused + .025, "resume advances from the paused native time using elapsed clock");
	const auto observed = viewport.mdlPlayback();
	ModelMdlPlaybackSample expected;
	ok &= expect(sampleModelMdl(mesh, observed, &expected) && viewport.frame() == expected.frame &&
					 viewport.mdlPlaybackSample().skinMember == expected.skinMember,
				 "delayed timer delivery resamples native time instead of stepping once");
	viewport.setReducedMotion(true);
	viewport.play();
	ok &= expect(!viewport.isPlaying() && viewport.mdlPlaybackActive() && viewport.seekAnimation(.13) && viewport.frame() == 1,
				 "reduced motion pauses native playback while retaining deliberate seeks");
	viewport.setReducedMotion(false);
	request.nativeFrame = 1;
	request.seconds = 0;
	ok &= expect(viewport.setMdlPlayback(request, skins, &error), "select native single pose");
	viewport.play();
	ok &=
		expect(viewport.isPlaying() && viewport.seekAnimation(.13) && viewport.frame() == 2 && viewport.mdlPlaybackSample().skinMember == 1,
			   "animated skins play on a single-pose native frame");
	const auto before = viewport.mdlPlayback();
	request.skin = 99;
	ok &= expect(!viewport.setMdlPlayback(request, skins, &error) && viewport.mdlPlayback().skin == before.skin && viewport.isPlaying(),
				 "invalid native adoption leaves the active transport intact");
	ok &= expect(source(viewport.mesh()) == original, "all playback leaves source, indexed pixels and timings exact");
	viewport.setFrame(0);
	ok &= expect(!viewport.mdlPlaybackActive() && !viewport.isPlaying() && viewport.frame() == 0,
				 "explicit stored pose selection leaves native timing");
	viewport.setMdlPlayback({}, skins);
	viewport.setMesh(mesh);
	ok &= expect(!viewport.mdlPlaybackActive() && viewport.mdlPlaybackSkins().isEmpty(), "source adoption releases native texture cache");
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
