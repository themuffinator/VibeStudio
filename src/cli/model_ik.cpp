#include "cli/model_tools.h"
#include "core/model_document.h"
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
	Q_DECLARE_TR_FUNCTIONS(ModelIkCli)
};
ModelToolsCliResult fail(int code, const QString &message)
{
	ModelToolsCliResult r;
	r.exitCode = code;
	r.error = message;
	return r;
}
bool integer(const QString &s, int *n)
{
	bool ok = false;
	*n = s.toInt(&ok);
	return ok && *n >= 0 && s == QString::number(*n);
}
bool vector(const QString &s, ModelVec3 *result)
{
	const auto parts = s.split(',');
	if (parts.size() != 3)
		return false;
	float values[3];
	for (int i = 0; i < 3; ++i)
	{
		bool ok = false;
		const double n = parts[i].toDouble(&ok);
		if (!ok || !std::isfinite(n) || std::abs(n) > 1000000)
			return false;
		values[i] = float(n);
	}
	*result = {values[0], values[1], values[2]};
	return true;
}
} // namespace
ModelToolsCliResult runModelIk(const QStringList &args)
{
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--dry-run", "--overwrite", "--nearest-reachable", "--follow-end"};
	const QSet<QString> options{"--input",	"--output", "--clip",		   "--first",  "--last",		"--end-joint",
								"--target", "--pole",	"--settings-file", "--locale", "--catalog-root"};
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
	if (positional.size() != (input ? 2 : 3) || positional.value(0) != "model" || positional.value(1) != "ik")
		return fail(2, Text::tr("Provide one editable model source, positionally or with --input."));
	if (!values.value("--output").endsWith(".mesh.json", Qt::CaseInsensitive))
		return fail(2, Text::tr("Inverse kinematics requires --output ending in .mesh.json."));
	ModelEdit edit;
	edit.kind = ModelEditKind::SolveTwoBoneIk;
	auto &o = edit.ik;
	if (!integer(values.value("--clip"), &o.clip) || !integer(values.value("--first"), &o.first) ||
		!integer(values.value("--last"), &o.last) || values.value("--end-joint").isEmpty())
		return fail(2, Text::tr("Provide --clip, --first and --last as nonnegative integers, and --end-joint as an exact joint name."));
	o.endJoint = values["--end-joint"];
	if (!vector(values.value("--target"), &o.target) || !vector(values.value("--pole"), &o.pole))
		return fail(2, Text::tr("Provide --target x,y,z and --pole x,y,z as finite model-space coordinates within one million units."));
	o.nearestReachable = seen.contains("--nearest-reachable");
	o.keepEndOrientation = !seen.contains("--follow-end");
	const auto source = input ? values["--input"] : positional[2];
	ModelDocument document;
	QString error;
	if (!document.load(source, &error))
		return fail(1, error);
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
	QJsonArray samples;
	int reached = 0;
	for (const auto &sample : modelIkSamples(document.mesh(), o))
	{
		reached += sample.reached;
		samples.append(QJsonObject{{"sample", sample.frame},
								   {"end", QJsonArray{sample.end.x, sample.end.y, sample.end.z}},
								   {"error", sample.error},
								   {"tolerance", sample.tolerance},
								   {"reached", sample.reached}});
	}
	ModelToolsCliResult result;
	result.payload = {{"source", QFileInfo(source).absoluteFilePath()},
					  {"output", QFileInfo(values["--output"]).absoluteFilePath()},
					  {"clip", o.clip},
					  {"endJoint", o.endJoint},
					  {"first", o.first},
					  {"last", o.last},
					  {"nearestReachable", o.nearestReachable},
					  {"keepEndOrientation", o.keepEndOrientation},
					  {"samples", samples},
					  {"dryRun", dry},
					  {"written", !dry}};
	result.lines << Text::tr("Inverse kinematics reviewed. Samples reaching the target: %1 / %2.").arg(reached).arg(samples.size());
	result.payload.insert("outputSource", modelSourceSaveJson(document));
	if (const auto packed = modelSourceSaveNotice(document); !packed.isEmpty())
		result.lines << packed;
	return result;
}
} // namespace vibestudio::cli
