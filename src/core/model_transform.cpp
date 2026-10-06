#include "core/model_transform.h"

#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace vibestudio
{
namespace
{
using Vector = std::array<double, 3>;
Vector vector(ModelVec3 p) { return {p.x, p.y, p.z}; }
double dot(Vector a, Vector b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
bool finite(Vector a)
{
	return std::all_of(a.begin(), a.end(), [](double v) { return std::isfinite(v); });
}
bool normalize(Vector *v)
{
	const double length = std::sqrt(dot(*v, *v));
	if (!std::isfinite(length) || length < 1e-6)
	{
		return false;
	}
	for (auto &value : *v)
	{
		value /= length;
	}
	return true;
}
bool validGrid(double grid) { return std::isfinite(grid) && (grid == 0 || (grid >= 0.000001 && grid <= 1000000)); }
bool intersect(const ModelMoveDrag &drag, ModelPickRay ray, Vector *result)
{
	const auto origin = vector(ray.origin), direction = vector(ray.direction);
	if (!finite(origin) || !finite(direction))
	{
		return false;
	}
	const double denominator = dot(direction, drag.normal);
	if (!std::isfinite(denominator) || std::abs(denominator) < 1e-6)
	{
		return false;
	}
	Vector delta;
	for (int i = 0; i < 3; ++i)
	{
		delta[i] = drag.origin[i] - origin[i];
	}
	const double t = dot(delta, drag.normal) / denominator;
	if (ray.forwardOnly && t <= 0)
	{
		return false;
	}
	Vector point;
	for (int i = 0; i < 3; ++i)
	{
		point[i] = origin[i] + direction[i] * t;
	}
	if (!finite(point) || std::any_of(point.begin(), point.end(), [](double p) { return std::abs(p) > 4000000; }))
	{
		return false;
	}
	*result = point;
	return true;
}
} // namespace

ModelVec3 rotateModelVector(ModelVec3 p, ModelVec3 degrees)
{
	const double rx = degrees.x * std::numbers::pi / 180, ry = degrees.y * std::numbers::pi / 180, rz = degrees.z * std::numbers::pi / 180;
	const ModelVec3 x{p.x, float(p.y * std::cos(rx) - p.z * std::sin(rx)), float(p.y * std::sin(rx) + p.z * std::cos(rx))};
	const ModelVec3 y{float(x.x * std::cos(ry) + x.z * std::sin(ry)), x.y, float(-x.x * std::sin(ry) + x.z * std::cos(ry))};
	return {float(y.x * std::cos(rz) - y.y * std::sin(rz)), float(y.x * std::sin(rz) + y.y * std::cos(rz)), y.z};
}
ModelVec3 transformModelPoint(ModelVec3 p, const ModelTransform &t)
{
	const auto delta = modelBasisToWorld(t.translation, t.basis);
	if (!t.basis.world && t.scale.x == 1 && t.scale.y == 1 && t.scale.z == 1 &&
		t.rotation.x == 0 && t.rotation.y == 0 && t.rotation.z == 0)
		return {p.x + delta.x, p.y + delta.y, p.z + delta.z};
	p = modelBasisFromWorld({p.x - t.pivot.x, p.y - t.pivot.y, p.z - t.pivot.z}, t.basis);
	p = rotateModelVector({p.x * t.scale.x, p.y * t.scale.y, p.z * t.scale.z}, t.rotation);
	p = modelBasisToWorld(p, t.basis);
	return {p.x + t.pivot.x + delta.x, p.y + t.pivot.y + delta.y, p.z + t.pivot.z + delta.z};
}
ModelVec3 transformModelNormal(ModelVec3 n, const ModelTransform &t)
{
	if (t.basis.world || t.scale.x != 1 || t.scale.y != 1 || t.scale.z != 1 ||
		t.rotation.x != 0 || t.rotation.y != 0 || t.rotation.z != 0)
	{
		n = modelBasisFromWorld(n, t.basis);
		n = rotateModelVector({n.x / t.scale.x, n.y / t.scale.y, n.z / t.scale.z}, t.rotation);
		n = modelBasisToWorld(n, t.basis);
	}
	const double length = std::sqrt(double(n.x) * n.x + double(n.y) * n.y + double(n.z) * n.z);
	return length > 1e-20 ? ModelVec3{float(n.x / length), float(n.y / length), float(n.z / length)} : ModelVec3{0, 0, 1};
}
bool validModelTransformBasis(const ModelTransformBasis &basis)
{
	for (int i = 0; i < 3; ++i)
	{
		const auto a = vector(basis.axes[i]);
		if (!finite(a) || std::abs(dot(a, a) - 1) > 0.00001)
			return false;
		for (int j = 0; j < 3; ++j)
		{
			if (i != j && std::abs(dot(a, vector(basis.axes[j]))) > 0.00001)
				return false;
			if (basis.world && a[j] != (i == j ? 1 : 0))
				return false;
		}
	}
	return true;
}
ModelVec3 modelBasisToWorld(ModelVec3 p, const ModelTransformBasis &basis)
{
	if (basis.world)
		return p;
	const auto &a = basis.axes;
	return {float(double(p.x) * a[0].x + double(p.y) * a[1].x + double(p.z) * a[2].x),
			float(double(p.x) * a[0].y + double(p.y) * a[1].y + double(p.z) * a[2].y),
			float(double(p.x) * a[0].z + double(p.y) * a[1].z + double(p.z) * a[2].z)};
}
ModelVec3 modelBasisFromWorld(ModelVec3 p, const ModelTransformBasis &basis)
{
	if (basis.world)
		return p;
	return {float(dot(vector(p), vector(basis.axes[0]))), float(dot(vector(p), vector(basis.axes[1]))),
			float(dot(vector(p), vector(basis.axes[2])))};
}
ModelVec3 rotateModelTransformVector(ModelVec3 p, const ModelTransform &t)
{
	if (!t.basis.world && t.rotation.x == 0 && t.rotation.y == 0 && t.rotation.z == 0)
		return p;
	return modelBasisToWorld(rotateModelVector(modelBasisFromWorld(p, t.basis), t.rotation), t.basis);
}
bool modelTransformPivot(const QVector<ModelVec3> &positions, const QSet<int> &vertices, ModelTransformPivot mode, ModelVec3 custom,
						 ModelVec3 *result)
{
	if (!result || vertices.isEmpty() || int(mode) < 0 || int(mode) > 2 || !finite(vector(custom)))
	{
		return false;
	}
	Vector low{INFINITY, INFINITY, INFINITY}, high{-INFINITY, -INFINITY, -INFINITY};
	for (int i : vertices)
	{
		if (i < 0 || i >= positions.size() || !finite(vector(positions[i])))
		{
			return false;
		}
		const auto p = vector(positions[i]);
		for (int axis = 0; axis < 3; ++axis)
		{
			low[axis] = std::min(low[axis], p[axis]);
			high[axis] = std::max(high[axis], p[axis]);
		}
	}
	*result = mode == ModelTransformPivot::Origin ? ModelVec3{}
			  : mode == ModelTransformPivot::Custom
				  ? custom
				  : ModelVec3{float((low[0] + high[0]) / 2), float((low[1] + high[1]) / 2), float((low[2] + high[2]) / 2)};
	return true;
}
bool snapModelRotation(ModelVec3 degrees, double step, ModelVec3 *result, QString *error)
{
	if (error)
	{
		error->clear();
	}
	auto values = vector(degrees);
	if (!result || !finite(values) || !std::isfinite(step) || step < 0 || step > 180 || (step > 0 && step < 0.000001))
	{
		if (error)
		{
			*error = QCoreApplication::translate("VibeStudioModelDocument",
												 "Rotation snapping requires finite angles and zero or 0.000001–180 degrees.");
		}
		return false;
	}
	if (step > 0)
	{
		for (double &v : values)
		{
			v = std::round(v / step) * step;
		}
	}
	if (std::any_of(values.begin(), values.end(), [](double v) { return std::abs(v) > 1000000; }))
	{
		if (error)
		{
			*error =
				QCoreApplication::translate("VibeStudioModelDocument", "Snapped rotation angles must remain within ±1,000,000 degrees.");
		}
		return false;
	}
	*result = {float(values[0]), float(values[1]), float(values[2])};
	return true;
}
bool snapModelScale(ModelVec3 factors, double step, ModelVec3 *result, QString *error)
{
	if (error)
	{
		error->clear();
	}
	auto values = vector(factors);
	const auto fail = [&]
	{
		if (error)
		{
			*error = QCoreApplication::translate(
				"VibeStudioModelDocument",
				"Scale snapping requires a step of zero or 0.000001–10000. Resulting scale magnitudes must be between 0.000001 and 10000.");
		}
		return false;
	};
	if (!result || !finite(values) || !std::isfinite(step) || step < 0 || step > 10000 || (step > 0 && step < 0.000001))
	{
		return fail();
	}
	if (step > 0)
	{
		for (double &v : values)
		{
			v = 1 + std::round((v - 1) / step) * step;
		}
	}
	if (std::any_of(values.begin(), values.end(), [](double v) { return std::abs(v) < double(1e-6f) || std::abs(v) > 10000; }))
	{
		return fail();
	}
	*result = {float(values[0]), float(values[1]), float(values[2])};
	return true;
}

bool snapModelTranslation(ModelVec3 translation, double grid, ModelVec3 *result, QString *error)
{
	if (error)
	{
		error->clear();
	}
	auto values = vector(translation);
	if (!result || !validGrid(grid) || !finite(values))
	{
		if (error)
		{
			*error = QCoreApplication::translate("VibeStudioModelDocument",
												 "Use a finite translation and a snap step of zero or 0.000001–1,000,000 units.");
		}
		return false;
	}
	if (grid > 0)
	{
		for (double &value : values)
		{
			value = std::round(value / grid) * grid;
		}
	}
	*result = {float(values[0]), float(values[1]), float(values[2])};
	return true;
}

bool beginModelMoveDrag(ModelVec3 pivot, ModelVec3 viewDirection, ModelMoveConstraint constraint, ModelPickRay ray, double grid,
						ModelMoveDrag *result)
{
	if (!result || !finite(vector(pivot)) || !validGrid(grid) || int(constraint) < 0 || int(constraint) > 3)
	{
		return false;
	}
	ModelMoveDrag drag;
	drag.origin = vector(pivot);
	drag.normal = vector(viewDirection);
	drag.constraint = constraint;
	drag.grid = grid;
	if (!normalize(&drag.normal))
	{
		return false;
	}
	if (constraint != ModelMoveConstraint::ViewPlane)
	{
		drag.axis[int(constraint)] = 1;
		drag.normal[int(constraint)] = 0;
		if (!normalize(&drag.normal))
		{
			return false;
		}
	}
	if (!intersect(drag, ray, &drag.start))
	{
		return false;
	}
	*result = drag;
	return true;
}

bool modelMoveDragDelta(const ModelMoveDrag &drag, ModelPickRay ray, ModelVec3 *translation)
{
	Vector point;
	if (!translation || !intersect(drag, ray, &point))
	{
		return false;
	}
	for (int i = 0; i < 3; ++i)
	{
		point[i] -= drag.start[i];
	}
	if (drag.constraint != ModelMoveConstraint::ViewPlane)
	{
		const double length = dot(point, drag.axis);
		for (int i = 0; i < 3; ++i)
		{
			point[i] = drag.axis[i] * length;
		}
	}
	return snapModelTranslation({float(point[0]), float(point[1]), float(point[2])}, drag.grid, translation);
}

bool beginModelRotateDrag(ModelVec3 pivot, int axis, ModelPickRay ray, double grid, ModelRotateDrag *result)
{
	ModelVec3 checked;
	if (!result || axis < 0 || axis > 2 || !finite(vector(pivot)) || !snapModelRotation({}, grid, &checked))
	{
		return false;
	}
	ModelRotateDrag candidate;
	candidate.axis = axis;
	candidate.grid = grid;
	candidate.plane.origin = vector(pivot);
	candidate.plane.normal[axis] = 1;
	if (!intersect(candidate.plane, ray, &candidate.previous))
	{
		return false;
	}
	for (int i = 0; i < 3; ++i)
	{
		candidate.previous[i] -= candidate.plane.origin[i];
	}
	if (!normalize(&candidate.previous))
	{
		return false;
	}
	*result = candidate;
	return true;
}
bool modelRotateDragAngle(ModelRotateDrag *drag, ModelPickRay ray, double *degrees)
{
	if (!drag || !degrees || drag->axis < 0 || drag->axis > 2)
	{
		return false;
	}
	Vector current;
	if (!intersect(drag->plane, ray, &current))
	{
		return false;
	}
	for (int i = 0; i < 3; ++i)
	{
		current[i] -= drag->plane.origin[i];
	}
	if (!normalize(&current))
	{
		return false;
	}
	const auto &p = drag->previous;
	const Vector cross{p[1] * current[2] - p[2] * current[1], p[2] * current[0] - p[0] * current[2], p[0] * current[1] - p[1] * current[0]};
	const double angle = drag->degrees + std::atan2(cross[drag->axis], dot(p, current)) * 180 / std::numbers::pi;
	ModelVec3 snapped;
	if (!snapModelRotation({float(angle), 0, 0}, drag->grid, &snapped))
	{
		return false;
	}
	drag->degrees = angle;
	drag->previous = current;
	*degrees = snapped.x;
	return true;
}
} // namespace vibestudio
