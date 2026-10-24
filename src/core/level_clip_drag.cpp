#include "core/level_clip_drag.h"
#include <algorithm>
#include <cmath>

namespace vibestudio {
namespace {
std::array<double,3> xyz(LevelMapVec3 p) { return {p.x,p.y,p.z}; }
std::array<double,3> xyz(ModelVec3 p) { return {p.x,p.y,p.z}; }
bool finite(const std::array<double,3>& p) { return std::all_of(p.begin(),p.end(),[](double v) { return std::isfinite(v); }); }
double roundEven(double value)
{
	const double lower = std::floor(value), fraction = value-lower;
	return fraction < .5 || (fraction == .5 && std::fmod(lower,2) == 0) ? lower : lower+1;
}
}
bool LevelClipPointDrag::pick(ModelPickRay ray,const std::array<double,3>& normal,LevelMapVec3* point) const
{
	const auto eye = xyz(ray.origin), direction = xyz(ray.direction), origin = xyz(m_origin);
	if (!point || !finite(eye) || !finite(direction)) { return false; }
	const double length = std::hypot(direction[0],direction[1],direction[2]);
	double numerator = 0,denominator = 0;
	for (int i = 0; i < 3; ++i) { numerator += (origin[i]-eye[i])*normal[i]; denominator += direction[i]*normal[i]; }
	if (!std::isfinite(length) || length < 1e-12 || !std::isfinite(numerator) || !std::isfinite(denominator) || std::abs(denominator) < length*1e-8) { return false; }
	const double distance = numerator/denominator;
	if (!std::isfinite(distance) || (ray.forwardOnly && distance < 0)) { return false; }
	std::array<double,3> hit{};
	for (int i = 0; i < 3; ++i) { hit[i] = eye[i]+direction[i]*distance; }
	if (!finite(hit)) { return false; }
	*point = {hit[0],hit[1],hit[2],true}; return true;
}
bool LevelClipPointDrag::begin(LevelMapVec3 point,ModelPickRay ray,int planeAxis,double grid)
{
	*this = {};
	if (!point.valid || !finite(xyz(point)) || planeAxis < 0 || planeAxis > 2 || !std::isfinite(grid) || grid < 0) { return false; }
	m_origin = point; m_axis = planeAxis; m_grid = grid; m_planeNormal[m_axis] = 1;
	const auto eye = xyz(ray.origin), origin = xyz(point);
	for (int i = 0; i < 3; ++i) { m_axisNormal[i] = i == m_axis ? 0 : eye[i]-origin[i]; }
	const double length = std::hypot(m_axisNormal[0],m_axisNormal[1],m_axisNormal[2]);
	if (std::isfinite(length) && length > 1e-12) {
		for (auto& component : m_axisNormal) { component /= length; }
		pick(ray,m_axisNormal,&m_axisStart);
	}
	pick(ray,m_planeNormal,&m_planeStart);
	m_started = m_planeStart.valid || m_axisStart.valid; return m_started;
}
bool LevelClipPointDrag::update(ModelPickRay ray,bool vertical,bool axisOnly,LevelMapVec3* point) const
{
	const auto start = vertical ? m_axisStart : m_planeStart;
	LevelMapVec3 hit;
	if (!m_started || !point || !start.valid || !pick(ray,vertical ? m_axisNormal : m_planeNormal,&hit)) { return false; }
	const auto a = xyz(start), b = xyz(hit), origin = xyz(m_origin);
	std::array<double,3> delta{};
	for (int i = 0; i < 3; ++i) { if ((i == m_axis) == vertical) { delta[i] = b[i]-a[i]; } }
	if (!finite(delta)) { return false; }
	if (axisOnly) {
		const int axis = int(std::max_element(delta.begin(),delta.end(),[](double x,double y) { return std::abs(x) < std::abs(y); })-delta.begin());
		for (int i = 0; i < 3; ++i) { if (i != axis) { delta[i] = 0; } }
	}
	std::array<double,3> result{};
	for (int i = 0; i < 3; ++i) {
		if (m_grid > 0) {
			const double steps = delta[i]/m_grid; if (!std::isfinite(steps)) { return false; }
			delta[i] = roundEven(steps)*m_grid;
		}
		result[i] = origin[i]+delta[i];
	}
	if (!finite(result)) { return false; }
	*point = {result[0],result[1],result[2],true}; return true;
}
} // namespace vibestudio
