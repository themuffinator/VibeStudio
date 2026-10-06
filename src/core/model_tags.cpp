#include "core/model_tags.h"
#include "core/model_document.h"
#include "core/model_transform_axes.h"

#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <iterator>

namespace vibestudio
{
namespace
{
bool finite(ModelVec3 p)
{
	return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::abs(p.x) <= 1000000 && std::abs(p.y) <= 1000000 &&
		   std::abs(p.z) <= 1000000;
}
bool nativeName(const QString &name)
{
	return !name.isEmpty() && name.size() <= 63 && name == name.trimmed() &&
		   std::all_of(name.cbegin(), name.cend(), [](QChar c) { return c.unicode() >= 32 && c.unicode() <= 126; });
}
void rotateAxes(ModelTag *tag, ModelVec3 rotation)
{
	for (int row = 0; row < 3; ++row)
	{
		const auto p = rotateModelVector({tag->axis[row * 3], tag->axis[row * 3 + 1], tag->axis[row * 3 + 2]}, rotation);
		tag->axis[row * 3] = p.x;
		tag->axis[row * 3 + 1] = p.y;
		tag->axis[row * 3 + 2] = p.z;
	}
}
} // namespace
QStringList modelTagNames(const ModelMesh &mesh)
{
	QStringList names;
	for (const auto &tag : mesh.tags)
	{
		if (tag.frameIndex == 0)
		{
			names << tag.name;
		}
	}
	return names;
}
const ModelTag *findModelTag(const ModelMesh &mesh, const QString &name, int frame)
{
	const auto found = std::find_if(mesh.tags.cbegin(), mesh.tags.cend(),
									[&](const ModelTag &tag) { return tag.name == name && tag.frameIndex == frame; });
	return found == mesh.tags.cend() ? nullptr : &*found;
}
bool validModelTagSelection(const ModelMesh &mesh, const ModelSelection &selection)
{
	return selection.tag.isEmpty() ||
		   (selection.surfaces.isEmpty() && selection.collision.isEmpty() && selection.vertices.isEmpty() && selection.faces.isEmpty() && selection.edges.isEmpty() && findModelTag(mesh, selection.tag, 0));
}
bool isModelTagEdit(ModelEditKind kind)
{
	switch (kind)
	{
	case ModelEditKind::AddTag:
	case ModelEditKind::DuplicateTag:
	case ModelEditKind::RenameTag:
	case ModelEditKind::DeleteTag:
	case ModelEditKind::SetTagOrigin:
	case ModelEditKind::ResetTagOrientation:
	case ModelEditKind::TransformTag:
	case ModelEditKind::CopyTagPose:
		return true;
	default:
		return false;
	}
}
ModelVec3 modelTagPoint(const ModelTag &tag, ModelVec3 local)
{
	// MD3 attachment convention: id Software Quake III Arena, cg_ents.c,
	// CG_PositionEntityOnTag (GPL-2.0-or-later), master reviewed 2026-10-04.
	// https://github.com/id-Software/Quake-III-Arena/blob/master/code/cgame/cg_ents.c
	return {tag.origin.x + local.x * tag.axis[0] + local.y * tag.axis[3] + local.z * tag.axis[6],
			tag.origin.y + local.x * tag.axis[1] + local.y * tag.axis[4] + local.z * tag.axis[7],
			tag.origin.z + local.x * tag.axis[2] + local.y * tag.axis[5] + local.z * tag.axis[8]};
}
ModelTag transformedModelTag(const ModelTag &tag, const ModelTransform &transform)
{
	auto result = tag;
	result.origin = transformModelPoint(tag.origin, transform);
	for (int row = 0; row < 3; ++row)
	{
		const auto axis = rotateModelTransformVector({tag.axis[row * 3], tag.axis[row * 3 + 1], tag.axis[row * 3 + 2]}, transform);
		result.axis[row * 3] = axis.x;
		result.axis[row * 3 + 1] = axis.y;
		result.axis[row * 3 + 2] = axis.z;
	}
	return result;
}
bool applyModelTagEdit(ModelMesh *candidate, const ModelEdit &edit, ModelSelection *selection, QString *error,
					   const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	const auto fail = [&](const QString &message)
	{
		if (error)
		{
			*error = message;
		}
		return false;
	};
	const auto kind = edit.kind;
	const bool identity = kind == ModelEditKind::AddTag || kind == ModelEditKind::DuplicateTag || kind == ModelEditKind::RenameTag ||
						  kind == ModelEditKind::DeleteTag;
	if (!work.check())
	{
		return false;
	}
	if (!candidate || !selection || !isModelTagEdit(kind) || edit.frame < -1 || edit.frame >= candidate->frames.size() ||
		(identity && edit.frame != -1))
	{
		return fail(QCoreApplication::translate(
			"VibeStudioModelTags",
			"Choose a valid tag operation and pose. Adding, duplicating, renaming, or deleting tags applies to every frame."));
	}
	if (edit.scale.x != 1 || edit.scale.y != 1 || edit.scale.z != 1 || edit.scaleGrid != 0)
	{
		return fail(QCoreApplication::translate("VibeStudioModelTags", "Attachment tags support rigid movement and rotation, not scale."));
	}
	const auto existing = findModelTag(*candidate, edit.selection.tag, 0);
	if (kind != ModelEditKind::AddTag && !existing)
	{
		return fail(QCoreApplication::translate("VibeStudioModelTags", "Select an existing attachment tag."));
	}
	const bool named = kind == ModelEditKind::AddTag || kind == ModelEditKind::DuplicateTag || kind == ModelEditKind::RenameTag;
	if (named && (!nativeName(edit.text) ||
				  ((edit.text != edit.selection.tag || kind != ModelEditKind::RenameTag) && findModelTag(*candidate, edit.text, 0))))
	{
		return fail(QCoreApplication::translate("VibeStudioModelTags",
												"Use a unique tag name with 1–63 printable ASCII characters and no surrounding spaces."));
	}
	if ((kind == ModelEditKind::AddTag || kind == ModelEditKind::DuplicateTag) && modelTagNames(*candidate).size() >= 16)
	{
		return fail(QCoreApplication::translate("VibeStudioModelTags", "A model supports at most 16 attachment tags per frame."));
	}
	if ((kind == ModelEditKind::AddTag || kind == ModelEditKind::SetTagOrigin) && !finite(edit.tagOrigin))
	{
		return fail(QCoreApplication::translate("VibeStudioModelTags", "Tag origins must be finite and within ±1,000,000 model units."));
	}
	ModelTransform transform;
	if (kind == ModelEditKind::TransformTag || kind == ModelEditKind::AddTag)
	{
		if (!snapModelRotation(edit.rotation, edit.rotationGrid, &transform.rotation, error))
		{
			return false;
		}
	}
	if (kind == ModelEditKind::TransformTag)
	{
		const int reference = edit.frame >= 0 ? edit.frame : edit.pivotFrame;
		const auto tag = findModelTag(*candidate, edit.selection.tag, reference);
		if (!tag || !modelTransformPivot({tag->origin}, {0}, edit.pivotMode, edit.pivot, &transform.pivot))
		{
			return fail(QCoreApplication::translate("VibeStudioModelTags", "Choose a valid tag pivot and reference pose."));
		}
		if (!snapModelTranslation(edit.translation, edit.translationGrid, &transform.translation, error) ||
			!resolveModelTransformAxes(*candidate, edit, &transform.basis, error, control))
		{
			return false;
		}
	}
	ModelTag pose;
	if (kind == ModelEditKind::CopyTagPose)
	{
		const auto source = findModelTag(*candidate, edit.selection.tag, edit.sourceFrame);
		if (!source)
		{
			return fail(QCoreApplication::translate("VibeStudioModelTags", "Choose an existing source pose for the selected tag."));
		}
		pose = *source;
	}
	if (kind == ModelEditKind::AddTag)
	{
		ModelTag tag;
		tag.name = edit.text;
		tag.origin = edit.tagOrigin;
		rotateAxes(&tag, transform.rotation);
		for (int frame = 0; frame < candidate->frames.size(); ++frame)
		{
			if (!work.step())
			{
				return false;
			}
			tag.frameIndex = frame;
			candidate->tags << tag;
		}
	}
	else
	{
		QVector<ModelTag> duplicates;
		for (auto &tag : candidate->tags)
		{
			if (!work.step())
			{
				return false;
			}
			if (tag.name != edit.selection.tag || (edit.frame >= 0 && edit.frame != tag.frameIndex))
			{
				continue;
			}
			switch (kind)
			{
			case ModelEditKind::DuplicateTag:
				duplicates << tag;
				duplicates.last().name = edit.text;
				break;
			case ModelEditKind::RenameTag:
				tag.name = edit.text;
				break;
			case ModelEditKind::SetTagOrigin:
				tag.origin = edit.tagOrigin;
				break;
			case ModelEditKind::ResetTagOrientation:
				std::copy(std::begin(pose.axis), std::end(pose.axis), std::begin(tag.axis));
				break;
			case ModelEditKind::TransformTag:
				tag = transformedModelTag(tag, transform);
				break;
			case ModelEditKind::CopyTagPose:
				tag.origin = pose.origin;
				std::copy(std::begin(pose.axis), std::end(pose.axis), std::begin(tag.axis));
				break;
			default:
				break;
			}
		}
		candidate->tags += duplicates;
		if (kind == ModelEditKind::DeleteTag)
		{
			candidate->tags.removeIf([&](const ModelTag &tag) { return tag.name == edit.selection.tag; });
		}
	}
	*selection = {edit.selection.surface,
				  {},
				  {},
				  {},
				  kind == ModelEditKind::DeleteTag ? QString{}
				  : named						   ? edit.text
												   : edit.selection.tag};
	return work.check();
}
} // namespace vibestudio
