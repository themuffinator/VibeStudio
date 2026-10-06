#include "core/model_surface_selection.h"
#include "core/model_transform_axes.h"

#include <QCoreApplication>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelSurfaceSelection)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
} // namespace
bool validModelSurfaceSelection(const ModelMesh &mesh, const ModelSelection &selection)
{
	return selection.surfaces.isEmpty() ||
		   (selection.surfaces.contains(selection.surface) && selection.vertices.isEmpty() && selection.faces.isEmpty() &&
			selection.edges.isEmpty() && selection.tag.isEmpty() && selection.collision.isEmpty() &&
			std::all_of(selection.surfaces.cbegin(), selection.surfaces.cend(),
						[&](int index) { return index >= 0 && index < mesh.surfaces.size(); }));
}
bool modelSurfaceSelectionPivot(const ModelMesh &mesh, const QSet<int> &surfaces, int frame, ModelTransformPivot mode, ModelVec3 custom,
								ModelVec3 *result, QString *error, const ModelWorkControl &control)
{
	if (error)
		error->clear();
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!work.check())
		return false;
	if (!result || surfaces.isEmpty() || frame < 0 || int(mode) < 0 || int(mode) > 2 || !std::isfinite(custom.x) ||
		!std::isfinite(custom.y) || !std::isfinite(custom.z))
		return fail(error, Text::tr("Select surfaces and a valid reference pose and pivot."));
	std::array<double, 3> low{INFINITY, INFINITY, INFINITY}, high{-INFINITY, -INFINITY, -INFINITY};
	for (int index : surfaces)
	{
		if (index < 0 || index >= mesh.surfaces.size() || frame >= mesh.surfaces[index].frames.size() ||
			mesh.surfaces[index].frames[frame].positions.isEmpty())
			return fail(error, Text::tr("A selected surface or its reference pose no longer exists."));
		for (const auto point : mesh.surfaces[index].frames[frame].positions)
		{
			if (!work.step())
				return false;
			const std::array<double, 3> values{point.x, point.y, point.z};
			for (int axis = 0; axis < 3; ++axis)
			{
				if (!std::isfinite(values[axis]))
					return fail(error, Text::tr("Surface positions must be finite."));
				low[axis] = std::min(low[axis], values[axis]);
				high[axis] = std::max(high[axis], values[axis]);
			}
		}
	}
	if (!work.check())
		return false;
	*result = mode == ModelTransformPivot::Origin ? ModelVec3{}
			  : mode == ModelTransformPivot::Custom
				  ? custom
				  : ModelVec3{float((low[0] + high[0]) / 2), float((low[1] + high[1]) / 2), float((low[2] + high[2]) / 2)};
	return true;
}
bool applyModelSurfaceTransform(ModelMesh *mesh, const ModelEdit &edit, QString *error, const ModelWorkControl &control)
{
	if (!mesh || edit.kind != ModelEditKind::Transform || edit.selection.surfaces.isEmpty() ||
		!validModelSurfaceSelection(*mesh, edit.selection) || edit.frame < -1 || edit.frame >= mesh->frames.size())
		return fail(error, Text::tr("Select whole surfaces without mixing them with mesh components."));
	ModelTransform transform;
	if (!snapModelTranslation(edit.translation, edit.translationGrid, &transform.translation, error) ||
		!snapModelRotation(edit.rotation, edit.rotationGrid, &transform.rotation, error) ||
		!snapModelScale(edit.scale, edit.scaleGrid, &transform.scale, error) ||
		!resolveModelTransformAxes(*mesh, edit, &transform.basis, error, control) ||
		!modelSurfaceSelectionPivot(*mesh, edit.selection.surfaces, edit.frame >= 0 ? edit.frame : edit.pivotFrame, edit.pivotMode,
									edit.pivot, &transform.pivot, error, control))
		return false;
	const bool mirrored = double(transform.scale.x) * transform.scale.y * transform.scale.z < 0;
	if (mirrored && edit.frame >= 0)
		return fail(error, Text::tr("Mirroring selected surfaces requires all frames so winding remains consistent."));
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	for (int index = 0; index < mesh->surfaces.size(); ++index)
	{
		if (!edit.selection.surfaces.contains(index))
			continue;
		auto &surface = mesh->surfaces[index];
		for (int frame = 0; frame < surface.frames.size(); ++frame)
		{
			if (edit.frame >= 0 && frame != edit.frame)
				continue;
			auto &pose = surface.frames[frame];
			for (int vertex = 0; vertex < pose.positions.size(); ++vertex)
			{
				if (!work.step())
					return false;
				pose.positions[vertex] = transformModelPoint(pose.positions[vertex], transform);
				pose.normals[vertex] = transformModelNormal(pose.normals[vertex], transform);
			}
		}
		if (mirrored)
			for (auto &triangle : surface.triangles)
			{
				if (!work.step())
					return false;
				std::swap(triangle.b, triangle.c);
			}
	}
	return work.check();
}
} // namespace vibestudio
