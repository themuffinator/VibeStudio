#include "cli/model_tools.h"
#include "core/model_document.h"
#include "core/model_skinning_tools.h"
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
	Q_DECLARE_TR_FUNCTIONS(ModelSkinningCli)
};
ModelToolsCliResult fail(int code, const QString &message)
{
	return {code, message, {}, {}};
}
bool integer(const QString &value, int *number)
{
	bool ok = false;
	*number = value.toInt(&ok);
	return ok && *number >= 0 && value == QString::number(*number);
}
QJsonObject summary(const ModelSkinReport &report)
{
	return {{"vertices", int(report.vertices.size())}, {"unbound", report.unbound},			 {"negative", report.negative},
			{"unnormalised", report.unnormalised},	   {"duplicate", report.duplicate},		 {"noncanonical", report.noncanonical},
			{"overLimit", report.overLimit},		   {"hasBindFrame", report.hasBindFrame}};
}
} // namespace
ModelToolsCliResult runModelSkinning(const QStringList &arguments)
{
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--dry-run", "--overwrite", "--replace-weights"};
	const QSet<QString> options{"--input",	"--output",	 "--operation", "--surface",		"--surfaces", "--vertices",
								"--joint",	"--joints",	 "--method",	"--max-influences", "--falloff",  "--threshold",
								"--filter", "--minimum", "--maximum",	"--settings-file",	"--locale",	  "--catalog-root"};
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
	if (positional.size() != (inputOption ? 2 : 3) || positional.value(0) != "model" || positional.value(1) != "skinning")
		return fail(2, Text::tr("Provide one editable model source, positionally or with --input."));
	const auto source = inputOption ? values.value("--input") : positional[2];
	const auto operation = values.value("--operation", "inspect");
	const QHash<QString, ModelEditKind> kinds{{"bind", ModelEditKind::BindSkin},
											  {"rebind", ModelEditKind::RebindSkin},
											  {"unbind", ModelEditKind::UnbindSkin},
											  {"prune", ModelEditKind::PruneSkinWeights}};
	const bool inspect = operation == "inspect", select = operation == "select", bind = operation == "bind", prune = operation == "prune";
	const bool read = inspect || select;
	if (!read && !kinds.contains(operation))
		return fail(2, Text::tr("Choose inspect, select, bind, rebind, unbind or prune."));
	const auto method = values.value("--method", "bones");
	QSet<QString> allowed{"--surface", "--surfaces", "--vertices"};
	if (inspect || select || prune)
		allowed.insert("--max-influences");
	if (select)
		allowed.unite({"--filter", "--joint", "--minimum", "--maximum"});
	if (!read)
		allowed.unite({"--output", "--dry-run", "--overwrite"});
	if (prune)
		allowed.insert("--threshold");
	if (bind)
	{
		allowed.unite({"--method", "--replace-weights"});
		if (method == "rigid")
			allowed.insert("--joint");
		else
			allowed.unite({"--joints", "--max-influences", "--falloff"});
	}
	for (const auto &key :
		 {"--output", "--surface", "--surfaces", "--vertices", "--joint", "--joints", "--method", "--max-influences", "--falloff",
		  "--threshold", "--filter", "--minimum", "--maximum", "--dry-run", "--overwrite", "--replace-weights"})
		if (seen.contains(key) && !allowed.contains(key))
			return fail(2, Text::tr("Operation %1 does not accept %2.").arg(operation, key));
	if ((!read && (!seen.contains("--output") || (!seen.contains("--surface") && !seen.contains("--surfaces")))) ||
		(seen.contains("--surface") && seen.contains("--surfaces")) ||
		(seen.contains("--surfaces") && (select || seen.contains("--vertices"))))
		return fail(2,
					Text::tr("Edits require --output and one surface target. Choose --surface with optional --vertices, or --surfaces."));
	if (!read && !values["--output"].endsWith(".mesh.json", Qt::CaseInsensitive))
		return fail(2, Text::tr("Skinning edits require an output ending in .mesh.json."));
	ModelEdit edit;
	if (!read)
		edit.kind = kinds[operation];
	if (seen.contains("--surface") && !integer(values["--surface"], &edit.selection.surface))
		return fail(2, Text::tr("Surface must be a nonnegative integer."));
	if (seen.contains("--surfaces"))
		for (const auto &value : values["--surfaces"].split(','))
		{
			int index = -1;
			if (!integer(value, &index) || edit.selection.surfaces.contains(index))
				return fail(2, Text::tr("Surface indices must be distinct nonnegative integers."));
			edit.selection.surfaces.insert(index);
		}
	if (seen.contains("--max-influences") &&
		(!integer(values["--max-influences"], &edit.skin.maxInfluences) || edit.skin.maxInfluences < 1 || edit.skin.maxInfluences > 64))
		return fail(2, Text::tr("Maximum influences must be an integer from 1 to 64."));
	const auto number = [&](const QString &key, double *out, double minimum, double maximum)
	{
		if (!seen.contains(key))
			return true;
		bool ok = false;
		*out = values[key].toDouble(&ok);
		return ok && std::isfinite(*out) && *out >= minimum && *out <= maximum;
	};
	double minimum = .001, maximum = 1;
	if (!number("--falloff", &edit.skin.falloff, .1, 16) || !number("--threshold", &edit.skin.minimumWeight, 0, 1) ||
		!number("--minimum", &minimum, 0, 1) || !number("--maximum", &maximum, 0, 1) || minimum > maximum)
		return fail(2, Text::tr("Falloff must be 0.1–16; thresholds and weight ranges must be from zero to one."));
	if (bind)
	{
		if (method != "rigid" && method != "joints" && method != "bones")
			return fail(2, Text::tr("Binding method must be rigid, joints or bones."));
		edit.skin.method = method == "rigid"	? ModelSkinBindingMethod::Rigid
						   : method == "joints" ? ModelSkinBindingMethod::NearestJoints
												: ModelSkinBindingMethod::NearestBones;
		if (method == "rigid")
		{
			if (!seen.contains("--joint"))
				return fail(2, Text::tr("Rigid binding requires --joint."));
			edit.skin.joints = {values["--joint"]};
		}
		else if (seen.contains("--joints"))
			edit.skin.joints = values["--joints"].split(',');
		edit.skin.replaceExisting = seen.contains("--replace-weights");
	}
	const QHash<QString, ModelSkinFilter> filters{
		{"weighted", ModelSkinFilter::Weighted},		 {"unbound", ModelSkinFilter::Unbound},
		{"unnormalised", ModelSkinFilter::Unnormalised}, {"negative", ModelSkinFilter::Negative},
		{"duplicate", ModelSkinFilter::Duplicate},		 {"noncanonical", ModelSkinFilter::Noncanonical},
		{"over-limit", ModelSkinFilter::OverLimit}};
	if (select &&
		(!filters.contains(values["--filter"]) ||
		 (values["--filter"] == "weighted" ? !seen.contains("--joint")
										   : (seen.contains("--joint") || seen.contains("--minimum") || seen.contains("--maximum")))))
		return fail(2,
					Text::tr("Select requires a known --filter. Weighted selection also needs --joint; only it accepts a weight range."));
	ModelDocument document;
	QString error;
	if (!document.load(source, &error))
		return fail(1, error);
	if (edit.selection.surface >= document.mesh().surfaces.size())
		return fail(4, Text::tr("The surface does not exist."));
	if (seen.contains("--vertices") && values["--vertices"] != "all")
		for (const auto &value : values["--vertices"].split(','))
		{
			int vertex = -1;
			if (!integer(value, &vertex) || edit.selection.vertices.contains(vertex) ||
				vertex >= document.mesh().surfaces[edit.selection.surface].vertexCount)
				return fail(2, Text::tr("Vertices must be distinct existing indices, or all."));
			edit.selection.vertices.insert(vertex);
		}
	ModelSkinReport before, after;
	if (!inspectModelSkinning(document.mesh(), edit.selection, edit.skin.maxInfluences, &before, &error))
		return fail(4, error);
	const bool dry = seen.contains("--dry-run");
	if (!read)
	{
		if (!document.edit(edit, &error))
			return fail(4, error);
		const auto target = inspectModelWriteTarget(values["--output"]);
		if (!target.isValid())
			return fail(1, target.error);
		if (target.existed && !seen.contains("--overwrite"))
			return fail(1, Text::tr("The output already exists; use --overwrite to replace it."));
		if (!document.save(values["--output"], seen.contains("--overwrite"), &error, {}, dry))
			return fail(1, error);
	}
	if (!inspectModelSkinning(document.mesh(), edit.selection, edit.skin.maxInfluences, &after, &error))
		return fail(4, error);
	ModelToolsCliResult result;
	result.payload = {{"source", QFileInfo(source).absoluteFilePath()},
					  {"operation", operation},
					  {"before", summary(before)},
					  {"after", summary(after)},
					  {"written", !read && !dry},
					  {"dryRun", dry},
					  {"influenceLimit", edit.skin.maxInfluences}};
	result.lines << Text::tr("Vertices: %1 · unbound: %2 · unnormalised: %3 · over limit: %4 · distinct offsets: %5.")
						.arg(after.vertices.size())
						.arg(after.unbound)
						.arg(after.unnormalised)
						.arg(after.overLimit)
						.arg(after.noncanonical);
	if (inspect)
	{
		QJsonArray vertices;
		for (const auto &vertex : after.vertices)
		{
			QJsonArray weights;
			QStringList descriptions;
			for (const auto &w : vertex.weights)
			{
				const auto name = document.mesh().skeleton.joints[w.joint].name;
				weights.append(QJsonObject{{"joint", name}, {"weight", w.weight}});
				descriptions << QStringLiteral("%1=%2").arg(name).arg(w.weight, 0, 'g', 8);
			}
			vertices.append(QJsonObject{{"surface", vertex.surface},
										{"vertex", vertex.vertex},
										{"sum", vertex.sum},
										{"weights", weights},
										{"unbound", vertex.unbound},
										{"negative", vertex.negative},
										{"unnormalised", vertex.unnormalised},
										{"duplicate", vertex.duplicate},
										{"noncanonical", vertex.noncanonical},
										{"overLimit", vertex.overLimit}});
			result.lines << Text::tr("Surface %1, vertex %2: %3; sum %4.")
								.arg(vertex.surface)
								.arg(vertex.vertex)
								.arg(descriptions.join(", "))
								.arg(vertex.sum, 0, 'g', 8);
		}
		result.payload.insert("vertices", vertices);
	}
	if (select)
	{
		ModelSelection selected;
		if (!selectModelSkinVertices(document.mesh(), after, edit.selection.surface, filters[values["--filter"]], values["--joint"],
									 minimum, maximum, &selected, &error))
			return fail(4, error);
		auto indices = selected.vertices.values();
		std::sort(indices.begin(), indices.end());
		QJsonArray json;
		QStringList text;
		for (int i : indices)
		{
			json.append(i);
			text << QString::number(i);
		}
		result.payload.insert("selectedVertices", json);
		result.payload.insert("surface", selected.surface);
		result.lines << Text::tr("Selected vertex indices: %1.").arg(text.join(','));
	}
	if (!read)
	{
		result.payload.insert("outputPath", QFileInfo(values["--output"]).absoluteFilePath());
		result.lines.prepend(dry ? Text::tr("Skinning edit validated; no files written.")
								 : Text::tr("Saved skinning edit to %1.").arg(values["--output"]));
		result.payload.insert("outputSource", modelSourceSaveJson(document));
		if (const auto packed = modelSourceSaveNotice(document); !packed.isEmpty())
			result.lines << packed;
	}
	return result;
}
} // namespace vibestudio::cli
