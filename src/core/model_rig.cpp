#include "core/model_rig.h"
#include "core/model_document.h"
#include "core/model_skeleton.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelRig)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
bool near(ModelVec3 a, ModelVec3 b)
{
	return std::abs(double(a.x) - b.x) <= 0.0001 && std::abs(double(a.y) - b.y) <= 0.0001 && std::abs(double(a.z) - b.z) <= 0.0001;
}
bool validName(const ModelSkeleton &skeleton, const QString &name, int except, QString *error)
{
	static const QRegularExpression pattern(QStringLiteral("^[A-Za-z_][A-Za-z0-9_.-]{0,62}$"));
	if (!pattern.match(name).hasMatch())
		return fail(error, Text::tr("Use a joint name of 1–63 ASCII letters, digits, underscores, "
									"dots or hyphens, starting with a letter or underscore."));
	const int found = modelJointIndex(skeleton, name);
	return found < 0 || found == except || fail(error, Text::tr("A joint named %1 already exists.").arg(name));
}
// Reorder every joint-indexed array together, retaining model-space poses.
bool reorder(ModelMesh *mesh, const QVector<int> &order, QString *error, ModelWorkProgress &work)
{
	auto &skeleton = mesh->skeleton;
	QVector<int> remap(skeleton.joints.size(), -1);
	QVector<ModelJoint> joints;
	for (int old : order)
	{
		remap[old] = joints.size();
		joints.append(skeleton.joints[old]);
	}
	for (auto &joint : joints)
		if (joint.parent >= 0)
			joint.parent = remap[joint.parent];
	skeleton.joints = std::move(joints);
	for (auto &clip : skeleton.clips)
		for (auto &pose : clip.frames)
		{
			if (!work.step())
				return false;
			QVector<ModelJointMatrix> matrices;
			matrices.reserve(order.size());
			for (int old : order)
				matrices.append(pose[old]);
			pose = std::move(matrices);
		}
	for (auto &tag : skeleton.tags)
		tag.joint = remap[tag.joint];
	for (auto &surface : mesh->surfaces)
		for (auto &influence : surface.skinning.influences)
		{
			if (!work.step())
				return false;
			influence.joint = remap[influence.joint];
			if (influence.joint < 0)
				return fail(error, Text::tr("Remove the joint's vertex influences before deleting it."));
		}
	return true;
}

bool setWeights(ModelMesh *mesh, const ModelEdit &edit, int joint, QString *error, ModelWorkProgress &work)
{
	if (!std::isfinite(edit.rig.weight) || edit.rig.weight < 0 || edit.rig.weight > 1)
		return fail(error, Text::tr("Joint weight must be between zero and one."));
	const auto &skeleton = mesh->skeleton;
	const int bindFrame = modelBindPoseFrame(*mesh);
	if (bindFrame < 0 || skeleton.bakedClipForFrame.size() != mesh->frames.size())
		return fail(error, Text::tr("Weight editing needs a retained bind pose and "
									"unchanged skeletal frame ranges."));
	auto &surface = mesh->surfaces[edit.selection.surface];
	if (surface.skinning.isEmpty())
		return fail(error, Text::tr("This surface has no skinning. Use Skin Weights and Binding to bind it, "
									"or add the first joint to a static model before editing weights."));
	QSet<int> vertices = edit.selection.vertices;
	for (const auto &edge : edit.selection.edges)
	{
		vertices.insert(edge.first);
		vertices.insert(edge.second);
	}
	for (int face : edit.selection.faces)
	{
		const auto &triangle = surface.triangles[face];
		vertices.insert(triangle.a);
		vertices.insert(triangle.b);
		vertices.insert(triangle.c);
	}
	if (vertices.isEmpty())
		return fail(error, Text::tr("Select vertices, edges or faces before assigning joint weights."));
	// Establish a complete, unambiguous mapping without replacing the frame
	// list, animation ranges, MDL groups, tags or collision tracks.
	QVector<const QVector<ModelJointMatrix> *> poses;
	const auto bind = modelSkeletonBindPose(skeleton);
	QVector<int> counts(skeleton.clips.size(), 0);
	int previous = -1;
	for (int clip : skeleton.bakedClipForFrame)
	{
		if (clip == -1)
			poses.append(&bind);
		else if (clip >= 0 && clip < skeleton.clips.size() && clip >= previous && counts[clip] < skeleton.clips[clip].frames.size())
		{
			poses.append(&skeleton.clips[clip].frames[counts[clip]++]);
			previous = clip;
		}
		else
			return fail(error, Text::tr("Weight editing cannot replace independently "
										"added or rearranged animation frames."));
	}
	for (int clip = 0; clip < counts.size(); ++clip)
		if (counts[clip] != 0 && counts[clip] != skeleton.clips[clip].frames.size())
			return fail(error, Text::tr("Weight editing needs complete skeletal clip ranges."));
	// Geometric normals can depend on neighbouring vertices. Validate the whole
	// affected surface before refreshing it, so no independent edits are lost.
	for (int frame = 0; frame < poses.size(); ++frame)
	{
		if (!work.check())
			return false;
		ModelFrameGeometry expected;
		if (!skinModelSurface(surface, *poses[frame], &expected, error))
			return false;
		for (int vertex = 0; vertex < surface.vertexCount; ++vertex)
			if (!near(expected.positions[vertex], surface.frames[frame].positions[vertex]) ||
				!near(expected.normals[vertex], surface.frames[frame].normals[vertex]))
				return fail(error, Text::tr("This surface has independent frame or "
											"normal edits. Weight editing would replace them; "
											"use an unmodified skeletal source."));
	}
	const auto &geometry = surface.frames[bindFrame];
	ModelSurfaceSkinning skinning;
	for (int vertex = 0; vertex < surface.vertexCount; ++vertex)
	{
		if (!work.step())
			return false;
		skinning.first.append(skinning.influences.size());
		const int first = surface.skinning.first[vertex], count = surface.skinning.count[vertex];
		if (!vertices.contains(vertex))
		{
			for (int i = first; i < first + count; ++i)
				skinning.influences.append(surface.skinning.influences[i]);
		}
		else
		{
			double remaining = 0;
			bool hasNormals = false;
			for (int i = first; i < first + count; ++i)
			{
				const auto &influence = surface.skinning.influences[i];
				hasNormals = hasNormals || influence.normalOffset.x != 0 || influence.normalOffset.y != 0 || influence.normalOffset.z != 0;
				if (influence.weight < 0)
					return fail(error, Text::tr("Negative imported weights must be "
												"repaired before assigning weights."));
				if (influence.joint != joint)
					remaining += influence.weight;
			}
			if (edit.rig.weight < 1 && remaining <= 0)
				return fail(error, Text::tr("This vertex has no other joint to carry the remaining weight. "
											"Assign weight one or add another influence first."));
			// Rebind each selected influence to the same bind geometry. Preserve
			// unusual per-influence offsets only when they already agree with it;
			// changing those offsets implicitly would alter imported animation.
			for (int i = first; i < first + count; ++i)
			{
				auto influence = surface.skinning.influences[i];
				if (influence.weight > 0 &&
					!near(modelJointTransformPoint(bind[influence.joint], influence.offset), geometry.positions[vertex]))
					return fail(error, Text::tr("This imported vertex uses distinct joint-space bind "
												"offsets; weight editing would change its bind shape."));
				if (influence.joint != joint && influence.weight > 0 && edit.rig.weight < 1)
				{
					influence.weight = float(influence.weight / remaining * (1 - edit.rig.weight));
					skinning.influences.append(influence);
				}
			}
			if (edit.rig.weight > 0)
			{
				bool invertible = false;
				const auto inverse = modelJointInverse(bind[joint], &invertible);
				if (!invertible)
					return fail(error, Text::tr("The selected joint has a singular bind transform."));
				skinning.influences.append({joint, float(edit.rig.weight), modelJointTransformPoint(inverse, geometry.positions[vertex]),
											hasNormals ? modelJointTransformVector(inverse, geometry.normals[vertex]) : ModelVec3{}});
			}
		}
		skinning.count.append(skinning.influences.size() - skinning.first.last());
		if (skinning.count.last() > 64)
			return fail(error, Text::tr("A vertex cannot have more than 64 "
										"influences in an editable source."));
	}
	surface.skinning = std::move(skinning);
	ModelFrameGeometry newBind;
	if (!skinModelSurface(surface, bind, &newBind, error))
		return false;
	for (int vertex : vertices)
		if (!near(newBind.positions[vertex], geometry.positions[vertex]) || !near(newBind.normals[vertex], geometry.normals[vertex]))
			return fail(error, Text::tr("The requested weight would change the bind geometry or normals. This imported binding is unsupported for weight editing."));
	for (int frame = 0; frame < poses.size(); ++frame)
	{
		if (!work.check())
			return false;
		ModelFrameGeometry updated;
		if (!skinModelSurface(surface, *poses[frame], &updated, error))
			return false;
		surface.frames[frame] = std::move(updated);
	}
	for (auto &clip : mesh->skeleton.clips)
	{
		clip.frameMins.clear();
		clip.frameMaxs.clear();
	}
	return true;
}
} // namespace

bool isModelRigEdit(ModelEditKind kind)
{
	return kind == ModelEditKind::AddJoint || kind == ModelEditKind::RenameJoint || kind == ModelEditKind::ReparentJoint ||
		   kind == ModelEditKind::DeleteJoint || kind == ModelEditKind::SetJointWeight;
}

bool applyModelRigEdit(ModelMesh *mesh, const ModelEdit &edit, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!mesh || !isModelRigEdit(edit.kind) || !work.check())
		return false;
	if (!validateModelSkeleton(*mesh, error))
		return false;
	auto &skeleton = mesh->skeleton;
	const auto &options = edit.rig;
	const bool add = edit.kind == ModelEditKind::AddJoint, rename = edit.kind == ModelEditKind::RenameJoint;
	const bool parentEdit = add || edit.kind == ModelEditKind::ReparentJoint;
	if ((!add && (options.origin.x != 0 || options.origin.y != 0 || options.origin.z != 0)) || (add && !options.joint.isEmpty()) ||
		(!add && !rename && !options.name.isEmpty()) || (!parentEdit && !options.parent.isEmpty()) ||
		(edit.kind != ModelEditKind::SetJointWeight && options.weight != 1))
		return fail(error, Text::tr("The supplied joint options do not apply to this operation."));
	const int joint = modelJointIndex(skeleton, options.joint, Qt::CaseSensitive);
	if (!add && joint < 0)
		return fail(error, Text::tr("Choose an existing joint by its exact name."));
	const int parent = options.parent.isEmpty() ? -1 : modelJointIndex(skeleton, options.parent, Qt::CaseSensitive);
	if (parentEdit && !options.parent.isEmpty() && parent < 0)
		return fail(error, Text::tr("The requested parent joint does not exist."));
	if ((add || rename) && !validName(skeleton, options.name, add ? -1 : joint, error))
		return false;
	if (edit.kind == ModelEditKind::SetJointWeight)
		return setWeights(mesh, edit, joint, error, work);
	if (rename)
	{
		for (auto &clip : skeleton.clips)
			for (auto &group : clip.curves)
				if (group.joint == skeleton.joints[joint].name) group.joint = options.name;
		skeleton.joints[joint].name = options.name;
		return true;
	}
	if (add)
	{
		if (skeleton.joints.size() >= 4096)
			return fail(error, Text::tr("An editable skeleton cannot contain more than 4,096 joints."));
		for (float value : {options.origin.x, options.origin.y, options.origin.z})
			if (!std::isfinite(value) || std::abs(double(value)) > 1000000)
				return fail(error, Text::tr("Joint origins must be finite and within "
											"one million model units."));
		if (skeleton.isEmpty() && mesh->frames.size() != 1)
			return fail(error, Text::tr("Create a skeleton from a single static frame. Existing "
										"vertex animation cannot be converted implicitly."));
		const auto bind = modelJointMatrix({}, options.origin);
		bool invertible = true;
		const auto local = parent < 0 ? bind : modelJointMultiply(modelJointInverse(skeleton.joints[parent].bind, &invertible), bind);
		if (!invertible)
			return fail(error, Text::tr("The parent joint has a singular bind transform."));
		skeleton.joints.append({options.name, parent, bind});
		for (auto &clip : skeleton.clips)
			for (auto &pose : clip.frames)
			{
				if (!work.step())
					return false;
				pose.append(parent < 0 ? bind : modelJointMultiply(pose[parent], local));
			}
		if (skeleton.joints.size() == 1)
		{
			skeleton.sourceFormat = QStringLiteral("authored");
			skeleton.bakedClipForFrame = {-1};
			for (auto &surface : mesh->surfaces)
			{
				if (!work.check())
					return false;
				if (!modelSkinningFromBindPose(skeleton, surface.frames[0].positions, surface.frames[0].normals,
											   QVector<int>(surface.vertexCount, 0), QVector<float>(surface.vertexCount, 1), 1,
											   &surface.skinning, error))
					return false;
			}
		}
		return true;
	}
	if (edit.kind == ModelEditKind::ReparentJoint)
	{
		for (int ancestor = parent; ancestor >= 0; ancestor = skeleton.joints[ancestor].parent)
			if (ancestor == joint)
				return fail(error, Text::tr("A joint cannot be parented to itself or "
											"one of its descendants."));
		skeleton.joints[joint].parent = parent;
	}
	else
	{
		if (skeleton.joints.size() == 1)
			return fail(error, Text::tr("Keep at least one joint in a skeletal model."));
		for (const auto &item : skeleton.joints)
			if (item.parent == joint)
				return fail(error, Text::tr("Reparent this joint's children before deleting it."));
		for (const auto &tag : skeleton.tags)
			if (tag.joint == joint)
				return fail(error, Text::tr("This joint drives an attachment and cannot be deleted."));
		for (const auto &surface : mesh->surfaces)
			for (const auto &influence : surface.skinning.influences)
			{
				if (!work.step())
					return false;
				if (influence.joint == joint)
					return fail(error, Text::tr("Remove the joint's vertex influences before deleting it."));
			}
	}
	QVector<int> order;
	QSet<int> included;
	const int target = skeleton.joints.size() - (edit.kind == ModelEditKind::DeleteJoint ? 1 : 0);
	while (order.size() < target)
	{
		const int before = order.size();
		for (int i = 0; i < skeleton.joints.size(); ++i)
		{
			if (!work.step())
				return false;
			if (included.contains(i) || (edit.kind == ModelEditKind::DeleteJoint && i == joint))
				continue;
			if (skeleton.joints[i].parent < 0 || included.contains(skeleton.joints[i].parent))
			{
				included.insert(i);
				order.append(i);
			}
		}
		if (order.size() == before)
			return fail(error, Text::tr("The joint hierarchy contains a cycle."));
	}
	return reorder(mesh, order, error, work);
}
} // namespace vibestudio
