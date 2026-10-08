#include "cli/model_tools.h"

#include "core/model_document.h"
#include "core/model_file_io.h"
#include "core/model_lod.h"
#include "core/model_mesh_tools.h"
#include "core/model_selection_tools.h"

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
							  QStringLiteral("--connected"),   QStringLiteral("--allow-boundary"), QStringLiteral("--fit")};
	const QSet<QString> options{
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
		QStringLiteral("--target-triangles"), QStringLiteral("--width"), QStringLiteral("--tile-size"), QStringLiteral("--u-axis"),
		QStringLiteral("--v-axis"), QStringLiteral("--settings-file"), QStringLiteral("--locale"),	   QStringLiteral("--catalog-root")};
	Arguments args;
	QString error;
	if (!parse(arguments, flags, options, &args, &error))
		return fail(2, error);
	const auto source = sourceArgument(args, QStringLiteral("tool"), &error);
	if (source.isEmpty())
		return fail(2, error);
	const QHash<QString, ModelEditKind> tools{
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
		{QStringLiteral("rotate-edges"), {QStringLiteral("--edges")}},
		{QStringLiteral("merge"), {QStringLiteral("--faces"), QStringLiteral("--vertices"), QStringLiteral("--edges"), QStringLiteral("--merge"), QStringLiteral("--point")}},
		{QStringLiteral("dissolve-vertices"), {QStringLiteral("--vertices"), QStringLiteral("--edges"), QStringLiteral("--faces")}},
		{QStringLiteral("dissolve-faces"), {QStringLiteral("--faces")}},
		{QStringLiteral("poke"), {QStringLiteral("--faces"), QStringLiteral("--distance")}},
		{QStringLiteral("beautify"), {QStringLiteral("--faces"), QStringLiteral("--angle")}},
		{QStringLiteral("make-face"), {QStringLiteral("--vertices"), QStringLiteral("--edges")}},
		{QStringLiteral("extrude-edges"), {QStringLiteral("--edges"), QStringLiteral("--offset")}},
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
		{QStringLiteral("symmetrize"), {QStringLiteral("--faces"), QStringLiteral("--axis"), QStringLiteral("--direction"), QStringLiteral("--threshold")}},
		{QStringLiteral("loop-cut"), {QStringLiteral("--edges"), QStringLiteral("--cuts"), QStringLiteral("--slide"), QStringLiteral("--angle")}},
		{QStringLiteral("add"), {QStringLiteral("--primitive"), QStringLiteral("--at"), QStringLiteral("--size"), QStringLiteral("--segments"), QStringLiteral("--rings"), QStringLiteral("--up")}},
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
	edit.pivotFrame = referenceFrame;
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
		!readNumber(QStringLiteral("--width"), &tool.bevelWidth, 0.000001, 1000000) ||
		!readNumber(QStringLiteral("--tile-size"), &tool.uvTileSize, 0.001, 1000000) ||
		!readNumber(QStringLiteral("--depth"), &tool.insetDepth, -1000000, 1000000) ||
		!readNumber(QStringLiteral("--distance"), &tool.offset, -1000000, 1000000) ||
		!readNumber(QStringLiteral("--factor"), &tool.smoothFactor, 0, 1) || !readInt(QStringLiteral("--iterations"), &tool.iterations, 1, 100) ||
		!readNumber(QStringLiteral("--proportional-radius"), &tool.proportionalRadius, 0, 1000000) ||
		!readNumber(QStringLiteral("--angle"), toolName == QStringLiteral("auto-smooth") ? &tool.smoothAngle : &tool.maxAngle, 0, 180) ||
		!readNumber(QStringLiteral("--threshold"), &tool.mergeThreshold, 0, 1000) || !readInt(QStringLiteral("--cuts"), &tool.cuts, 1, 64) ||
		!readNumber(QStringLiteral("--slide"), &tool.slide, -1, 1) || !readInt(QStringLiteral("--segments"), &tool.segments, 3, 256) ||
		!readInt(QStringLiteral("--rings"), &tool.rings, 1, 256) || !readNumber(QStringLiteral("--ratio"), &tool.ratio, 0, 1) ||
		!readInt(QStringLiteral("--target-triangles"), &tool.targetTriangles, 1, 1000000))
		return fail(2, Text::tr("A numeric tool option is malformed or outside its range. See model tool in the CLI reference."));
	tool.evenThickness = !args.seen.contains(QStringLiteral("--no-even"));
	tool.pinBoundary = args.seen.contains(QStringLiteral("--pin-boundary"));
	tool.connectedOnly = args.seen.contains(QStringLiteral("--connected"));
	tool.preserveBoundary = !args.seen.contains(QStringLiteral("--allow-boundary"));
	tool.inset = args.seen.contains(QStringLiteral("--individual")) ? ModelInsetMode::Individual : ModelInsetMode::Region;
	tool.uvFit = args.seen.contains(QStringLiteral("--fit"));
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
	if ((args.values.contains(QStringLiteral("--point")) && !triple(args.values.value(QStringLiteral("--point")), &tool.point)) ||
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
	const auto required = [&](const QString &key) { return args.values.contains(key); };
	if ((kind == ModelEditKind::ExtrudeEdges && !required(QStringLiteral("--offset"))) ||
		(kind == ModelEditKind::ShrinkFatten && !required(QStringLiteral("--distance"))) ||
		(kind == ModelEditKind::Bisect && !required(QStringLiteral("--plane-normal"))) ||
		(kind == ModelEditKind::Decimate && !required(QStringLiteral("--ratio")) && !required(QStringLiteral("--target-triangles"))) ||
		(kind == ModelEditKind::WeightedTransform && !required(QStringLiteral("--offset")) && !required(QStringLiteral("--rotate")) &&
		 !required(QStringLiteral("--scale"))))
		return fail(2, Text::tr("--tool %1 is missing a required value. See model tool in the CLI reference.").arg(toolName));
	ModelDocument document;
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
	if (!document.edit(edit, &error))
		return fail(4, error);
	const bool dry = args.seen.contains(QStringLiteral("--dry-run"));
	const auto target = inspectModelWriteTarget(output);
	if (!target.isValid())
		return fail(1, target.error);
	if (target.existed && !args.seen.contains(QStringLiteral("--overwrite")))
		return fail(1, Text::tr("The output already exists; use --overwrite to replace it."));
	if (dry)
	{
		const auto json = editableModelJson(document.mesh(), &error);
		if (json.isEmpty())
			return fail(4, error);
		if (QJsonDocument(json).toJson(QJsonDocument::Compact).size() > modelDocumentMaxSourceBytes)
			return fail(4, Text::tr("The edited model exceeds the 64 MiB source limit."));
	}
	else if (!document.save(output, args.seen.contains(QStringLiteral("--overwrite")), &error))
		return fail(1, error);
	const auto &surface = document.mesh().surfaces[surfaceIndex];
	ModelToolsCliResult result;
	result.payload = {{QStringLiteral("source"), QFileInfo(source).absoluteFilePath()},
					  {QStringLiteral("tool"), toolName},
					  {QStringLiteral("outputPath"), QFileInfo(output).absoluteFilePath()},
					  {QStringLiteral("written"), !dry},
					  {QStringLiteral("dryRun"), dry},
					  {QStringLiteral("frames"), qint64(document.mesh().frames.size())},
					  {QStringLiteral("surface"),
					   QJsonObject{{QStringLiteral("index"), surfaceIndex},
								   {QStringLiteral("name"), surface.name},
								   {QStringLiteral("vertices"), surface.vertexCount},
								   {QStringLiteral("triangles"), qint64(surface.triangles.size())},
								   {QStringLiteral("seams"), qint64(surface.uvSeams.size())}}},
					  {QStringLiteral("selection"), selectionJson(document.selection())}};
	result.lines << (dry ? Text::tr("%1 validated; no files written.").arg(toolName) : Text::tr("Saved %1 result to %2.").arg(toolName, output));
	result.lines << Text::tr("Surface %1 (%2): %3 vertices, %4 triangles.").arg(surfaceIndex).arg(surface.name).arg(surface.vertexCount).arg(surface.triangles.size());
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
	ModelSelection selected;
	if (!selectModelComponents(mesh, current, request, &selected, &error))
		return fail(4, error);
	ModelToolsCliResult result;
	result.payload = {{QStringLiteral("source"), QFileInfo(source).absoluteFilePath()},
					  {QStringLiteral("operation"), operationName},
					  {QStringLiteral("mode"), modeText},
					  {QStringLiteral("frame"), request.frame},
					  {QStringLiteral("selection"), selectionJson(selected)}};
	result.lines << Text::tr("Select %1 (%2) on surface %3:").arg(operationName, modeText).arg(surfaceIndex);
	result.lines += selectionLines(selected);
	return result;
}
ModelToolsCliResult runModelLod(const QStringList &arguments)
{
	const QSet<QString> flags{QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--quiet"), QStringLiteral("--verbose"),
							  QStringLiteral("--dry-run"), QStringLiteral("--overwrite")};
	const QSet<QString> options{QStringLiteral("--input"), QStringLiteral("--output"), QStringLiteral("--levels"), QStringLiteral("--ratio"),
								QStringLiteral("--settings-file"), QStringLiteral("--locale"), QStringLiteral("--catalog-root")};
	Arguments args;
	QString error;
	if (!parse(arguments, flags, options, &args, &error))
		return fail(2, error);
	const auto source = sourceArgument(args, QStringLiteral("lod"), &error);
	if (source.isEmpty())
		return fail(2, error);
	const auto output = args.values.value(QStringLiteral("--output"));
	if (!output.endsWith(QStringLiteral(".md3"), Qt::CaseInsensitive))
		return fail(2, Text::tr("model lod requires --output <name.md3>; levels are written as name_1.md3 and name_2.md3 beside it."));
	int levels = 2;
	double ratio = 0.5;
	if ((args.values.contains(QStringLiteral("--levels")) &&
		 (!index(args.values.value(QStringLiteral("--levels")), &levels) || levels < 1 || levels > 3)) ||
		(args.values.contains(QStringLiteral("--ratio")) &&
		 (!number(args.values.value(QStringLiteral("--ratio")), &ratio) || ratio < 0.05 || ratio > 0.95)))
		return fail(2, Text::tr("Use --levels 1-3 and --ratio 0.05-0.95."));
	ModelDocument document;
	if (!document.load(source, &error))
	{
		ModelMesh imported;
		QByteArray bytes;
		if (!readModelFile(source, &bytes, &error) || !importEditableModel(source, bytes, &imported, &error) || !document.setMesh(imported, &error))
			return fail(1, error);
	}
	QVector<ModelLodLevel> lods;
	if (!buildModelLods(document.mesh(), levels, ratio, &lods, &error))
		return fail(4, error);
	struct Output
	{
		QString path;
		QByteArray bytes;
		int level = 0, triangles = 0;
		QStringList notes;
	};
	QVector<Output> outputs;
	int baseTriangles = 0;
	for (const auto &surface : document.mesh().surfaces)
		baseTriangles += surface.triangles.size();
	outputs.append({output, exportEditableModel(document.mesh(), QStringLiteral("md3"), 0, &error), 0, baseTriangles, {}});
	if (outputs.last().bytes.isEmpty())
		return fail(4, error);
	for (int level = 0; level < lods.size(); ++level)
	{
		Output entry{modelLodPath(output, level + 1), exportEditableModel(lods[level].mesh, QStringLiteral("md3"), 0, &error), level + 1,
					 lods[level].triangles, lods[level].notes};
		if (entry.bytes.isEmpty())
			return fail(4, error);
		outputs.append(entry);
	}
	const bool dry = args.seen.contains(QStringLiteral("--dry-run"));
	QVector<ModelWriteTarget> targets;
	for (const auto &entry : std::as_const(outputs))
	{
		const auto target = inspectModelWriteTarget(entry.path);
		if (!target.isValid())
			return fail(1, target.error);
		if (target.existed && !args.seen.contains(QStringLiteral("--overwrite")))
			return fail(1, Text::tr("%1 already exists; use --overwrite to replace it.").arg(entry.path));
		if (modelPathsReferToSameFile(entry.path, source))
			return fail(2, Text::tr("Choose an output separate from the source."));
		targets.append(target);
	}
	if (!dry)
	{
		for (int i = 0; i < outputs.size(); ++i)
		{
			if (!writeModelFile(targets[i], outputs[i].bytes, &error))
				return fail(1, error);
		}
	}
	ModelToolsCliResult result;
	QJsonArray files;
	result.lines << (dry ? Text::tr("Detail levels validated; no files written.") : Text::tr("Wrote %n model file(s).", nullptr, int(outputs.size())));
	for (const auto &entry : std::as_const(outputs))
	{
		QJsonArray notes;
		for (const auto &note : entry.notes)
			notes.append(note);
		files.append(QJsonObject{{QStringLiteral("path"), QFileInfo(entry.path).absoluteFilePath()},
								 {QStringLiteral("level"), entry.level},
								 {QStringLiteral("triangles"), entry.triangles},
								 {QStringLiteral("bytes"), qint64(entry.bytes.size())},
								 {QStringLiteral("notes"), notes}});
		result.lines << Text::tr("Level %1: %2 triangles · %3").arg(entry.level).arg(entry.triangles).arg(entry.path);
		result.lines += entry.notes;
	}
	result.payload = {{QStringLiteral("source"), QFileInfo(source).absoluteFilePath()},
					  {QStringLiteral("levels"), levels},
					  {QStringLiteral("ratio"), ratio},
					  {QStringLiteral("outputs"), files},
					  {QStringLiteral("written"), !dry},
					  {QStringLiteral("dryRun"), dry}};
	return result;
}
} // namespace vibestudio::cli
