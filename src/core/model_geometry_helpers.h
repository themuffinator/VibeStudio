#pragma once

#include "core/model_mesh.h"
#include <cmath>

// Internal double-precision geometry shared by boundary authoring and diagnostics.
namespace vibestudio::model_geometry
{
struct P2
{
	double x, y;
};
struct P3
{
	double x, y, z;
};
inline P3 point(ModelVec3 p)
{
	return {p.x, p.y, p.z};
}
inline P3 operator+(P3 a, P3 b)
{
	return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline P3 operator-(P3 a, P3 b)
{
	return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline P3 operator*(P3 a, double b)
{
	return {a.x * b, a.y * b, a.z * b};
}
inline double dot(P3 a, P3 b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline P3 cross(P3 a, P3 b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double length(P3 a)
{
	return std::sqrt(dot(a, a));
}
inline P2 project(P3 p, int axis)
{
	return axis == 0 ? P2{p.y, p.z} : axis == 1 ? P2{p.z, p.x} : P2{p.x, p.y};
}
inline double orient(P2 a, P2 b, P2 c)
{
	return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}
inline double area(const QVector<P2> &points)
{
	double sum = 0;
	for (int i = 1; i + 1 < points.size(); ++i)
	{
		sum += orient(points[0], points[i], points[i + 1]);
	}
	return sum;
}
// Match editable-model admission exactly, including float coordinate arithmetic.
// Kept here so import repair cannot disagree with the document's collapse rule.
inline bool collapsedTriangle(ModelVec3 a, ModelVec3 b, ModelVec3 c)
{
	const ModelVec3 ab{b.x - a.x, b.y - a.y, b.z - a.z}, ac{c.x - a.x, c.y - a.y, c.z - a.z};
	const ModelVec3 n{ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x};
	return double(n.x) * n.x + double(n.y) * n.y + double(n.z) * n.z < 1e-20;
}
} // namespace vibestudio::model_geometry
