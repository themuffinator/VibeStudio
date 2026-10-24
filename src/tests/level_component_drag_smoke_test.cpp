#include "core/level_component_drag.h"
#include <QCoreApplication>
#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value,const char* label) { if (!value) { ok = false; std::cerr << label << '\n'; } return value; }
bool near(LevelMapVec3 a,LevelMapVec3 b) { return a.valid && b.valid && std::abs(a.x-b.x) < .0001 && std::abs(a.y-b.y) < .0001 && std::abs(a.z-b.z) < .0001; }
ModelPickRay ray(LevelMapVec3 p,int axis = 2) {
	return {{float(p.x),float(p.y),float(p.z)},{axis == 0 ? -1.f : 0.f,axis == 1 ? -1.f : 0.f,axis == 2 ? -1.f : 0.f},false};
}
ModelPickRay perspective(LevelMapVec3 p) {
	return {{-100,-120,100},{float(p.x+100),float(p.y+120),float(p.z-100)},true};
}
void snapping() {
	const LevelMapVec3 origin{1,2,3,true}; LevelComponentDrag drag; LevelMapVec3 delta;
	for (int plane = 0; plane < 3; ++plane) {
		auto options = LevelComponentDragOptions{false,true,-1,8,ray(origin,plane).direction};
		expect(drag.begin(origin,ray(origin,plane),options,Qt::NoModifier),"begin orthographic world plane");
		auto target = origin; if (plane != 0) { target.x += 10; } if (plane != 1) { target.y += 18; } if (plane != 2) { target.z += 26; }
		const LevelMapVec3 relative{plane != 0 ? 8. : 0,plane != 1 ? 16. : 0,plane != 2 ? 24. : 0,true};
		expect(drag.update(ray(target,plane),Qt::AltModifier,&delta) && near(delta,relative) && !drag.vertical(),"2D ignores Alt and snaps relative to off-grid origin");
		drag.accept(delta);
		expect(drag.update(ray(target,plane),Qt::ControlModifier,&delta) && near(delta,{plane != 0 ? 7. : -1.,plane != 1 ? 22. : -2.,plane != 2 ? 29. : -3.,true}),"absolute vertex grid snaps all coordinates including hidden axis");
	}
	LevelComponentDragOptions options{true,true,-1,8,{1,1,-1}};
	expect(drag.begin(origin,perspective(origin),options,Qt::NoModifier),"perspective begin");
	expect(drag.update(perspective({11,20,3,true}),Qt::NoModifier,&delta) && near(delta,{8,16,0,true}),"perspective horizontal XY relative movement");
	drag.accept(delta);
	expect(drag.update(perspective({11,20,3,true}),Qt::ControlModifier,&delta) && near(delta,{7,22,-3,true}) && drag.absolute(),"live Ctrl changes to absolute snapping");
	options.absoluteAllowed = false;
	expect(drag.begin(origin,perspective(origin),options,Qt::ControlModifier) && drag.update(perspective({11,20,3,true}),Qt::ControlModifier,&delta)
		&& near(delta,{8,16,0,true}) && !drag.absolute(),"edge/face movement always relative");
	options.grid = 0; options.absoluteAllowed = true;
	expect(drag.begin(origin,perspective(origin),options,Qt::NoModifier) && drag.update(perspective({11,20,3,true}),Qt::ControlModifier,&delta)
		&& near(delta,{10,18,0,true}),"disabled grid does not quantise");
	// The click may be several pixels away from the handle centre.
	expect(drag.begin(origin,perspective({3,4,3,true}),options,Qt::NoModifier) && drag.update(perspective({13,22,3,true}),Qt::NoModifier,&delta)
		&& near(delta,{10,18,0,true}),"pointer grab offset remains stable");
}
void constraints() {
	LevelComponentDrag drag; LevelMapVec3 delta; const LevelMapVec3 origin{0,0,0,true};
	const LevelComponentDragOptions options{true,true,-1,0,{1,1,-1}};
	expect(drag.begin(origin,perspective(origin),options,Qt::ShiftModifier) && drag.update(perspective({20,10,0,true}),Qt::ShiftModifier,&delta)
		&& near(delta,{20,10,0,true}) && drag.axis() == -1,"initial Shift insertion chord does not constrain");
	drag.accept(delta); drag.update(perspective({20,10,0,true}),Qt::NoModifier,&delta); drag.accept(delta);
	expect(drag.update(perspective({25,0,0,true}),Qt::ShiftModifier,&delta) && near(delta,{25,0,0,true}) && drag.axis() == 0,"live Shift constrains dominant accepted axis");
	drag.accept(delta);
	expect(drag.update(perspective({25,0,30,true}),Qt::AltModifier|Qt::ShiftModifier,&delta) && near(delta,{25,0,30,true}) && drag.vertical(),"Alt vertical takes precedence over Shift");
	drag.accept(delta);
	expect(drag.update(perspective({25,0,30,true}),Qt::NoModifier,&delta) && near(delta,{25,0,30,true}),"leaving vertical retains height and starts a new horizontal segment");
	drag.accept(delta);
	expect(drag.update(perspective({45,10,30,true}),Qt::NoModifier,&delta) && near(delta,{45,10,30,true}),"horizontal movement after vertical release");
	drag.accept(delta);
	expect(drag.update(perspective({50,0,30,true}),Qt::ShiftModifier,&delta) && near(delta,{50,0,30,true}) && drag.axis() == 0,"dominant axis uses segment since vertical release");
	// An unaccepted (failed or stale) preview cannot become a constraint anchor.
	drag.begin(origin,perspective(origin),options,Qt::NoModifier); drag.update(perspective({10,20,0,true}),Qt::NoModifier,&delta); drag.accept(delta);
	drag.update(perspective({80,1,0,true}),Qt::NoModifier,&delta);
	expect(drag.update(perspective({0,40,0,true}),Qt::ShiftModifier,&delta) && near(delta,{0,40,0,true}) && drag.axis() == 1,"unvalidated preview cannot change dominant axis");
	drag.begin(origin,perspective(origin),options,Qt::NoModifier); drag.update(perspective({10,10,0,true}),Qt::NoModifier,&delta); drag.accept(delta);
	expect(drag.update(perspective({20,30,0,true}),Qt::ShiftModifier,&delta) && drag.axis() == -1,"tied displacement remains unconstrained");
	for (int axis = 0; axis < 3; ++axis) {
		auto fixed = options; fixed.fixedAxis = axis; auto target = origin;
		if (axis == 0) { target.x = 30; } else if (axis == 1) { target.y = 30; } else { target.z = 30; }
		expect(drag.begin(origin,perspective(origin),fixed,Qt::AltModifier) && drag.update(perspective(target),Qt::ShiftModifier,&delta)
			&& near(delta,target) && drag.axis() == axis,"explicit XYZ gizmo retains chosen axis");
	}
	drag.begin(origin,perspective(origin),options,Qt::AltModifier);
	drag.update(perspective({0,0,30,true}),Qt::AltModifier,&delta); drag.accept(delta);
	drag.update(perspective({40,10,30,true}),Qt::NoModifier,&delta); drag.accept(delta);
	expect(drag.update(perspective({40,20,30,true}),Qt::ShiftModifier,&delta) && drag.axis() == -1,"vertical release resets basis to replacement proposal");
}
void refusals() {
	LevelComponentDrag drag; LevelMapVec3 delta{7,8,9,true}; const auto sentinel = delta;
	const LevelMapVec3 origin{0,0,0,true}; const LevelComponentDragOptions options{true,true,-1,8,{1,1,-1}};
	expect(!drag.update(perspective(origin),Qt::NoModifier,&delta) && near(delta,sentinel),"unstarted update preserves output");
	auto invalid = options; invalid.grid = std::numeric_limits<double>::quiet_NaN();
	expect(!drag.begin(origin,perspective(origin),invalid,Qt::NoModifier),"nonfinite grid refused");
	invalid = options; invalid.fixedAxis = 3; expect(!drag.begin(origin,perspective(origin),invalid,Qt::NoModifier),"invalid axis refused");
	expect(!drag.begin({40000,0,0,true},perspective(origin),options,Qt::NoModifier),"out-of-bounds origin refused");
	expect(drag.begin(origin,perspective(origin),options,Qt::NoModifier),"valid start after refusals");
	expect(!drag.update({{0,0,0},{1,0,0},true},Qt::NoModifier,&delta) && near(delta,sentinel),"parallel ray refuses without replacing output");
	expect(!drag.update({{0,0,100},{0,0,1},true},Qt::NoModifier,&delta),"behind-camera plane refused");
	expect(!drag.update(perspective({40000,0,0,true}),Qt::NoModifier,&delta),"world limit enforced");
	expect(!drag.update(perspective(origin),Qt::NoModifier,nullptr),"null output refused");
	expect(levelComponentDragChord(1,Qt::ControlModifier|Qt::AltModifier|Qt::ShiftModifier) && !levelComponentDragChord(2,Qt::ControlModifier)
		&& levelComponentDragChord(3,Qt::AltModifier) && !levelComponentDragChord(0,Qt::NoModifier),"native vertex versus edge/face initial chords");
	expect(levelComponentModifierState(Qt::Key_Shift,Qt::ControlModifier,true) == (Qt::ShiftModifier|Qt::ControlModifier)
		&& levelComponentModifierState(Qt::Key_Alt,Qt::AltModifier|Qt::ControlModifier,false) == Qt::ControlModifier,"modifier press/release flags without OS polling");
}
}
int main(int argc,char** argv) { QCoreApplication app(argc,argv); snapping(); constraints(); refusals(); return ok ? 0 : 1; }
