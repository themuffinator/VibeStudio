#include "cli/model_skin_bindings.h"
#include "core/model_document.h"
#include "core/model_skin_bindings.h"
#include "core/package_draft.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonDocument>
#include <limits>

namespace vibestudio::cli
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelSkinBindingsCli)
};
ModelSkinBindingsCliResult fail(int code, const QString &message)
{
	return {code, message, {}, {}};
}
} // namespace
ModelSkinBindingsCliResult runModelSkinBindings(const QStringList &arguments)
{
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--dry-run", "--overwrite"};
	const QSet<QString> options{"--input",	"--file",		   "--package", "--entry",		 "--entry-index",
								"--output", "--settings-file", "--locale",	"--catalog-root"};
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
		const auto equal = arg.indexOf('=');
		const auto key = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(key))
		{
			return fail(2, Text::tr("Repeated option: %1.").arg(key));
		}
		seen.insert(key);
		if (flags.contains(key) && equal < 0)
		{
			continue;
		}
		if (!options.contains(key))
		{
			return fail(2, Text::tr("Unexpected option: %1.").arg(arg));
		}
		const auto value = equal >= 0														? arg.mid(equal + 1)
						   : i + 1 < arguments.size() && !arguments[i + 1].startsWith("--") ? arguments[++i]
																							: QString();
		if (value.trimmed().isEmpty())
		{
			return fail(2, Text::tr("Missing value for %1.").arg(key));
		}
		values.insert(key, value);
	}
	if (positional.size() != (values.contains("--input") ? 2 : 3) || positional.value(0) != "model" || positional.value(1) != "skin")
	{
		return fail(2, Text::tr("Use model skin <source.mesh.json> with one input source."));
	}
	const bool package = values.contains("--package");
	if (package == values.contains("--file") || (package && !values.contains("--entry") && !values.contains("--entry-index")) ||
		(!package && (values.contains("--entry") || values.contains("--entry-index"))))
	{
		return fail(2, Text::tr("Choose --file <skin> or --package <archive-or-draft> with --entry or --entry-index."));
	}
	const auto source = values.value("--input", positional.value(2)), output = values.value("--output");
	if (!output.endsWith(".mesh.json", Qt::CaseInsensitive))
	{
		return fail(2, Text::tr("Use --output with a .mesh.json authoring source, then model build for a native export."));
	}
	QString error;
	ModelDocument document;
	if (!document.load(source, &error))
	{
		return fail(1, error);
	}
	ModelSkinBindingInput input;
	PackageArchive archive;
	if (package)
	{
		const auto path = values.value("--package");
		if (path.endsWith(".vibepackage", Qt::CaseInsensitive))
		{
			PackageStagingModel staging;
			if (!PackageDraft::load(path, &staging, &error))
			{
				return fail(1, error);
			}
			archive = packagePlannedArchive(staging, &error);
			if (!archive.isOpen())
			{
				return fail(1, error);
			}
		}
		else if (!archive.load(path, &error))
		{
			return fail(1, error);
		}
		if (archive.protectsInputPath(output))
		{
			return fail(2, Text::tr("The output must be separate from the package, draft and their input files."));
		}
		ModelSkinSourceReference reference{values.value("--entry"), -1};
		if (values.contains("--entry-index"))
		{
			bool valid = false;
			const auto text = values.value("--entry-index");
			const auto index = text.toULongLong(&valid);
			for (auto c : text)
			{
				valid &= c >= QLatin1Char('0') && c <= QLatin1Char('9');
			}
			if (!valid || index > quint64(std::numeric_limits<qsizetype>::max()))
			{
				return fail(2, Text::tr("Use a zero-based nonnegative --entry-index."));
			}
			reference.entryIndex = qsizetype(index);
		}
		if (!readModelSkinBindings(archive, reference, &input, &error))
		{
			return fail(4, error);
		}
	}
	else
	{
		if (modelPathsReferToSameFile(values.value("--file"), output))
		{
			return fail(2, Text::tr("The output must be separate from the imported skin file."));
		}
		if (!readModelSkinBindings(values.value("--file"), &input, &error))
		{
			return fail(4, error);
		}
	}
	ModelSkinBindingPlan plan;
	if (!planModelSkinBindings(document.mesh(), input.bytes, &plan, &error))
	{
		return fail(4, error);
	}
	ModelEdit edit;
	edit.kind = ModelEditKind::ApplySkinBindings;
	edit.skinBindings = input.bytes;
	if (!document.edit(edit, &error))
	{
		return fail(4, error);
	}
	const auto target = inspectModelWriteTarget(output);
	if (!target.isValid())
	{
		return fail(1, target.error);
	}
	if (target.existed && !seen.contains("--overwrite"))
	{
		return fail(1, Text::tr("The output already exists; use --overwrite to replace it."));
	}
	const bool dry = seen.contains("--dry-run");
	if (dry)
	{
		const auto json = editableModelJson(document.mesh(), &error);
		if (json.isEmpty())
		{
			return fail(4, error);
		}
		if (QJsonDocument(json).toJson(QJsonDocument::Compact).size() > modelDocumentMaxSourceBytes)
		{
			return fail(4, Text::tr("The assigned model exceeds the 64 MiB source limit."));
		}
	}
	if (!dry && !document.save(output, seen.contains("--overwrite"), &error))
	{
		return fail(1, error);
	}
	ModelSkinBindingsCliResult result;
	result.payload = {
		{"source", QFileInfo(source).absoluteFilePath()},
		{"outputPath", QFileInfo(output).absoluteFilePath()},
		{"written", !dry},
		{"dryRun", dry},
		{"bindings", modelSkinBindingPlanJson(plan)},
		{"skinSource",
		 QJsonObject{{"path", input.path},
					 {"entryIndex", qint64(input.entryIndex)},
					 {"bytes", qint64(input.bytes.size())},
					 {"sha256", QString::fromLatin1(QCryptographicHash::hash(input.bytes, QCryptographicHash::Sha256).toHex())}}}};
	if (package)
	{
		result.payload.insert("packagePath", QFileInfo(values.value("--package")).absoluteFilePath());
	}
	result.lines << (dry ? Text::tr("Skin assignments validated; no files written.")
						 : Text::tr("Saved skin assignments to %1.").arg(QFileInfo(output).absoluteFilePath()));
	result.lines += modelSkinBindingPlanText(plan);
	return result;
}
} // namespace vibestudio::cli
