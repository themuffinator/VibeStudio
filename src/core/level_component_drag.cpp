#include "core/level_component_drag.h"
#include <QCoreApplication>
#include <algorithm>
#include <cmath>

namespace vibestudio {
namespace {
using V = LevelMapVec3;
V add(V a,V b) { return {a.x+b.x,a.y+b.y,a.z+b.z,true}; }
V sub(V a,V b) { return {a.x-b.x,a.y-b.y,a.z-b.z,true}; }
V mul(V a,double s) { return {a.x*s,a.y*s,a.z*s,true}; }
double dot(V a,V b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
double coordinate(V p,int axis) { return axis == 0 ? p.x : axis == 1 ? p.y : p.z; }
V axisVector(int axis) { return {axis == 0 ? 1. : 0,axis == 1 ? 1. : 0,axis == 2 ? 1. : 0,true}; }
int dominant(V v) { return std::abs(v.x) >= std::abs(v.y) && std::abs(v.x) >= std::abs(v.z) ? 0 : std::abs(v.y) >= std::abs(v.z) ? 1 : 2; }
bool finite(V v) { return v.valid && std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool bounded(V v) { return finite(v) && std::max({std::abs(v.x),std::abs(v.y),std::abs(v.z)}) <= 32768; }
Qt::KeyboardModifiers keys(Qt::KeyboardModifiers modifiers) { return modifiers & (Qt::ShiftModifier|Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier); }
}
bool LevelComponentDrag::pick(ModelPickRay ray,V* position) const
{
	const V origin{ray.origin.x,ray.origin.y,ray.origin.z,true},direction{ray.direction.x,ray.direction.y,ray.direction.z,true};
	if (!position || !finite(origin) || !finite(direction) || dot(direction,direction) < 1e-12) { return false; }
	const auto anchor = sub(m_anchor,m_offset); V point;
	if (m_axis >= 0) {
		const auto axis = axisVector(m_axis),difference = sub(origin,anchor);
		const double dd = dot(direction,direction),along = dot(direction,axis),denominator = dd-along*along;
		if (denominator < 1e-10*dd) { return false; }
		const double rayDistance = (along*dot(difference,axis)-dot(difference,direction))/denominator;
		if (ray.forwardOnly && rayDistance < 0) { return false; }
		point = add(anchor,mul(axis,dot(difference,axis)+rayDistance*along));
	} else {
		const double divisor = coordinate(direction,m_planeAxis); if (std::abs(divisor) < 1e-8) { return false; }
		const double distance = (coordinate(anchor,m_planeAxis)-coordinate(origin,m_planeAxis))/divisor;
		if (ray.forwardOnly && distance < 0) { return false; }
		point = add(origin,mul(direction,distance));
	}
	point = add(point,m_offset); if (!finite(point)) { return false; }
	*position = point; return true;
}
bool LevelComponentDrag::begin(V origin,ModelPickRay ray,const LevelComponentDragOptions& options,Qt::KeyboardModifiers modifiers)
{
	const V direction{options.viewDirection.x,options.viewDirection.y,options.viewDirection.z,true};
	if (!bounded(origin) || !finite(direction) || dot(direction,direction) < 1e-12 || !std::isfinite(options.grid)
		|| options.grid < 0 || options.grid > 32768 || options.fixedAxis < -1 || options.fixedAxis > 2) { return false; }
	LevelComponentDrag draft; draft.m_options = options; draft.m_origin = draft.m_segmentOrigin = draft.m_accepted = draft.m_proposed = draft.m_anchor = origin;
	draft.m_offset = {0,0,0,true}; draft.m_modifiers = keys(modifiers);
	draft.m_planeAxis = options.perspective ? 2 : dominant(direction);
	draft.m_vertical = options.fixedAxis < 0 && options.perspective && modifiers.testFlag(Qt::AltModifier);
	draft.m_axis = options.fixedAxis >= 0 ? options.fixedAxis : draft.m_vertical ? 2 : -1;
	draft.m_absolute = options.absoluteAllowed && modifiers.testFlag(Qt::ControlModifier);
	V picked; if (!draft.pick(ray,&picked)) { return false; }
	draft.m_offset = sub(origin,picked); draft.m_started = true;
	*this = draft; return true;
}
bool LevelComponentDrag::update(ModelPickRay ray,Qt::KeyboardModifiers modifiers,V* delta)
{
	if (!m_started || !delta) { return false; }
	auto draft = *this; modifiers = keys(modifiers); bool resetOrigin = false;
	if (modifiers != draft.m_modifiers) {
		const bool vertical = draft.m_options.fixedAxis < 0 && draft.m_options.perspective && modifiers.testFlag(Qt::AltModifier);
		int axis = draft.m_options.fixedAxis >= 0 ? draft.m_options.fixedAxis : vertical ? 2 : -1;
		if (axis < 0 && modifiers.testFlag(Qt::ShiftModifier)) {
			const auto displacement = sub(draft.m_accepted,draft.m_segmentOrigin); const int strongest = dominant(displacement);
			const double largest = std::abs(coordinate(displacement,strongest));
			bool tied = false;
			for (int i = 0; i < 3; ++i) { if (i != strongest && std::abs(std::abs(coordinate(displacement,i))-largest) < 1e-9) { tied = true; } }
			if (!tied) { axis = strongest; }
		}
		const bool absolute = draft.m_options.absoluteAllowed && modifiers.testFlag(Qt::ControlModifier);
		const bool constrained = axis >= 0 && !vertical && draft.m_options.fixedAxis < 0;
		const bool wasConstrained = draft.m_axis >= 0 && !draft.m_vertical && draft.m_options.fixedAxis < 0;
		const bool snapChanged = modifiers.testFlag(Qt::ControlModifier) != draft.m_modifiers.testFlag(Qt::ControlModifier);
		if (constrained != wasConstrained || vertical != draft.m_vertical || snapChanged) {
			resetOrigin = draft.m_vertical && !vertical;
			draft.m_anchor = axis >= 0 && !vertical && draft.m_options.fixedAxis < 0 ? draft.m_segmentOrigin : draft.m_accepted;
		} else { axis = draft.m_axis; }
		draft.m_axis = axis; draft.m_vertical = vertical; draft.m_absolute = absolute; draft.m_modifiers = modifiers;
	}
	V position; if (!draft.pick(ray,&position)) { return false; }
	if (draft.m_options.grid > 0) {
		const auto basis = draft.m_absolute ? V{0,0,0,true} : draft.m_segmentOrigin;
		const auto offset = sub(position,basis); const double grid = draft.m_options.grid;
		position = add(basis,{std::round(offset.x/grid)*grid,std::round(offset.y/grid)*grid,std::round(offset.z/grid)*grid,true});
	}
	if (!bounded(position)) { return false; }
	// HandleDragTracker resets the relative basis to the new snapped proposal
	// when leaving vertical mode, after constructing the replacement picker.
	if (resetOrigin) { draft.m_segmentOrigin = position; }
	draft.m_proposed = position; *delta = sub(position,draft.m_origin); *this = draft; return true;
}
void LevelComponentDrag::accept(const V& delta)
{
	const auto position = add(m_origin,delta),difference = sub(position,m_proposed);
	const auto requested = sub(m_proposed,m_origin);
	// The camera's existing service boundary transports ModelVec3 floats.
	const bool transported = delta.x == double(float(requested.x)) && delta.y == double(float(requested.y)) && delta.z == double(float(requested.z));
	if (m_started && finite(delta) && bounded(position) && (dot(difference,difference) < 1e-8 || transported)) { m_accepted = position; }
}
QString LevelComponentDrag::description() const
{
	const auto direction = m_axis >= 0 ? QCoreApplication::translate("LevelComponentDrag","%1 axis").arg(QString(QLatin1Char("XYZ"[m_axis])))
		: QCoreApplication::translate("LevelComponentDrag","%1 plane").arg(m_planeAxis == 0 ? QStringLiteral("YZ") : m_planeAxis == 1 ? QStringLiteral("XZ") : QStringLiteral("XY"));
	return direction+QStringLiteral(" · ")+(m_options.grid == 0 ? QCoreApplication::translate("LevelComponentDrag","Grid off")
		: m_absolute ? QCoreApplication::translate("LevelComponentDrag","Absolute grid") : QCoreApplication::translate("LevelComponentDrag","Relative grid"));
}
bool levelComponentDragChord(int componentMode,Qt::KeyboardModifiers modifiers)
{
	modifiers = keys(modifiers);
	if (componentMode < 1 || componentMode > 3 || modifiers.testFlag(Qt::MetaModifier)) { return false; }
	return componentMode == 1 || (modifiers & ~(Qt::AltModifier)) == Qt::NoModifier;
}
bool levelComponentModifierKey(int key) { return key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Meta; }
Qt::KeyboardModifiers levelComponentModifierState(int key,Qt::KeyboardModifiers modifiers,bool pressed)
{
	const auto flag = key == Qt::Key_Shift ? Qt::ShiftModifier : key == Qt::Key_Control ? Qt::ControlModifier : key == Qt::Key_Alt ? Qt::AltModifier : key == Qt::Key_Meta ? Qt::MetaModifier : Qt::NoModifier;
	if (flag != Qt::NoModifier) { modifiers.setFlag(flag,pressed); } return keys(modifiers);
}
} // namespace vibestudio
