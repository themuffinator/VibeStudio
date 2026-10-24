#include "app/level_vertex_insertion.h"
#include "core/level_component_selection.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio {
namespace {
using V = LevelMapVec3;
V sub(V a,V b) { return {a.x-b.x,a.y-b.y,a.z-b.z,true}; }
V add(V a,V b) { return {a.x+b.x,a.y+b.y,a.z+b.z,true}; }
V mul(V a,double s) { return {a.x*s,a.y*s,a.z*s,true}; }
double dot(V a,V b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
V cross(V a,V b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x,true}; }
double coordinate(V p,int axis) { return axis == 0 ? p.x : axis == 1 ? p.y : p.z; }
void assign(V& p,int axis,double value) { if (axis == 0) { p.x = value; } else if (axis == 1) { p.y = value; } else { p.z = value; } }
int dominant(V v) { return std::abs(v.x) >= std::abs(v.y) && std::abs(v.x) >= std::abs(v.z) ? 0 : std::abs(v.y) >= std::abs(v.z) ? 1 : 2; }
bool finite(QPointF p) { return std::isfinite(p.x()) && std::isfinite(p.y()); }
double squared(QPointF p) { return QPointF::dotProduct(p,p); }
bool inside(V p,const QVector<V>& polygon,V normal) {
	bool positive = false,negative = false;
	for (int i = 0; i < polygon.size(); ++i) {
		const auto edge = sub(polygon[(i+1)%polygon.size()],polygon[i]);
		const double side = dot(cross(edge,sub(p,polygon[i])),normal);
		const double tolerance = .01*std::sqrt(dot(edge,edge));
		positive |= side > tolerance; negative |= side < -tolerance;
	}
	return !(positive && negative);
}
}

LevelVertexInsertionHit LevelVertexInsertion::pick(const QPointF& point,const LevelComponentGizmoFrame& frame) const
{
	LevelVertexInsertionHit result;
	if (!m_available || !m_data || !frame.project || !frame.ray || !finite(point)) { return result; }
	const auto ray = frame.ray(point); const V origin{ray.origin.x,ray.origin.y,ray.origin.z,true},direction{ray.direction.x,ray.direction.y,ray.direction.z,true};
	const double rayLength = dot(direction,direction);
	if (!std::isfinite(rayLength) || rayLength < 1e-12) { return result; }
	double bestDepth = std::numeric_limits<double>::infinity(),bestDistance = std::numeric_limits<double>::infinity();
	// Edges have a screen-space tolerance; faces require a ray hit inside the
	// solved polygon. Never invent a point on the unbounded supporting plane.
	for (const auto kind : {LevelBrushComponent::Edge,LevelBrushComponent::Face}) {
		for (const auto& brush : *m_data) {
			const auto& topology = brush.topology;
			const auto count = kind == LevelBrushComponent::Edge ? topology.edges.size() : topology.faces.size();
			for (int index = 0; index < count; ++index) {
				QVector<V> corners; V position; double distance = 0;
				if (kind == LevelBrushComponent::Edge) {
					const auto edge = topology.edges[index]; const auto a = topology.vertices[edge[0]],b = topology.vertices[edge[1]];
					const auto sa = frame.project(a),sb = frame.project(b); if (!finite(sa) || !finite(sb) || squared(sb-sa) < 1e-8) { continue; }
					const double screenT = std::clamp(QPointF::dotProduct(point-sa,sb-sa)/squared(sb-sa),0.,1.);
					distance = squared(sa+(sb-sa)*screenT-point); if (distance > 81) { continue; }
					// Closest point on the 3D segment to the pick ray, preserving
					// perspective depth instead of interpolating screen endpoints.
					const auto edgeVector = sub(b,a),offset = sub(a,origin);
					const double ee = dot(edgeVector,edgeVector),ed = dot(edgeVector,direction);
					const double divisor = ee*rayLength-ed*ed; if (divisor < 1e-12) { continue; }
					double t = std::clamp((ed*dot(offset,direction)-rayLength*dot(offset,edgeVector))/divisor,0.,1.);
					const int axis = dominant(edgeVector);
					if (m_grid > 0) {
						const double snapped = std::round((coordinate(a,axis)+t*coordinate(edgeVector,axis))/m_grid)*m_grid;
						t = (snapped-coordinate(a,axis))/coordinate(edgeVector,axis);
					}
					if (t <= 0 || t >= 1) { continue; }
					position = add(a,mul(edgeVector,t)); corners = {a,b};
				} else {
					for (int vertex : topology.faces[index]) { corners << topology.vertices[vertex]; }
					if (corners.size() < 3) { continue; }
					auto normal = cross(sub(corners[1],corners[0]),sub(corners[2],corners[0])); const double length = std::sqrt(dot(normal,normal));
					if (length < 1e-8) { continue; } normal = mul(normal,1/length);
					const double divisor = dot(normal,direction); if (std::abs(divisor) < 1e-8) { continue; }
					const double t = dot(normal,sub(corners[0],origin))/divisor; if (ray.forwardOnly && t < 0) { continue; }
					position = add(origin,mul(direction,t)); if (!inside(position,corners,normal)) { continue; }
					if (m_grid > 0) {
						const int axis = dominant(normal); double remainder = dot(normal,corners[0]);
						for (int component = 0; component < 3; ++component) { if (component != axis) {
							assign(position,component,std::round(coordinate(position,component)/m_grid)*m_grid);
							remainder -= coordinate(normal,component)*coordinate(position,component);
						} }
						assign(position,axis,remainder/coordinate(normal,axis));
						if (!inside(position,corners,normal)) { continue; }
					}
					// A snapped boundary point belongs to the edge picker and its
					// incident brushes, not just this one face. It is offered when
					// the pointer is within the edge's screen-space pick radius.
					bool boundary = false;
					for (int corner = 0; corner < corners.size(); ++corner) {
						const auto a = corners[corner],edge = sub(corners[(corner+1)%corners.size()],a); const double lengthSquared = dot(edge,edge);
						if (lengthSquared < 1e-12) { continue; }
						const double along = std::clamp(dot(sub(position,a),edge)/lengthSquared,0.,1.);
						const auto difference = sub(position,add(a,mul(edge,along)));
						boundary |= dot(difference,difference) < .0001;
					}
					if (boundary) { continue; }
				}
				const auto screen = frame.project(position);
				if (!finite(screen) || (frame.visible && !frame.visible(position))) { continue; }
				const double depth = dot(sub(position,origin),direction)/rayLength;
				if (!std::isfinite(depth) || (ray.forwardOnly && depth < 0)) { continue; }
				if (std::any_of(topology.vertices.cbegin(),topology.vertices.cend(),[&](V v) { return dot(sub(v,position),sub(v,position)) < .0001; })) { continue; }
				const auto key = levelComponentGeometryKey(corners);
				if (result.position.valid && result.kind == kind && key == result.geometryKey) {
					if (!result.brushes.contains(brush.id)) { result.brushes << brush.id; } continue;
				}
				// Nearer surfaces win over edges behind them. At equal depth,
				// keep an edge hit so its incident brushes are edited together.
				if (depth > bestDepth+.01 || (std::abs(depth-bestDepth) <= .01 && (kind == LevelBrushComponent::Face || distance >= bestDistance))) { continue; }
				result = {position,kind,{brush.id},key}; bestDepth = depth; bestDistance = distance;
			}
		}
	}
	std::sort(result.brushes.begin(),result.brushes.end());
	return result;
}
} // namespace vibestudio
