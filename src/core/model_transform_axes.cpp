#include "core/model_transform_axes.h"
#include "core/model_collision.h"
#include "core/model_document.h"
#include "core/model_surface_selection.h"
#include "core/model_tags.h"

#include <QCoreApplication>
#include <algorithm>
#include <array>
#include <cmath>

namespace vibestudio
{
namespace
{
struct Text { Q_DECLARE_TR_FUNCTIONS(ModelTransformAxes) };
using Vector = std::array<double, 3>;
Vector difference(ModelVec3 a, ModelVec3 b) { return {double(a.x) - b.x, double(a.y) - b.y, double(a.z) - b.z}; }
double dot(Vector a, Vector b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vector cross(Vector a, Vector b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
bool normalize(Vector *v)
{
	const double length = std::sqrt(dot(*v, *v));
	if (!std::isfinite(length) || length <= 1e-20)
		return false;
	for (auto &value : *v)
		value /= length;
	return true;
}
bool fail(QString *error, const QString &message)
{
	if (error) *error = message;
	return false;
}
bool basis(Vector x, Vector y, Vector z, ModelTransformBasis *result)
{
	if (!normalize(&x) || !normalize(&y) || !normalize(&z))
		return false;
	// Remove native-tag roundoff without changing its handedness or X axis.
	const double projection = dot(x, y);
	for (int i = 0; i < 3; ++i) y[i] -= x[i] * projection;
	if (!normalize(&y)) return false;
	auto normal = cross(x, y);
	const double handedness = dot(normal, z);
	if (!std::isfinite(handedness) || std::abs(handedness) < 0.999)
		return false;
	if (handedness < 0)
		for (auto &value : normal) value = -value;
	ModelTransformBasis candidate;
	candidate.world = false;
	candidate.axes = {{{float(x[0]), float(x[1]), float(x[2])}, {float(y[0]), float(y[1]), float(y[2])},
		{float(normal[0]), float(normal[1]), float(normal[2])}}};
	if (!validModelTransformBasis(candidate)) return false;
	*result = candidate;
	return true;
}
} // namespace
bool validModelTransformAxesOptions(const ModelEdit &edit, QString *error)
{
	const bool rotated = edit.axisRotation.x != 0 || edit.axisRotation.y != 0 || edit.axisRotation.z != 0;
	const bool specified = edit.transformSpace != ModelTransformSpace::World || rotated || edit.axesFrame != -1;
	const bool supported = edit.kind == ModelEditKind::Transform || edit.kind == ModelEditKind::TransformTag ||
		edit.kind == ModelEditKind::TransformCollisionBox || edit.kind == ModelEditKind::Extrude || edit.kind == ModelEditKind::DuplicateFaces;
	ModelVec3 checked;
	if ((specified && !supported) || int(edit.transformSpace) < 0 || int(edit.transformSpace) > 2 ||
		(rotated && edit.transformSpace != ModelTransformSpace::Custom) || edit.axesFrame < -1 ||
		(edit.axesFrame != -1 && edit.transformSpace != ModelTransformSpace::Selection) ||
		!snapModelRotation(edit.axisRotation, 0, &checked))
	{
		return fail(error, Text::tr("Transform axes apply to mesh, tag or collision transforms, face extrusion and duplication. Use world, selection or custom axes; axis rotation requires custom axes and a reference pose requires selection axes."));
	}
	return true;
}
bool resolveModelTransformAxes(const ModelMesh &mesh, const ModelEdit &edit, ModelTransformBasis *result, QString *error,
							   const ModelWorkControl &control)
{
	if (error) error->clear();
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!work.check() || !validModelTransformAxesOptions(edit, error)) return false;
	if (!result) return fail(error, Text::tr("No transform axes output was provided."));
	ModelTransformBasis candidate;
	if (edit.transformSpace == ModelTransformSpace::World)
	{
		*result = candidate;
		return true;
	}
	if (edit.transformSpace == ModelTransformSpace::Custom)
	{
		candidate.world = false;
		for (auto &axis : candidate.axes) axis = rotateModelVector(axis, edit.axisRotation);
		if (!validModelTransformBasis(candidate)) return fail(error, Text::tr("Custom axes must form a finite orthonormal basis."));
		*result = candidate;
		return true;
	}
	const int reference = edit.axesFrame >= 0 ? edit.axesFrame : edit.frame >= 0 ? edit.frame : edit.pivotFrame;
	const auto &selected = edit.selection;
	if (reference < 0 || reference >= mesh.frames.size() || !validModelSurfaceSelection(mesh, selected) ||
		!validModelTagSelection(mesh, selected) || !validModelCollisionSelection(mesh, selected))
		return fail(error, Text::tr("Choose a valid selection and reference pose for transform axes."));
	if (!selected.tag.isEmpty())
	{
		const auto tag = findModelTag(mesh, selected.tag, reference);
		if (!tag || !basis({tag->axis[0], tag->axis[1], tag->axis[2]}, {tag->axis[3], tag->axis[4], tag->axis[5]},
			{tag->axis[6], tag->axis[7], tag->axis[8]}, &candidate))
			return fail(error, Text::tr("The selected tag does not provide usable transform axes."));
		*result = candidate;
		return true;
	}
	if (!selected.collision.isEmpty())
	{
		const auto box = findModelCollisionBox(mesh, selected.collision);
		if (!box) return fail(error, Text::tr("Select an existing collision box for transform axes."));
		ModelCollisionBox pose;
		if (!sampleModelCollisionBox(*box, reference, reference, 0, &pose))
			return fail(error, Text::tr("The collision axes reference pose is unavailable."));
		candidate.axes = modelCollisionAxes(pose);
		candidate.world = false;
		if (!validModelTransformBasis(candidate)) return fail(error, Text::tr("The selected collision box does not provide usable transform axes."));
		*result = candidate;
		return true;
	}
	int surfaceIndex = selected.surface;
	if (!selected.surfaces.isEmpty() && !selected.surfaces.contains(surfaceIndex))
		surfaceIndex = *std::min_element(selected.surfaces.cbegin(), selected.surfaces.cend());
	if (surfaceIndex < 0 || surfaceIndex >= mesh.surfaces.size())
		return fail(error, Text::tr("Select mesh components or a whole surface for transform axes."));
	const auto &surface = mesh.surfaces[surfaceIndex];
	if (reference >= surface.frames.size()) return fail(error, Text::tr("The axes reference pose is unavailable on the selected surface."));
	const auto &positions = surface.frames[reference].positions;
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		if (!work.step()) return false;
		const auto triangle = surface.triangles[face];
		const std::array<int, 3> indices{triangle.a, triangle.b, triangle.c};
		bool touches = !selected.surfaces.isEmpty() || selected.faces.contains(face);
		if (selected.faces.isEmpty() && selected.surfaces.isEmpty())
		{
			for (int i = 0; i < 3; ++i)
				touches = touches || selected.vertices.contains(indices[i]) ||
					selected.edges.contains(modelEdge(indices[i], indices[(i + 1) % 3]));
		}
		if (!touches) continue;
		if (std::any_of(indices.begin(), indices.end(), [&](int index) { return index < 0 || index >= positions.size(); }))
			return fail(error, Text::tr("The selected face has invalid vertex indices."));
		auto x = difference(positions[triangle.b], positions[triangle.a]);
		auto edge = difference(positions[triangle.c], positions[triangle.a]);
		if (!normalize(&x) || !normalize(&edge)) continue;
		auto z = cross(x, edge);
		if (dot(z, z) < 1e-16 || !normalize(&z)) continue;
		if (!basis(x, cross(z, x), z, &candidate)) continue;
		if (!work.check()) return false;
		*result = candidate;
		return true;
	}
	return fail(error, Text::tr("Selection axes need a nondegenerate selected or touching face. Select another face, or use world or custom axes."));
}
} // namespace vibestudio
