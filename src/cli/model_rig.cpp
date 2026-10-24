#include "cli/model_tools.h"
#include "core/model_document.h"
#include "core/model_skeleton.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>

namespace vibestudio::cli
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelRigCli)
};
ModelToolsCliResult fail(int code, const QString &error)
{
	return {code, error, {}, {}};
}
bool index(const QString &text, int *out)
{
	bool ok = false;
	*out = text.toInt(&ok);
	return ok && *out >= 0 && text == QString::number(*out);
}
} // namespace

ModelToolsCliResult runModelRig(const QStringList &arguments)
{
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--dry-run", "--overwrite"};
	const QSet<QString> options{"--input",	 "--output",   "--operation", "--joint",		 "--name",	 "--parent",	  "--origin",
								"--surface", "--vertices", "--weight",	  "--settings-file", "--locale", "--catalog-root"};
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
	const bool inputOption = seen.contains("--input");
	if (positional.size() != (inputOption ? 2 : 3) || positional.value(0) != "model" || positional.value(1) != "rig")
		return fail(2, Text::tr("Provide one editable model source, positionally "
								"or with --input."));
	const auto source = inputOption ? values.value("--input") : positional[2];
	const auto operation = values.value("--operation", "list");
	const QHash<QString, ModelEditKind> kinds{{"add", ModelEditKind::AddJoint},
											  {"rename", ModelEditKind::RenameJoint},
											  {"reparent", ModelEditKind::ReparentJoint},
											  {"remove", ModelEditKind::DeleteJoint},
											  {"weight", ModelEditKind::SetJointWeight}};
	const bool list = operation == "list", add = operation == "add", weight = operation == "weight";
	if (!list && !kinds.contains(operation))
		return fail(2, Text::tr("Choose list, add, rename, reparent, remove or weight."));
	const QSet<QString> required = list						 ? QSet<QString>{}
								   : add					 ? QSet<QString>{"--name", "--output"}
								   : operation == "rename"	 ? QSet<QString>{"--joint", "--name", "--output"}
								   : operation == "reparent" ? QSet<QString>{"--joint", "--parent", "--output"}
								   : weight					 ? QSet<QString>{"--joint", "--surface", "--vertices", "--weight", "--output"}
															 : QSet<QString>{"--joint", "--output"};
	QSet<QString> allowed = required;
	if (add)
		allowed.unite({"--parent", "--origin"});
	if (list)
		allowed.unite({"--surface", "--vertices"});
	for (const auto &key : {"--joint", "--name", "--parent", "--origin", "--surface", "--vertices", "--weight", "--output"})
		if ((required.contains(key) && !seen.contains(key)) || (!allowed.contains(key) && seen.contains(key)))
			return fail(2, Text::tr("Operation %1 has a missing or inapplicable option: %2.").arg(operation, key));
	if (list && (seen.contains("--dry-run") || seen.contains("--overwrite") || seen.contains("--vertices") != seen.contains("--surface")))
		return fail(2, Text::tr("Listing accepts --surface and --vertices "
								"together, without write flags."));
	if (!list && !values.value("--output").endsWith(".mesh.json", Qt::CaseInsensitive))
		return fail(2, Text::tr("Rig edits require an output ending in .mesh.json."));
	ModelEdit edit;
	edit.rig.joint = values.value("--joint");
	edit.rig.name = values.value("--name");
	edit.rig.parent = values.value("--parent");
	if (edit.rig.parent == "-")
		edit.rig.parent.clear();
	if (seen.contains("--weight"))
	{
		bool ok = false;
		edit.rig.weight = values.value("--weight").toDouble(&ok);
		if (!ok || !std::isfinite(edit.rig.weight) || edit.rig.weight < 0 || edit.rig.weight > 1)
			return fail(2, Text::tr("Weight must be a number from zero to one."));
	}
	if (seen.contains("--origin"))
	{
		const auto parts = values.value("--origin").split(',');
		if (parts.size() != 3)
			return fail(2, Text::tr("Origin requires three comma-separated model coordinates."));
		float *axes[]{&edit.rig.origin.x, &edit.rig.origin.y, &edit.rig.origin.z};
		for (int i = 0; i < 3; ++i)
		{
			bool ok = false;
			*axes[i] = parts[i].toFloat(&ok);
			if (!ok || !std::isfinite(*axes[i]))
				return fail(2, Text::tr("Origin coordinates must be finite numbers."));
		}
	}
	if (seen.contains("--surface") && !index(values.value("--surface"), &edit.selection.surface))
		return fail(2, Text::tr("Surface must be a nonnegative integer."));
	ModelDocument document;
	QString error;
	if (!document.load(source, &error))
		return fail(1, error);
	if (edit.selection.surface >= document.mesh().surfaces.size())
		return fail(4, Text::tr("The requested surface does not exist."));
	if (seen.contains("--vertices"))
	{
		const int count = document.mesh().surfaces[edit.selection.surface].vertexCount;
		if (values.value("--vertices") == "all")
			for (int i = 0; i < count; ++i)
				edit.selection.vertices.insert(i);
		else
			for (const auto &value : values.value("--vertices").split(','))
			{
				int vertex = -1;
				if (!index(value, &vertex) || vertex >= count || edit.selection.vertices.contains(vertex))
					return fail(2, Text::tr("Vertices must be distinct existing indices, or all."));
				edit.selection.vertices.insert(vertex);
			}
	}
	const bool dry = seen.contains("--dry-run");
	if (!list)
	{
		edit.kind = kinds[operation];
		if (!document.edit(edit, &error))
			return fail(4, error);
		const auto target = inspectModelWriteTarget(values.value("--output"));
		if (!target.isValid())
			return fail(1, target.error);
		if (target.existed && !seen.contains("--overwrite"))
			return fail(1, Text::tr("The output already exists; use --overwrite to replace it."));
		if (!document.save(values.value("--output"), seen.contains("--overwrite"), &error, {}, dry))
			return fail(1, error);
	}
	ModelToolsCliResult result;
	QJsonArray joints, vertices;
	const auto &mesh = document.mesh();
	QVector<int> influences(mesh.skeleton.joints.size(), 0);
	for (const auto &surface : mesh.surfaces)
		for (const auto &influence : surface.skinning.influences)
			++influences[influence.joint];
	for (int i = 0; i < mesh.skeleton.joints.size(); ++i)
	{
		const auto &joint = mesh.skeleton.joints[i];
		const auto origin = modelJointTranslation(joint.bind);
		joints.append(QJsonObject{{"index", i},
								  {"name", joint.name},
								  {"parent", joint.parent},
								  {"origin", QJsonArray{origin.x, origin.y, origin.z}},
								  {"influences", influences[i]}});
		result.lines << Text::tr("%1: %2 · parent %3 · %4 influences").arg(i).arg(joint.name).arg(joint.parent).arg(influences[i]);
	}
	auto selected = edit.selection.vertices.values();
	std::sort(selected.begin(), selected.end());
	const auto &skinning = mesh.surfaces[edit.selection.surface].skinning;
	for (int vertex : selected)
	{
		QJsonArray weights;
		if (!skinning.isEmpty())
			for (int i = skinning.first[vertex]; i < skinning.first[vertex] + skinning.count[vertex]; ++i)
			{
				const auto &influence = skinning.influences[i];
				weights.append(QJsonObject{{"joint", mesh.skeleton.joints[influence.joint].name}, {"weight", influence.weight}});
				result.lines << Text::tr("Vertex %1: %2 = %3")
									.arg(vertex)
									.arg(mesh.skeleton.joints[influence.joint].name)
									.arg(influence.weight, 0, 'g', 8);
			}
		vertices.append(QJsonObject{{"index", vertex}, {"weights", weights}});
	}
	result.payload = {{"source", QFileInfo(source).absoluteFilePath()},
					  {"operation", operation},
					  {"joints", joints},
					  {"surface", edit.selection.surface},
					  {"vertices", vertices},
					  {"written", !list && !dry},
					  {"dryRun", dry}};
	if (!list)
	{
		result.payload.insert("outputPath", QFileInfo(values.value("--output")).absoluteFilePath());
		result.lines.prepend(dry ? Text::tr("Rig edit validated; no files written.")
								 : Text::tr("Saved rig edit to %1.").arg(values.value("--output")));
		result.payload.insert("outputSource", modelSourceSaveJson(document));
		if (const auto packed = modelSourceSaveNotice(document); !packed.isEmpty())
			result.lines << packed;
	}
	return result;
}
} // namespace vibestudio::cli
