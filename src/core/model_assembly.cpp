#include "core/model_assembly.h"

#include "core/model_archive.h"
#include "core/model_tags.h"
#include "core/package_archive.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace vibestudio
{
namespace
{
bool fail(QString *error, const QString &message)
{
	if (error)
	{
		*error = message;
	}
	return false;
}
bool finite(ModelVec3 point)
{
	return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z) && std::abs(point.x) <= 1000000 &&
		   std::abs(point.y) <= 1000000 && std::abs(point.z) <= 1000000;
}
QJsonArray vector(ModelVec3 v)
{
	return {v.x, v.y, v.z};
}
bool parseVector(const QJsonValue &value, ModelVec3 *out)
{
	if (!value.isArray() || value.toArray().size() != 3)
	{
		return false;
	}
	const auto a = value.toArray();
	for (const auto &item : a)
	{
		if (!item.isDouble() || !std::isfinite(item.toDouble()))
		{
			return false;
		}
	}
	*out = {float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble())};
	return finite(*out);
}
bool keys(const QJsonObject &value, const QSet<QString> &allowed)
{
	for (auto it = value.begin(); it != value.end(); ++it)
	{
		if (!allowed.contains(it.key()))
		{
			return false;
		}
	}
	return true;
}
ModelVec3 direction(const ModelTag &basis, ModelVec3 v)
{
	auto rotation = basis;
	rotation.origin = {};
	return modelTagPoint(rotation, v);
}
ModelVec3 scaled(ModelVec3 v, double scale)
{
	return {float(v.x * scale), float(v.y * scale), float(v.z * scale)};
}
ModelTag combine(const ModelTag &parent, double parentScale, const ModelTag &local)
{
	auto result = local;
	result.origin = modelTagPoint(parent, scaled(local.origin, parentScale));
	for (int axis = 0; axis < 3; ++axis)
	{
		const auto v = direction(parent, {local.axis[axis * 3], local.axis[axis * 3 + 1], local.axis[axis * 3 + 2]});
		result.axis[axis * 3] = v.x;
		result.axis[axis * 3 + 1] = v.y;
		result.axis[axis * 3 + 2] = v.z;
	}
	return result;
}
bool reflected(const ModelTag &tag)
{
	const auto *a = tag.axis;
	return double(a[0]) * (double(a[4]) * a[8] - double(a[5]) * a[7]) - double(a[1]) * (double(a[3]) * a[8] - double(a[5]) * a[6]) +
			   double(a[2]) * (double(a[3]) * a[7] - double(a[4]) * a[6]) <
		   0;
}
bool animationSample(const ModelAssemblyPart &part, const ModelMesh &mesh, double seconds, ModelAnimationSample *sample)
{
	const int last = part.lastFrame < 0 ? int(mesh.frames.size()) - 1 : part.lastFrame;
	if (part.firstFrame < 0 || last < part.firstFrame || last >= mesh.frames.size())
	{
		return false;
	}
	const double offset = seconds * part.framesPerSecond + part.phase;
	if (!std::isfinite(offset))
	{
		return false;
	}
	if (!part.loop && offset >= last - part.firstFrame)
	{
		*sample = {last, last, 0};
		return true;
	}
	return sampleModelAnimation(part.firstFrame, last - part.firstFrame + 1, offset, part.interpolate, sample);
}
bool assemblySample(const ModelAssembly &assembly, const ModelAssemblyPart &part, const ModelMesh &mesh, double seconds,
					ModelAnimationSample *sample)
{
	if (assembly.q3Animation)
	{
		const auto &binding = *assembly.q3Animation;
		const int slot = part.id == binding.lowerPart ? binding.lowerAnimation : part.id == binding.upperPart ? binding.upperAnimation : -1;
		if (slot >= 0)
			return sampleModelQ3Animation(binding.config, slot, seconds, part.interpolate, sample);
	}
	return animationSample(part, mesh, seconds, sample);
}
} // namespace

bool validateModelAssembly(const ModelAssembly &assembly, QString *error, QVector<int> *order)
{
	if (error)
	{
		error->clear();
	}
	if (assembly.name.trimmed().isEmpty() || assembly.name.size() > 128 || assembly.name.contains(QChar(0)) ||
		assembly.parts.size() > modelAssemblyMaxParts)
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelAssembly",
													   "An assembly needs a name of at most 128 characters and at most 32 parts."));
	}
	const QRegularExpression identifier(QStringLiteral("^[A-Za-z][A-Za-z0-9_-]{0,31}$"));
	QHash<QString, int> ids;
	QSet<QString> portableIds;
	int roots = 0;
	for (int i = 0; i < assembly.parts.size(); ++i)
	{
		const auto &part = assembly.parts[i];
		if (!identifier.match(part.id).hasMatch() || portableIds.contains(part.id.toCaseFolded()) || part.source.trimmed().isEmpty() ||
			part.source.size() > 4096 || part.source.contains(QChar(0)) || part.tag.size() > 63 || part.tag.contains(QChar(0)) ||
			(part.sourceKind != ModelAssemblySource::File && part.sourceKind != ModelAssemblySource::Package))
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelAssembly",
														   "Use unique part IDs of 1–32 ASCII letters, digits, underscores or hyphens, "
														   "starting with a letter, and valid model paths."));
		}
		if (part.sourceKind == ModelAssemblySource::Package &&
			(part.source.startsWith('/') || part.source.contains('\\') || part.source.contains(':') ||
			 part.source.split('/').contains(QStringLiteral("..")) || QDir::cleanPath(part.source) != part.source))
		{
			return fail(error,
						QCoreApplication::translate("VibeStudioModelAssembly",
													"Package model references must use normalized relative paths with forward slashes."));
		}
		if (part.skin && !validateModelAssemblySkin(*part.skin, error))
			return false;
		if (!finite(part.translation) || !finite(part.rotation) || !std::isfinite(part.scale) || part.scale < .000001 ||
			part.scale > 1000 || part.firstFrame < 0 || part.firstFrame >= modelDocumentMaxFrames || part.lastFrame < -1 ||
			part.lastFrame >= modelDocumentMaxFrames || (part.lastFrame >= 0 && part.lastFrame < part.firstFrame) ||
			!std::isfinite(part.framesPerSecond) || part.framesPerSecond < 0 || part.framesPerSecond > 1000 || !std::isfinite(part.phase) ||
			part.phase < 0 || part.phase > 1000000)
		{
			return fail(error, QCoreApplication::translate(
								   "VibeStudioModelAssembly",
								   "Part transforms and animation settings must be finite and within their supported ranges."));
		}
		if (part.parent.isEmpty())
		{
			++roots;
			if (!part.tag.isEmpty())
			{
				return fail(error, QCoreApplication::translate("VibeStudioModelAssembly", "The root part cannot name a parent tag."));
			}
		}
		else if (part.tag.isEmpty())
		{
			return fail(error,
						QCoreApplication::translate("VibeStudioModelAssembly", "Attached parts need a parent part and an attachment tag."));
		}
		ids.insert(part.id, i);
		portableIds.insert(part.id.toCaseFolded());
	}
	if (!assembly.parts.isEmpty() && roots != 1)
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelAssembly", "An assembly must have exactly one root part."));
	}
	if (assembly.q3Animation)
	{
		const auto &binding = *assembly.q3Animation;
		if (!ids.contains(binding.lowerPart) || !ids.contains(binding.upperPart) || binding.lowerPart == binding.upperPart ||
			!modelQ3AnimationUsesLower(binding.lowerAnimation) || !modelQ3AnimationUsesUpper(binding.upperAnimation))
			return fail(error,
						QCoreApplication::translate(
							"VibeStudioModelAssembly",
							"Native animation needs distinct existing lower and upper parts and matching lower/upper animation slots."));
		if (!validateModelQ3Animation(binding.config, error))
			return false;
	}
	QVector<int> result, state(assembly.parts.size());
	std::function<bool(int)> visit = [&](int index) {
		if (state[index] == 2)
		{
			return true;
		}
		if (state[index] == 1)
		{
			return false;
		}
		state[index] = 1;
		const auto &parent = assembly.parts[index].parent;
		if (!parent.isEmpty() && (!ids.contains(parent) || !visit(ids.value(parent))))
		{
			return false;
		}
		state[index] = 2;
		result.append(index);
		return true;
	};
	for (int i = 0; i < state.size(); ++i)
	{
		if (!visit(i))
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelAssembly",
														   "Every parent must exist, and attachment links cannot form a cycle."));
		}
	}
	if (order)
	{
		*order = std::move(result);
	}
	return true;
}
QJsonObject modelAssemblyJson(const ModelAssembly &assembly)
{
	QJsonArray parts;
	bool hasSkins = false;
	for (const auto &part : assembly.parts)
	{
		QJsonObject node{{"id", part.id},
						 {"source", part.source},
						 {"kind", part.sourceKind == ModelAssemblySource::File ? "file" : "package"},
						 {"parent", part.parent},
						 {"tag", part.tag},
						 {"translation", vector(part.translation)},
						 {"rotation", vector(part.rotation)},
						 {"scale", part.scale},
						 {"firstFrame", part.firstFrame},
						 {"lastFrame", part.lastFrame},
						 {"fps", part.framesPerSecond},
						 {"phase", part.phase},
						 {"loop", part.loop},
						 {"interpolate", part.interpolate}};
		if (part.skin)
		{
			hasSkins = true;
			node.insert("skin", QJsonObject{{"source", part.skin->source},
											{"kind", part.skin->sourceKind == ModelAssemblySource::File ? "file" : "package"},
											{"entryIndex", part.skin->entryIndex}});
		}
		parts.append(node);
	}
	QJsonObject result{{"format", "vibestudio-model-assembly"},
					   {"version", hasSkins				  ? 3
								   : assembly.q3Animation ? 2
														  : 1},
					   {"name", assembly.name},
					   {"parts", parts}};
	if (assembly.q3Animation)
	{
		const auto &binding = *assembly.q3Animation;
		result.insert("q3Animation", QJsonObject{{"lowerPart", binding.lowerPart},
												 {"upperPart", binding.upperPart},
												 {"lowerAnimation", modelQ3AnimationName(binding.lowerAnimation)},
												 {"upperAnimation", modelQ3AnimationName(binding.upperAnimation)},
												 {"config", QString::fromLatin1(exportModelQ3Animation(binding.config))}});
	}
	return result;
}
QByteArray modelAssemblyFingerprint(const ModelAssembly &assembly)
{
	return QCryptographicHash::hash(QJsonDocument(modelAssemblyJson(assembly)).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256);
}
bool parseModelAssembly(const QByteArray &bytes, ModelAssembly *assembly, QString *error)
{
	if (error)
	{
		error->clear();
	}
	const auto malformed = [&] {
		return fail(error, QCoreApplication::translate("VibeStudioModelAssembly",
													   "The assembly source is malformed, unsupported or exceeds 1 MiB."));
	};
	if (!assembly || bytes.size() > modelAssemblyMaxSourceBytes)
	{
		return malformed();
	}
	QJsonParseError parse;
	const auto document = QJsonDocument::fromJson(bytes, &parse);
	const auto object = document.object();
	const auto version = object.value("version").toDouble(-1);
	if (parse.error != QJsonParseError::NoError || !document.isObject() ||
		!keys(object, {"format", "version", "name", "parts", "q3Animation"}) ||
		object.value("format") != QStringLiteral("vibestudio-model-assembly") || (version != 1 && version != 2 && version != 3) ||
		(version == 1 && object.contains("q3Animation")) || (version == 2 && !object.value("q3Animation").isObject()) ||
		(object.contains("q3Animation") && !object.value("q3Animation").isObject()) || !object.value("name").isString() ||
		!object.value("parts").isArray() || object.value("parts").toArray().size() > modelAssemblyMaxParts)
	{
		return malformed();
	}
	ModelAssembly result;
	result.name = object.value("name").toString();
	if (object.contains("q3Animation"))
	{
		const auto node = object.value("q3Animation").toObject();
		if (!keys(node, {"lowerPart", "upperPart", "lowerAnimation", "upperAnimation", "config"}))
			return malformed();
		for (const auto &key : {"lowerPart", "upperPart", "lowerAnimation", "upperAnimation", "config"})
			if (!node.value(key).isString())
				return malformed();
		ModelAssemblyQ3Animation binding;
		binding.lowerPart = node.value("lowerPart").toString();
		binding.upperPart = node.value("upperPart").toString();
		binding.lowerAnimation = modelQ3AnimationSlot(node.value("lowerAnimation").toString());
		binding.upperAnimation = modelQ3AnimationSlot(node.value("upperAnimation").toString());
		const auto config = node.value("config").toString();
		if (QString::fromLatin1(config.toLatin1()) != config)
			return malformed();
		if (!parseModelQ3Animation(config.toLatin1(), &binding.config, error))
			return false;
		result.q3Animation = std::move(binding);
	}
	for (const auto &value : object.value("parts").toArray())
	{
		if (!value.isObject())
		{
			return malformed();
		}
		const auto node = value.toObject();
		if (!keys(node, {"id", "source", "kind", "parent", "tag", "translation", "rotation", "scale", "firstFrame", "lastFrame", "fps",
						 "phase", "loop", "interpolate", "skin"}) ||
			(version < 3 && node.contains("skin")))
		{
			return malformed();
		}
		ModelAssemblyPart part;
		for (const auto &key : {"id", "source", "kind", "parent", "tag"})
		{
			if (!node.value(key).isString())
			{
				return malformed();
			}
		}
		part.id = node.value("id").toString();
		part.source = node.value("source").toString();
		part.parent = node.value("parent").toString();
		part.tag = node.value("tag").toString();
		const auto kind = node.value("kind").toString();
		if (kind != "file" && kind != "package")
		{
			return malformed();
		}
		part.sourceKind = kind == "file" ? ModelAssemblySource::File : ModelAssemblySource::Package;
		if (!parseVector(node.value("translation"), &part.translation) || !parseVector(node.value("rotation"), &part.rotation))
		{
			return malformed();
		}
		for (const auto &key : {"scale", "firstFrame", "lastFrame", "fps", "phase"})
		{
			if (!node.value(key).isDouble())
			{
				return malformed();
			}
		}
		for (const auto &key : {"firstFrame", "lastFrame"})
		{
			const double number = node.value(key).toDouble();
			if (!std::isfinite(number) || std::floor(number) != number || number < -1 || number > modelDocumentMaxFrames)
			{
				return malformed();
			}
		}
		if (!node.value("loop").isBool() || !node.value("interpolate").isBool())
		{
			return malformed();
		}
		part.scale = node.value("scale").toDouble();
		part.firstFrame = node.value("firstFrame").toInt();
		part.lastFrame = node.value("lastFrame").toInt();
		part.framesPerSecond = node.value("fps").toDouble();
		part.phase = node.value("phase").toDouble();
		part.loop = node.value("loop").toBool();
		part.interpolate = node.value("interpolate").toBool();
		if (node.contains("skin"))
		{
			const auto skin = node.value("skin").toObject();
			const double index = skin.value("entryIndex").toDouble(-2);
			if (!node.value("skin").isObject() || !keys(skin, {"source", "kind", "entryIndex"}) || !skin.value("source").isString() ||
				(skin.value("kind") != "file" && skin.value("kind") != "package") || !skin.value("entryIndex").isDouble() ||
				!std::isfinite(index) || std::floor(index) != index || index < -1 || index > std::numeric_limits<int>::max())
				return malformed();
			part.skin =
				ModelAssemblySkin{skin.value("source").toString(),
								  skin.value("kind") == "file" ? ModelAssemblySource::File : ModelAssemblySource::Package, int(index)};
		}
		result.parts.append(part);
	}
	if (!validateModelAssembly(result, error))
	{
		return false;
	}
	*assembly = std::move(result);
	return true;
}

bool resolveModelAssembly(const ModelAssembly &assembly, const ModelAssemblyContext &context, ModelAssemblyResolved *resolved,
						  QString *error, const ModelWorkControl &control)
{
	ModelAssemblyResolved result;
	if (!resolved || !validateModelAssembly(assembly, error, &result.order) ||
		!modelWorkCheckpoint(control, ModelWorkPhase::Reading, 0, 0, error))
	{
		return false;
	}
	if (assembly.parts.isEmpty())
	{
		return fail(error,
					QCoreApplication::translate("VibeStudioModelAssembly", "Add a root model before previewing or baking an assembly."));
	}
	result.recipeSha256 = modelAssemblyFingerprint(assembly);
	qint64 readBytes = 0, frameVertices = 0, vertices = 0, triangles = 0, surfaces = 0;
	for (const auto &part : assembly.parts)
	{
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, result.inputs.size(), assembly.parts.size(), error))
		{
			return false;
		}
		const auto inputFailure = [&]() {
			return fail(error, QCoreApplication::translate("VibeStudioModelAssembly", "Part %1 (%2): %3")
								   .arg(part.id, part.source, error ? *error : QString()));
		};
		ModelAssemblyInput input;
		input.part = part.id;
		input.sourceKind = part.sourceKind;
		QByteArray bytes;
		IdTechPaletteResolution palette;
		if (part.sourceKind == ModelAssemblySource::File)
		{
			if (!QFileInfo(part.source).isAbsolute() && context.directory.isEmpty())
			{
				return fail(error, QCoreApplication::translate("VibeStudioModelAssembly",
															   "Relative model references need an assembly source directory."));
			}
			input.source = QDir::cleanPath(QDir(context.directory).absoluteFilePath(part.source));
			if (QFileInfo(input.source).size() > modelAssemblyMaxInputBytes - readBytes)
			{
				return fail(
					error, QCoreApplication::translate("VibeStudioModelAssembly", "Assembly model inputs exceed the 256 MiB read budget."));
			}
			if (!readModelFile(input.source, &bytes, error, control))
			{
				return inputFailure();
			}
		}
		else
		{
			if (!context.archive || !context.archive->isOpen())
			{
				return fail(error, QCoreApplication::translate("VibeStudioModelAssembly",
															   "Open the package used by this assembly before loading package parts."));
			}
			input.source = part.source;
			const ModelArchiveReader reader(*context.archive, control);
			if (!reader.readEntryBytes(part.source, &bytes, error, std::min(modelFileByteLimit, modelAssemblyMaxInputBytes - readBytes)))
			{
				return inputFailure();
			}
			if (detectModelMeshFormat(part.source, bytes) == ModelMeshFormat::QuakeMdl)
			{
				palette = resolveIdTechPalette(reader, context.paletteId.isEmpty() ? QStringLiteral("quake") : context.paletteId);
			}
		}
		readBytes += bytes.size();
		if (readBytes > modelAssemblyMaxInputBytes)
		{
			return fail(error,
						QCoreApplication::translate("VibeStudioModelAssembly", "Assembly model inputs exceed the 256 MiB read budget."));
		}
		input.bytes = bytes.size();
		input.sha256 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
		if (!importEditableModel(part.source, bytes, &input.mesh, error, palette.palette.isValid() ? &palette.palette : nullptr, control))
		{
			return inputFailure();
		}
		if (part.skin)
		{
			if (!resolveModelAssemblySkin(*part.skin, context, &input, error, control))
				return fail(error, QCoreApplication::translate("VibeStudioModelAssembly", "Part %1 skin (%2): %3")
									   .arg(part.id, part.skin->source, error ? *error : QString()));
			readBytes += input.skin->bytes;
			if (readBytes > modelAssemblyMaxInputBytes)
				return fail(error, QCoreApplication::translate("VibeStudioModelAssembly",
															   "Assembly model and skin inputs exceed the 256 MiB read budget."));
			result.notes << QCoreApplication::translate("VibeStudioModelAssembly",
														"Part %1 uses linked skin %2. Baking writes its material assignments into the "
														"composite; input files stay unchanged.")
								.arg(part.id, input.skin->source);
		}
		vertices += input.mesh.vertexCount;
		triangles += input.mesh.triangleCount;
		surfaces += input.mesh.surfaces.size();
		frameVertices += qint64(input.mesh.vertexCount) * input.mesh.frames.size();
		if (vertices > modelDocumentMaxVertices || triangles > modelDocumentMaxTriangles || surfaces > 32 ||
			frameVertices > 4 * modelDocumentMaxFrameVertices)
		{
			return fail(error,
						QCoreApplication::translate(
							"VibeStudioModelAssembly",
							"The assembly exceeds 32 surfaces, 65,536 vertices, 131,072 triangles or 4,194,304 stored frame vertices."));
		}
		ModelAnimationSample sample;
		if (!assemblySample(assembly, part, input.mesh, 0, &sample))
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelAssembly",
														   "Part %1 refers to animation frames that are not in its model.")
								   .arg(part.id));
		}
		result.notes += palette.warnings;
		result.inputs.append(std::move(input));
	}
	if (!validateModelAssemblyQ3Animation(assembly, result, error))
		return false;
	if (assembly.q3Animation)
		result.notes << QCoreApplication::translate(
			"VibeStudioModelAssembly",
			"Quake III clip playback uses native frame offsets, reversed ranges, loop tails and integer millisecond periods. "
			"Bound parts use the selected native clips instead of their ordinary range, FPS, phase and loop settings. "
			"Head offset, footsteps, sex and fixed-body flags are preserved for export; game-state transitions, haste and procedural body "
			"aiming are not simulated.");
	for (const auto &part : assembly.parts)
	{
		if (part.parent.isEmpty())
		{
			continue;
		}
		const auto parent =
			std::find_if(result.inputs.cbegin(), result.inputs.cend(), [&](const auto &input) { return input.part == part.parent; });
		if (parent == result.inputs.cend() || !findModelTag(parent->mesh, part.tag, 0))
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelAssembly", "Parent %1 has no attachment tag named %2.")
								   .arg(part.parent, part.tag));
		}
	}
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, result.inputs.size(), result.inputs.size(), error))
	{
		return false;
	}
	*resolved = std::move(result);
	return true;
}

bool sampleModelAssembly(const ModelAssembly &assembly, const ModelAssemblyResolved &resolved, double seconds, ModelAssemblyPose *pose,
						 QString *error, const ModelWorkControl &control)
{
	QVector<int> order;
	if (!pose || !validateModelAssembly(assembly, error, &order))
	{
		return false;
	}
	if (!std::isfinite(seconds) || seconds < 0 || seconds > 1000000 || assembly.parts.isEmpty() ||
		resolved.inputs.size() != assembly.parts.size() || resolved.recipeSha256 != modelAssemblyFingerprint(assembly))
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelAssembly",
													   "Reload the assembly inputs and choose a time from 0 to 1,000,000 seconds."));
	}
	if (!validateModelAssemblyQ3Animation(assembly, resolved, error))
		return false;
	ModelWorkProgress progress(control, ModelWorkPhase::Editing, error);
	if (!progress.check())
	{
		return false;
	}
	ModelAssemblyPose result;
	result.samples.resize(assembly.parts.size());
	result.partTransforms.resize(assembly.parts.size());
	QVector<double> scales(assembly.parts.size(), 1);
	QHash<QString, int> indices;
	for (int i = 0; i < assembly.parts.size(); ++i)
	{
		indices.insert(assembly.parts[i].id, i);
	}
	result.mesh.geometryAvailable = true;
	result.mesh.sourcePath = assembly.name;
	result.mesh.formatId = QStringLiteral("mesh");
	result.mesh.formatName = QStringLiteral("VibeStudio Mesh");
	ModelFrameInfo frame;
	frame.name = QStringLiteral("assembly_pose");
	result.mesh.frames.append(frame);
	for (int index : order)
	{
		const auto &part = assembly.parts[index];
		const auto &input = resolved.inputs[index];
		const auto &mesh = input.mesh;
		auto &sample = result.samples[index];
		if (input.part != part.id || !assemblySample(assembly, part, mesh, seconds, &sample))
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelAssembly",
														   "The resolved part or its animation range no longer matches the assembly."));
		}
		ModelTag basis;
		double parentScale = 1;
		if (!part.parent.isEmpty())
		{
			const int parent = indices.value(part.parent);
			const auto &parentMesh = resolved.inputs[parent].mesh;
			const auto parentSample = result.samples[parent];
			const auto *first = findModelTag(parentMesh, part.tag, parentSample.frame),
					   *next = findModelTag(parentMesh, part.tag, parentSample.nextFrame);
			ModelTag sampled;
			if (first && parentSample.fraction == 0)
			{
				sampled = *first;
			}
			else if (!first || !next || !interpolateModelTag(*first, *next, parentSample.fraction, &sampled))
			{
				return fail(error, QCoreApplication::translate(
									   "VibeStudioModelAssembly",
									   "The parent attachment cannot be interpolated. Use stored poses or repair its orientation."));
			}
			parentScale = scales[parent];
			basis = combine(result.partTransforms[parent], parentScale, sampled);
		}
		ModelTag local;
		local.origin = part.translation;
		for (int axis = 0; axis < 3; ++axis)
		{
			ModelVec3 unit;
			if (axis == 0)
			{
				unit.x = 1;
			}
			else if (axis == 1)
			{
				unit.y = 1;
			}
			else
			{
				unit.z = 1;
			}
			const auto rotated = rotateModelVector(unit, part.rotation);
			local.axis[axis * 3] = rotated.x;
			local.axis[axis * 3 + 1] = rotated.y;
			local.axis[axis * 3 + 2] = rotated.z;
		}
		auto transform = combine(basis, parentScale, local);
		transform.name = part.id;
		result.partTransforms[index] = transform;
		scales[index] = parentScale * part.scale;
		if (!std::isfinite(scales[index]) || scales[index] < .000001 || scales[index] > 1000000 || !finite(transform.origin))
		{
			return fail(error, QCoreApplication::translate(
								   "VibeStudioModelAssembly",
								   "The accumulated attachment transform exceeds the supported scale or coordinate range."));
		}
		for (int surfaceIndex = 0; surfaceIndex < mesh.surfaces.size(); ++surfaceIndex)
		{
			const auto &original = mesh.surfaces[surfaceIndex];
			if (sample.frame >= original.frames.size() || sample.nextFrame >= original.frames.size())
			{
				return fail(
					error,
					QCoreApplication::translate("VibeStudioModelAssembly", "Part %1 has inconsistent surface frame counts.").arg(part.id));
			}
			ModelSurface surface = original;
			// Quake III strips a final underscore plus one character from MD3
			// surface names. Keep a letter before the index so sibling surfaces
			// retain distinct native matching identities (see Model Skin Bindings).
			surface.name = part.id + QStringLiteral("_s") + QString::number(surfaceIndex);
			const auto &first = original.frames[sample.frame], &next = original.frames[sample.nextFrame];
			ModelFrameGeometry geometry;
			geometry.positions.reserve(original.vertexCount);
			geometry.normals.reserve(original.vertexCount);
			if (first.positions.size() != original.vertexCount || next.positions.size() != original.vertexCount ||
				first.normals.size() != original.vertexCount || next.normals.size() != original.vertexCount)
			{
				return fail(error,
							QCoreApplication::translate("VibeStudioModelAssembly", "Part %1 has inconsistent position or normal arrays.")
								.arg(part.id));
			}
			for (int vertex = 0; vertex < original.vertexCount; ++vertex)
			{
				if (!progress.step())
				{
					return false;
				}
				const auto position = modelTagPoint(
					transform,
					scaled(interpolateModelPosition(first.positions[vertex], next.positions[vertex], sample.fraction), scales[index]));
				ModelVec3 normal;
				if (!finite(position) || !interpolateModelNormal(first.normals[vertex], next.normals[vertex], sample.fraction, &normal))
				{
					return fail(error, QCoreApplication::translate(
										   "VibeStudioModelAssembly",
										   "A composed vertex or normal is invalid. Use stored poses or adjust the part transform."));
				}
				geometry.positions.append(position);
				geometry.normals.append(direction(transform, normal));
			}
			surface.frames = {std::move(geometry)};
			if (reflected(transform))
			{
				for (auto &triangle : surface.triangles)
				{
					std::swap(triangle.b, triangle.c);
				}
			}
			if (!mesh.embeddedSkins.isEmpty() && !mesh.embeddedSkins.first().image.isNull())
			{
				result.embeddedSkins.insert(result.mesh.surfaces.size(), mesh.embeddedSkins.first().image);
			}
			result.mesh.surfaces.append(std::move(surface));
			result.surfaceParts.append(index);
		}
	}
	updateEditableModelMetadata(&result.mesh);
	const auto issues = validateEditableModel(result.mesh, control);
	if (!issues.isEmpty())
	{
		return fail(error, issues.join(QLatin1Char('\n')));
	}
	result.notes = resolved.notes;
	if (std::any_of(resolved.inputs.cbegin(), resolved.inputs.cend(),
					[](const auto &input) { return !input.mesh.collisionBoxes.isEmpty(); }))
	{
		result.notes << QCoreApplication::translate("VibeStudioModelAssembly",
													"Input collision boxes remain in their model sources. Assembly baking does not compose "
													"or export them; author collision on the baked pose when needed.");
	}
	result.notes << QCoreApplication::translate("VibeStudioModelAssembly",
												"This bake contains one composed pose. Keep the assembly and model sources to retain "
												"links, animation, attachment tags and native skin metadata.");
	if (!result.embeddedSkins.isEmpty())
	{
		result.notes << QCoreApplication::translate(
			"VibeStudioModelAssembly",
			"Embedded skins are previewed from their first image; baking does not write those texture images or native skin animation.");
	}
	if (!progress.check())
	{
		return false;
	}
	*pose = std::move(result);
	return true;
}

bool exportModelAssemblyPose(const ModelAssembly &assembly, const ModelAssemblyResolved &resolved, double seconds, const QString &path,
							 const QString &sourcePath, bool overwrite, bool dryRun, QString *error, const ModelWorkControl &control,
							 ModelExportReport *report, const std::shared_ptr<const PackageArchiveReader> &archive)
{
	if (error)
	{
		error->clear();
	}
	if (report)
	{
		*report = {};
	}
	const auto target = inspectModelWriteTarget(path, control);
	if (!target.isValid())
	{
		return fail(error, target.error);
	}
	bool protectedInput = modelPathsReferToSameFile(path, sourcePath) ||
						  (archive && (archive->protectsInputPath(target.path) || archive->protectsInputPath(target.resolvedPath)));
	for (const auto &input : resolved.inputs)
	{
		protectedInput |= modelAssemblyInputProtectsPath(input, path);
	}
	if (protectedInput || (target.existed && !overwrite))
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelAssembly",
													   "Choose a separate output; assembly, model and package inputs are protected, and "
													   "replacing another output requires explicit overwrite."));
	}
	ModelAssemblyPose pose;
	if (!sampleModelAssembly(assembly, resolved, seconds, &pose, error, control))
	{
		return false;
	}
	ModelExportReport details;
	const auto bytes = exportEditableModel(pose.mesh, QFileInfo(path).suffix(), 0, error, control, &details);
	if (bytes.isEmpty() || (!dryRun && !writeModelFile(target, bytes, error, control)))
	{
		return false;
	}
	details.notes += pose.notes;
	if (report)
	{
		*report = std::move(details);
	}
	return true;
}
} // namespace vibestudio
