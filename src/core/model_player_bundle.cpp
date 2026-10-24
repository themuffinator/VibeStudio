#include "core/model_player_bundle.h"

#include "core/audio_delivery.h"
#include "core/model_archive.h"
#include "core/model_lod.h"
#include "core/model_skin_variants.h"
#include "core/model_targets.h"
#include "core/texture_export.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QtEndian>

#include <algorithm>
#include <array>

// Layout/tag contracts: id Software Quake III Arena cg_players.c,
// CG_RegisterClientModelname / CG_RegisterClientSkin / CG_FindClientModelFile /
// CG_LoadClientInfo (custom sounds) / CG_Player, cg_weapons.c CG_AddPlayerWeapon
// (tag_weapon), tr_model.c R_LoadMD3 (name_1.md3 and name_2.md3 detail levels)
// and snd_mem.c S_LoadSound (mono Microsoft PCM).
// GPL-2.0-or-later reference, reviewed 2026-10-06 and 2026-10-22; original implementation.
// https://github.com/id-Software/Quake-III-Arena/blob/master/code/cgame/cg_players.c
namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelPlayerBundle)
};
constexpr qint64 bundleLimit = 256 * 1024 * 1024;
constexpr qint64 soundLimit = 16 * 1024 * 1024;
constexpr int skinLimit = 16;
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
QByteArray hash(const QByteArray &bytes)
{
	return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}
QString hex(const QByteArray &bytes)
{
	return QString::fromLatin1(bytes.toHex());
}
ModelVec3 scaled(ModelVec3 p, double factor)
{
	return {float(p.x * factor), float(p.y * factor), float(p.z * factor)};
}
void protect(ModelPlayerBundle *bundle, const QString &path)
{
	if (path.isEmpty())
		return;
	bundle->protectedPaths << QFileInfo(path).absoluteFilePath();
	const auto canonical = QFileInfo(path).canonicalFilePath();
	if (!canonical.isEmpty())
		bundle->protectedPaths << canonical;
}
bool add(ModelPlayerBundle *bundle, QString path, QString role, QByteArray bytes, QString *error)
{
	if (bytes.isEmpty() || !isSafePackageVirtualPath(path) || packageFilesystemPathIssue(path) != PackagePathIssue::None ||
		path.contains('\\') || bytes.size() > bundleLimit - bundle->totalBytes || bundle->files.size() >= 4096)
		return fail(error, Text::tr("Player bundles require safe nonempty files, at most 4,096 entries and 256 MiB of content."));
	for (const auto &file : bundle->files)
		if (file.path.compare(path, Qt::CaseInsensitive) == 0)
			return fail(error, Text::tr("Player output and dependency paths collide: %1.").arg(path));
	bundle->totalBytes += bytes.size();
	const auto digest = hash(bytes);
	bundle->files.append({std::move(path), std::move(role), std::move(bytes), digest});
	return true;
}
bool stage(const ModelPlayerBundle &bundle, PackageStagingModel *plan, QString *error, const ModelWorkControl &control)
{
	if (!plan->createEmpty(PackageArchiveFormat::Pk3, {}, error) || !plan->beginOperationGroup(Text::tr("Player bundle"), error))
		return false;
	qint64 total = 0;
	for (const auto &file : bundle.files)
	{
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Serializing, total, bundle.totalBytes, error))
			return false;
		if (file.bytes.isEmpty() || file.sha256 != hash(file.bytes) || file.bytes.size() > bundleLimit - total)
			return fail(error, Text::tr("The reviewed player content changed. Prepare the bundle again."));
		total += file.bytes.size();
		if (!plan->addBytes(file.bytes, file.path, error))
			return false;
	}
	PackageReadControl packageControl;
	packageControl.isCancelled = control.cancelled;
	if (total != bundle.totalBytes || total <= 0)
		return fail(error, Text::tr("The reviewed player byte count changed. Prepare the bundle again."));
	return plan->endOperationGroup(true, error, packageControl);
}
bool transformPart(ModelMesh *mesh, const ModelAssemblyPart &part, double parentScale, QString *error, const ModelWorkControl &control)
{
	ModelTransform transform;
	transform.translation = scaled(part.translation, parentScale);
	transform.rotation = part.rotation;
	transform.scale = scaled({1, 1, 1}, parentScale * part.scale);
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	for (auto &surface : mesh->surfaces)
		for (auto &frame : surface.frames)
			for (int v = 0; v < frame.positions.size(); ++v)
			{
				if (!work.step())
					return false;
				frame.positions[v] = transformModelPoint(frame.positions[v], transform);
				frame.normals[v] = transformModelNormal(frame.normals[v], transform);
			}
	for (auto &tag : mesh->tags)
	{
		if (!work.step())
			return false;
		tag.origin = transformModelPoint(tag.origin, transform);
		for (int axis = 0; axis < 3; ++axis)
		{
			const auto value = rotateModelVector({tag.axis[axis * 3], tag.axis[axis * 3 + 1], tag.axis[axis * 3 + 2]}, part.rotation);
			tag.axis[axis * 3] = value.x;
			tag.axis[axis * 3 + 1] = value.y;
			tag.axis[axis * 3 + 2] = value.z;
		}
	}
	for (auto &frame : mesh->frames)
		frame.origin = transformModelPoint(frame.origin, transform);
	updateEditableModelMetadata(mesh);
	return work.check();
}
struct ReadReference
{
	QString path;
	qsizetype entryIndex = -1;
	QByteArray bytes;
};
// The same file/package reference validation as linked skins; the caller's
// decoder supplies the content contract.
bool readReference(const ModelAssemblyContext &context, const ModelPlayerReference &reference, ModelPlayerBundle *bundle, qint64 limit,
				   ReadReference *output, QString *error, const ModelWorkControl &control)
{
	if (!validateModelAssemblySkin(reference, nullptr))
		return fail(error, Text::tr("Choose a valid file or normalized package path. Only package entries can name an entry index."));
	ReadReference result;
	if (reference.sourceKind == ModelAssemblySource::File)
	{
		if (!QFileInfo(reference.source).isAbsolute() && context.directory.isEmpty())
			return fail(error, Text::tr("A relative path needs a source directory: %1.").arg(reference.source));
		result.path = QDir(context.directory).absoluteFilePath(reference.source);
		const auto canonical = QFileInfo(result.path).canonicalFilePath();
		protect(bundle, result.path);
		if (!readModelFile(result.path, &result.bytes, error, control))
			return false;
		if (canonical.isEmpty() || canonical != QFileInfo(result.path).canonicalFilePath())
			return fail(error, Text::tr("%1 changed while reading.").arg(result.path));
		if (result.bytes.size() > limit)
			return fail(error, Text::tr("%1 is larger than %2 bytes.").arg(result.path).arg(limit));
	}
	else
	{
		if (!resolveModelSkinSource(*context.archive, {reference.source, reference.entryIndex}, &result.entryIndex, error))
			return false;
		result.path = context.archive->entries()[result.entryIndex].virtualPath;
		if (!ModelArchiveReader(*context.archive, control).readEntryAt(result.entryIndex, &result.bytes, error, limit))
			return false;
	}
	*output = std::move(result);
	return true;
}
bool iconBytes(const ModelAssemblyContext &context, const ModelPlayerReference &reference, ModelPlayerBundle *bundle, QByteArray *output,
			   QJsonObject *receipt, QString *error, const ModelWorkControl &control)
{
	ReadReference input;
	if (!readReference(context, reference, bundle, modelFileByteLimit, &input, error, control))
		return fail(error, Text::tr("Choose a readable icon image. %1").arg(error ? *error : QString()));
	const auto decoded = decodeIdTechImage(input.path, input.bytes, {});
	if (!decoded.decoded || decoded.image.isNull() || !decoded.frames.isEmpty() || decoded.image.width() > 1024 ||
		decoded.image.height() > 1024)
		return fail(error, Text::tr("Choose a single icon image up to 1024 × 1024 pixels. %1").arg(decoded.error));
	TextureExportOptions texture;
	texture.format = TextureExportFormat::Targa;
	const auto encoded = encodeTextureExport(decoded.image, texture, {}, [&](qint64 done, qint64 total) {
		return modelWorkCheckpoint(control, ModelWorkPhase::Serializing, done, total, error);
	});
	if (!encoded.succeeded)
		return fail(error, encoded.error);
	*receipt = {{"kind", reference.sourceKind == ModelAssemblySource::File ? "file" : "package"},
				{"entryIndex", input.entryIndex},
				{"sha256", hex(hash(input.bytes))},
				{"bytes", input.bytes.size()},
				{"width", decoded.image.width()},
				{"height", decoded.image.height()}};
	*output = encoded.bytes;
	return true;
}
// A WAV the original sound loader reads as it is: GetWavinfo accepts only
// Microsoft PCM (format 1), finding chunks by name in any order, and
// S_LoadSound refuses more than one channel. 8 and 16 bits at any rate load.
bool nativeWav(const QByteArray &bytes, int *rate, int *bits)
{
	if (bytes.size() < 12 || !bytes.startsWith("RIFF") || bytes.mid(8, 4) != "WAVE")
		return false;
	bool format = false, data = false;
	qsizetype at = 12;
	while (at + 8 <= bytes.size())
	{
		const auto id = bytes.mid(at, 4);
		const quint32 size = qFromLittleEndian<quint32>(bytes.constData() + at + 4);
		if (quint64(size) > quint64(bytes.size() - at - 8))
			return false;
		if (id == "fmt ")
		{
			if (size < 16)
				return false;
			const auto *field = bytes.constData() + at + 8;
			*rate = int(qFromLittleEndian<quint32>(field + 4));
			*bits = qFromLittleEndian<quint16>(field + 14);
			format = qFromLittleEndian<quint16>(field) == 1 && qFromLittleEndian<quint16>(field + 2) == 1 && (*bits == 8 || *bits == 16) &&
					 *rate > 0;
			if (!format)
				return false;
		}
		else if (id == "data")
			data = size > 0;
		at += 8 + qsizetype(size) + (size & 1);
	}
	return format && data;
}
bool soundBytes(const ModelAssemblyContext &context, const ModelPlayerSound &sound, ModelPlayerBundle *bundle, QByteArray *output,
				QJsonObject *receipt, QString *note, QString *error, const ModelWorkControl &control)
{
	ReadReference input;
	if (!readReference(context, sound.source, bundle, soundLimit, &input, error, control))
		return fail(error, Text::tr("Sound %1: %2").arg(sound.name, error ? *error : QString()));
	const AudioWorkControl audioControl{control.cancelled};
	const auto decoded = decodeAudioClip(input.path, input.bytes, audioControl);
	if (!decoded.succeeded())
	{
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, 0, 0, error))
			return false;
		return fail(error, Text::tr("Sound %1 cannot be read as audio: %2").arg(sound.name, decoded.error));
	}
	int rate = 0, bits = 0;
	const bool native = nativeWav(input.bytes, &rate, &bits);
	if (native)
	{
		*output = input.bytes;
		note->clear();
		if (rate != 22050 || bits != 16)
			*note = Text::tr("Sound %1 is kept as it is (%2 Hz, %3-bit mono PCM); the game resamples it as it loads.").arg(sound.name).arg(rate).arg(bits);
	}
	else
	{
		AudioDeliveryOptions delivery;
		delivery.preset = AudioDeliveryPreset::Quake3;
		const auto rendered = renderAudioDelivery(decoded.clip, delivery, audioControl);
		if (!rendered.succeeded())
		{
			if (!modelWorkCheckpoint(control, ModelWorkPhase::Serializing, 0, 0, error))
				return false;
			return fail(error, Text::tr("Sound %1 cannot be converted for Quake III: %2").arg(sound.name, rendered.error));
		}
		*output = rendered.bytes;
		*note = Text::tr("Sound %1 was converted from %2 Hz and %n channel(s) to 22,050 Hz mono 16-bit PCM, which Quake III reads.", nullptr,
						 decoded.clip.channels)
					.arg(sound.name)
					.arg(decoded.clip.sampleRate);
		if (rendered.samplesAboveFullScale > 0)
			*note += QLatin1Char(' ') + Text::tr("Samples clipped at full scale: %n. Lower its gain in the Audio Editor to avoid this.",
												  nullptr, int(std::min<qint64>(rendered.samplesAboveFullScale, 1000000000)));
	}
	if (output->isEmpty() || output->size() > soundLimit)
		return fail(error, Text::tr("Sound %1 must be between 1 byte and 16 MiB once packaged.").arg(sound.name));
	*receipt = {{"name", sound.name},
				{"kind", sound.source.sourceKind == ModelAssemblySource::File ? "file" : "package"},
				{"entryIndex", input.entryIndex},
				{"sourceSha256", hex(hash(input.bytes))},
				{"sourceBytes", input.bytes.size()},
				{"converted", !native},
				{"durationMs", decoded.clip.durationMs()}};
	return true;
}
// How one role looks in one skin: an explicit .skin, else (for the package's
// own skin) the part's linked assembly skin, else the part's stored variant of
// that name, else its own materials (`own`).
bool roleSkin(const ModelAssemblyContext &context, const ModelAssemblyInput &input, const ModelMesh &transformed, const QString &name,
			  bool packageSkin, const std::optional<ModelPlayerReference> &explicitSkin, ModelPlayerBundle *bundle, ModelMesh *output,
			  QJsonObject *receipt, bool *own, QString *error, const ModelWorkControl &control)
{
	*own = false;
	if (explicitSkin)
	{
		ModelAssemblyInput copy;
		copy.mesh = transformed;
		if (!resolveModelAssemblySkin(*explicitSkin, context, &copy, error, control))
			return false;
		if (copy.skin->sourceKind == ModelAssemblySource::File)
		{
			protect(bundle, copy.skin->source);
			protect(bundle, copy.skin->resolvedPath);
		}
		*receipt = {{"origin", explicitSkin->sourceKind == ModelAssemblySource::File ? "file" : "package"},
					{"entryIndex", copy.skin->entryIndex},
					{"sha256", hex(copy.skin->sha256)}};
		*output = std::move(copy.mesh);
		return true;
	}
	if (packageSkin && input.skin)
	{
		*receipt = {{"origin", "linked"}, {"sha256", hex(input.skin->sha256)}};
		*output = transformed;
		return true;
	}
	const int stored = findModelSkinVariant(input.mesh.skinVariants, name);
	if (stored >= 0)
	{
		if (!applyModelSkinVariant(transformed, input.mesh.skinVariants[stored], output, error))
			return false;
		*receipt = {{"origin", "variant"}, {"variant", input.mesh.skinVariants[stored].name}};
		return true;
	}
	*receipt = {{"origin", "model"}};
	*output = transformed;
	*own = true;
	return true;
}
} // namespace

const QStringList &modelPlayerSoundNames()
{
	static const QStringList names{QStringLiteral("death1"),   QStringLiteral("death2"),   QStringLiteral("death3"),	QStringLiteral("jump1"),
								   QStringLiteral("pain25_1"), QStringLiteral("pain50_1"), QStringLiteral("pain75_1"),	QStringLiteral("pain100_1"),
								   QStringLiteral("falling1"), QStringLiteral("gasp"),	   QStringLiteral("drown"),		QStringLiteral("fall1"),
								   QStringLiteral("taunt")};
	return names;
}

ModelPlayerReference modelPlayerReference(const QString &text)
{
	if (text.startsWith(QStringLiteral("package:")))
		return {text.mid(8), ModelAssemblySource::Package, -1};
	return {text, ModelAssemblySource::File, -1};
}

QString modelPlayerReferenceText(const ModelPlayerReference &reference)
{
	return reference.sourceKind == ModelAssemblySource::Package ? QStringLiteral("package:") + reference.source : reference.source;
}

bool prepareModelPlayerBundle(const ModelAssembly &assembly, const ModelAssemblyResolved &resolved, const ModelAssemblyContext &context,
							  const ModelPlayerBundleOptions &options, const QString &sourcePath, ModelPlayerBundle *result, QString *error,
							  const ModelWorkControl &control)
{
	if (error)
		error->clear();
	if (!result || !modelWorkCheckpoint(control, ModelWorkPhase::Validating, 0, 0, error))
		return false;
	if (!assembly.q3Animation || resolved.recipeSha256 != modelAssemblyFingerprint(assembly) ||
		!validateModelAssemblyQ3Animation(assembly, resolved, error))
		return fail(error, Text::tr("Configure valid native animation and reload the assembly before preparing a player bundle. %1")
							   .arg(error ? *error : QString()));
	if (!context.archive || !context.archive->isOpen())
		return fail(error, Text::tr("Open the project asset package or folder before preparing a player bundle."));
	const QRegularExpression id(QStringLiteral("^[A-Za-z][A-Za-z0-9_-]{0,23}$"));
	const QString directory = QStringLiteral("models/players/%1/").arg(options.modelName);
	// The package's own skin first, then the extra skins and, for team games,
	// red and blue when they are missing.
	QVector<ModelPlayerSkinVariant> skins{ModelPlayerSkinVariant{options.skinName, {}, {}}};
	skins += options.variants;
	if (options.teamVariants)
		for (const auto &team : {QStringLiteral("red"), QStringLiteral("blue")})
			if (std::none_of(skins.cbegin(), skins.cend(), [&](const auto &skin) { return skin.name.compare(team, Qt::CaseInsensitive) == 0; }))
				skins.append(ModelPlayerSkinVariant{team, {}, {}});
	const auto fits = [&](const QString &skin) { return id.match(skin).hasMatch() && directory.size() + 6 + skin.size() + 5 <= 63; };
	if (!id.match(options.modelName).hasMatch() || !std::all_of(skins.cbegin(), skins.cend(), [&](const auto &skin) { return fits(skin.name); }) ||
		!isSafePackageVirtualPath(directory + "lower.md3") || packageFilesystemPathIssue(directory + "lower.md3") != PackagePathIssue::None)
		return fail(error, Text::tr("Use player and skin IDs of 1–24 ASCII letters, digits, underscores or hyphens, starting with a "
									"letter. Their generated paths must fit 63 characters."));
	QSet<QString> skinNames;
	for (const auto &skin : std::as_const(skins))
	{
		if (skinNames.contains(skin.name.toLower()))
			return fail(error, Text::tr("Skin %1 is listed twice. Each skin of a player needs its own name.").arg(skin.name));
		skinNames.insert(skin.name.toLower());
	}
	if (skins.size() > skinLimit)
		return fail(error, Text::tr("A player package can publish at most %1 skins.").arg(skinLimit));
	QSet<QString> soundNames;
	for (const auto &sound : options.sounds)
	{
		if (!modelPlayerSoundNames().contains(sound.name))
			return fail(error, Text::tr("Quake III has no custom player sound named %1. Use one of: %2.")
								   .arg(sound.name, modelPlayerSoundNames().join(QStringLiteral(", "))));
		if (soundNames.contains(sound.name))
			return fail(error, Text::tr("Custom sound %1 is listed twice.").arg(sound.name));
		soundNames.insert(sound.name);
	}
	ModelTargetProfile quake3;
	findModelTargetProfile(QStringLiteral("md3"), QStringLiteral("quake3"), &quake3);
	if (options.detailLevels == ModelPlayerDetailLevels::Generated &&
		(options.lodLevels < 1 || options.lodLevels > std::max(1, quake3.limits.detailLevels) || !(options.lodRatio >= .05 && options.lodRatio <= .95)))
		return fail(error, Text::tr("Quake III loads up to %n detail level(s) per model; choose that many or fewer, each keeping 0.05 to 0.95 of "
									"the level above.",
									nullptr, quake3.limits.detailLevels));
	if (assembly.parts.size() != 3 || resolved.inputs.size() != 3)
		return fail(
			error,
			Text::tr(
				"A native player bundle needs exactly three parts: lower, upper and head. Merge accessories into those models first."));
	const std::array<QString, 3> ids{assembly.q3Animation->lowerPart, assembly.q3Animation->upperPart, options.headPart};
	const std::array<QString, 3> roles{QStringLiteral("lower"), QStringLiteral("upper"), QStringLiteral("head")};
	if (QSet<QString>(ids.cbegin(), ids.cend()).size() != 3)
		return fail(error, Text::tr("Choose distinct lower, upper and head parts."));
	ModelPlayerBundle candidate;
	candidate.options = options;
	candidate.recipeSha256 = resolved.recipeSha256;
	candidate.sourceArchive = context.archive;
	protect(&candidate, sourcePath);
	QJsonArray parts, skinReceipts, levelReceipts;
	QStringList completeness;
	ModelMesh materials;
	materials.sourcePath = directory;
	double parentScale = 1;
	// Per skin: whether each role fell back to its own materials.
	QVector<std::array<bool, 3>> ownMaterials(skins.size(), {false, false, false});
	QVector<QJsonArray> skinRoles(skins.size());
	for (int role = 0; role < 3; ++role)
	{
		const auto part = std::find_if(assembly.parts.cbegin(), assembly.parts.cend(), [&](const auto &p) { return p.id == ids[role]; });
		const auto input =
			std::find_if(resolved.inputs.cbegin(), resolved.inputs.cend(), [&](const auto &p) { return p.part == ids[role]; });
		if (part == assembly.parts.cend() || input == resolved.inputs.cend() ||
			(role == 0 ? !part->parent.isEmpty() : part->parent != ids[role - 1] || part->tag != (role == 1 ? "tag_torso" : "tag_head")))
			return fail(error, Text::tr("Use lower as the root, upper on lower's tag_torso, and head on upper's tag_head."));
		if (role == 2 && (input->mesh.frames.size() != 1 || part->firstFrame != 0 || part->phase != 0))
			return fail(error, Text::tr("Native Quake III heads use frame zero. Supply a single-pose head before publishing this player."));
		const auto hasTag = [&](const QString &name) {
			return std::any_of(input->mesh.tags.cbegin(), input->mesh.tags.cend(), [&](const auto &tag) { return tag.name == name; });
		};
		// CG_AddPlayerWeapon positions the held weapon on the torso's tag_weapon;
		// without it the weapon is drawn at the torso origin.
		if (role == 1 && !hasTag(QStringLiteral("tag_weapon")))
			return fail(error, Text::tr("Upper part %1 has no tag_weapon, so the game would draw every held weapon at the torso origin. Add a "
										"tag_weapon at the hand under Attachment tags on the Mesh Editor's Animation page.")
								   .arg(part->id));
		if (role == 1 && !hasTag(QStringLiteral("tag_flag")))
			completeness << Text::tr("Upper has no tag_flag, so a carried CTF flag trails behind the player (CG_TrailItem) instead of being "
									 "held on the torso as with Team Arena models.");
		if (role == 2 && !input->mesh.tags.isEmpty())
			completeness << Text::tr("The head's tags are not used by Quake III; it is placed on upper's tag_head.");
		if (input->sourceKind == ModelAssemblySource::File)
			protect(&candidate, input->source);
		if (input->skin && input->skin->sourceKind == ModelAssemblySource::File)
		{
			protect(&candidate, input->skin->source);
			protect(&candidate, input->skin->resolvedPath);
		}
		auto mesh = input->mesh;
		if (!transformPart(&mesh, *part, parentScale, error, control))
			return false;
		ModelExportReport exportReport;
		const auto md3 = exportEditableModel(mesh, "md3", 0, error, control, &exportReport);
		if (md3.isEmpty())
			return false;
		if (!add(&candidate, directory + roles[role] + ".md3", roles[role] + "-model", md3, error))
			return false;
		// Detail levels are made in the part's own space and then placed like
		// the part, so every level keeps the baked transform and the tags of
		// the base model (R_LerpTag reads the base model only).
		QVector<ModelLodLevel> levels;
		if (options.detailLevels == ModelPlayerDetailLevels::Generated)
		{
			if (!buildModelLods(input->mesh, options.lodLevels, options.lodRatio, &levels, error, control))
				return fail(error, Text::tr("Detail levels of %1: %2").arg(roles[role], error ? *error : QString()));
		}
		else if (options.detailLevels == ModelPlayerDetailLevels::Stored && !input->mesh.detailLevels.isEmpty())
		{
			const bool handMade = std::any_of(input->mesh.detailLevels.cbegin(), input->mesh.detailLevels.cend(), [](const auto &l) { return !l.generated; });
			if (handMade && input->sourceKind != ModelAssemblySource::File)
				return fail(error, Text::tr("Part %1 is read from the package, so its hand-made detail levels cannot be found. Use a source file "
											"or generated levels.")
									   .arg(part->id));
			QString recipeError;
			if (!validateModelLodRecipe(input->mesh.detailLevels, quake3.limits.detailLevels, &recipeError))
				return fail(error, Text::tr("Detail levels of %1: %2").arg(roles[role], recipeError));
			const auto baseDirectory = input->sourceKind == ModelAssemblySource::File ? QFileInfo(input->source).absolutePath() : QString();
			for (const auto &level : input->mesh.detailLevels)
				if (!level.generated)
					protect(&candidate, modelLodRecipeSourcePath(level, baseDirectory));
			if (!buildModelLodsFromRecipe(input->mesh, input->mesh.detailLevels, baseDirectory, &levels, error, control))
				return fail(error, Text::tr("Detail levels of %1: %2").arg(roles[role], error ? *error : QString()));
		}
		else if (options.detailLevels == ModelPlayerDetailLevels::Stored)
			completeness << Text::tr("No detail levels are stored for %1, so it is drawn at full detail at every distance.").arg(roles[role]);
		QVector<ModelMesh> levelMeshes;
		for (auto &level : levels)
		{
			if (!transformPart(&level.mesh, *part, parentScale, error, control))
				return false;
			levelMeshes << level.mesh;
		}
		if (!levels.isEmpty())
		{
			ModelLodValidation validation;
			if (!validateModelLodSet(mesh, levelMeshes, quake3, &validation, error, control))
				return false;
			if (!validation.ok())
				return fail(error, Text::tr("The detail levels of %1 would not load correctly:\n%2")
									   .arg(roles[role], modelLodValidationLines(validation).join(QLatin1Char('\n'))));
			for (int level = 1; level <= levels.size(); ++level)
			{
				ModelExportReport levelReport;
				const auto bytes = exportEditableModel(levelMeshes[level - 1], "md3", 0, error, control, &levelReport);
				if (bytes.isEmpty() ||
					!add(&candidate, directory + roles[role] + QStringLiteral("_%1.md3").arg(level), roles[role] + "-detail", bytes, error))
					return false;
				levelReceipts.append(QJsonObject{{"role", roles[role]},
												 {"level", level},
												 {"triangles", levels[level - 1].triangles},
												 {"handMade", !levels[level - 1].source.isEmpty()}});
				candidate.notes += levels[level - 1].notes;
			}
		}
		parentScale *= part->scale;
		// Every skin covers every surface: writeModelQ3Skin plans the written
		// text against the part, and its detail levels keep the part's names.
		for (int skin = 0; skin < skins.size(); ++skin)
		{
			ModelMesh appearance;
			QJsonObject origin;
			bool own = false;
			if (!roleSkin(context, *input, mesh, skins[skin].name, skin == 0, skins[skin].skins[role], &candidate, &appearance, &origin, &own,
						  error, control))
				return fail(error, Text::tr("The %1 skin of %2: %3").arg(skins[skin].name, roles[role], error ? *error : QString()));
			QByteArray bytes;
			if (!writeModelQ3Skin(appearance, &bytes, error, control))
				return fail(error, Text::tr("The %1 skin of %2: %3").arg(skins[skin].name, roles[role], error ? *error : QString()));
			if (!add(&candidate, directory + roles[role] + '_' + skins[skin].name + ".skin", roles[role] + "-skin", bytes, error))
				return false;
			ownMaterials[skin][role] = own;
			origin.insert("role", roles[role]);
			skinRoles[skin].append(origin);
			materials.surfaces += appearance.surfaces;
		}
		candidate.notes += exportReport.notes;
		QJsonObject receipt{{"part", part->id},
							{"role", roles[role]},
							{"sourceSha256", hex(input->sha256)},
							{"sourceBytes", input->bytes},
							{"frames", mesh.frames.size()},
							{"surfaces", mesh.surfaces.size()},
							{"positionQuantum", 1.0 / 64.0},
							{"normalEncoding", "md3-byte-latitude-longitude"},
							{"notes", QJsonArray::fromStringList(exportReport.notes)}};
		if (input->skin)
			receipt.insert("skinSha256", hex(input->skin->sha256));
		parts.append(receipt);
	}
	for (int skin = 0; skin < skins.size(); ++skin)
	{
		const auto &name = skins[skin].name;
		QStringList own;
		for (int role = 0; role < 3; ++role)
			if (ownMaterials[skin][role])
				own << roles[role];
		const bool team = name.compare(QStringLiteral("red"), Qt::CaseInsensitive) == 0 || name.compare(QStringLiteral("blue"), Qt::CaseInsensitive) == 0;
		if (options.teamVariants && team && own.size() == 3)
			return fail(error, Text::tr("The %1 team skin would look exactly like the model's own materials on every part. Give at least one part "
										"a %1 skin variant (Mesh Editor, Skin Variants) or a .skin file for %1.")
								   .arg(name));
		if (skin > 0 && !own.isEmpty())
			completeness << Text::tr("The %1 skin uses the own materials of %2: no .skin file or stored %1 variant was given for them.")
								.arg(name, own.join(QStringLiteral(", ")));
	}
	// Sampling additionally checks the required native attachments. The native
	// writer validates a complete tag track in every pose, including unused ones.
	ModelAssemblyPose pose;
	if (!sampleModelAssembly(assembly, resolved, 0, &pose, error, control))
		return false;
	const auto config = exportModelQ3Animation(assembly.q3Animation->config, error);
	if (!add(&candidate, directory + "animation.cfg", "animation", config, error))
		return false;
	// Every skin needs its icon: CG_RegisterClientModelname fails, and the
	// game falls back to its default model, when icon_<skin> is missing.
	QByteArray icon;
	QJsonObject iconReceipt;
	if (!iconBytes(context, {options.iconSource, options.iconKind, options.iconEntryIndex}, &candidate, &icon, &iconReceipt, error, control) ||
		!add(&candidate, directory + "icon_" + options.skinName + ".tga", "icon", icon, error))
		return false;
	QStringList reusedIcons;
	for (int skin = 0; skin < skins.size(); ++skin)
	{
		QJsonObject skinIcon{{"reused", true}};
		if (skin > 0)
		{
			auto bytes = icon;
			if (skins[skin].icon)
			{
				if (!iconBytes(context, *skins[skin].icon, &candidate, &bytes, &skinIcon, error, control))
					return fail(error, Text::tr("The icon of the %1 skin: %2").arg(skins[skin].name, error ? *error : QString()));
			}
			else
				reusedIcons << skins[skin].name;
			if (!add(&candidate, directory + "icon_" + skins[skin].name + ".tga", "icon", bytes, error))
				return false;
		}
		else
			skinIcon = iconReceipt;
		skinReceipts.append(QJsonObject{{"name", skins[skin].name}, {"roles", skinRoles[skin]}, {"icon", skinIcon}});
		candidate.skins << skins[skin].name;
	}
	QJsonArray soundReceipts;
	for (const auto &sound : options.sounds)
	{
		QByteArray bytes;
		QJsonObject receipt;
		QString note;
		if (!soundBytes(context, sound, &candidate, &bytes, &receipt, &note, error, control) ||
			!add(&candidate, QStringLiteral("sound/player/%1/%2.wav").arg(options.modelName, sound.name), "sound", bytes, error))
			return false;
		if (!note.isEmpty())
			completeness << note;
		soundReceipts.append(receipt);
	}
	const auto progress = [&](int done, int total) { return modelWorkCheckpoint(control, ModelWorkPhase::Reading, done, total, error); };
	const ModelArchiveReader reader(*context.archive, control);
	candidate.dependencies = inspectModelMaterialDependencies(materials, reader, progress);
	if (!candidate.dependencies.canExport())
		return fail(error, Text::tr("Resolve the player material dependencies before publication. %1")
							   .arg(levelDependencyReportText(candidate.dependencies)));
	const auto nativePath = [](const QString &path) {
		return path.size() <= 63 &&
			   std::all_of(path.cbegin(), path.cend(), [](QChar ch) { return ch.unicode() >= 32 && ch.unicode() < 127; });
	};
	for (const auto &dependency : candidate.dependencies.dependencies)
		if (!nativePath(dependency.reference))
			return fail(
				error, Text::tr("Native player material references must fit 63 printable ASCII characters: %1.").arg(dependency.reference));
	for (const auto &path : candidate.dependencies.resolvedPaths)
	{
		const auto suffix = QFileInfo(path).suffix().toLower();
		if (!nativePath(path) || (suffix != "tga" && suffix != "jpg" && suffix != "shader" && suffix != "roq"))
			return fail(error, Text::tr("Original Quake III player dependencies need TGA/JPG images, shader scripts or RoQ video and paths "
										"up to 63 characters. Convert %1 in the asset editor first.")
								   .arg(path));
		QByteArray bytes;
		if (!reader.readEntryBytes(path, &bytes, error, std::min(modelFileByteLimit, bundleLimit - candidate.totalBytes)) ||
			!add(&candidate, path, "dependency", bytes, error))
			return false;
		if (suffix == "tga" || suffix == "jpg")
		{
			const auto image = decodeIdTechImage(path, bytes, {});
			if (!image.decoded || image.image.isNull())
				return fail(error, Text::tr("Player image %1 is unreadable: %2").arg(path, image.error));
			// Original tr_image.c LoadTGA ignores both image origin flags (its
			// vertical-flip block is disabled). Require bottom-left native pixels.
			const bool tga =
				bytes.size() >= 18 && bytes[1] == 0 && (bytes[17] & 0x30) == 0 &&
				(((bytes[2] == 2 || bytes[2] == 10) && (bytes[16] == 24 || bytes[16] == 32)) || (bytes[2] == 3 && bytes[16] == 8));
			const bool jpeg = bytes.startsWith(QByteArray::fromHex("ffd8ff"));
			if ((suffix == "tga" && !tga) || (suffix == "jpg" && !jpeg))
				return fail(error, Text::tr("Image %1 is readable by the studio but does not use an original Quake III image layout. "
											"Re-export it in the Texture Editor.")
									   .arg(path));
		}
	}
	// Recheck the owned bytes, never an earlier live package view. A shader that
	// changes while captured cannot introduce an uncaptured dependency.
	PackageStagingModel owned;
	if (!stage(candidate, &owned, error, control))
		return false;
	PackageReadControl packageControl;
	packageControl.isCancelled = control.cancelled;
	const PackageStagingArchive frozen(owned, PackageStagingReadMode::CompletePlan, packageControl);
	if (!frozen.isOpen())
		return fail(error, frozen.errorString());
	const auto closure = inspectModelMaterialDependencies(materials, frozen, progress);
	if (!closure.canExport() || closure.resolvedPaths != candidate.dependencies.resolvedPaths)
		return fail(error, Text::tr("The captured shader dependencies changed. Reload the package and prepare the bundle again."));
	const bool team = std::any_of(skins.cbegin(), skins.cend(), [](const auto &skin) { return skin.name.compare(QStringLiteral("red"), Qt::CaseInsensitive) == 0; }) &&
					  std::any_of(skins.cbegin(), skins.cend(), [](const auto &skin) { return skin.name.compare(QStringLiteral("blue"), Qt::CaseInsensitive) == 0; });
	candidate.notes.prepend(Text::tr("Publishes separate native models, primary skin assignments, animation.cfg, a TGA icon and reviewed "
									 "material dependencies. Local transforms and inherited scale are baked into every native pose and tag."));
	candidate.notes << Text::tr("Skin %1/%2 is the non-team skin. Skins: %3.").arg(options.modelName, options.skinName, candidate.skins.join(QStringLiteral(", ")))
					<< (team ? Text::tr("Team games use the red and blue skins (lower_red.skin, icon_red.tga and so on) whichever skin a player "
										"chooses.")
							 : Text::tr("Without red and blue skins, team games cannot find lower_red.skin and show the default team model "
										"instead. Turn on Team skins to publish them."));
	if (!reusedIcons.isEmpty())
		candidate.notes << Text::tr("The icons of %1 reuse the package icon.").arg(reusedIcons.join(QStringLiteral(", ")));
	candidate.notes << (levelReceipts.isEmpty()
							? Text::tr("No detail levels: the game draws every part at full detail at every distance.")
							: Text::tr("Detail level files beside lower.md3, upper.md3 and head.md3: %n. The game picks a level by distance "
									   "and r_lodbias.",
									   nullptr, int(levelReceipts.size())));
	candidate.notes << (options.sounds.isEmpty()
							? Text::tr("No custom sounds: the game plays its default model's sounds for this player.")
							: Text::tr("Custom sounds packaged: %1 of %2. The game plays the default model's sound for each of the others.")
								  .arg(options.sounds.size())
								  .arg(modelPlayerSoundNames().size()));
	candidate.notes += completeness;
	candidate.notes << Text::tr("Whole referenced shader scripts are retained. Review their other declarations for naming conflicts with "
								"the target game. Editable sources, collision boxes and custom clip metadata remain in the project.")
					<< Text::tr("Shader effects, video playback and gameplay still require testing in the target game.");
	candidate.notes.removeDuplicates();
	candidate.protectedPaths.removeDuplicates();
	QJsonArray files;
	for (const auto &file : candidate.files)
		files.append(QJsonObject{{"path", file.path}, {"role", file.role}, {"bytes", file.bytes.size()}, {"sha256", hex(file.sha256)}});
	// The embedded receipt contains content identities and virtual paths, never
	// machine-specific source paths or times. It is stable across project moves.
	// Schema 2 adds skins, detail levels and sounds; a single-skin package
	// without them keeps the schema 1 shape.
	const bool extended = skins.size() > 1 || !levelReceipts.isEmpty() || !soundReceipts.isEmpty();
	candidate.receipt = {{"schemaVersion", extended ? 2 : 1},
						 {"format", "quake3-player-bundle"},
						 {"model", options.modelName},
						 {"skin", options.skinName},
						 {"parts", parts},
						 {"icon", iconReceipt},
						 {"files", files}};
	if (extended)
	{
		candidate.receipt.insert("skins", skinReceipts);
		candidate.receipt.insert("teamSkins", team);
		candidate.receipt.insert("detailLevels", levelReceipts);
		candidate.receipt.insert("sounds", soundReceipts);
	}
	if (!add(&candidate, "vibestudio/player-bundle.json", "receipt", QJsonDocument(candidate.receipt).toJson(), error) ||
		!modelWorkCheckpoint(control, ModelWorkPhase::Validating, candidate.totalBytes, candidate.totalBytes, error))
		return false;
	std::sort(candidate.files.begin(), candidate.files.end(), [](const auto &a, const auto &b) { return a.path < b.path; });
	*result = std::move(candidate);
	return true;
}

QJsonObject modelPlayerBundleJson(const ModelPlayerBundle &bundle)
{
	QJsonArray files;
	for (const auto &file : bundle.files)
		files.append(QJsonObject{{"path", file.path}, {"role", file.role}, {"bytes", file.bytes.size()}, {"sha256", hex(file.sha256)}});
	QJsonArray sounds;
	for (const auto &sound : bundle.options.sounds)
		sounds.append(sound.name);
	return {{"model", bundle.options.modelName},
			{"skin", bundle.options.skinName},
			{"skins", QJsonArray::fromStringList(bundle.skins)},
			{"teamSkins", bundle.skins.contains(QStringLiteral("red"), Qt::CaseInsensitive) && bundle.skins.contains(QStringLiteral("blue"), Qt::CaseInsensitive)},
			{"sounds", sounds},
			{"headPart", bundle.options.headPart},
			{"bytes", bundle.totalBytes},
			{"files", files},
			{"receipt", bundle.receipt},
			{"recipeSha256", hex(bundle.recipeSha256)},
			{"dependencies", levelDependencyReportJson(bundle.dependencies)},
			{"notes", QJsonArray::fromStringList(bundle.notes)}};
}
QStringList modelPlayerBundleText(const ModelPlayerBundle &bundle)
{
	QStringList lines{Text::tr("Player %1/%2: %3 files, %4 bytes.")
						  .arg(bundle.options.modelName, bundle.options.skinName)
						  .arg(bundle.files.size())
						  .arg(bundle.totalBytes)};
	for (const auto &file : bundle.files)
		lines << QStringLiteral("%1 · %2 bytes · %3").arg(file.path).arg(file.bytes.size()).arg(hex(file.sha256));
	return lines + bundle.notes;
}
PackageWriteReport writeModelPlayerBundle(const ModelPlayerBundle &bundle, const QString &path, bool overwrite, bool dryRun,
										  const ModelWorkControl &requestControl)
{
	bool cancelled = false;
	ModelWorkControl control = requestControl;
	control.cancelled = [&] { return cancelled = cancelled || (requestControl.cancelled && requestControl.cancelled()); };
	PackageWriteReport failure;
	failure.outputPath = QFileInfo(path).absoluteFilePath();
	failure.dryRun = dryRun;
	failure.format = PackageArchiveFormat::Pk3;
	const auto reject = [&](const QString &message) {
		failure.blockedMessages << message;
		failure.cancelled = control.cancelled && control.cancelled();
		return failure;
	};
	if (!path.endsWith(".pk3", Qt::CaseInsensitive) || bundle.files.isEmpty())
		return reject(Text::tr("Prepare a player bundle and choose a separate .pk3 output."));
	const auto target = inspectModelWriteTarget(path, control);
	if (!target.isValid())
		return reject(target.error);
	const QStringList outputs{target.path, target.resolvedPath, target.path + ".bak", target.path + ".vibestudio-save.lock",
							  target.resolvedPath + ".bak", target.resolvedPath + ".vibestudio-save.lock"};
	QString error;
	if (!validateModelArchiveOutputs(bundle.sourceArchive.get(), outputs, &error, control))
		return reject(error);
	for (const auto &output : outputs)
	{
		for (const auto &input : bundle.protectedPaths)
			if (modelPathsReferToSameFile(input, output))
				return reject(Text::tr("Player output and backup paths must not replace an assembly, model, skin, icon or sound input."));
	}
	PackageStagingModel staging;
	if (!stage(bundle, &staging, &error, control))
		return reject(error);
	PackageWriteRequest request;
	request.destinationPath = target.path;
	request.format = PackageArchiveFormat::Pk3;
	request.allowOverwrite = overwrite;
	request.dryRun = dryRun;
	request.verifyDeterminism = true;
	request.expectedDestinationSha256 = hex(target.sha256);
	request.isCancelled = control.cancelled;
	request.byteProgress = [&](PackageWritePhase, const QString &, quint64 done, quint64 total) {
		if (control.progress)
			control.progress(ModelWorkPhase::Writing, qint64(done), qint64(total));
	};
	return staging.writeArchive(request);
}
} // namespace vibestudio
