#include "core/model_joint_tools.h"
#include "core/model_document.h"
#include "core/model_skeleton.h"

#include <QCoreApplication>
#include <QHash>
#include <algorithm>
#include <cmath>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelJointTools)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
bool same(ModelVec3 a, ModelVec3 b, double tolerance = .0001)
{
	return std::abs(double(a.x) - b.x) <= tolerance && std::abs(double(a.y) - b.y) <= tolerance && std::abs(double(a.z) - b.z) <= tolerance;
}
bool same(const ModelFrameGeometry &a, const ModelFrameGeometry &b, double tolerance = .0001)
{
	if (a.positions.size() != b.positions.size() || a.normals.size() != b.normals.size())
		return false;
	for (int i = 0; i < a.positions.size(); ++i)
		if (!same(a.positions[i], b.positions[i], tolerance) || !same(a.normals[i], b.normals[i], tolerance))
			return false;
	return true;
}
ModelTag posedTag(const ModelSkeletalTag &source, const QVector<ModelJointMatrix> &pose, int frame)
{
	const auto matrix = modelJointMultiply(pose[source.joint], source.offset);
	ModelTag tag;
	tag.name = source.name;
	tag.frameIndex = frame;
	tag.origin = modelJointTranslation(matrix);
	for (int axis = 0; axis < 3; ++axis)
	{
		const double length = std::sqrt(double(matrix.m[axis]) * matrix.m[axis] + double(matrix.m[4 + axis]) * matrix.m[4 + axis] +
										double(matrix.m[8 + axis]) * matrix.m[8 + axis]);
		for (int row = 0; row < 3; ++row)
			tag.axis[axis * 3 + row] = length > 0 ? float(matrix.m[row * 4 + axis] / length) : 0;
	}
	return tag;
}
bool same(const ModelTag &a, const ModelTag &b, double tolerance = .0001)
{
	if (!same(a.origin, b.origin, tolerance))
		return false;
	for (int i = 0; i < 9; ++i)
		if (std::abs(double(a.axis[i]) - b.axis[i]) > tolerance)
			return false;
	return true;
}
} // namespace

bool modelJointFrame(const ModelMesh &mesh, int frame, ModelJointFrame *result, QString *error)
{
	if (!result || mesh.skeleton.isEmpty() || frame < -1 || frame >= mesh.frames.size())
		return fail(error, Text::tr("Choose a model with joints and an existing skeletal frame."));
	ModelJointFrame candidate;
	if (frame < 0)
		candidate.pose = modelSkeletonBindPose(mesh.skeleton);
	else
	{
		if (mesh.skeleton.bakedClipForFrame.size() != mesh.frames.size())
			return fail(error, Text::tr("The current frame has no retained skeletal pose."));
		QVector<int> counts(mesh.skeleton.clips.size(), 0);
		int previous = -1;
		for (int i = 0; i < mesh.frames.size(); ++i)
		{
			const int clip = mesh.skeleton.bakedClipForFrame[i];
			if (clip < -1 || clip >= counts.size() || (clip == -1 && i != 0) || clip < previous)
				return fail(error,
							Text::tr("Joint authoring needs complete retained clip ranges without independent or rearranged frames."));
			if (clip >= 0)
				++counts[clip];
			previous = clip;
		}
		for (int i = 0; i < counts.size(); ++i)
			if (counts[i] != 0 && counts[i] != mesh.skeleton.clips[i].frames.size())
				return fail(error,
							Text::tr("Joint authoring needs complete retained clip ranges without independent or rearranged frames."));
		candidate.clip = mesh.skeleton.bakedClipForFrame[frame];
		if (candidate.clip == -1)
			candidate.pose = modelSkeletonBindPose(mesh.skeleton);
		else
		{
			if (candidate.clip < 0 || candidate.clip >= mesh.skeleton.clips.size())
				return fail(error,
							Text::tr("The current frame has independent geometry. Select a retained skeletal pose before editing joints."));
			const int first = mesh.skeleton.bakedClipForFrame.indexOf(candidate.clip);
			candidate.local = frame - first;
			for (int i = first; i <= frame; ++i)
				if (mesh.skeleton.bakedClipForFrame[i] != candidate.clip)
					return fail(error, Text::tr("The skeletal frame range was rearranged and cannot identify this pose."));
			const auto &clip = mesh.skeleton.clips[candidate.clip];
			if (candidate.local >= clip.frames.size())
				return fail(error, Text::tr("The current frame is outside its retained skeletal clip."));
			candidate.pose = clip.frames[candidate.local];
		}
	}
	if (candidate.pose.size() != mesh.skeleton.joints.size() ||
		!std::all_of(candidate.pose.cbegin(), candidate.pose.cend(), modelJointMatrixIsFinite))
		return fail(error, Text::tr("The skeletal pose has missing or invalid joint matrices."));
	*result = std::move(candidate);
	return true;
}
bool validModelJointSelection(const ModelMesh &mesh, const ModelSelection &selection)
{
	return selection.joint.isEmpty() || (selection.vertices.isEmpty() && selection.faces.isEmpty() && selection.edges.isEmpty() &&
										 selection.surfaces.isEmpty() && selection.tag.isEmpty() && selection.collision.isEmpty() &&
										 modelJointIndex(mesh.skeleton, selection.joint, Qt::CaseSensitive) >= 0);
}
bool validModelJointDelta(const ModelJointMatrix &delta, QString *error)
{
	for (float value : delta.m)
		if (!std::isfinite(value) || std::abs(double(value)) > 1000000)
			return fail(error, Text::tr("A joint delta requires finite matrix values within one million."));
	const auto &m = delta.m;
	const double determinant = double(m[0]) * (double(m[5]) * m[10] - double(m[6]) * m[9]) -
							   double(m[1]) * (double(m[4]) * m[10] - double(m[6]) * m[8]) +
							   double(m[2]) * (double(m[4]) * m[9] - double(m[5]) * m[8]);
	bool invertible = false;
	const auto inverse = modelJointInverse(delta, &invertible);
	if (determinant <= 0 || !invertible || !modelJointMatrixIsFinite(inverse))
		return fail(error, Text::tr("A joint delta must preserve orientation and remain invertible."));
	return true;
}
bool modelJointTransformDelta(const ModelTransform &transform, ModelJointMatrix *result, QString *error)
{
	ModelVec3 checked;
	if (!result || !validModelTransformBasis(transform.basis) || !snapModelTranslation(transform.translation, 0, &checked) ||
		!snapModelTranslation(transform.pivot, 0, &checked) || !snapModelRotation(transform.rotation, 0, &checked) ||
		!snapModelScale(transform.scale, 0, &checked) || transform.scale.x <= 0 || transform.scale.y <= 0 || transform.scale.z <= 0)
		return fail(error, Text::tr("Choose finite joint transforms with positive scale and valid axes."));
	// Build the linear columns without subtracting a large pivot/translation;
	// subtraction would erase small scales through float cancellation.
	auto linear = transform;
	linear.translation = linear.pivot = {};
	const ModelVec3 axes[]{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
	ModelJointMatrix candidate;
	for (int column = 0; column < 3; ++column)
	{
		const auto value = transformModelPoint(axes[column], linear);
		candidate.m[column] = value.x;
		candidate.m[4 + column] = value.y;
		candidate.m[8 + column] = value.z;
	}
	const auto origin = transformModelPoint({}, transform);
	candidate.m[3] = origin.x;
	candidate.m[7] = origin.y;
	candidate.m[11] = origin.z;
	if (!validModelJointDelta(candidate, error))
		return false;
	*result = candidate;
	return true;
}
bool applyModelJointDelta(QVector<ModelJointMatrix> *pose, const ModelSkeleton &skeleton, int joint, const ModelJointMatrix &delta,
						  bool descendants, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!work.check())
		return false;
	if (!pose || pose->size() != skeleton.joints.size() || joint < 0 || joint >= pose->size())
		return fail(error, Text::tr("Choose an existing joint and complete skeletal pose."));
	if (!validModelJointDelta(delta, error))
		return false;
	QVector<bool> affected(pose->size(), false);
	for (int i = 0; i < affected.size(); ++i)
	{
		if (!work.step())
			return false;
		const int parent = skeleton.joints[i].parent;
		if (parent < -1 || parent >= i)
			return fail(error, Text::tr("Joint parents must precede their children."));
		affected[i] = i == joint || (descendants && parent >= 0 && affected[parent]);
	}
	auto candidate = *pose;
	for (int i = 0; i < candidate.size(); ++i)
	{
		if (!work.step())
			return false;
		if (affected[i])
			candidate[i] = modelJointMultiply(delta, candidate[i]);
		if (!modelJointMatrixIsFinite(candidate[i]))
			return fail(error, Text::tr("The joint transform produced an invalid pose."));
	}
	if (!work.check())
		return false;
	*pose = std::move(candidate);
	return true;
}
bool previewModelJointTransform(const ModelMesh &mesh, int frame, const QString &joint, const ModelJointMatrix &delta, bool descendants,
								ModelJointPreview *result, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!work.check())
		return false;
	if (!result || frame < 0 || frame >= mesh.frames.size())
		return fail(error, Text::tr("Choose an existing preview frame."));
	ModelJointFrame source;
	if (!validateModelSkeleton(mesh, error) || !modelJointFrame(mesh, frame, &source, error))
		return false;
	ModelJointPreview candidate;
	candidate.pose = source.pose;
	if (!applyModelJointDelta(&candidate.pose, mesh.skeleton, modelJointIndex(mesh.skeleton, joint, Qt::CaseSensitive), delta, descendants,
							  error, control))
		return false;
	for (const auto &surface : mesh.surfaces)
	{
		if (!work.check())
			return false;
		if (frame >= surface.frames.size())
			return fail(error, Text::tr("A surface is missing the preview frame."));
		const auto &original = surface.frames[frame];
		if (source.clip < 0 || surface.skinning.isEmpty())
		{
			candidate.surfaces.append(original);
			continue;
		}
		ModelFrameGeometry expected, updated;
		if (!skinModelSurface(surface, source.pose, &expected, error) || !skinModelSurface(surface, candidate.pose, &updated, error))
			return false;
		if (same(expected, updated, 0))
			candidate.surfaces.append(original);
		else
		{
			if (!same(expected, original))
				return fail(
					error,
					Text::tr("Surface %1 has independent edits in this pose. Joint manipulation would replace them.").arg(surface.name));
			candidate.surfaces.append(std::move(updated));
		}
	}
	QHash<QString, int> skeletalTags;
	for (int i = 0; i < mesh.skeleton.tags.size(); ++i)
		skeletalTags.insert(mesh.skeleton.tags[i].name, i);
	for (const auto &tag : mesh.tags)
	{
		if (!work.step())
			return false;
		if (tag.frameIndex != frame)
			continue;
		auto updated = tag;
		const int index = skeletalTags.value(tag.name, -1);
		if (source.clip >= 0 && index >= 0)
		{
			const auto expected = posedTag(mesh.skeleton.tags[index], source.pose, frame);
			const auto posed = posedTag(mesh.skeleton.tags[index], candidate.pose, frame);
			if (!same(expected, posed, 0))
			{
				if (!same(expected, tag))
					return fail(error,
								Text::tr("Attachment %1 has an independent pose edit. Joint manipulation would replace it.").arg(tag.name));
				updated = posed;
			}
		}
		candidate.tags.append(updated);
	}
	// Validate just the transient frame through the normal editable geometry
	// contract. In particular, affine skinning may be valid while a rigid tag
	// would become sheared, or a transformed point exceeds the source bounds.
	ModelMesh validation = mesh;
	validation.skeleton = {};
	validation.mdl = {};
	validation.animations.clear();
	validation.collisionBoxes.clear();
	validation.frames = {mesh.frames[frame]};
	validation.tags = candidate.tags;
	for (auto &tag : validation.tags)
		tag.frameIndex = 0;
	for (int i = 0; i < validation.surfaces.size(); ++i)
	{
		validation.surfaces[i].frames = {candidate.surfaces[i]};
		validation.surfaces[i].skinning = {};
	}
	updateEditableModelMetadata(&validation);
	const auto problems = validateEditableModel(validation, control);
	if (!problems.isEmpty())
		return fail(error, problems.join(QLatin1Char('\n')));
	if (!work.check())
		return false;
	*result = std::move(candidate);
	return true;
}
} // namespace vibestudio
