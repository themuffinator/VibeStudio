#include "cli/model_tools.h"
#include "core/model_document.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
namespace vibestudio::cli
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelCurveKeysCli)
};
ModelToolsCliResult fail(int code, const QString &error)
{
	ModelToolsCliResult r;
	r.exitCode = code;
	r.error = error;
	return r;
}
bool integer(const QString &text, int *value)
{
	bool ok = false;
	*value = text.toInt(&ok);
	return ok && *value >= 0 && text == QString::number(*value);
}
bool number(const QString &text, double *value)
{
	bool ok = false;
	*value = text.toDouble(&ok);
	return ok && std::isfinite(*value) && std::abs(*value) <= 1000000;
}
} // namespace
ModelToolsCliResult runModelCurveKeys(const QStringList &args)
{
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--dry-run", "--overwrite", "--snap"};
	const QSet<QString> options{
		"--input",		 "--output",		"--operation", "--clip",		"--joint",	"--channel",		   "--first",
		"--last",		 "--samples",		"--pivot",	   "--factor",		"--offset", "--translation-error", "--rotation-error",
		"--scale-error", "--settings-file", "--locale",	   "--catalog-root"};
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional, joints;
	QVector<int> channels;
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
		if (seen.contains(key) && key != "--joint" && key != "--channel")
			return fail(2, Text::tr("Repeated option: %1.").arg(key));
		seen.insert(key);
		if (flags.contains(key) && equal < 0)
			continue;
		if (!options.contains(key))
			return fail(2, Text::tr("Unknown or invalid option: %1.").arg(key));
		const auto value = equal < 0 ? args.value(++i) : arg.mid(equal + 1);
		if (value.isEmpty() || value.startsWith("--"))
			return fail(2, Text::tr("Option %1 requires a value.").arg(key));
		if (key == "--joint")
		{
			if (joints.contains(value))
				return fail(2, Text::tr("Joint selectors must be distinct."));
			joints.append(value);
		}
		else if (key == "--channel")
		{
			const int channel = modelCurveChannelIndex(value);
			if (channel < 0 || channels.contains(channel))
				return fail(2, Text::tr("Channel selectors must be distinct tx/ty/tz/rx/ry/rz/sx/sy/sz identifiers."));
			channels.append(channel);
		}
		else
			values[key] = value;
	}
	const bool input = seen.contains("--input");
	if (positional.size() != (input ? 2 : 3) || positional.value(0) != "model" || positional.value(1) != "curve-keys")
		return fail(2, Text::tr("Provide one editable model source, positionally or with --input."));
	const QStringList actions{"retime", "duplicate", "remove", "reduce"};
	const auto action = values.value("--operation", "inspect");
	const bool inspect = action == "inspect";
	const int actionIndex = actions.indexOf(action);
	if (!inspect && actionIndex < 0)
		return fail(2, Text::tr("Operation must be inspect, retime, duplicate, remove or reduce."));
	ModelEdit edit;
	edit.kind = ModelEditKind::EditCurveKeys;
	auto &o = edit.curveKeys;
	if (!integer(values.value("--clip"), &o.clip))
		return fail(2, Text::tr("Provide --clip as a nonnegative integer."));
	QSet<QString> allowed{"--cli",	   "--json",  "--quiet", "--verbose", "--input",		 "--operation", "--clip",		 "--joint",
						  "--channel", "--first", "--last",	 "--samples", "--settings-file", "--locale",	"--catalog-root"};
	if (!inspect)
	{
		o.action = ModelCurveKeyAction(actionIndex);
		allowed.unite({"--output", "--dry-run", "--overwrite"});
		if (!values.value("--output").endsWith(".mesh.json", Qt::CaseInsensitive))
			return fail(2, Text::tr("Key edits require --output ending in .mesh.json."));
		if (action == "retime" || action == "duplicate")
		{
			allowed.unite({"--pivot", "--factor", "--offset", "--snap"});
			o.snap = seen.contains("--snap");
			for (const auto &name : QStringList{"--pivot", "--factor", "--offset"})
				if (seen.contains(name))
				{
					double value = 0;
					if (!number(values[name], &value))
						return fail(2, Text::tr("Timing options require finite numbers within one million units."));
					if (name == "--pivot")
						o.pivot = value;
					else if (name == "--factor")
						o.factor = value;
					else
						o.offset = value;
				}
		}
		if (action == "reduce")
		{
			const QStringList errors{"--translation-error", "--rotation-error", "--scale-error"};
			for (int i = 0; i < 3; ++i)
			{
				allowed.insert(errors[i]);
				if (seen.contains(errors[i]) && (!number(values[errors[i]], &o.tolerance[i]) || o.tolerance[i] < 0))
					return fail(2, Text::tr("Reduction error limits must be finite and nonnegative."));
			}
		}
	}
	for (const auto &key : seen)
		if (!allowed.contains(key))
			return fail(2, Text::tr("Option %1 does not apply to %2.").arg(key, action));
	if (seen.contains("--samples") && (seen.contains("--first") || seen.contains("--last")))
		return fail(2, Text::tr("Choose an inclusive sample range or an exact sample list, not both."));
	int first = 0, last = 0;
	QVector<int> samples;
	if (seen.contains("--samples"))
	{
		for (const auto &part : values["--samples"].split(','))
		{
			int n = 0;
			if (!integer(part, &n) || samples.contains(n))
				return fail(2, Text::tr("Sample lists need distinct nonnegative integer indices."));
			samples.append(n);
		}
		std::sort(samples.begin(), samples.end());
	}
	const auto source = input ? values["--input"] : positional[2];
	ModelDocument document;
	QString error;
	if (!document.load(source, &error))
		return fail(1, error);
	if (o.clip >= document.mesh().skeleton.clips.size())
		return fail(4, Text::tr("The selected skeletal clip does not exist."));
	const auto before = document.mesh().skeleton.clips[o.clip];
	last = int(before.frames.size()) - 1;
	if ((seen.contains("--first") && !integer(values["--first"], &first)) ||
		(seen.contains("--last") && !integer(values["--last"], &last)) || first > last || last >= before.frames.size())
		return fail(2, Text::tr("Choose an inclusive sample range inside the clip."));
	QSet<QString> matchedJoints;
	QSet<int> matchedSamples;
	for (const auto &group : before.curves)
	{
		if (!joints.isEmpty() && !joints.contains(group.joint))
			continue;
		matchedJoints.insert(group.joint);
		for (int c = 0; c < 9; ++c)
		{
			if (!channels.isEmpty() && !channels.contains(c))
				continue;
			ModelCurveKeySelection selection{group.joint, c, {}};
			for (const auto &key : group.channels[c])
				if (samples.isEmpty() ? key.frame >= first && key.frame <= last
									  : std::binary_search(samples.cbegin(), samples.cend(), key.frame))
				{
					selection.samples.append(key.frame);
					matchedSamples.insert(key.frame);
				}
			if (!selection.samples.isEmpty())
				o.selection.append(selection);
		}
	}
	if (o.selection.isEmpty() || (!joints.isEmpty() && matchedJoints.size() != joints.size()) ||
		(!samples.isEmpty() && matchedSamples.size() != samples.size()))
		return fail(4, Text::tr("Selectors must match stored joint curves and existing keys. Capture sampled motion first if needed."));
	if (!inspect && !document.edit(edit, &error))
		return fail(4, error);
	ModelCurveKeyReport report;
	if (!describeModelCurveKeyChanges(before.curves, document.mesh().skeleton.clips[o.clip].curves, int(before.frames.size()), o.selection,
									  &report, &error))
		return fail(4, error);
	const bool dry = seen.contains("--dry-run");
	if (!inspect)
	{
		const auto target = inspectModelWriteTarget(values["--output"]);
		if (!target.isValid())
			return fail(1, target.error);
		if (target.existed && !seen.contains("--overwrite"))
			return fail(1, Text::tr("The output already exists; use --overwrite to replace it."));
		if (!document.save(values["--output"], seen.contains("--overwrite"), &error, {}, dry))
			return fail(1, error);
	}
	ModelToolsCliResult result;
	result.payload = {{"source", QFileInfo(source).absoluteFilePath()},
					  {"operation", action},
					  {"clip", o.clip},
					  {"dryRun", dry},
					  {"written", !inspect && !dry},
					  {"channels", modelCurveKeyReportJson(report)}};
	if (!inspect)
	{
		result.payload["output"] = QFileInfo(values["--output"]).absoluteFilePath();
		result.payload.insert("outputSource", modelSourceSaveJson(document));
		if (const auto packed = modelSourceSaveNotice(document); !packed.isEmpty())
			result.lines << packed;
	}
	result.lines << Text::tr("Key operation %1 validated across %2 channels.").arg(action).arg(report.channels.size());
	return result;
}
} // namespace vibestudio::cli
