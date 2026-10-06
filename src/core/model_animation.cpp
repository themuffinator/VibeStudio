#include "core/model_animation.h"
#include "core/model_mdl.h"

#include "core/model_document.h"
#include "core/model_collision.h"
#include "core/model_pose.h"
#include "core/model_tags.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

namespace vibestudio
{
namespace
{
bool fail(QString *error, const char *message)
{
	if (error)
	{
		*error = QCoreApplication::translate("VibeStudioModelAnimation", message);
	}
	return false;
}
bool nameOk(const QString &name, int maximum = 128)
{
	return !name.isEmpty() && name == name.trimmed() && name.size() <= maximum &&
		   std::none_of(name.begin(), name.end(), [](QChar c) { return c.isNull() || c.category() == QChar::Other_Control; });
}
struct TagEndpoints
{
	ModelTag first, second;
};
} // namespace

bool isModelAnimationEdit(ModelEditKind kind)
{
	return kind == ModelEditKind::AddAnimation || kind == ModelEditKind::RenameAnimation || kind == ModelEditKind::SetAnimationRange ||
		   kind == ModelEditKind::DeleteAnimation || kind == ModelEditKind::InsertInbetweens || kind == ModelEditKind::CopyFramePose ||
		   kind == ModelEditKind::SetAnimationRate;
}

bool applyModelAnimationEdit(ModelMesh *candidate, const ModelEdit &edit, QString *error, const ModelWorkControl &control)
{
	if (error)
	{
		error->clear();
	}
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!work.check())
	{
		return false;
	}
	if (!candidate || !isModelAnimationEdit(edit.kind))
	{
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioModelAnimation", "Choose a valid animation operation."));
	}
	const bool pose = edit.kind == ModelEditKind::InsertInbetweens || edit.kind == ModelEditKind::CopyFramePose;
	if (pose)
	{
		if (edit.frame < 0 || edit.frame >= candidate->frames.size())
		{
			return fail(error, QT_TRANSLATE_NOOP("VibeStudioModelAnimation", "Choose an existing destination frame."));
		}
	}
	else
	{
		if (edit.frame != -1 ||
			(edit.kind != ModelEditKind::AddAnimation && (edit.animationIndex < 0 || edit.animationIndex >= candidate->animations.size())))
		{
			return fail(error, QT_TRANSLATE_NOOP("VibeStudioModelAnimation",
												 "Select an existing animation clip. Clip metadata uses all-frame scope."));
		}
		if (edit.kind == ModelEditKind::AddAnimation || edit.kind == ModelEditKind::RenameAnimation)
		{
			if (!nameOk(edit.text))
			{
				return fail(error, QT_TRANSLATE_NOOP(
									   "VibeStudioModelAnimation",
									   "Use a unique clip name of 1–128 characters without surrounding spaces or control characters."));
			}
			for (int i = 0; i < candidate->animations.size(); ++i)
			{
				if (!work.step())
				{
					return false;
				}
				if (candidate->animations[i].name == edit.text && (edit.kind == ModelEditKind::AddAnimation || i != edit.animationIndex))
				{
					return fail(error, QT_TRANSLATE_NOOP("VibeStudioModelAnimation", "That animation clip name is already used."));
				}
			}
		}
		if (edit.kind == ModelEditKind::AddAnimation || edit.kind == ModelEditKind::SetAnimationRange)
		{
			if (edit.rangeFirst < 0 || edit.rangeLast < edit.rangeFirst || edit.rangeLast >= candidate->frames.size())
			{
				return fail(error, QT_TRANSLATE_NOOP("VibeStudioModelAnimation",
													 "Clip bounds must be existing frames, with the first frame no later than the last."));
			}
		}
		if ((edit.kind == ModelEditKind::AddAnimation || edit.kind == ModelEditKind::SetAnimationRate) &&
			(!std::isfinite(edit.animationRate) || edit.animationRate < 0 || edit.animationRate > 1000 ||
			 (edit.animationRate > 0 && edit.animationRate < .001)))
		{
			return fail(error, QT_TRANSLATE_NOOP("VibeStudioModelAnimation", "Clip FPS must be zero (unspecified) or from 0.001 to 1000."));
		}
		switch (edit.kind)
		{
		case ModelEditKind::AddAnimation:
			if (candidate->animations.size() >= modelDocumentMaxAnimations)
			{
				return fail(error, QT_TRANSLATE_NOOP("VibeStudioModelAnimation", "A model supports at most 1024 animation clips."));
			}
			candidate->animations << ModelAnimation{edit.text, edit.rangeFirst, edit.rangeLast - edit.rangeFirst + 1, edit.animationRate};
			break;
		case ModelEditKind::SetAnimationRate:
			candidate->animations[edit.animationIndex].framesPerSecond = edit.animationRate;
			break;
		case ModelEditKind::RenameAnimation:
			candidate->animations[edit.animationIndex].name = edit.text;
			break;
		case ModelEditKind::SetAnimationRange:
			candidate->animations[edit.animationIndex].firstFrame = edit.rangeFirst;
			candidate->animations[edit.animationIndex].frameCount = edit.rangeLast - edit.rangeFirst + 1;
			break;
		case ModelEditKind::DeleteAnimation:
			candidate->animations.removeAt(edit.animationIndex);
			break;
		default:
			break;
		}
		return work.check();
	}
	const auto source = *candidate;
	if (edit.kind == ModelEditKind::CopyFramePose)
	{
		if (edit.sourceFrame < 0 || edit.sourceFrame >= source.frames.size() || edit.sourceFrame == edit.frame)
		{
			return fail(error, QT_TRANSLATE_NOOP("VibeStudioModelAnimation", "Copy a pose from a different existing source frame."));
		}
		for (auto &surface : candidate->surfaces)
		{
			if (!work.step())
			{
				return false;
			}
			surface.frames[edit.frame] = surface.frames[edit.sourceFrame];
		}
		candidate->frames[edit.frame].origin = source.frames[edit.sourceFrame].origin;
		for (auto &box : candidate->collisionBoxes)
		{
			if (!work.step())
				return false;
			if (box.framePoses.isEmpty())
				continue;
			auto poses = box.framePoses;
			poses[edit.frame] = poses[edit.sourceFrame];
			setModelCollisionFrames(&box, std::move(poses));
		}
		for (auto &tag : candidate->tags)
		{
			if (!work.step())
			{
				return false;
			}
			if (tag.frameIndex == edit.frame)
			{
				const auto original = findModelTag(source, tag.name, edit.sourceFrame);
				if (!original)
				{
					return fail(error, QT_TRANSLATE_NOOP("VibeStudioModelAnimation", "The source pose is missing an attachment tag."));
				}
				tag.origin = original->origin;
				std::copy(std::begin(original->axis), std::end(original->axis), std::begin(tag.axis));
			}
		}
		return work.check();
	}
	const int inserted = edit.frame + 1, count = edit.inbetweenCount;
	const QString prefix = edit.text.isEmpty() ? QStringLiteral("blend") : edit.text;
	qint64 vertices = 0;
	for (const auto &surface : source.surfaces)
	{
		vertices += surface.vertexCount;
	}
	if (inserted >= source.frames.size() || count < 1 || count > modelDocumentMaxFrames - 2 ||
		source.frames.size() + count > modelDocumentMaxFrames || vertices * (source.frames.size() + count) > modelDocumentMaxFrameVertices)
	{
		return fail(error,
					QT_TRANSLATE_NOOP("VibeStudioModelAnimation",
									  "Insert in-between frames before the next pose, within 1024 frames and 1,048,576 frame vertices."));
	}
	if (!nameOk(prefix, 123))
	{
		return fail(error, QT_TRANSLATE_NOOP(
							   "VibeStudioModelAnimation",
							   "The generated frame-name prefix needs 1–123 characters without surrounding spaces or control characters."));
	}
	if (!changeModelMdlFrames(candidate, ModelMdlFrameChange::InsertInbetweens, edit.frame, count, error))
	{
		return false;
	}
	QVector<TagEndpoints> endpoints;
	for (auto &box : candidate->collisionBoxes)
	{
		if (box.framePoses.isEmpty())
			continue;
		QVector<ModelCollisionPose> poses;
		poses.reserve(box.framePoses.size() + count);
		for (int frame = 0; frame < box.framePoses.size(); ++frame)
		{
			if (!work.step())
				return false;
			if (frame == inserted)
			{
				for (int n = 1; n <= count; ++n)
				{
					if (!work.step())
						return false;
					ModelCollisionBox generated;
					if (!sampleModelCollisionBox(box, edit.frame, inserted, double(n) / (count + 1), &generated))
						return fail(error,
									QT_TRANSLATE_NOOP("VibeStudioModelAnimation", "Collision endpoint poses cannot be interpolated."));
					poses << ModelCollisionPose{generated.centre, generated.size, generated.rotation};
				}
			}
			poses << box.framePoses[frame];
		}
		setModelCollisionFrames(&box, std::move(poses));
	}
	for (const auto &tag : source.tags)
	{
		if (!work.step())
		{
			return false;
		}
		if (tag.frameIndex != edit.frame)
		{
			continue;
		}
		const auto next = findModelTag(source, tag.name, inserted);
		ModelTag checked;
		if (!next || !interpolateModelTag(tag, *next, 0.5, &checked))
		{
			return fail(error, QT_TRANSLATE_NOOP(
								   "VibeStudioModelAnimation",
								   "Attachment tags must exist with the same handedness in both endpoint poses. No frames were inserted."));
		}
		endpoints << TagEndpoints{tag, *next};
	}
	for (int surfaceIndex = 0; surfaceIndex < source.surfaces.size(); ++surfaceIndex)
	{
		const auto &original = source.surfaces[surfaceIndex];
		const auto &a = original.frames[edit.frame], &b = original.frames[inserted];
		auto &frames = candidate->surfaces[surfaceIndex].frames;
		frames.clear();
		frames.reserve(original.frames.size() + count);
		for (int frame = 0; frame < original.frames.size(); ++frame)
		{
			if (!work.step())
			{
				return false;
			}
			if (frame == inserted)
			{
				for (int n = 1; n <= count; ++n)
				{
					const double amount = double(n) / (count + 1);
					ModelFrameGeometry generated;
					generated.positions.reserve(original.vertexCount);
					generated.normals.reserve(original.vertexCount);
					for (int vertex = 0; vertex < original.vertexCount; ++vertex)
					{
						if (!work.step())
						{
							return false;
						}
						generated.positions << interpolateModelPosition(a.positions[vertex], b.positions[vertex], amount);
						ModelVec3 normal;
						if (!interpolateModelNormal(a.normals[vertex], b.normals[vertex], amount, &normal))
						{
							return fail(
								error,
								QT_TRANSLATE_NOOP(
									"VibeStudioModelAnimation",
									"Opposing endpoint normals cancel in a generated pose. Adjust the normals before inserting frames."));
						}
						generated.normals << normal;
					}
					frames << std::move(generated);
				}
			}
			frames << original.frames[frame];
		}
	}
	candidate->frames.clear();
	candidate->frames.reserve(source.frames.size() + count);
	for (int frame = 0; frame < source.frames.size(); ++frame)
	{
		if (!work.step())
		{
			return false;
		}
		if (frame == inserted)
		{
			for (int n = 1; n <= count; ++n)
			{
				if (!work.step())
				{
					return false;
				}
				const double amount = double(n) / (count + 1);
				ModelFrameInfo generated;
				generated.name = prefix + QLatin1Char('_') + QString::number(n).rightJustified(3, QLatin1Char('0'));
				generated.origin = interpolateModelPosition(source.frames[edit.frame].origin, source.frames[inserted].origin, amount);
				candidate->frames << generated;
			}
		}
		candidate->frames << source.frames[frame];
	}
	for (auto &tag : candidate->tags)
	{
		if (!work.step())
		{
			return false;
		}
		if (tag.frameIndex >= inserted)
		{
			tag.frameIndex += count;
		}
	}
	for (int n = 1; n <= count; ++n)
	{
		const double amount = double(n) / (count + 1);
		for (const auto &pair : endpoints)
		{
			if (!work.step())
			{
				return false;
			}
			auto generated = pair.first;
			// Endpoints were checked before any allocation; share the preview's
			// rigid interpolation so authored and transient intermediate poses agree.
			interpolateModelTag(pair.first, pair.second, amount, &generated);
			generated.frameIndex = edit.frame + n;
			candidate->tags << generated;
		}
	}
	for (auto &clip : candidate->animations)
	{
		if (!work.step())
		{
			return false;
		}
		if (clip.firstFrame >= inserted)
		{
			clip.firstFrame += count;
		}
		else if (clip.firstFrame + clip.frameCount > inserted)
		{
			clip.frameCount += count;
		}
	}
	return work.check();
}
} // namespace vibestudio
