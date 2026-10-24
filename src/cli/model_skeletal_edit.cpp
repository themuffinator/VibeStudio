#include "cli/model_tools.h"
#include "core/model_document.h"
#include "core/model_skeleton.h"
#include "core/model_joint_tools.h"
#include "core/model_skeletal_working_set.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <cmath>

namespace vibestudio::cli
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelPoseCli)
};
ModelToolsCliResult fail(int code, const QString &message)
{
	return {code, message, {}, {}};
}
bool integer(const QString &text, int *value)
{
	bool ok = false;
	*value = text.toInt(&ok);
	return ok && *value >= 0 && text == QString::number(*value);
}
} // namespace
ModelToolsCliResult runModelPose(const QStringList &arguments)
{
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--dry-run", "--overwrite"};
	const QSet<QString> options{"--input",	"--output", "--operation", "--joint",	 "--name",			"--clip",		  "--frames",
								"--fps",	"--loop",	"--first",	   "--last",	 "--source-clip",	"--source-frame", "--translate",
								"--rotate", "--scale", "--matrix", "--space", "--children", "--clips", "--settings-file", "--locale", "--catalog-root"};
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional;
	for (int i = 1; i < arguments.size(); ++i)
	{
		const auto argument = arguments[i];
		if (!argument.startsWith('-'))
		{
			positional.append(argument);
			continue;
		}
		const int equal = argument.indexOf('=');
		const auto key = equal < 0 ? argument : argument.left(equal);
		if (seen.contains(key))
			return fail(2, Text::tr("Repeated option: %1.").arg(key));
		seen.insert(key);
		if (flags.contains(key) && equal < 0)
			continue;
		if (!options.contains(key))
			return fail(2, Text::tr("Unknown or invalid option: %1.").arg(key));
		const auto value = equal < 0 ? arguments.value(++i) : argument.mid(equal + 1);
		if (value.isEmpty() || value.startsWith("--"))
			return fail(2, Text::tr("Option %1 requires a value.").arg(key));
		values.insert(key, value);
	}
	if (positional.size() != (seen.contains("--input") ? 2 : 3) || positional.value(0) != "model" || positional.value(1) != "pose")
		return fail(2, Text::tr("Provide one editable model source, positionally or with --input."));
	const auto input = seen.contains("--input") ? values.value("--input") : positional[2];
	const auto operation = values.value("--operation", "list");
	const QHash<QString, ModelEditKind> kinds{
		{"bind", ModelEditKind::TransformBindJoint},		 {"add", ModelEditKind::AddSkeletalClip},
		{"duplicate", ModelEditKind::DuplicateSkeletalClip}, {"rename", ModelEditKind::RenameSkeletalClip},
		{"remove", ModelEditKind::DeleteSkeletalClip},		 {"resize", ModelEditKind::ResizeSkeletalClip},
		{"timing", ModelEditKind::SetSkeletalClipTiming},	 {"transform", ModelEditKind::TransformSkeletalPose},
		{"copy", ModelEditKind::CopySkeletalPose},			 {"interpolate", ModelEditKind::InterpolateSkeletalPoses},
		{"working-set", ModelEditKind::SetSkeletalWorkingSet}};
	const bool list = operation == "list", add = operation == "add", bind = operation == "bind", transform = operation == "transform";
	const bool workingSet = operation == "working-set";
	if (!list && !kinds.contains(operation))
		return fail(2, Text::tr("Choose list, bind, add, duplicate, rename, remove, resize, timing, transform, copy, interpolate or working-set."));
	QSet<QString> required;
	if (!list)
		required.insert("--output");
	if (!list && !add && !bind && !workingSet)
		required.insert("--clip");
	if (add || operation == "duplicate" || operation == "rename")
		required.insert("--name");
	if (add || operation == "resize")
		required.insert("--frames");
	if (add || operation == "timing")
		required.insert("--fps");
	if (transform || operation == "copy" || operation == "interpolate")
		required.unite({"--first", "--last"});
	if (bind || transform)
		required.insert("--joint");
	if (operation == "copy")
		required.unite({"--source-clip", "--source-frame"});
	QSet<QString> allowed = required;
	allowed.unite({"--input", "--operation", "--settings-file", "--locale", "--catalog-root"});
	if (list)
		allowed.unite({"--clip", "--first"});
	if (add || operation == "timing")
		allowed.insert("--loop");
	if (bind || transform)
		allowed.unite({"--translate", "--rotate", "--scale", "--matrix", "--space", "--children"});
	if (workingSet)
		allowed.insert("--clips");
	for (const auto &key : required)
		if (!seen.contains(key))
			return fail(2, Text::tr("Operation %1 requires %2.").arg(operation, key));
	for (const auto &key : seen)
		if ((!flags.contains(key) && !allowed.contains(key)) || (list && (key == "--dry-run" || key == "--overwrite")))
			return fail(2, Text::tr("Option %1 does not apply to %2.").arg(key, operation));
	if (list && seen.contains("--clip") != seen.contains("--first"))
		return fail(2, Text::tr("Pose inspection requires --clip and --first together."));
	if (!list && !values.value("--output").endsWith(".mesh.json", Qt::CaseInsensitive))
		return fail(2, Text::tr("Pose edits require an output ending in .mesh.json."));
	ModelEdit edit;
	auto &o = edit.skeletal;
	o.joint = values.value("--joint");
	o.name = values.value("--name");
	if (seen.contains("--matrix"))
	{
		for (const auto &key : {"--translate", "--rotate", "--scale", "--space"})
			if (seen.contains(key)) return fail(2, Text::tr("Matrix deltas cannot be combined with numeric transforms or --space."));
		const auto parts = values.value("--matrix").split(',');
		if (parts.size() != 12) return fail(2, Text::tr("A matrix delta requires twelve row-major, comma-separated numbers."));
		for (int i = 0; i < 12; ++i)
		{
			bool ok = false;
			o.delta.m[i] = parts[i].toFloat(&ok);
			if (!ok) return fail(2, Text::tr("Matrix coordinates must be finite numbers."));
		}
		QString error;
		if (!validModelJointDelta(o.delta, &error)) return fail(2, error);
		o.matrixTransform = true;
	}
	for (const auto &field : {qMakePair("--clip", &o.clip), qMakePair("--first", &o.first), qMakePair("--last", &o.last),
							  qMakePair("--frames", &o.frameCount), qMakePair("--source-frame", &o.sourceFrame)})
		if (seen.contains(field.first) && !integer(values.value(field.first), field.second))
			return fail(2, Text::tr("Option %1 requires a nonnegative integer.").arg(field.first));
	if (seen.contains("--source-clip") && values.value("--source-clip") != "bind" && !integer(values.value("--source-clip"), &o.sourceClip))
		return fail(2, Text::tr("Source clip must be an index or bind."));
	for (const auto &field : {qMakePair("--loop", &o.loops), qMakePair("--children", &o.descendants)})
		if (seen.contains(field.first))
		{
			const auto value = values.value(field.first);
			if (value != "yes" && value != "no")
				return fail(2, Text::tr("Option %1 requires yes or no.").arg(field.first));
			*field.second = value == "yes";
		}
	if (seen.contains("--space"))
	{
		const auto value = values.value("--space");
		if (value != "model" && value != "local")
			return fail(2, Text::tr("Space must be model or local."));
		o.local = value == "local";
	}
	if (seen.contains("--fps"))
	{
		bool ok = false;
		o.rate = values.value("--fps").toDouble(&ok);
		if (!ok || !std::isfinite(o.rate) || o.rate < .001 || o.rate > 1000)
			return fail(2, Text::tr("FPS must be from 0.001 to 1000."));
	}
	for (const auto &field : {qMakePair("--translate", &o.translation), qMakePair("--rotate", &o.rotation), qMakePair("--scale", &o.scale)})
		if (seen.contains(field.first))
		{
			const auto parts = values.value(field.first).split(',');
			if (parts.size() != 3)
				return fail(2, Text::tr("Option %1 requires three comma-separated numbers.").arg(field.first));
			float *axes[]{&field.second->x, &field.second->y, &field.second->z};
			for (int i = 0; i < 3; ++i)
			{
				bool ok = false;
				*axes[i] = parts[i].toFloat(&ok);
				if (!ok || !std::isfinite(*axes[i]))
					return fail(2, Text::tr("Transform coordinates must be finite numbers."));
			}
		}
	ModelDocument document;
	QString error;
	if (!document.load(input, &error))
		return fail(1, error);
	const bool dry = seen.contains("--dry-run");
	if (workingSet)
	{
		// Without --clips, every clip that fits the automatic budget, in order;
		// "-" keeps only the bind pose.
		const auto &mesh = document.mesh();
		if (mesh.skeleton.isEmpty())
			return fail(4, Text::tr("The model has no skeleton, so it has no clips for a working set."));
		if (!seen.contains("--clips"))
			o.workingSet = planModelSkeletalWorkingSet(mesh, nullptr, modelSkeletalAutomaticBudget()).clips;
		else if (values.value("--clips") != "-" &&
				 !modelSkeletalClipIndexes(mesh.skeleton, values.value("--clips").split(','), &o.workingSet, &error))
			return fail(2, error);
	}
	if (!list)
	{
		edit.kind = kinds[operation];
		if (!document.edit(edit, &error))
			return fail(4, error);
		const auto target = inspectModelWriteTarget(values.value("--output"));
		if (!target.isValid())
			return fail(1, target.error);
		if (target.existed && !seen.contains("--overwrite"))
			return fail(1, Text::tr("The output exists; use --overwrite to replace it."));
		if (!document.save(values.value("--output"), seen.contains("--overwrite"), &error, {}, dry))
			return fail(1, error);
	}
	const auto &skeleton = document.mesh().skeleton;
	QJsonArray clips, joints;
	ModelToolsCliResult result;
	for (int i = 0; i < skeleton.clips.size(); ++i)
	{
		const auto &clip = skeleton.clips[i];
		const int first = skeleton.bakedClipForFrame.indexOf(i);
		clips.append(QJsonObject{{"index", i},
								 {"name", clip.name},
								 {"frames", int(clip.frames.size())},
								 {"fps", clip.framesPerSecond},
								 {"loops", clip.loops},
								 {"previewFirst", first},
								 {"inWorkingSet", first >= 0}});
		result.lines << Text::tr("%1: %2 · %3 frames · %4 FPS · loop %5 · preview first %6")
							.arg(i)
							.arg(clip.name)
							.arg(clip.frames.size())
							.arg(clip.framesPerSecond)
							.arg(clip.loops ? Text::tr("yes") : Text::tr("no"))
							.arg(first);
	}
	// The clips baked into editable frames, the budget they use, and why the
	// others are left out.
	const auto workingSetPlan = modelSkeletalWorkingSet(document.mesh());
	if (!skeleton.isEmpty())
		result.lines << modelSkeletalWorkingSetLines(workingSetPlan);
	auto matrices = modelSkeletonBindPose(skeleton);
	const bool inspectPose = list && seen.contains("--clip");
	if (inspectPose)
	{
		if (o.clip >= skeleton.clips.size() || o.first >= skeleton.clips[o.clip].frames.size())
			return fail(4, Text::tr("The inspected clip/frame does not exist."));
		matrices = skeleton.clips[o.clip].frames[o.first];
	}
	result.lines << (inspectPose ? Text::tr("Joint poses: clip %1, pose %2.").arg(o.clip).arg(o.first)
								 : Text::tr("Joint poses: bind pose."));
	for (int i = 0; i < matrices.size(); ++i)
	{
		QJsonArray matrix;
		for (float value : matrices[i].m)
			matrix.append(value);
		joints.append(QJsonObject{{"index", i}, {"name", skeleton.joints[i].name}, {"matrix", matrix}});
		const auto position = modelJointTranslation(matrices[i]);
		result.lines << Text::tr("Joint %1: %2 · origin %3, %4, %5")
							.arg(i)
							.arg(skeleton.joints[i].name)
							.arg(position.x)
							.arg(position.y)
							.arg(position.z);
	}
	result.payload = {{"source", QFileInfo(input).absoluteFilePath()},
					  {"operation", operation},
					  {"clips", clips},
					  {"pose", joints},
					  {"poseSpace", "model"},
					  {"poseClip", inspectPose ? o.clip : -1},
					  {"poseFrame", inspectPose ? o.first : 0},
					  {"workingSet", modelSkeletalWorkingSetJson(workingSetPlan)},
					  {"written", !list && !dry},
					  {"dryRun", dry}};
	if (!list)
	{
		result.payload.insert("outputPath", QFileInfo(values.value("--output")).absoluteFilePath());
		result.lines.prepend(dry ? Text::tr("Skeletal edit validated; no files written.")
								 : Text::tr("Saved skeletal edit to %1.").arg(values.value("--output")));
		result.payload.insert("outputSource", modelSourceSaveJson(document));
		if (const auto packed = modelSourceSaveNotice(document); !packed.isEmpty())
			result.lines << packed;
	}
	return result;
}
} // namespace vibestudio::cli
