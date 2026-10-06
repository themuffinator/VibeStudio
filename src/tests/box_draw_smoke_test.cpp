#include "core/box_draw.h"
#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << "FAIL: " << message << '\n'; }
	return value;
}
BoxResizeRay ray(int axis, double u, double v, double direction = -1)
{
	BoxResizeRay result; result.origin[axis] = direction < 0 ? 200 : -200;
	result.origin[(axis+1)%3] = u; result.origin[(axis+2)%3] = v;
	result.direction[axis] = direction; result.forwardOnly = true; return result;
}
}
int main()
{
	bool ok = true; int cases = 0;
	for (int axis = 0; axis < 3; ++axis) {
		for (double grid : {0.0,1.0,16.0}) { for (double facing : {-1.0,1.0}) {
			for (int sign : {-1,1}) {
				BoxDrawDrag drag;
				ok &= expect(beginBoxDraw(ray(axis,0,0,facing),axis,8,64,grid,&drag),"all planes begin from either side");
				ResizeBox box;
				const auto end = ray(axis,sign*48,sign*32,facing);
				ok &= expect(updateBoxDraw(drag,end,false,false,&box),"valid footprint");
				ok &= expect(box.mins[axis] == 8 && box.maxs[axis] == 72
					&& box.maxs[(axis+1)%3]-box.mins[(axis+1)%3] == 48
					&& box.maxs[(axis+2)%3]-box.mins[(axis+2)%3] == 32,"exact source axes and depth");
				ok &= expect(updateBoxDraw(drag,end,true,false,&box) && box.maxs[(axis+2)%3]-box.mins[(axis+2)%3] == 48,"square footprint");
				ok &= expect(updateBoxDraw(drag,end,false,true,&box) && box.maxs[axis]-box.mins[axis] == 48,"cube depth");
				ok &= expect(adjustBoxDrawDepth(&drag,2),"depth adjustment");
				ok &= expect(updateBoxDraw(drag,end,false,false,&box) && box.maxs[axis] > 72,"wheel grows the hidden axis");
				ok &= expect(adjustBoxDrawDepth(&drag,-100000) && drag.depth >= 1,"depth clamps without inversion");
				++cases;
			}
		} }
	}
	BoxDrawDrag drag;
	ok &= beginBoxDraw(ray(2,-7,9),2,0,64,16,&drag);
	ok &= expect(drag.start[0] == 0 && drag.start[1] == 16,"absolute world grid snap");
	ResizeBox box{{-1,-1,-1},{1,1,1}}; const auto unchanged = box;
	ok &= expect(!updateBoxDraw(drag,ray(2,0,16),false,false,&box) && box == unchanged,"collapsed footprint leaves output unchanged");
	auto bad = ray(2,32,32); bad.direction = {1,0,0};
	ok &= expect(!updateBoxDraw(drag,bad,false,false,&box),"parallel ray rejected");
	bad.direction = {1,0,1e-8}; ok &= expect(!updateBoxDraw(drag,bad,false,false,&box),"grazing ray rejected");
	bad.direction = {0,0,1}; ok &= expect(!updateBoxDraw(drag,bad,false,false,&box),"backward perspective ray rejected");
	bad.forwardOnly = false; ok &= expect(updateBoxDraw(drag,bad,false,false,&box),"orthographic line may intersect backwards");
	bad.origin[0] = std::numeric_limits<double>::quiet_NaN(); ok &= expect(!updateBoxDraw(drag,bad,false,false,&box),"nonfinite input rejected");
	ok &= expect(!updateBoxDraw(drag,ray(2,40000,32),false,false,&box),"world limit enforced");
	ok &= expect(!beginBoxDraw(ray(2,0,0),3,0,64,16,&drag) && !beginBoxDraw(ray(2,0,0),2,32767,64,16,&drag)
		&& !beginBoxDraw(ray(2,0,0),2,0,0,16,&drag),"invalid plane and depth rejected");
	std::cout << "{\"ok\":" << (ok ? "true" : "false") << ",\"cases\":" << cases << "}\n";
	return ok ? 0 : 1;
}
