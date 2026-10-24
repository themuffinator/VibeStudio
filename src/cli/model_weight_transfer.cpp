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
	Q_DECLARE_TR_FUNCTIONS(ModelWeightTransferCli)
};
ModelToolsCliResult fail(int code, const QString &message)
{
	ModelToolsCliResult r;
	r.exitCode = code;
	r.error = message;
	return r;
}
bool integer(const QString &text, int *result)
{
	bool ok = false;
	*result = text.toInt(&ok);
	return ok && *result >= 0 && text == QString::number(*result);
}
} // namespace
ModelToolsCliResult runModelWeightTransfer(const QStringList &arguments)
{
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--dry-run", "--overwrite", "--skip-unmatched"};
	const QSet<QString> options{"--input",	  "--output",	 "--source-surface", "--surface",		   "--vertices", "--method",
								"--mirror",	  "--side",		 "--plane",			 "--centre-tolerance", "--distance", "--normal-angle",
								"--strength", "--joint-map", "--locked-joints",	 "--settings-file",	   "--locale",	 "--catalog-root"};
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional;
	for (int i = 1; i < arguments.size(); ++i)
	{
		const auto arg = arguments[i];
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
		const auto value = equal < 0 ? arguments.value(++i) : arg.mid(equal + 1);
		if (value.isEmpty() || value.startsWith("--"))
			return fail(2, Text::tr("Option %1 requires a value.").arg(key));
		values[key] = value;
	}

	const bool input = seen.contains("--input");
	if (positional.size() != (input ? 2 : 3) || positional.value(0) != "model" || positional.value(1) != "weight-transfer")
		return fail(2, Text::tr("Provide one editable model source, positionally or with --input."));
	if (!values.value("--output").endsWith(".mesh.json", Qt::CaseInsensitive))
		return fail(2, Text::tr("Weight transfer requires --output ending in .mesh.json."));
	ModelEdit edit;
	edit.kind = ModelEditKind::TransferSkinWeights;
	auto &o = edit.weightTransfer;
	if (!integer(values.value("--source-surface"), &o.sourceSurface) || !integer(values.value("--surface"), &edit.selection.surface) ||
		!seen.contains("--vertices"))
		return fail(2, Text::tr("Provide --source-surface, --surface and --vertices all or a comma-separated list."));
	const auto method = values.value("--method", "triangle");
	if (method != "vertex" && method != "triangle")
		return fail(2, Text::tr("Method must be vertex or triangle."));
	o.method = method == "vertex" ? ModelWeightTransferMethod::Vertex : ModelWeightTransferMethod::Triangle;
	const auto mirror = values.value("--mirror", "none");
	const QStringList axes{"none", "x", "y", "z"};
	if (!axes.contains(mirror))
		return fail(2, Text::tr("Mirror must be none, x, y or z."));
	o.mirrorAxis = axes.indexOf(mirror) - 1;
	const auto side = values.value("--side", "all");
	const QStringList sides{"all", "positive-to-negative", "negative-to-positive"};
	if (!sides.contains(side) ||
		(o.mirrorAxis < 0 && (seen.contains("--side") || seen.contains("--plane") || seen.contains("--centre-tolerance"))))
		return fail(
			2,
			Text::tr(
				"Side, plane and centre tolerance require a mirror axis. Side must be all, positive-to-negative or negative-to-positive."));
	o.side = ModelWeightTransferSide(sides.indexOf(side));
	const QMap<QString, double *> numbers{{"--plane", &o.plane},
										  {"--centre-tolerance", &o.centreTolerance},
										  {"--distance", &o.maximumDistance},
										  {"--normal-angle", &o.normalAngle},
										  {"--strength", &o.strength}};
	for (auto it = numbers.cbegin(); it != numbers.cend(); ++it)
		if (seen.contains(it.key()))
		{
			bool ok = false;
			*it.value() = values[it.key()].toDouble(&ok);
			if (!ok || !std::isfinite(*it.value()))
				return fail(2, Text::tr("Transfer numbers must be finite."));
		}
	if (seen.contains("--locked-joints"))
		o.lockedJoints = values["--locked-joints"].split(',');
	o.skipUnmatched = seen.contains("--skip-unmatched");
	if (seen.contains("--joint-map"))
	{
		if (modelPathsReferToSameFile(values["--output"], values["--joint-map"]))
			return fail(1, Text::tr("The output cannot replace the joint map input, including aliases of the same file."));
		QFile file(values["--joint-map"]);
		if (!file.open(QIODevice::ReadOnly) || file.size() > 65536)
			return fail(2, Text::tr("Joint maps must be readable JSON objects of at most 64 KiB."));
		const auto bytes = file.read(65537);
		QJsonParseError parse;
		const auto doc = QJsonDocument::fromJson(bytes, &parse);
		if (bytes.size() > 65536 || parse.error != QJsonParseError::NoError || !doc.isObject())
			return fail(2, Text::tr("A joint map is a JSON object mapping exact source names to destination names."));
		const auto object = doc.object();
		for (auto it = object.constBegin(); it != object.constEnd(); ++it)
		{
			if (it.key().isEmpty() || !it.value().isString() || it.value().toString().isEmpty())
				return fail(2, Text::tr("Every joint mapping needs two nonempty names."));
			o.joints[it.key()] = it.value().toString();
		}
	}
	if (values["--vertices"] != "all")
		for (const auto &part : values["--vertices"].split(','))
		{
			int vertex = -1;
			if (!integer(part, &vertex) || edit.selection.vertices.contains(vertex))
				return fail(2, Text::tr("Vertices must be distinct nonnegative integers or all."));
			edit.selection.vertices.insert(vertex);
		}
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
	ModelToolsCliResult result;
	result.payload = {{"source", QFileInfo(source).absoluteFilePath()},
					  {"output", QFileInfo(values["--output"]).absoluteFilePath()},
					  {"sourceSurface", o.sourceSurface},
					  {"surface", edit.selection.surface},
					  {"method", method},
					  {"mirror", mirror},
					  {"side", side},
					  {"skipUnmatched", o.skipUnmatched},
					  {"dryRun", dry},
					  {"written", !dry}};
	result.lines << Text::tr("Weight transfer reviewed from surface %1 to surface %2.").arg(o.sourceSurface).arg(edit.selection.surface);
	result.payload.insert("outputSource", modelSourceSaveJson(document));
	if (const auto packed = modelSourceSaveNotice(document); !packed.isEmpty())
		result.lines << packed;
	return result;
}
} // namespace vibestudio::cli
