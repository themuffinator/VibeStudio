#include "core/material_library.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>
#include <QStringDecoder>
#include <QtEndian>

#include <algorithm>

namespace vibestudio {
namespace {

struct Text {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioMaterials)
};

QString suffixOf(const QString& path)
{
	const QString name = packageVirtualPathFileName(path);
	const int dot = name.lastIndexOf(QLatin1Char('.'));
	return dot < 0 ? QString() : name.mid(dot + 1).toLower();
}

QString withoutSuffix(const QString& path)
{
	const int slash = path.lastIndexOf(QLatin1Char('/'));
	const int dot = path.lastIndexOf(QLatin1Char('.'));
	return dot > slash ? path.left(dot) : path;
}

QString decodeText(const QByteArray& bytes)
{
	QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
	QString text = decoder(bytes);
	if (decoder.hasError()) {
		text = QString::fromLatin1(bytes);
	}
	return text;
}

bool isImageSuffix(const QString& suffix)
{
	static const QSet<QString> suffixes {QStringLiteral("tga"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
		QStringLiteral("pcx"), QStringLiteral("bmp"), QStringLiteral("dds")};
	return suffixes.contains(suffix);
}

// Doom 3 companion images are inputs to materials, not materials.
bool isDoom3CompanionImage(const QString& base)
{
	static const QStringList suffixes {QStringLiteral("_local"), QStringLiteral("_h"), QStringLiteral("_s"), QStringLiteral("_bmp"),
		QStringLiteral("_norm"), QStringLiteral("_spec"), QStringLiteral("_add"), QStringLiteral("_glow"), QStringLiteral("_fx")};
	const QString lower = base.toLower();
	for (const QString& suffix : suffixes) {
		if (lower.endsWith(suffix)) {
			return true;
		}
	}
	return false;
}

MaterialLibraryEntry entryFor(const MaterialDefinition& definition)
{
	MaterialLibraryEntry entry;
	entry.name = definition.name;
	entry.engine = definition.engine;
	entry.kind = definition.kind;
	entry.sourcePath = definition.sourcePath;
	entry.sourceLayer = definition.sourceLayer;
	entry.line = definition.span.line;
	entry.animated = definition.isAnimated();
	entry.sky = definition.isSky();
	entry.light = definition.isLight();
	entry.fog = definition.isFog();
	entry.translucent = definition.translucent || definition.coverage == QStringLiteral("translucent")
		|| (definition.engine == MaterialEngine::Quake2 && (definition.classic.surfaceFlags & (kQuake2SurfTrans33 | kQuake2SurfTrans66)) != 0);
	entry.rejected = !definition.engineRejection.isEmpty();
	entry.stageCount = static_cast<int>(definition.stages.size());
	entry.errors = definition.errorCount();
	entry.warnings = definition.warningCount();
	entry.images = definition.imageReferences();
	entry.thumbnailImage = definition.editorImage;
	if (entry.thumbnailImage.isEmpty()) {
		for (const MaterialStage& stage : definition.stages) {
			if (stage.imageKind == MaterialImageKind::File && !stage.imagePath.isEmpty()) {
				entry.thumbnailImage = stage.imagePath;
				break;
			}
		}
	}
	if (entry.thumbnailImage.isEmpty() && !definition.classic.kind.isEmpty()) {
		entry.thumbnailImage = definition.name;
	}
	return entry;
}

} // namespace

int MaterialLibrary::indexOf(const QString& name, MaterialEngine engine) const
{
	const QString key = materialLookupKey(name);
	int fallback = -1;
	for (int index = 0; index < entries.size(); ++index) {
		const MaterialLibraryEntry& entry = entries.at(index);
		if (materialLookupKey(entry.name) != key) {
			continue;
		}
		if (engine != MaterialEngine::Unknown && entry.engine != engine) {
			continue;
		}
		if (!entry.shadowed) {
			return index;
		}
		if (fallback < 0) {
			fallback = index;
		}
	}
	return fallback;
}

MaterialDefinition MaterialLibrary::definition(int entry) const
{
	if (entry < 0 || entry >= entries.size()) {
		return {};
	}
	const MaterialLibraryEntry& item = entries.at(entry);
	if (item.scriptIndex >= 0 && item.scriptIndex < scripts.size()) {
		const MaterialScript& script = scripts.at(item.scriptIndex);
		if (item.materialIndex >= 0 && item.materialIndex < script.materials.size()) {
			return script.materials.at(item.materialIndex);
		}
	}
	if (item.scriptIndex == -1 && item.materialIndex >= 0 && item.materialIndex < ownDefinitions.size()) {
		return ownDefinitions.at(item.materialIndex);
	}
	if (item.scriptIndex == -2 && item.materialIndex >= 0 && item.materialIndex < implicitDefinitions.size()) {
		return implicitDefinitions.at(item.materialIndex);
	}
	return {};
}

const MaterialScript* MaterialLibrary::script(int entry) const
{
	if (entry < 0 || entry >= entries.size()) {
		return nullptr;
	}
	const int index = entries.at(entry).scriptIndex;
	return index >= 0 && index < scripts.size() ? &scripts.at(index) : nullptr;
}

QVector<MaterialEngine> MaterialLibrary::engines() const
{
	QVector<MaterialEngine> list;
	for (const MaterialLibraryEntry& entry : entries) {
		if (!list.contains(entry.engine)) {
			list.push_back(entry.engine);
		}
	}
	std::sort(list.begin(), list.end(), [](MaterialEngine a, MaterialEngine b) { return static_cast<int>(a) < static_cast<int>(b); });
	return list;
}

int MaterialLibrary::count(MaterialEngine engine) const
{
	return static_cast<int>(std::count_if(entries.cbegin(), entries.cend(), [engine](const MaterialLibraryEntry& entry) { return entry.engine == engine; }));
}

QVector<int> MaterialLibrary::entriesUsingImage(const QString& image) const
{
	QVector<int> result;
	const QString key = materialLookupKey(withoutSuffix(image));
	for (int index = 0; index < entries.size(); ++index) {
		for (const QString& reference : entries.at(index).images) {
			if (materialLookupKey(withoutSuffix(reference)) == key) {
				result.push_back(index);
				break;
			}
		}
	}
	return result;
}

void MaterialLibrary::replaceScript(int scriptIndex, const MaterialScript& script)
{
	if (scriptIndex < 0 || scriptIndex >= scripts.size()) {
		return;
	}
	scripts[scriptIndex] = script;
	if (script.engine == MaterialEngine::Doom3) {
		MaterialTableSet rebuilt;
		for (const MaterialScript& each : std::as_const(scripts)) {
			rebuilt.addAll(each.tables);
		}
		rebuilt.addStandIns();
		tables = rebuilt;
	}
	rebuildEntries();
}

int MaterialLibrary::addScript(const MaterialScript& script)
{
	scripts.push_back(script);
	if (script.engine == MaterialEngine::Doom3) {
		tables.addAll(script.tables);
	}
	rebuildEntries();
	return static_cast<int>(scripts.size()) - 1;
}

int MaterialLibrary::adoptScript(MaterialScript script, const QString& virtualPath)
{
	if (!virtualPath.isEmpty()) {
		script.path = virtualPath;
	}
	for (int index = 0; index < scripts.size() && !script.path.isEmpty(); ++index) {
		if (scripts.at(index).path.compare(script.path, Qt::CaseInsensitive) == 0) {
			replaceScript(index, script);
			return index;
		}
	}
	return addScript(script);
}

QString materialScriptVirtualPath(const QString& filePath, const QString& packageRoot, MaterialEngine engine)
{
	const QFileInfo file(filePath);
	if (!packageRoot.isEmpty() && QFileInfo(packageRoot).isDir()) {
		const QString relative = QDir(packageRoot).relativeFilePath(file.absoluteFilePath());
		if (!relative.startsWith(QStringLiteral("../")) && relative != QStringLiteral("..") && !QDir::isAbsolutePath(relative)) {
			return relative;
		}
	}
	const QString folder = engine == MaterialEngine::Doom3 ? QStringLiteral("materials/") : QStringLiteral("scripts/");
	return folder + file.fileName();
}

void MaterialLibrary::rebuildEntries()
{
	entries.clear();
	QSet<QString> defined;
	for (int scriptIndex = 0; scriptIndex < scripts.size(); ++scriptIndex) {
		const MaterialScript& script = scripts.at(scriptIndex);
		for (int materialIndex = 0; materialIndex < script.materials.size(); ++materialIndex) {
			MaterialLibraryEntry entry = entryFor(script.materials.at(materialIndex));
			entry.scriptIndex = scriptIndex;
			entry.materialIndex = materialIndex;
			if (entry.sourcePath.isEmpty()) {
				entry.sourcePath = script.path;
			}
			defined.insert(materialLookupKey(entry.name));
			entries.push_back(entry);
		}
	}
	for (int index = 0; index < ownDefinitions.size(); ++index) {
		MaterialLibraryEntry entry = entryFor(ownDefinitions.at(index));
		entry.scriptIndex = -1;
		entry.materialIndex = index;
		entries.push_back(entry);
	}
	implicitDefinitions.clear();
	for (const QString& image : std::as_const(implicitImages)) {
		const QString name = withoutSuffix(image);
		if (defined.contains(materialLookupKey(name))) {
			continue;
		}
		MaterialDefinition definition = implicitEngine == MaterialEngine::Doom3 ? implicitDoom3Material(name)
																				 : implicitQuake3Material(name, MaterialSurfaceContext::World, image);
		definition.sourcePath = image;
		MaterialLibraryEntry entry = entryFor(definition);
		entry.scriptIndex = -2;
		entry.materialIndex = static_cast<int>(implicitDefinitions.size());
		entry.thumbnailImage = image;
		implicitDefinitions.push_back(definition);
		entries.push_back(entry);
	}
	resolveShadowing();
}

void MaterialLibrary::resolveShadowing()
{
	// Group by engine and name.
	QHash<QString, QVector<int>> groups;
	for (int index = 0; index < entries.size(); ++index) {
		groups[materialEngineId(entries.at(index).engine) + QLatin1Char('|') + materialLookupKey(entries.at(index).name)].push_back(index);
	}
	for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
		const QVector<int>& members = it.value();
		if (members.size() < 2) {
			continue;
		}
		int winner = members.first();
		const MaterialEngine engine = entries.at(winner).engine;
		if (engine == MaterialEngine::Quake3) {
			// ioquake3: the alphabetically last script wins; inside one
			// script, its first definition.
			for (int member : members) {
				const QString path = entries.at(member).sourcePath.toLower();
				const QString best = entries.at(winner).sourcePath.toLower();
				if (path > best) {
					winner = member;
				}
			}
		}
		// Doom 3 keeps the first definition parsed, files in sorted order;
		// classic entries keep the last lump, which the scan put first.
		for (int member : members) {
			if (member == winner) {
				continue;
			}
			entries[member].shadowed = true;
			entries[member].shadowedBy = entries.at(winner).sourcePath;
		}
	}
}

MaterialEngine detectMaterialEngine(const PackageArchiveReader& reader)
{
	int shaders = 0;
	int mtr = 0;
	int wal = 0;
	int miptex = 0;
	int flats = 0;
	int doomLumps = 0;
	int truecolor = 0;
	for (const PackageEntry& entry : reader.entries()) {
		if (entry.kind != PackageEntryKind::File) {
			continue;
		}
		const QString suffix = suffixOf(entry.virtualPath);
		const QString lower = entry.virtualPath.toLower();
		if (suffix == QStringLiteral("shader")) {
			++shaders;
		} else if (suffix == QStringLiteral("mtr")) {
			++mtr;
		} else if (suffix == QStringLiteral("wal")) {
			++wal;
		} else if (entry.wadLumpType == 0x44 || entry.wadLumpType == 0x43) {
			++miptex;
		} else if (entry.typeHint == QStringLiteral("wad-flat") || lower.startsWith(QStringLiteral("flats/"))) {
			++flats;
		} else if (lower == QStringLiteral("texture1") || lower == QStringLiteral("pnames") || lower == QStringLiteral("playpal")) {
			++doomLumps;
		} else if (lower.startsWith(QStringLiteral("textures/")) && isImageSuffix(suffix)) {
			++truecolor;
		}
	}
	if (mtr > 0) {
		return MaterialEngine::Doom3;
	}
	if (shaders > 0) {
		return MaterialEngine::Quake3;
	}
	if (wal > 0) {
		return MaterialEngine::Quake2;
	}
	if (miptex > 0) {
		return MaterialEngine::Quake;
	}
	if (flats > 0 || doomLumps > 0) {
		return MaterialEngine::Doom;
	}
	if (truecolor > 0) {
		return MaterialEngine::Quake3;
	}
	return MaterialEngine::Unknown;
}

MaterialLibrary scanMaterialLibrary(const PackageArchiveReader& reader, const MaterialLibraryOptions& options, const MaterialLibraryProgress& progress)
{
	MaterialLibrary library;
	library.sourcePath = reader.sourcePath();
	const QVector<PackageEntry> entries = reader.entries();
	const auto wanted = [&](MaterialEngine engine) { return options.engines.isEmpty() || options.engines.contains(engine); };
	const auto report = [&](int done, int total, const QString& phase) {
		if (progress && !progress(done, total, phase)) {
			library.cancelled = true;
			library.complete = false;
			return false;
		}
		return true;
	};
	qint64 totalBytes = 0;

	// 1. Scripts, in sorted path order.
	QVector<int> scriptEntries;
	for (int index = 0; index < entries.size(); ++index) {
		const PackageEntry& entry = entries.at(index);
		if (entry.kind != PackageEntryKind::File) {
			continue;
		}
		const MaterialEngine engine = materialEngineForScriptPath(entry.virtualPath);
		if (engine != MaterialEngine::Unknown && wanted(engine)) {
			scriptEntries.push_back(index);
		}
	}
	std::sort(scriptEntries.begin(), scriptEntries.end(), [&](int a, int b) {
		return entries.at(a).virtualPath.compare(entries.at(b).virtualPath, Qt::CaseInsensitive) < 0;
	});
	if (scriptEntries.size() > options.maximumScripts) {
		library.warnings << Text::tr("Only the first %1 of %2 scripts are read.").arg(options.maximumScripts).arg(scriptEntries.size());
		scriptEntries.resize(options.maximumScripts);
		library.complete = false;
	}
	const QString scriptsPhase = Text::tr("Reading material scripts");
	for (int position = 0; position < scriptEntries.size(); ++position) {
		if (!report(position, static_cast<int>(scriptEntries.size()), scriptsPhase)) {
			return library;
		}
		const PackageEntry& entry = entries.at(scriptEntries.at(position));
		if (entry.sizeBytes > static_cast<quint64>(options.maximumScriptBytes) || totalBytes + static_cast<qint64>(entry.sizeBytes) > options.maximumTotalBytes) {
			library.warnings << Text::tr("%1 is skipped: it is larger than the read budget.").arg(entry.virtualPath);
			library.complete = false;
			continue;
		}
		QByteArray bytes;
		QString error;
		if (!reader.readEntryAt(scriptEntries.at(position), &bytes, &error, options.maximumScriptBytes)) {
			library.warnings << Text::tr("%1 could not be read: %2").arg(entry.virtualPath, error);
			continue;
		}
		totalBytes += bytes.size();
		MaterialScript script = parseMaterialScript(decodeText(bytes), MaterialEngine::Unknown, entry.virtualPath);
		const QString layer = entry.layerId.isEmpty() ? entry.sourceArchiveId : entry.layerId;
		for (MaterialDefinition& definition : script.materials) {
			definition.sourceLayer = layer;
		}
		if (script.engine == MaterialEngine::Doom3) {
			library.tables.addAll(script.tables);
		}
		library.scripts.push_back(script);
	}
	library.tables.addStandIns();

	// 2. Images no script defines: Quake III or Doom 3 implicit materials.
	const MaterialEngine detected = detectMaterialEngine(reader);
	if (options.includeImplicit && (detected == MaterialEngine::Quake3 || detected == MaterialEngine::Doom3) && wanted(detected)) {
		library.implicitEngine = detected;
		QSet<QString> seen;
		for (const PackageEntry& entry : entries) {
			if (entry.kind != PackageEntryKind::File || !entry.virtualPath.startsWith(QStringLiteral("textures/"), Qt::CaseInsensitive)) {
				continue;
			}
			const QString suffix = suffixOf(entry.virtualPath);
			if (!isImageSuffix(suffix) || suffix == QStringLiteral("dds")) {
				continue;
			}
			const QString base = withoutSuffix(entry.virtualPath);
			if (detected == MaterialEngine::Doom3 && isDoom3CompanionImage(base)) {
				continue;
			}
			const QString key = materialLookupKey(base);
			if (seen.contains(key)) {
				continue;
			}
			seen.insert(key);
			library.implicitImages << entry.virtualPath;
		}
		std::sort(library.implicitImages.begin(), library.implicitImages.end(), [](const QString& a, const QString& b) {
			return a.compare(b, Qt::CaseInsensitive) < 0;
		});
	}

	// 3. Classic textures.
	if (options.includeClassic) {
		if (wanted(MaterialEngine::Doom)
			&& std::any_of(entries.cbegin(), entries.cend(), [](const PackageEntry& entry) {
				   const QString name = packageVirtualPathFileName(entry.virtualPath).toUpper();
				   return entry.typeHint == QStringLiteral("wad-flat") || name == QStringLiteral("TEXTURE1") || name == QStringLiteral("PNAMES")
					   || entry.virtualPath.startsWith(QStringLiteral("flats/"), Qt::CaseInsensitive);
			   })) {
			if (!report(0, 1, Text::tr("Reading Doom textures"))) {
				return library;
			}
			auto catalog = std::make_shared<DoomMaterialCatalog>(readDoomMaterialCatalog(reader));
			library.warnings += catalog->warnings;
			for (MaterialDefinition definition : doomMaterialDefinitions(*catalog)) {
				definition.sourcePath = definition.classic.definitionLump.isEmpty() ? definition.name : definition.classic.definitionLump;
				library.ownDefinitions.push_back(definition);
			}
			library.doomCatalog = catalog;
		}
		if (wanted(MaterialEngine::Quake)) {
			QStringList names;
			QVector<int> indexes;
			for (int index = 0; index < entries.size(); ++index) {
				const PackageEntry& entry = entries.at(index);
				if (entry.kind == PackageEntryKind::File && (entry.wadLumpType == 0x44 || entry.wadLumpType == 0x43)) {
					names << packageVirtualPathFileName(entry.virtualPath);
					indexes << index;
				}
			}
			for (int position = 0; position < indexes.size(); ++position) {
				if (position % 64 == 0 && !report(position, static_cast<int>(indexes.size()), Text::tr("Reading Quake textures"))) {
					return library;
				}
				const PackageEntry& entry = entries.at(indexes.at(position));
				QByteArray header;
				QString error;
				QSize size;
				if (reader.readEntryAt(indexes.at(position), &header, &error, 24) && header.size() >= 24) {
					size = QSize(static_cast<int>(qFromLittleEndian<quint32>(header.constData() + 16)),
						static_cast<int>(qFromLittleEndian<quint32>(header.constData() + 20)));
				}
				MaterialDefinition definition = quakeMaterialDefinition(names.at(position), names, size);
				definition.sourcePath = entry.virtualPath;
				definition.sourceLayer = entry.layerId.isEmpty() ? entry.sourceArchiveId : entry.layerId;
				library.ownDefinitions.push_back(definition);
			}
		}
		if (wanted(MaterialEngine::Quake2)) {
			QHash<QString, Quake2WalInfo> infos;
			QHash<QString, QString> paths;
			QStringList order;
			for (int index = 0; index < entries.size(); ++index) {
				const PackageEntry& entry = entries.at(index);
				if (entry.kind != PackageEntryKind::File || suffixOf(entry.virtualPath) != QStringLiteral("wal")) {
					continue;
				}
				QByteArray header;
				QString error;
				Quake2WalInfo info;
				if (!reader.readEntryAt(index, &header, &error, 100) || !readQuake2WalInfo(header, &info, &error)) {
					library.warnings << Text::tr("%1 is not a readable WAL: %2").arg(entry.virtualPath, error);
					continue;
				}
				QString name = withoutSuffix(entry.virtualPath);
				if (name.startsWith(QStringLiteral("textures/"), Qt::CaseInsensitive)) {
					name = name.mid(9);
				}
				infos.insert(materialLookupKey(name), info);
				paths.insert(materialLookupKey(name), entry.virtualPath);
				order << name;
			}
			// ericw-tools .wal_json sidecars override the WAL header.
			for (int index = 0; index < entries.size(); ++index) {
				const PackageEntry& entry = entries.at(index);
				if (entry.kind != PackageEntryKind::File || suffixOf(entry.virtualPath) != QStringLiteral("wal_json")) {
					continue;
				}
				QString name = withoutSuffix(entry.virtualPath);
				if (name.startsWith(QStringLiteral("textures/"), Qt::CaseInsensitive)) {
					name = name.mid(9);
				}
				QByteArray bytes;
				QString error;
				if (!reader.readEntryAt(index, &bytes, &error, 64 * 1024)) {
					continue;
				}
				const QString key = materialLookupKey(name);
				Quake2WalInfo info = infos.value(key);
				if (info.name.isEmpty()) {
					info.name = name;
					order << name;
					paths.insert(key, entry.virtualPath);
				}
				if (parseQuake2WalInfoText(decodeText(bytes), &info, &error)) {
					infos.insert(key, info);
				} else {
					library.warnings << Text::tr("%1: %2").arg(entry.virtualPath, error);
				}
			}
			for (const QString& name : std::as_const(order)) {
				const Quake2WalInfo info = infos.value(materialLookupKey(name));
				MaterialDefinition definition = quake2MaterialDefinition(name, info, [&](const QString& next, Quake2WalInfo* out) {
					const auto found = infos.constFind(materialLookupKey(next));
					if (found == infos.constEnd()) {
						return false;
					}
					*out = *found;
					return true;
				});
				definition.sourcePath = paths.value(materialLookupKey(name));
				library.ownDefinitions.push_back(definition);
			}
		}
	}
	library.rebuildEntries();
	if (library.entries.size() > options.maximumEntries) {
		library.warnings << Text::tr("Only the first %1 materials are listed.").arg(options.maximumEntries);
		library.entries.resize(options.maximumEntries);
		library.complete = false;
	}
	report(1, 1, Text::tr("Materials ready"));
	return library;
}

StudioQueryProperties materialLibraryEntryQueryProperties(const MaterialLibraryEntry& entry, const MaterialDefinition& definition)
{
	StudioQueryProperties properties;
	const auto yesNo = [](bool value) { return value ? QStringLiteral("yes") : QStringLiteral("no"); };
	properties.insert(QStringLiteral("name"), entry.name);
	properties.insert(QStringLiteral("engine"), materialEngineId(entry.engine));
	properties.insert(QStringLiteral("kind"), entry.kind);
	properties.insert(QStringLiteral("source"), entry.sourcePath);
	properties.insert(QStringLiteral("stages"), QString::number(entry.stageCount));
	properties.insert(QStringLiteral("images"), QString::number(entry.images.size()));
	properties.insert(QStringLiteral("animated"), yesNo(entry.animated));
	properties.insert(QStringLiteral("sky"), yesNo(entry.sky));
	properties.insert(QStringLiteral("light"), yesNo(entry.light));
	properties.insert(QStringLiteral("fog"), yesNo(entry.fog));
	properties.insert(QStringLiteral("translucent"), yesNo(entry.translucent));
	properties.insert(QStringLiteral("rejected"), yesNo(entry.rejected));
	properties.insert(QStringLiteral("shadowed"), yesNo(entry.shadowed));
	properties.insert(QStringLiteral("errors"), QString::number(entry.errors));
	properties.insert(QStringLiteral("warnings"), QString::number(entry.warnings));
	properties.insert(QStringLiteral("frames"), QString::number(definition.classic.frames.size()));
	if (!definition.sort.isEmpty()) {
		properties.insert(QStringLiteral("sort"), definition.sort);
	}
	properties.insert(QStringLiteral("cull"), materialCullId(definition.cull));
	for (const QString& parm : definition.surfaceParms) {
		properties.insert(parm, QStringLiteral("yes"));
		properties.insert(QStringLiteral("surfaceparm"), parm);
	}
	for (const QString& flag : definition.flags) {
		properties.insert(flag, QStringLiteral("yes"));
	}
	if (definition.engine == MaterialEngine::Quake2) {
		for (const QString& flag : quake2FlagIds(definition.classic.surfaceFlags, quake2SurfaceFlagDescriptors())) {
			properties.insert(flag, QStringLiteral("yes"));
		}
		properties.insert(QStringLiteral("value"), QString::number(definition.classic.surfaceValue));
	}
	QStringList blends;
	for (const MaterialStage& stage : definition.stages) {
		blends << materialBlendFactorId(stage.blend.source) + QLatin1Char(' ') + materialBlendFactorId(stage.blend.destination);
		if (!stage.blend.written.isEmpty()) {
			blends << stage.blend.written;
		}
	}
	if (!blends.isEmpty()) {
		properties.insert(QStringLiteral("blend"), blends.join(QStringLiteral(", ")));
	}
	if (!entry.images.isEmpty()) {
		properties.insert(QStringLiteral("image"), entry.images.join(QLatin1Char(' ')));
	}
	return properties;
}

QJsonObject materialLibraryEntryJson(const MaterialLibraryEntry& entry)
{
	QJsonObject object;
	object.insert(QStringLiteral("name"), entry.name);
	object.insert(QStringLiteral("engine"), materialEngineId(entry.engine));
	object.insert(QStringLiteral("kind"), entry.kind);
	object.insert(QStringLiteral("source"), entry.sourcePath);
	if (entry.line > 0) {
		object.insert(QStringLiteral("line"), entry.line);
	}
	object.insert(QStringLiteral("stages"), entry.stageCount);
	object.insert(QStringLiteral("animated"), entry.animated);
	object.insert(QStringLiteral("sky"), entry.sky);
	object.insert(QStringLiteral("light"), entry.light);
	object.insert(QStringLiteral("fog"), entry.fog);
	object.insert(QStringLiteral("translucent"), entry.translucent);
	object.insert(QStringLiteral("rejected"), entry.rejected);
	object.insert(QStringLiteral("errors"), entry.errors);
	object.insert(QStringLiteral("warnings"), entry.warnings);
	if (entry.shadowed) {
		object.insert(QStringLiteral("shadowedBy"), entry.shadowedBy);
	}
	QJsonArray images;
	for (const QString& image : entry.images) {
		images.append(image);
	}
	object.insert(QStringLiteral("images"), images);
	return object;
}

QJsonObject materialLibraryJson(const MaterialLibrary& library, bool includeEntries)
{
	QJsonObject object;
	object.insert(QStringLiteral("source"), library.sourcePath);
	object.insert(QStringLiteral("complete"), library.complete);
	object.insert(QStringLiteral("scripts"), library.scripts.size());
	object.insert(QStringLiteral("materials"), library.entries.size());
	QJsonObject counts;
	for (MaterialEngine engine : library.engines()) {
		counts.insert(materialEngineId(engine), library.count(engine));
	}
	object.insert(QStringLiteral("engines"), counts);
	QJsonArray tables;
	for (const QString& name : library.tables.names()) {
		const MaterialTable* table = library.tables.find(name);
		QJsonObject item;
		item.insert(QStringLiteral("name"), name);
		item.insert(QStringLiteral("values"), table ? table->values.size() : 0);
		item.insert(QStringLiteral("generated"), table && table->generated);
		tables.append(item);
	}
	object.insert(QStringLiteral("tables"), tables);
	QJsonArray warnings;
	for (const QString& warning : library.warnings) {
		warnings.append(warning);
	}
	object.insert(QStringLiteral("warnings"), warnings);
	if (includeEntries) {
		QJsonArray entries;
		for (const MaterialLibraryEntry& entry : library.entries) {
			entries.append(materialLibraryEntryJson(entry));
		}
		object.insert(QStringLiteral("entries"), entries);
	}
	return object;
}

QStringList materialLibrarySummaryLines(const MaterialLibrary& library)
{
	QStringList lines;
	const int materialCount = static_cast<int>(library.entries.size());
	lines << Text::tr("%n material(s)", nullptr, materialCount);
	for (MaterialEngine engine : library.engines()) {
		lines << Text::tr("  %1 (%2): %3").arg(materialEngineDisplayName(engine), materialEngineGeneration(engine)).arg(library.count(engine));
	}
	const int scriptCount = static_cast<int>(library.scripts.size());
	if (scriptCount > 0) {
		lines << Text::tr("%n script(s)", nullptr, scriptCount);
	}
	int errors = 0;
	int rejected = 0;
	int shadowed = 0;
	for (const MaterialLibraryEntry& entry : library.entries) {
		errors += entry.errors;
		rejected += entry.rejected ? 1 : 0;
		shadowed += entry.shadowed ? 1 : 0;
	}
	if (rejected > 0) {
		lines << Text::tr("%n material(s) the engine would drop", nullptr, rejected);
	}
	if (shadowed > 0) {
		lines << Text::tr("%n definition(s) shadowed by another of the same name", nullptr, shadowed);
	}
	if (errors > 0) {
		lines << Text::tr("%n error(s)", nullptr, errors);
	}
	lines += library.warnings;
	return lines;
}

} // namespace vibestudio
