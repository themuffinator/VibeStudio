#include "cli/model_tools.h"
#include "core/model_document.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <cmath>
namespace vibestudio::cli
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelAnimationCurvesCli)
};
ModelToolsCliResult fail(int code, const QString &error)
{
	ModelToolsCliResult r;
	r.exitCode = code;
	r.error = error;
	return r;
}
bool integer(const QString &s, int *n)
{
	bool ok = false;
	*n = s.toInt(&ok);
	return ok && *n >= 0 && s == QString::number(*n);
}
bool number(const QString &s, double *n)
{
	bool ok = false;
	*n = s.toDouble(&ok);
	return ok && std::isfinite(*n) && std::abs(*n) <= 1000000;
}
} // namespace
ModelToolsCliResult runModelCurves(const QStringList &args)
{
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--dry-run", "--overwrite"};
	const QSet<QString> options{"--input",	   "--output",	  "--operation",	 "--clip",			"--joint",		 "--channel",
								"--frame",	   "--new-frame", "--value",		 "--interpolation", "--tangent",	 "--in-slope",
								"--out-slope", "--curves",	  "--settings-file", "--locale",		"--catalog-root"};
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional;
	for (int i = 1; i < args.size(); ++i)
	{
		const auto arg = args[i];
		if (!arg.startsWith('-'))
		{
			positional.append(arg);
			continue;
		}
		const int equal = arg.indexOf('=');
		const auto key = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(key))
			return fail(2, Text::tr("Repeated option: %1.").arg(key));
		seen.insert(key);
		if (flags.contains(key) && equal < 0)
			continue;
		if (!options.contains(key))
			return fail(2, Text::tr("Unknown or invalid option: %1.").arg(key));
		const auto value = equal < 0 ? args.value(++i) : arg.mid(equal + 1);
		if (value.isEmpty() || value.startsWith("--"))
			return fail(2, Text::tr("Option %1 requires a value.").arg(key));
		values[key] = value;
	}
	const bool input = seen.contains("--input");
	if (positional.size() != (input ? 2 : 3) || positional.value(0) != "model" || positional.value(1) != "curves")
		return fail(2, Text::tr("Provide one editable model source, positionally or with --input."));
	const QStringList actions{"capture", "replace", "set-key", "remove-key", "move-key", "bake"};
	const auto action = values.value("--operation", "inspect");
	const bool inspect = action == "inspect";
	const int actionIndex = actions.indexOf(action);
	if (!inspect && actionIndex < 0)
		return fail(2, Text::tr("Operation must be inspect, capture, replace, set-key, remove-key, move-key or bake."));
	ModelEdit edit;
	edit.kind = ModelEditKind::EditAnimationCurves;
	auto &o = edit.curves;
	if (!integer(values.value("--clip"), &o.clip))
		return fail(2, Text::tr("Provide --clip as a nonnegative integer."));
	o.joint = values.value("--joint");
	QSet<QString> allowed{"--cli",	"--json",  "--quiet",		  "--verbose", "--operation",	"--input",
						  "--clip", "--joint", "--settings-file", "--locale",  "--catalog-root"};
	if (!inspect)
	{
		allowed.unite({"--output", "--dry-run", "--overwrite"});
		o.action = ModelCurveAction(actionIndex);
		if (o.joint.isEmpty() || !values.value("--output").endsWith(".mesh.json", Qt::CaseInsensitive))
			return fail(2, Text::tr("Curve edits require --joint and --output ending in .mesh.json."));
		if (action == "replace")
			allowed.insert("--curves");
		if (action.endsWith("-key"))
		{
			allowed.unite({"--channel", "--frame"});
			o.channel = modelCurveChannelIndex(values.value("--channel"));
			if (o.channel < 0 || !integer(values.value("--frame"), &o.frame))
				return fail(2, Text::tr("Key edits require --channel tx/ty/tz/rx/ry/rz/sx/sy/sz and a nonnegative --frame."));
		}
		if (action == "set-key")
			allowed.unite({"--value", "--interpolation", "--tangent", "--in-slope", "--out-slope"});
		if (action == "move-key")
		{
			allowed.insert("--new-frame");
			if (!integer(values.value("--new-frame"), &o.newFrame))
				return fail(2, Text::tr("Moving a key requires a nonnegative --new-frame."));
		}
	}
	for (const auto &key : seen)
		if (!allowed.contains(key))
			return fail(2, Text::tr("Option %1 does not apply to %2.").arg(key, action));
	const auto source = input ? values["--input"] : positional[2];
	ModelDocument document;
	QString error;
	if (!document.load(source, &error))
		return fail(1, error);
	if (o.clip >= document.mesh().skeleton.clips.size())
		return fail(4, Text::tr("The selected skeletal clip does not exist."));
	const auto &clip = document.mesh().skeleton.clips[o.clip];
	if (action == "set-key")
	{
		for (const auto &group : clip.curves)
			if (group.joint == o.joint)
				for (const auto &key : group.channels[o.channel])
					if (key.frame == o.frame)
						o.key = key;
		o.key.frame = o.frame;
		if (!number(values.value("--value"), &o.key.value))
			return fail(2, Text::tr("Setting a key requires a finite --value within one million units."));
		if (seen.contains("--interpolation"))
		{
			const int mode = QStringList{"step", "linear", "cubic", "step-next"}.indexOf(values["--interpolation"]);
			if (mode < 0)
				return fail(2, Text::tr("Interpolation must be step, linear, cubic or step-next."));
			o.key.interpolation = ModelCurveInterpolation(mode);
		}
		if (seen.contains("--tangent"))
		{
			const int mode = QStringList{"auto", "clamped", "free", "flat"}.indexOf(values["--tangent"]);
			if (mode < 0)
				return fail(2, Text::tr("Tangent mode must be auto, clamped, free or flat."));
			o.key.tangent = ModelCurveTangent(mode);
		}
		for (const auto &name : QStringList{"--in-slope", "--out-slope"})
			if (seen.contains(name))
			{
				double slope = 0;
				if (o.key.tangent != ModelCurveTangent::Free || !number(values[name], &slope))
					return fail(2, Text::tr("Explicit slopes require free tangents and finite values within one million units."));
				if (name == "--in-slope")
					o.key.inSlope = slope;
				else
					o.key.outSlope = slope;
			}
	}
	if (action == "replace")
	{
		if (modelPathsReferToSameFile(values["--output"], values["--curves"]))
			return fail(1, Text::tr("The output cannot replace the curve input, including aliases of the same file."));
		QFile file(values.value("--curves"));
		if (!file.open(QIODevice::ReadOnly))
			return fail(1, Text::tr("Cannot read the replacement curve file."));
		const auto bytes = file.read(modelDocumentMaxSourceBytes + 1);
		QJsonParseError parse;
		const auto json = QJsonDocument::fromJson(bytes, &parse);
		if (bytes.size() > modelDocumentMaxSourceBytes || parse.error != QJsonParseError::NoError || !json.isObject())
			return fail(2, Text::tr("Replacement curves must be one bounded JSON joint-curve object."));
		if (!modelPoseCurvesFromJson(json.object(), int(clip.frames.size()), &o.group, &error))
			return fail(4, error);
	}
	if (inspect)
	{
		QVector<ModelPoseCurves> groups;
		for (const auto &group : clip.curves)
			if (o.joint.isEmpty() || group.joint == o.joint)
				groups.append(group);
		if (!o.joint.isEmpty() && groups.isEmpty())
			return fail(4, Text::tr("The selected joint has no stored curves."));
		ModelToolsCliResult result;
		result.payload = {{"clip", o.clip}, {"name", clip.name}, {"samples", clip.frames.size()}, {"curves", modelClipCurvesJson(groups)}};
		result.lines << Text::tr("Clip %1 · joints with curves: %2.").arg(clip.name).arg(groups.size());
		return result;
	}
	if (!document.edit(edit, &error))
		return fail(4, error);
	const auto target = inspectModelWriteTarget(values["--output"]);
	if (!target.isValid())
		return fail(1, target.error);
	if (target.existed && !seen.contains("--overwrite"))
		return fail(1, Text::tr("The output already exists; use --overwrite to replace it."));
	const bool dry = seen.contains("--dry-run");
	if (!document.save(values["--output"], seen.contains("--overwrite"), &error, {}, dry))
		return fail(1, error);
	ModelToolsCliResult result;
	result.payload = {{"source", QFileInfo(source).absoluteFilePath()},
					  {"output", QFileInfo(values["--output"]).absoluteFilePath()},
					  {"operation", action},
					  {"clip", o.clip},
					  {"joint", o.joint},
					  {"dryRun", dry},
					  {"written", !dry},
					  {"curves", modelClipCurvesJson(document.mesh().skeleton.clips[o.clip].curves)}};
	result.lines << Text::tr("Animation curve operation %1 validated for joint %2.").arg(action, o.joint);
	result.payload.insert("outputSource", modelSourceSaveJson(document));
	if (const auto packed = modelSourceSaveNotice(document); !packed.isEmpty())
		result.lines << packed;
	return result;
}
} // namespace vibestudio::cli
