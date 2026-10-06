#include "app/model_viewport.h"
#include "core/model_pose.h"
#include "core/model_tags.h"
#include "tests/model_animation_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"

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
bool near(double a, double b, double tolerance = .00001) { return std::abs(a - b) <= tolerance; }
bool near(ModelVec3 a, ModelVec3 b) { return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z); }
bool sameTag(const ModelTag &a, const ModelTag &b)
{
	if (a.name != b.name || !near(a.origin, b.origin))
	{
		return false;
	}
	for (int i = 0; i < 9; ++i)
	{
		if (!near(a.axis[i], b.axis[i]))
		{
			return false;
		}
	}
	return true;
}
QByteArray bytes(const ModelMesh &mesh) { return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact); }
bool coreChecks()
{
	bool ok = true;
	ModelAnimationSample sample;
	ok &= expect(sampleModelAnimation(5, 3, 2.5, true, &sample) && sample.frame == 7 && sample.nextFrame == 5 && sample.fraction == .5,
				 "last clip frame blends to the clip's first frame");
	ok &=
		expect(sampleModelAnimation(5, 3, 123456.25, true, &sample) && sample.frame == 5 && sample.nextFrame == 6 && sample.fraction == .25,
			   "delayed sampling skips whole loops without repeated stepping");
	ok &= expect(sampleModelAnimation(5, 3, 3, true, &sample) && sample.frame == 5 && sample.fraction == 0,
				 "exact loop boundary returns first stored pose");
	ok &= expect(sampleModelAnimation(2, 1, 4.75, true, &sample) && sample.frame == 2 && sample.nextFrame == 2 && sample.fraction == 0,
				 "single-pose range has no interpolation");
	ok &= expect(sampleModelAnimation(0, 3, 1.75, false, &sample) && sample.frame == 1 && sample.fraction == 0,
				 "stored-frame sampling retains floor semantics");
	for (double offset : {-1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
	{
		const auto before = sample;
		ok &=
			expect(!sampleModelAnimation(0, 3, offset, true, &sample) && sample.frame == before.frame && sample.fraction == before.fraction,
				   "invalid time leaves sample untouched");
	}
	ok &= expect(!sampleModelAnimation(-1, 1, 0, true, &sample) && !sampleModelAnimation(0, 0, 0, true, &sample) &&
					 !sampleModelAnimation(std::numeric_limits<int>::max(), 2, 0, true, &sample) &&
					 !sampleModelAnimation(0, 1, 0, true, nullptr),
				 "invalid sample output or frame bounds are refused");
	ok &= expect(near(interpolateModelPosition({2, 4, -6}, {6, -4, 2}, .25), {3, 2, -4}), "position blending uses expected linear values");
	ModelVec3 normal;
	ok &= expect(interpolateModelNormal({0, 0, 2}, {0, 3, 0}, .5, &normal) && near(normal, {0, float(std::sqrt(.5)), float(std::sqrt(.5))}),
				 "normal blend normalizes both endpoints before interpolation");
	const auto originalNormal = normal;
	ok &= expect(!interpolateModelNormal({0, 0, 1}, {0, 0, -1}, .5, &normal) && near(normal, originalNormal) &&
					 !interpolateModelNormal({}, {0, 0, 1}, .5, &normal) &&
					 !interpolateModelNormal({0, 0, 1}, {0, 0, 1}, std::numeric_limits<double>::quiet_NaN(), &normal),
				 "cancelling normals and invalid inputs fail without changing output");
	const auto mesh = tests::animationFixture();
	for (const auto &name : modelTagNames(mesh))
	{
		const auto &a = *findModelTag(mesh, name, 0), &b = *findModelTag(mesh, name, 1);
		ModelTag tag;
		ok &= expect(interpolateModelTag(a, b, .5, &tag) && near(tag.origin, {1, 2, 7}) && near(tag.axis[0], std::sqrt(.5)) &&
						 near(tag.axis[1], std::sqrt(.5)) && near(tag.axis[8], name == "tag_reflected" ? -1 : 1),
					 "rigid tags interpolate expected origins and 45-degree orientations with common handedness");
		ok &= expect(interpolateModelTag(a, b, 0, &tag) && sameTag(tag, a) && interpolateModelTag(a, b, 1, &tag) && sameTag(tag, b),
					 "exact tag endpoints are preserved");
		auto invalid = b;
		invalid.axis[8] = -invalid.axis[8];
		const auto unchanged = tag;
		ok &= expect(!interpolateModelTag(a, invalid, .5, &tag) && sameTag(tag, unchanged), "opposite tag handedness fails atomically");
		invalid = b;
		invalid.axis[0] = std::numeric_limits<float>::infinity();
		ok &= expect(!interpolateModelTag(a, invalid, .5, &tag) && !interpolateModelTag(a, b, 2, &tag),
					 "nonrigid or nonfinite tag data and invalid fractions are refused");
	}
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
	// Public values, QObject signals and widget-owned renders; no input injection
	// or OS capture. Blocking the timer signal lets seeks render deterministically.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont("Segoe UI", 10));
#endif
	bool ok = coreChecks();
	const auto original = tests::animationFixture();
	const auto originalBytes = bytes(original);
	ModelMesh inserted = original;
	ModelEdit edit;
	edit.kind = ModelEditKind::InsertInbetweens;
	edit.frame = 0;
	ModelSelection selection;
	QString error;
	ok &= expect(applyModelEdit(&inserted, edit, &selection, &error), "prepare authored midpoint for preview parity");
	ModelViewport viewport, reference;
	for (auto *view : {&viewport, &reference})
	{
		view->resize(576, 480);
		view->setMesh(view == &viewport ? original : inserted);
		view->setShowAxes(false);
		view->setShowGrid(false);
		view->setShowTags(true);
		view->setBackfaceCulling(false);
		view->setRenderMode(ModelViewportRenderMode::Textured);
		QImage red(4, 4, QImage::Format_RGB32), blue(4, 4, QImage::Format_RGB32);
		red.fill(Qt::red);
		blue.fill(Qt::blue);
		view->setSurfaceSkins({{0, red}, {1, blue}});
		view->show();
	}
	ok &= expect(viewport.hasSkin() && reference.hasSkin(), "surface textures bind after mesh adoption");
	reference.setFrame(1);
	viewport.setAnimationInterpolation(true);
	viewport.setAnimationIndex(0);
	viewport.setFramesPerSecond(10);
	auto *timer = viewport.findChild<QTimer *>("modelPlaybackTimer");
	if (!expect(timer, "playback clock timer is available"))
	{
		return EXIT_FAILURE;
	}
	{
		QSignalBlocker fixedTime(timer);
		for (bool perspective : {false, true})
		{
			for (auto *view : {&viewport, &reference})
			{
				CameraViewControls controls;
				controls.perspective = perspective;
				view->setCameraControls(controls);
				if (perspective)
				{
					view->setCameraView({65, 40, 35}, -145, -25);
				}
				else
				{
					view->setOrbit(35, 25);
				}
			}
			viewport.play();
			ok &= expect(viewport.seekAnimation(.05) && viewport.animationSample().frame == 0 &&
							 viewport.animationSample().nextFrame == 1 && viewport.animationSample().fraction == .5,
						 "seek samples halfway between the selected clip's poses");
			ok &= expect(tests::settleModelViewport(viewport) && tests::settleModelViewport(reference),
						 "fractional and authored poses finish rendering");
			for (int s = 0; s < original.surfaces.size(); ++s)
			{
				for (int v = 0; v < original.surfaces[s].vertexCount; ++v)
				{
					ok &= expect(QLineF(viewport.vertexScreenPosition(s, v), reference.vertexScreenPosition(s, v)).length() < .001,
								 "all surfaces project the same vertices as the authored midpoint");
				}
			}
			for (const auto &name : modelTagNames(original))
			{
				ModelTag previewTag, referenceTag;
				ok &= expect(viewport.tagPose(name, &previewTag) && reference.tagPose(name, &referenceTag) &&
								 sameTag(previewTag, referenceTag),
							 "preview attachment pose agrees with shared authored interpolation");
			}
			const auto centre =
				(viewport.vertexScreenPosition(0, 0) + viewport.vertexScreenPosition(0, 1) + viewport.vertexScreenPosition(0, 2)) / 3;
			const auto hit = viewport.hitAt(centre), expectedHit = reference.hitAt(centre);
			ok &= expect(hit.valid && hit.surface == expectedHit.surface && hit.triangle == expectedHit.triangle,
						 "fractional face picking follows visible authored geometry");
			const auto edgePoint = (viewport.vertexScreenPosition(0, 0) + viewport.vertexScreenPosition(0, 1)) / 2;
			const auto edgeHit = viewport.edgeAt(edgePoint);
			ok &= expect(edgeHit.valid && edgeHit.surface == 0 && edgeHit.a == 0 && edgeHit.b == 1,
						 "fractional edge picking uses interpolated endpoints");
			QImage image(viewport.size(), QImage::Format_RGB32), expected(reference.size(), QImage::Format_RGB32);
			viewport.render(&image);
			reference.render(&expected);
			const QRect geometry(24, 95, 528, 310);
			ok &= expect(image.copy(geometry) == expected.copy(geometry),
						 "textured fractional raster agrees with authored geometry in both cameras");
			viewport.setShowTags(false);
			ok &= expect(tests::settleModelViewport(viewport), "hiding tags retires their projected raster markers");
			QImage hidden(viewport.size(), QImage::Format_RGB32);
			viewport.render(&hidden);
			ok &= expect(hidden.copy(geometry) != image.copy(geometry), "hidden attachment markers leave the displayed image");
			viewport.setShowTags(true);
			ok &= expect(tests::settleModelViewport(viewport), "showing tags refreshes their projected raster markers");
			viewport.render(&hidden);
			ok &= expect(hidden.copy(geometry) == image.copy(geometry), "restored attachment markers match the same completed pose");
			// Let the clock advance while retaining the previous completed raster.
			// Painting queues a new raster, but must keep its displayed attachments
			// aligned to the completed image until that new work is delivered.
			QThread::msleep(30);
			fixedTime.unblock();
			QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection);
			fixedTime.reblock();
			QImage pending(viewport.size(), QImage::Format_RGB32);
			viewport.render(&pending);
			ok &= expect(pending.copy(geometry) == image.copy(geometry),
						 "attachment markers stay with the completed mesh raster while a later pose renders");
			viewport.seekAnimation(.05);
			ok &= expect(tests::settleModelViewport(viewport), "explicit seek retires delayed pose work");
			const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
			if (!evidence.isEmpty())
			{
				QDir().mkpath(evidence);
				ok &= expect(image.save(QDir(evidence).filePath(
								 QStringLiteral("smooth-%1-%2x.png")
									 .arg(perspective ? QStringLiteral("perspective") : QStringLiteral("orthographic"))
									 .arg(viewport.devicePixelRatioF()))),
							 "save fractional widget render");
			}
			viewport.setTagPicking(true);
			ok &= expect(viewport.tagAt(viewport.tagScreenPosition("tag_mount")) == "tag_mount",
						 "tag origin picking follows the blended pose");
			viewport.setEditSelection(0, {0, 1});
			viewport.setMoveGizmo(true);
			ok &= expect(!std::isfinite(viewport.moveGizmoPoints()[0].x()), "smooth playback hides editing handles until paused");
			viewport.pause();
			ok &= expect(viewport.animationSample().fraction == 0 && viewport.frame() == 0 && tests::settleModelViewport(viewport),
						 "pause retires fractional work and restores an exact stored pose");
			viewport.setEditSelection(-1, {});
			viewport.setMoveGizmo(false);
		}
		viewport.play();
		viewport.seekAnimation(.175);
		ok &= expect(viewport.frame() == 1 && viewport.animationSample().nextFrame == 0 && near(viewport.animationSample().fraction, .75),
					 "end-to-start interpolation stays within the selected clip");
		viewport.stepFrame(1);
		ok &= expect(viewport.frame() == 0 && viewport.animationSample().fraction == 0, "explicit step clears any fractional pose");
		int frameSignals = 0;
		const auto connection = QObject::connect(&viewport, &ModelViewport::frameChanged, [&] { ++frameSignals; });
		viewport.seekAnimation(.025);
		viewport.seekAnimation(.075);
		ok &= expect(frameSignals == 0, "subframe updates do not emit whole-frame editor refresh signals");
		QObject::disconnect(connection);
		viewport.setFrame(0);
		ok &= expect(viewport.animationSample().fraction == 0, "selecting the same stored frame clears a blend");
		viewport.seekAnimation(.05);
		const auto before = viewport.animationSample();
		ok &= expect(!viewport.seekAnimation(-1) && !viewport.seekAnimation(std::numeric_limits<double>::infinity()) &&
						 viewport.animationSample().fraction == before.fraction,
					 "invalid seek is atomic");
		viewport.setFramesPerSecond(24);
		ok &= expect(viewport.frame() == before.frame && viewport.animationSample().fraction == before.fraction,
					 "changing speed retains the displayed pose and resets the elapsed-time anchor");
		viewport.setAnimationInterpolation(false);
		ok &= expect(viewport.animationSample().fraction == 0 && viewport.seekAnimation(.025) && viewport.animationSample().fraction == 0,
					 "stored-frame playback never displays fractional geometry");
		viewport.setAnimationInterpolation(true);
		viewport.seekAnimation(.025);
		viewport.setReducedMotion(true);
		ok &= expect(!viewport.isPlaying() && viewport.animationSample().fraction == 0, "reduced motion pauses on an exact saved pose");
		viewport.play();
		ok &= expect(!viewport.isPlaying() && viewport.seekAnimation(.075) && viewport.animationSample().fraction == 0,
					 "reduced motion still allows deliberate exact-pose seeks");
		viewport.setReducedMotion(false);
		viewport.play();
		viewport.setAnimationIndex(2);
		ok &= expect(!viewport.isPlaying() && viewport.frame() == 1 && viewport.animationSample().fraction == 0,
					 "selecting a one-pose clip stops smooth playback");
		viewport.setAnimationIndex(4);
		viewport.setFramesPerSecond(10);
		viewport.play();
		viewport.seekAnimation(.15);
		ok &= expect(viewport.frame() == 2 && viewport.animationSample().nextFrame == 1 && near(viewport.animationSample().fraction, .5),
					 "a nonzero-first clip wraps without reading adjacent unrelated poses");
		ok &= expect(bytes(viewport.mesh()) == originalBytes, "seeking, timing, interpolation and camera changes never mutate source data");
		viewport.pause();
		auto incompatible = original;
		for (auto &tag : incompatible.tags)
		{
			if (tag.frameIndex == 1)
			{
				tag.axis[8] = -tag.axis[8];
			}
		}
		viewport.setMesh(incompatible);
		viewport.play();
		viewport.seekAnimation(.05);
		ModelTag tag;
		ok &= expect(!viewport.tagPose("tag_mount", &tag) && viewport.playbackSummary().contains("incompatible") &&
						 viewport.accessibleSummary().contains("incompatible"),
					 "incompatible attachment interpolation is hidden with visible and accessible diagnostics");
		viewport.setShowTags(false);
		ok &= expect(!viewport.playbackSummary().contains("incompatible") && !viewport.accessibleDescription().contains("incompatible"),
					 "hidden attachment previews need no interpolation diagnostic");
		viewport.setShowTags(true);
		ok &= expect(viewport.playbackSummary().contains("incompatible") && viewport.accessibleDescription().contains("incompatible"),
					 "showing attachments restores their interpolation diagnostic");
		viewport.pause();
		ok &= expect(viewport.tagPose("tag_mount", &tag), "exact stored attachments remain inspectable after incompatible preview");
		incompatible = original;
		incompatible.surfaces[0].frames[1].positions.removeLast();
		viewport.setShowTags(false);
		viewport.setMesh(incompatible);
		const auto stored = viewport.vertexScreenPosition(0, 0);
		viewport.play();
		viewport.seekAnimation(.05);
		ok &= expect(viewport.vertexScreenPosition(0, 0) == stored && viewport.playbackSummary().contains("incompatible"),
					 "mismatched surface geometry retains its stored pose and diagnostic even when tags are hidden");
		// Native MD3 preview accepts more tags than an editable document. Keep this
		// synthetic mesh on that read-only path and avoid per-tag pose lookups when
		// attachments are hidden. Timing is an observation, not a latency promise.
		auto large = original;
		large.tags.clear();
		large.tagCount = 8192;
		large.tags.reserve(8192 * 3);
		for (int frame = 0; frame < 3; ++frame)
		{
			for (int index = 0; index < 8192; ++index)
			{
				auto entry = original.tags[frame * 2];
				entry.name = QStringLiteral("tag_%1").arg(index);
				large.tags << entry;
			}
		}
		QElapsedTimer hiddenTags;
		hiddenTags.start();
		viewport.setMesh(large);
		viewport.play();
		ok &= expect(viewport.seekAnimation(.05) && viewport.animationSample().fraction == .5 && tests::settleModelViewport(viewport),
					 "native preview renders 8192 hidden attachments per pose without projecting their markers");
		std::cout << "Hidden attachment preview (8192 tags x 3 poses): " << hiddenTags.elapsed() << " ms\n";
		viewport.setMesh(original);
		ok &= expect(!viewport.isPlaying() && viewport.animationSample().fraction == 0, "replacing the mesh retires fractional state");
	}
	// Deliberately defer a timer delivery. One delivered timeout must sample real
	// elapsed time, including more than one frame, rather than step exactly once.
	viewport.setAnimationIndex(-1);
	viewport.setFramesPerSecond(10);
	viewport.play();
	timer->stop();
	QElapsedTimer elapsed;
	elapsed.start();
	viewport.seekAnimation(0);
	QThread::msleep(235);
	const double lower = elapsed.nsecsElapsed() / 1e9 * 10;
	ok &= expect(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection), "deliver a delayed playback timer signal");
	const double upper = elapsed.nsecsElapsed() / 1e9 * 10;
	const auto sampled = viewport.animationSample();
	const double phase = sampled.frame + sampled.fraction;
	const double unwrapped = phase + std::round((lower - phase) / 3) * 3;
	ok &= expect(unwrapped >= lower - .05 && unwrapped <= upper + .05,
				 "delayed timer delivery follows elapsed time instead of callback count");
	viewport.pause();
	viewport.clearMesh();
	ok &= expect(!viewport.seekAnimation(0) && viewport.animationSample().fraction == 0 && !timer->isActive(),
				 "clearing mesh stops clock and invalidates seeking");
	if (qEnvironmentVariable("QT_SCALE_FACTOR") == "2")
	{
		ok &= expect(near(viewport.devicePixelRatioF(), 2, .01), "playback widget uses actual DPR2");
	}
	std::cout << "Playback device pixel ratio: " << viewport.devicePixelRatioF() << '\n';
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
