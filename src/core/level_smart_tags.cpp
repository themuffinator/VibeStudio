#include "core/level_smart_tags.h"
#include "core/level_entity_create.h"
#include "core/level_face_flags.h"
#include "core/level_history_p.h"
#include "core/level_patch_tools.h"
#include "core/level_placement_control_p.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_visibility.h"
#include "core/asset_tools.h"
#include "core/package_archive.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

namespace vibestudio {
namespace {
struct Text { Q_DECLARE_TR_FUNCTIONS(VibeStudioLevelSmartTags) };
bool fail(QString* error, const QString& message)
{
	if (error) { *error = message; }
	return false;
}
QString exactPattern(const QString& pattern)
{
	QString result;
	for (qsizetype i = 0; i < pattern.size(); ++i) {
		const QChar ch = pattern[i];
		if (ch == QLatin1Char('\\') && i + 1 < pattern.size()) { result += pattern[++i]; }
		else if (ch == QLatin1Char('*') || ch == QLatin1Char('?')) { return {}; }
		else { result += ch; }
	}
	return result;
}
} // namespace

QStringList levelSmartTagPackageMaterials(const PackageArchiveReader& reader)
{
	QStringList names;
	QSet<QString> seen;
	for (const PackageEntry& entry : reader.entries()) {
		if (entry.kind != PackageEntryKind::File) { continue; }
		QString name;
		if (entry.typeHint == QLatin1String("wad-texture")) {
			name = QFileInfo(entry.virtualPath).completeBaseName();
		} else if (entry.virtualPath.startsWith(QLatin1String("textures/"), Qt::CaseInsensitive)
			&& assetPreviewKindForEntry(entry.virtualPath, entry.typeHint) == AssetPreviewKind::Image) {
			name = entry.virtualPath.mid(9);
			const qsizetype dot = name.lastIndexOf('.');
			if (dot > name.lastIndexOf('/')) { name.truncate(dot); }
		}
		if (!name.isEmpty() && !seen.contains(name.toCaseFolded())) { names << name; seen.insert(name.toCaseFolded()); }
	}
	names.sort(Qt::CaseInsensitive);
	return names;
}

bool levelSmartTagCanDisable(const LevelFilterTag& tag)
{
	return tag.match == LevelFilterTagMatch::ContentFlag || tag.match == LevelFilterTagMatch::SurfaceFlag || tag.match == LevelFilterTagMatch::ClassName;
}

QString levelSmartTagCommandId(const QString& tagId, bool enabled)
{
	return isLevelFilterTagId(tagId) ? (enabled ? QStringLiteral("map.makeTag.") : QStringLiteral("map.makeNonTag.")) + tagId.mid(4) : QString();
}

bool levelSmartTagForCommand(const QString& commandId, QString* tagId, bool* enabled)
{
	for (const QString& prefix : {QStringLiteral("map.makeTag."), QStringLiteral("map.make-tag."), QStringLiteral("map.makeNonTag."), QStringLiteral("map.make-non-tag.")}) {
		if (!commandId.startsWith(prefix)) { continue; }
		const QString id = QStringLiteral("tag:") + commandId.mid(prefix.size());
		if (!isLevelFilterTagId(id)) { return false; }
		if (tagId) { *tagId = id; }
		if (enabled) { *enabled = !prefix.contains(QStringLiteral("Non")) && !prefix.contains(QStringLiteral("non-")); }
		return true;
	}
	return false;
}

QStringList levelSmartTagChoices(const LevelFilterTag& tag, const QStringList& materials,
	const EntityDefinitionCatalogue& definitions, const LevelViewFilterMaterials& shaders)
{
	QStringList candidates;
	if (tag.match == LevelFilterTagMatch::ClassName) {
		for (const EntityClassDefinition& definition : definitions.classes) {
			if (definition.kind == EntityClassKind::Brush) { candidates << definition.className; }
		}
	} else if (tag.match == LevelFilterTagMatch::Texture || tag.match == LevelFilterTagMatch::SurfaceParm) {
		candidates = materials;
		candidates += shaders.shaders.keys();
	} else { return {}; }
	if (tag.match != LevelFilterTagMatch::SurfaceParm) {
		for (const QString& pattern : tag.patterns) {
			const QString exact = exactPattern(pattern);
			if (!exact.isEmpty()) { candidates << exact; }
		}
	}
	QStringList choices;
	QSet<QString> seen;
	for (const QString& candidate : candidates) {
		if (!seen.contains(candidate.toLower()) && levelFilterTagMatchesName(tag, candidate, shaders)) {
			choices << candidate;
			seen.insert(candidate.toLower());
		}
	}
	choices.sort(Qt::CaseInsensitive);
	return choices;
}

bool applyLevelSmartTag(LevelMapDocument* document, const LevelSmartTagRequest& request, QString* error)
{
	if (error) { error->clear(); }
	if (!document || !levelFilterTagsApply(*document)) { return fail(error, Text::tr("Smart tags edit Quake-family maps.")); }
	if (!request.enabled && !levelSmartTagCanDisable(request.tag)) {
		return fail(error, Text::tr("Texture and surfaceparm tags cannot be removed; apply another material instead."));
	}
	const auto& tag = request.tag;
	LevelMapDocument candidate = *document;
	{
		const detail::LevelMapHistoryScope history(candidate);
		const qsizetype before = candidate.undoStack.size();
		const QSet<QString> excluded = levelMapHiddenObjects(candidate) | levelSceneLockedObjects(candidate) | request.excludedObjects;
		const auto context = levelSceneSelectionContext(candidate);
		const auto available = [&](LevelMapSelectionKind kind, int id, int owner) {
			const QString self = levelMapSelectionRefId({kind, id});
			return !excluded.contains(self) && !excluded.contains(levelMapSelectionRefId({LevelMapSelectionKind::Entity, owner}))
				&& (context.openGroup.isEmpty() || context.allowedObjects.contains(self))
				&& (!request.surfaceTargets || !context.closedGroups.contains(self));
		};
		if (tag.match == LevelFilterTagMatch::ClassName) {
			if (request.surfaceTargets) { return fail(error, Text::tr("Select whole brushes or brush entities to change a classname tag.")); }
			QSet<int> brushes, patches, owners;
			for (const auto& ref : candidate.selection) {
				if (ref.kind == LevelMapSelectionKind::QuakeBrush) { brushes.insert(ref.objectId); }
				else if (ref.kind == LevelMapSelectionKind::QuakePatch) { patches.insert(ref.objectId); }
				else if (ref.kind == LevelMapSelectionKind::Entity) { owners.insert(ref.objectId); }
				else { return fail(error, Text::tr("Select brushes, patches or brush entities for a classname tag.")); }
			}
			QSet<int> affectedOwners;
			for (const auto& brush : candidate.brushes) {
				if (brushes.contains(brush.id) || owners.contains(brush.entityId)) { affectedOwners.insert(brush.entityId); brushes.insert(brush.id); }
			}
			for (const auto& patch : candidate.patches) {
				if (patches.contains(patch.id) || owners.contains(patch.entityId)) { affectedOwners.insert(patch.entityId); patches.insert(patch.id); }
			}
			if (brushes.isEmpty() && patches.isEmpty()) { return fail(error, Text::tr("Select brushes or patches for a classname tag.")); }
			for (const int owner : owners) {
				if (!affectedOwners.contains(owner)) { return fail(error, Text::tr("Select brushes, patches or brush entities for a classname tag.")); }
			}
			if (!request.enabled) {
				for (const auto& entity : candidate.entities) {
					if (affectedOwners.contains(entity.id) && !levelFilterTagMatchesName(tag, entity.className)) {
						return fail(error, Text::tr("The selected owner %1 does not match %2; nothing changed.").arg(entity.className, tag.name));
					}
				}
			}
			// Move to World removes entire owners, including unselected siblings.
			// Check those too, so a hidden/locked sibling cannot change indirectly.
			for (const auto& brush : candidate.brushes) {
				if ((brushes.contains(brush.id) || (!request.enabled && affectedOwners.contains(brush.entityId)))
					&& !available(LevelMapSelectionKind::QuakeBrush, brush.id, brush.entityId)) {
					return fail(error, Text::tr("A targeted brush is hidden, locked or outside the open group."));
				}
			}
			for (const auto& patch : candidate.patches) {
				if ((patches.contains(patch.id) || (!request.enabled && affectedOwners.contains(patch.entityId)))
					&& !available(LevelMapSelectionKind::QuakePatch, patch.id, patch.entityId)) {
					return fail(error, Text::tr("A targeted patch is hidden, locked or outside the open group."));
				}
			}
			if (request.enabled) {
				if (!levelEntityCreateClassNameUsable(request.choice) || !levelFilterTagMatchesName(tag, request.choice)) {
					return fail(error, Text::tr("Choose a brush classname matching %1.").arg(tag.name));
				}
				if (!tag.texture.isEmpty()) {
					LevelTextureResetRequest reset;
					reset.faces = levelMapSelectedSurfaces(candidate);
					reset.patches = levelMapSelectedPatchIds(candidate);
					reset.renameTo = tag.texture;
					LevelSurfaceEditPlan plan;
					if (!prepareLevelTextureReset(candidate, reset, {}, &plan, error)) { return false; }
					if (plan.ready() && !commitLevelSurfaceEdit(&candidate, plan, error)) { return false; }
				}
				if (!tieLevelMapSelectionToEntity(&candidate, request.choice, {}, nullptr, error)) { return false; }
			} else if (!moveLevelMapSelectionToWorld(&candidate, nullptr, error)) { return false; }
		} else {
			const auto faces = request.surfaceTargets ? request.faces : levelMapSelectedSurfaces(candidate);
			const auto patches = request.surfaceTargets ? request.patches : levelMapSelectedPatchIds(candidate);
			if (faces.isEmpty() && patches.isEmpty()) { return fail(error, Text::tr("Select brushes or patches, or pick faces, to apply a smart tag.")); }
			QHash<int, const LevelMapBrush*> brushById;
			for (const auto& brush : candidate.brushes) { brushById.insert(brush.id, &brush); }
			QHash<int, QSet<int>> faceIndices;
			for (const auto& target : faces) {
				const auto* brush = brushById.value(target.brushId);
				if (!brush || target.faceIndex < 0 || target.faceIndex >= brush->faces.size()) { return fail(error, Text::tr("A targeted face no longer exists.")); }
				if (!available(LevelMapSelectionKind::QuakeBrush, brush->id, brush->entityId)) { return fail(error, Text::tr("A targeted brush is hidden, locked or outside the open group.")); }
				faceIndices[brush->id].insert(target.faceIndex);
			}
			for (const int id : patches) {
				const auto found = std::find_if(candidate.patches.cbegin(), candidate.patches.cend(), [id](const auto& patch) { return patch.id == id; });
				if (found == candidate.patches.cend() || !available(LevelMapSelectionKind::QuakePatch, id, found->entityId)) {
					return fail(error, Text::tr("A targeted patch is missing, hidden, locked or outside the open group."));
				}
			}
			if (tag.match == LevelFilterTagMatch::Texture || tag.match == LevelFilterTagMatch::SurfaceParm) {
				if (!levelFilterTagMatchesName(tag, request.choice, request.shaders)) { return fail(error, Text::tr("Choose a material matching %1.").arg(tag.name)); }
				LevelTextureResetRequest reset;
				reset.faces = faces; reset.patches = patches; reset.renameTo = request.choice;
				LevelSurfaceEditPlan plan;
				if (!prepareLevelTextureReset(candidate, reset, {}, &plan, error) || !commitLevelSurfaceEdit(&candidate, plan, error)) { return false; }
			} else {
				if (!patches.isEmpty()) { return fail(error, Text::tr("Patches carry no content or surface flags; select brush faces only.")); }
				if (levelFaceFlagGame(candidate) == LevelFaceFlagGame::None) {
					return fail(error, QCoreApplication::translate("VibeStudioLevelFaceFlags", "Face flags belong to Quake II and Quake III maps; this map's faces carry none."));
				}
				QStringList unresolved;
				quint32 bits = levelFilterTagFlagBits(tag, candidate, &unresolved);
				if (!unresolved.isEmpty() || bits == 0) { return fail(error, Text::tr("The map cannot resolve these tag flags: %1.").arg(unresolved.join(QStringLiteral(", ")))); }
				if (tag.match == LevelFilterTagMatch::ContentFlag && (bits & kLevelFaceDetailContent)) {
					QVector<LevelMapSelectionRef> whole;
					bool changesDetail = false;
					for (auto it = faceIndices.cbegin(); it != faceIndices.cend(); ++it) {
						const auto* brush = brushById.value(it.key());
						if (it->size() != brush->faces.size()) { return fail(error, Text::tr("The detail tag changes whole brushes; select every face of each brush.")); }
						whole << LevelMapSelectionRef{LevelMapSelectionKind::QuakeBrush, it.key()};
						for (const auto& face : brush->faces) { changesDetail |= ((face.contentFlags & kLevelFaceDetailContent) != 0) != request.enabled; }
					}
					if (changesDetail) {
						const auto selected = candidate.selection;
						if (!setLevelMapSelection(&candidate, whole, error)) { return false; }
						if (!setLevelMapSelectionDetail(&candidate, request.enabled, nullptr, error)) { return false; }
						if (!setLevelMapSelection(&candidate, selected, error)) { return false; }
						candidate.undoStack.last().hasSelectionSnapshot = true;
						candidate.undoStack.last().selectionSnapshot = selected;
						candidate.undoStack.last().selectionResult = selected;
					}
					bits &= ~kLevelFaceDetailContent;
				}
				if (bits != 0) {
					LevelFaceFlagsEdit edit;
					if (tag.match == LevelFilterTagMatch::ContentFlag) { (request.enabled ? edit.setContent : edit.clearContent) = bits; }
					else { (request.enabled ? edit.setSurface : edit.clearSurface) = bits; }
					const auto summary = levelFaceFlagsSummary(candidate, faces);
					const quint32 all = tag.match == LevelFilterTagMatch::ContentFlag ? summary.contentAll : summary.surfaceAll;
					const quint32 any = tag.match == LevelFilterTagMatch::ContentFlag ? summary.contentAny : summary.surfaceAny;
					if ((request.enabled && (all & bits) != bits) || (!request.enabled && (any & bits) != 0)) {
						if (!setLevelMapFaceFlags(&candidate, faces, edit, nullptr, error)) { return false; }
					}
				}
			}
		}
		const int added = static_cast<int>(candidate.undoStack.size() - before);
		if (added == 0) { return fail(error, Text::tr("The selection already has this tag state; nothing changed.")); }
		if (added > 1 && !foldLevelMapFollowUpEdits(&candidate, added - 1, error)) { return false; }
		detail::placementCancellationCheckpoint();
	}
	*document = std::move(candidate);
	return true;
}
} // namespace vibestudio
