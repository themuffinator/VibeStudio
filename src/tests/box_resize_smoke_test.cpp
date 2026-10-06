#include "core/box_resize.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << "FAIL: " << message << '\n'; }
	return value;
}
BoxResizeRay rayAt(BoxResizePoint point, int axis, bool perspective)
{
	BoxResizeRay ray; ray.forwardOnly = perspective;
	for (int i = 0; i < 3; ++i) { ray.direction[i] = i == axis ? 0 : -1; ray.origin[i] = point[i] - ray.direction[i]*100; }
	return ray;
}
}
int main()
{
	bool ok = true;
	const ResizeBox source{{-4.5,-8.25,0.125},{12.5,23.75,64.125}};
	for (bool perspective : {false,true}) { for (double grid : {0.0,1.0,16.0}) { for (int handle = 0; handle < 6; ++handle) {
		BoxResizePoint point; ok &= boxResizeHandle(source,handle,&point);
		const int axis = handle/2; const bool maximum = handle%2;
		const auto press = rayAt(point,axis,perspective); BoxResizeDrag drag;
		ok &= expect(beginBoxResize(source,handle,{1,1,1},press,grid,&drag),"all six axes begin in perspective and orthographic views");
		ResizeBox result;
		ok &= expect(updateBoxResize(drag,press,&result) && result == source,"a press without travel never snaps an off-grid box");
		point[axis] += maximum ? 23.75 : -23.75;
		ok &= expect(updateBoxResize(drag,rayAt(point,axis,perspective),&result),"outward resize succeeds");
		const double target = grid ? std::round(point[axis]/grid)*grid : point[axis];
		ok &= expect((maximum ? result.maxs[axis] : result.mins[axis]) == target,"moved face snaps to absolute map grid");
		for (int i = 0; i < 3; ++i) {
			if (i != axis) { ok &= expect(result.mins[i] == source.mins[i] && result.maxs[i] == source.maxs[i],"unaffected coordinates retain exact source precision"); }
		}
		ok &= expect((maximum ? result.mins[axis] : result.maxs[axis]) == (maximum ? source.mins[axis] : source.maxs[axis]),"opposite face remains fixed");
		point[axis] += maximum ? -10000 : 10000;
		ok &= expect(updateBoxResize(drag,rayAt(point,axis,perspective),&result)
			&& std::abs(result.maxs[axis]-result.mins[axis]-std::min(grid ? grid : 1.0,source.maxs[axis]-source.mins[axis])) < 1e-9,
			"crossing the opposite face clamps to the same minimum span as plan resizing");
		auto invalid = press; invalid.direction = {0,0,0}; const auto kept = result;
		ok &= expect(!updateBoxResize(drag,invalid,&result) && result == kept,"invalid ray leaves last valid result untouched");
	} } }
	BoxResizePoint point; BoxResizeDrag drag;
	ok &= boxResizeHandle(source,1,&point);
	const auto press = rayAt(point,0,true);
	ok &= expect(!beginBoxResize(source,1,{1,0,0},press,0,&drag),"axis facing straight at the camera has no honest drag plane");
	auto behind = press; behind.direction = {0,1,1};
	ok &= expect(!beginBoxResize(source,1,{1,1,1},behind,0,&drag),"perspective intersections behind the eye are refused");
	const double nan = std::numeric_limits<double>::quiet_NaN();
	ok &= expect(!beginBoxResize(source,-1,{1,1,1},press,0,&drag)
		&& !beginBoxResize(source,1,{1,1,1},press,nan,&drag)
		&& !beginBoxResize(source,1,{nan,1,1},press,0,&drag),"invalid handles, grids and directions are refused");
	const ResizeBox thin{{0,0,0},{0.125,4,0}};
	ok &= expect(!boxResizeHandle(thin,5,&point),"collapsed source axes do not advertise a resize handle");
	ok &= boxResizeHandle(thin,1,&point);
	ok &= beginBoxResize(thin,1,{1,1,1},rayAt(point,0,true),16,&drag);
	point[0] = -100;
	ResizeBox result;
	ok &= expect(updateBoxResize(drag,rayAt(point,0,true),&result) && result == thin,"sub-grid source thickness is preserved when clamping");
	const ResizeBox to{{-4.5,-8.25,0.125},{29.5,23.75,64.125}};
	const auto moved = resizeBoxPoint(source,to,{4,7.75,32.125});
	ok &= expect(moved == BoxResizePoint{12.5,7.75,32.125},"preview maps source points by the same affine bounds transform as committed resizing");
	std::cout << "36 axis/scale/projection cases and invalid-ray/thin-box checks completed.\n";
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
