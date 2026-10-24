#include "cli/model_tools.h"
#include "core/model_document.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <cmath>
namespace vibestudio::cli
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelCurveTransferCli)
};
ModelToolsCliResult fail(int code, const QString &message)
{
	ModelToolsCliResult result;
	result.exitCode = code;
	result.error = message;
	return result;
}
bool integer(const QString &text, int *value)
{
	bool ok = false;
	*value = text.toInt(&ok);
	return ok && *value >= 0 && text == QString::number(*value);
}
} // namespace
ModelToolsCliResult runModelCurveTransfer(const QStringList &args)
{
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--dry-run", "--overwrite", "--reverse", "--match-rate"};
	const QSet<QString> options{"--input",	"--output", "--source-clip", "--target-clip",	"--first",	"--last",		 "--target-first",
								"--factor", "--joint",	"--channel",	 "--settings-file", "--locale", "--catalog-root"};
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional;
	ModelEdit edit;
	edit.kind = ModelEditKind::TransferAnimationCurves;
	auto &o = edit.curveTransfer;
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
			o.joints.append(value);
		else if (key == "--channel")
		{
			const int channel = modelCurveChannelIndex(value);
			if (channel < 0)
				return fail(2, Text::tr("Use tx/ty/tz/rx/ry/rz/sx/sy/sz channel identifiers."));
			o.channels.append(channel);
		}
		else
			values[key] = value;
	}
	const bool input = seen.contains("--input");
	if (positional.size() != (input ? 2 : 3) || positional.value(0) != "model" || positional.value(1) != "curve-transfer")
		return fail(2, Text::tr("Provide one editable model source, positionally or with --input."));
	for (const auto &pair : QList<QPair<QString, int *>>{{"--source-clip", &o.sourceClip},
														 {"--target-clip", &o.targetClip},
														 {"--first", &o.first},
														 {"--last", &o.last},
														 {"--target-first", &o.targetFirst}})
		if (!integer(values.value(pair.first), pair.second))
			return fail(2, Text::tr("Provide %1 as a nonnegative integer.").arg(pair.first));
	if (seen.contains("--factor"))
	{
		bool ok = false;
		o.factor = values["--factor"].toDouble(&ok);
		if (!ok || !std::isfinite(o.factor) || o.factor <= 0 || o.factor > 1000000 || seen.contains("--match-rate"))
			return fail(2, Text::tr("Use a positive time scale within one million, or --match-rate, not both."));
	}
	o.reverse = seen.contains("--reverse");
	if (!values.value("--output").endsWith(".mesh.json", Qt::CaseInsensitive))
		return fail(2, Text::tr("Animation transfer requires --output ending in .mesh.json."));
	const auto source = input ? values["--input"] : positional[2];
	if (modelPathsReferToSameFile(source, values["--output"]))
		return fail(2, Text::tr("Choose an output different from the source, including aliases of the same file."));
	ModelDocument document;
	QString error;
	if (!document.load(source, &error))
		return fail(1, error);
	const auto before = document.mesh();
	if (o.sourceClip >= before.skeleton.clips.size() || o.targetClip >= before.skeleton.clips.size())
		return fail(4, Text::tr("Choose existing source and destination clips."));
	if (seen.contains("--match-rate"))
	{
		const double a = before.skeleton.clips[o.sourceClip].framesPerSecond, b = before.skeleton.clips[o.targetClip].framesPerSecond;
		if (a <= 0 || b <= 0)
			return fail(4, Text::tr("Matching duration requires explicit positive rates on both clips."));
		o.factor = b / a;
	}
	if (!document.edit(edit, &error))
		return fail(4, error);
	ModelCurveKeyReport report;
	if (!describeModelCurveTransfer(before, document.mesh(), o, &report, &error))
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
					  {"sourceClip", o.sourceClip},
					  {"targetClip", o.targetClip},
					  {"first", o.first},
					  {"last", o.last},
					  {"targetFirst", o.targetFirst},
					  {"targetLast", o.targetFirst + (o.last - o.first) * o.factor},
					  {"factor", o.factor},
					  {"reverse", o.reverse},
					  {"dryRun", dry},
					  {"written", !dry},
					  {"channels", modelCurveKeyReportJson(report)}};
	result.lines << Text::tr("Animation transfer validated across %1 channels. Review destination transitions and motion.")
						.arg(report.channels.size());
	result.payload.insert("outputSource", modelSourceSaveJson(document));
	if (const auto packed = modelSourceSaveNotice(document); !packed.isEmpty())
		result.lines << packed;
	return result;
}
} // namespace vibestudio::cli
