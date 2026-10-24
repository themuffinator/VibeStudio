#include "cli/model_tools.h"

#include "core/game_installation.h"
#include "core/model_document.h"
#include "core/model_file_io.h"
#include "core/model_lod.h"
#include "core/model_mesh_tools.h"
#include "core/model_selection_tools.h"
#include "core/model_target_prepare.h"
#include "core/model_targets.h"
#include "core/model_vertex_origins.h"
#include "core/package_archive.h"
#include "core/package_draft.h"
#include "core/project_manifest.h"
#include "core/studio_settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
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
	Q_DECLARE_TR_FUNCTIONS(ModelToolsCli)
};
ModelToolsCliResult fail(int code, const QString &message)
{
	return {code, message, {}, {}};
}
struct Arguments
{
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional;
};
// Every option appears at most once; values use separate or --flag=value forms.
bool parse(const QStringList &arguments, const QSet<QString> &flags, const QSet<QString> &options, Arguments *parsed, QString *error)
{
	for (int i = 1; i < arguments.size(); ++i)
	{
		const auto arg = arguments[i];
		if (!arg.startsWith(QLatin1Char('-')) || arg == QStringLiteral("-"))
		{
			parsed->positional.append(arg);
			continue;
		}
		const auto equal = arg.indexOf(QLatin1Char('='));
		const auto key = equal < 0 ? arg : arg.left(equal);
		if (parsed->seen.contains(key))
		{
			*error = Text::tr("Repeated option: %1.").arg(key);
			return false;
		}
		parsed->seen.insert(key);
		if (flags.contains(key) && equal < 0)
			continue;
		if (!options.contains(key))
		{
			*error = Text::tr("Unknown or invalid option: %1.").arg(key);
			return false;
		}
		const auto value = equal < 0 ? arguments.value(++i) : arg.mid(equal + 1);
		// Negative numbers are values, not options.
		bool numeric = false;
		value.left(value.indexOf(QLatin1Char(','))).toDouble(&numeric);
		if (value.isEmpty() || (value.startsWith(QStringLiteral("--")) && !numeric))
		{
			*error = Text::tr("Option %1 requires a value.").arg(key);
			return false;
		}
		parsed->values.insert(key, value);
	}
	return true;
}
bool index(const QString &text, int *result)
{
	bool ok = false;
	*result = text.toInt(&ok);
	return ok && *result >= 0 && text == QString::number(*result);
}
bool indices(const QString &text, QSet<int> *result)
{
	for (const auto &token : text.split(QLatin1Char(',')))
	{
		int value = -1;
		if (!index(token, &value) || result->contains(value))
			return false;
		result->insert(value);
	}
	return !result->isEmpty();
}
bool edgeList(const QString &text, QSet<ModelEdge> *result)
{
	for (const auto &token : text.split(QLatin1Char(',')))
	{
		const auto parts = token.split(QLatin1Char(':'));
		int a = -1, b = -1;
		if (parts.size() != 2 || !index(parts[0], &a) || !index(parts[1], &b) || a == b || result->contains(modelEdge(a, b)))
			return false;
		result->insert(modelEdge(a, b));
	}
	return !result->isEmpty();
}
bool number(const QString &text, double *result)
{
	bool ok = false;
	*result = text.toDouble(&ok);
	return ok && std::isfinite(*result) && std::abs(*result) <= 1000000;
}
bool triple(const QString &text, std::array<double, 3> *result)
{
	const auto parts = text.split(QLatin1Char(','));
	if (parts.size() != 3)
		return false;
	for (int i = 0; i < 3; ++i)
	{
		if (!number(parts[i], &(*result)[i]))
			return false;
	}
	return true;
}
bool vector(const QString &text, ModelVec3 *result)
{
	std::array<double, 3> values{};
	if (!triple(text, &values))
		return false;
	*result = {float(values[0]), float(values[1]), float(values[2])};
	return true;
}
int axisFrom(const QString &text)
{
	const auto lower = text.toLower();
	return lower == QStringLiteral("x") ? 0 : lower == QStringLiteral("y") ? 1 : lower == QStringLiteral("z") ? 2 : -1;
}
QJsonArray sortedJson(const QSet<int> &values)
{
	auto list = values.values();
	std::sort(list.begin(), list.end());
	QJsonArray array;
	for (int value : std::as_const(list))
		array.append(value);
	return array;
}
QJsonArray sortedJson(const QSet<ModelEdge> &values)
{
	auto list = values.values();
	std::sort(list.begin(), list.end());
	QJsonArray array;
	for (auto edge : std::as_const(list))
		array.append(QJsonArray{edge.first, edge.second});
	return array;
}
QString chain(const QSet<int> &values)
{
	auto list = values.values();
	std::sort(list.begin(), list.end());
	QStringList text;
	for (int value : std::as_const(list))
		text << QString::number(value);
	return text.join(QLatin1Char(','));
}
QString chain(const QSet<ModelEdge> &values)
{
	auto list = values.values();
	std::sort(list.begin(), list.end());
	QStringList text;
	for (auto edge : std::as_const(list))
		text << QStringLiteral("%1:%2").arg(edge.first).arg(edge.second);
	return text.join(QLatin1Char(','));
}
// Resolves --faces/--vertices/--edges (comma lists or `all`) for one surface.
bool readSelection(const Arguments &args, const ModelSurface &surface, ModelSelection *selection, QString *error)
{
	if (args.values.contains(QStringLiteral("--faces")))
	{
		const auto text = args.values.value(QStringLiteral("--faces"));
		if (text == QStringLiteral("all"))
		{
			for (int face = 0; face < surface.triangles.size(); ++face)
				selection->faces.insert(face);
		}
		else if (!indices(text, &selection->faces))
		{
			*error = Text::tr("Use --faces all or a comma-separated list of unique face indices.");
			return false;
		}
	}
	if (args.values.contains(QStringLiteral("--vertices")))
	{
		const auto text = args.values.value(QStringLiteral("--vertices"));
		if (text == QStringLiteral("all"))
		{
			for (int v = 0; v < surface.vertexCount; ++v)
				selection->vertices.insert(v);
		}
		else if (!indices(text, &selection->vertices))
		{
			*error = Text::tr("Use --vertices all or a comma-separated list of unique vertex indices.");
			return false;
		}
	}
	if (args.values.contains(QStringLiteral("--edges")))
	{
		const auto text = args.values.value(QStringLiteral("--edges"));
		if (text == QStringLiteral("all"))
		{
			for (auto edge : modelSurfaceEdges(surface))
				selection->edges.insert(edge);
		}
		else if (!edgeList(text, &selection->edges))
		{
			*error = Text::tr("Use --edges all or comma-separated endpoint pairs such as 0:2,2:3.");
			return false;
		}
	}
	for (int face : std::as_const(selection->faces))
	{
		if (face >= surface.triangles.size())
		{
			*error = Text::tr("Face %1 does not exist on this surface.").arg(face);
			return false;
		}
	}
	for (int v : std::as_const(selection->vertices))
	{
		if (v >= surface.vertexCount)
		{
			*error = Text::tr("Vertex %1 does not exist on this surface.").arg(v);
			return false;
		}
	}
	return true;
}
QString sourceArgument(const Arguments &args, const QString &command, QString *error)
{
	const bool inputOption = args.seen.contains(QStringLiteral("--input"));
	if (args.positional.size() != (inputOption ? 2 : 3) || args.positional.value(0) != QStringLiteral("model") ||
		args.positional.value(1) != command)
	{
		*error = Text::tr("Provide one editable model source, positionally or with --input.");
		return {};
	}
	return inputOption ? args.values.value(QStringLiteral("--input")) : args.positional[2];
}
QJsonObject selectionJson(const ModelSelection &selection)
{
	return {{QStringLiteral("surface"), selection.surface},
			{QStringLiteral("faces"), sortedJson(selection.faces)},
			{QStringLiteral("vertices"), sortedJson(selection.vertices)},
			{QStringLiteral("edges"), sortedJson(selection.edges)}};
}
QStringList selectionLines(const ModelSelection &selection)
{
	QStringList lines;
	if (!selection.faces.isEmpty())
		lines << Text::tr("Faces (%1): --faces %2").arg(selection.faces.size()).arg(chain(selection.faces));
	if (!selection.vertices.isEmpty())
		lines << Text::tr("Vertices (%1): --vertices %2").arg(selection.vertices.size()).arg(chain(selection.vertices));
	if (!selection.edges.isEmpty())
		lines << Text::tr("Edges (%1): --edges %2").arg(selection.edges.size()).arg(chain(selection.edges));
	if (lines.isEmpty())
		lines << Text::tr("Nothing selected.");
	return lines;
}
} // namespace

ModelToolsCliResult runModelTool(const QStringList &arguments)
{
	const QSet<QString> flags{QStringLiteral("--cli"),		   QStringLiteral("--json"),	   QStringLiteral("--quiet"),
							  QStringLiteral("--verbose"),	   QStringLiteral("--dry-run"),	   QStringLiteral("--overwrite"),
							  QStringLiteral("--individual"),  QStringLiteral("--no-even"),	   QStringLiteral("--pin-boundary"),
							  QStringLiteral("--connected"),   QStringLiteral("--allow-boundary"), QStringLiteral("--fit"),
							  QStringLiteral("--through"), QStringLiteral("--perspective"), QStringLiteral("--opposite"),
							  QStringLiteral("--duplicate"), QStringLiteral("--no-close"), QStringLiteral("--fill"),
							  QStringLiteral("--keep-joint-names")};
	const QSet<QString> options{
		QStringLiteral("--side-face"),
		QStringLiteral("--input"),			 QStringLiteral("--output"),		 QStringLiteral("--tool"),		   QStringLiteral("--surface"),
		QStringLiteral("--faces"),			 QStringLiteral("--vertices"),		 QStringLiteral("--edges"),		   QStringLiteral("--frame"),
		QStringLiteral("--reference-frame"), QStringLiteral("--merge"),			 QStringLiteral("--point"),		   QStringLiteral("--offset"),
		QStringLiteral("--distance"),		 QStringLiteral("--thickness"),		 QStringLiteral("--depth"),		   QStringLiteral("--factor"),
		QStringLiteral("--iterations"),		 QStringLiteral("--rotate"),		 QStringLiteral("--scale"),		   QStringLiteral("--pivot-mode"),
		QStringLiteral("--pivot"),			 QStringLiteral("--proportional-radius"), QStringLiteral("--falloff"), QStringLiteral("--mirror"),
		QStringLiteral("--angle"),			 QStringLiteral("--plane-point"),	 QStringLiteral("--plane-normal"), QStringLiteral("--keep"),
		QStringLiteral("--axis"),			 QStringLiteral("--direction"),		 QStringLiteral("--threshold"),	   QStringLiteral("--cuts"),
		QStringLiteral("--slide"),			 QStringLiteral("--primitive"),		 QStringLiteral("--at"),		   QStringLiteral("--size"),
		QStringLiteral("--segments"),		 QStringLiteral("--rings"),			 QStringLiteral("--up"),		   QStringLiteral("--ratio"),
		QStringLiteral("--target-triangles"), QStringLiteral("--width"), QStringLiteral("--roundness"), QStringLiteral("--tile-size"), QStringLiteral("--u-axis"),
		QStringLiteral("--v-axis"), QStringLiteral("--settings-file"), QStringLiteral("--locale"), QStringLiteral("--catalog-root"),
		QStringLiteral("--path"), QStringLiteral("--path-file"), QStringLiteral("--view"), QStringLiteral("--camera-origin"),
		QStringLiteral("--camera-right"), QStringLiteral("--camera-up"), QStringLiteral("--camera-forward"),
		QStringLiteral("--steps"), QStringLiteral("--centre"), QStringLiteral("--axis-vector"), QStringLiteral("--joint-map"),
		QStringLiteral("--bind"), QStringLiteral("--bind-joint"), QStringLiteral("--new-surface")};
	Arguments args;
	QString error;
	if (!parse(arguments, flags, options, &args, &error))
		return fail(2, error);
	const auto source = sourceArgument(args, QStringLiteral("tool"), &error);
	if (source.isEmpty())
		return fail(2, error);
	const QHash<QString, ModelEditKind> tools{
		{QStringLiteral("spin"), ModelEditKind::Spin},
		{QStringLiteral("rip"), ModelEditKind::Rip},
		{QStringLiteral("knife"), ModelEditKind::Knife},
		{QStringLiteral("bevel-edges"), ModelEditKind::BevelEdges},
		{QStringLiteral("edge-slide"), ModelEditKind::SlideEdges},
		{QStringLiteral("rotate-edges"), ModelEditKind::RotateEdges},		{QStringLiteral("merge"), ModelEditKind::MergeVertices},
		{QStringLiteral("dissolve-vertices"), ModelEditKind::DissolveVertices}, {QStringLiteral("dissolve-faces"), ModelEditKind::DissolveFaces},
		{QStringLiteral("poke"), ModelEditKind::PokeFaces},					{QStringLiteral("beautify"), ModelEditKind::BeautifyFaces},
		{QStringLiteral("make-face"), ModelEditKind::MakeFace},				{QStringLiteral("extrude-edges"), ModelEditKind::ExtrudeEdges},
		{QStringLiteral("inset"), ModelEditKind::InsetFaces},				{QStringLiteral("shrink-fatten"), ModelEditKind::ShrinkFatten},
		{QStringLiteral("smooth"), ModelEditKind::SmoothVertices},			{QStringLiteral("transform"), ModelEditKind::WeightedTransform},
		{QStringLiteral("shade-flat"), ModelEditKind::ShadeFlat},			{QStringLiteral("shade-smooth"), ModelEditKind::ShadeSmooth},
		{QStringLiteral("auto-smooth"), ModelEditKind::ShadeAutoSmooth},	{QStringLiteral("bisect"), ModelEditKind::Bisect},
		{QStringLiteral("symmetrize"), ModelEditKind::Symmetrize},			{QStringLiteral("loop-cut"), ModelEditKind::LoopCut},
		{QStringLiteral("add"), ModelEditKind::AddPrimitive},				{QStringLiteral("decimate"), ModelEditKind::Decimate},
		{QStringLiteral("bevel-vertices"), ModelEditKind::BevelVertices},	{QStringLiteral("solidify"), ModelEditKind::Solidify},
		{QStringLiteral("uv-cube"), ModelEditKind::ProjectUvMapping},		{QStringLiteral("uv-view"), ModelEditKind::ProjectUvMapping},
		{QStringLiteral("uv-cylinder"), ModelEditKind::ProjectUvMapping},	{QStringLiteral("uv-sphere"), ModelEditKind::ProjectUvMapping}};
	const auto toolName = args.values.value(QStringLiteral("--tool"));
	if (!tools.contains(toolName))
	{
		auto names = tools.keys();
		std::sort(names.begin(), names.end());
		return fail(2, Text::tr("Choose --tool %1.").arg(names.join(QLatin1Char('|'))));
	}
	const auto kind = tools.value(toolName);
	// Options each tool accepts beyond the shared source/output/surface ones.
	const QHash<QString, QStringList> accepted{
		{QStringLiteral("rip"), {QStringLiteral("--vertices"), QStringLiteral("--edges"), QStringLiteral("--side-face"),
			QStringLiteral("--offset"), QStringLiteral("--opposite"), QStringLiteral("--fill"), QStringLiteral("--tile-size"), QStringLiteral("--angle")}},
		{QStringLiteral("knife"), {QStringLiteral("--faces"), QStringLiteral("--path"), QStringLiteral("--path-file"), QStringLiteral("--view"),
			QStringLiteral("--through"), QStringLiteral("--perspective"), QStringLiteral("--opposite"), QStringLiteral("--camera-origin"),
			QStringLiteral("--camera-right"), QStringLiteral("--camera-up"), QStringLiteral("--camera-forward")}},
		{QStringLiteral("bevel-edges"), {QStringLiteral("--edges"), QStringLiteral("--width"), QStringLiteral("--segments"), QStringLiteral("--roundness"), QStringLiteral("--tile-size"), QStringLiteral("--angle")}},
		{QStringLiteral("edge-slide"), {QStringLiteral("--edges"), QStringLiteral("--slide"), QStringLiteral("--distance"), QStringLiteral("--angle"), QStringLiteral("--frame")}},
		{QStringLiteral("rotate-edges"), {QStringLiteral("--edges")}},
		{QStringLiteral("merge"), {QStringLiteral("--faces"), QStringLiteral("--vertices"), QStringLiteral("--edges"), QStringLiteral("--merge"), QStringLiteral("--point")}},
		{QStringLiteral("dissolve-vertices"), {QStringLiteral("--vertices"), QStringLiteral("--edges"), QStringLiteral("--faces")}},
		{QStringLiteral("dissolve-faces"), {QStringLiteral("--faces")}},
		{QStringLiteral("poke"), {QStringLiteral("--faces"), QStringLiteral("--distance")}},
		{QStringLiteral("beautify"), {QStringLiteral("--faces"), QStringLiteral("--angle")}},
		{QStringLiteral("make-face"), {QStringLiteral("--vertices"), QStringLiteral("--edges")}},
		{QStringLiteral("extrude-edges"), {QStringLiteral("--edges"), QStringLiteral("--offset")}},
		{QStringLiteral("spin"), {QStringLiteral("--faces"), QStringLiteral("--edges"), QStringLiteral("--angle"), QStringLiteral("--steps"),
			QStringLiteral("--centre"), QStringLiteral("--axis"), QStringLiteral("--axis-vector"), QStringLiteral("--duplicate"),
			QStringLiteral("--no-close"), QStringLiteral("--tile-size")}},
		{QStringLiteral("inset"), {QStringLiteral("--faces"), QStringLiteral("--thickness"), QStringLiteral("--depth"), QStringLiteral("--individual"), QStringLiteral("--no-even")}},
		{QStringLiteral("shrink-fatten"), {QStringLiteral("--faces"), QStringLiteral("--vertices"), QStringLiteral("--edges"), QStringLiteral("--distance"), QStringLiteral("--no-even"), QStringLiteral("--frame")}},
		{QStringLiteral("smooth"), {QStringLiteral("--faces"), QStringLiteral("--vertices"), QStringLiteral("--edges"), QStringLiteral("--factor"), QStringLiteral("--iterations"), QStringLiteral("--pin-boundary"), QStringLiteral("--frame")}},
		{QStringLiteral("transform"), {QStringLiteral("--faces"), QStringLiteral("--vertices"), QStringLiteral("--edges"), QStringLiteral("--offset"), QStringLiteral("--rotate"), QStringLiteral("--scale"),
									   QStringLiteral("--pivot-mode"), QStringLiteral("--pivot"), QStringLiteral("--proportional-radius"), QStringLiteral("--falloff"), QStringLiteral("--connected"),
									   QStringLiteral("--mirror"), QStringLiteral("--threshold"), QStringLiteral("--frame")}},
		{QStringLiteral("shade-flat"), {QStringLiteral("--faces")}},
		{QStringLiteral("shade-smooth"), {QStringLiteral("--faces")}},
		{QStringLiteral("auto-smooth"), {QStringLiteral("--faces"), QStringLiteral("--angle")}},
		{QStringLiteral("bisect"), {QStringLiteral("--faces"), QStringLiteral("--plane-point"), QStringLiteral("--plane-normal"), QStringLiteral("--keep")}},
		{QStringLiteral("symmetrize"), {QStringLiteral("--faces"), QStringLiteral("--axis"), QStringLiteral("--direction"), QStringLiteral("--threshold"),
			QStringLiteral("--joint-map"), QStringLiteral("--keep-joint-names")}},
		{QStringLiteral("loop-cut"), {QStringLiteral("--edges"), QStringLiteral("--cuts"), QStringLiteral("--slide"), QStringLiteral("--angle")}},
		{QStringLiteral("add"), {QStringLiteral("--primitive"), QStringLiteral("--at"), QStringLiteral("--size"), QStringLiteral("--segments"), QStringLiteral("--rings"), QStringLiteral("--up"),
			QStringLiteral("--bind"), QStringLiteral("--bind-joint"), QStringLiteral("--new-surface")}},
		{QStringLiteral("decimate"), {QStringLiteral("--faces"), QStringLiteral("--ratio"), QStringLiteral("--target-triangles"), QStringLiteral("--allow-boundary")}},
		{QStringLiteral("bevel-vertices"), {QStringLiteral("--vertices"), QStringLiteral("--edges"), QStringLiteral("--faces"), QStringLiteral("--width")}},
		{QStringLiteral("solidify"), {QStringLiteral("--faces"), QStringLiteral("--thickness"), QStringLiteral("--no-even")}},
		{QStringLiteral("uv-cube"), {QStringLiteral("--faces"), QStringLiteral("--tile-size"), QStringLiteral("--fit")}},
		{QStringLiteral("uv-view"), {QStringLiteral("--faces"), QStringLiteral("--tile-size"), QStringLiteral("--fit"), QStringLiteral("--u-axis"), QStringLiteral("--v-axis")}},
		{QStringLiteral("uv-cylinder"), {QStringLiteral("--faces"), QStringLiteral("--fit"), QStringLiteral("--axis")}},
		{QStringLiteral("uv-sphere"), {QStringLiteral("--faces"), QStringLiteral("--fit"), QStringLiteral("--axis")}}};
	const QSet<QString> shared{QStringLiteral("--cli"),	  QStringLiteral("--json"),			  QStringLiteral("--quiet"),
							   QStringLiteral("--verbose"), QStringLiteral("--dry-run"),		  QStringLiteral("--overwrite"),
							   QStringLiteral("--input"),   QStringLiteral("--output"),		  QStringLiteral("--tool"),
							   QStringLiteral("--surface"), QStringLiteral("--reference-frame"), QStringLiteral("--settings-file"),
							   QStringLiteral("--locale"),  QStringLiteral("--catalog-root")};
	const auto allowed = accepted.value(toolName);
	for (const auto &key : std::as_const(args.seen))
	{
		if (!shared.contains(key) && !allowed.contains(key))
			return fail(2, Text::tr("--tool %1 does not accept %2.").arg(toolName, key));
	}
	const auto output = args.values.value(QStringLiteral("--output"));
	if (!output.endsWith(QStringLiteral(".mesh.json"), Qt::CaseInsensitive))
		return fail(2, Text::tr("Mesh tools require --output <new.mesh.json>."));
	ModelEdit edit;
	edit.kind = kind;
	auto &tool = edit.tool;
	int surfaceIndex = 0, referenceFrame = 0;
	if ((args.values.contains(QStringLiteral("--surface")) && !index(args.values.value(QStringLiteral("--surface")), &surfaceIndex)) ||
		(args.values.contains(QStringLiteral("--reference-frame")) &&
		 !index(args.values.value(QStringLiteral("--reference-frame")), &referenceFrame)))
		return fail(2, Text::tr("--surface and --reference-frame take nonnegative indices."));
	tool.referenceFrame = referenceFrame;
	if (kind == ModelEditKind::SlideEdges)
	{
		if (args.values.contains(QStringLiteral("--slide")) == args.values.contains(QStringLiteral("--distance")))
			return fail(2, Text::tr("Edge slide requires exactly one of --slide fraction or --distance units."));
		tool.slideByDistance = args.values.contains(QStringLiteral("--distance"));
	}
	edit.pivotFrame = referenceFrame;
	if (kind == ModelEditKind::Knife)
	{
		auto &knife = tool.knife;
		knife.through = args.seen.contains(QStringLiteral("--through"));
		knife.perspective = args.seen.contains(QStringLiteral("--perspective"));
		knife.selectedOnly = args.seen.contains(QStringLiteral("--faces"));
		if (args.seen.contains(QStringLiteral("--path")) == args.seen.contains(QStringLiteral("--path-file")))
			return fail(2, Text::tr("Knife requires exactly one of --path or --path-file."));
		const auto view = args.values.value(QStringLiteral("--view"), QStringLiteral("xy"));
		if (view == QStringLiteral("xz")) { knife.up = {0, 0, 1}; knife.forward = {0, 1, 0}; }
		else if (view == QStringLiteral("yz")) { knife.right = {0, 1, 0}; knife.up = {0, 0, 1}; knife.forward = {-1, 0, 0}; }
		else if (view != QStringLiteral("xy")) return fail(2, Text::tr("Knife views are xy, xz or yz."));
		const int axes = int(args.seen.contains(QStringLiteral("--camera-right"))) + int(args.seen.contains(QStringLiteral("--camera-up"))) + int(args.seen.contains(QStringLiteral("--camera-forward")));
		if (axes && (axes != 3 || args.seen.contains(QStringLiteral("--view")) || args.seen.contains(QStringLiteral("--opposite"))))
			return fail(2, Text::tr("Supply all three camera axes together, without --view or --opposite."));
		if ((args.seen.contains(QStringLiteral("--camera-origin")) && !triple(args.values.value(QStringLiteral("--camera-origin")), &knife.origin)) ||
			(axes && (!triple(args.values.value(QStringLiteral("--camera-right")), &knife.right) ||
				!triple(args.values.value(QStringLiteral("--camera-up")), &knife.up) || !triple(args.values.value(QStringLiteral("--camera-forward")), &knife.forward))))
			return fail(2, Text::tr("Knife camera values use three comma-separated finite numbers."));
		if (knife.perspective && !args.seen.contains(QStringLiteral("--camera-origin")))
			return fail(2, Text::tr("Perspective knife projection requires --camera-origin x,y,z."));
		if (args.seen.contains(QStringLiteral("--opposite"))) for (double &v : knife.forward) v = -v;
		QVector<QVector<std::array<double, 2>>> paths;
		if (args.seen.contains(QStringLiteral("--path-file")))
		{
			if (modelPathsReferToSameFile(output, args.values.value(QStringLiteral("--path-file"))))
				return fail(1, Text::tr("The output cannot replace the knife path input, including aliases of the same file."));
			QFile file(args.values.value(QStringLiteral("--path-file")));
			if (!file.open(QIODevice::ReadOnly) || file.size() > 65536)
				return fail(2, Text::tr("Open a knife path JSON file no larger than 64 KiB."));
			QJsonParseError problem;
			const auto data = file.read(65537);
			if (data.size() > 65536 || file.error() != QFileDevice::NoError)
				return fail(2, Text::tr("Open a knife path JSON file no larger than 64 KiB."));
			const auto json = QJsonDocument::fromJson(data, &problem);
			if (problem.error != QJsonParseError::NoError || !json.isArray())
				return fail(2, Text::tr("Knife path JSON is an array of paths, each containing [u,v] point arrays."));
			for (const auto &path : json.array())
			{
				if (!path.isArray()) return fail(2, Text::tr("Each knife path must be a point array."));
				QVector<std::array<double, 2>> points;
				for (const auto &point : path.toArray())
				{
					const auto coordinates = point.toArray();
					if (!point.isArray() || coordinates.size() != 2 || !coordinates[0].isDouble() || !coordinates[1].isDouble())
						return fail(2, Text::tr("Each knife point needs exactly two numeric coordinates."));
					points.append(std::array<double, 2>{coordinates[0].toDouble(), coordinates[1].toDouble()});
				}
				paths.append(points);
			}
		}
		else
		{
			const auto text = args.values.value(QStringLiteral("--path"));
			if (text.toUtf8().size() > 65536) return fail(2, Text::tr("The knife path exceeds 64 KiB."));
			for (const auto &path : text.split(QLatin1Char('|')))
			{
				QVector<std::array<double, 2>> points;
				for (const auto &point : path.split(QLatin1Char(';')))
				{
					const auto values = point.split(QLatin1Char(','));
					std::array<double, 2> coordinates;
					bool first = false, second = false;
					if (values.size() == 2)
					{
						coordinates[0] = values[0].toDouble(&first);
						coordinates[1] = values[1].toDouble(&second);
					}
					if (!first || !second)
						return fail(2, Text::tr("Use --path 'u,v;u,v;…'; separate disconnected paths with |."));
					points.append(coordinates);
				}
				paths.append(points);
			}
		}
		for (const auto &path : paths)
		{
			if (path.size() < 2) return fail(2, Text::tr("Each knife path needs at least two points."));
			for (int i = 1; i < path.size(); ++i) knife.segments.append(ModelKnifeSegment{path[i - 1], path[i]});
		}
		if (knife.segments.isEmpty() || knife.segments.size() > 256) return fail(2, Text::tr("Use between 1 and 256 knife segments."));
	}
	if (args.values.contains(QStringLiteral("--frame")))
	{
		const auto text = args.values.value(QStringLiteral("--frame"));
		if (text != QStringLiteral("all") && !index(text, &edit.frame))
			return fail(2, Text::tr("Use --frame all or one frame index."));
		if (edit.frame >= 0)
			edit.pivotFrame = edit.frame;
	}
	const auto readNumber = [&](const QString &key, double *target, double low, double high)
	{
		if (!args.values.contains(key))
			return true;
		double value = 0;
		if (!number(args.values.value(key), &value) || value < low || value > high)
			return false;
		*target = value;
		return true;
	};
	const auto readInt = [&](const QString &key, int *target, int low, int high)
	{
		if (!args.values.contains(key))
			return true;
		int value = 0;
		if (!index(args.values.value(key), &value) || value < low || value > high)
			return false;
		*target = value;
		return true;
	};
	if (!readNumber(QStringLiteral("--thickness"), toolName == QStringLiteral("solidify") ? &tool.solidifyThickness : &tool.insetThickness,
					 toolName == QStringLiteral("solidify") ? -1000000 : 0, 1000000) ||
		!readNumber(QStringLiteral("--width"), &tool.bevelWidth, kind == ModelEditKind::BevelEdges ? 0 : 0.000001, 1000000) ||
		!readNumber(QStringLiteral("--roundness"), &tool.bevelRoundness, 0, 1) ||
		!readNumber(QStringLiteral("--tile-size"), &tool.uvTileSize, 0.001, 1000000) ||
		!readNumber(QStringLiteral("--depth"), &tool.insetDepth, -1000000, 1000000) ||
		!readNumber(QStringLiteral("--distance"), &tool.offset, -1000000, 1000000) ||
		!readNumber(QStringLiteral("--factor"), &tool.smoothFactor, 0, 1) || !readInt(QStringLiteral("--iterations"), &tool.iterations, 1, 100) ||
		!readNumber(QStringLiteral("--proportional-radius"), &tool.proportionalRadius, 0, 1000000) ||
		!readNumber(QStringLiteral("--angle"), kind == ModelEditKind::Spin ? &tool.spinAngle : toolName == QStringLiteral("auto-smooth") ? &tool.smoothAngle : &tool.maxAngle,
			kind == ModelEditKind::Spin ? -360 : 0, kind == ModelEditKind::Spin ? 360 : 180) ||
		!readInt(QStringLiteral("--steps"), &tool.spinSteps, 1, 256) ||
		!readInt(QStringLiteral("--side-face"), &tool.ripFace, 0, 131071) ||
		!readNumber(QStringLiteral("--threshold"), &tool.mergeThreshold, 0, 1000) || !readInt(QStringLiteral("--cuts"), &tool.cuts, 1, 64) ||
		!readNumber(QStringLiteral("--slide"), &tool.slide, -1, 1) ||
		!readInt(QStringLiteral("--segments"), kind == ModelEditKind::BevelEdges ? &tool.bevelSegments : &tool.segments,
				 kind == ModelEditKind::BevelEdges ? 1 : 3, kind == ModelEditKind::BevelEdges ? 32 : 256) ||
		!readInt(QStringLiteral("--rings"), &tool.rings, 1, 256) || !readNumber(QStringLiteral("--ratio"), &tool.ratio, 0, 1) ||
		!readInt(QStringLiteral("--target-triangles"), &tool.targetTriangles, 1, 1000000))
		return fail(2, Text::tr("A numeric tool option is malformed or outside its range. See model tool in the CLI reference."));
	tool.evenThickness = !args.seen.contains(QStringLiteral("--no-even"));
	tool.pinBoundary = args.seen.contains(QStringLiteral("--pin-boundary"));
	tool.connectedOnly = args.seen.contains(QStringLiteral("--connected"));
	tool.preserveBoundary = !args.seen.contains(QStringLiteral("--allow-boundary"));
	tool.inset = args.seen.contains(QStringLiteral("--individual")) ? ModelInsetMode::Individual : ModelInsetMode::Region;
	tool.uvFit = args.seen.contains(QStringLiteral("--fit"));
	tool.spinDuplicate = args.seen.contains(QStringLiteral("--duplicate"));
	tool.spinClose = !args.seen.contains(QStringLiteral("--no-close"));
	tool.ripFill = args.seen.contains(QStringLiteral("--fill"));
	tool.ripOpposite = kind == ModelEditKind::Rip && args.seen.contains(QStringLiteral("--opposite"));
	tool.keepJointNames = args.seen.contains(QStringLiteral("--keep-joint-names"));
	const bool detectJoints = args.values.value(QStringLiteral("--joint-map")) == QStringLiteral("detect");
	if (args.values.contains(QStringLiteral("--joint-map")) && !detectJoints &&
		!parseModelMirrorJointMap(args.values.value(QStringLiteral("--joint-map")), &tool.mirrorJoints, &error))
		return fail(2, Text::tr("Use --joint-map detect or --joint-map left=right,... %1").arg(error));
	tool.uvProjection = toolName == QStringLiteral("uv-view")		 ? ModelUvProjection::View
						: toolName == QStringLiteral("uv-cylinder") ? ModelUvProjection::Cylinder
						: toolName == QStringLiteral("uv-sphere")	 ? ModelUvProjection::Sphere
																	 : ModelUvProjection::Cube;
	if (args.values.contains(QStringLiteral("--merge")))
	{
		const auto text = args.values.value(QStringLiteral("--merge"));
		if (text == QStringLiteral("centre") || text == QStringLiteral("center"))
			tool.merge = ModelMergeMode::Centre;
		else if (text == QStringLiteral("point"))
			tool.merge = ModelMergeMode::Point;
		else if (text == QStringLiteral("collapse"))
			tool.merge = ModelMergeMode::Collapse;
		else
			return fail(2, Text::tr("Use --merge centre|point|collapse."));
	}
	if ((args.values.contains(QStringLiteral("--centre")) && !triple(args.values.value(QStringLiteral("--centre")), &tool.point)) ||
		(args.values.contains(QStringLiteral("--axis-vector")) && !triple(args.values.value(QStringLiteral("--axis-vector")), &tool.normal)) ||
		(args.values.contains(QStringLiteral("--point")) && !triple(args.values.value(QStringLiteral("--point")), &tool.point)) ||
		(args.values.contains(QStringLiteral("--at")) && !triple(args.values.value(QStringLiteral("--at")), &tool.point)) ||
		(args.values.contains(QStringLiteral("--plane-point")) && !triple(args.values.value(QStringLiteral("--plane-point")), &tool.point)) ||
		(args.values.contains(QStringLiteral("--plane-normal")) && !triple(args.values.value(QStringLiteral("--plane-normal")), &tool.normal)) ||
		(args.values.contains(QStringLiteral("--size")) && !triple(args.values.value(QStringLiteral("--size")), &tool.size)) ||
		(args.values.contains(QStringLiteral("--offset")) && !vector(args.values.value(QStringLiteral("--offset")), &edit.translation)) ||
		(args.values.contains(QStringLiteral("--rotate")) && !vector(args.values.value(QStringLiteral("--rotate")), &edit.rotation)) ||
		(args.values.contains(QStringLiteral("--scale")) && !vector(args.values.value(QStringLiteral("--scale")), &edit.scale)) ||
		(args.values.contains(QStringLiteral("--pivot")) && !vector(args.values.value(QStringLiteral("--pivot")), &edit.pivot)) ||
		(args.values.contains(QStringLiteral("--u-axis")) && !triple(args.values.value(QStringLiteral("--u-axis")), &tool.uvAxisU)) ||
		(args.values.contains(QStringLiteral("--v-axis")) && !triple(args.values.value(QStringLiteral("--v-axis")), &tool.uvAxisV)))
		return fail(2, Text::tr("Coordinates use three comma-separated finite numbers, such as 0,0,16."));
	if (toolName == QStringLiteral("merge") && tool.merge == ModelMergeMode::Point && !args.values.contains(QStringLiteral("--point")))
		return fail(2, Text::tr("--merge point requires --point x,y,z."));
	if (args.values.contains(QStringLiteral("--axis")) || args.values.contains(QStringLiteral("--up")))
	{
		tool.axis = axisFrom(args.values.value(args.values.contains(QStringLiteral("--axis")) ? QStringLiteral("--axis") : QStringLiteral("--up")));
		if (tool.axis < 0)
			return fail(2, Text::tr("Axes are x, y or z."));
	}
	else if (toolName == QStringLiteral("add") || toolName == QStringLiteral("uv-cylinder") || toolName == QStringLiteral("uv-sphere"))
		tool.axis = 2;
	if (kind == ModelEditKind::Spin && args.values.contains(QStringLiteral("--axis")))
	{
		if (args.values.contains(QStringLiteral("--axis-vector")))
			return fail(2, Text::tr("Spin accepts either --axis or --axis-vector, not both."));
		tool.normal = {0, 0, 0}; tool.normal[tool.axis] = 1;
	}
	if (args.values.contains(QStringLiteral("--direction")))
	{
		const auto text = args.values.value(QStringLiteral("--direction"));
		if (text != QStringLiteral("positive") && text != QStringLiteral("negative"))
			return fail(2, Text::tr("Use --direction positive (copy +axis onto -axis) or negative."));
		tool.positiveToNegative = text == QStringLiteral("positive");
	}
	if (args.values.contains(QStringLiteral("--keep")))
	{
		const auto text = args.values.value(QStringLiteral("--keep"));
		if (text == QStringLiteral("both"))
			tool.keep = ModelBisectKeep::Both;
		else if (text == QStringLiteral("front"))
			tool.keep = ModelBisectKeep::Front;
		else if (text == QStringLiteral("back"))
			tool.keep = ModelBisectKeep::Back;
		else
			return fail(2, Text::tr("Use --keep both|front|back."));
	}
	if (args.values.contains(QStringLiteral("--falloff")))
	{
		const QHash<QString, ModelFalloff> falloffs{{QStringLiteral("smooth"), ModelFalloff::Smooth},
													{QStringLiteral("sphere"), ModelFalloff::Sphere},
													{QStringLiteral("root"), ModelFalloff::Root},
													{QStringLiteral("inverse-square"), ModelFalloff::InverseSquare},
													{QStringLiteral("sharp"), ModelFalloff::Sharp},
													{QStringLiteral("linear"), ModelFalloff::Linear},
													{QStringLiteral("constant"), ModelFalloff::Constant}};
		const auto text = args.values.value(QStringLiteral("--falloff"));
		if (!falloffs.contains(text))
			return fail(2, Text::tr("Use --falloff smooth|sphere|root|inverse-square|sharp|linear|constant."));
		tool.falloff = falloffs.value(text);
	}
	if (args.values.contains(QStringLiteral("--mirror")))
	{
		tool.mirrorAxes = 0;
		for (const QChar axis : args.values.value(QStringLiteral("--mirror")).toLower())
		{
			const int value = axisFrom(QString(axis));
			if (value < 0 || (tool.mirrorAxes & (1 << value)))
				return fail(2, Text::tr("Use --mirror with axes x, y and/or z, such as x or xy."));
			tool.mirrorAxes |= 1 << value;
		}
	}
	if (args.values.contains(QStringLiteral("--pivot-mode")))
	{
		const auto text = args.values.value(QStringLiteral("--pivot-mode"));
		if (text == QStringLiteral("origin"))
			edit.pivotMode = ModelTransformPivot::Origin;
		else if (text == QStringLiteral("selection"))
			edit.pivotMode = ModelTransformPivot::SelectionCentre;
		else if (text == QStringLiteral("custom"))
			edit.pivotMode = ModelTransformPivot::Custom;
		else
			return fail(2, Text::tr("Use --pivot-mode origin|selection|custom."));
	}
	else if (kind == ModelEditKind::WeightedTransform)
		edit.pivotMode = args.values.contains(QStringLiteral("--pivot")) ? ModelTransformPivot::Custom : ModelTransformPivot::SelectionCentre;
	if (args.values.contains(QStringLiteral("--pivot")) && edit.pivotMode != ModelTransformPivot::Custom)
		return fail(2, Text::tr("--pivot requires --pivot-mode custom."));
	if (args.values.contains(QStringLiteral("--primitive")))
	{
		const QHash<QString, ModelPrimitive> primitives{
			{QStringLiteral("plane"), ModelPrimitive::Plane},		 {QStringLiteral("cube"), ModelPrimitive::Cube},
			{QStringLiteral("circle"), ModelPrimitive::Circle},		 {QStringLiteral("grid"), ModelPrimitive::Grid},
			{QStringLiteral("cylinder"), ModelPrimitive::Cylinder}, {QStringLiteral("cone"), ModelPrimitive::Cone},
			{QStringLiteral("uv-sphere"), ModelPrimitive::UvSphere}, {QStringLiteral("ico-sphere"), ModelPrimitive::IcoSphere},
			{QStringLiteral("torus"), ModelPrimitive::Torus}};
		const auto text = args.values.value(QStringLiteral("--primitive"));
		if (!primitives.contains(text))
			return fail(2, Text::tr("Use --primitive plane|cube|circle|grid|cylinder|cone|uv-sphere|ico-sphere|torus."));
		tool.primitive = primitives.value(text);
		// Ico spheres take rings as subdivisions; keep a light default.
		if (tool.primitive == ModelPrimitive::IcoSphere && !args.values.contains(QStringLiteral("--rings")))
			tool.rings = 2;
		if (tool.primitive == ModelPrimitive::Grid && !args.values.contains(QStringLiteral("--segments")))
			tool.segments = 4;
		if (tool.primitive == ModelPrimitive::Grid && !args.values.contains(QStringLiteral("--rings")))
			tool.rings = 4;
	}
	else if (kind == ModelEditKind::AddPrimitive)
		return fail(2, Text::tr("--tool add requires --primitive."));
	// New geometry on a skeletal model: rigidly to one joint, to the nearest
	// bones, or unbound (refused on a skinned surface), optionally as a new surface.
	if (args.values.contains(QStringLiteral("--bind-joint")))
	{
		if (args.values.contains(QStringLiteral("--bind")))
			return fail(2, Text::tr("Use either --bind-joint NAME or --bind nearest-bones, not both."));
		tool.newGeometryBinding = ModelNewGeometryBinding::Joint;
		tool.bindJoint = args.values.value(QStringLiteral("--bind-joint"));
	}
	else if (args.values.contains(QStringLiteral("--bind")))
	{
		if (args.values.value(QStringLiteral("--bind")) != QStringLiteral("nearest-bones"))
			return fail(2, Text::tr("Use --bind nearest-bones, or --bind-joint NAME for a rigid binding."));
		tool.newGeometryBinding = ModelNewGeometryBinding::NearestBones;
	}
	tool.newSurface = args.values.value(QStringLiteral("--new-surface"));
	if (args.values.contains(QStringLiteral("--new-surface")) && tool.newSurface.trimmed().isEmpty())
		return fail(2, Text::tr("--new-surface needs a surface name."));
	const auto required = [&](const QString &key) { return args.values.contains(key); };
	if ((kind == ModelEditKind::ExtrudeEdges && !required(QStringLiteral("--offset"))) ||
		(kind == ModelEditKind::Rip && (!required(QStringLiteral("--offset")) || !required(QStringLiteral("--side-face")))) ||
		(kind == ModelEditKind::ShrinkFatten && !required(QStringLiteral("--distance"))) ||
		(kind == ModelEditKind::Bisect && !required(QStringLiteral("--plane-normal"))) ||
		(kind == ModelEditKind::Decimate && !required(QStringLiteral("--ratio")) && !required(QStringLiteral("--target-triangles"))) ||
		(kind == ModelEditKind::WeightedTransform && !required(QStringLiteral("--offset")) && !required(QStringLiteral("--rotate")) &&
		 !required(QStringLiteral("--scale"))))
		return fail(2, Text::tr("--tool %1 is missing a required value. See model tool in the CLI reference.").arg(toolName));
	ModelDocument document;
	if (modelPathsReferToSameFile(output, source))
		return fail(2, Text::tr("Choose an output separate from the source."));
	if (!document.load(source, &error))
		return fail(1, error);
	const auto &mesh = document.mesh();
	if (surfaceIndex >= mesh.surfaces.size())
		return fail(4, Text::tr("The source surface does not exist."));
	if (referenceFrame >= mesh.frames.size() || edit.frame >= mesh.frames.size())
		return fail(4, Text::tr("The requested frame does not exist."));
	edit.selection.surface = surfaceIndex;
	if (!readSelection(args, mesh.surfaces[surfaceIndex], &edit.selection, &error))
		return fail(2, error);
	// The editor's Detect Pairs: the same name patterns, keeping only pairs
	// that mirror across this plane and involve a joint of this surface.
	if (detectJoints)
		tool.mirrorJoints = modelMirrorJointPairsForAxis(mesh, detectModelMirrorJointPairs(mesh.skeleton), tool.axis, surfaceIndex,
			std::max(tool.mergeThreshold, 1e-4));
	const auto jointMap = tool.mirrorJoints;
	ModelEditReport report;
	if (!document.edit(edit, &error, {}, &report))
		return fail(4, error);
	const bool dry = args.seen.contains(QStringLiteral("--dry-run"));
	const auto target = inspectModelWriteTarget(output);
	if (!target.isValid())
		return fail(1, target.error);
	if (target.existed && !args.seen.contains(QStringLiteral("--overwrite")))
		return fail(1, Text::tr("The output already exists; use --overwrite to replace it."));
	if (!document.save(output, args.seen.contains(QStringLiteral("--overwrite")), &error, {}, dry))
		return fail(1, error);
	// Add Primitive with --new-surface reports the surface it created.
	const int resultSurface = tool.newSurface.isEmpty() ? surfaceIndex : document.selection().surface;
	const auto &surface = document.mesh().surfaces[resultSurface];
	ModelToolsCliResult result;
	result.payload = {{QStringLiteral("source"), QFileInfo(source).absoluteFilePath()},
					  {QStringLiteral("tool"), toolName},
					  {QStringLiteral("outputPath"), QFileInfo(output).absoluteFilePath()},
					  {QStringLiteral("written"), !dry},
					  {QStringLiteral("dryRun"), dry},
					  {QStringLiteral("frames"), qint64(document.mesh().frames.size())},
					  {QStringLiteral("surface"),
					   QJsonObject{{QStringLiteral("index"), resultSurface},
								   {QStringLiteral("name"), surface.name},
								   {QStringLiteral("vertices"), surface.vertexCount},
								   {QStringLiteral("triangles"), qint64(surface.triangles.size())},
								   {QStringLiteral("seams"), qint64(surface.uvSeams.size())}}},
					  {QStringLiteral("selection"), selectionJson(document.selection())}};
	if (!document.mesh().skeleton.isEmpty())
		result.payload.insert(QStringLiteral("skinProvenance"), modelSkinProvenanceJson(document.mesh(), report.skin));
	if (kind == ModelEditKind::AddPrimitive && !document.mesh().skeleton.isEmpty())
		result.payload.insert(QStringLiteral("binding"),
			QJsonObject{{QStringLiteral("method"), tool.newGeometryBinding == ModelNewGeometryBinding::Joint		  ? QStringLiteral("joint")
												   : tool.newGeometryBinding == ModelNewGeometryBinding::NearestBones ? QStringLiteral("nearest-bones")
																														: QStringLiteral("none")},
						{QStringLiteral("joint"), tool.bindJoint},
						{QStringLiteral("skinned"), !surface.skinning.isEmpty()}});
	if (kind == ModelEditKind::Symmetrize && !document.mesh().skeleton.isEmpty())
	{
		QJsonObject pairs;
		for (auto i = jointMap.cbegin(); i != jointMap.cend(); ++i)
			pairs.insert(i.key(), i.value());
		result.payload.insert(QStringLiteral("jointMap"), pairs);
		result.payload.insert(QStringLiteral("keepJointNames"), tool.keepJointNames);
	}
	result.lines << (dry ? Text::tr("%1 validated; no files written.").arg(toolName) : Text::tr("Saved %1 result to %2.").arg(toolName, output));
	result.lines << Text::tr("Surface %1 (%2): %3 vertices, %4 triangles.").arg(resultSurface).arg(surface.name).arg(surface.vertexCount).arg(surface.triangles.size());
	if (kind == ModelEditKind::Symmetrize && !jointMap.isEmpty())
		result.lines << Text::tr("Joint mirror map: %1.").arg(modelMirrorJointMapText(jointMap));
	result.lines += modelSkinProvenanceLines(document.mesh(), report.skin);
	result.lines += selectionLines(document.selection());
	return result;
}

ModelToolsCliResult runModelSelect(const QStringList &arguments)
{
	const QSet<QString> flags{QStringLiteral("--cli"),	 QStringLiteral("--json"),			QStringLiteral("--quiet"),
							  QStringLiteral("--verbose"), QStringLiteral("--extend"),		QStringLiteral("--delimit-seams"),
							  QStringLiteral("--negative")};
	const QSet<QString> options{QStringLiteral("--input"),	 QStringLiteral("--select"),   QStringLiteral("--mode"),		QStringLiteral("--surface"),
								QStringLiteral("--faces"),	 QStringLiteral("--vertices"), QStringLiteral("--edges"),		QStringLiteral("--edge"),
								QStringLiteral("--from"),	 QStringLiteral("--to"),	   QStringLiteral("--similar"),		QStringLiteral("--threshold"),
								QStringLiteral("--ratio"),	 QStringLiteral("--seed"),	   QStringLiteral("--nth"),			QStringLiteral("--offset"),
								QStringLiteral("--axis"),	 QStringLiteral("--frame"),	   QStringLiteral("--angle"),		QStringLiteral("--settings-file"),
								QStringLiteral("--locale"), QStringLiteral("--catalog-root")};
	Arguments args;
	QString error;
	if (!parse(arguments, flags, options, &args, &error))
		return fail(2, error);
	const auto source = sourceArgument(args, QStringLiteral("select"), &error);
	if (source.isEmpty())
		return fail(2, error);
	const QHash<QString, ModelSelectOperation> operations{
		{QStringLiteral("all"), ModelSelectOperation::All},				 {QStringLiteral("none"), ModelSelectOperation::None},
		{QStringLiteral("invert"), ModelSelectOperation::Invert},		 {QStringLiteral("linked"), ModelSelectOperation::Linked},
		{QStringLiteral("more"), ModelSelectOperation::More},			 {QStringLiteral("less"), ModelSelectOperation::Less},
		{QStringLiteral("loop"), ModelSelectOperation::Loop},			 {QStringLiteral("ring"), ModelSelectOperation::Ring},
		{QStringLiteral("path"), ModelSelectOperation::ShortestPath},	 {QStringLiteral("similar"), ModelSelectOperation::Similar},
		{QStringLiteral("non-manifold"), ModelSelectOperation::NonManifold}, {QStringLiteral("loose"), ModelSelectOperation::Loose},
		{QStringLiteral("boundary"), ModelSelectOperation::Boundary},	 {QStringLiteral("sharp"), ModelSelectOperation::Sharp},
		{QStringLiteral("random"), ModelSelectOperation::Random},		 {QStringLiteral("checker"), ModelSelectOperation::Checker},
		{QStringLiteral("side"), ModelSelectOperation::Side},			 {QStringLiteral("facing"), ModelSelectOperation::Facing},
		{QStringLiteral("mirror"), ModelSelectOperation::Mirror}};
	const auto operationName = args.values.value(QStringLiteral("--select"));
	if (!operations.contains(operationName))
	{
		auto names = operations.keys();
		std::sort(names.begin(), names.end());
		return fail(2, Text::tr("Choose --select %1.").arg(names.join(QLatin1Char('|'))));
	}
	ModelSelectRequest request;
	request.operation = operations.value(operationName);
	const auto modeText = args.values.value(QStringLiteral("--mode"), QStringLiteral("faces"));
	if (modeText == QStringLiteral("faces"))
		request.mode = ModelSelectionMode::Faces;
	else if (modeText == QStringLiteral("vertices"))
		request.mode = ModelSelectionMode::Vertices;
	else if (modeText == QStringLiteral("edges"))
		request.mode = ModelSelectionMode::Edges;
	else
		return fail(2, Text::tr("Use --mode faces|vertices|edges."));
	// Defaults suited to each operator; --threshold overrides them.
	request.threshold = request.operation == ModelSelectOperation::Sharp ? 30
						: request.operation == ModelSelectOperation::Side ? 0
						: request.operation == ModelSelectOperation::Facing ? 45
																		  : 5;
	int surfaceIndex = 0;
	if ((args.values.contains(QStringLiteral("--surface")) && !index(args.values.value(QStringLiteral("--surface")), &surfaceIndex)) ||
		(args.values.contains(QStringLiteral("--frame")) && !index(args.values.value(QStringLiteral("--frame")), &request.frame)) ||
		(args.values.contains(QStringLiteral("--from")) && !index(args.values.value(QStringLiteral("--from")), &request.from)) ||
		(args.values.contains(QStringLiteral("--to")) && !index(args.values.value(QStringLiteral("--to")), &request.to)) ||
		(args.values.contains(QStringLiteral("--nth")) && (!index(args.values.value(QStringLiteral("--nth")), &request.nth) || request.nth < 2)) ||
		(args.values.contains(QStringLiteral("--offset")) && !index(args.values.value(QStringLiteral("--offset")), &request.offset)))
		return fail(2, Text::tr("Index options take nonnegative integers; --nth needs at least 2."));
	if ((args.values.contains(QStringLiteral("--threshold")) &&
		 (!number(args.values.value(QStringLiteral("--threshold")), &request.threshold) || request.threshold < 0)) ||
		(args.values.contains(QStringLiteral("--ratio")) &&
		 (!number(args.values.value(QStringLiteral("--ratio")), &request.ratio) || request.ratio < 0 || request.ratio > 1)) ||
		(args.values.contains(QStringLiteral("--angle")) &&
		 (!number(args.values.value(QStringLiteral("--angle")), &request.maxAngle) || request.maxAngle < 0 || request.maxAngle > 180)))
		return fail(2, Text::tr("A numeric selection option is malformed or outside its range."));
	if (args.values.contains(QStringLiteral("--seed")))
	{
		bool ok = false;
		request.seed = args.values.value(QStringLiteral("--seed")).toUInt(&ok);
		if (!ok)
			return fail(2, Text::tr("--seed takes a nonnegative integer."));
	}
	if (args.values.contains(QStringLiteral("--edge")))
	{
		QSet<ModelEdge> edge;
		if (!edgeList(args.values.value(QStringLiteral("--edge")), &edge) || edge.size() != 1)
			return fail(2, Text::tr("--edge takes one endpoint pair such as 4:5."));
		request.edge = *edge.cbegin();
	}
	if (args.values.contains(QStringLiteral("--axis")))
	{
		request.axis = axisFrom(args.values.value(QStringLiteral("--axis")));
		if (request.axis < 0)
			return fail(2, Text::tr("Axes are x, y or z."));
	}
	request.positive = !args.seen.contains(QStringLiteral("--negative"));
	request.extend = args.seen.contains(QStringLiteral("--extend"));
	request.delimitSeams = args.seen.contains(QStringLiteral("--delimit-seams"));
	if (args.values.contains(QStringLiteral("--similar")))
	{
		const QHash<QString, ModelSimilarity> similarities{
			{QStringLiteral("normal"), ModelSimilarity::Normal},	   {QStringLiteral("area"), ModelSimilarity::Area},
			{QStringLiteral("coplanar"), ModelSimilarity::Coplanar},   {QStringLiteral("length"), ModelSimilarity::Length},
			{QStringLiteral("direction"), ModelSimilarity::Direction}, {QStringLiteral("face-angle"), ModelSimilarity::FaceAngle},
			{QStringLiteral("seam"), ModelSimilarity::Seam},		   {QStringLiteral("valence"), ModelSimilarity::Valence}};
		const auto text = args.values.value(QStringLiteral("--similar"));
		if (!similarities.contains(text) || request.operation != ModelSelectOperation::Similar)
			return fail(2, Text::tr("--similar normal|area|coplanar|length|direction|face-angle|seam|valence applies to --select similar."));
		request.similarity = similarities.value(text);
	}
	else if (request.operation == ModelSelectOperation::Similar)
		request.similarity = request.mode == ModelSelectionMode::Edges ? ModelSimilarity::Length : ModelSimilarity::Normal;
	ModelDocument document;
	if (!document.load(source, &error))
		return fail(1, error);
	const auto &mesh = document.mesh();
	if (surfaceIndex >= mesh.surfaces.size())
		return fail(4, Text::tr("The source surface does not exist."));
	if (request.frame >= mesh.frames.size())
		return fail(4, Text::tr("The requested frame does not exist."));
	ModelSelection current{surfaceIndex, {}, {}};
	if (!readSelection(args, mesh.surfaces[surfaceIndex], &current, &error))
		return fail(2, error);
	// Without --edge, loop and ring grow every selected edge (or every edge
	// between two --vertices) through the service behind the editor's Select
	// Edge Loops and Select Edge Rings, which seeds the same way.
	const bool fromSelection = (request.operation == ModelSelectOperation::Loop || request.operation == ModelSelectOperation::Ring) &&
							   !args.values.contains(QStringLiteral("--edge"));
	ModelSelection selected;
	ModelEdgeLoopsResult loops;
	if (fromSelection)
	{
		if (current.edges.isEmpty() && current.vertices.isEmpty())
			return fail(4, Text::tr("Give --edge a:b for one loop or ring, or --edges (or --vertices) to grow every selected edge."));
		if (!selectModelEdgeLoops(mesh, current, request, {}, &loops, &error))
			return fail(4, error);
		selected = loops.selection;
	}
	else if (!selectModelComponents(mesh, current, request, &selected, &error))
		return fail(4, error);
	ModelToolsCliResult result;
	result.payload = {{QStringLiteral("source"), QFileInfo(source).absoluteFilePath()},
					  {QStringLiteral("operation"), operationName},
					  {QStringLiteral("mode"), modeText},
					  {QStringLiteral("frame"), request.frame},
					  {QStringLiteral("selection"), selectionJson(selected)}};
	if (fromSelection)
	{
		QJsonArray refused;
		for (const auto &refusal : std::as_const(loops.refused))
			refused.append(QJsonObject{{QStringLiteral("edge"), QJsonArray{refusal.edge.first, refusal.edge.second}},
									   {QStringLiteral("reason"), refusal.reason}});
		result.payload.insert(QStringLiteral("seeds"), sortedJson(QSet<ModelEdge>(loops.seeds.cbegin(), loops.seeds.cend())));
		result.payload.insert(QStringLiteral("refused"), refused);
	}
	result.lines << Text::tr("Select %1 (%2) on surface %3:").arg(operationName, modeText).arg(surfaceIndex);
	result.lines += selectionLines(selected);
	result.lines += modelEdgeLoopRefusalLines(loops);
	return result;
}
ModelCliDelivery modelCliDelivery(const QString &projectOption, const QString &source)
{
	ModelCliDelivery delivery;
	delivery.projectRoot = !projectOption.trimmed().isEmpty() ? QFileInfo(projectOption).absoluteFilePath() : findModelProjectRoot(source);
	ProjectManifest manifest;
	if (!delivery.projectRoot.isEmpty())
	{
		QString error;
		if (loadProjectManifest(delivery.projectRoot, &manifest, &error))
			delivery.project = projectModelDeliveryPreferences(manifest, &delivery.warnings);
		else
		{
			manifest = {};
			// An explicit project without a manifest has nothing saved yet.
			if (projectOption.trimmed().isEmpty() || QFileInfo::exists(projectManifestPath(delivery.projectRoot)))
				delivery.warnings << Text::tr("The project manifest could not be read, so its saved delivery targets are not used: %1").arg(error);
		}
	}
	// The installation in use suggests targets from its engine, chosen as the
	// Mesh Editor and releases choose it.
	const StudioSettings settings(StudioSettings::AccessMode::ReadOnly);
	delivery.installation =
		installationModelDeliveryPreferences(manifest, settings.gameInstallations(), settings.selectedGameInstallationId(), &delivery.installationName);
	return delivery;
}

ModelDeliveryChoice ModelCliDelivery::target(const QString &format, const QString &explicitTarget) const
{
	return resolveModelDeliveryTarget(format, explicitTarget, project, installation);
}

bool ModelCliDelivery::strict(bool strictFlag, bool noStrictFlag, ModelDeliveryOrigin *origin) const
{
	if (strictFlag || noStrictFlag)
	{
		if (origin)
			*origin = ModelDeliveryOrigin::Explicit;
		return strictFlag;
	}
	if (origin)
		*origin = project.strictSet ? ModelDeliveryOrigin::Project : ModelDeliveryOrigin::Default;
	return project.strictSet && project.strict;
}

QJsonObject ModelCliDelivery::json(const QString &format, const ModelDeliveryChoice &choice, bool strictValue, ModelDeliveryOrigin strictOrigin) const
{
	QJsonObject object{{QStringLiteral("format"), format},
					   {QStringLiteral("target"), choice.target},
					   {QStringLiteral("targetOrigin"), modelDeliveryOriginId(choice.origin)},
					   {QStringLiteral("strict"), strictValue},
					   {QStringLiteral("strictOrigin"), modelDeliveryOriginId(strictOrigin)}};
	if (!projectRoot.isEmpty())
		object.insert(QStringLiteral("project"), projectRoot);
	if (!warnings.isEmpty())
		object.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(warnings));
	return object;
}

ModelToolsCliResult runModelTargets(const QStringList &arguments)
{
	const QSet<QString> flags{QStringLiteral("--cli"),	QStringLiteral("--json"),  QStringLiteral("--quiet"),	  QStringLiteral("--verbose"),
							  QStringLiteral("--save"), QStringLiteral("--clear"), QStringLiteral("--strict"), QStringLiteral("--no-strict")};
	const QSet<QString> options{QStringLiteral("--input"),		   QStringLiteral("--format"), QStringLiteral("--target"),		 QStringLiteral("--project"),
								QStringLiteral("--settings-file"), QStringLiteral("--locale"), QStringLiteral("--catalog-root")};
	Arguments args;
	QString error;
	if (!parse(arguments, flags, options, &args, &error))
		return fail(2, error);
	const bool inputOption = args.seen.contains(QStringLiteral("--input"));
	if (args.positional.value(0) != QStringLiteral("model") || args.positional.value(1) != QStringLiteral("targets") ||
		args.positional.size() > (inputOption ? 2 : 3))
		return fail(2, Text::tr("Give at most one model source, positionally or with --input; without one, model targets lists the targets."));
	const auto source = inputOption ? args.values.value(QStringLiteral("--input")) : args.positional.value(2);
	const auto format = args.values.value(QStringLiteral("--format")).toLower();
	const auto targetId = args.values.value(QStringLiteral("--target")).toLower();
	const bool save = args.seen.contains(QStringLiteral("--save")), clear = args.seen.contains(QStringLiteral("--clear"));
	const bool strictFlag = args.seen.contains(QStringLiteral("--strict")), noStrictFlag = args.seen.contains(QStringLiteral("--no-strict"));
	if (!format.isEmpty() && modelTargetProfiles(format).isEmpty())
		return fail(2, Text::tr("%1 has no delivery targets. Formats with targets: mdl, md2, md3, iqm, md5mesh, md5anim.").arg(format));
	if (strictFlag && noStrictFlag)
		return fail(2, Text::tr("Use --strict or --no-strict, not both."));
	const auto projectOption = args.values.value(QStringLiteral("--project"));
	// Saving and clearing write the project's choices; they take no source.
	if (save || clear)
	{
		if (save == clear || projectOption.isEmpty() || !source.isEmpty())
			return fail(2, Text::tr("Use --save or --clear with --project <folder> and no model source."));
		if (save && targetId.isEmpty() && !strictFlag && !noStrictFlag)
			return fail(2, Text::tr("--save needs --format and --target, or --strict or --no-strict."));
		if (clear && (!targetId.isEmpty() || strictFlag || noStrictFlag))
			return fail(2, Text::tr("--clear takes --format, or nothing to clear every saved choice."));
		if (!targetId.isEmpty() && format.isEmpty())
			return fail(2, Text::tr("--save needs --format to say which format's target to save."));
		if (save && !targetId.isEmpty() && !findModelTargetProfile(format, targetId, nullptr))
			return fail(2, Text::tr("There is no %1 delivery target called \"%2\". Choose one of: %3.").arg(format, targetId, modelTargetChoices(format)));
		const auto root = QFileInfo(projectOption).absoluteFilePath();
		ProjectManifest manifest;
		if (!loadProjectManifest(root, &manifest, &error))
		{
			if (!QFileInfo(root).isDir())
				return fail(1, Text::tr("%1 is not a project folder.").arg(root));
			// Only a project without a manifest starts a new one; replacing one
			// that cannot be read would lose every other project setting.
			if (QFileInfo::exists(projectManifestPath(root)))
				return fail(1, Text::tr("The project manifest %1 could not be read (%2), so nothing was saved. Fix or restore it, then save the "
										"delivery targets again.")
								   .arg(QDir::toNativeSeparators(projectManifestPath(root)), error));
			manifest = defaultProjectManifest(root);
		}
		QStringList warnings;
		auto preferences = projectModelDeliveryPreferences(manifest, &warnings);
		if (save)
		{
			if (!targetId.isEmpty())
				preferences.targets.insert(format, targetId);
			if (strictFlag || noStrictFlag)
			{
				preferences.strictSet = true;
				preferences.strict = strictFlag;
			}
		}
		else if (!format.isEmpty())
			preferences.targets.remove(format);
		else
			preferences = {};
		setProjectModelDeliveryPreferences(&manifest, preferences);
		if (!saveProjectManifest(manifest, &error))
			return fail(1, Text::tr("The project manifest could not be saved: %1").arg(error));
		ModelToolsCliResult result;
		result.payload = {{QStringLiteral("project"), root},
						  {QStringLiteral("saved"), modelDeliveryPreferencesJson(preferences)},
						  {QStringLiteral("warnings"), QJsonArray::fromStringList(warnings)}};
		result.lines << (preferences.isEmpty() ? Text::tr("The project saves no delivery targets.") : Text::tr("Saved delivery targets for %1:").arg(root));
		for (auto it = preferences.targets.cbegin(); it != preferences.targets.cend(); ++it)
			result.lines << QStringLiteral("  %1 %2").arg(it.key(), it.value());
		if (preferences.strictSet)
			result.lines << (preferences.strict ? Text::tr("  Strict exports: on") : Text::tr("  Strict exports: off"));
		result.lines += warnings;
		return result;
	}
	if (strictFlag || noStrictFlag)
		return fail(2, Text::tr("--strict and --no-strict apply with --save."));
	QVector<ModelTargetProfile> profiles;
	const bool projectView = !projectOption.isEmpty();
	const auto delivery = modelCliDelivery(projectOption, source);
	QJsonArray resolved;
	if (projectView)
	{
		// A project's view: the target each format's exports use.
		ModelDeliveryOrigin strictOrigin = ModelDeliveryOrigin::Default;
		const bool strict = delivery.strict(false, false, &strictOrigin);
		for (const auto &id : modelExportFormatIds())
		{
			if (modelTargetProfiles(id).isEmpty() || (!format.isEmpty() && id != format))
				continue;
			const auto choice = delivery.target(id, targetId);
			ModelTargetProfile profile;
			if (!findModelTargetProfile(id, choice.target, &profile))
				return fail(2, Text::tr("There is no %1 delivery target called \"%2\". Choose one of: %3.").arg(id, choice.target, modelTargetChoices(id)));
			profiles.append(profile);
			resolved.append(delivery.json(id, choice, strict, strictOrigin));
		}
	}
	else
	{
		for (const auto &profile : modelTargetProfiles())
		{
			if ((format.isEmpty() || profile.format == format) && (targetId.isEmpty() || profile.id == targetId))
				profiles.append(profile);
		}
	}
	if (profiles.isEmpty())
	{
		QStringList ids;
		for (const auto &profile : modelTargetProfiles())
		{
			if (!ids.contains(profile.id))
				ids << profile.id;
		}
		return fail(2, Text::tr("There is no delivery target called \"%1\"%2. Choose one of: %3.")
						   .arg(targetId, format.isEmpty() ? QString() : Text::tr(" for %1").arg(format),
								format.isEmpty() ? ids.join(QStringLiteral(", ")) : modelTargetChoices(format)));
	}
	// Some ids name targets of several formats (fte for MDL and IQM, ioquake3
	// for MD3 and IQM); a readiness check needs the one meant.
	QStringList formats;
	for (const auto &profile : std::as_const(profiles))
	{
		if (!formats.contains(profile.format))
			formats << profile.format;
	}
	if (!source.isEmpty() && !targetId.isEmpty() && formats.size() > 1)
		return fail(2, Text::tr("\"%1\" names delivery targets for %2; give --format to choose the one to check.")
						   .arg(targetId, formats.join(QStringLiteral(", "))));
	ModelToolsCliResult result;
	if (source.isEmpty())
	{
		QJsonArray list;
		for (int index = 0; index < profiles.size(); ++index)
		{
			const auto &profile = profiles.at(index);
			list.append(modelTargetProfileJson(profile));
			if (projectView)
			{
				const auto origin = modelDeliveryOriginName(delivery.target(profile.format, targetId).origin);
				result.lines << Text::tr("%1 %2: %3 (%4)").arg(profile.format, profile.id, profile.label, origin);
				continue;
			}
			result.lines << Text::tr("%1 %2%3: %4")
								.arg(profile.format, profile.id, profile.isDefault ? Text::tr(" (default)") : QString(), profile.label);
			result.lines << QStringLiteral("  ") + profile.description;
		}
		result.payload = {{QStringLiteral("targets"), list}};
		if (projectView)
		{
			result.payload.insert(QStringLiteral("project"), delivery.projectRoot);
			result.payload.insert(QStringLiteral("delivery"), resolved);
			result.lines += delivery.warnings;
		}
		return result;
	}
	ModelDocument document;
	if (!document.load(source, &error))
	{
		ModelMesh imported;
		QByteArray bytes;
		if (!readModelFile(source, &bytes, &error) || !importEditableModel(QFileInfo(source).absoluteFilePath(), bytes, &imported, &error) || !document.setMesh(imported, &error))
			return fail(1, error);
	}
	QJsonArray reports;
	int blocked = 0, blockers = 0;
	for (const auto &profile : std::as_const(profiles))
	{
		ModelTargetReport report;
		if (!assessModelTarget(document.mesh(), profile, &report, &error))
			return fail(1, error);
		blocked += !report.ready();
		blockers += int(report.issues.size());
		reports.append(modelTargetReportJson(report));
		const int issues = int(report.issues.size());
		result.lines << (report.ready() ? Text::tr("%1 %2 (%3): ready").arg(profile.format, profile.id, profile.label)
										: Text::tr("%1 %2 (%3): %n blocker(s)", nullptr, issues).arg(profile.format, profile.id, profile.label));
		for (const auto &line : modelTargetIssueLines(report))
			result.lines << QStringLiteral("  - ") + line;
	}
	result.payload = {{QStringLiteral("source"), QFileInfo(source).absoluteFilePath()},
					  {QStringLiteral("reports"), reports},
					  {QStringLiteral("ready"), blocked == 0}};
	if (projectView)
	{
		result.payload.insert(QStringLiteral("project"), delivery.projectRoot);
		result.payload.insert(QStringLiteral("delivery"), resolved);
		result.lines += delivery.warnings;
	}
	// A named target (one format's) that is not ready fails like a validation;
	// a survey of several targets reports and succeeds.
	if (!targetId.isEmpty() && blocked > 0)
	{
		result.exitCode = 4;
		result.error = Text::tr("The model is not ready for the %1 delivery target %2; limits it exceeds: %n.", nullptr, blockers)
						   .arg(profiles.first().format, targetId);
	}
	return result;
}

ModelToolsCliResult runModelPrepare(const QStringList &arguments)
{
	const QSet<QString> flags{QStringLiteral("--cli"),	   QStringLiteral("--json"),	  QStringLiteral("--quiet"), QStringLiteral("--verbose"),
							  QStringLiteral("--dry-run"), QStringLiteral("--overwrite")};
	const QSet<QString> options{QStringLiteral("--input"),	QStringLiteral("--output"),		   QStringLiteral("--format"), QStringLiteral("--target"),
								QStringLiteral("--project"), QStringLiteral("--skip"),		   QStringLiteral("--settings-file"),
								QStringLiteral("--locale"),	QStringLiteral("--catalog-root")};
	Arguments args;
	QString error;
	if (!parse(arguments, flags, options, &args, &error))
		return fail(2, error);
	const auto source = sourceArgument(args, QStringLiteral("prepare"), &error);
	if (source.isEmpty())
		return fail(2, error);
	const auto format = args.values.value(QStringLiteral("--format")).toLower();
	if (format.isEmpty() || modelTargetProfiles(format).isEmpty())
		return fail(2, Text::tr("model prepare needs --format mdl, md2, md3, iqm, md5mesh or md5anim: the export the model is prepared for."));
	const bool dry = args.seen.contains(QStringLiteral("--dry-run")), overwrite = args.seen.contains(QStringLiteral("--overwrite"));
	const auto output = args.values.value(QStringLiteral("--output"));
	if (!dry && !output.endsWith(QStringLiteral(".mesh.json"), Qt::CaseInsensitive))
		return fail(2, Text::tr("model prepare writes an editable source: give --output <name.mesh.json>, or --dry-run to review only."));
	ModelTargetPrepareOptions prepareOptions;
	if (args.values.contains(QStringLiteral("--skip")) && !skipModelTargetFixes(args.values.value(QStringLiteral("--skip")), &prepareOptions, &error))
		return fail(2, error);
	const auto delivery = modelCliDelivery(args.values.value(QStringLiteral("--project")), source);
	const auto choice = delivery.target(format, args.values.value(QStringLiteral("--target")));
	ModelTargetProfile profile;
	if (!findModelTargetProfile(format, choice.target, &profile))
		return fail(2, Text::tr("There is no %1 delivery target called \"%2\". Choose one of: %3.").arg(format, choice.target, modelTargetChoices(format)));
	ModelDocument document;
	if (!document.load(source, &error))
	{
		ModelMesh imported;
		QByteArray bytes;
		if (!readModelFile(source, &bytes, &error) || !importEditableModel(source, bytes, &imported, &error) || !document.setMesh(imported, &error))
			return fail(1, error);
	}
	ModelTargetPreparation preparation;
	if (!prepareModelForTarget(document.mesh(), profile, prepareOptions, &preparation, &error))
		return fail(1, error);
	ModelToolsCliResult result;
	bool written = false;
	if (!dry)
	{
		ModelDocument prepared;
		if (!prepared.setMesh(preparation.mesh, &error) || !prepared.save(output, overwrite, &error))
			return fail(output.isEmpty() || QFileInfo::exists(output) ? 2 : 1, error);
		written = true;
	}
	result.lines += modelTargetPreparationLines(preparation);
	result.lines << (dry ? Text::tr("Reviewed only; nothing was written.") : Text::tr("Wrote %1.").arg(output));
	result.lines += delivery.warnings;
	result.payload = modelTargetPreparationJson(preparation);
	result.payload.insert(QStringLiteral("source"), QFileInfo(source).absoluteFilePath());
	result.payload.insert(QStringLiteral("targetOrigin"), modelDeliveryOriginId(choice.origin));
	if (!delivery.projectRoot.isEmpty())
		result.payload.insert(QStringLiteral("project"), delivery.projectRoot);
	result.payload.insert(QStringLiteral("dryRun"), dry);
	result.payload.insert(QStringLiteral("written"), written);
	if (written)
		result.payload.insert(QStringLiteral("output"), QFileInfo(output).absoluteFilePath());
	// Limits no fix met still block the export: the prepared source is written,
	// and the exit code says it is not ready.
	if (!preparation.after.ready())
	{
		result.exitCode = 4;
		const int left = int(preparation.after.issues.size());
		result.error = Text::tr("The prepared model still exceeds %n limit(s) of the %1 delivery target %2.", nullptr, left).arg(format, profile.id);
	}
	return result;
}

ModelToolsCliResult runModelLod(const QStringList &arguments)
{
	const QSet<QString> flags{QStringLiteral("--cli"),		 QStringLiteral("--json"),		QStringLiteral("--quiet"),	   QStringLiteral("--verbose"),
							  QStringLiteral("--dry-run"),	 QStringLiteral("--overwrite"), QStringLiteral("--strict"),	   QStringLiteral("--no-strict"),
							  QStringLiteral("--inspect"),	 QStringLiteral("--use-recipe"),	QStringLiteral("--save-recipe")};
	const QSet<QString> options{QStringLiteral("--input"),			  QStringLiteral("--output"),		 QStringLiteral("--levels"),
								QStringLiteral("--ratio"),			  QStringLiteral("--target"),		 QStringLiteral("--target-triangles"),
								QStringLiteral("--level-source"),	  QStringLiteral("--project"),		 QStringLiteral("--package"),
								QStringLiteral("--entry"),			  QStringLiteral("--settings-file"), QStringLiteral("--locale"),
								QStringLiteral("--catalog-root")};
	Arguments args;
	QString error;
	if (!parse(arguments, flags, options, &args, &error))
		return fail(2, error);
	const bool inspect = args.seen.contains(QStringLiteral("--inspect"));
	const bool package = args.seen.contains(QStringLiteral("--package"));
	QString source;
	if (!(inspect && package))
	{
		source = sourceArgument(args, QStringLiteral("lod"), &error);
		if (source.isEmpty())
			return fail(2, error);
	}
	else if (args.positional.size() != 2 || !args.seen.contains(QStringLiteral("--entry")) || args.seen.contains(QStringLiteral("--input")))
		return fail(2, Text::tr("model lod --inspect --package <package> --entry <models/name.md3> inspects a set in a package; give no other source."));
	if (args.seen.contains(QStringLiteral("--strict")) && args.seen.contains(QStringLiteral("--no-strict")))
		return fail(2, Text::tr("Use --strict or --no-strict, not both."));
	const auto delivery = modelCliDelivery(args.values.value(QStringLiteral("--project")), package ? args.values.value(QStringLiteral("--package")) : source);
	const auto choice = delivery.target(QStringLiteral("md3"), args.values.value(QStringLiteral("--target")));
	ModelTargetProfile target;
	if (!findModelTargetProfile(QStringLiteral("md3"), choice.target, &target))
		return fail(2, Text::tr("There is no MD3 delivery target called \"%1\". Choose one of: %2.")
						   .arg(choice.target, modelTargetChoices(QStringLiteral("md3"))));
	const int loaded = std::max(0, target.limits.detailLevels);

	// Inspecting an existing set: loose files beside the base, or package entries.
	if (inspect)
	{
		for (const auto *option : {"--output", "--levels", "--ratio", "--target-triangles", "--level-source"})
		{
			if (args.seen.contains(QLatin1String(option)))
				return fail(2, Text::tr("--inspect reads an existing set; %1 applies to writing one.").arg(QLatin1String(option)));
		}
		for (const auto *option : {"--use-recipe", "--save-recipe", "--dry-run", "--overwrite", "--strict", "--no-strict"})
		{
			if (args.seen.contains(QLatin1String(option)))
				return fail(2, Text::tr("--inspect reads an existing set; %1 applies to writing one.").arg(QLatin1String(option)));
		}
		PackageArchive archive;
		ModelLodFileReader read = modelLodDiskReader();
		QString base = source;
		if (package)
		{
			const auto path = args.values.value(QStringLiteral("--package"));
			if (path.endsWith(QStringLiteral(".vibepackage"), Qt::CaseInsensitive))
			{
				PackageStagingModel staging;
				if (!PackageDraft::load(path, &staging, &error))
					return fail(1, error);
				archive = packagePlannedArchive(staging, &error);
				if (!archive.isOpen())
					return fail(1, error);
			}
			else if (!archive.load(path, &error))
				return fail(1, error);
			base = args.values.value(QStringLiteral("--entry"));
			// The game's file system matches package paths without case.
			QHash<QString, QString> entries;
			for (const auto &entry : archive.entries())
			{
				if (entry.kind == PackageEntryKind::File)
					entries.insert(entry.virtualPath.toLower(), entry.virtualPath);
			}
			read = [&archive, entries](const QString &entry, QByteArray *bytes, QString *problem)
			{
				if (problem)
					problem->clear();
				const auto found = entries.constFind(QString(entry).replace(QLatin1Char('\\'), QLatin1Char('/')).toLower());
				if (found == entries.cend())
					return false;
				return archive.readEntryBytes(found.value(), bytes, problem, modelFileByteLimit);
			};
		}
		ModelLodInspection inspection;
		if (!inspectModelLodSet(base, read, target, &inspection, &error))
			return fail(1, error);
		ModelToolsCliResult result;
		result.lines += modelLodInspectionLines(inspection);
		result.payload = modelLodInspectionJson(inspection);
		result.payload.insert(QStringLiteral("base"), package ? base : QFileInfo(base).absoluteFilePath());
		if (package)
			result.payload.insert(QStringLiteral("package"), QFileInfo(args.values.value(QStringLiteral("--package"))).absoluteFilePath());
		result.payload.insert(QStringLiteral("targetOrigin"), modelDeliveryOriginId(choice.origin));
		if (!inspection.validation.ok())
		{
			result.exitCode = 4;
			const int errors = inspection.validation.count(ModelLodSeverity::Error);
			result.error = Text::tr("The detail set has %n problem(s) that the game would read wrongly.", nullptr, errors);
		}
		return result;
	}
	if (package || args.seen.contains(QStringLiteral("--entry")))
		return fail(2, Text::tr("--package and --entry apply to --inspect."));

	const auto output = args.values.value(QStringLiteral("--output"));
	const bool saveRecipe = args.seen.contains(QStringLiteral("--save-recipe"));
	if (!(saveRecipe && output.isEmpty()) && !output.endsWith(QStringLiteral(".md3"), Qt::CaseInsensitive))
		return fail(2, Text::tr("model lod requires --output <name.md3>; levels are written as name_1.md3 and name_2.md3 beside it."));
	ModelDocument document;
	const bool editable = document.load(source, &error);
	if (!editable)
	{
		ModelMesh imported;
		QByteArray bytes;
		if (!readModelFile(source, &bytes, &error) || !importEditableModel(QFileInfo(source).absoluteFilePath(), bytes, &imported, &error) || !document.setMesh(imported, &error))
			return fail(1, error);
	}
	if (saveRecipe && !editable)
		return fail(2, Text::tr("--save-recipe stores the detail levels in an editable .mesh.json source; %1 is not one.").arg(source));
	// The recipe: the source's own, or one the options describe.
	const bool described = args.seen.contains(QStringLiteral("--levels")) || args.seen.contains(QStringLiteral("--ratio")) ||
						   args.seen.contains(QStringLiteral("--target-triangles")) || args.seen.contains(QStringLiteral("--level-source"));
	const bool useStored = args.seen.contains(QStringLiteral("--use-recipe")) || (!described && !document.mesh().detailLevels.isEmpty());
	if (useStored && described)
		return fail(2, Text::tr("--use-recipe builds the levels the source stores; leave out --levels, --ratio, --target-triangles and --level-source."));
	if (args.seen.contains(QStringLiteral("--use-recipe")) && document.mesh().detailLevels.isEmpty())
		return fail(4, Text::tr("%1 stores no detail levels; describe them with --levels and --ratio, then --save-recipe.").arg(source));
	const auto sourceDirectory = QFileInfo(source).absolutePath();
	QVector<ModelLodRecipeLevel> recipe;
	if (useStored)
		recipe = document.mesh().detailLevels;
	else
	{
		int levels = std::min(2, loaded);
		double ratio = 0.5;
		QVector<int> triangles;
		if (args.values.contains(QStringLiteral("--target-triangles")))
		{
			for (const auto &part : args.values.value(QStringLiteral("--target-triangles")).split(QLatin1Char(',')))
			{
				int value = 0;
				if (!index(part.trimmed(), &value) || value < 1)
					return fail(2, Text::tr("--target-triangles takes one positive triangle count per level, such as 1200,600."));
				triangles << value;
			}
			levels = int(triangles.size());
		}
		if ((args.values.contains(QStringLiteral("--levels")) &&
			 (!index(args.values.value(QStringLiteral("--levels")), &levels) || levels < 1 || levels > loaded ||
			  (!triangles.isEmpty() && levels != triangles.size()))) ||
			(args.values.contains(QStringLiteral("--ratio")) &&
			 (!number(args.values.value(QStringLiteral("--ratio")), &ratio) || ratio < 0.05 || ratio > 0.95)))
			return fail(2, loaded == 0 ? Text::tr("%1 loads no detail levels beside the model; choose an MD3 delivery target that loads them with --target.")
											 .arg(target.label)
									   : Text::tr("Use --levels 1-%1 and --ratio 0.05-0.95: %2 loads name_1.md3 up to name_%1.md3 beside the model.")
											 .arg(loaded)
											 .arg(target.label));
		recipe = modelLodGeneratedRecipe(levels, ratio);
		// A triangle target replaces the ratio, which keeps its default.
		for (int level = 0; level < triangles.size() && level < recipe.size(); ++level)
		{
			recipe[level].targetTriangles = triangles[level];
			recipe[level].ratio = 0.5;
		}
		if (args.values.contains(QStringLiteral("--level-source")))
		{
			for (const auto &part : args.values.value(QStringLiteral("--level-source")).split(QLatin1Char(',')))
			{
				const auto equals = part.indexOf(QLatin1Char('='));
				int level = 0;
				if (equals < 1 || !index(part.left(equals).trimmed(), &level) || level < 1 || level > recipe.size() || part.mid(equals + 1).trimmed().isEmpty())
					return fail(2, Text::tr("--level-source takes LEVEL=PATH pairs, such as 2=models/prop_low.mesh.json, for levels 1 to %1.").arg(recipe.size()));
				auto &entry = recipe[level - 1];
				entry.generated = false;
				entry.targetTriangles = 0;
				entry.ratio = 0.5;
				entry.preserveBoundary = true;
				// Given relative to here; the source file stores it relative to
				// its own folder when it is saved (ModelDocument::save).
				entry.source = QFileInfo(part.mid(equals + 1).trimmed()).absoluteFilePath();
			}
		}
	}
	if (!validateModelLodRecipe(recipe, loaded, &error))
		return fail(2, error);
	ModelDeliveryOrigin strictOrigin = ModelDeliveryOrigin::Default;
	const bool strict = delivery.strict(args.seen.contains(QStringLiteral("--strict")), args.seen.contains(QStringLiteral("--no-strict")), &strictOrigin);
	const bool dry = args.seen.contains(QStringLiteral("--dry-run")), overwrite = args.seen.contains(QStringLiteral("--overwrite"));
	ModelToolsCliResult result;
	result.payload = {{QStringLiteral("source"), QFileInfo(source).absoluteFilePath()},
					  {QStringLiteral("target"), target.id},
					  {QStringLiteral("targetOrigin"), modelDeliveryOriginId(choice.origin)},
					  {QStringLiteral("targetLevels"), loaded},
					  {QStringLiteral("levels"), int(recipe.size())},
					  {QStringLiteral("recipe"), modelLodRecipeJson(recipe)},
					  {QStringLiteral("recipeFromSource"), useStored},
					  {QStringLiteral("strict"), strict},
					  {QStringLiteral("strictOrigin"), modelDeliveryOriginId(strictOrigin)},
					  {QStringLiteral("dryRun"), dry}};
	if (!delivery.projectRoot.isEmpty())
		result.payload.insert(QStringLiteral("project"), delivery.projectRoot);
	ModelLodSet set;
	if (!output.isEmpty())
	{
		if (!prepareModelLodSet(document.mesh(), recipe, sourceDirectory, output, target.id, {source}, strict, &set, &error))
		{
			// Aliased or unusable destinations are usage errors; the rest are the
			// model's (decimation, a hand-made level, or the target's preflight).
			bool destination = false;
			for (int level = 0; level <= recipe.size() && !destination; ++level)
			{
				const auto path = level == 0 ? output : modelLodPath(output, level);
				destination = modelPathsReferToSameFile(path, source) || !inspectModelWriteTarget(path).isValid();
			}
			return fail(destination ? 2 : 4, error);
		}
		const auto existing = set.existing();
		if (!existing.isEmpty() && !overwrite)
			return fail(1, Text::tr("Nothing was written: outputs already exist (%n): %1. Use --overwrite to replace them.", nullptr,
									int(existing.size()))
							   .arg(existing.join(QStringLiteral(", "))));
		result.payload.insert(QStringLiteral("existing"), QJsonArray::fromStringList(existing));
	}
	// The recipe goes into the source before any detail file is written, so a
	// set on disk never claims a recipe the source does not have.
	bool recipeSaved = false;
	if (saveRecipe && !dry)
	{
		auto mesh = document.mesh();
		mesh.detailLevels = recipe;
		if (!document.adopt(mesh, document.selection(), document.revisionFingerprint(), &error) || !document.save(source, false, &error))
			return fail(1, error);
		recipeSaved = true;
	}
	result.payload.insert(QStringLiteral("recipeSaved"), recipeSaved);
	if (output.isEmpty())
	{
		result.lines << (recipeSaved ? Text::tr("Stored %n detail level(s) in %1.", nullptr, int(recipe.size())).arg(source)
									 : Text::tr("The detail-level recipe is valid; nothing was written."));
		return result;
	}
	bool published = true;
	if (!dry)
		published = publishModelLodSet(&set, overwrite, &error);
	if (!published)
	{
		result.exitCode = 1;
		result.error = error;
	}
	const int written = set.written();
	// Every file's omissions once: the detail levels share the base model's.
	QStringList omissions;
	for (const auto &file : std::as_const(set.files))
	{
		for (const auto &omission : file.exportOmissions)
		{
			if (!omissions.contains(omission))
				omissions << omission;
		}
	}
	result.lines << (dry ? Text::tr("Detail levels validated for %1; no files written.").arg(set.targetLabel)
						 : Text::tr("Wrote %n model file(s) for %1.", nullptr, written).arg(set.targetLabel));
	if (recipeSaved)
		result.lines << Text::tr("Stored the detail levels in %1.").arg(source);
	for (const auto &file : std::as_const(set.files))
	{
		result.lines << (file.source.isEmpty() ? Text::tr("Level %1: %2 triangles · %3").arg(file.level).arg(file.triangles).arg(file.path)
											   : Text::tr("Level %1: %2 triangles · %3 (from %4)").arg(file.level).arg(file.triangles).arg(file.path, file.source));
		result.lines += file.notes;
		for (const auto &note : file.exportNotes)
			result.lines << QStringLiteral("  ") + note;
	}
	result.lines += modelLodValidationLines(set.validation);
	// Detail files the set leaves in place still load with it.
	result.lines += modelLodSiblingLines(set.stale);
	result.payload.insert(QStringLiteral("leftInPlace"), modelLodSiblingJson(set.stale));
	result.payload.insert(QStringLiteral("ratio"), set.ratio);
	result.payload.insert(QStringLiteral("outputs"), modelLodSetJson(set));
	result.payload.insert(QStringLiteral("written"), !dry && published);
	result.payload.insert(QStringLiteral("committed"), written);
	result.payload.insert(QStringLiteral("exportOmissions"), QJsonArray::fromStringList(omissions));
	result.payload.insert(QStringLiteral("findings"), modelLodValidationJson(set.validation));
	return result;
}
} // namespace vibestudio::cli
