#include "app/level_sweep_canvas.h"
#include <QApplication>
#include <QPainter>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>

using namespace vibestudio;
namespace
{
using V = LevelMapVec3;
bool ok = true;
bool expect(bool value, const char* message)
{
	if (!value) {
		ok = false;
		std::cerr << message << '\n';
	}
	return value;
}
bool near(double a, double b)
{
	return std::abs(a - b) < .0001;
}
bool near(V a, V b)
{
	return a.valid && b.valid && near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}
V add(V a, V b)
{
	return {a.x + b.x, a.y + b.y, a.z + b.z, true};
}
V mul(V a, double s)
{
	return {a.x * s, a.y * s, a.z * s, true};
}
LevelSweepSource source()
{
	LevelSweepSource s;
	s.centre = {0, 0, 0, true};
	s.normal = {0, 0, 1, true};
	s.scaleArm = {32, 16, 0, true};
	s.profiles = {LevelSweepProfile{0, 0, {{32, 16, 0, true}, {-32, 16, 0, true}, {-32, -16, 0, true}, {32, -16, 0, true}}}};
	return s;
}
void mathematics()
{
	auto s = source();
	s.centre = {3, 5, 7, true};
	s.scaleArm = {0, 32, 0, true};
	LevelSweepRequest r;
	r.translation = {10, 20, 30, true};
	r.rotationAxis = {1, 0, 0, true};
	r.angle = 90;
	QString error;
	expect(rotateLevelSweepDestination(&r, {0, 0, 1, true}, 90, &error), "compose a world Z turn after X");
	expect(near(levelSweepDestinationCentre(s, r), {13, 25, 37, true}), "rotation preserves translated centre");
	expect(near(levelSweepScaleHandle(s, r), {13, 25, 69, true}), "noncommuting rotations use world delta times initial");
	const auto before = levelSweepScaleHandle(s, r);
	expect(rotateLevelSweepDestination(&r, {0, 0, 1, true}, 360, &error) && near(before, levelSweepScaleHandle(s, r)),
	       "whole turn preserves the effective rotation");
	expect(!rotateLevelSweepDestination(&r, {0, 0, 0, true}, 45, &error) && !error.isEmpty() && near(before, levelSweepScaleHandle(s, r)),
	       "invalid axis leaves destination intact");
	s = source();
	r = {};
	r.translation = {0, 0, 64, true};
	r.scale = {2, 2, 2, true};
	r.angle = 90;
	r.iterations = 2;
	const auto caps = levelSweepDestinationCaps(s, r);
	expect(caps.size() == 2 && near(caps[0][0], {-32, 64, 64, true}) && near(caps[1][0], {-128, -64, 192, true}),
	       "successive caps apply cumulative affine transforms");
	s.centre = {3, 5, 7, true};
	r = {};
	expect(scaleLevelSweepTo(s, &r, add(s.centre, mul(s.scaleArm, 1.6)), 16, &error) && near(r.scale.x, 1.6875) &&
	           near(r.scale.z, r.scale.x),
	       "scale chooses closest grid-plane crossing on an off-grid diagonal arm");
	r = {};
	expect(stepLevelSweepScale(s, &r, 16, 16, &error) && near(r.scale.x, 1.40625),
	       "grid step advances the largest arm coordinate then snaps");
	r = {};
	r.angle = 90;
	s.centre = {0, 0, 0, true};
	expect(stepLevelSweepScale(s, &r, 16, 16, &error) && near(r.scale.x, 1.5) && near(levelSweepScaleHandle(s, r), {-24, 48, 0, true}),
	       "scale step follows the rotated arm");
	expect(stepLevelSweepScale(s, &r, -16, 16, &error) && near(r.scale.x, 1), "aligned scale step reverses exactly");
	expect(scaleLevelSweepTo(s, &r, {100, -200, 0, true}, 0, &error) && near(r.scale.x, .01),
	       "scale through centre clamps without reflection");
	const auto saved = r.scale;
	expect(!scaleLevelSweepTo(s, &r, {0, 500000, 0, true}, 0, &error) && near(saved, r.scale), "oversized scale is atomic");
	expect(!scaleLevelSweepTo(s, &r, {0, 10, 0, true}, std::numeric_limits<double>::quiet_NaN(), &error) && near(saved, r.scale),
	       "nonfinite grid is atomic");
	s.scaleArm = {0, 0, 0, true};
	expect(!stepLevelSweepScale(s, &r, 16, 16, &error), "zero scale arm refused");
}
LevelComponentGizmoFrame frame(int axis = 2, bool perspective = false)
{
	LevelComponentGizmoFrame f;
	f.radius = 48;
	f.worldAxes = true;
	f.perspective = perspective;
	f.rotationAxis = axis;
	f.viewDirection = axis == 2 ? ModelVec3{0, 0, -1} : axis == 1 ? ModelVec3{0, -1, 0} : ModelVec3{-1, 0, 0};
	f.project = [axis](V p) { return QPointF(400 + 2 * (axis == 0 ? p.y : p.x), 300 - 2 * (axis == 2 ? p.y : p.z)); };
	f.ray = [axis](QPointF p) {
		const float u = float((p.x() - 400) / 2), v = float((300 - p.y()) / 2);
		return axis == 2   ? ModelPickRay{{u, v, 500}, {0, 0, -1}, false}
		       : axis == 1 ? ModelPickRay{{u, 500, v}, {0, -1, 0}, false}
		                   : ModelPickRay{{500, u, v}, {-1, 0, 0}, false};
	};
	return f;
}
LevelComponentGizmoFrame cameraFrame()
{
	// A pinhole camera at (0,-400,300), aimed at the origin. Independent of
	// widget input and of the production camera projection implementation.
	LevelComponentGizmoFrame f;
	f.radius = 48;
	f.perspective = true;
	f.worldAxes = true;
	f.viewDirection = {0, .8f, -.6f};
	f.project = [](V p) {
		const double depth = .8 * (p.y + 400) - .6 * (p.z - 300);
		return QPointF(400 + 500 * p.x / depth, 300 - 500 * (.6 * (p.y + 400) + .8 * (p.z - 300)) / depth);
	};
	f.ray = [](QPointF p) {
		const double x = (p.x() - 400) / 500, y = (300 - p.y()) / 500;
		const double dx = x, dy = .8 + .6 * y, dz = -.6 + .8 * y, length = std::sqrt(dx * dx + dy * dy + dz * dz);
		return ModelPickRay{{0, -400, 300}, {float(dx / length), float(dy / length), float(dz / length)}, true};
	};
	return f;
}
void gestures()
{
	const auto s = source();
	LevelSweepRequest r;
	LevelSweepGizmo g(s, r);
	g.setSteps(16, 15);
	int revisions = 0;
	QObject::connect(&g, &LevelSweepGizmo::requestChanged, &g, [&](const auto&) { ++revisions; });
	for (int axis : {0, 1, 2}) {
		g.reset();
		const auto f = frame(axis);
		const auto c = f.project(g.centre());
		expect(g.rotationHandles(f).size() == 1 && g.rotationHandles(f).first().axis == axis,
		       "plan displays only its perpendicular rotation ring");
		if (!expect(g.begin(c, f), "begin plan centre drag")) {
			continue;
		}
		expect(g.updateDrag(c + QPointF(39, -21)), "plan translation proposal");
		g.finish(true);
		const V expected = axis == 0 ? V{0, 16, 16, true} : axis == 1 ? V{16, 0, 16, true} : V{16, 16, 0, true};
		if (!near(g.request().translation, expected)) {
			std::cerr << "axis " << axis << " translation " << g.request().translation.x << "," << g.request().translation.y << ","
			          << g.request().translation.z << " scale " << g.request().scale.x << " hit " << g.hitTest(c, f) << "\n";
		}
		expect(near(g.request().translation, expected), "relative plan grid and untouched hidden axis");
		expect(g.cancel() && near(g.request().translation, {0, 0, 0, true}) && !g.cancel(),
		       "Escape resets destination then leaves identity");
	}
	const auto f = frame();
	auto c = f.project(g.centre());
	expect(!g.begin(c, f, Qt::ControlModifier) && !g.begin(c, f, Qt::ShiftModifier), "centre does not steal component selection chords");
	expect(g.begin(c, f, Qt::AltModifier) && g.updateDrag(c + QPointF(40, -20), Qt::AltModifier), "Alt accepted at centre press in a plan");
	g.finish(false);
	expect(near(g.request().translation, {0, 0, 0, true}), "cancel drag restores exact initial transform");
	expect(g.begin(c, f) && g.updateDrag(c + QPointF(50, -20)), "start constrained drag");
	expect(g.updateDrag(c + QPointF(50, -20), Qt::ShiftModifier) && g.updateDrag(c + QPointF(80, -60), Qt::ShiftModifier),
	       "live Shift constraint");
	expect(near(g.request().translation, {48, 0, 0, true}), "Shift pins strongest world axis");
	g.finish(false);
	r.translation = {3, 5, 0, true};
	g.setRequest(r);
	c = f.project(g.centre());
	expect(g.begin(c, f) && g.updateDrag(c + QPointF(40, -40), Qt::ControlModifier), "live absolute grid mode");
	expect(near(g.centre(), {16, 32, 0, true}), "Ctrl snaps destination coordinates to world grid");
	g.finish(false);
	expect(near(g.request().translation, r.translation), "absolute-grid cancellation restores off-grid start");
	g.reset();
	const auto scale = f.project(g.scaleHandle());
	expect(g.hitTest(scale, f) == 4 && !g.begin(scale, f, Qt::AltModifier), "scale wins hit test and requires a plain press");
	expect(g.begin(scale, f) && g.updateDrag(scale + QPointF(32, -16)), "drag diagonal scale arm");
	g.finish(true);
	expect(near(g.request().scale, {1.5, 1.5, 1.5, true}), "dragging scale stays uniform");
	expect(g.scaleStep(-1) && near(g.request().scale, {1, 1, 1, true}), "scale command returns to original cap");
	const auto ring = g.rotationHandles(f).first();
	if (expect(g.begin(ring.label, f), "begin cap rotation ring")) {
		const auto pivot = f.project(g.centre());
		const auto d = ring.label - pivot;
		expect(g.updateDrag(pivot + QPointF(d.y(), -d.x())), "rotate cap a quarter turn");
		g.finish(true);
		expect(near(levelSweepScaleHandle(s, g.request()), {-16, 32, 0, true}), "rotation moves cap and scale arm together");
	}
	g.reset();
	bool invalidRay=false; auto unstable=frame(); const auto validRay=unstable.ray;
	unstable.ray=[&](QPointF p) { return invalidRay ? ModelPickRay{{0,0,0},{0,0,0},true} : validRay(p); };
	const auto ringStart=g.rotationHandles(unstable).first().label;
	if(expect(g.begin(ringStart,unstable),"begin rotation before an invalid captured ray")) {
		const auto pivot=unstable.project(g.centre()); const auto d=ringStart-pivot;
		expect(g.updateDrag(pivot+QPointF(d.y(),-d.x())),"valid ring proposal before ray failure");
		invalidRay=true; expect(!g.updateDrag(pivot-d),"finite pointer with invalid ring ray refused"); g.finish(true);
		expect(near(g.request().angle,0),"invalid ring release restores its initial destination");
	}
	g.reset();
	expect(g.begin(c = f.project(g.centre()), f) && g.updateDrag(c + QPointF(64, 0)), "valid drag before invalid release");
	expect(!g.updateDrag({std::numeric_limits<double>::infinity(), 0}), "invalid pointer refused");
	g.finish(true);
	expect(near(g.request().translation, {0, 0, 0, true}), "invalid final proposal cannot retain a prior draft");
	const auto camera = cameraFrame();
	c = camera.project(g.centre());
	expect(g.rotationHandles(camera).size() == 3, "camera offers all rotation axes");
	expect(g.begin(c, camera) && g.updateDrag(camera.project({32, 16, 0, true})), "perspective centre uses the world XY plane");
	if (!near(g.centre(), {32, 16, 0, true})) {
		std::cerr << "camera centre " << g.centre().x << "," << g.centre().y << "," << g.centre().z << " scale " << g.request().scale.x
		          << "\n";
	}
	expect(near(g.centre(), {32, 16, 0, true}), "perspective snapped XY destination");
	g.finish(false);
	expect(g.begin(c, camera, Qt::AltModifier) && g.updateDrag(camera.project({0, 0, 32, true}), Qt::AltModifier),
	       "Alt press starts vertical camera move");
	expect(near(g.centre(), {0, 0, 32, true}), "perspective Alt holds XY fixed");
	g.finish(false);
	expect(g.begin(c, camera) && g.updateDrag(camera.project({32, 0, 0, true})), "camera draft before grid change");
	g.setSteps(8, 15);
	expect(!g.dragging() && near(g.centre(), {0, 0, 0, true}), "grid change cancels a captured gesture");
	expect(revisions > 10, "destination changes are published for shared preview checks");
}
void routing()
{
	QWidget view;
	view.resize(800, 600);
	view.setAccessibleDescription("original");
	LevelSweepGizmo g(source(), {});
	const auto f = frame();
	auto* canvas = new LevelSweepCanvas(&view, &g, [f] { return f; }, true);
	using C = LevelSweepCanvas::Command;
	expect(canvas->commandForKey(QKeyCombination(Qt::Key_Return)) == C::Apply &&
	           canvas->commandForKey(QKeyCombination(Qt::Key_BracketLeft)) == C::DecreaseScale,
	       "tool-local keys resolve without global grid commands");
	canvas->setCommandKeys(C::DecreaseScale, {QKeySequence("Ctrl+J")});
	canvas->setCommandKeys(C::IncreaseScale, {});
	expect(canvas->commandForKey(QKeyCombination(Qt::Key_BracketLeft)) == C::None &&
	           canvas->commandForKey(QKeyCombination(Qt::Key_BracketRight)) == C::None &&
	           canvas->commandForKey(QKeyCombination(Qt::ControlModifier, Qt::Key_J)) == C::DecreaseScale,
	       "custom and explicitly unbound keys replace defaults");
	int apply = 0, leave = 0;
	QObject::connect(canvas, &LevelSweepCanvas::applyRequested, &view, [&] { ++apply; });
	QObject::connect(canvas, &LevelSweepCanvas::leaveRequested, &view, [&] { ++leave; });
	canvas->executeCommand(C::IncreaseScale);
	expect(near(g.request().scale.x, 1.5), "semantic command routes to shared scale service");
	const auto c = f.project(g.centre());
	g.begin(c, f);
	canvas->executeCommand(C::Apply);
	expect(apply == 0, "apply suppressed during gesture");
	canvas->executeCommand(C::Cancel);
	expect(!g.dragging() && near(g.request().scale.x, 1.5), "first cancel restores gesture start");
	canvas->executeCommand(C::Cancel);
	expect(near(g.request().scale.x, 1) && leave == 0, "second cancel resets transform");
	canvas->executeCommand(C::Cancel);
	expect(leave == 1, "identity cancel leaves tool");
	canvas->executeCommand(C::Apply);
	expect(apply == 1, "apply request routes only after gesture ends");
	expect(view.accessibleDescription().contains("Sweep destination"), "view exposes shared destination state");
	QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::black);
	{
		QPainter p(&image);
		g.paint(p, f, true);
	}
	int accent = 0;
	for (int y = 0; y < image.height(); ++y) {
		for (int x = 0; x < image.width(); ++x) {
			const auto colour = image.pixelColor(x, y);
			accent += colour.red() > 220 && colour.green() > 220 && colour.blue() < 50;
		}
	}
	expect(accent > 300, "high visibility cap and handles render into owned image");
	canvas->stop();
	expect(view.accessibleDescription() == "original", "leaving tool restores view description");
}
} // namespace
int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);
	mathematics();
	gestures();
	routing();
	return ok ? 0 : 1;
}
