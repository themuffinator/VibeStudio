#include "core/level_map.h"
#include "core/map_assets.h"
#include "core/operation_state.h"
#include "core/package_archive.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVector>

#include <cstdlib>
#include <iostream>

using namespace vibestudio;

namespace {

bool expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << message << "\n";
		return false;
	}
	return true;
}

bool writePackageFile(const QString& root, const QString& relativePath, const QByteArray& bytes)
{
	const QString absolutePath = QDir(root).absoluteFilePath(relativePath);
	const QString parent = QFileInfo(absolutePath).absolutePath();
	if (!QDir().mkpath(parent)) {
		return false;
	}
	QFile file(absolutePath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	return file.write(bytes) == bytes.size();
}

// A 64x64 Doom flat is exactly 4096 raw palette indices.
// https://doomwiki.org/wiki/Flat
QByteArray flatBytes()
{
	QByteArray bytes(4096, '\0');
	for (int index = 0; index < bytes.size(); ++index) {
		bytes[index] = static_cast<char>(index % 256);
	}
	return bytes;
}

QByteArray shaderScript()
{
	return QByteArray(
		"// base wall shaders\n"
		"textures/base_wall/shaderonly\n"
		"{\n"
		"\tqer_editorimage textures/base_wall/tgaonly.tga\n"
		"\t{\n"
		"\t\tmap $lightmap\n"
		"\t}\n"
		"}\n"
		"\n"
		"/* a block comment containing a stray { brace\n"
		"   spread over two lines */\n"
		"textures/base_wall/tgaonly\n"
		"{\n"
		"\t{\n"
		"\t\tmap textures/base_wall/tgaonly.tga\n"
		"\t}\n"
		"}\n");
}

QByteArray malformedShaderScript()
{
	return QByteArray(
		"}\n"
		"textures/base_wall/brokenshader\n"
		"{\n"
		"\t{\n"
		"\t\tmap textures/base_wall/nothing.tga\n");
}

bool buildPackage(const QString& root)
{
	bool ok = true;
	ok &= expect(writePackageFile(root, QStringLiteral("flats/FLOOR4_8"), flatBytes()), "Failed to write the Doom flat fixture.");
	ok &= expect(writePackageFile(root, QStringLiteral("patches/STARTAN3"), QByteArray("not a picture", 13)), "Failed to write the Doom patch fixture.");
	ok &= expect(writePackageFile(root, QStringLiteral("textures/quakewall"), QByteArray("miptex placeholder", 18)), "Failed to write the Quake texture fixture.");
	ok &= expect(writePackageFile(root, QStringLiteral("textures/base_wall/tgaonly.tga"), QByteArray(96, 'T')), "Failed to write the Quake III TGA fixture.");
	ok &= expect(writePackageFile(root, QStringLiteral("textures/base_wall/jpgonly.jpg"), QByteArray(96, 'J')), "Failed to write the Quake III JPEG fixture.");
	ok &= expect(writePackageFile(root, QStringLiteral("scripts/base.shader"), shaderScript()), "Failed to write the shader script fixture.");
	ok &= expect(writePackageFile(root, QStringLiteral("scripts/broken.shader"), malformedShaderScript()), "Failed to write the malformed shader fixture.");
	return ok;
}

const MapTextureReference* findReference(const MapTextureAudit& audit, const char* name)
{
	const QString wanted = QString::fromLatin1(name);
	for (const MapTextureReference& reference : audit.references) {
		if (reference.textureName.compare(wanted, Qt::CaseInsensitive) == 0) {
			return &reference;
		}
	}
	return nullptr;
}

bool hasUse(const MapTextureReference& reference, MapTextureUseKind kind, int objectId)
{
	for (const MapTextureUse& use : reference.uses) {
		if (use.kind == kind && use.objectId == objectId) {
			return true;
		}
	}
	return false;
}

LevelMapEntity entity(int id, const char* className)
{
	LevelMapEntity value;
	value.id = id;
	value.className = QString::fromLatin1(className);
	return value;
}

LevelMapBrushFace face(const char* textureName, int line)
{
	LevelMapBrushFace value;
	value.textureName = QString::fromLatin1(textureName);
	value.line = line;
	return value;
}

LevelMapDocument doomDocument()
{
	LevelMapDocument document;
	document.mapName = QStringLiteral("MAP01");
	document.engineFamily = QStringLiteral("idTech1");
	document.format = LevelMapFormat::DoomWad;

	LevelMapDoomSidedef front;
	front.id = 0;
	front.sector = 0;
	front.upperTexture = QStringLiteral("-");
	front.lowerTexture = QStringLiteral("startan3");
	front.middleTexture = QStringLiteral("MISSTEX1");

	LevelMapDoomSidedef back;
	back.id = 1;
	back.sector = 0;
	back.upperTexture = QStringLiteral("-");
	back.lowerTexture = QStringLiteral("-");
	back.middleTexture = QStringLiteral("-");

	LevelMapDoomSector sector;
	sector.id = 0;
	// The map writes the name in lower case; the package stores the lump in
	// upper case. Both must resolve to the same reference.
	sector.floorTexture = QStringLiteral("floor4_8");
	sector.ceilingTexture = QStringLiteral("FLOOR4_8");

	document.doomSidedefs = {front, back};
	document.doomSectors = {sector};
	return document;
}

LevelMapDocument quakeDocument()
{
	LevelMapDocument document;
	document.mapName = QStringLiteral("start");
	document.engineFamily = QStringLiteral("idTech2");
	document.format = LevelMapFormat::QuakeMap;
	document.entities = {entity(0, "worldspawn"), entity(1, "func_door")};

	LevelMapBrush world;
	world.id = 0;
	world.entityId = 0;
	world.faces = {face("QuakeWall", 12), face("clip", 13), face("*water1", 14), face("sky1", 15), face("missingwall", 16)};
	world.faceCount = static_cast<int>(world.faces.size());

	LevelMapBrush door;
	door.id = 1;
	door.entityId = 1;
	door.faces = {face("quakewall", 40)};
	door.faceCount = 1;

	document.brushes = {world, door};
	return document;
}

LevelMapDocument quake3Document()
{
	LevelMapDocument document;
	document.mapName = QStringLiteral("q3dm-sample");
	document.engineFamily = QStringLiteral("idTech3");
	document.format = LevelMapFormat::Quake3Map;
	document.entities = {entity(0, "worldspawn")};

	LevelMapBrush world;
	world.id = 0;
	world.entityId = 0;
	world.faces = {face("textures/base_wall/tgaonly", 20), face("textures/base_wall/jpgonly", 21),
		face("textures/base_wall/shaderonly", 22), face("textures/common/caulk", 23), face("noshader", 24),
		face("textures/base_wall/gone", 25)};
	world.faceCount = static_cast<int>(world.faces.size());

	LevelMapPatch patch;
	patch.id = 3;
	patch.entityId = 0;
	patch.textureName = QStringLiteral("textures/base_wall/shaderonly");
	patch.startLine = 60;

	document.brushes = {world};
	document.patches = {patch};
	return document;
}

bool runCandidatePathSmoke()
{
	bool ok = true;

	ok &= expect(mapTextureCandidatePaths(QStringLiteral("-"), LevelMapFormat::DoomWad, QStringLiteral("idTech1")).isEmpty(),
		"The Doom `-` placeholder must produce no candidate paths.");
	ok &= expect(isMapTexturePlaceholder(QStringLiteral("-"), LevelMapFormat::DoomWad), "`-` must be recognised as the Doom placeholder.");
	ok &= expect(!isMapTexturePlaceholder(QStringLiteral("-"), LevelMapFormat::QuakeMap), "`-` is only a placeholder in the Doom family.");

	const QStringList doom = mapTextureCandidatePaths(QStringLiteral("floor4_8"), LevelMapFormat::DoomWad, QStringLiteral("idTech1"));
	ok &= expect(!doom.isEmpty() && doom.first() == QStringLiteral("FLOOR4_8"), "Doom lookup should try the uppercased bare lump first.");
	ok &= expect(doom.contains(QStringLiteral("flats/FLOOR4_8")), "Doom lookup should try the flat namespace.");
	ok &= expect(doom.contains(QStringLiteral("patches/FLOOR4_8")), "Doom lookup should try the patch namespace.");

	const QStringList quake = mapTextureCandidatePaths(QStringLiteral("Metal1_1"), LevelMapFormat::QuakeMap, QStringLiteral("idTech2"));
	ok &= expect(!quake.isEmpty() && quake.first() == QStringLiteral("metal1_1"), "Quake lookup should try the bare miptex name first.");
	ok &= expect(quake.contains(QStringLiteral("textures/metal1_1")), "Quake lookup should try the textures/ form.");
	ok &= expect(quake.contains(QStringLiteral("metal1_1.mip")) && quake.contains(QStringLiteral("metal1_1.lmp")),
		"Quake lookup should probe the .mip and .lmp forms.");

	const QStringList quake3 = mapTextureCandidatePaths(QStringLiteral("textures/base_wall/foo"), LevelMapFormat::Quake3Map, QStringLiteral("idTech3"));
	ok &= expect(quake3.size() == 5, "Quake III lookup should try the shader name plus four image extensions.");
	if (quake3.size() == 5) {
		ok &= expect(quake3.at(0) == QStringLiteral("textures/base_wall/foo"), "Quake III lookup should try the shader name as written.");
		ok &= expect(quake3.at(1) == QStringLiteral("textures/base_wall/foo.tga"), "Quake III lookup should probe .tga before the lossy formats.");
		ok &= expect(quake3.at(2) == QStringLiteral("textures/base_wall/foo.jpg"), "Quake III lookup should probe .jpg.");
	}

	ok &= expect(isEngineHandledMapTexture(QStringLiteral("clip"), LevelMapFormat::QuakeMap, QStringLiteral("idTech2")),
		"Quake's clip texture is handled by the compiler.");
	ok &= expect(isEngineHandledMapTexture(QStringLiteral("*water1"), LevelMapFormat::QuakeMap, QStringLiteral("idTech2")),
		"Quake liquids are handled by the engine.");
	ok &= expect(isEngineHandledMapTexture(QStringLiteral("SKY4"), LevelMapFormat::QuakeMap, QStringLiteral("idTech2")),
		"Quake sky textures are handled by the engine.");
	ok &= expect(!isEngineHandledMapTexture(QStringLiteral("metal1_1"), LevelMapFormat::QuakeMap, QStringLiteral("idTech2")),
		"An ordinary Quake texture must still be looked up.");
	ok &= expect(isEngineHandledMapTexture(QStringLiteral("textures/common/caulk"), LevelMapFormat::Quake3Map, QStringLiteral("idTech3")),
		"The Quake III common family is handled by q3map2.");
	ok &= expect(isEngineHandledMapTexture(QStringLiteral("noshader"), LevelMapFormat::Quake3Map, QStringLiteral("idTech3")),
		"Quake III noshader is an engine placeholder.");
	ok &= expect(!isEngineHandledMapTexture(QStringLiteral("textures/base_wall/foo"), LevelMapFormat::Quake3Map, QStringLiteral("idTech3")),
		"An ordinary Quake III shader must still be looked up.");

	ok &= expect(normalizeMapTextureKey(QStringLiteral(" floor4_8 "), LevelMapFormat::DoomWad) == QStringLiteral("FLOOR4_8"),
		"Doom keys should be trimmed and uppercased.");
	ok &= expect(normalizeMapTextureKey(QStringLiteral("Textures\\Base_Wall\\Foo"), LevelMapFormat::Quake3Map) == QStringLiteral("textures/base_wall/foo"),
		"Quake III keys should be lowercased with forward slashes.");
	return ok;
}

bool runShaderScriptSmoke()
{
	bool ok = true;

	QStringList warnings;
	const QStringList names = collectShaderScriptNames(shaderScript(), &warnings);
	ok &= expect(names.size() == 2, "The shader script declares two shaders.");
	ok &= expect(names.contains(QStringLiteral("textures/base_wall/shaderonly")), "The first shader name should be collected.");
	ok &= expect(names.contains(QStringLiteral("textures/base_wall/tgaonly")), "The second shader name should be collected.");
	ok &= expect(warnings.isEmpty(), "A well-formed shader script should not warn.");

	warnings.clear();
	const QStringList broken = collectShaderScriptNames(malformedShaderScript(), &warnings);
	ok &= expect(broken.size() == 1, "A malformed script should still yield the names it did declare.");
	ok &= expect(!warnings.isEmpty(), "A malformed script should warn instead of failing silently.");

	warnings.clear();
	const QStringList truncated = collectShaderScriptNames(QByteArray("textures/a\n{\n}\n/* unterminated"), &warnings);
	ok &= expect(truncated.size() == 1, "An unterminated comment should not lose the earlier shader.");
	ok &= expect(!warnings.isEmpty(), "An unterminated block comment should warn.");

	ok &= expect(collectShaderScriptNames(QByteArray(), nullptr).isEmpty(), "An empty script should yield no names.");
	ok &= expect(collectShaderScriptNames(QByteArray("{{{{}}}}"), nullptr).isEmpty(), "Braces with no names should yield no names.");
	ok &= expect(collectShaderScriptNames(QByteArray("}}}}"), nullptr).isEmpty(), "Stray closing braces must not crash the scan.");
	return ok;
}

bool runDoomAuditSmoke(const PackageArchive& archive)
{
	bool ok = true;
	const MapTextureAudit audit = auditLevelMapTextures(doomDocument(), archive, true);

	ok &= expect(audit.referenceCount == 4, "The Doom fixture records four texture use sites.");
	ok &= expect(audit.uniqueCount == 3, "The Doom fixture uses three unique names.");
	ok &= expect(audit.references.size() == 3, "The reference list should match the unique count.");
	ok &= expect(audit.missingCount == 1, "Only MISSTEX1 is missing from the Doom fixture.");
	ok &= expect(audit.resolvedCount == 2, "The flat and the patch should both resolve.");
	ok &= expect(audit.state() == OperationState::Warning, "A map with a missing texture should report Warning.");
	ok &= expect(findReference(audit, "-") == nullptr, "The `-` placeholder must never appear as a reference.");

	const MapTextureReference* flat = findReference(audit, "floor4_8");
	ok &= expect(flat != nullptr, "The sector flat should be recorded.");
	if (flat) {
		ok &= expect(flat->resolved, "A lower-case sector flat should resolve against an upper-case lump.");
		ok &= expect(flat->resolvedPath == QStringLiteral("flats/FLOOR4_8"), "The flat should resolve to its real package path.");
		ok &= expect(flat->lookupKey == QStringLiteral("FLOOR4_8"), "Doom lookup keys should be uppercase.");
		ok &= expect(flat->useCount == 2, "The flat is used by the sector floor and the sector ceiling.");
		ok &= expect(flat->uses.size() == 2 && !flat->usesTruncated, "Both flat use sites should be recorded.");
		ok &= expect(hasUse(*flat, MapTextureUseKind::DoomSectorFloor, 0), "The sector floor use site should be attributed.");
		ok &= expect(hasUse(*flat, MapTextureUseKind::DoomSectorCeiling, 0), "The sector ceiling use site should be attributed.");
		ok &= expect(flat->decoded, "A 4096-byte flat should decode.");
		ok &= expect(flat->width == 64 && flat->height == 64, "A Doom flat is 64x64.");
		ok &= expect(!flat->sourceLayer.isEmpty(), "A resolved entry should name the package that provided it.");
	}

	const MapTextureReference* patch = findReference(audit, "startan3");
	ok &= expect(patch != nullptr, "The sidedef lower texture should be recorded.");
	if (patch) {
		ok &= expect(patch->resolved && patch->resolvedPath == QStringLiteral("patches/STARTAN3"), "The patch should resolve case-insensitively.");
		ok &= expect(hasUse(*patch, MapTextureUseKind::DoomSidedefLower, 0), "The sidedef lower use site should be attributed.");
		ok &= expect(!patch->decoded, "Unreadable image data should not be reported as decoded.");
	}
	ok &= expect(audit.undecodableCount == 1, "The undecodable patch should be counted, not fatal.");
	ok &= expect(!audit.warnings.isEmpty(), "An undecodable but resolved entry should warn.");

	const MapTextureReference* missing = findReference(audit, "MISSTEX1");
	ok &= expect(missing != nullptr, "The missing sidedef texture should be recorded.");
	if (missing) {
		ok &= expect(missing->isMissing() && !missing->resolved, "MISSTEX1 should be reported as missing.");
		ok &= expect(missing->resolution == MapTextureResolution::Missing, "A missing name should carry the missing resolution.");
		ok &= expect(hasUse(*missing, MapTextureUseKind::DoomSidedefMiddle, 0), "The sidedef middle use site should be attributed.");
		ok &= expect(!missing->candidatePaths.isEmpty(), "A missing name should still list what was searched.");
	}

	// Sorted by normalized key: FLOOR4_8, MISSTEX1, STARTAN3.
	ok &= expect(audit.references.at(0).lookupKey == QStringLiteral("FLOOR4_8")
			&& audit.references.at(1).lookupKey == QStringLiteral("MISSTEX1")
			&& audit.references.at(2).lookupKey == QStringLiteral("STARTAN3"),
		"References should be sorted by normalized name.");

	const QString text = mapTextureAuditText(audit);
	ok &= expect(text.contains(QStringLiteral("MAP01")), "The report should name the map.");
	ok &= expect(text.contains(QStringLiteral("MISSTEX1")), "The report should name the missing texture.");

	const QJsonObject json = mapTextureAuditJson(audit);
	ok &= expect(json.value(QStringLiteral("state")).toString() == QStringLiteral("warning"), "JSON should carry the untranslated state id.");
	ok &= expect(json.value(QStringLiteral("totals")).toObject().value(QStringLiteral("missing")).toInt() == 1, "JSON totals should report the missing count.");
	ok &= expect(json.value(QStringLiteral("references")).toArray().size() == 3, "JSON should list every reference.");
	return ok;
}

bool runQuakeAuditSmoke(const PackageArchive& archive)
{
	bool ok = true;
	const MapTextureAudit audit = auditLevelMapTextures(quakeDocument(), archive, false);

	ok &= expect(audit.referenceCount == 6, "The Quake fixture records six face use sites.");
	ok &= expect(audit.uniqueCount == 5, "The Quake fixture uses five unique names.");
	ok &= expect(audit.engineHandledCount == 3, "clip, *water1 and sky1 are engine-handled.");
	ok &= expect(audit.missingCount == 1, "Only missingwall is genuinely missing.");
	ok &= expect(audit.resolvedCount == 1, "Only quakewall resolves against the package.");

	for (const char* name : {"clip", "*water1", "sky1"}) {
		const MapTextureReference* reference = findReference(audit, name);
		ok &= expect(reference != nullptr, "Engine-handled names must still be listed.");
		if (reference) {
			ok &= expect(reference->engineHandled, "The special name should be flagged engine-handled.");
			ok &= expect(!reference->isMissing(), "An engine-handled name must never count as missing.");
			ok &= expect(reference->resolution == MapTextureResolution::EngineHandled, "The resolution should say engine-handled.");
			ok &= expect(!reference->noteId.isEmpty(), "An engine-handled name should carry an untranslated reason token.");
		}
	}

	const MapTextureReference* wall = findReference(audit, "QuakeWall");
	ok &= expect(wall != nullptr, "The shared wall texture should be recorded.");
	if (wall) {
		ok &= expect(wall->resolved && wall->resolvedPath == QStringLiteral("textures/quakewall"), "The wall texture should resolve case-insensitively.");
		ok &= expect(wall->useCount == 2, "The wall texture is used by two brushes.");
		ok &= expect(hasUse(*wall, MapTextureUseKind::WorldspawnFace, 0), "The worldspawn face should be attributed.");
		ok &= expect(hasUse(*wall, MapTextureUseKind::BrushEntityFace, 1), "The brush entity face should be attributed.");
		ok &= expect(!wall->uses.isEmpty() && wall->uses.first().line == 12, "A face use site should record the source line.");
		ok &= expect(!wall->decoded, "Nothing should be decoded when decoding was not requested.");
	}

	const MapTextureReference* missing = findReference(audit, "missingwall");
	ok &= expect(missing != nullptr && missing->isMissing(), "missingwall should be reported as missing.");
	return ok;
}

bool runQuake3AuditSmoke(const PackageArchive& archive)
{
	bool ok = true;
	const MapTextureAudit audit = auditLevelMapTextures(quake3Document(), archive, false);

	ok &= expect(audit.referenceCount == 7, "The Quake III fixture records seven use sites.");
	ok &= expect(audit.uniqueCount == 6, "The Quake III fixture uses six unique names.");
	ok &= expect(audit.shaderNameCount >= 2, "The package's shader scripts should be parsed.");
	ok &= expect(audit.missingCount == 1, "Only the absent shader should be missing.");
	ok &= expect(audit.engineHandledCount == 2, "The common family and noshader are engine-handled.");

	const MapTextureReference* tga = findReference(audit, "textures/base_wall/tgaonly");
	ok &= expect(tga != nullptr, "The TGA-backed shader should be recorded.");
	if (tga) {
		ok &= expect(tga->resolved && tga->resolution == MapTextureResolution::ResolvedEntry, "An image file should win over the shader declaration.");
		ok &= expect(tga->resolvedPath == QStringLiteral("textures/base_wall/tgaonly.tga"), "Extension probing should find the .tga file.");
	}

	const MapTextureReference* jpg = findReference(audit, "textures/base_wall/jpgonly");
	ok &= expect(jpg != nullptr, "The JPEG-backed shader should be recorded.");
	if (jpg) {
		ok &= expect(jpg->resolved && jpg->resolvedPath == QStringLiteral("textures/base_wall/jpgonly.jpg"), "Extension probing should find the .jpg file.");
	}

	const MapTextureReference* shaderOnly = findReference(audit, "textures/base_wall/shaderonly");
	ok &= expect(shaderOnly != nullptr, "The script-only shader should be recorded.");
	if (shaderOnly) {
		ok &= expect(shaderOnly->resolved, "A shader declared by a script counts as resolved.");
		ok &= expect(shaderOnly->resolution == MapTextureResolution::ResolvedByShader, "The resolution should say it came from a shader script.");
		ok &= expect(shaderOnly->resolvedPath == QStringLiteral("scripts/base.shader"), "The declaring script should be named.");
		ok &= expect(shaderOnly->useCount == 2, "The script-only shader is used by a brush face and a patch.");
		ok &= expect(hasUse(*shaderOnly, MapTextureUseKind::WorldspawnFace, 0), "The brush face use site should be attributed.");
		ok &= expect(hasUse(*shaderOnly, MapTextureUseKind::PatchShader, 3), "The patch use site should be attributed.");
	}

	const MapTextureReference* common = findReference(audit, "textures/common/caulk");
	ok &= expect(common != nullptr && common->engineHandled && !common->isMissing(), "textures/common/* must not be reported as missing.");

	const MapTextureReference* gone = findReference(audit, "textures/base_wall/gone");
	ok &= expect(gone != nullptr && gone->isMissing(), "A shader with neither script nor image should be missing.");

	// The package holds a deliberately malformed script; the audit must warn
	// rather than fail, and must still resolve everything the good script declares.
	ok &= expect(!audit.warnings.isEmpty(), "The malformed shader script should produce a warning.");
	ok &= expect(audit.state() == OperationState::Warning, "A Quake III map with a missing shader should report Warning.");

	const MapTextureAudit repeat = auditLevelMapTextures(quake3Document(), archive, false);
	const QByteArray first = QJsonDocument(mapTextureAuditJson(audit)).toJson(QJsonDocument::Compact);
	const QByteArray second = QJsonDocument(mapTextureAuditJson(repeat)).toJson(QJsonDocument::Compact);
	ok &= expect(first == second, "Two audits of the same inputs must produce identical JSON.");
	ok &= expect(mapTextureAuditText(audit) == mapTextureAuditText(repeat), "Two audits of the same inputs must produce identical text.");
	return ok;
}

bool runDirectoryAuditSmoke(const QString& root)
{
	bool ok = true;
	const MapTextureAudit audit = auditLevelMapTexturesInDirectories(doomDocument(), {root}, false);
	ok &= expect(audit.uniqueCount == 3, "The folder audit should see the same unique names.");
	ok &= expect(audit.resolvedCount == 2, "The folder audit should resolve the flat and the patch.");
	ok &= expect(audit.missingCount == 1, "The folder audit should report the same missing texture.");
	ok &= expect(!audit.packageSource.isEmpty(), "The folder audit should name the roots it searched.");

	const MapTextureReference* flat = findReference(audit, "floor4_8");
	ok &= expect(flat != nullptr && flat->resolved && !flat->sourceLayer.isEmpty(), "A folder root should be reported as the providing layer.");

	const MapTextureAudit withMissingRoot = auditLevelMapTexturesInDirectories(doomDocument(), {root, root + QStringLiteral("/does-not-exist")}, false);
	ok &= expect(!withMissingRoot.warnings.isEmpty(), "A missing asset folder should warn.");
	ok &= expect(withMissingRoot.missingCount == 1, "A missing asset folder should not change the resolved results.");

	const MapTextureAudit noRoots = auditLevelMapTexturesInDirectories(doomDocument(), QStringList(), false);
	ok &= expect(!noRoots.warnings.isEmpty(), "An empty root list should warn.");
	ok &= expect(noRoots.missingCount == 3, "With no roots, every real reference is unresolved.");
	return ok;
}

bool runEmptyDocumentSmoke(const PackageArchive& archive)
{
	bool ok = true;
	LevelMapDocument document;
	document.mapName = QStringLiteral("empty");
	document.format = LevelMapFormat::QuakeMap;
	const MapTextureAudit audit = auditLevelMapTextures(document, archive, true);
	ok &= expect(audit.references.isEmpty(), "A map with no textures should produce no references.");
	ok &= expect(audit.state() == OperationState::Idle, "A map with no references should report Idle.");
	ok &= expect(audit.missingCount == 0, "A map with no references has nothing missing.");

	LevelMapDocument placeholders = doomDocument();
	placeholders.doomSectors.clear();
	placeholders.doomSidedefs[0].lowerTexture = QStringLiteral("-");
	placeholders.doomSidedefs[0].middleTexture = QStringLiteral("  ");
	placeholders.doomSidedefs[0].upperTexture = QStringLiteral("-");
	const MapTextureAudit onlyPlaceholders = auditLevelMapTextures(placeholders, archive, false);
	ok &= expect(onlyPlaceholders.references.isEmpty(), "A map of nothing but placeholders has no references.");
	ok &= expect(onlyPlaceholders.state() == OperationState::Idle, "A map of nothing but placeholders should report Idle.");
	return ok;
}

} // namespace

int main()
{
	QTemporaryDir temporaryDir;
	if (!temporaryDir.isValid()) {
		std::cerr << "Unable to create a temporary directory.\n";
		return EXIT_FAILURE;
	}

	const QString root = QDir(temporaryDir.path()).absoluteFilePath(QStringLiteral("package"));
	bool ok = buildPackage(root);
	if (!ok) {
		return EXIT_FAILURE;
	}

	PackageArchive archive;
	QString error;
	if (!archive.load(root, &error)) {
		std::cerr << "Unable to open the folder package fixture.\n";
		return EXIT_FAILURE;
	}

	ok &= runCandidatePathSmoke();
	ok &= runShaderScriptSmoke();
	ok &= runDoomAuditSmoke(archive);
	ok &= runQuakeAuditSmoke(archive);
	ok &= runQuake3AuditSmoke(archive);
	ok &= runDirectoryAuditSmoke(root);
	ok &= runEmptyDocumentSmoke(archive);
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
