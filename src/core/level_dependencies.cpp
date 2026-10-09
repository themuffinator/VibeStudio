#include "core/level_dependencies.h"
#include "core/level_quake_assets.h"
#include "core/level_document.h"
#include "core/idtech_image.h"

#include "core/advanced_studio.h"
#include "core/doom_preview_geometry.h"
#include "core/game_asset_register.h"
#include "core/level_materials.h"
#include "core/map_assets.h"
#include "core/model_mesh.h"
#include "core/level_model_appearance.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonArray>
#include <QMap>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <utility>

namespace vibestudio {
namespace {

constexpr qint64 maxScriptBytes = 4 * 1024 * 1024;
constexpr quint64 maxAllScriptBytes = 64 * 1024 * 1024;
constexpr int maxScripts = 2048;
constexpr int maxDependencies = 65536;
constexpr int maxShaders = 65536;
constexpr int maxSites = 64;
constexpr qint64 maxModelBytes = 8 * 1024 * 1024;
constexpr quint64 maxAllModelBytes = 64 * 1024 * 1024;
constexpr int maxModels = 128;

QString cleanName(QString name)
{
	return name.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/'));
}

QString keyFor(const QString& name)
{
	return cleanName(name).toCaseFolded();
}

void appendUnique(QStringList* target, const QString& value)
{
	if (!value.isEmpty() && !target->contains(value)) {
		target->append(value);
	}
}

QStringList imageCandidates(const QString& reference)
{
	const QString name = cleanName(reference);
	QStringList paths {name};
	QString stem = name;
	const QString suffix = QFileInfo(name).suffix().toLower();
	if (suffix == QStringLiteral("tga") || suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg")
		|| suffix == QStringLiteral("png")) {
		stem.chop(suffix.size() + 1);
	} else if (!suffix.isEmpty()) {
		return paths;
	}
	for (const QString& ext : {QStringLiteral(".tga"), QStringLiteral(".jpg"), QStringLiteral(".png"), QStringLiteral(".jpeg")}) {
		appendUnique(&paths, stem + ext);
	}
	return paths;
}

// Original Quake III R_LoadImage / R_FindShader (tr_image.c, tr_shader.c),
// GPL-2.0-or-later interfaces reviewed 2026-10-06; see docs/CREDITS.md.
// Native stages require explicit image names; implicit model shaders default
// to TGA. Only a missing TGA falls back to JPG, never the reverse or PNG.
QStringList nativePlayerImageCandidates(const QString& reference, bool modelMaterial)
{
	QString name = cleanName(reference);
	const auto suffix = QFileInfo(name).suffix().toLower();
	if (suffix.isEmpty()) {
		if (!modelMaterial) { return {}; }
		name += QStringLiteral(".tga");
	}
	QStringList paths{name};
	if (name.endsWith(QStringLiteral(".tga"), Qt::CaseInsensitive)) { paths << name.left(name.size() - 4) + QStringLiteral(".jpg"); }
	return paths;
}

bool builtin(const QString& name, const QString& kind, LevelMapFormat format)
{
	const QString key = keyFor(name);
	if (kind == QStringLiteral("model") && QRegularExpression(QStringLiteral("^\\*[0-9]+$")).match(key).hasMatch()) {
		return true;
	}
	if (kind == QStringLiteral("sound")) {
		return key.startsWith(QLatin1Char('*')) || QRegularExpression(QStringLiteral("^[0-9]+$")).match(key).hasMatch();
	}
	if (kind == QStringLiteral("shader-image")) {
		return key == QStringLiteral("$lightmap") || key == QStringLiteral("$whiteimage");
	}
	if (kind == QStringLiteral("texture") && format == LevelMapFormat::QuakeMap) {
		return key == QStringLiteral("clip") || key == QStringLiteral("skip") || key == QStringLiteral("hint") || key == QStringLiteral("origin");
	}
	return kind == QStringLiteral("texture") && format == LevelMapFormat::Quake3Map && key == QStringLiteral("noshader");
}

struct ShaderOwner {
	QString path;
	ShaderDefinition shader;
};

QString stockLayerId(const GameAssetRegister& stock, int source)
{
	const GameAssetRegisterSource* found = stock.source(source);
	return found ? QStringLiteral("stock:") + found->id : QStringLiteral("stock");
}

// The first candidate the game's own packages hold. Leaves the row untouched
// when none matches.
bool resolveStockFile(LevelDependency* dependency, const QStringList& candidates, const GameAssetRegister* stock)
{
	if (!stock) {
		return false;
	}
	for (const QString& candidate : candidates) {
		if (const GameAssetRegisterFile* file = stock->file(candidate)) {
			dependency->status = LevelDependencyStatus::Stock;
			dependency->resolvedPath = file->path;
			dependency->sourceLayer = stockLayerId(*stock, file->source);
			dependency->stockSource = stock->sourceLabel(file->source);
			dependency->sizeBytes = file->sizeBytes;
			dependency->sourceOrdinal = -1;
			return true;
		}
	}
	return false;
}

LevelDependencyReport inspectDoomDependencies(const LevelMapDocument& document, const PackageArchiveReader& archive,
	LevelDependencyReport report, const LevelDependencyProgress& progress, const GameAssetRegister* stock)
{
	// Whole TEXTURE tables may reference additional, unused patches. A portable
	// subset needs table rewriting and namespace/occurrence closure, not merely
	// copying the files that happened to contribute pixels to this camera.
	report.exportSupported = false;
	report.limitations = {QCoreApplication::translate("VibeStudioLevelDependencies", "Reviews Doom wall textures, flats, patches, texture definitions and palette inputs in the selected package. Thing sprites, sounds, scripts and engine-selected resources require a separate review.")};
	report.limitations << QCoreApplication::translate("VibeStudioLevelDependencies", "Doom review identifies exact flat, patch, texture-table and palette inputs. Asset subset export is unavailable until whole texture tables and WAD namespace groups can be preserved or rewritten together.");
	if (document.doomFormat == LevelMapDoomFormat::Udmf) {
		report.complete = false;
		report.warnings << QCoreApplication::translate("VibeStudioLevelDependencies", "UDMF material dependencies are not decoded.");
		return report;
	}
	LevelPreviewAssetOptions options; options.materialLimit = maxDependencies;
	const auto assets = resolveLevelPreviewAssets(document, archive, options, progress);
	report.complete = assets.complete; report.cancelled = assets.cancelled; report.warnings = assets.warnings;
	QHash<QString, QStringList> uses;
	const auto use = [&](const QString& name, bool flat, const QString& selector) {
		auto& list = uses[doomPreviewMaterialKey(name, flat)]; if (list.size() < maxSites) { appendUnique(&list, selector); }
	};
	for (const auto& sector : document.doomSectors) {
		const auto selector = QStringLiteral("sector:%1").arg(sector.id);
		use(sector.floorTexture, true, selector); use(sector.ceilingTexture, true, selector);
	}
	QHash<int, const LevelMapDoomSidedef*> sides;
	for (const auto& side : document.doomSidedefs) { sides.insert(side.id, &side); }
	for (const auto& line : document.doomLinedefs) {
		for (int id : {line.frontSidedef, line.backSidedef}) {
			if (const auto* side = sides.value(id)) {
				for (const auto& name : {side->upperTexture, side->lowerTexture, side->middleTexture}) { use(name, false, QStringLiteral("linedef:%1").arg(line.id)); }
			}
		}
	}
	QHash<qsizetype, int> inputRows;
	int completed = 0;
	for (const auto& material : assets.materials) {
		if (progress && !progress(completed++, int(assets.materials.size()))) { report.cancelled = true; report.complete = false; break; }
		LevelDependency dependency;
		dependency.kind = material.key.startsWith(QStringLiteral("flats/")) ? QStringLiteral("doom-flat") : QStringLiteral("doom-wall");
		dependency.reference = material.name; dependency.resolvedPath = material.imagePath; dependency.sourceLayer = material.sourceLayer;
		dependency.selectors = uses.value(material.key); dependency.requiredBy = {QStringLiteral("map")}; dependency.note = material.note;
		dependency.status = material.ready() ? LevelDependencyStatus::Resolved : material.status == QStringLiteral("builtin") ? LevelDependencyStatus::Builtin
			: material.status == QStringLiteral("ambiguous") ? LevelDependencyStatus::Ambiguous : material.status == QStringLiteral("missing") ? LevelDependencyStatus::Missing : LevelDependencyStatus::Unreadable;
		// A PWAD without its own texture tables, or with tables naming the
		// IWAD's patches, leaves stock names unresolved here. The IWAD has them.
		if (stock && (dependency.status == LevelDependencyStatus::Missing || dependency.status == LevelDependencyStatus::Unreadable)) {
			const bool flat = dependency.kind == QStringLiteral("doom-flat");
			const int source = stock->doomNameSource(flat ? QStringLiteral("flat") : QStringLiteral("texture"), material.name);
			if (source >= 0) {
				dependency.status = LevelDependencyStatus::Stock;
				dependency.sourceLayer = stockLayerId(*stock, source);
				dependency.stockSource = stock->sourceLabel(source);
				dependency.resolvedPath = material.name.toUpper();
				dependency.note = QCoreApplication::translate("VibeStudioLevelDependencies", "Provided by %1.").arg(dependency.stockSource);
			}
		}
		if (dependency.status == LevelDependencyStatus::Stock) { ++report.stockCount; }
		else if (dependency.status == LevelDependencyStatus::Builtin) { ++report.builtinCount; }
		else if (dependency.status != LevelDependencyStatus::Resolved) { ++report.problemCount; if (dependency.status == LevelDependencyStatus::Missing) { ++report.missingCount; } }
		report.dependencies << dependency;
		for (const auto& warning : material.warnings) { appendUnique(&report.warnings, warning); }
		for (const auto& input : material.inputs) {
			const auto existing = inputRows.constFind(input.entryIndex);
			if (existing != inputRows.cend()) {
				auto& row = report.dependencies[*existing];
				if (row.requiredBy.size() < maxSites) { appendUnique(&row.requiredBy, material.key); }
				for (const auto& selector : dependency.selectors) { if (row.selectors.size() < maxSites) { appendUnique(&row.selectors, selector); } }
				continue;
			}
			if (report.dependencies.size() >= maxDependencies) { report.complete = false; break; }
			LevelDependency row;
			row.kind = QStringLiteral("doom-input"); row.reference = input.path; row.resolvedPath = input.path; row.sourceLayer = input.layer;
			row.status = LevelDependencyStatus::Resolved; row.sizeBytes = input.sizeBytes; row.sourceOrdinal = input.sourceOrdinal; row.namespaceId = input.namespaceId;
			row.selectors = dependency.selectors; row.requiredBy = {material.key};
			row.note = QCoreApplication::translate("VibeStudioLevelDependencies", "Doom input: %1 · namespace %2 · directory occurrence %3").arg(input.role, input.namespaceId).arg(input.sourceOrdinal);
			inputRows.insert(input.entryIndex, int(report.dependencies.size())); report.dependencies << std::move(row);
			report.resolvedPaths << input.path; report.totalBytes += input.sizeBytes;
		}
		if (report.dependencies.size() >= maxDependencies) {
			report.complete = false; report.warnings << QCoreApplication::translate("VibeStudioLevelDependencies", "Dependency limit exceeded; the report is incomplete."); break;
		}
	}
	return report;
}

} // namespace

bool LevelDependencyReport::canExport() const
{
	return exportSupported && complete && !cancelled && problemCount == 0 && !resolvedPaths.isEmpty();
}

static LevelDependencyReport inspectDependencies(const LevelMapDocument& document, const PackageArchiveReader& archive,
	LevelDependencyProgress progress, const QString& buildTarget, const ModelMesh* materialMesh, const GameAssetRegister* stock,
	bool quakeTexturesEmbedded = false)
{
	const auto target = buildTarget.isEmpty() && document.originalText.section('\n', 0, 0).trimmed() == QString::fromLatin1(kQuake2MapTargetHeader).trimmed() ? QStringLiteral("quake2") : buildTarget;
	LevelDependencyReport report;
	report.mapPath = document.sourcePath;
	report.mapName = document.mapName;
	report.packagePath = archive.sourcePath();
	report.limitations << QCoreApplication::translate("VibeStudioLevelDependencies", "Checks explicit map references, native model materials, MD3 misc_model compiler skins and remaps, and images in referenced Quake III shaders. Compiler overrides on other model formats, secondary shader references, game-code assets, and dynamically selected files require a separate review.");
	report.limitations << QCoreApplication::translate("VibeStudioLevelDependencies", "An asset subset contains resolved files and whole shader scripts. It does not include the map, compiled BSP, or game installation.");
	if (!archive.isOpen() || document.format == LevelMapFormat::Unknown) {
		report.complete = false;
		report.warnings << QCoreApplication::translate("VibeStudioLevelDependencies", "Open a supported map and package before checking dependencies.");
		return report;
	}
	if (document.format == LevelMapFormat::DoomWad) { return inspectDoomDependencies(document, archive, report, progress, stock); }
	if (stock) {
		report.limitations << QCoreApplication::translate("VibeStudioLevelDependencies", "References the game's own packages provide are listed as provided by the game and are not expanded further.");
	}
	int completed = 0;
	int total = 0;
	const auto tick = [&]() {
		if (progress && !progress(completed, total)) {
			report.cancelled = true;
			report.complete = false;
			return false;
		}
		return true;
	};
	if (!tick()) {
		return report;
	}
	QMap<QString, QVector<PackageEntry>> files;
	LevelQuakeTextures quakeTextures;
	const bool skipTextures = quakeTexturesEmbedded && document.format == LevelMapFormat::QuakeMap && target != QStringLiteral("quake2");
	if (skipTextures) {
		report.limitations << QCoreApplication::translate("VibeStudioLevelDependencies", "Quake textures are compiled into the BSP, so they are not checked.");
	}
	if (document.format == LevelMapFormat::QuakeMap && target != QStringLiteral("quake2") && !skipTextures) {
		PackageReadControl control;
		control.isCancelled = [&] { return !tick(); };
		quakeTextures = inspectLevelQuakeTextures(archive, control);
		if (!quakeTextures.error.isEmpty()) {
			report.complete = false;
			report.cancelled = quakeTextures.cancelled;
			report.warnings << quakeTextures.error;
			return report;
		}
	}
	QStringList scriptKeys;
	for (const PackageEntry& entry : archive.entries()) {
		if (entry.kind != PackageEntryKind::File) {
			continue;
		}
		files[keyFor(entry.virtualPath)].append(entry);
	}
	if (document.format == LevelMapFormat::Quake3Map) {
		for (auto it = files.cbegin(); it != files.cend(); ++it) {
			if (it.key().startsWith(QStringLiteral("scripts/")) && it.key().endsWith(QStringLiteral(".shader"))) {
				scriptKeys << it.key();
			}
		}
	}
	total = static_cast<int>(scriptKeys.size());
	QMap<QString, QVector<ShaderOwner>> shaders;
	quint64 scriptBytes = 0;
	int shaderCount = 0;
	for (const QString& scriptKey : std::as_const(scriptKeys)) {
		if (!tick()) {
			return report;
		}
		const QVector<PackageEntry>& matches = files[scriptKey];
		const PackageEntry& entry = matches.first();
		if (completed >= maxScripts || shaderCount >= maxShaders || entry.sizeBytes > static_cast<quint64>(maxScriptBytes)
			|| entry.sizeBytes > maxAllScriptBytes - scriptBytes) {
			report.complete = false;
			report.warnings << QCoreApplication::translate("VibeStudioLevelDependencies", "Shader scan budget exceeded at %1; the report is incomplete.").arg(entry.virtualPath);
			break;
		}
		++completed;
		scriptBytes += entry.sizeBytes;
		QByteArray bytes;
		QString error;
		if (matches.size() != 1 || !entry.readable || !isSafePackageVirtualPath(entry.virtualPath)
			|| !archive.readEntryBytes(entry.virtualPath, &bytes, &error, maxScriptBytes + 1)
			|| static_cast<quint64>(bytes.size()) != entry.sizeBytes) {
			report.complete = false;
			report.warnings << QCoreApplication::translate("VibeStudioLevelDependencies", "Cannot inspect shader script %1: %2").arg(entry.virtualPath,
				error.isEmpty() ? QCoreApplication::translate("VibeStudioLevelDependencies", "ambiguous, unsafe, unreadable, or changed entry") : error);
			continue;
		}
		if (!tick()) {
			return report;
		}
		const ShaderDocument parsed = parseShaderScriptText(QString::fromUtf8(bytes), entry.virtualPath);
		for (const AdvancedStudioIssue& issue : parsed.issues) {
			report.complete = false;
			report.warnings << QStringLiteral("%1:%2: %3").arg(entry.virtualPath).arg(issue.line).arg(issue.message);
		}
		for (const ShaderDefinition& shader : parsed.shaders) {
			if (++shaderCount > maxShaders) {
				report.complete = false;
				report.warnings << QCoreApplication::translate("VibeStudioLevelDependencies", "Too many shader declarations; the report is incomplete.");
				break;
			}
			shaders[keyFor(shader.name)].append({entry.virtualPath, shader});
		}
	}

	// As in PakFu's asset graph, edges keep their source and resolution evidence
	// instead of treating filename hints as proof. Original implementation;
	// reference: PakFu src/cli/cli.cpp, 13111e4c (GPL-3.0), reviewed 2026-10-03.
	QVector<LevelDependency> pending;
	QHash<QString, int> pendingIndex;
	bool warnedLimit = false;
	const auto enqueue = [&](const QString& kind, const QString& name, const QStringList& selectors, const QString& requiredBy,
	                         const QStringList& exactCandidates = QStringList{}) {
		const QString cleaned = cleanName(name);
		if (cleaned.isEmpty() || (kind == QStringLiteral("texture") && (skipTextures || isMapTexturePlaceholder(cleaned, document.format)))) {
			return;
		}
		QString key = kind + QLatin1Char(':') + keyFor(cleaned);
		if (!exactCandidates.isEmpty()) { key += QLatin1Char(':') + exactCandidates.join(QLatin1Char('|')); }
		// A bare skin name can be relative to its model's directory. Two models
		// using "skin.tga" need independent resolution and ownership evidence.
		if (kind == QStringLiteral("model-material") && !cleaned.contains(QLatin1Char('/'))) {
			key += QLatin1Char(':') + keyFor(requiredBy);
		}
		int index = pendingIndex.value(key, -1);
		if (index < 0) {
			if (pending.size() >= maxDependencies) {
				report.complete = false;
				if (!warnedLimit) {
					report.warnings << QCoreApplication::translate("VibeStudioLevelDependencies", "Dependency limit exceeded; the report is incomplete.");
					warnedLimit = true;
				}
				return;
			}
			LevelDependency dependency;
			dependency.kind = kind;
			dependency.reference = cleaned;
			dependency.candidates = exactCandidates;
			index = static_cast<int>(pending.size());
			pending.append(dependency);
			pendingIndex.insert(key, index);
		}
		LevelDependency& dependency = pending[index];
		for (const QString& selector : selectors) {
			if (dependency.selectors.size() < maxSites) {
				appendUnique(&dependency.selectors, selector);
			}
		}
		if (dependency.requiredBy.size() < maxSites) {
			appendUnique(&dependency.requiredBy, requiredBy);
		}
	};
	// Walk objects once; a per-texture search would be quadratic on large maps.
	for (const LevelMapBrush& brush : document.brushes) {
		for (const LevelMapBrushFace& face : brush.faces) {
			enqueue(QStringLiteral("texture"), face.textureName, {QStringLiteral("brush:%1").arg(brush.id)}, QStringLiteral("map"));
		}
		if (!tick()) { return report; }
	}
	for (const LevelMapPatch& patch : document.patches) {
		if (!tick()) { return report; }
		enqueue(QStringLiteral("texture"), patch.textureName, {QStringLiteral("patch:%1").arg(patch.id)}, QStringLiteral("map"));
	}
	for (const LevelMapDoomSector& sector : document.doomSectors) {
		if (!tick()) { return report; }
		const QString selector = QStringLiteral("sector:%1").arg(sector.id);
		enqueue(QStringLiteral("texture"), sector.floorTexture, {selector}, QStringLiteral("map"));
		enqueue(QStringLiteral("texture"), sector.ceilingTexture, {selector}, QStringLiteral("map"));
	}
	for (const LevelMapDoomLinedef& line : document.doomLinedefs) {
		if (!tick()) { return report; }
		for (int side : {line.frontSidedef, line.backSidedef}) {
			if (side < 0 || side >= document.doomSidedefs.size()) { continue; }
			const auto& sidedef = document.doomSidedefs.at(side);
			for (const QString& name : {sidedef.upperTexture, sidedef.middleTexture, sidedef.lowerTexture}) {
				enqueue(QStringLiteral("texture"), name, {QStringLiteral("linedef:%1").arg(line.id)}, QStringLiteral("map"));
			}
		}
	}
	struct AppearanceUse { LevelModelAppearance appearance; QStringList selectors; };
	QHash<QString, QMap<QString, AppearanceUse>> modelAppearances;
	QHash<QString, QStringList> nativeModelSelectors;
	for (const LevelMapEntity& entity : document.entities) {
		if (!tick()) { return report; }
		const auto appearance = levelModelAppearance(document, entity);
		for (const LevelMapProperty& property : entity.properties) {
			const QString key = property.key.toLower();
			QString kind;
			if (key == QStringLiteral("model") || key == QStringLiteral("model2")) {
				kind = QStringLiteral("model");
			} else if (key == QStringLiteral("noise") || key == QStringLiteral("noise1") || key == QStringLiteral("noise2")
				|| key == QStringLiteral("noise3") || key == QStringLiteral("noise4") || key == QStringLiteral("sound") || key == QStringLiteral("music")) {
				kind = QStringLiteral("sound");
			} else {
				continue;
			}
			const QStringList values = key == QStringLiteral("music")
				? property.value.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts) : QStringList {property.value};
			for (const QString& value : values) {
				if (kind == QStringLiteral("model")) {
					const auto selector = QStringLiteral("entity:%1").arg(entity.id);
					if (key == QStringLiteral("model") && appearance.compiler && keyFor(value) == keyFor(appearance.modelPath)) {
						auto& use = modelAppearances[keyFor(value)][appearance.cacheKey]; use.appearance = appearance;
						if (use.selectors.size() < maxSites) { appendUnique(&use.selectors, selector); }
					} else if (nativeModelSelectors[keyFor(value)].size() < maxSites) { appendUnique(&nativeModelSelectors[keyFor(value)], selector); }
				}
				QStringList exactCandidates;
				if (entity.className == QStringLiteral("target_speaker") && key == QStringLiteral("noise") &&
				    (document.format == LevelMapFormat::QuakeMap || document.format == LevelMapFormat::Quake3Map)) {
					// Original id Software target_speaker path rules; see audio_level.cpp
					// and docs/CREDITS.md. A root file must not shadow a Quake II sound/ file.
					QString sound = cleanName(value);
					if (!sound.contains(QStringLiteral(".wav"))) { sound += QStringLiteral(".wav"); }
					exactCandidates << (document.format == LevelMapFormat::QuakeMap ? QStringLiteral("sound/") + sound : sound);
				}
				enqueue(kind, value, {QStringLiteral("entity:%1").arg(entity.id)}, property.key, exactCandidates);
			}
		}
	}
	if (materialMesh) {
		for (int surface = 0; surface < materialMesh->surfaces.size(); ++surface) {
			if (!tick()) { return report; }
			for (const auto& material : materialMesh->surfaces[surface].skinPaths) {
				enqueue(QStringLiteral("model-material"), material, {QStringLiteral("surface:%1").arg(surface)}, materialMesh->sourcePath);
			}
		}
	}
	QMap<QString, PackageEntry> resolvedFiles;
	const auto resolveFile = [&](LevelDependency* dependency, const QStringList& candidates) {
		dependency->candidates = candidates;
		for (const QString& candidate : candidates) {
			const auto it = files.constFind(keyFor(candidate));
			if (it == files.cend()) { continue; }
			if (it->size() != 1) {
				dependency->status = LevelDependencyStatus::Ambiguous;
				return;
			}
			const PackageEntry& entry = it->first();
			if (!entry.readable) {
				dependency->status = LevelDependencyStatus::Unreadable;
				return;
			}
			if (!isSafePackageVirtualPath(entry.virtualPath)) {
				dependency->status = LevelDependencyStatus::Unsafe;
				return;
			}
			dependency->status = LevelDependencyStatus::Resolved;
			dependency->resolvedPath = entry.virtualPath;
			dependency->sourceLayer = entry.layerId.isEmpty() ? entry.sourceArchiveId : entry.layerId;
			dependency->sizeBytes = entry.sizeBytes;
			dependency->sourceOrdinal = entry.sourceOrdinal;
			if (stock) {
				if (const GameAssetRegisterFile* shadowed = stock->file(entry.virtualPath)) {
					dependency->stockShadowed = true;
					dependency->stockSource = stock->sourceLabel(shadowed->source);
				}
			}
			resolvedFiles.insert(keyFor(entry.virtualPath), entry);
			return;
		}
	};
	// The game's declaration of a shader the project does not declare itself.
	const auto stockShader = [&](const QString& reference, bool texture) -> const GameAssetRegisterFile* {
		if (!stock) {
			return nullptr;
		}
		const QStringList names = texture ? mapTextureMaterialCandidates(reference, document.format, document.engineFamily) : QStringList {reference};
		for (QString name : names) {
			if (!texture) {
				const auto dot = name.indexOf(QLatin1Char('.'));
				if (dot >= 0) { name.truncate(dot); }
			}
			if (const GameAssetRegisterFile* script = stock->shaderScript(name)) {
				return script;
			}
		}
		return nullptr;
	};
	const int scriptWork = completed;
	quint64 modelBytes = 0;
	quint64 walBytes = 0;
	int modelsRead = 0;
	int appearancesRead = 0;
	ModelWorkControl modelControl; modelControl.cancelled = [&] { return !tick(); };
	for (int index = 0; index < pending.size(); ++index) {
		total = scriptWork + static_cast<int>(pending.size());
		completed = scriptWork + index;
		if (!tick()) { return report; }
		// Copy: shader expansion can reallocate pending.
		LevelDependency dependency = pending.at(index);
		QString nameKey = keyFor(dependency.reference);
		if (materialMesh && dependency.kind == QStringLiteral("model-material")) {
			// The original COM_StripExtension stops at the first dot.
			const auto dot = nameKey.indexOf(QLatin1Char('.'));
			if (dot >= 0) { nameKey.truncate(dot); }
		}
		if (dependency.kind == QStringLiteral("texture")) {
			for (const QString& material : mapTextureMaterialCandidates(dependency.reference, document.format, document.engineFamily)) {
				if (shaders.contains(keyFor(material))) {
					nameKey = keyFor(material);
					break;
				}
			}
		}
		// A speaker's numeric noise value is a filename (for example 123.wav),
		// not a numeric sound ID. Player-relative '*' names remain dynamic.
		const bool explicitSoundFile = dependency.kind == QStringLiteral("sound") &&
			!dependency.candidates.isEmpty() && !nameKey.startsWith(QLatin1Char('*'));
		if (!explicitSoundFile && builtin(dependency.reference, dependency.kind, document.format)) {
			dependency.status = LevelDependencyStatus::Builtin;
		} else if (!isSafePackageVirtualPath(dependency.reference)) {
			dependency.status = LevelDependencyStatus::Unsafe;
		} else if ((dependency.kind == QStringLiteral("texture") || dependency.kind == QStringLiteral("model-material")) && shaders.contains(nameKey)) {
			const QVector<ShaderOwner>& owners = shaders[nameKey];
			if (owners.size() != 1) {
				dependency.status = LevelDependencyStatus::Ambiguous;
				for (const ShaderOwner& owner : owners) { appendUnique(&dependency.candidates, owner.path); }
				dependency.note = QCoreApplication::translate("VibeStudioLevelDependencies", "Multiple scripts declare this shader; resolve the duplicate before exporting.");
			} else {
				const ShaderOwner& owner = owners.first();
				resolveFile(&dependency, {owner.path});
				for (const QString& reference : owner.shader.textureReferences) {
					enqueue(QStringLiteral("shader-image"), reference, dependency.selectors, owner.path + QLatin1Char(':') + owner.shader.name);
				}
			}
		} else if (const GameAssetRegisterFile* script = (dependency.kind == QStringLiteral("texture") || dependency.kind == QStringLiteral("model-material"))
				? stockShader(dependency.reference, dependency.kind == QStringLiteral("texture")) : nullptr) {
			// A shader the game declares wins over loose images of the same name,
			// as in R_FindShader; its images are the game's too.
			dependency.status = LevelDependencyStatus::Stock;
			dependency.resolvedPath = script->path;
			dependency.sourceLayer = stockLayerId(*stock, script->source);
			dependency.stockSource = stock->sourceLabel(script->source);
			dependency.sizeBytes = script->sizeBytes;
			dependency.note = QCoreApplication::translate("VibeStudioLevelDependencies", "Declared by %1 in %2.").arg(script->path, dependency.stockSource);
		} else {
			QStringList candidates {dependency.reference};
			if (dependency.kind == QStringLiteral("texture")) {
				candidates = mapTextureCandidatePaths(dependency.reference, document.format, document.engineFamily);
				if (target == QStringLiteral("quake2")) {
					const auto name = dependency.reference;
					candidates = {QStringLiteral("textures/") + name + QStringLiteral(".wal")};
				}
			} else if (dependency.kind == QStringLiteral("shader-image") || dependency.kind == QStringLiteral("model-material")) {
				candidates = materialMesh ? nativePlayerImageCandidates(dependency.reference, dependency.kind == QStringLiteral("model-material"))
					: imageCandidates(dependency.reference);
				if (!materialMesh && dependency.kind == QStringLiteral("model-material") && !dependency.reference.contains(QLatin1Char('/'))) {
					const QStringList relative = candidates;
					for (const QString& model : dependency.requiredBy) {
						for (const QString& candidate : relative) { appendUnique(&candidates, packageVirtualPathParent(model) + QLatin1Char('/') + candidate); }
					}
				}
			} else if (dependency.kind == QStringLiteral("sound") && !dependency.candidates.isEmpty()) {
				candidates = dependency.candidates;
			} else if (dependency.kind == QStringLiteral("sound") && !nameKey.startsWith(QStringLiteral("sound/")) && !nameKey.startsWith(QStringLiteral("music/"))) {
				candidates << QStringLiteral("sound/") + dependency.reference;
			}
			resolveFile(&dependency, candidates);
			if (dependency.kind == QStringLiteral("texture") && dependency.status == LevelDependencyStatus::Missing) {
				const auto found = quakeTextures.textures.constFind(nameKey);
				if (found != quakeTextures.textures.cend()) {
					if (found->ambiguous) { dependency.status = LevelDependencyStatus::Ambiguous; }
					else { resolveFile(&dependency, {found->sourcePath}); }
				}
			}
			if (dependency.status == LevelDependencyStatus::Missing) {
				resolveStockFile(&dependency, candidates, stock);
			}
		}
		if (target == QStringLiteral("quake2") && dependency.kind == QStringLiteral("texture") && dependency.status == LevelDependencyStatus::Resolved) {
			QByteArray bytes; QString error;
			if (dependency.sizeBytes > 64ULL * 1024 * 1024 || dependency.sizeBytes > 256ULL * 1024 * 1024 - walBytes || !readIdTechImageEntry(archive, dependency.resolvedPath, &bytes, &error)) {
				dependency.status = LevelDependencyStatus::Unreadable;
				dependency.note = error.isEmpty() ? QCoreApplication::translate("VibeStudioLevelDependencies", "WAL inspection exceeds the texture byte limit.") : error;
			} else {
				walBytes += dependency.sizeBytes;
				IdTechImageDecodeContext context; context.isCancelled = [&] { return !tick(); };
				const auto decoded = decodeIdTechImage(dependency.resolvedPath, bytes, {}, context);
				if (!decoded.decoded || decoded.format != IdTechImageFormat::Quake2Wal) {
					dependency.status = LevelDependencyStatus::Unreadable; dependency.note = decoded.error;
				} else if (!decoded.animationNextName.isEmpty()) {
					enqueue(QStringLiteral("texture"), decoded.animationNextName, dependency.selectors, dependency.resolvedPath);
				}
			}
		}
		if (dependency.kind == QStringLiteral("model") && dependency.status == LevelDependencyStatus::Resolved) {
			QByteArray bytes; QString error;
			if (++modelsRead > maxModels || dependency.sizeBytes > static_cast<quint64>(maxModelBytes) || dependency.sizeBytes > maxAllModelBytes - modelBytes) {
				report.complete = false;
				dependency.note = QCoreApplication::translate("VibeStudioLevelDependencies", "Model dependency scan budget exceeded; materials are incomplete.");
			} else {
				modelBytes += dependency.sizeBytes;
				if (!archive.readEntryBytes(dependency.resolvedPath, &bytes, &error, maxModelBytes + 1) || bytes.size() != static_cast<qint64>(dependency.sizeBytes)) {
					dependency.status = LevelDependencyStatus::Unreadable; report.complete = false;
					dependency.note = error.isEmpty() ? QCoreApplication::translate("VibeStudioLevelDependencies", "The model changed or could not be read completely.") : error;
				} else {
					if (!tick()) { return report; }
					const ModelMesh model = decodeModelMesh(dependency.resolvedPath, bytes, nullptr, modelControl);
					QStringList warnings = model.warnings;
					for (const auto& surface : model.surfaces) { warnings += surface.warnings; }
					if (!model.geometryAvailable || !model.error.isEmpty() || !warnings.isEmpty()) {
						report.complete = false;
						dependency.note = QCoreApplication::translate("VibeStudioLevelDependencies", "Model material coverage is incomplete: %1").arg(!model.error.isEmpty() ? model.error : warnings.join(QLatin1Char(';')));
					}
					const auto variants = modelAppearances.value(nameKey);
					const auto nativeSelectors = nativeModelSelectors.value(nameKey);
					if (variants.isEmpty() || !nativeSelectors.isEmpty()) {
						for (const auto& material : model.skinPaths) { enqueue(QStringLiteral("model-material"), material, variants.isEmpty() ? dependency.selectors : nativeSelectors, dependency.resolvedPath); }
					}
					for (const auto& use : variants) {
						if (++appearancesRead > maxModels) {
							report.complete = false;
							dependency.note = QCoreApplication::translate("VibeStudioLevelDependencies", "Model appearance scan budget exceeded; materials are incomplete.");
							break;
						}
						if (!use.appearance.skinPath.isEmpty()) { enqueue(QStringLiteral("model-skin"), use.appearance.skinPath, use.selectors, dependency.resolvedPath); }
						if (model.format == ModelMeshFormat::Quake3Md3 && files.contains(keyFor(use.appearance.defaultSkinPath))) {
							enqueue(QStringLiteral("model-skin"), use.appearance.defaultSkinPath, use.selectors, dependency.resolvedPath);
						}
						auto prepared = prepareLevelModelAppearance(model, use.appearance, archive, modelControl);
						prepared.receipt.insert(QStringLiteral("selectors"), QJsonArray::fromStringList(use.selectors));
						prepared.receipt.insert(QStringLiteral("modelSha256"), QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
						report.modelAppearances.append(prepared.receipt);
						if (!prepared.succeeded()) {
							report.complete = false; dependency.status = LevelDependencyStatus::Unreadable;
							dependency.note = prepared.error;
							if (prepared.cancelled) { report.cancelled = true; return report; }
							continue;
						}
						for (const auto& material : prepared.mesh.skinPaths) { enqueue(QStringLiteral("model-material"), material, use.selectors, dependency.resolvedPath); }
					}
				}
			}
			if (!dependency.note.isEmpty()) { report.warnings << dependency.resolvedPath + QStringLiteral(": ") + dependency.note; }
		}
		if (dependency.status == LevelDependencyStatus::Stock) { ++report.stockCount; }
		else if (dependency.status == LevelDependencyStatus::Builtin) { ++report.builtinCount; }
		else if (dependency.status != LevelDependencyStatus::Resolved) {
			++report.problemCount;
			if (dependency.status == LevelDependencyStatus::Missing) { ++report.missingCount; }
		}
		report.dependencies.append(dependency);
	}
	// A later model may add another map use to a material already resolved.
	for (int i = 0; i < report.dependencies.size(); ++i) {
		report.dependencies[i].selectors = pending[i].selectors;
		report.dependencies[i].requiredBy = pending[i].requiredBy;
	}
	for (const PackageEntry& entry : std::as_const(resolvedFiles)) {
		report.resolvedPaths << entry.virtualPath;
		report.totalBytes += entry.sizeBytes;
	}
	std::stable_sort(report.dependencies.begin(), report.dependencies.end(), [](const LevelDependency& left, const LevelDependency& right) {
		return std::make_pair(left.kind, keyFor(left.reference)) < std::make_pair(right.kind, keyFor(right.reference));
	});
	completed = total;
	tick();
	return report;
}

LevelDependencyReport inspectLevelDependencies(const LevelMapDocument& document, const PackageArchiveReader& archive,
	LevelDependencyProgress progress, const QString& buildTarget)
{
	return inspectDependencies(document, archive, std::move(progress), buildTarget, nullptr, nullptr);
}

LevelDependencyReport inspectLevelDependencies(const LevelMapDocument& document, const PackageArchiveReader& archive,
	LevelDependencyProgress progress, const LevelDependencyOptions& options)
{
	return inspectDependencies(document, archive, std::move(progress), options.buildTarget, nullptr, options.stock.get(), options.quakeTexturesEmbedded);
}

LevelDependencyReport inspectModelMaterialDependencies(const ModelMesh& mesh, const PackageArchiveReader& archive,
	LevelDependencyProgress progress, std::shared_ptr<const GameAssetRegister> stock)
{
	LevelMapDocument context;
	context.format = LevelMapFormat::Quake3Map;
	context.sourcePath = mesh.sourcePath;
	context.mapName = mesh.sourcePath;
	auto report = inspectDependencies(context, archive, std::move(progress), QStringLiteral("quake3"), &mesh, stock.get());
	report.limitations = {QCoreApplication::translate("VibeStudioLevelDependencies",
		"Reviews all retained model material slots and explicit images in their Quake III shaders. Whole declaring scripts are retained; game-code assets and source-port shader extensions need a separate review.")};
	return report;
}

QString levelDependencyStatusId(LevelDependencyStatus status)
{
	switch (status) {
	case LevelDependencyStatus::Resolved: return QStringLiteral("resolved");
	case LevelDependencyStatus::Missing: return QStringLiteral("missing");
	case LevelDependencyStatus::Builtin: return QStringLiteral("builtin");
	case LevelDependencyStatus::Ambiguous: return QStringLiteral("ambiguous");
	case LevelDependencyStatus::Unreadable: return QStringLiteral("unreadable");
	case LevelDependencyStatus::Unsafe: return QStringLiteral("unsafe");
	case LevelDependencyStatus::Stock: return QStringLiteral("stock");
	}
	return QStringLiteral("missing");
}

QString levelDependencyStatusName(LevelDependencyStatus status)
{
	switch (status) {
	case LevelDependencyStatus::Resolved: return QCoreApplication::translate("VibeStudioLevelDependencies", "Resolved");
	case LevelDependencyStatus::Missing: return QCoreApplication::translate("VibeStudioLevelDependencies", "Missing");
	case LevelDependencyStatus::Builtin: return QCoreApplication::translate("VibeStudioLevelDependencies", "Engine provided");
	case LevelDependencyStatus::Ambiguous: return QCoreApplication::translate("VibeStudioLevelDependencies", "Ambiguous");
	case LevelDependencyStatus::Unreadable: return QCoreApplication::translate("VibeStudioLevelDependencies", "Unreadable");
	case LevelDependencyStatus::Unsafe: return QCoreApplication::translate("VibeStudioLevelDependencies", "Unsafe path");
	case LevelDependencyStatus::Stock: return QCoreApplication::translate("VibeStudioLevelDependencies", "Provided by the game");
	}
	return {};
}

QJsonObject levelDependencyReportJson(const LevelDependencyReport& report)
{
	QJsonArray dependencies;
	for (const LevelDependency& dependency : report.dependencies) {
		dependencies.append(QJsonObject {{QStringLiteral("kind"), dependency.kind}, {QStringLiteral("reference"), dependency.reference},
			{QStringLiteral("status"), levelDependencyStatusId(dependency.status)}, {QStringLiteral("path"), dependency.resolvedPath},
			{QStringLiteral("layer"), dependency.sourceLayer}, {QStringLiteral("bytes"), static_cast<qint64>(dependency.sizeBytes)},
			{QStringLiteral("sourceOrdinal"), dependency.sourceOrdinal}, {QStringLiteral("namespace"), dependency.namespaceId},
			{QStringLiteral("candidates"), QJsonArray::fromStringList(dependency.candidates)},
			{QStringLiteral("selectors"), QJsonArray::fromStringList(dependency.selectors)},
			{QStringLiteral("requiredBy"), QJsonArray::fromStringList(dependency.requiredBy)}, {QStringLiteral("note"), dependency.note},
			{QStringLiteral("stockShadowed"), dependency.stockShadowed}, {QStringLiteral("stockSource"), dependency.stockSource}});
	}
	return {{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("map"), report.mapPath}, {QStringLiteral("mapName"), report.mapName},
		{QStringLiteral("package"), report.packagePath}, {QStringLiteral("complete"), report.complete}, {QStringLiteral("cancelled"), report.cancelled},
		{QStringLiteral("canExport"), report.canExport()}, {QStringLiteral("exportSupported"), report.exportSupported}, {QStringLiteral("missing"), report.missingCount}, {QStringLiteral("problems"), report.problemCount},
		{QStringLiteral("builtin"), report.builtinCount}, {QStringLiteral("stock"), report.stockCount}, {QStringLiteral("bytes"), static_cast<qint64>(report.totalBytes)},
		{QStringLiteral("files"), QJsonArray::fromStringList(report.resolvedPaths)}, {QStringLiteral("dependencies"), dependencies},
		{QStringLiteral("modelAppearances"), report.modelAppearances},
		{QStringLiteral("warnings"), QJsonArray::fromStringList(report.warnings)}, {QStringLiteral("limitations"), QJsonArray::fromStringList(report.limitations)}};
}

QString levelDependencyReportText(const LevelDependencyReport& report)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioLevelDependencies", "Dependencies for %1: %2 file(s), %3 bytes, %4 problem(s)")
		.arg(report.mapName).arg(report.resolvedPaths.size()).arg(report.totalBytes).arg(report.problemCount);
	if (report.stockCount > 0) {
		lines << QCoreApplication::translate("VibeStudioLevelDependencies", "Provided by the game: %n reference(s)", nullptr, report.stockCount);
	}
	if (!report.complete) {
		lines << (report.cancelled ? QCoreApplication::translate("VibeStudioLevelDependencies", "Cancelled; results are incomplete.")
			: QCoreApplication::translate("VibeStudioLevelDependencies", "Incomplete; inspect the warnings before exporting."));
	}
	for (const LevelDependency& dependency : report.dependencies) {
		lines << QStringLiteral("[%1] %2: %3%4").arg(levelDependencyStatusName(dependency.status), dependency.kind, dependency.reference,
			dependency.resolvedPath.isEmpty() ? QString() : QStringLiteral(" -> ") + dependency.resolvedPath);
		if (!dependency.selectors.isEmpty()) { lines << QStringLiteral("  ") + dependency.selectors.join(QStringLiteral(", ")); }
		if (dependency.kind == QStringLiteral("doom-input")) { lines << QStringLiteral("  ") + dependency.note; }
		if (dependency.status != LevelDependencyStatus::Resolved && !dependency.candidates.isEmpty()) {
			lines << QCoreApplication::translate("VibeStudioLevelDependencies", "  Searched: %1").arg(dependency.candidates.join(QStringLiteral(", ")));
		}
	}
	lines += report.warnings;
	lines += report.limitations;
	return lines.join(QLatin1Char('\n'));
}

} // namespace vibestudio
