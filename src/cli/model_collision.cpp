#include "cli/model_collision.h"
#include "cli/model_transform_options.h"
#include "core/model_collision.h"
#include "core/model_document.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <cmath>
#include <tuple>

namespace vibestudio::cli
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelCollisionCli)
};
ModelCollisionCliResult fail(int code, const QString &error)
{
	return {code, error, {}, {}};
}
bool vector(const QString &text, ModelVec3 *value)
{
	const auto parts = text.split(',');
	if (parts.size() != 3)
	{
		return false;
	}
	float values[3]{};
	for (int i = 0; i < 3; ++i)
	{
		bool ok = false;
		const auto number = parts[i].toDouble(&ok);
		if (!ok || !std::isfinite(number) || std::abs(number) > 1000000)
		{
			return false;
		}
		values[i] = float(number);
	}
	*value = {values[0], values[1], values[2]};
	return true;
}
bool integer(const QString &text, int *value)
{
	static const QRegularExpression digits(QStringLiteral("^[0-9]+$"));
	bool ok = false;
	*value = text.toInt(&ok);
	return ok && digits.match(text).hasMatch();
}
QString vectorText(ModelVec3 value)
{
	return QStringLiteral("%1,%2,%3").arg(value.x, 0, 'g', 9).arg(value.y, 0, 'g', 9).arg(value.z, 0, 'g', 9);
}
} // namespace
ModelCollisionCliResult runModelCollision(const QStringList &arguments)
{
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--overwrite", "--dry-run"};
	const QSet<QString> globals{"--settings-file", "--locale", "--catalog-root"};
	const QSet<QString> options{"--input",		"--operation",		 "--output",		"--box",		"--name",	   "--centre",
								"--size",		"--rotation",		 "--frame",			"--surface",	"--vertices",  "--faces",
								"--edges",		"--target",			 "--material",		"--origin",		"--map",	   "--offset",
								"--rotate",		"--scale",			 "--pivot",			"--pivot-mode", "--snap-grid", "--snap-angle",
								"--snap-scale", "--transform-space", "--axis-rotation", "--axes-frame"};
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional;
	for (int i = 1; i < arguments.size(); ++i)
	{
		const auto arg = arguments[i];
		if (!arg.startsWith('-'))
		{
			positional << arg;
			continue;
		}
		const auto equal = arg.indexOf('=');
		const auto key = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(key))
		{
			return fail(2, Text::tr("Repeated option: %1").arg(key));
		}
		seen.insert(key);
		if (flags.contains(key) && equal < 0)
		{
			continue;
		}
		if (!options.contains(key) && !globals.contains(key))
		{
			return fail(2, Text::tr("Unexpected option: %1").arg(arg));
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
	if (positional.size() != (values.contains("--input") ? 2 : 3))
	{
		return fail(2, Text::tr("Use model collision <source.mesh.json> with one explicit input source."));
	}
	const auto operation = values.value("--operation", "inspect");
	const bool exportMap = operation == "export-map", place = operation == "place";
	const bool transform = operation == "transform";
	const bool edit =
		QSet<QString>{"add", "fit", "fit-animated", "animate", "freeze", "update", "duplicate", "delete", "transform"}.contains(operation);
	if (!edit && !exportMap && !place && operation != "inspect")
	{
		return fail(
			2,
			Text::tr("Use inspect, add, fit, fit-animated, animate, freeze, update, transform, duplicate, delete, export-map or place."));
	}
	QSet<QString> allowed{"--input", "--operation", "--cli", "--json", "--quiet", "--verbose"};
	allowed.unite(globals);
	if (operation != "inspect")
	{
		allowed.unite({"--output", "--overwrite", "--dry-run"});
	}
	if (operation == "add" || operation == "update")
	{
		allowed.unite({"--name", "--centre", "--size", "--rotation"});
	}
	if (operation == "fit" || operation == "fit-animated")
	{
		allowed.unite({"--name", "--surface", "--vertices", "--faces", "--edges"});
	}
	if (operation == "fit" || operation == "freeze" || operation == "update" || transform || exportMap || place || operation == "inspect")
		allowed << "--frame";
	if (operation == "duplicate")
	{
		allowed << "--name";
	}
	if (transform)
	{
		allowed.unite({"--offset", "--rotate", "--scale", "--pivot", "--pivot-mode", "--snap-grid", "--snap-angle", "--snap-scale",
					   "--transform-space", "--axis-rotation", "--axes-frame"});
	}
	if (operation == "update" || operation == "duplicate" || operation == "delete" || operation == "animate" || operation == "freeze" ||
		transform)
	{
		allowed << "--box";
	}
	if (exportMap || place)
	{
		allowed.unite({"--target", "--material", "--origin"});
	}
	if (place)
	{
		allowed << "--map";
	}
	if (!(seen - allowed).isEmpty())
	{
		return fail(2, Text::tr("Option does not apply to %1: %2").arg(operation, QStringList((seen - allowed).values()).join(", ")));
	}
	const auto source = values.value("--input", positional.value(2));
	const auto output = values.value("--output");
	if (operation != "inspect" && output.isEmpty())
	{
		return fail(2, Text::tr("This operation requires --output."));
	}
	if ((edit && !output.endsWith(".mesh.json", Qt::CaseInsensitive)) ||
		((exportMap || place) && !output.endsWith(".map", Qt::CaseInsensitive)))
	{
		return fail(2, Text::tr("Authoring outputs use .mesh.json; collision export and placement outputs use .map."));
	}
	const bool dry = seen.contains("--dry-run"), overwrite = seen.contains("--overwrite");
	QString error;
	ModelDocument document;
	if (!document.load(source, &error))
	{
		return fail(4, error);
	}
	int frame = -1;
	if (values.contains("--frame") && values.value("--frame") != "all" &&
		(!integer(values.value("--frame"), &frame) || frame >= document.mesh().frames.size()))
		return fail(2, Text::tr("Use an existing zero-based frame, or all where the operation supports it."));
	if ((operation == "freeze" || operation == "inspect" || exportMap || place) && values.contains("--frame") && frame < 0)
		return fail(2, Text::tr("This operation requires one stored frame, not all."));
	if (operation == "freeze" && frame < 0)
		return fail(2, Text::tr("Freeze requires --frame N to select the retained pose."));
	ModelCollisionCliResult result;
	result.payload = {{"operation", operation}, {"written", false}, {"dryRun", dry}};
	QByteArray bytes;
	if (edit)
	{
		ModelEdit change;
		change.kind = transform						? ModelEditKind::TransformCollisionBox
					  : operation == "fit-animated" ? ModelEditKind::FitAnimatedCollisionBox
					  : operation == "animate"		? ModelEditKind::AnimateCollisionBox
					  : operation == "freeze"		? ModelEditKind::FreezeCollisionBox
					  : operation == "add"			? ModelEditKind::AddCollisionBox
					  : operation == "fit"			? ModelEditKind::FitCollisionBox
					  : operation == "update"		? ModelEditKind::UpdateCollisionBox
					  : operation == "duplicate"	? ModelEditKind::DuplicateCollisionBox
													: ModelEditKind::DeleteCollisionBox;
		change.selection.collision = values.value("--box");
		change.frame = frame;
		if (operation == "update")
		{
			const auto box = findModelCollisionBox(document.mesh(), change.selection.collision);
			if (!box)
			{
				return fail(2, Text::tr("Choose an existing --box name."));
			}
			change.collisionBox = *box;
			change.collisionBox.framePoses.clear();
			change.collisionFields =
				(values.contains("--centre") ? 1 : 0) | (values.contains("--size") ? 2 : 0) | (values.contains("--rotation") ? 4 : 0);
		}
		const auto selected = findModelCollisionBox(document.mesh(), change.selection.collision);
		if (selected && selected->framePoses.isEmpty() && frame >= 0 && (transform || operation == "update"))
			return fail(2, Text::tr("A static box has no frame-local edit scope. Animate it first, or use all frames."));
		if (selected && !selected->framePoses.isEmpty() && (transform || (operation == "update" && change.collisionFields)) &&
			!seen.contains("--frame"))
			return fail(2, Text::tr("Animated collision edits require an explicit --frame all|N scope."));
		if (values.contains("--name"))
		{
			change.collisionBox.name = values.value("--name");
		}
		if (operation != "delete" && operation != "animate" && operation != "freeze" && !transform && change.collisionBox.name.isEmpty())
		{
			return fail(2, Text::tr("A new collision box requires --name."));
		}
		for (const auto &[key, field] :
			 std::array<std::pair<const char *, ModelVec3 *>, 3>{{{"--centre", &change.collisionBox.centre},
																  {"--size", &change.collisionBox.size},
																  {"--rotation", &change.collisionBox.rotation}}})
		{
			if (values.contains(key) && !vector(values.value(key), field))
			{
				return fail(2, Text::tr("Use three finite comma-separated numbers for %1.").arg(key));
			}
		}
		if (transform)
		{
			if (!parseModelTransformAxesOptions(values, int(document.mesh().frames.size()), &change, &error))
				return fail(2, error);
			for (const auto &[key, field] : std::array<std::pair<const char *, ModelVec3 *>, 4>{{{"--offset", &change.translation},
																								 {"--rotate", &change.rotation},
																								 {"--scale", &change.scale},
																								 {"--pivot", &change.pivot}}})
			{
				if (values.contains(key) && !vector(values.value(key), field))
				{
					return fail(2, Text::tr("Use three finite comma-separated numbers for %1.").arg(key));
				}
			}
			const auto pivot = values.value("--pivot-mode", values.contains("--pivot") ? "custom" : "selection");
			if (!QSet<QString>{"origin", "selection", "custom"}.contains(pivot) || (pivot != "custom" && values.contains("--pivot")) ||
				(pivot == "custom" && !values.contains("--pivot")))
			{
				return fail(2, Text::tr("Choose --pivot-mode selection|origin|custom; custom requires --pivot X,Y,Z."));
			}
			change.pivotMode = pivot == "selection" ? ModelTransformPivot::SelectionCentre
							   : pivot == "origin"	? ModelTransformPivot::Origin
													: ModelTransformPivot::Custom;
			for (const auto &[key, field] : std::array<std::pair<const char *, double *>, 3>{{{"--snap-grid", &change.translationGrid},
																							  {"--snap-angle", &change.rotationGrid},
																							  {"--snap-scale", &change.scaleGrid}}})
			{
				if (!values.contains(key))
				{
					continue;
				}
				bool valid = false;
				*field = values.value(key).toDouble(&valid);
				ModelVec3 checked;
				if (!valid || !snapModelTranslation({}, change.translationGrid, &checked) ||
					!snapModelRotation({}, change.rotationGrid, &checked) || !snapModelScale({1, 1, 1}, change.scaleGrid, &checked))
				{
					return fail(2, Text::tr("Use a valid transform snap step for %1.").arg(key));
				}
			}
		}
		if (operation == "fit" || operation == "fit-animated")
		{
			if (values.contains("--surface") && (!integer(values.value("--surface"), &change.selection.surface) ||
												 change.selection.surface >= document.mesh().surfaces.size()))
			{
				return fail(2, Text::tr("Choose an existing zero-based surface."));
			}
			const auto &surface = document.mesh().surfaces[change.selection.surface];
			for (const auto &[key, selected, limit] : std::array<std::tuple<const char *, QSet<int> *, int>, 2>{
					 {{"--vertices", &change.selection.vertices, surface.vertexCount},
					  {"--faces", &change.selection.faces, int(surface.triangles.size())}}})
			{
				if (!values.contains(key))
				{
					continue;
				}
				if (values.value(key) == "all")
				{
					for (int n = 0; n < limit; ++n)
					{
						selected->insert(n);
					}
					continue;
				}
				for (const auto &part : values.value(key).split(','))
				{
					int index = -1;
					if (!integer(part, &index) || index >= limit || selected->contains(index))
					{
						return fail(2, Text::tr("Use distinct existing component indices or all for %1.").arg(key));
					}
					selected->insert(index);
				}
			}
			if (values.contains("--edges"))
			{
				for (const auto &part : values.value("--edges").split(','))
				{
					const auto pair = part.split('-');
					int a = -1, b = -1;
					if (pair.size() != 2 || !integer(pair[0], &a) || !integer(pair[1], &b) || a >= b ||
						change.selection.edges.contains({a, b}))
					{
						return fail(2, Text::tr("Use distinct canonical edge pairs, for example --edges 0-1,1-2."));
					}
					change.selection.edges.insert({a, b});
				}
			}
			if (values.contains("--surface") && change.selection.vertices.isEmpty() && change.selection.faces.isEmpty() &&
				change.selection.edges.isEmpty())
			{
				return fail(2,
							Text::tr("Use --vertices all to fit one surface; without component selectors fitting spans the whole model."));
			}
		}
		if (!document.edit(change, &error))
		{
			return fail(4, error);
		}
		bytes = QJsonDocument(editableModelJson(document.mesh(), &error)).toJson(QJsonDocument::Compact);
	}
	else if (exportMap || place)
	{
		ModelCollisionExport request{values.value("--target"), values.value("--material"), {}, frame};
		if (!seen.contains("--target") || (values.contains("--origin") && !vector(values.value("--origin"), &request.origin)))
		{
			return fail(2, Text::tr("Use --target quake|quake2|quake3 and an optional finite --origin X,Y,Z."));
		}
		ModelCollisionMap map;
		if (!exportModelCollisionMap(document.mesh(), request, &map, &error))
		{
			return fail(4, error);
		}
		result.payload.insert("notes", QJsonArray::fromStringList(map.notes));
		result.payload.insert("brushes", map.brushCount);
		result.payload.insert("material", map.material);
		result.payload.insert("target", request.target);
		if (request.frame >= 0)
			result.payload.insert("frame", request.frame);
		result.lines += map.notes;
		bytes = map.bytes;
		if (place)
		{
			if (!values.contains("--map"))
			{
				return fail(2, Text::tr("Placement requires --map and a separate output .map path."));
			}
			LevelMapDocument sourceMap;
			if (!loadLevelMap({values.value("--map"), {}, request.target == "quake3" ? "idtech3" : "idtech2"}, &sourceMap, &error))
			{
				return fail(4, error);
			}
			const auto placed = prepareModelCollisionPlacement(document.mesh(), request, sourceMap);
			if (!placed.succeeded)
			{
				return fail(4, placed.error);
			}
			const auto serialized = serializeLevelMap(placed.document);
			if (!serialized.succeeded())
			{
				return fail(4, serialized.errors.join('\n'));
			}
			bytes = serialized.bytes;
		}
	}
	result.payload.insert("collisionBoxes", modelCollisionJson(document.mesh()));
	result.lines.prepend(Text::tr("%1 collision box(es).").arg(document.mesh().collisionBoxes.size()));
	for (const auto &box : document.mesh().collisionBoxes)
	{
		ModelCollisionBox pose;
		if (!sampleModelCollisionBox(box, std::max(0, frame), std::max(0, frame), 0, &pose))
			return fail(4, Text::tr("The requested collision pose is unavailable."));
		result.lines << Text::tr("%1: centre (%2), size (%3), rotation (%4) degrees.")
							.arg(box.name, vectorText(pose.centre), vectorText(pose.size), vectorText(pose.rotation));
		if (!box.framePoses.isEmpty())
			result.lines << Text::tr("Animated: %1 stored poses; displayed frame %2.").arg(box.framePoses.size()).arg(std::max(0, frame));
	}
	if (operation == "inspect" && frame >= 0)
	{
		ModelMesh sampled;
		for (const auto &box : document.mesh().collisionBoxes)
		{
			ModelCollisionBox pose;
			sampleModelCollisionBox(box, frame, frame, 0, &pose);
			sampled.collisionBoxes << pose;
		}
		result.payload.insert("frame", frame);
		result.payload.insert("sampledBoxes", modelCollisionJson(sampled));
	}
	if (operation == "inspect")
	{
		return result;
	}
	if (bytes.isEmpty() || !error.isEmpty())
	{
		return fail(4, error);
	}
	const auto target = inspectModelWriteTarget(output);
	if (!target.isValid())
	{
		return fail(4, target.error);
	}
	const bool current = edit && !document.path().isEmpty() && modelPathsReferToSameFile(output, document.path());
	if ((!current && modelPathsReferToSameFile(output, source)) || (place && modelPathsReferToSameFile(output, values.value("--map"))) ||
		(target.existed && !overwrite && !current))
	{
		return fail(4, Text::tr("Protect source inputs. Use a separate output; existing derivatives require --overwrite."));
	}
	if (current && target.sha256 != document.sourceFingerprint())
	{
		return fail(4, Text::tr("The source changed on disk. Reopen it before saving."));
	}
	if (!dry && !(edit ? document.save(output, overwrite, &error) : writeModelFile(target, bytes, &error)))
	{
		return fail(4, error);
	}
	result.payload.insert("outputPath", QFileInfo(output).absoluteFilePath());
	result.payload.insert("written", !dry);
	result.lines << (dry ? Text::tr("Validated without writing: %1") : Text::tr("Wrote: %1")).arg(output);
	return result;
}
} // namespace vibestudio::cli
