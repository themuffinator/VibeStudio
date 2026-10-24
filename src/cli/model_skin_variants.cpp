#include "cli/model_skin_variants.h"
#include "core/model_document.h"
#include "core/model_lod.h"
#include "core/model_skin_bindings.h"
#include "core/model_skin_variants.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonArray>

namespace vibestudio::cli
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelSkinVariantsCli)
};
ModelSkinVariantsCliResult fail(int code, const QString &message)
{
	return {code, message, {}, {}};
}
struct Arguments
{
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional, repeated;
	QString error;
};
// `repeatable` may be given more than once; its values are kept in order.
Arguments parse(const QStringList &arguments, const QSet<QString> &options, const QString &repeatable)
{
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--dry-run", "--overwrite"};
	const QSet<QString> globals{"--settings-file", "--locale", "--catalog-root"};
	Arguments result;
	for (int i = 1; i < arguments.size(); ++i)
	{
		const auto arg = arguments[i];
		if (!arg.startsWith('-'))
		{
			result.positional.append(arg);
			continue;
		}
		const int equal = arg.indexOf('=');
		const auto key = equal < 0 ? arg : arg.left(equal);
		if (result.seen.contains(key) && key != repeatable)
		{
			result.error = Text::tr("Repeated option: %1.").arg(key);
			return result;
		}
		result.seen.insert(key);
		if (flags.contains(key) && equal < 0)
			continue;
		if (!options.contains(key) && !globals.contains(key))
		{
			result.error = Text::tr("Unexpected option: %1.").arg(arg);
			return result;
		}
		const auto value = equal >= 0 ? arg.mid(equal + 1) : i + 1 < arguments.size() && !arguments[i + 1].startsWith("--") ? arguments[++i] : QString();
		if (value.trimmed().isEmpty())
		{
			result.error = Text::tr("Missing value for %1.").arg(key);
			return result;
		}
		if (key == repeatable)
			result.repeated.append(value);
		else
			result.values.insert(key, value);
	}
	return result;
}
QString sha256(const QByteArray &bytes)
{
	return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
} // namespace

ModelSkinVariantsCliResult runModelSkinVariants(const QStringList &arguments)
{
	const auto args = parse(arguments, {"--input", "--output", "--operation", "--name", "--to", "--surface-material"}, QStringLiteral("--surface-material"));
	if (!args.error.isEmpty())
		return fail(2, args.error);
	if (args.positional.size() != (args.values.contains("--input") ? 2 : 3) || args.positional.value(0) != "model" ||
		args.positional.value(1) != "skin-variants")
		return fail(2, Text::tr("Use model skin-variants <source.mesh.json> with one editable source."));
	const auto operation = args.values.value("--operation", QStringLiteral("list"));
	const QHash<QString, ModelSkinVariantAction> actions{{"add", ModelSkinVariantAction::Add},
														 {"rename", ModelSkinVariantAction::Rename},
														 {"remove", ModelSkinVariantAction::Remove},
														 {"set", ModelSkinVariantAction::Set}};
	if (operation != "list" && !actions.contains(operation))
		return fail(2, Text::tr("--operation accepts list, add, rename, remove or set."));
	const bool list = operation == "list";
	if (list && (args.seen.contains("--output") || args.seen.contains("--name") || args.seen.contains("--to") ||
				 args.seen.contains("--surface-material") || args.seen.contains("--dry-run") || args.seen.contains("--overwrite")))
		return fail(2, Text::tr("list only reads the source; it takes no --output, --name, --to, --surface-material, --dry-run or --overwrite."));
	if (!list && (!args.values.contains("--output") || !args.values.contains("--name")))
		return fail(2, Text::tr("%1 requires --name and --output <variant.mesh.json>.").arg(operation));
	if (args.seen.contains("--to") != (operation == "rename"))
		return fail(2, Text::tr("--to names the new name, and only rename takes it."));
	if (operation == "set" ? args.repeated.isEmpty() : (operation != "add" && !args.repeated.isEmpty()))
		return fail(2, Text::tr("set needs at least one --surface-material SURFACE=MATERIAL; only add and set take them."));
	const auto source = args.values.value("--input", args.positional.value(2)), output = args.values.value("--output");
	if (!list && !output.endsWith(".mesh.json", Qt::CaseInsensitive))
		return fail(2, Text::tr("Use --output with a .mesh.json authoring source, then model build for a native export."));
	QString error;
	ModelDocument document;
	if (!document.load(source, &error))
		return fail(1, error);
	ModelSkinVariantsCliResult result;
	if (!list)
	{
		ModelSkinVariantEdit edit;
		edit.action = actions.value(operation);
		edit.name = args.values.value("--name");
		edit.newName = args.values.value("--to");
		for (const auto &item : args.repeated)
		{
			const int equal = item.indexOf('=');
			if (equal <= 0)
				return fail(2, Text::tr("Use --surface-material SURFACE=MATERIAL; an empty material returns the surface to its primary material."));
			edit.bindings.append({item.left(equal), item.mid(equal + 1)});
		}
		auto mesh = document.mesh();
		if (!editModelSkinVariants(mesh, mesh.skinVariants, edit, &mesh.skinVariants, &error))
			return fail(4, error);
		if (!document.adopt(mesh, document.selection(), document.revisionFingerprint(), &error))
			return fail(4, error);
		const auto target = inspectModelWriteTarget(output);
		if (!target.isValid())
			return fail(1, target.error);
		if (target.existed && !args.seen.contains("--overwrite"))
			return fail(1, Text::tr("The output already exists; use --overwrite to replace it."));
		const bool dry = args.seen.contains("--dry-run");
		if (!document.save(output, args.seen.contains("--overwrite"), &error, {}, dry))
			return fail(1, error);
		result.payload.insert("outputPath", QFileInfo(output).absoluteFilePath());
		result.payload.insert("written", !dry);
		result.payload.insert("dryRun", dry);
		result.lines << (dry ? Text::tr("Skin variants validated; no files written.")
							 : Text::tr("Saved skin variants to %1.").arg(QFileInfo(output).absoluteFilePath()));
	}
	result.payload.insert("source", QFileInfo(source).absoluteFilePath());
	result.payload.insert("operation", operation);
	result.payload.insert("variants", modelSkinVariantsReportJson(document.mesh()));
	result.lines += modelSkinVariantsReportLines(document.mesh());
	return result;
}

ModelSkinVariantsCliResult runModelSkinExport(const QStringList &arguments)
{
	const auto args = parse(arguments, {"--input", "--output", "--variant", "--format", "--decl-name", "--model-path"}, {});
	if (!args.error.isEmpty())
		return fail(2, args.error);
	if (args.positional.size() != (args.values.contains("--input") ? 2 : 3) || args.positional.value(0) != "model" ||
		args.positional.value(1) != "skin-export")
		return fail(2, Text::tr("Use model skin-export <model> with one editable source or native model."));
	const auto format = args.values.value("--format", QStringLiteral("q3skin"));
	if (format != "q3skin" && format != "doom3")
		return fail(2, Text::tr("--format accepts q3skin (a Quake III .skin) or doom3 (a Doom 3 skin declaration)."));
	if (!args.values.contains("--output"))
		return fail(2, Text::tr("Choose the file to write with --output."));
	if (format == "doom3" && !args.values.contains("--variant"))
		return fail(2, Text::tr("A Doom 3 skin maps the primary materials to a variant's; choose it with --variant."));
	if (format == "q3skin" && (args.values.contains("--decl-name") || args.values.contains("--model-path")))
		return fail(2, Text::tr("--decl-name and --model-path apply to --format doom3 only."));
	const auto source = args.values.value("--input", args.positional.value(2)), output = args.values.value("--output");
	if (modelPathsReferToSameFile(source, output))
		return fail(2, Text::tr("The output must be separate from the model."));
	QString error;
	ModelMesh mesh;
	if (!loadModelLodSource(source, &mesh, &error))
		return fail(1, error);
	ModelSkinVariant variant;
	QStringList inherited;
	ModelMesh appearance = mesh;
	if (args.values.contains("--variant"))
	{
		const int index = findModelSkinVariant(mesh.skinVariants, args.values.value("--variant"));
		if (index < 0)
		{
			QStringList names;
			for (const auto &item : std::as_const(mesh.skinVariants))
				names << item.name;
			return fail(4, names.isEmpty() ? Text::tr("The model has no skin variants. Add them with model skin-variants.")
										   : Text::tr("The model has no skin variant named %1. It has: %2.").arg(args.values.value("--variant"), names.join(QStringLiteral(", "))));
		}
		variant = mesh.skinVariants[index];
		if (!applyModelSkinVariant(mesh, variant, &appearance, &error, &inherited))
			return fail(4, error);
	}
	QByteArray bytes;
	ModelSkinVariantsCliResult result;
	if (format == "q3skin")
	{
		if (!writeModelQ3Skin(appearance, &bytes, &error))
			return fail(4, error);
		ModelSkinBindingPlan plan;
		if (!planModelSkinBindings(appearance, bytes, &plan, &error))
			return fail(4, error);
		result.payload.insert("bindings", modelSkinBindingPlanJson(plan));
		result.lines += modelSkinBindingPlanText(plan);
	}
	else
	{
		const auto modelPath = args.values.value("--model-path");
		const auto declName = args.values.value("--decl-name", modelDoom3SkinDeclName(modelPath.isEmpty() ? QFileInfo(source).baseName() : modelPath, variant.name));
		if (!writeModelDoom3Skin(mesh, variant, declName, modelPath, &bytes, &error))
			return fail(4, error);
		QVector<ModelDoom3Skin> skins;
		if (!parseModelDoom3Skins(bytes, &skins, &error) || skins.size() != 1)
			return fail(4, error);
		QJsonArray mappings;
		for (const auto &mapping : std::as_const(skins[0].mappings))
		{
			mappings.append(QJsonObject{{"from", mapping.from}, {"to", mapping.to}});
			result.lines << Text::tr("%1 → %2").arg(mapping.from, mapping.to);
		}
		result.payload.insert("declName", declName);
		result.payload.insert("modelPath", modelPath);
		result.payload.insert("mappings", mappings);
	}
	const auto target = inspectModelWriteTarget(output);
	if (!target.isValid())
		return fail(1, target.error);
	if (target.existed && !args.seen.contains("--overwrite"))
		return fail(1, Text::tr("The output already exists; use --overwrite to replace it."));
	const bool dry = args.seen.contains("--dry-run");
	if (!dry && !writeModelFile(target, bytes, &error))
		return fail(1, error);
	result.payload.insert("source", QFileInfo(source).absoluteFilePath());
	result.payload.insert("outputPath", QFileInfo(output).absoluteFilePath());
	result.payload.insert("format", format);
	result.payload.insert("variant", variant.name);
	result.payload.insert("inheritedSurfaces", QJsonArray::fromStringList(inherited));
	result.payload.insert("bytes", qint64(bytes.size()));
	result.payload.insert("sha256", sha256(bytes));
	result.payload.insert("written", !dry);
	result.payload.insert("dryRun", dry);
	const auto what = variant.name.isEmpty() ? Text::tr("the primary materials") : Text::tr("skin variant %1").arg(variant.name);
	result.lines.prepend(dry ? Text::tr("Checked the skin of %1; no file written.").arg(what)
							 : Text::tr("Wrote the skin of %1 to %2.").arg(what, QFileInfo(output).absoluteFilePath()));
	if (!inherited.isEmpty())
		result.lines << Text::tr("Surfaces keeping their primary material: %1.").arg(inherited.join(QStringLiteral(", ")));
	return result;
}
} // namespace vibestudio::cli
