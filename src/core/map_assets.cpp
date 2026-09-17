#include "core/map_assets.h"

#include "core/idtech_image.h"
#include "core/package_archive.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>

#include <algorithm>
#include <memory>

namespace vibestudio {

namespace {

QString assetText(const char* source)
{
	return QCoreApplication::translate("VibeStudioMapAssets", source);
}

// ---------------------------------------------------------------------------
// Limits
//
// Every bound below exists so a hostile or merely broken package cannot turn an
// audit into an unbounded allocation.
// ---------------------------------------------------------------------------

constexpr int kMaxUniqueTextures = 65536;
constexpr int kMaxShaderScripts = 4096;
constexpr int kMaxShaderNames = 200000;
constexpr qint64 kMaxShaderScriptBytes = 16LL * 1024LL * 1024LL;
constexpr qint64 kMaxDecodeBytes = 64LL * 1024LL * 1024LL;
constexpr qsizetype kMaxShaderTokens = 4000000;
// Vanilla Doom lump names are eight characters; see
// https://doomwiki.org/wiki/Lump.
constexpr qsizetype kDoomLumpNameLength = 8;

enum class TextureFamily {
	Unknown,
	Doom,
	Quake,
	Quake3,
};

TextureFamily textureFamilyFor(LevelMapFormat format, const QString& engineFamily)
{
	switch (format) {
	case LevelMapFormat::DoomWad:
		return TextureFamily::Doom;
	case LevelMapFormat::QuakeMap:
		return TextureFamily::Quake;
	case LevelMapFormat::Quake3Map:
		return TextureFamily::Quake3;
	case LevelMapFormat::Unknown:
		break;
	}

	const QString hint = engineFamily.trimmed().toLower();
	if (hint == QStringLiteral("idtech1")) {
		return TextureFamily::Doom;
	}
	if (hint == QStringLiteral("idtech2")) {
		return TextureFamily::Quake;
	}
	if (hint == QStringLiteral("idtech3")) {
		return TextureFamily::Quake3;
	}
	return TextureFamily::Unknown;
}

QString paletteIdForFamily(TextureFamily family)
{
	switch (family) {
	case TextureFamily::Doom:
		return QStringLiteral("doom");
	case TextureFamily::Quake:
		return QStringLiteral("quake");
	case TextureFamily::Quake3:
	case TextureFamily::Unknown:
		break;
	}
	return QStringLiteral("generic");
}

QString cleanTextureName(const QString& textureName)
{
	QString value = textureName.trimmed();
	value.replace(QLatin1Char('\\'), QLatin1Char('/'));
	return value;
}

// Untranslated reason token for a name the engine or the compiler provides, or
// an empty string when the name has to come from a package.
QString engineHandledReason(const QString& textureName, TextureFamily family)
{
	const QString value = cleanTextureName(textureName);
	if (value.isEmpty()) {
		return QString();
	}

	if (family == TextureFamily::Quake) {
		const QString lower = value.toLower();
		// Liquids are written `*water1`, `*lava1` and so on; the engine builds
		// them from the map's own miptex data. Any name starting with "sky" is
		// treated as a sky by the released qbsp sources.
		if (lower.startsWith(QLatin1Char('*'))) {
			return QStringLiteral("quake-liquid");
		}
		if (lower.startsWith(QStringLiteral("sky"))) {
			return QStringLiteral("quake-sky");
		}
		if (lower == QStringLiteral("clip") || lower == QStringLiteral("skip") || lower == QStringLiteral("trigger")
			|| lower == QStringLiteral("hint") || lower == QStringLiteral("origin")) {
			return QStringLiteral("quake-compiler-name");
		}
		return QString();
	}

	if (family == TextureFamily::Quake3) {
		const QString lower = value.toLower();
		if (lower.startsWith(QStringLiteral("textures/common/"))) {
			return QStringLiteral("quake3-common-family");
		}
		if (lower.startsWith(QStringLiteral("textures/editor/"))) {
			return QStringLiteral("quake3-editor-family");
		}
		if (lower == QStringLiteral("noshader")) {
			return QStringLiteral("quake3-noshader");
		}
		return QString();
	}

	return QString();
}

void addCandidate(QStringList* paths, const QString& candidate)
{
	if (!paths || candidate.isEmpty()) {
		return;
	}
	if (!paths->contains(candidate)) {
		paths->append(candidate);
	}
}

void addDoomCandidates(QStringList* paths, const QString& base)
{
	addCandidate(paths, base);
	addCandidate(paths, QStringLiteral("flats/") + base);
	addCandidate(paths, QStringLiteral("patches/") + base);
	addCandidate(paths, QStringLiteral("textures/") + base);
	addCandidate(paths, base + QStringLiteral(".lmp"));
	addCandidate(paths, QStringLiteral("flats/") + base + QStringLiteral(".lmp"));
	addCandidate(paths, QStringLiteral("patches/") + base + QStringLiteral(".lmp"));
}

struct CollectedReference {
	QString textureName;
	QString lookupKey;
	int useCount = 0;
	QVector<MapTextureUse> uses;
	bool usesTruncated = false;
};

struct ReferenceCollection {
	QVector<CollectedReference> references;
	QHash<QString, int> indexByKey;
	int totalUses = 0;
	bool truncated = false;
};

void addReferenceUse(ReferenceCollection* collection, const QString& textureName, TextureFamily family,
	MapTextureUseKind kind, int objectId, int entityId, int faceIndex, int line)
{
	if (!collection) {
		return;
	}
	const QString cleaned = cleanTextureName(textureName);
	if (cleaned.isEmpty()) {
		return;
	}
	// Doom's `-` placeholder means "no texture"; it is not a reference at all.
	// https://doomwiki.org/wiki/Sidedef
	if (family == TextureFamily::Doom && cleaned == QStringLiteral("-")) {
		return;
	}

	const QString key = family == TextureFamily::Doom ? cleaned.toUpper() : cleaned.toLower();
	int index = collection->indexByKey.value(key, -1);
	if (index < 0) {
		if (collection->references.size() >= kMaxUniqueTextures) {
			collection->truncated = true;
			return;
		}
		CollectedReference reference;
		reference.textureName = cleaned;
		reference.lookupKey = key;
		collection->references.append(reference);
		index = static_cast<int>(collection->references.size()) - 1;
		collection->indexByKey.insert(key, index);
	}

	CollectedReference& reference = collection->references[index];
	++reference.useCount;
	++collection->totalUses;
	if (reference.uses.size() >= kMapTextureUseSampleLimit) {
		reference.usesTruncated = true;
		return;
	}

	MapTextureUse use;
	use.kind = kind;
	use.objectId = objectId;
	use.entityId = entityId;
	use.faceIndex = faceIndex;
	use.line = line;
	reference.uses.append(use);
}

int worldspawnEntityId(const LevelMapDocument& document)
{
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) == 0) {
			return entity.id;
		}
	}
	return 0;
}

// Walks the document's own objects rather than the flat `textureReferences`
// list, because only the objects know where each name is written.
ReferenceCollection collectReferences(const LevelMapDocument& document, TextureFamily family)
{
	ReferenceCollection collection;
	const int worldspawnId = worldspawnEntityId(document);

	for (const LevelMapBrush& brush : document.brushes) {
		const MapTextureUseKind kind = brush.entityId == worldspawnId ? MapTextureUseKind::WorldspawnFace : MapTextureUseKind::BrushEntityFace;
		if (!brush.faces.isEmpty()) {
			for (qsizetype faceIndex = 0; faceIndex < brush.faces.size(); ++faceIndex) {
				const LevelMapBrushFace& face = brush.faces.at(faceIndex);
				addReferenceUse(&collection, face.textureName, family, kind, brush.id, brush.entityId,
					static_cast<int>(faceIndex), face.line);
			}
			continue;
		}
		// A brush parsed without per-face records still carries its texture
		// names; attribute them to the brush itself so nothing is lost.
		for (qsizetype nameIndex = 0; nameIndex < brush.textureNames.size(); ++nameIndex) {
			addReferenceUse(&collection, brush.textureNames.at(nameIndex), family, kind, brush.id, brush.entityId,
				static_cast<int>(nameIndex), brush.startLine);
		}
	}

	for (const LevelMapPatch& patch : document.patches) {
		addReferenceUse(&collection, patch.textureName, family, MapTextureUseKind::PatchShader, patch.id, patch.entityId, -1, patch.startLine);
	}

	// Binary Doom map lumps have no source line, so use sites record 0.
	for (const LevelMapDoomSidedef& sidedef : document.doomSidedefs) {
		addReferenceUse(&collection, sidedef.upperTexture, family, MapTextureUseKind::DoomSidedefUpper, sidedef.id, -1, -1, 0);
		addReferenceUse(&collection, sidedef.lowerTexture, family, MapTextureUseKind::DoomSidedefLower, sidedef.id, -1, -1, 0);
		addReferenceUse(&collection, sidedef.middleTexture, family, MapTextureUseKind::DoomSidedefMiddle, sidedef.id, -1, -1, 0);
	}

	for (const LevelMapDoomSector& sector : document.doomSectors) {
		addReferenceUse(&collection, sector.floorTexture, family, MapTextureUseKind::DoomSectorFloor, sector.id, -1, -1, 0);
		addReferenceUse(&collection, sector.ceilingTexture, family, MapTextureUseKind::DoomSectorCeiling, sector.id, -1, -1, 0);
	}

	return collection;
}

struct ArchiveIndex {
	// Lowercased virtual path -> the entry path exactly as the archive spells it.
	QHash<QString, QString> pathByKey;
	QHash<QString, QString> layerByKey;
	QStringList shaderScriptPaths;
};

ArchiveIndex buildArchiveIndex(const PackageArchiveReader& archive)
{
	ArchiveIndex index;
	const QVector<PackageEntry> entries = archive.entries();
	index.pathByKey.reserve(static_cast<int>(entries.size()));
	for (const PackageEntry& entry : entries) {
		if (entry.kind != PackageEntryKind::File || entry.virtualPath.isEmpty()) {
			continue;
		}
		const QString key = entry.virtualPath.toLower();
		// Later entries shadow earlier ones, matching pk3 mount semantics.
		index.pathByKey.insert(key, entry.virtualPath);
		index.layerByKey.insert(key, entry.layerId.isEmpty() ? entry.sourceArchiveId : entry.layerId);
		if (key.startsWith(QStringLiteral("scripts/")) && key.endsWith(QStringLiteral(".shader"))) {
			index.shaderScriptPaths.append(entry.virtualPath);
		}
	}
	std::sort(index.shaderScriptPaths.begin(), index.shaderScriptPaths.end());
	return index;
}

// Collects `scripts/*.shader` declarations so a Quake III name backed only by a
// shader definition is not reported as a missing image.
QHash<QString, QString> collectShaderDeclarations(const PackageArchiveReader& archive, const ArchiveIndex& index, QStringList* warnings)
{
	QHash<QString, QString> owners;
	int scriptCount = 0;
	for (const QString& scriptPath : index.shaderScriptPaths) {
		if (scriptCount >= kMaxShaderScripts) {
			if (warnings) {
				*warnings << assetText("Stopped after %1 shader scripts; the package declares more than the audit reads.").arg(kMaxShaderScripts);
			}
			break;
		}
		++scriptCount;

		QByteArray bytes;
		QString error;
		if (!archive.readEntryBytes(scriptPath, &bytes, &error, kMaxShaderScriptBytes)) {
			if (warnings) {
				*warnings << assetText("Unable to read shader script %1: %2").arg(scriptPath, error.isEmpty() ? assetText("unknown error") : error);
			}
			continue;
		}

		QStringList scriptWarnings;
		const QStringList names = collectShaderScriptNames(bytes, &scriptWarnings);
		for (const QString& scriptWarning : scriptWarnings) {
			if (warnings) {
				*warnings << assetText("%1: %2").arg(scriptPath, scriptWarning);
			}
		}
		for (const QString& name : names) {
			if (owners.size() >= kMaxShaderNames) {
				break;
			}
			const QString key = name.toLower();
			if (!owners.contains(key)) {
				owners.insert(key, scriptPath);
			}
		}
	}
	return owners;
}

QString useKindLabel(MapTextureUseKind kind)
{
	return mapTextureUseKindDisplayName(kind);
}

QString describeUse(const MapTextureUse& use)
{
	QString text = useKindLabel(use.kind);
	if (use.objectId >= 0) {
		text += QStringLiteral(" #%1").arg(use.objectId);
	}
	if (use.faceIndex >= 0) {
		text += QCoreApplication::translate("VibeStudioMapAssets", " face %1").arg(use.faceIndex);
	}
	if (use.line > 0) {
		text += QCoreApplication::translate("VibeStudioMapAssets", " line %1").arg(use.line);
	}
	return text;
}

// Shared core so the archive audit and the loose-folder audit cannot drift.
MapTextureAudit runAudit(const LevelMapDocument& document, const PackageArchiveReader& archive, bool decodeSizes, const QStringList& seedWarnings)
{
	MapTextureAudit audit;
	audit.mapName = document.mapName;
	audit.sourcePath = document.sourcePath;
	audit.engineFamily = document.engineFamily;
	audit.format = document.format;
	audit.packageSource = archive.sourcePath();
	audit.decodeRequested = decodeSizes;
	audit.warnings = seedWarnings;

	const TextureFamily family = textureFamilyFor(document.format, document.engineFamily);
	audit.paletteId = paletteIdForFamily(family);

	ReferenceCollection collection = collectReferences(document, family);
	audit.referenceCount = collection.totalUses;
	audit.uniqueCount = static_cast<int>(collection.references.size());
	if (collection.truncated) {
		audit.warnings << assetText("Stopped after %1 unique texture names; the map references more.").arg(kMaxUniqueTextures);
	}
	if (collection.references.isEmpty()) {
		if (!archive.isOpen()) {
			audit.warnings << assetText("The package is not open; no texture lookup was attempted.");
		}
		return audit;
	}

	if (!archive.isOpen()) {
		audit.warnings << assetText("The package is not open; every texture is reported as unresolved.");
	}

	const ArchiveIndex index = archive.isOpen() ? buildArchiveIndex(archive) : ArchiveIndex();
	QHash<QString, QString> shaderOwners;
	if (family == TextureFamily::Quake3 && archive.isOpen() && !index.shaderScriptPaths.isEmpty()) {
		shaderOwners = collectShaderDeclarations(archive, index, &audit.warnings);
	}
	audit.shaderNameCount = static_cast<int>(shaderOwners.size());

	// Sort before resolving so the reference list, the warnings and the JSON all
	// come out in the same order on every run.
	std::sort(collection.references.begin(), collection.references.end(),
		[](const CollectedReference& a, const CollectedReference& b) { return a.lookupKey < b.lookupKey; });

	bool paletteLoaded = false;
	IdTechPalette palette;

	audit.references.reserve(static_cast<int>(collection.references.size()));
	for (const CollectedReference& collected : collection.references) {
		MapTextureReference reference;
		reference.textureName = collected.textureName;
		reference.lookupKey = collected.lookupKey;
		reference.useCount = collected.useCount;
		reference.uses = collected.uses;
		reference.usesTruncated = collected.usesTruncated;
		reference.candidatePaths = mapTextureCandidatePaths(collected.textureName, document.format, document.engineFamily);
		reference.noteId = engineHandledReason(collected.textureName, family);
		reference.engineHandled = !reference.noteId.isEmpty();

		QString resolvedKey;
		for (const QString& candidate : reference.candidatePaths) {
			const QString key = candidate.toLower();
			if (index.pathByKey.contains(key)) {
				resolvedKey = key;
				break;
			}
		}

		if (!resolvedKey.isEmpty()) {
			reference.resolved = true;
			reference.resolution = MapTextureResolution::ResolvedEntry;
			reference.resolvedPath = index.pathByKey.value(resolvedKey);
			reference.sourceLayer = index.layerByKey.value(resolvedKey);
		} else if (family == TextureFamily::Quake3 && shaderOwners.contains(collected.lookupKey)) {
			reference.resolved = true;
			reference.resolution = MapTextureResolution::ResolvedByShader;
			reference.resolvedPath = shaderOwners.value(collected.lookupKey);
			reference.sourceLayer = index.layerByKey.value(reference.resolvedPath.toLower());
			if (reference.noteId.isEmpty()) {
				reference.noteId = QStringLiteral("quake3-shader-script");
			}
		} else if (reference.engineHandled) {
			reference.resolution = MapTextureResolution::EngineHandled;
		} else {
			reference.resolution = MapTextureResolution::Missing;
		}
		reference.resolutionId = mapTextureResolutionId(reference.resolution);

		if (decodeSizes && reference.resolution == MapTextureResolution::ResolvedEntry) {
			if (!paletteLoaded) {
				const IdTechPaletteResolution paletteResolution = resolveIdTechPalette(archive, audit.paletteId);
				palette = paletteResolution.palette;
				paletteLoaded = true;
			}
			QByteArray bytes;
			QString error;
			if (!archive.readEntryBytes(reference.resolvedPath, &bytes, &error, kMaxDecodeBytes)) {
				++audit.undecodableCount;
				audit.warnings << assetText("Unable to read %1 for %2: %3")
									  .arg(reference.resolvedPath, reference.textureName,
										  error.isEmpty() ? assetText("unknown error") : error);
			} else {
				const IdTechImageDecodeResult decoded = decodeIdTechImage(reference.resolvedPath, bytes, palette);
				if (decoded.decoded && decoded.width > 0 && decoded.height > 0) {
					reference.decoded = true;
					reference.width = decoded.width;
					reference.height = decoded.height;
					reference.formatId = decoded.formatId.isEmpty() ? idTechImageFormatId(decoded.format) : decoded.formatId;
				} else {
					++audit.undecodableCount;
					audit.warnings << assetText("Resolved %1 for %2 but could not decode it: %3")
										  .arg(reference.resolvedPath, reference.textureName,
											  decoded.error.isEmpty() ? assetText("unsupported image data") : decoded.error);
				}
			}
		}

		if (reference.resolved) {
			++audit.resolvedCount;
		}
		if (reference.engineHandled) {
			++audit.engineHandledCount;
		}
		if (reference.isMissing()) {
			++audit.missingCount;
		}
		audit.references.append(reference);
	}

	return audit;
}

// A read-only reader over one or more plain folders, for projects that keep
// loose assets on disk instead of inside a package. Later roots shadow earlier
// ones, matching the mount order used by PackageArchiveSession.
class DirectoryReader final : public PackageArchiveReader {
public:
	DirectoryReader(const QStringList& roots, QStringList* warnings)
	{
		QStringList sources;
		QHash<QString, int> entryIndexByKey;
		for (const QString& root : roots) {
			const QString trimmed = root.trimmed();
			if (trimmed.isEmpty()) {
				continue;
			}
			const QFileInfo info(trimmed);
			if (!info.exists() || !info.isDir()) {
				if (warnings) {
					*warnings << assetText("Asset folder not found: %1").arg(QDir::toNativeSeparators(trimmed));
				}
				continue;
			}

			auto archive = std::make_shared<PackageArchive>();
			QString error;
			if (!archive->load(info.absoluteFilePath(), &error)) {
				if (warnings) {
					*warnings << assetText("Unable to read asset folder %1: %2")
									 .arg(QDir::toNativeSeparators(info.absoluteFilePath()),
										 error.isEmpty() ? assetText("unknown error") : error);
				}
				continue;
			}

			const int archiveIndex = static_cast<int>(m_archives.size());
			m_archives.append(archive);
			sources.append(QDir::toNativeSeparators(info.absoluteFilePath()));
			const QString layerId = info.fileName().isEmpty() ? info.absoluteFilePath() : info.fileName();

			const QVector<PackageEntry> entries = archive->entries();
			for (const PackageEntry& entry : entries) {
				if (entry.kind != PackageEntryKind::File || entry.virtualPath.isEmpty()) {
					continue;
				}
				PackageEntry copy = entry;
				copy.layerId = layerId;
				const QString key = entry.virtualPath.toLower();
				m_ownerByKey.insert(key, archiveIndex);
				const int existing = entryIndexByKey.value(key, -1);
				if (existing >= 0) {
					m_entries[existing] = copy;
					continue;
				}
				m_entries.append(copy);
				entryIndexByKey.insert(key, static_cast<int>(m_entries.size()) - 1);
			}
		}
		m_sourcePath = sources.join(QStringLiteral("; "));
		m_open = !m_archives.isEmpty();
	}

	[[nodiscard]] PackageArchiveFormat format() const override { return PackageArchiveFormat::Folder; }
	[[nodiscard]] QString sourcePath() const override { return m_sourcePath; }
	[[nodiscard]] bool isOpen() const override { return m_open; }
	[[nodiscard]] QVector<PackageEntry> entries() const override { return m_entries; }

	bool readEntryBytes(const QString& virtualPath, QByteArray* out, QString* error, qint64 maxBytes = -1) const override
	{
		const int owner = m_ownerByKey.value(virtualPath.toLower(), -1);
		if (owner < 0 || owner >= static_cast<int>(m_archives.size())) {
			if (error) {
				*error = assetText("No asset folder provides this entry.");
			}
			return false;
		}
		return m_archives.at(owner)->readEntryBytes(virtualPath, out, error, maxBytes);
	}

private:
	QVector<std::shared_ptr<PackageArchive>> m_archives;
	QVector<PackageEntry> m_entries;
	QHash<QString, int> m_ownerByKey;
	QString m_sourcePath;
	bool m_open = false;
};

QJsonArray useArray(const QVector<MapTextureUse>& uses)
{
	QJsonArray array;
	for (const MapTextureUse& use : uses) {
		QJsonObject object;
		object.insert(QStringLiteral("kind"), mapTextureUseKindId(use.kind));
		object.insert(QStringLiteral("objectId"), use.objectId);
		object.insert(QStringLiteral("entityId"), use.entityId);
		object.insert(QStringLiteral("faceIndex"), use.faceIndex);
		object.insert(QStringLiteral("line"), use.line);
		array.append(object);
	}
	return array;
}

QJsonArray stringArray(const QStringList& values)
{
	QJsonArray array;
	for (const QString& value : values) {
		array.append(value);
	}
	return array;
}

} // namespace

bool MapTextureReference::isMissing() const
{
	return !resolved && !engineHandled;
}

OperationState MapTextureAudit::state() const
{
	if (references.isEmpty()) {
		return OperationState::Idle;
	}
	if (missingCount > 0) {
		return OperationState::Warning;
	}
	return OperationState::Completed;
}

QString mapTextureUseKindId(MapTextureUseKind kind)
{
	switch (kind) {
	case MapTextureUseKind::Unknown:
		return QStringLiteral("unknown");
	case MapTextureUseKind::WorldspawnFace:
		return QStringLiteral("worldspawn-face");
	case MapTextureUseKind::BrushEntityFace:
		return QStringLiteral("brush-entity-face");
	case MapTextureUseKind::PatchShader:
		return QStringLiteral("patch-shader");
	case MapTextureUseKind::DoomSidedefUpper:
		return QStringLiteral("doom-sidedef-upper");
	case MapTextureUseKind::DoomSidedefLower:
		return QStringLiteral("doom-sidedef-lower");
	case MapTextureUseKind::DoomSidedefMiddle:
		return QStringLiteral("doom-sidedef-middle");
	case MapTextureUseKind::DoomSectorFloor:
		return QStringLiteral("doom-sector-floor");
	case MapTextureUseKind::DoomSectorCeiling:
		return QStringLiteral("doom-sector-ceiling");
	}
	return QStringLiteral("unknown");
}

QString mapTextureUseKindDisplayName(MapTextureUseKind kind)
{
	switch (kind) {
	case MapTextureUseKind::Unknown:
		break;
	case MapTextureUseKind::WorldspawnFace:
		return assetText("Worldspawn brush face");
	case MapTextureUseKind::BrushEntityFace:
		return assetText("Brush entity face");
	case MapTextureUseKind::PatchShader:
		return assetText("Patch shader");
	case MapTextureUseKind::DoomSidedefUpper:
		return assetText("Sidedef upper");
	case MapTextureUseKind::DoomSidedefLower:
		return assetText("Sidedef lower");
	case MapTextureUseKind::DoomSidedefMiddle:
		return assetText("Sidedef middle");
	case MapTextureUseKind::DoomSectorFloor:
		return assetText("Sector floor");
	case MapTextureUseKind::DoomSectorCeiling:
		return assetText("Sector ceiling");
	}
	return assetText("Unknown use");
}

QString mapTextureResolutionId(MapTextureResolution resolution)
{
	switch (resolution) {
	case MapTextureResolution::Missing:
		return QStringLiteral("missing");
	case MapTextureResolution::ResolvedEntry:
		return QStringLiteral("resolved-entry");
	case MapTextureResolution::ResolvedByShader:
		return QStringLiteral("resolved-by-shader");
	case MapTextureResolution::EngineHandled:
		return QStringLiteral("engine-handled");
	}
	return QStringLiteral("missing");
}

QString mapTextureResolutionDisplayName(MapTextureResolution resolution)
{
	switch (resolution) {
	case MapTextureResolution::Missing:
		return assetText("Missing");
	case MapTextureResolution::ResolvedEntry:
		return assetText("Resolved");
	case MapTextureResolution::ResolvedByShader:
		return assetText("Resolved by shader script");
	case MapTextureResolution::EngineHandled:
		return assetText("Engine-handled");
	}
	return assetText("Missing");
}

QString normalizeMapTextureKey(const QString& textureName, LevelMapFormat format)
{
	const QString cleaned = cleanTextureName(textureName);
	if (cleaned.isEmpty()) {
		return QString();
	}
	return format == LevelMapFormat::DoomWad ? cleaned.toUpper() : cleaned.toLower();
}

bool isMapTexturePlaceholder(const QString& textureName, LevelMapFormat format)
{
	const QString cleaned = cleanTextureName(textureName);
	if (cleaned.isEmpty()) {
		return true;
	}
	return format == LevelMapFormat::DoomWad && cleaned == QStringLiteral("-");
}

bool isEngineHandledMapTexture(const QString& textureName, LevelMapFormat format, const QString& engineFamily)
{
	return !engineHandledReason(textureName, textureFamilyFor(format, engineFamily)).isEmpty();
}

QStringList mapTextureCandidatePaths(const QString& textureName, LevelMapFormat format, const QString& engineFamily)
{
	QStringList paths;
	const QString cleaned = cleanTextureName(textureName);
	if (cleaned.isEmpty() || isMapTexturePlaceholder(cleaned, format)) {
		return paths;
	}

	const TextureFamily family = textureFamilyFor(format, engineFamily);
	switch (family) {
	case TextureFamily::Doom: {
		// Doom lump names are uppercase and eight characters long; flats and wall
		// patches live in their own WAD namespaces, which extracted trees usually
		// keep as `flats/` and `patches/` folders.
		// https://doomwiki.org/wiki/Flat, https://doomwiki.org/wiki/Patch
		const QString upper = cleaned.toUpper();
		addDoomCandidates(&paths, upper);
		if (upper.size() > kDoomLumpNameLength) {
			addDoomCandidates(&paths, upper.left(kDoomLumpNameLength));
		}
		break;
	}
	case TextureFamily::Quake: {
		// Quake miptex names live in a WAD2 or inside the BSP; extracted trees and
		// Quake II packages put the same names under `textures/`.
		const QString lower = cleaned.toLower();
		addCandidate(&paths, lower);
		addCandidate(&paths, QStringLiteral("textures/") + lower);
		addCandidate(&paths, lower + QStringLiteral(".mip"));
		addCandidate(&paths, QStringLiteral("textures/") + lower + QStringLiteral(".mip"));
		addCandidate(&paths, lower + QStringLiteral(".lmp"));
		addCandidate(&paths, QStringLiteral("gfx/") + lower + QStringLiteral(".lmp"));
		// Quake II keeps the same family of names as `.wal` files.
		addCandidate(&paths, lower + QStringLiteral(".wal"));
		addCandidate(&paths, QStringLiteral("textures/") + lower + QStringLiteral(".wal"));
		break;
	}
	case TextureFamily::Quake3: {
		// Quake III shader names are already virtual paths; a name with no shader
		// definition falls back to an image file of the same name.
		const QString lower = cleaned.toLower();
		addCandidate(&paths, lower);
		addCandidate(&paths, lower + QStringLiteral(".tga"));
		addCandidate(&paths, lower + QStringLiteral(".jpg"));
		addCandidate(&paths, lower + QStringLiteral(".jpeg"));
		addCandidate(&paths, lower + QStringLiteral(".png"));
		break;
	}
	case TextureFamily::Unknown: {
		const QString lower = cleaned.toLower();
		addCandidate(&paths, lower);
		addCandidate(&paths, QStringLiteral("textures/") + lower);
		addCandidate(&paths, lower + QStringLiteral(".tga"));
		addCandidate(&paths, lower + QStringLiteral(".png"));
		break;
	}
	}
	return paths;
}

QStringList collectShaderScriptNames(const QByteArray& bytes, QStringList* warnings)
{
	QStringList names;
	if (bytes.isEmpty()) {
		return names;
	}

	// Quake III shader scripts are plain text: a shader name at brace depth 0 is
	// followed by a `{ ... }` body that may nest stage blocks.
	// https://www.qeradiant.com/manual/Q3AShader_Manual/
	const QString text = QString::fromLatin1(bytes);
	const qsizetype length = text.size();
	qsizetype position = 0;
	qsizetype tokenCount = 0;
	int depth = 0;
	QString pendingName;
	bool unterminatedComment = false;

	while (position < length) {
		const QChar current = text.at(position);
		if (current.isSpace()) {
			++position;
			continue;
		}
		if (current == QLatin1Char('/') && position + 1 < length) {
			const QChar next = text.at(position + 1);
			if (next == QLatin1Char('/')) {
				while (position < length && text.at(position) != QLatin1Char('\n')) {
					++position;
				}
				continue;
			}
			if (next == QLatin1Char('*')) {
				const qsizetype end = text.indexOf(QStringLiteral("*/"), position + 2);
				if (end < 0) {
					unterminatedComment = true;
					position = length;
					continue;
				}
				position = end + 2;
				continue;
			}
		}

		if (++tokenCount > kMaxShaderTokens) {
			if (warnings) {
				*warnings << assetText("Stopped reading the shader script after %1 tokens.").arg(kMaxShaderTokens);
			}
			break;
		}

		if (current == QLatin1Char('{')) {
			if (depth == 0) {
				if (!pendingName.isEmpty()) {
					if (names.size() < kMaxShaderNames) {
						names.append(pendingName);
					}
					pendingName.clear();
				}
			}
			++depth;
			++position;
			continue;
		}
		if (current == QLatin1Char('}')) {
			if (depth <= 0) {
				if (warnings) {
					*warnings << assetText("Ignored an unmatched closing brace in the shader script.");
				}
				depth = 0;
			} else {
				--depth;
			}
			++position;
			continue;
		}

		if (current == QLatin1Char('"')) {
			const qsizetype start = position + 1;
			qsizetype end = start;
			while (end < length && text.at(end) != QLatin1Char('"') && text.at(end) != QLatin1Char('\n')) {
				++end;
			}
			if (depth == 0) {
				pendingName = text.mid(start, end - start).trimmed();
			}
			position = end < length && text.at(end) == QLatin1Char('"') ? end + 1 : end;
			continue;
		}

		const qsizetype start = position;
		while (position < length) {
			const QChar wordChar = text.at(position);
			if (wordChar.isSpace() || wordChar == QLatin1Char('{') || wordChar == QLatin1Char('}')) {
				break;
			}
			if (wordChar == QLatin1Char('/') && position + 1 < length
				&& (text.at(position + 1) == QLatin1Char('/') || text.at(position + 1) == QLatin1Char('*'))) {
				break;
			}
			++position;
		}
		if (position == start) {
			// Defensive: never let an unexpected character stall the scan.
			++position;
			continue;
		}
		if (depth == 0) {
			pendingName = text.mid(start, position - start);
		}
	}

	if (unterminatedComment && warnings) {
		*warnings << assetText("The shader script ends inside a block comment.");
	}
	if (depth != 0 && warnings) {
		*warnings << assetText("The shader script ends with %1 unclosed brace(s).").arg(depth);
	}
	if (names.size() >= kMaxShaderNames && warnings) {
		*warnings << assetText("Stopped after %1 shader names in one script.").arg(kMaxShaderNames);
	}
	return names;
}

MapTextureAudit auditLevelMapTextures(const LevelMapDocument& document, const PackageArchiveReader& archive, bool decodeSizes)
{
	return runAudit(document, archive, decodeSizes, QStringList());
}

MapTextureAudit auditLevelMapTexturesInDirectories(const LevelMapDocument& document, const QStringList& roots, bool decodeSizes)
{
	QStringList warnings;
	const DirectoryReader reader(roots, &warnings);
	if (!reader.isOpen()) {
		warnings << assetText("No readable asset folder was supplied.");
	}
	return runAudit(document, reader, decodeSizes, warnings);
}

QStringList mapTextureAuditLines(const MapTextureAudit& audit)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioMapAssets", "Texture audit: %1")
				 .arg(audit.mapName.isEmpty() ? assetText("(unnamed map)") : audit.mapName);
	lines << QCoreApplication::translate("VibeStudioMapAssets", "State: %1").arg(operationStateDisplayName(audit.state()));
	lines << QCoreApplication::translate("VibeStudioMapAssets", "Format: %1").arg(levelMapFormatDisplayName(audit.format));
	lines << QCoreApplication::translate("VibeStudioMapAssets", "Engine: %1")
				 .arg(audit.engineFamily.isEmpty() ? assetText("unknown") : audit.engineFamily);
	lines << QCoreApplication::translate("VibeStudioMapAssets", "Package: %1")
				 .arg(audit.packageSource.isEmpty() ? assetText("none") : audit.packageSource);
	lines << QCoreApplication::translate("VibeStudioMapAssets", "References: %1 across %2 unique name(s)")
				 .arg(audit.referenceCount)
				 .arg(audit.uniqueCount);
	lines << QCoreApplication::translate("VibeStudioMapAssets", "Resolved: %1  Engine-handled: %2  Missing: %3  Undecodable: %4")
				 .arg(audit.resolvedCount)
				 .arg(audit.engineHandledCount)
				 .arg(audit.missingCount)
				 .arg(audit.undecodableCount);
	if (audit.shaderNameCount > 0) {
		lines << QCoreApplication::translate("VibeStudioMapAssets", "Shader names declared by the package: %1").arg(audit.shaderNameCount);
	}

	if (audit.missingCount > 0) {
		lines << QString();
		lines << assetText("Missing textures");
		for (const MapTextureReference& reference : audit.references) {
			if (!reference.isMissing()) {
				continue;
			}
			QString line = QCoreApplication::translate("VibeStudioMapAssets", "  %1 (%2 use(s))").arg(reference.textureName).arg(reference.useCount);
			if (!reference.uses.isEmpty()) {
				line += QCoreApplication::translate("VibeStudioMapAssets", " first at %1").arg(describeUse(reference.uses.first()));
			}
			lines << line;
		}
	}

	if (audit.engineHandledCount > 0) {
		lines << QString();
		lines << assetText("Engine-handled names");
		for (const MapTextureReference& reference : audit.references) {
			if (!reference.engineHandled) {
				continue;
			}
			lines << QCoreApplication::translate("VibeStudioMapAssets", "  %1 (%2 use(s)) [%3]")
						 .arg(reference.textureName)
						 .arg(reference.useCount)
						 .arg(reference.noteId);
		}
	}

	if (!audit.references.isEmpty()) {
		lines << QString();
		lines << assetText("All references");
		for (const MapTextureReference& reference : audit.references) {
			QString line = QCoreApplication::translate("VibeStudioMapAssets", "  %1 - %2")
							   .arg(reference.textureName, mapTextureResolutionDisplayName(reference.resolution));
			if (!reference.resolvedPath.isEmpty()) {
				line += QCoreApplication::translate("VibeStudioMapAssets", " [%1]").arg(reference.resolvedPath);
			}
			if (reference.decoded) {
				line += QCoreApplication::translate("VibeStudioMapAssets", " %1x%2 %3")
							.arg(reference.width)
							.arg(reference.height)
							.arg(reference.formatId);
			}
			lines << line;
		}
	}

	if (!audit.warnings.isEmpty()) {
		lines << QString();
		lines << assetText("Warnings");
		for (const QString& warning : audit.warnings) {
			lines << QStringLiteral("  ") + warning;
		}
	}
	return lines;
}

QString mapTextureAuditText(const MapTextureAudit& audit)
{
	return mapTextureAuditLines(audit).join(QStringLiteral("\n"));
}

QJsonObject mapTextureAuditJson(const MapTextureAudit& audit)
{
	QJsonObject object;
	object.insert(QStringLiteral("map"), audit.mapName);
	object.insert(QStringLiteral("source"), audit.sourcePath);
	object.insert(QStringLiteral("engineFamily"), audit.engineFamily);
	object.insert(QStringLiteral("format"), levelMapFormatId(audit.format));
	object.insert(QStringLiteral("package"), audit.packageSource);
	object.insert(QStringLiteral("paletteId"), audit.paletteId);
	object.insert(QStringLiteral("decodeRequested"), audit.decodeRequested);
	object.insert(QStringLiteral("state"), operationStateId(audit.state()));

	QJsonObject totals;
	totals.insert(QStringLiteral("references"), audit.referenceCount);
	totals.insert(QStringLiteral("unique"), audit.uniqueCount);
	totals.insert(QStringLiteral("resolved"), audit.resolvedCount);
	totals.insert(QStringLiteral("missing"), audit.missingCount);
	totals.insert(QStringLiteral("engineHandled"), audit.engineHandledCount);
	totals.insert(QStringLiteral("undecodable"), audit.undecodableCount);
	totals.insert(QStringLiteral("shaderNames"), audit.shaderNameCount);
	object.insert(QStringLiteral("totals"), totals);

	QJsonArray references;
	for (const MapTextureReference& reference : audit.references) {
		QJsonObject entry;
		entry.insert(QStringLiteral("name"), reference.textureName);
		entry.insert(QStringLiteral("key"), reference.lookupKey);
		entry.insert(QStringLiteral("useCount"), reference.useCount);
		entry.insert(QStringLiteral("usesTruncated"), reference.usesTruncated);
		entry.insert(QStringLiteral("resolution"), reference.resolutionId);
		entry.insert(QStringLiteral("resolved"), reference.resolved);
		entry.insert(QStringLiteral("engineHandled"), reference.engineHandled);
		entry.insert(QStringLiteral("missing"), reference.isMissing());
		entry.insert(QStringLiteral("path"), reference.resolvedPath);
		entry.insert(QStringLiteral("layer"), reference.sourceLayer);
		entry.insert(QStringLiteral("decoded"), reference.decoded);
		entry.insert(QStringLiteral("width"), reference.width);
		entry.insert(QStringLiteral("height"), reference.height);
		entry.insert(QStringLiteral("imageFormat"), reference.formatId);
		entry.insert(QStringLiteral("note"), reference.noteId);
		entry.insert(QStringLiteral("candidates"), stringArray(reference.candidatePaths));
		entry.insert(QStringLiteral("uses"), useArray(reference.uses));
		references.append(entry);
	}
	object.insert(QStringLiteral("references"), references);
	object.insert(QStringLiteral("warnings"), stringArray(audit.warnings));
	return object;
}

} // namespace vibestudio
