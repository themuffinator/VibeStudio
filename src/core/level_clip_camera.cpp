#include "core/level_clip_camera.h"
#include "core/map_geometry.h"
#include <QCoreApplication>
#include <algorithm>
#include <array>
#include <cmath>

namespace vibestudio {
bool levelCameraClipAxis(const QVector<LevelMapVec3>& points,const LevelMapVec3& eye,const LevelMapVec3& forward,
	const LevelMapVec3& mins,const LevelMapVec3& maxs,int* axis,int* direction,bool preserveAxis,QString* error)
{
	if (error) { error->clear(); }
	const auto fail = [error](const char* text) { if (error) { *error = QCoreApplication::translate("LevelClipper",text); } return false; };
	const auto finite = [](const LevelMapVec3& p) { return p.valid && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); };
	const auto xyz = [](const LevelMapVec3& p) { return std::array<double,3>{p.x,p.y,p.z}; };
	if (!axis || !direction || points.size() != 2 || !finite(points[0]) || !finite(points[1]) || !finite(eye) || !finite(forward)
		|| (preserveAxis && (*axis < 0 || *axis > 2 || (*direction != 1 && *direction != -1)))) {
		return fail(QT_TRANSLATE_NOOP("LevelClipper","A camera clip plane needs two finite points, a camera position and a non-zero viewing direction."));
	}
	const auto a = xyz(points[0]), b = xyz(points[1]), origin = xyz(eye); auto view = xyz(forward);
	const double viewLength = std::hypot(view[0],view[1],view[2]);
	const std::array<double,3> segment{b[0]-a[0],b[1]-a[1],b[2]-a[2]};
	const double length = std::hypot(segment[0],segment[1],segment[2]);
	if (!std::isfinite(viewLength) || viewLength < 1e-12 || !std::isfinite(length) || length <= .001) {
		return fail(QT_TRANSLATE_NOOP("LevelClipper","Separate the two clip points and use a non-zero camera viewing direction."));
	}
	for (auto& v : view) { v /= viewLength; }
	const auto dominant = [](const auto& v) { return int(std::max_element(v.begin(),v.end(),[](double x,double y) { return std::abs(x) < std::abs(y); })-v.begin()); };
	int resultAxis = preserveAxis ? *axis : 2, resultDirection = preserveAxis ? *direction : 1;
	const auto fromView = [&](const auto& v) { resultAxis = dominant(v); resultDirection = v[resultAxis] > 0 ? -1 : 1; };
	const auto parallel = [&] { return std::abs(segment[resultAxis]/length) > .999; };
	if (!preserveAxis) { fromView(view); }
	if (parallel()) {
		fromView(view);
		if (parallel()) {
			// Perspective near-plane projection needs only eye and forward; FOV
			// and viewport dimensions cancel when converting back to world space.
			double depthA = 0,depthB = 0;
			for (int i = 0; i < 3; ++i) { depthA += (a[i]-origin[i])*view[i]; depthB += (b[i]-origin[i])*view[i]; }
			if (!std::isfinite(depthA) || !std::isfinite(depthB) || depthA <= 1e-6 || depthB <= 1e-6) { return fail(QT_TRANSLATE_NOOP("LevelClipper","Move both clip points in front of the camera before choosing a two-point plane.")); }
			std::array<double,3> projected{};
			for (int i = 0; i < 3; ++i) { projected[i] = ((b[i]-origin[i])/depthB-(a[i]-origin[i])/depthA)*(depthB > depthA ? -1 : 1); }
			const double projectedLength = std::hypot(projected[0],projected[1],projected[2]);
			if (!std::isfinite(projectedLength) || projectedLength < 1e-12) {
				return fail(QT_TRANSLATE_NOOP("LevelClipper","The clip points overlap along the viewing ray. Move a point sideways or add a third point."));
			}
			fromView(projected);
		}
	}
	if (mins.valid || maxs.valid) {
		if (!finite(mins) || !finite(maxs)) { return fail(QT_TRANSLATE_NOOP("LevelClipper","The selected geometry has invalid clipping bounds.")); }
		const auto low = xyz(mins), high = xyz(maxs);
		for (int i = 0; i < 3; ++i) { if (low[i] > high[i] || !std::isfinite(high[i]-low[i])) { return fail(QT_TRANSLATE_NOOP("LevelClipper","The selected geometry has invalid or reversed clipping bounds.")); } }
		LevelMapVec3 p,q,r;
		if (!levelMapClipPlane(points,resultAxis,resultDirection,&p,&q,&r)) { return fail(QT_TRANSLATE_NOOP("LevelClipper","The two points still need a third point to define a clip plane.")); }
		const auto plane = planeFromPoints(q,p,r); const std::array<double,3> normal{plane.normalX,plane.normalY,plane.normalZ};
		const int faceAxis = dominant(normal); const double anchor = normal[faceAxis]*plane.distance;
		if (std::abs(normal[faceAxis]) > .999) {
			if (anchor > low[faceAxis]+(high[faceAxis]-low[faceAxis])*.5 && anchor-high[faceAxis] > -.1) { resultAxis = faceAxis; resultDirection = 1; }
			else if (anchor <= low[faceAxis]+(high[faceAxis]-low[faceAxis])*.5 && low[faceAxis]-anchor > -.1) { resultAxis = faceAxis; resultDirection = -1; }
		}
	}
	if (!levelMapClipPlane(points,resultAxis,resultDirection,nullptr,nullptr,nullptr)) {
		return fail(QT_TRANSLATE_NOOP("LevelClipper","The two points still need a third point to define a clip plane."));
	}
	*axis = resultAxis; *direction = resultDirection; return true;
}
} // namespace vibestudio
