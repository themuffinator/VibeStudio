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

bool auditCancelled(const PackageReadControl& control)
{
	return control.isCancelled && control.isCancelled();
}

void appendAuditWarning(QStringList* warnings, const QString& message)
{
	if (!warnings) { return; }
	constexpr qsizetype maximum = 128;
	if (warnings->size() < maximum) { warnings->append(message); }
	else if (warnings->size() == maximum) {
		warnings->append(QCoreApplication::translate("VibeStudioMapAssets", "Further texture-audit diagnostics were truncated."));
	}
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
constexpr quint64 kMaxShaderAuditBytes = 64ULL * 1024ULL * 1024ULL;
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
		if (lower.startsWith(QStringLiteral("textures/common/")) || lower.startsWith(QStringLiteral("common/"))) {
			return QStringLiteral("quake3-common-family");
		}
		if (lower.startsWith(QStringLiteral("textures/editor/")) || lower.startsWith(QStringLiteral("editor/"))) {
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

int worldspawnEntityId(const LevelMapDocument& document, const PackageReadControl& control)
{
	for (const LevelMapEntity& entity : document.entities) {
		if (auditCancelled(control)) { return 0; }
		if (entity.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) == 0) {
			return entity.id;
		}
	}
	return 0;
}

// Walks the document's own objects rather than the flat `textureReferences`
// list, because only the objects know where each name is written.
ReferenceCollection collectReferences(const LevelMapDocument& document, TextureFamily family, const PackageReadControl& control)
{
	ReferenceCollection collection;
	const int worldspawnId = worldspawnEntityId(document, control);

	for (const LevelMapBrush& brush : document.brushes) {
		if (auditCancelled(control)) { return collection; }
		const MapTextureUseKind kind = brush.entityId == worldspawnId ? MapTextureUseKind::WorldspawnFace : MapTextureUseKind::BrushEntityFace;
		if (!brush.faces.isEmpty()) {
			for (qsizetype faceIndex = 0; faceIndex < brush.faces.size(); ++faceIndex) {
				if (auditCancelled(control)) { return collection; }
				const LevelMapBrushFace& face = brush.faces.at(faceIndex);
				addReferenceUse(&collection, face.textureName, family, kind, brush.id, brush.entityId,
					static_cast<int>(faceIndex), face.line);
			}
			continue;
		}
		// A brush parsed without per-face records still carries its texture
		// names; attribute them to the brush itself so nothing is lost.
		for (qsizetype nameIndex = 0; nameIndex < brush.textureNames.size(); ++nameIndex) {
			if (auditCancelled(control)) { return collection; }
			addReferenceUse(&collection, brush.textureNames.at(nameIndex), family, kind, brush.id, brush.entityId,
				static_cast<int>(nameIndex), brush.startLine);
		}
	}

	for (const LevelMapPatch& patch : document.patches) {
		if (auditCancelled(control)) { return collection; }
		addReferenceUse(&collection, patch.textureName, family, MapTextureUseKind::PatchShader, patch.id, patch.entityId, -1, patch.startLine);
	}

	// Binary Doom map lumps have no source line, so use sites record 0.
	for (const LevelMapDoomSidedef& sidedef : document.doomSidedefs) {
		if (auditCancelled(control)) { return collection; }
		addReferenceUse(&collection, sidedef.upperTexture, family, MapTextureUseKind::DoomSidedefUpper, sidedef.id, -1, -1, 0);
		addReferenceUse(&collection, sidedef.lowerTexture, family, MapTextureUseKind::DoomSidedefLower, sidedef.id, -1, -1, 0);
		addReferenceUse(&collection, sidedef.middleTexture, family, MapTextureUseKind::DoomSidedefMiddle, sidedef.id, -1, -1, 0);
	}

	for (const LevelMapDoomSector& sector : document.doomSectors) {
		if (auditCancelled(control)) { return collection; }
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
	QHash<QString, qsizetype> positionByKey;
};

ArchiveIndex buildArchiveIndex(const PackageArchiveReader& archive, const PackageReadControl& control)
{
	ArchiveIndex index;
	const QVector<PackageEntry> entries = archive.entries();
	index.pathByKey.reserve(static_cast<int>(entries.size()));
	for (qsizetype at = 0; at < entries.size(); ++at) {
		if (auditCancelled(control)) { return index; }
		const auto& entry = entries.at(at);
		if (entry.kind != PackageEntryKind::File || entry.virtualPath.isEmpty()) {
			continue;
		}
		const QString key = entry.virtualPath.toLower();
		// Later entries shadow earlier ones, matching pk3 mount semantics.
		index.positionByKey.insert(key, at);
		index.pathByKey.insert(key, entry.virtualPath);
		index.layerByKey.insert(key, entry.layerId.isEmpty() ? entry.sourceArchiveId : entry.layerId);
		if (key.startsWith(QStringLiteral("scripts/")) && key.endsWith(QStringLiteral(".shader"))) {
			index.shaderScriptPaths.append(entry.virtualPath);
		}
	}
	std::sort(index.shaderScriptPaths.begin(), index.shaderScriptPaths.end());
	index.shaderScriptPaths.removeDuplicates();
	return index;
}

// Read the exact indexed occurrence, with bounded buffering and cancellation
// inside native streaming readers. Default callers retain generic-reader support.
bool readAuditBytes(const PackageArchiveReader& archive, const ArchiveIndex& index, const QString& path,
	QByteArray* bytes, QString* error, qint64 limit, const PackageReadControl& control)
{
	const auto at = index.positionByKey.value(path.toLower(), -1);
	const auto entries = archive.entries();
	if (at < 0 || at >= entries.size() || !entries.at(at).readable || entries.at(at).sizeBytes > quint64(limit)) {
		if (error) { *error = QCoreApplication::translate("VibeStudioMapAssets", "The audit input is unavailable or exceeds its byte limit."); }
		return false;
	}
	if (auditCancelled(control)) { return false; }
	if (!control.isCancelled && !control.progress) {
		QByteArray complete;
		if (!archive.readEntryAt(at, &complete, error, limit)) { return false; }
		if (quint64(complete.size()) != entries.at(at).sizeBytes) {
			if (error) { *error = QCoreApplication::translate("VibeStudioMapAssets", "The audit input does not match its declared size."); }
			return false;
		}
		*bytes = std::move(complete); return true;
	}
	QByteArray buffered;
	const qint64 total = qint64(entries.at(at).sizeBytes);
	if (control.progress) { control.progress(path, 0, total); }
	const bool read = archive.streamEntryAt(at, [&](QByteArrayView chunk) {
		if (auditCancelled(control) || chunk.size() > limit - buffered.size()) { return false; }
		buffered.append(chunk.data(), chunk.size());
		if (control.progress) { control.progress(path, buffered.size(), total); }
		return !auditCancelled(control);
	}, error, control.isCancelled);
	if (!read || auditCancelled(control) || buffered.size() != total) { return false; }
	*bytes = std::move(buffered); return true;
}

// Collects `scripts/*.shader` declarations so a Quake III name backed only by a
// shader definition is not reported as a missing image.
QHash<QString, QString> collectShaderDeclarations(const PackageArchiveReader& archive, const ArchiveIndex& index, QStringList* warnings, const PackageReadControl& control)
{
	QHash<QString, QString> owners;
	int scriptCount = 0; quint64 admittedBytes = 0;
	const auto entries = archive.entries();
	for (const QString& scriptPath : index.shaderScriptPaths) {
		if (auditCancelled(control)) { return owners; }
		if (scriptCount >= kMaxShaderScripts) {
			if (warnings) {
				appendAuditWarning(warnings, QCoreApplication::translate("VibeStudioMapAssets", "Stopped after %1 shader scripts; the package declares more than the audit reads.").arg(kMaxShaderScripts));
			}
			break;
		}
		++scriptCount;
		const auto position = index.positionByKey.value(scriptPath.toLower(), -1);
		if (position < 0 || position >= entries.size()) { continue; }
		const auto size = entries.at(position).sizeBytes;
		if (size > kMaxShaderAuditBytes - admittedBytes) {
			appendAuditWarning(warnings, QCoreApplication::translate("VibeStudioMapAssets", "Shader scripts exceed the audit's aggregate byte limit."));
			break;
		}
		admittedBytes += size;

		QByteArray bytes;
		QString error;
		if (!readAuditBytes(archive, index, scriptPath, &bytes, &error, kMaxShaderScriptBytes, control)) {
			if (warnings) {
				appendAuditWarning(warnings, QCoreApplication::translate("VibeStudioMapAssets", "Unable to read shader script %1: %2").arg(scriptPath, error.isEmpty() ? QCoreApplication::translate("VibeStudioMapAssets", "unknown error") : error));
			}
			continue;
		}

		QStringList scriptWarnings;
		const QStringList names = collectShaderScriptNames(bytes, &scriptWarnings, control);
		for (const QString& scriptWarning : scriptWarnings) {
			if (warnings) {
				appendAuditWarning(warnings, QCoreApplication::translate("VibeStudioMapAssets", "%1: %2").arg(scriptPath, scriptWarning));
			}
		}
		for (const QString& name : names) {
			if (auditCancelled(control)) { return owners; }
			if (owners.size() >= kMaxShaderNames) {
				if (warnings) { appendAuditWarning(warnings, QCoreApplication::translate("VibeStudioMapAssets", "The audit reached its shader-name limit.")); }
				return owners;
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
MapTextureAudit runAudit(const LevelMapDocument& document, const PackageArchiveReader& archive, bool decodeSizes, const QStringList& seedWarnings, const PackageReadControl& control = {})
{
	MapTextureAudit audit;
	audit.mapName = document.mapName;
	audit.sourcePath = document.sourcePath;
	audit.engineFamily = document.engineFamily;
	audit.format = document.format;
	audit.packageSource = archive.sourcePath();
	audit.decodeRequested = decodeSizes;
	audit.sourceIndexComplete = archive.isOpen();
	audit.complete = archive.isOpen();
	audit.warnings = seedWarnings;

	const TextureFamily family = textureFamilyFor(document.format, document.engineFamily);
	audit.paletteId = paletteIdForFamily(family);

	ReferenceCollection collection = collectReferences(document, family, control);
	audit.referenceCount = collection.totalUses;
	audit.uniqueCount = static_cast<int>(collection.references.size());
	if (collection.truncated) {
		audit.complete = false;
		appendAuditWarning(&audit.warnings, QCoreApplication::translate("VibeStudioMapAssets", "Stopped after %1 unique texture names; the map references more.").arg(kMaxUniqueTextures));
	}
	if (collection.references.isEmpty()) {
		if (!archive.isOpen()) {
			appendAuditWarning(&audit.warnings, QCoreApplication::translate("VibeStudioMapAssets", "The package is not open; no texture lookup was attempted."));
		}
		return audit;
	}

	if (!archive.isOpen()) {
		appendAuditWarning(&audit.warnings, QCoreApplication::translate("VibeStudioMapAssets", "The package is not open; every texture is reported as unresolved."));
	}

	const ArchiveIndex index = archive.isOpen() ? buildArchiveIndex(archive, control) : ArchiveIndex();
	QHash<QString, QString> shaderOwners;
	if (family == TextureFamily::Quake3 && archive.isOpen() && !index.shaderScriptPaths.isEmpty()) {
		shaderOwners = collectShaderDeclarations(archive, index, &audit.warnings, control);
	}
	audit.shaderNameCount = static_cast<int>(shaderOwners.size());
	if (!audit.warnings.isEmpty()) { audit.complete = false; }

	// Sort before resolving so the reference list, the warnings and the JSON all
	// come out in the same order on every run.
	std::sort(collection.references.begin(), collection.references.end(),
		[](const CollectedReference& a, const CollectedReference& b) { return a.lookupKey < b.lookupKey; });

	const auto metadata = archive.entries();
	bool paletteLoaded = false;
	IdTechPalette palette;

	audit.references.reserve(static_cast<int>(collection.references.size()));
	for (const CollectedReference& collected : collection.references) {
		if (auditCancelled(control)) { break; }
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
		QString shaderKey;
		if (family == TextureFamily::Quake3) {
			for (const QString& material : mapTextureMaterialCandidates(collected.textureName, document.format, document.engineFamily)) {
				if (shaderOwners.contains(material.toLower())) {
					shaderKey = material.toLower();
					break;
				}
			}
		}
		for (const QString& candidate : reference.candidatePaths) {
			const QString key = candidate.toLower();
			if (index.pathByKey.contains(key)) {
				resolvedKey = key;
				break;
			}
		}

		// The declaring shader takes precedence over a same-named image, as it
		// does in the shared dependency resolver. Unreadable winning image rows
		// cannot be treated as resolved merely because their names remain.
		if (!shaderKey.isEmpty()) {
			reference.resolved = true;
			reference.resolution = MapTextureResolution::ResolvedByShader;
			reference.resolvedPath = shaderOwners.value(shaderKey);
			reference.sourceLayer = index.layerByKey.value(reference.resolvedPath.toLower());
			if (reference.noteId.isEmpty()) { reference.noteId = QStringLiteral("quake3-shader-script"); }
		} else if (!resolvedKey.isEmpty()) {
			const auto position = index.positionByKey.value(resolvedKey, -1);
			if (position < 0 || position >= metadata.size() || !metadata.at(position).readable) {
				audit.complete = false; reference.noteId = QStringLiteral("unreadable-package-entry");
				appendAuditWarning(&audit.warnings, QCoreApplication::translate("VibeStudioMapAssets", "Texture entry %1 is unavailable: %2")
					.arg(index.pathByKey.value(resolvedKey), position >= 0 && position < metadata.size() ? metadata.at(position).note : QString()));
			} else {
				reference.resolved = true; reference.resolution = MapTextureResolution::ResolvedEntry;
				reference.resolvedPath = index.pathByKey.value(resolvedKey);
				reference.sourceLayer = index.layerByKey.value(resolvedKey);
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
			if (!readAuditBytes(archive, index, reference.resolvedPath, &bytes, &error, kMaxDecodeBytes, control)) {
				++audit.undecodableCount; audit.complete = false;
				appendAuditWarning(&audit.warnings, QCoreApplication::translate("VibeStudioMapAssets", "Unable to read %1 for %2: %3")
									  .arg(reference.resolvedPath, reference.textureName,
										  error.isEmpty() ? QCoreApplication::translate("VibeStudioMapAssets", "unknown error") : error));
			} else {
				const IdTechImageDecodeResult decoded = decodeIdTechImage(reference.resolvedPath, bytes, palette);
				if (decoded.decoded && decoded.width > 0 && decoded.height > 0) {
					reference.decoded = true;
					reference.width = decoded.width;
					reference.height = decoded.height;
					reference.formatId = decoded.formatId.isEmpty() ? idTechImageFormatId(decoded.format) : decoded.formatId;
				} else {
					++audit.undecodableCount; audit.complete = false;
					appendAuditWarning(&audit.warnings, QCoreApplication::translate("VibeStudioMapAssets", "Resolved %1 for %2 but could not decode it: %3")
										  .arg(reference.resolvedPath, reference.textureName,
											  decoded.error.isEmpty() ? QCoreApplication::translate("VibeStudioMapAssets", "unsupported image data") : decoded.error));
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
	DirectoryReader(const QStringList& roots, QStringList* warnings, const PackageIndexLimits& limits) : m_session(limits)
	{
		if (roots.size() > PackageArchiveSession::layerCeiling) {
			if (warnings) { appendAuditWarning(warnings, QCoreApplication::translate("VibeStudioMapAssets", "Asset lookup supports at most %1 folder roots. Choose fewer roots before retrying.").arg(PackageArchiveSession::layerCeiling)); }
			return;
		}
		QStringList sources;
		for (const QString& root : roots) {
			const QString trimmed = root.trimmed();
			if (trimmed.isEmpty()) {
				continue;
			}
			const QFileInfo info(trimmed);
			if (!info.exists() || !info.isDir()) {
				m_complete = false;
				if (warnings) {
					appendAuditWarning(warnings, QCoreApplication::translate("VibeStudioMapAssets", "Asset folder not found: %1").arg(QDir::toNativeSeparators(trimmed)));
				}
				continue;
			}

			const int archiveIndex = m_session.depth();
			QString error;
			const bool opened = archiveIndex == 0 ? m_session.openPrimaryArchive(info.absoluteFilePath(), &error)
				: m_session.mountArchive(info.absoluteFilePath(), {}, &error);
			if (!opened) {
				if (warnings) {
					appendAuditWarning(warnings, QCoreApplication::translate("VibeStudioMapAssets", "Unable to read asset folder %1: %2")
									 .arg(QDir::toNativeSeparators(info.absoluteFilePath()),
										 error.isEmpty() ? QCoreApplication::translate("VibeStudioMapAssets", "unknown error") : error));
				}
				// A rejected source can override earlier roots. Do not publish a
				// partial catalog that quietly resolves to the wrong asset.
				m_session.clear();
				return;
			}
			sources.append(QDir::toNativeSeparators(info.absoluteFilePath()));
		}
		// Use the same winning records for metadata and payload reads, including
		// case-folded duplicate paths within a source on case-sensitive systems.
		for (const auto& entry : m_session.entries()) {
			if (entry.kind == PackageEntryKind::File && !entry.virtualPath.isEmpty()) { m_entries.append(entry); }
		}
		m_sourcePath = sources.join(QStringLiteral("; "));
		m_open = m_session.hasOpenArchive();
	}

	[[nodiscard]] PackageArchiveFormat format() const override { return PackageArchiveFormat::Folder; }
	[[nodiscard]] QString sourcePath() const override { return m_sourcePath; }
	[[nodiscard]] bool isOpen() const override { return m_open; }
	[[nodiscard]] bool sourceIndexComplete() const { return m_open && m_complete; }
	[[nodiscard]] QVector<PackageEntry> entries() const override { return m_entries; }

	bool readEntryBytes(const QString& virtualPath, QByteArray* out, QString* error, qint64 maxBytes = -1) const override
	{
		return m_session.readEntryBytes(virtualPath, out, error, maxBytes);
	}

private:
	PackageArchiveSession m_session;
	QVector<PackageEntry> m_entries;
	QString m_sourcePath;
	bool m_open = false;
	bool m_complete = true;
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
	if (cancelled) { return OperationState::Cancelled; }
	if (!sourceIndexComplete || !complete || !warnings.isEmpty()) { return OperationState::Warning; }
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
		return QCoreApplication::translate("VibeStudioMapAssets", "Worldspawn brush face");
	case MapTextureUseKind::BrushEntityFace:
		return QCoreApplication::translate("VibeStudioMapAssets", "Brush entity face");
	case MapTextureUseKind::PatchShader:
		return QCoreApplication::translate("VibeStudioMapAssets", "Patch shader");
	case MapTextureUseKind::DoomSidedefUpper:
		return QCoreApplication::translate("VibeStudioMapAssets", "Sidedef upper");
	case MapTextureUseKind::DoomSidedefLower:
		return QCoreApplication::translate("VibeStudioMapAssets", "Sidedef lower");
	case MapTextureUseKind::DoomSidedefMiddle:
		return QCoreApplication::translate("VibeStudioMapAssets", "Sidedef middle");
	case MapTextureUseKind::DoomSectorFloor:
		return QCoreApplication::translate("VibeStudioMapAssets", "Sector floor");
	case MapTextureUseKind::DoomSectorCeiling:
		return QCoreApplication::translate("VibeStudioMapAssets", "Sector ceiling");
	}
	return QCoreApplication::translate("VibeStudioMapAssets", "Unknown use");
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
		return QCoreApplication::translate("VibeStudioMapAssets", "Missing");
	case MapTextureResolution::ResolvedEntry:
		return QCoreApplication::translate("VibeStudioMapAssets", "Resolved");
	case MapTextureResolution::ResolvedByShader:
		return QCoreApplication::translate("VibeStudioMapAssets", "Resolved by shader script");
	case MapTextureResolution::EngineHandled:
		return QCoreApplication::translate("VibeStudioMapAssets", "Engine-handled");
	}
	return QCoreApplication::translate("VibeStudioMapAssets", "Missing");
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

QStringList mapTextureMaterialCandidates(const QString& textureName, LevelMapFormat format, const QString& engineFamily)
{
	QStringList names;
	const QString cleaned = cleanTextureName(textureName);
	if (isMapTexturePlaceholder(cleaned, format)) {
		return names;
	}
	// Verified against NetRadiant Custom q3map2 ParsePatch / ParseRawBrush,
	// revision 68ecbed6 (GPL-2.0-or-later); source links are in docs/CREDITS.md.
	// Preserve the raw map token and apply this only to package lookups.
	if (textureFamilyFor(format, engineFamily) == TextureFamily::Quake3
		&& !cleaned.startsWith(QStringLiteral("textures/"), Qt::CaseInsensitive)) {
		names << QStringLiteral("textures/") + cleaned;
	}
	addCandidate(&names, cleaned);
	return names;
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
		// Without a shader definition, use an image under the same material path.
		for (const QString& material : mapTextureMaterialCandidates(cleaned, format, engineFamily)) {
			const QString lower = material.toLower();
			addCandidate(&paths, lower);
			addCandidate(&paths, lower + QStringLiteral(".tga"));
			addCandidate(&paths, lower + QStringLiteral(".jpg"));
			addCandidate(&paths, lower + QStringLiteral(".jpeg"));
			addCandidate(&paths, lower + QStringLiteral(".png"));
		}
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

QStringList collectShaderScriptNames(const QByteArray& bytes, QStringList* warnings, const PackageReadControl& control)
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
		if ((position & 4095) == 0 && auditCancelled(control)) { return {}; }
		const QChar current = text.at(position);
		if (current.isSpace()) {
			++position;
			continue;
		}
		if (current == QLatin1Char('/') && position + 1 < length) {
			const QChar next = text.at(position + 1);
			if (next == QLatin1Char('/')) {
				while (position < length && text.at(position) != QLatin1Char('\n')) {
					if ((position & 4095) == 0 && auditCancelled(control)) { return {}; }
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
				appendAuditWarning(warnings, QCoreApplication::translate("VibeStudioMapAssets", "Stopped reading the shader script after %1 tokens.").arg(kMaxShaderTokens));
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
					appendAuditWarning(warnings, QCoreApplication::translate("VibeStudioMapAssets", "Ignored an unmatched closing brace in the shader script."));
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
				if ((end & 4095) == 0 && auditCancelled(control)) { return {}; }
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
			if ((position & 4095) == 0 && auditCancelled(control)) { return {}; }
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
		appendAuditWarning(warnings, QCoreApplication::translate("VibeStudioMapAssets", "The shader script ends inside a block comment."));
	}
	if (depth != 0 && warnings) {
		appendAuditWarning(warnings, QCoreApplication::translate("VibeStudioMapAssets", "The shader script ends with %1 unclosed brace(s).").arg(depth));
	}
	if (names.size() >= kMaxShaderNames && warnings) {
		appendAuditWarning(warnings, QCoreApplication::translate("VibeStudioMapAssets", "Stopped after %1 shader names in one script.").arg(kMaxShaderNames));
	}
	return names;
}

MapTextureAudit auditLevelMapTextures(const LevelMapDocument& document, const PackageArchiveReader& archive, bool decodeSizes, const PackageReadControl& control)
{
	auto audit = runAudit(document, archive, decodeSizes, QStringList(), control);
	audit.cancelled = auditCancelled(control);
	if (audit.cancelled) { audit.complete = false; }
	return audit;
}

MapTextureAudit auditLevelMapTexturesInDirectories(const LevelMapDocument& document, const QStringList& roots, bool decodeSizes, const PackageIndexLimits& limits)
{
	QStringList warnings;
	const DirectoryReader reader(roots, &warnings, limits);
	if (!reader.isOpen()) {
		warnings << QCoreApplication::translate("VibeStudioMapAssets", "No readable asset folder was supplied.");
	}
	auto audit = runAudit(document, reader, decodeSizes, warnings);
	audit.sourceIndexComplete = reader.sourceIndexComplete();
	audit.complete &= audit.sourceIndexComplete;
	return audit;
}

QStringList mapTextureAuditLines(const MapTextureAudit& audit)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioMapAssets", "Texture audit: %1")
				 .arg(audit.mapName.isEmpty() ? QCoreApplication::translate("VibeStudioMapAssets", "(unnamed map)") : audit.mapName);
	lines << QCoreApplication::translate("VibeStudioMapAssets", "State: %1").arg(operationStateDisplayName(audit.state()));
	if (!audit.complete) { lines << QCoreApplication::translate("VibeStudioMapAssets", "Texture audit incomplete; partial counts do not prove that every reference resolves."); }
	lines << QCoreApplication::translate("VibeStudioMapAssets", "Format: %1").arg(levelMapFormatDisplayName(audit.format));
	lines << QCoreApplication::translate("VibeStudioMapAssets", "Engine: %1")
				 .arg(audit.engineFamily.isEmpty() ? QCoreApplication::translate("VibeStudioMapAssets", "unknown") : audit.engineFamily);
	lines << QCoreApplication::translate("VibeStudioMapAssets", "Package: %1")
				 .arg(audit.packageSource.isEmpty() ? QCoreApplication::translate("VibeStudioMapAssets", "none") : audit.packageSource);
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
		lines << QCoreApplication::translate("VibeStudioMapAssets", "Missing textures");
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
		lines << QCoreApplication::translate("VibeStudioMapAssets", "Engine-handled names");
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
		lines << QCoreApplication::translate("VibeStudioMapAssets", "All references");
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
		lines << QCoreApplication::translate("VibeStudioMapAssets", "Warnings");
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
	object.insert(QStringLiteral("sourceIndexComplete"), audit.sourceIndexComplete);
	object.insert(QStringLiteral("complete"), audit.complete);
	object.insert(QStringLiteral("cancelled"), audit.cancelled);
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
