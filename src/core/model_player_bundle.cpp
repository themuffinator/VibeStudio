#include "core/model_player_bundle.h"

#include "core/model_archive.h"
#include "core/texture_export.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>
#include <array>

// Layout/tag contracts: id Software Quake III Arena cg_players.c,
// CG_RegisterClientModelname / CG_RegisterClientSkin / CG_Player.
// GPL-2.0-or-later reference, reviewed 2026-10-06; original implementation.
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
QByteArray skinBytes(const ModelMesh &mesh, QString *error, const ModelWorkControl &control)
{
	QString text;
	for (const auto &surface : mesh.surfaces)
	{
		auto name = surface.name.toLower();
		if (name.size() > 2 && name[name.size() - 2] == '_')
			name.chop(2);
		const auto material = surface.skinPaths.value(0);
		if (name.contains('"') || name.contains(',') || name.contains(QStringLiteral("tag_")) || material.contains('"'))
		{
			fail(
				error,
				Text::tr("Surface %1 cannot be represented by a native skin. Rename it without quotes, commas or tag_.").arg(surface.name));
			return {};
		}
		text += QStringLiteral("\"%1\",\"%2\"\n").arg(name, material);
	}
	const auto bytes = text.toUtf8();
	ModelSkinBindingPlan plan;
	return planModelSkinBindings(mesh, bytes, &plan, error, control) ? bytes : QByteArray();
}
bool iconBytes(const ModelAssemblyContext &context, const ModelPlayerBundleOptions &options, ModelPlayerBundle *bundle, QByteArray *output,
			   QJsonObject *receipt, QString *error, const ModelWorkControl &control)
{
	// Same file/package reference validation as linked skins; the image decoder
	// supplies the content contract instead of the skin parser.
	if (!validateModelAssemblySkin({options.iconSource, options.iconKind, options.iconEntryIndex}, nullptr))
		return fail(error, Text::tr("Choose a valid icon file or normalized package path. Only package icons can name an entry index."));
	QByteArray bytes;
	QString path = options.iconSource;
	qsizetype index = -1;
	if (options.iconKind == ModelAssemblySource::File)
	{
		if (!QFileInfo(path).isAbsolute() && context.directory.isEmpty())
			return fail(error, Text::tr("A relative icon needs a source directory."));
		path = QDir(context.directory).absoluteFilePath(path);
		const auto canonical = QFileInfo(path).canonicalFilePath();
		protect(bundle, path);
		if (!readModelFile(path, &bytes, error, control))
			return false;
		if (canonical.isEmpty() || canonical != QFileInfo(path).canonicalFilePath())
			return fail(error, Text::tr("The icon path changed while reading."));
	}
	else
	{
		if (!resolveModelSkinSource(*context.archive, {path, options.iconEntryIndex}, &index, error))
			return false;
		path = context.archive->entries()[index].virtualPath;
		if (!ModelArchiveReader(*context.archive, control).readEntryAt(index, &bytes, error, modelFileByteLimit))
			return false;
	}
	const auto decoded = decodeIdTechImage(path, bytes, {});
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
	*receipt = {{"kind", options.iconKind == ModelAssemblySource::File ? "file" : "package"},
				{"entryIndex", index},
				{"sha256", hex(hash(bytes))},
				{"bytes", bytes.size()},
				{"width", decoded.image.width()},
				{"height", decoded.image.height()}};
	*output = encoded.bytes;
	return true;
}
} // namespace

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
	if (!id.match(options.modelName).hasMatch() || !id.match(options.skinName).hasMatch() ||
		directory.size() + 6 + options.skinName.size() + 5 > 63 || !isSafePackageVirtualPath(directory + "lower.md3") ||
		packageFilesystemPathIssue(directory + "lower.md3") != PackagePathIssue::None)
		return fail(error, Text::tr("Use player and skin IDs of 1–24 ASCII letters, digits, underscores or hyphens, starting with a "
									"letter. Their generated paths must fit 63 characters."));
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
	QJsonArray parts;
	ModelMesh materials;
	materials.sourcePath = directory;
	double parentScale = 1;
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
		parentScale *= part->scale;
		ModelExportReport exportReport;
		const auto md3 = exportEditableModel(mesh, "md3", 0, error, control, &exportReport);
		if (md3.isEmpty())
			return false;
		const auto skin = skinBytes(mesh, error, control);
		if (skin.isEmpty())
			return false;
		if (!add(&candidate, directory + roles[role] + ".md3", roles[role] + "-model", md3, error) ||
			!add(&candidate, directory + roles[role] + '_' + options.skinName + ".skin", roles[role] + "-skin", skin, error))
			return false;
		materials.surfaces += mesh.surfaces;
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
	// Sampling additionally checks the required native attachments. The native
	// writer validates a complete tag track in every pose, including unused ones.
	ModelAssemblyPose pose;
	if (!sampleModelAssembly(assembly, resolved, 0, &pose, error, control))
		return false;
	const auto config = exportModelQ3Animation(assembly.q3Animation->config, error);
	if (!add(&candidate, directory + "animation.cfg", "animation", config, error))
		return false;
	QByteArray icon;
	QJsonObject iconReceipt;
	if (!iconBytes(context, options, &candidate, &icon, &iconReceipt, error, control) ||
		!add(&candidate, directory + "icon_" + options.skinName + ".tga", "icon", icon, error))
		return false;
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
	candidate.notes << Text::tr("Publishes separate native models, primary skin assignments, animation.cfg, a TGA icon and reviewed "
								"material dependencies. Local transforms and inherited scale are baked into every native pose and tag.")
					<< Text::tr("This skin is selected as %1/%2 for non-team play. Team variants and custom sounds are not generated; game "
								"defaults still supply player sounds.")
						   .arg(options.modelName, options.skinName)
					<< Text::tr("Whole referenced shader scripts are retained. Review their other declarations for naming conflicts with "
								"the target game. Editable sources, collision boxes and custom clip metadata remain in the project.")
					<< Text::tr("Shader effects, video playback and gameplay still require testing in the target game.");
	candidate.notes.removeDuplicates();
	candidate.protectedPaths.removeDuplicates();
	QJsonArray files;
	for (const auto &file : candidate.files)
		files.append(QJsonObject{{"path", file.path}, {"role", file.role}, {"bytes", file.bytes.size()}, {"sha256", hex(file.sha256)}});
	// The embedded receipt contains content identities and virtual paths, never
	// machine-specific source paths or times. It is stable across project moves.
	candidate.receipt = {{"schemaVersion", 1},
						 {"format", "quake3-player-bundle"},
						 {"model", options.modelName},
						 {"skin", options.skinName},
						 {"parts", parts},
						 {"icon", iconReceipt},
						 {"files", files}};
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
	return {{"model", bundle.options.modelName},
			{"skin", bundle.options.skinName},
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
										  const ModelWorkControl &control)
{
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
	for (const auto &output : {target.path, target.resolvedPath, target.path + ".bak", target.path + ".vibestudio-save.lock",
							   target.resolvedPath + ".bak", target.resolvedPath + ".vibestudio-save.lock"})
	{
		if (bundle.sourceArchive && bundle.sourceArchive->protectsInputPath(output))
			return reject(Text::tr("Write the player package outside its input package or asset folder."));
		for (const auto &input : bundle.protectedPaths)
			if (modelPathsReferToSameFile(input, output))
				return reject(Text::tr("Player output and backup paths must not replace an assembly, model, skin or icon input."));
	}
	PackageStagingModel staging;
	QString error;
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
