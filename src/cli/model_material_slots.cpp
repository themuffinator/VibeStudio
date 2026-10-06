#include "cli/model_material_slots.h"
#include "core/model_document.h"
#include "core/model_material_slots.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

namespace vibestudio::cli
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelMaterialSlotsCli)
};
ModelMaterialSlotsCliResult fail(int code, const QString &error)
{
	return {code, error, {}, {}};
}
bool index(const QString &text, int *value)
{
	bool ok = false;
	*value = text.toInt(&ok);
	return ok && *value >= 0 && text == QString::number(*value);
}
} // namespace
ModelMaterialSlotsCliResult runModelMaterialSlots(const QStringList &arguments)
{
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--dry-run", "--overwrite"};
	const QSet<QString> options{"--input", "--output",	 "--operation",		"--surface", "--slot",
								"--to",	   "--material", "--settings-file", "--locale",	 "--catalog-root"};
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional, materials;
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
		if (seen.contains(key) && key != "--material")
			return fail(2, Text::tr("Repeated option: %1.").arg(key));
		seen.insert(key);
		if (flags.contains(key) && equal < 0)
			continue;
		if (!options.contains(key))
			return fail(2, Text::tr("Unknown or invalid option: %1.").arg(key));
		const auto value = equal < 0 ? arguments.value(++i) : arg.mid(equal + 1);
		if (value.isEmpty() || value.startsWith("--"))
			return fail(2, Text::tr("Option %1 requires a value.").arg(key));
		if (key == "--material")
			materials.append(value);
		else
			values.insert(key, value);
	}
	const bool inputOption = seen.contains("--input");
	if (positional.size() != (inputOption ? 2 : 3) || positional.value(0) != "model" || positional.value(1) != "slots")
		return fail(2, Text::tr("Provide one editable model source, positionally or with --input."));
	const auto source = inputOption ? values.value("--input") : positional[2];
	const auto operation = values.value("--operation", "list");
	const QHash<QString, ModelMaterialSlotAction> actions{
		{"replace", ModelMaterialSlotAction::Replace}, {"set", ModelMaterialSlotAction::Set},	{"insert", ModelMaterialSlotAction::Insert},
		{"remove", ModelMaterialSlotAction::Remove},   {"move", ModelMaterialSlotAction::Move}, {"clear", ModelMaterialSlotAction::Clear}};
	const bool list = operation == "list", move = operation == "move";
	if (!list && !actions.contains(operation))
		return fail(2, Text::tr("Choose list, replace, set, insert, remove, move or clear."));
	const bool indexed = operation == "set" || operation == "insert" || operation == "remove" || move;
	const bool material = operation == "set" || operation == "insert" || operation == "replace";
	const QVector<QPair<QString, bool>> applicable{{"--slot", indexed}, {"--to", move}, {"--material", material}, {"--output", !list}};
	for (const auto &[key, required] : applicable)
		if (seen.contains(key) != required)
			return fail(2,
						Text::tr("Operation %1 %2 %3.").arg(operation, required ? Text::tr("requires") : Text::tr("does not accept"), key));
	if ((!list && !seen.contains("--surface")) || (list && (seen.contains("--dry-run") || seen.contains("--overwrite"))))
		return fail(2, Text::tr("Edits require --surface; listing does not accept write flags."));
	int surface = -1;
	ModelMaterialSlotEdit request;
	if ((seen.contains("--surface") && !index(values.value("--surface"), &surface)) ||
		(indexed && !index(values.value("--slot"), &request.slot)) || (move && !index(values.value("--to"), &request.destination)))
		return fail(2, Text::tr("Surface and slot indices must be nonnegative integers."));
	if (!list && !values.value("--output").endsWith(".mesh.json", Qt::CaseInsensitive))
		return fail(2, Text::tr("Material slot edits require an output ending in .mesh.json."));
	ModelDocument document;
	QString error;
	if (!document.load(source, &error))
		return fail(1, error);
	if (seen.contains("--surface") && surface >= document.mesh().surfaces.size())
		return fail(4, Text::tr("The selected surface does not exist."));
	QStringList before;
	if (!list)
	{
		before = document.mesh().surfaces[surface].skinPaths;
		request.action = actions.value(operation);
		request.materials = materials;
		ModelEdit edit;
		edit.kind = ModelEditKind::SetMaterialSlots;
		edit.selection.surface = surface;
		if (!editModelMaterialSlots(before, request, &edit.materialSlots, &error) || !document.edit(edit, &error))
			return fail(4, error);
	}
	const bool dry = seen.contains("--dry-run");
	if (!list)
	{
		const auto output = values.value("--output");
		const auto target = inspectModelWriteTarget(output);
		if (!target.isValid())
			return fail(1, target.error);
		if (target.existed && !seen.contains("--overwrite"))
			return fail(1, Text::tr("The output already exists; use --overwrite to replace it."));
		if (dry)
		{
			const auto json = editableModelJson(document.mesh(), &error);
			if (json.isEmpty())
				return fail(4, error);
			if (QJsonDocument(json).toJson(QJsonDocument::Compact).size() > modelDocumentMaxSourceBytes)
				return fail(4, Text::tr("The edited model exceeds the 64 MiB source limit."));
		}
		else if (!document.save(output, seen.contains("--overwrite"), &error))
			return fail(1, error);
	}
	ModelMaterialSlotsCliResult result;
	QJsonArray surfaces;
	for (int i = 0; i < document.mesh().surfaces.size(); ++i)
	{
		if (surface >= 0 && i != surface)
			continue;
		const auto &item = document.mesh().surfaces[i];
		surfaces.append(QJsonObject{{"index", i}, {"name", item.name}, {"materials", QJsonArray::fromStringList(item.skinPaths)}});
		result.lines << Text::tr("Surface %1: %2 · %3 material slots").arg(i).arg(item.name).arg(item.skinPaths.size());
		for (int slot = 0; slot < item.skinPaths.size(); ++slot)
			result.lines << QStringLiteral("  %1: %2").arg(slot).arg(item.skinPaths[slot]);
	}
	result.payload = {{"source", QFileInfo(source).absoluteFilePath()},
					  {"operation", operation},
					  {"surfaces", surfaces},
					  {"written", !list && !dry},
					  {"dryRun", dry},
					  {"externalSlotsEditable", supportsModelMaterialSlots(document.mesh())}};
	if (!list)
	{
		result.payload.insert("previousMaterials", QJsonArray::fromStringList(before));
		result.payload.insert("outputPath", QFileInfo(values.value("--output")).absoluteFilePath());
		result.lines.prepend(dry ? Text::tr("Material slots validated; no files written.")
								 : Text::tr("Saved material slots to %1.").arg(values.value("--output")));
	}
	return result;
}
} // namespace vibestudio::cli
