#include "core/level_clip_drag.h"
#include "core/editor_profiles.h"
#include "core/level_gestures.h"
#include <QCoreApplication>
#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value,const char* label) { if (!value) { ok = false; std::cerr << label << '\n'; } return value; }
bool near(LevelMapVec3 a,LevelMapVec3 b) { return a.valid && b.valid && std::abs(a.x-b.x) < .0001 && std::abs(a.y-b.y) < .0001 && std::abs(a.z-b.z) < .0001; }
ModelPickRay ray(LevelMapVec3 p) { return {{-100,-120,100},{float(p.x+100),float(p.y+120),float(p.z-100)},true}; }
void movement()
{
	const LevelMapVec3 origin{1,2,3,true}; LevelClipPointDrag drag; LevelMapVec3 point;
	expect(drag.begin(origin,ray(origin),2,8),"begin relative-grid clip drag");
	expect(drag.update(ray({11,20,3,true}),false,false,&point) && near(point,{9,18,3,true}),"horizontal relative grid preserves off-grid origin");
	expect(drag.update(ray({11,20,3,true}),false,true,&point) && near(point,{1,18,3,true}),"Shift constrains strongest displacement before grid");
	expect(drag.update(ray({21,12,3,true}),false,true,&point) && near(point,{17,2,3,true}),"live axis change uses fixed press anchor");
	expect(drag.update(ray({1,2,29,true}),true,true,&point) && near(point,{1,2,27,true}),"Alt plus Shift changes only vertical axis");
	expect(drag.update(ray({11,20,3,true}),false,true,&point) && near(point,{1,18,3,true}),"leaving vertical reuses original anchor without accumulated drift");
	drag.begin(origin,ray({3,4,3,true}),2,0);
	expect(drag.update(ray({13,22,3,true}),false,false,&point) && near(point,{11,20,3,true}),"grab offset is stable with grid disabled");
	drag.begin(origin,ray(origin),2,0);
	expect(drag.update(ray({11,12,3,true}),false,true,&point) && near(point,{11,2,3,true}),"equal axis movement uses first dominant axis");
	for (int axis = 0; axis < 3; ++axis) {
		drag.begin(origin,ray(origin),axis,8);
		auto target = origin; double* coordinates[] = {&target.x,&target.y,&target.z};
		for (int i = 0; i < 3; ++i) { if (i != axis) { *coordinates[i] += 18; } }
		auto expected = origin; double* result[] = {&expected.x,&expected.y,&expected.z};
		for (int i = 0; i < 3; ++i) { if (i != axis) { *result[i] += 16; } }
		expect(drag.update(ray(target),false,false,&point) && near(point,expected),"view-axis plane fixes the hidden coordinate");
		target = origin; *coordinates[axis] += 26; expected = origin; *result[axis] += 24;
		expect(drag.update(ray(target),true,false,&point) && near(point,expected),"view-axis depth movement follows its selected axial direction");
	}
	// NRC llrint uses nearest-even grid ties. Make the deterministic tie policy
	// explicit rather than depending on the process floating-point rounding mode.
	const LevelMapVec3 zero{0,0,0,true}; drag.begin(zero,ray(zero),2,8);
	for (const auto pair : {std::pair{4.,0.},std::pair{12.,16.},std::pair{-4.,0.},std::pair{-12.,-16.}}) {
		expect(drag.update(ray({pair.first,0,0,true}),false,false,&point) && near(point,{pair.second,0,0,true}),"nearest-even grid ties");
	}
}
void refusal()
{
	LevelClipPointDrag drag; const LevelMapVec3 origin{0,0,0,true}, sentinel{1,2,3,true}; LevelMapVec3 point = sentinel;
	expect(drag.begin(origin,{{0,0,100},{0,0,-1},true},2,0),"top-down camera supports horizontal movement");
	expect(!drag.update({{0,0,100},{0,0,-1},true},true,false,&point) && near(point,sentinel),"unresolvable vertical axis refuses without changing point");
	expect(drag.begin(origin,{{-100,0,0},{1,0,0},true},2,0),"level camera supports vertical movement");
	expect(!drag.update({{-100,0,0},{1,0,0},true},false,true,&point) && near(point,sentinel),"parallel horizontal plane refuses atomically");
	drag.begin(origin,ray(origin),2,0); auto away = ray(origin); away.direction.x *= -1; away.direction.y *= -1; away.direction.z *= -1;
	expect(!drag.update(away,false,false,&point) && near(point,sentinel),"behind-camera intersection refuses");
	auto invalid = ray(origin); invalid.direction.x = std::numeric_limits<float>::infinity();
	expect(!drag.update(invalid,false,false,&point) && near(point,sentinel),"non-finite ray refuses");
	expect(!drag.update(ray(origin),false,false,nullptr),"null output refuses");
	expect(!drag.begin(origin,ray(origin),3,0) && !drag.update(ray(origin),false,false,&point),"invalid axis clears stale tracking");
	expect(!drag.begin(origin,ray(origin),2,-1),"negative grid refuses");
	expect(!drag.begin(origin,ray(origin),2,std::numeric_limits<double>::infinity()),"non-finite grid refuses");
	expect(!drag.begin({},ray(origin),2,0),"invalid point refuses");
	expect(!drag.begin(origin,{{0,0,100},{0,0,0},true},2,0),"zero ray refuses");
	drag.begin(origin,ray(origin),2,std::numeric_limits<double>::denorm_min());
	expect(!drag.update(ray({10,0,0,true}),false,false,&point) && near(point,sentinel),"grid quotient overflow refuses atomically");
}
void preferences()
{
	QString error;
	for (const auto& profile : editorProfileDescriptors()) {
		expect((profile.controls.camera.clipDragStyle == CameraClipDragStyle::WorldAxes) == (profile.id == "netradiant-custom"),"only NRC enables world clip dragging by default");
		LevelEditorControls effective; QJsonObject overrides;
		expect(applyLevelGestureOverrides(profile.controls,{{"camera.clipDragStyle","view"}},&effective,&overrides,&error)
			&& effective.camera.clipDragStyle == CameraClipDragStyle::ViewAxes,"all profiles support explicit view-axis dragging");
		QString id; QJsonObject parsed;
		expect(parseLevelGestures(serializeLevelGestures(profile.id,overrides),&id,&parsed,&error) && id == profile.id && parsed == overrides,"clip movement policy exports and imports");
	}
	expect(!applyLevelGestureOverrides(netRadiantCustomLevelControls(),{{"camera.clipDragStyle","diagonal"}},nullptr,nullptr,&error),"unknown clip style refuses");
}
}
int main(int argc,char** argv)
{
	QCoreApplication app(argc,argv); movement(); refusal(); preferences(); return ok ? 0 : 1;
}
