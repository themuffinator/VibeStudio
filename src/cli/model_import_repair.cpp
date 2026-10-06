#include "cli/model_import_repair.h"
#include "core/model_import_repair.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>

namespace vibestudio::cli
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelImportRepairCli)
};
ModelImportRepairCliResult fail(int code, const QString &error)
{
	return {code, error, {}, {}};
}
} // namespace
ModelImportRepairCliResult runModelImportRepair(const QStringList &arguments)
{
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--dry-run"};
	const QSet<QString> options{"--input", "--output", "--settings-file", "--locale", "--catalog-root"};
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
		values.insert(key, value);
	}
	const bool inputOption = seen.contains("--input"), write = seen.contains("--output"), dry = seen.contains("--dry-run");
	if (positional.size() != (inputOption ? 2 : 3) || positional.value(0) != "model" || positional.value(1) != "repair-import")
		return fail(2, Text::tr("Provide one repair input, positionally or with --input."));
	if ((dry && !write) || (write && !values.value("--output").endsWith(".mesh.json", Qt::CaseInsensitive)))
		return fail(2, Text::tr("--dry-run requires --output; repaired copies require a new .mesh.json destination."));
	const auto source = inputOption ? values.value("--input") : positional[2];
	QFile input(source);
	if (!input.open(QIODevice::ReadOnly))
		return fail(1, input.errorString());
	input.close();
	ModelImportRepairPlan plan;
	QString error;
	if (!prepareModelImportRepair(source, &plan, &error))
		return fail(4, error);
	if (write)
	{
		const auto output = values.value("--output");
		const auto target = inspectModelWriteTarget(output);
		if (!target.isValid())
			return fail(1, target.error);
		if (target.existed || modelPathsReferToSameFile(output, source))
			return fail(1, Text::tr("A repaired copy requires a new destination. The input and existing files cannot be replaced."));
		if (dry)
		{
			const auto json = editableModelJson(plan.mesh, &error);
			if (json.isEmpty())
				return fail(4, error);
			if (QJsonDocument(json).toJson(QJsonDocument::Compact).size() > modelDocumentMaxSourceBytes)
				return fail(4, Text::tr("The repaired copy exceeds the 64 MiB source limit."));
		}
		else
		{
			ModelDocument document;
			if (!saveModelImportRepair(plan, output, &document, &error))
				return fail(1, error);
		}
	}
	ModelImportRepairCliResult result;
	result.payload = modelImportRepairJson(plan);
	result.payload.insert("written", write && !dry);
	result.payload.insert("dryRun", dry);
	if (write)
		result.payload.insert("outputPath", QFileInfo(values.value("--output")).absoluteFilePath());
	if (seen.contains("--json") || seen.contains("--quiet"))
		return result;
	result.lines << Text::tr("Faces removed: %1 · Normals rebuilt: %2 · +Z fallbacks: %3 · Seams removed: %4")
						.arg(plan.removedFaces)
						.arg(plan.rebuiltNormals)
						.arg(plan.fallbackNormals)
						.arg(plan.removedSeams);
	result.lines << Text::tr("Face removal affects all poses. Other vertices and authored attributes remain unchanged.");
	for (const auto &change : plan.changes)
		result.lines << Text::tr("Surface %1 · %2 · pose %3 · element %4%5%6")
							.arg(change.surface)
							.arg(modelImportRepairKindId(change.kind))
							.arg(change.frame)
							.arg(change.element)
							.arg(change.other < 0 ? QString{} : QStringLiteral(":%1").arg(change.other))
							.arg(change.fallback ? Text::tr(" · +Z fallback") : QString{});
	result.lines << (write && !dry ? Text::tr("Saved repaired copy to %1.").arg(values.value("--output"))
								   : Text::tr("Repair prepared; no files written."));
	return result;
}
} // namespace vibestudio::cli
