#include "cli/model_surfaces.h"
#include "core/model_document.h"

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
	Q_DECLARE_TR_FUNCTIONS(ModelSurfacesCli)
};
ModelSurfacesCliResult fail(int code, const QString &message)
{
	return {code, message, {}, {}};
}
bool index(const QString &text, int *result)
{
	bool ok = false;
	*result = text.toInt(&ok);
	return ok && *result >= 0 && text == QString::number(*result);
}
bool indices(const QString &text, QSet<int> *result)
{
	for (const auto &token : text.split(','))
	{
		int value = -1;
		if (!index(token, &value) || result->contains(value))
			return false;
		result->insert(value);
	}
	return !result->isEmpty();
}
} // namespace
ModelSurfacesCliResult runModelSurfaces(const QStringList &arguments)
{
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--dry-run", "--overwrite", "--adopt-target-materials"};
	const QSet<QString> options{"--input", "--output", "--operation",	  "--surface", "--surfaces",	"--target-surface",
								"--name",  "--faces",  "--settings-file", "--locale",  "--catalog-root"};
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
	const bool inputOption = seen.contains("--input");
	if (positional.size() != (inputOption ? 2 : 3) || positional.value(0) != "model" || positional.value(1) != "surfaces")
		return fail(2, Text::tr("Provide one editable model source, positionally or with --input."));
	const auto source = inputOption ? values.value("--input") : positional[2];
	const auto operation = values.value("--operation", "list");
	const QHash<QString, ModelEditKind> operations{
		{"rename", ModelEditKind::RenameSurface},	 {"separate", ModelEditKind::SeparateFaces},
		{"move", ModelEditKind::MoveFacesToSurface}, {"duplicate", ModelEditKind::DuplicateSurface},
		{"delete", ModelEditKind::DeleteSurface},	 {"join", ModelEditKind::JoinSurfaces}};
	if (operation != "list" && !operations.contains(operation))
		return fail(2, Text::tr("Choose list, rename, separate, move, duplicate, delete or join."));
	const bool list = operation == "list", join = operation == "join", move = operation == "move", separate = operation == "separate";
	const bool named = operation == "rename" || operation == "duplicate" || separate;
	const QVector<QPair<QString, bool>> applicable{{"--surface", !list && !join}, {"--surfaces", join}, {"--target-surface", move || join},
												   {"--faces", separate || move}, {"--name", named},	{"--output", !list}};
	for (const auto &[key, required] : applicable)
		if (seen.contains(key) != required)
			return fail(2,
						Text::tr("Operation %1 %2 %3.").arg(operation, required ? Text::tr("requires") : Text::tr("does not accept"), key));
	if ((seen.contains("--adopt-target-materials") && !(move || join)) ||
		(list && (seen.contains("--dry-run") || seen.contains("--overwrite"))))
		return fail(2, Text::tr("Material adoption and write flags do not apply to this operation."));
	ModelEdit edit;
	if (!list)
	{
		edit.kind = operations.value(operation);
		if ((!join && !index(values.value("--surface"), &edit.selection.surface)) ||
			((move || join) && !index(values.value("--target-surface"), &edit.targetSurface)) ||
			(join && !indices(values.value("--surfaces"), &edit.surfaces)))
			return fail(2, Text::tr("Surface indices must be unique nonnegative integers."));
		if ((move || separate) && values.value("--faces") != "all" && !indices(values.value("--faces"), &edit.selection.faces))
			return fail(2, Text::tr("Use --faces all or a comma-separated list of unique nonnegative face indices."));
		edit.text = values.value("--name");
		edit.adoptTargetMaterials = seen.contains("--adopt-target-materials");
		if (join)
			edit.selection.surface = edit.targetSurface;
		if (!values.value("--output").endsWith(".mesh.json", Qt::CaseInsensitive))
			return fail(2, Text::tr("Surface edits require an output ending in .mesh.json."));
	}
	ModelDocument document;
	QString error;
	if (!document.load(source, &error))
		return fail(1, error);
	if (!list && (edit.selection.surface < 0 || edit.selection.surface >= document.mesh().surfaces.size()))
		return fail(4, Text::tr("The source surface does not exist."));
	if ((move || separate) && values.value("--faces") == "all")
		for (int face = 0; face < document.mesh().surfaces[edit.selection.surface].triangles.size(); ++face)
			edit.selection.faces.insert(face);
	if (!list && !document.edit(edit, &error))
		return fail(4, error);
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
	ModelSurfacesCliResult result;
	QJsonArray surfaces;
	for (const auto &surface : document.mesh().surfaces)
	{
		surfaces.append(QJsonObject{{"index", surface.index},
									{"name", surface.name},
									{"vertices", surface.vertexCount},
									{"triangles", qint64(surface.triangles.size())},
									{"seams", qint64(surface.uvSeams.size())},
									{"materials", QJsonArray::fromStringList(surface.skinPaths)}});
		result.lines << Text::tr("%1: %2 · %3 vertices · %4 triangles · Materials: %5")
							.arg(surface.index)
							.arg(surface.name)
							.arg(surface.vertexCount)
							.arg(surface.triangles.size())
							.arg(surface.skinPaths.join(", "));
	}
	result.payload = {{"source", QFileInfo(source).absoluteFilePath()},
					  {"operation", operation},
					  {"surfaces", surfaces},
					  {"written", !list && !dry},
					  {"dryRun", dry},
					  {"frames", qint64(document.mesh().frames.size())}};
	if (!list)
	{
		result.payload.insert("outputPath", QFileInfo(values.value("--output")).absoluteFilePath());
		result.payload.insert("adoptTargetMaterials", edit.adoptTargetMaterials);
		result.payload.insert(
			"selection", QJsonObject{{"surface", document.selection().surface}, {"faceCount", qint64(document.selection().faces.size())}});
		result.lines.prepend(dry ? Text::tr("Surface edit validated; no files written.")
								 : Text::tr("Saved surface edit to %1.").arg(values.value("--output")));
	}
	return result;
}
} // namespace vibestudio::cli
