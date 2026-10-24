#include "core/level_material_find.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_textured_selection.h"
#include "core/level_visibility.h"
#include <QCoreApplication>

// Workflow references, no source copied: TrenchBroom v2026.2 (4c756be1,
// GPL-3.0-or-later) MapWindow::revealMaterial and its materialToReveal rule,
// and NetRadiant Custom 449778b3 (GPL-2.0-or-later) FindReplaceTextures,
// Scene_BrushFindReplaceShader* and Scene_PatchFindReplaceShader*. The scope
// model, visibility, lock and group rules, picked-face scope and preview are
// original studio implementations; see docs/CREDITS.md.
namespace vibestudio {
namespace {
struct Text { Q_DECLARE_TR_FUNCTIONS(LevelMaterialFind) };

bool isMaterial(const QString& trimmed) { return !trimmed.isEmpty() && trimmed != QLatin1String("-"); }

LevelMaterialTarget targetFor(const LevelMapTextureChange& surface)
{
	LevelMaterialTarget target;
	target.objectId = surface.objectId;
	if (surface.kind == QLatin1String("face")) { target.kind = LevelMaterialKind::BrushFace; target.faceIndex = surface.part; }
	else if (surface.kind == QLatin1String("patch")) { target.kind = LevelMaterialKind::Patch; }
	else if (surface.kind == QLatin1String("sidedef")) {
		target.kind = surface.part == 0 ? LevelMaterialKind::WallUpper : surface.part == 1 ? LevelMaterialKind::WallLower : LevelMaterialKind::WallMiddle;
	} else if (surface.kind == QLatin1String("sector")) { target.kind = surface.part == 0 ? LevelMaterialKind::SectorFloor : LevelMaterialKind::SectorCeiling; }
	return target;
}

LevelMapTextureChange surfaceFor(const LevelMaterialTarget& target)
{
	LevelMapTextureChange surface;
	surface.objectId = target.objectId;
	switch (target.kind) {
	case LevelMaterialKind::BrushFace: surface.kind = QStringLiteral("face"); surface.part = target.faceIndex; break;
	case LevelMaterialKind::Patch: surface.kind = QStringLiteral("patch"); break;
	case LevelMaterialKind::WallUpper: surface.kind = QStringLiteral("sidedef"); surface.part = 0; break;
	case LevelMaterialKind::WallLower: surface.kind = QStringLiteral("sidedef"); surface.part = 1; break;
	case LevelMaterialKind::WallMiddle: surface.kind = QStringLiteral("sidedef"); surface.part = 2; break;
	case LevelMaterialKind::SectorFloor: surface.kind = QStringLiteral("sector"); surface.part = 0; break;
	case LevelMaterialKind::SectorCeiling: surface.kind = QStringLiteral("sector"); surface.part = 1; break;
	default: break;
	}
	return surface;
}
} // namespace

LevelSelectionMaterial levelSelectionMaterial(const LevelMapDocument& document, const QVector<LevelSurfaceFace>& pickedFaces,
	const QVector<int>& pickedPatches)
{
	LevelSelectionMaterial result;
	// TrenchBroom reveals only when every face shares one material; the first
	// different name settles Mixed, so a large mixed selection stops early.
	const auto visit = [&result](const QString& name, bool flat, bool wall) {
		const QString trimmed = name.trimmed();
		if (!isMaterial(trimmed)) { return true; }
		++result.surfaces;
		if (result.state == LevelSelectionMaterial::State::None) {
			result.state = LevelSelectionMaterial::State::Single;
			result.material = trimmed;
		} else if (trimmed.compare(result.material, Qt::CaseInsensitive) != 0) {
			result.state = LevelSelectionMaterial::State::Mixed;
			result.other = trimmed;
			return false;
		}
		result.flat = result.flat || flat;
		result.wall = result.wall || wall;
		return true;
	};
	if (!pickedFaces.isEmpty() || !pickedPatches.isEmpty()) {
		result.fromPicks = true;
		QHash<int, const LevelMapBrush*> brushes;
		for (const auto& brush : document.brushes) { brushes.insert(brush.id, &brush); }
		for (const auto& face : pickedFaces) {
			const auto* brush = brushes.value(face.brushId);
			if (brush && face.faceIndex >= 0 && face.faceIndex < brush->faces.size() && !visit(brush->faces[face.faceIndex].textureName, false, false)) {
				return result;
			}
		}
		const QSet<int> patches(pickedPatches.cbegin(), pickedPatches.cend());
		for (const auto& patch : document.patches) {
			if (patches.contains(patch.id) && !visit(patch.textureName, false, false)) { return result; }
		}
		return result;
	}
	QSet<int> entities, brushIds, patchIds, lines, sectors;
	for (const auto& ref : document.selection) {
		switch (ref.kind) {
		case LevelMapSelectionKind::Entity: entities.insert(ref.objectId); break;
		case LevelMapSelectionKind::QuakeBrush: brushIds.insert(ref.objectId); break;
		case LevelMapSelectionKind::QuakePatch: patchIds.insert(ref.objectId); break;
		case LevelMapSelectionKind::DoomLinedef: lines.insert(ref.objectId); break;
		case LevelMapSelectionKind::DoomSector: sectors.insert(ref.objectId); break;
		default: break;
		}
	}
	if (entities.isEmpty() && brushIds.isEmpty() && patchIds.isEmpty() && lines.isEmpty() && sectors.isEmpty()) { return result; }
	for (const auto& brush : document.brushes) {
		if (!brushIds.contains(brush.id) && !entities.contains(brush.entityId)) { continue; }
		for (const auto& face : brush.faces) {
			if (!visit(face.textureName, false, false)) { return result; }
		}
	}
	for (const auto& patch : document.patches) {
		if ((patchIds.contains(patch.id) || entities.contains(patch.entityId)) && !visit(patch.textureName, false, false)) { return result; }
	}
	if (document.format == LevelMapFormat::DoomWad) {
		QSet<int> sides;
		for (const auto& line : document.doomLinedefs) {
			if (lines.contains(line.id)) { sides.insert(line.frontSidedef); sides.insert(line.backSidedef); }
		}
		for (const auto& side : document.doomSidedefs) {
			if (!sides.contains(side.id)) { continue; }
			for (const QString* name : {&side.upperTexture, &side.lowerTexture, &side.middleTexture}) {
				if (!visit(*name, false, true)) { return result; }
			}
		}
		for (const auto& sector : document.doomSectors) {
			if (sectors.contains(sector.id) && (!visit(sector.floorTexture, true, false) || !visit(sector.ceilingTexture, true, false))) { return result; }
		}
	}
	return result;
}

bool previewLevelMaterialReplace(const LevelMapDocument& document, const LevelMaterialReplaceRequest& request,
	LevelMaterialReplaceReport* report, QString* error)
{
	if (error) { error->clear(); }
	const auto fail = [error](const QString& message) { if (error) { *error = message; } return false; };
	const bool doom = document.format == LevelMapFormat::DoomWad;
	if (document.format == LevelMapFormat::Unknown) {
		return fail(Text::tr("Open a map to find or replace its textures."));
	}
	if (doom && document.doomFormat == LevelMapDoomFormat::Udmf) {
		return fail(Text::tr("Use UDMF Properties to edit this map's textures; Find and Replace does not support UDMF yet."));
	}
	const QString find = request.find.trimmed();
	if (find.isEmpty()) { return fail(Text::tr("Name the texture to find.")); }
	if (request.scope == LevelMaterialFindScope::Selection && document.selection.isEmpty()) {
		return fail(Text::tr("Nothing is selected. Select objects, or search the whole map."));
	}
	QVector<LevelMapTextureChange> surfaces;
	if (request.scope == LevelMaterialFindScope::Picks) {
		if (doom) { return fail(Text::tr("Picked faces belong to Quake-family maps.")); }
		if (request.faces.isEmpty() && request.patches.isEmpty()) {
			return fail(Text::tr("No faces are picked. Pick faces, or search the selection or the whole map."));
		}
		if (request.faces.size() + request.patches.size() > kLevelTexturedSelectionLimit) {
			return fail(Text::tr("Find and Replace accepts at most 16,384 picked faces and patches."));
		}
		QHash<int, const LevelMapBrush*> brushes;
		for (const auto& brush : document.brushes) { brushes.insert(brush.id, &brush); }
		QSet<QPair<int, int>> seen;
		for (const auto& face : request.faces) {
			const auto* brush = brushes.value(face.brushId);
			if (!brush || face.faceIndex < 0 || face.faceIndex >= brush->faces.size()) {
				return fail(Text::tr("A picked face is missing: brush %1, face %2. Pick the faces again.").arg(face.brushId).arg(face.faceIndex + 1));
			}
			if (seen.contains({face.brushId, face.faceIndex})) { continue; }
			seen.insert({face.brushId, face.faceIndex});
			surfaces.push_back({QStringLiteral("face"), face.brushId, face.faceIndex, brush->faces[face.faceIndex].textureName, QString()});
		}
		QHash<int, const LevelMapPatch*> patches;
		for (const auto& patch : document.patches) { patches.insert(patch.id, &patch); }
		QSet<int> seenPatches;
		for (int id : request.patches) {
			const auto* patch = patches.value(id);
			if (!patch) { return fail(Text::tr("Picked patch %1 is missing. Pick it again.").arg(id)); }
			if (seenPatches.contains(id)) { continue; }
			seenPatches.insert(id);
			surfaces.push_back({QStringLiteral("patch"), id, 0, patch->textureName, QString()});
		}
	} else {
		surfaces = levelMapTextureSurfaces(document, request.scope == LevelMaterialFindScope::Selection);
	}

	// Editable means visible, unlocked and inside the open group. Closed groups
	// stay editable as wholes, as the selection commands treat them.
	const auto context = levelSceneSelectionContext(document);
	const auto excluded = levelMapHiddenObjects(document) | levelSceneLockedObjects(document) | request.excludedObjects;
	QHash<int, int> brushOwners, patchOwners;
	for (const auto& brush : document.brushes) { brushOwners.insert(brush.id, brush.entityId); }
	for (const auto& patch : document.patches) { patchOwners.insert(patch.id, patch.entityId); }
	QHash<int, QVector<int>> sideLines;
	if (doom) {
		for (const auto& line : document.doomLinedefs) {
			for (int side : {line.frontSidedef, line.backSidedef}) {
				if (side >= 0) { sideLines[side].append(line.id); }
			}
		}
	}
	const auto editable = [&](const QString& key, const QString& owner, bool* closed) {
		if (!context.allowedObjects.contains(key) || excluded.contains(key) || (!owner.isEmpty() && excluded.contains(owner))) { return false; }
		*closed = *closed || context.closedGroups.contains(key);
		return true;
	};
	const QString replacement = request.replace.trimmed();
	LevelMaterialReplaceReport result;
	for (const auto& surface : surfaces) {
		if (surface.oldName.trimmed().compare(find, Qt::CaseInsensitive) != 0) { continue; }
		bool closed = false;
		bool ok = true;
		if (surface.kind == QLatin1String("face")) {
			ok = editable(levelMapSelectionRefId({LevelMapSelectionKind::QuakeBrush, surface.objectId}),
				levelMapSelectionRefId({LevelMapSelectionKind::Entity, brushOwners.value(surface.objectId, -1)}), &closed);
		} else if (surface.kind == QLatin1String("patch")) {
			ok = editable(levelMapSelectionRefId({LevelMapSelectionKind::QuakePatch, surface.objectId}),
				levelMapSelectionRefId({LevelMapSelectionKind::Entity, patchOwners.value(surface.objectId, -1)}), &closed);
		} else if (surface.kind == QLatin1String("sidedef")) {
			// A side changes with every line that shows it.
			ok = !excluded.contains(QStringLiteral("sidedef:%1").arg(surface.objectId));
			for (int line : sideLines.value(surface.objectId)) {
				ok = ok && editable(levelMapSelectionRefId({LevelMapSelectionKind::DoomLinedef, line}), QString(), &closed);
			}
		} else if (surface.kind == QLatin1String("sector")) {
			ok = editable(levelMapSelectionRefId({LevelMapSelectionKind::DoomSector, surface.objectId}), QString(), &closed);
		}
		if (!ok) {
			++result.skipped;
			continue;
		}
		result.targets.push_back(targetFor(surface));
		result.inClosedGroups += closed ? 1 : 0;
		result.changes += !replacement.isEmpty() && surface.oldName != replacement ? 1 : 0;
	}
	if (report) { *report = std::move(result); }
	return true;
}

bool replaceLevelMapMaterial(LevelMapDocument* document, const LevelMaterialReplaceRequest& request,
	LevelMaterialReplaceReport* report, QString* error)
{
	if (error) { error->clear(); }
	const auto fail = [error](const QString& message) { if (error) { *error = message; } return false; };
	if (!document) { return fail(Text::tr("Open a map to find or replace its textures.")); }
	LevelMaterialReplaceReport result;
	if (!previewLevelMaterialReplace(*document, request, &result, error)) { return false; }
	const QString find = request.find.trimmed();
	const QString replacement = request.replace.trimmed();
	if (replacement.isEmpty()) { return fail(Text::tr("Name the texture to use instead.")); }
	if (result.targets.isEmpty()) {
		return fail(result.skipped > 0 ? Text::tr("Every use of %1 in scope is hidden, locked or outside the open group.").arg(find)
									   : Text::tr("Nothing in scope uses %1.").arg(find));
	}
	if (result.changes == 0) { return fail(Text::tr("Everything in scope already uses %1.").arg(replacement)); }
	QVector<LevelMapTextureChange> uses;
	uses.reserve(result.targets.size());
	for (const auto& target : std::as_const(result.targets)) { uses.push_back(surfaceFor(target)); }
	int replaced = 0;
	if (!replaceLevelMapTextureUses(document, find, replacement, uses, &replaced, error)) { return false; }
	result.replaced = replaced;
	if (report) { *report = std::move(result); }
	return true;
}

} // namespace vibestudio
