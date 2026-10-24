#include "core/level_entity_selection_p.h"
#include "core/level_history_p.h"
#include "core/level_placement_control_p.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_visibility.h"
#include <QCoreApplication>
#include <algorithm>

// Original adaptation of NRC Select_ConnectedEntities, Select_AllOfType and their scene walkers,
// radiant/select.cpp at 449778b3840f8f2062daab15c87cb012f2ac945c (GPL-2.0-or-later).
// VibeStudio's closed groups, locks, selection history and worker cancellation
// use the shared studio model. See docs/CREDITS.md for attribution and review.
namespace vibestudio::detail {
namespace {
struct Text { Q_DECLARE_TR_FUNCTIONS(LevelEntitySelection) };
struct Child { LevelMapSelectionRef ref; int line = 0; };
QString key(const LevelMapSelectionRef& ref) { return levelMapSelectionRefId(ref); }
LevelMapSelectionRef entityRef(int id) { return {LevelMapSelectionKind::Entity,id}; }
QString property(const LevelMapEntity& entity, const char* name)
{
	for (const auto& p : entity.properties) { placementCancellationCheckpoint(); if (p.key == QLatin1String(name)) { return p.value; } }
	return {};
}
}
bool selectLevelMapEntityQuery(LevelMapDocument* document, const LevelObjectSelectionRequest& request,
	LevelObjectSelectionReport* report, QString* error)
{
	if (error) { error->clear(); }
	const auto fail = [&](const QString& message) { if (error) { *error = message; } return false; };
	const bool byType = request.mode == LevelObjectSelectionMode::RadiantType;
	if (!document || (document->format != LevelMapFormat::QuakeMap && document->format != LevelMapFormat::Quake3Map)) {
		return fail(Text::tr("Entity selection requires a Quake-family text map."));
	}
	if (!request.lines.isEmpty()) { return fail(Text::tr("Entity selection does not accept source line numbers.")); }
	if ((!byType && (request.material || request.typeScope != LevelTypeSelectionScope::Objects))
		|| (byType && request.typeScope != LevelTypeSelectionScope::Objects && request.typeScope != LevelTypeSelectionScope::Vertices
			&& request.typeScope != LevelTypeSelectionScope::Edges && request.typeScope != LevelTypeSelectionScope::Faces)) {
		return fail(Text::tr("Choose a valid selection scope; material and component scope are only valid for Select All of Type."));
	}
	if (!document->scene.opaqueMetadata.isEmpty() || !document->scene.problem.isEmpty()) {
		return fail(Text::tr("Resolve the map's unrecognised scene metadata before selecting entities."));
	}
	placementCheckpoint(LevelPlacementPhase::SelectingObjects);
	const auto context = levelSceneSelectionContext(*document);
	auto excluded = levelMapHiddenObjects(*document) | levelSceneLockedObjects(*document) | request.excludedObjects;
	QHash<int,const LevelMapEntity*> entities;
	QHash<QString,int> owners;
	QHash<int,QVector<Child>> children;
	QSet<int> world;
	for (const auto& entity : document->entities) {
		placementCancellationCheckpoint(); entities.insert(entity.id,&entity); owners.insert(key(entityRef(entity.id)),entity.id);
		if (entity.className.compare(QLatin1String("worldspawn"),Qt::CaseInsensitive) == 0) { world.insert(entity.id); }
	}
	const auto appendChildren = [&](const auto& records, LevelMapSelectionKind kind) {
		for (const auto& child : records) {
			placementCancellationCheckpoint(); const LevelMapSelectionRef ref{kind,child.id};
			owners.insert(key(ref),child.entityId); children[child.entityId].append({ref,child.startLine});
			if (excluded.contains(key(entityRef(child.entityId)))) { excluded.insert(key(ref)); }
		}
	};
	appendChildren(document->brushes,LevelMapSelectionKind::QuakeBrush);
	appendChildren(document->patches,LevelMapSelectionKind::QuakePatch);
	QSet<QString> targetNames, sourceNames, original;
	QSet<int> sourceOwners, selectedEntities;
	for (const auto& ref : document->selection) {
		placementCancellationCheckpoint(); const auto object = key(ref);
		const auto* entity = owners.contains(object) ? entities.value(owners.value(object)) : nullptr;
		if (!entity || !context.allowedObjects.contains(object) || original.contains(object)) {
			return fail(Text::tr("A selected object is missing, repeated or outside the open group: %1.").arg(object));
		}
		original.insert(object);
		if (ref.kind == LevelMapSelectionKind::Entity) { selectedEntities.insert(ref.objectId); }
		if (world.contains(entity->id) || sourceOwners.contains(entity->id)) { continue; }
		sourceOwners.insert(entity->id);
		if (!byType) {
			const auto target = property(*entity,"target"), name = property(*entity,"targetname");
			if (!target.isEmpty()) { targetNames.insert(target); }
			if (!name.isEmpty()) { sourceNames.insert(name); }
		}
	}
	LevelObjectSelectionReport result;
	if (byType && (request.typeScope == LevelTypeSelectionScope::Vertices || request.typeScope == LevelTypeSelectionScope::Edges)) {
		result.selection = document->selection;
		placementCheckpoint(LevelPlacementPhase::Finalizing,1,1); if (report) { *report = std::move(result); } return true;
	}
	if (byType && request.typeScope == LevelTypeSelectionScope::Objects) {
		for (const auto& entity : document->entities) {
			placementCancellationCheckpoint(); if (!sourceOwners.contains(entity.id)) { continue; }
			const auto name = property(entity,"classname");
			if (!sourceNames.contains(name)) { sourceNames.insert(name); result.matchedClasses.append(name); }
		}
		result.materialFallback = sourceNames.isEmpty();
	}
	if (byType && (result.materialFallback || request.typeScope == LevelTypeSelectionScope::Faces) && !request.material) {
		return fail(Text::tr("Choose a material when selecting faces or when no non-world entity class is selected."));
	}
	// Preserve original picks, including temporarily hidden picks. NRC only
	// filters the new result traversal; names are gathered before adding hits.
	QVector<LevelMapSelectionRef> selection = byType ? QVector<LevelMapSelectionRef>{} : document->selection;
	QSet<QString> selected = byType ? QSet<QString>{} : original, blockedGroups;
	const auto permitted = [&](const LevelMapSelectionRef& ref) {
		const auto object = key(ref);
		return owners.contains(object) && entities.contains(owners.value(object)) && context.allowedObjects.contains(object)
			&& !excluded.contains(object) && !(ref.kind == LevelMapSelectionKind::Entity && world.contains(ref.objectId));
	};
	const auto sameMaterial = [&](const QString& name) { return request.material && name.compare(*request.material,Qt::CaseInsensitive) == 0; };
	if (byType && request.typeScope == LevelTypeSelectionScope::Faces) {
		result.selection = document->selection; result.componentSelectionChanged = true;
		qint64 completed = 0;
		for (const auto& brush : document->brushes) {
			placementCheckpoint(LevelPlacementPhase::SelectingObjects,completed++,document->brushes.size());
			const LevelMapSelectionRef ref{LevelMapSelectionKind::QuakeBrush,brush.id};
			if ((!original.contains(key(ref)) && !selectedEntities.contains(brush.entityId))
				|| !permitted(ref) || context.closedGroups.contains(key(ref))) { continue; }
			QVector<int> faces;
			for (int i = 0; i < brush.faces.size(); ++i) {
				placementCancellationCheckpoint(); if (sameMaterial(brush.faces[i].textureName)) { faces.append(i); }
			}
			if (faces.isEmpty()) { continue; }
			LevelBrushTopology topology; QString problem;
			if (!levelBrushTopology(brush,&topology,&problem) || topology.faces.size() != brush.faces.size()) {
				return fail(Text::tr("Cannot select matching faces of brush %1: %2").arg(brush.id).arg(problem));
			}
			result.components.append({ref,LevelBrushComponent::Face,faces});
		}
		placementCheckpoint(LevelPlacementPhase::Finalizing,1,1); if (report) { *report = std::move(result); } return true;
	}
	const auto completeOwner = [&](int id, const QString& group) {
		for (const auto& child : children.value(id)) {
			placementCancellationCheckpoint();
			if (!permitted(child.ref) || context.closedGroups.value(key(child.ref)) != group) { return false; }
		}
		return true;
	};
	for (auto it = context.groupSelections.cbegin(); it != context.groupSelections.cend(); ++it) {
		for (const auto& ref : it.value()) {
			placementCancellationCheckpoint();
			if (!permitted(ref) || (ref.kind == LevelMapSelectionKind::Entity && !completeOwner(ref.objectId,it.key()))) {
				blockedGroups.insert(it.key()); break;
			}
		}
	}
	const auto append = [&](const LevelMapSelectionRef& ref) { const auto object = key(ref); if (!selected.contains(object)) { selected.insert(object); selection.append(ref); } };
	QSet<QString> visitedGroups;
	const auto add = [&](const LevelMapSelectionRef& ref) {
		if (!permitted(ref)) { return; }
		const auto group = context.closedGroups.value(key(ref));
		if (!group.isEmpty()) {
			if (blockedGroups.contains(group) || visitedGroups.contains(group)) { return; }
			visitedGroups.insert(group); for (const auto& member : context.groupSelections.value(group)) { append(member); }
		} else if (ref.kind != LevelMapSelectionKind::Entity || completeOwner(ref.objectId,{})) { append(ref); }
	};
	const auto addEntity = [&](const LevelMapEntity& entity) {
		add(entityRef(entity.id)); auto owned = children.value(entity.id);
		// Preserve mixed primitive order from loaded source; new records follow
		// loaded records in stable native-array order when no source span exists.
		std::stable_sort(owned.begin(),owned.end(),[](const auto& a,const auto& b) {
			if (a.line <= 0 || b.line <= 0) { return a.line > 0 && b.line <= 0; } return a.line < b.line;
		});
		for (const auto& child : owned) { placementCancellationCheckpoint(); add(child.ref); }
	};
	qint64 completed = 0;
	if (byType && result.materialFallback) {
		const auto total = document->brushes.size()+document->patches.size();
		for (const auto& brush : document->brushes) {
			placementCheckpoint(LevelPlacementPhase::SelectingObjects,completed++,total);
			for (const auto& face : brush.faces) {
				placementCancellationCheckpoint();
				if (sameMaterial(face.textureName)) { add({LevelMapSelectionKind::QuakeBrush,brush.id}); break; }
			}
		}
		for (const auto& patch : document->patches) {
			placementCheckpoint(LevelPlacementPhase::SelectingObjects,completed++,total);
			if (sameMaterial(patch.textureName)) { add({LevelMapSelectionKind::QuakePatch,patch.id}); }
		}
	} else if (byType) {
		for (const auto& entity : document->entities) {
			placementCheckpoint(LevelPlacementPhase::SelectingObjects,completed++,document->entities.size());
			if (!world.contains(entity.id) && !excluded.contains(key(entityRef(entity.id))) && sourceNames.contains(property(entity,"classname"))) { addEntity(entity); }
		}
	} else {
		QSet<int> visitedEntities;
		for (bool incoming : {true,false}) {
			for (const auto& entity : document->entities) {
				placementCheckpoint(LevelPlacementPhase::SelectingObjects,completed++,document->entities.size()*2);
				if (world.contains(entity.id) || visitedEntities.contains(entity.id) || excluded.contains(key(entityRef(entity.id)))) { continue; }
				const auto name = property(entity,incoming ? "target" : "targetname");
				if (name.isEmpty() || !(incoming ? sourceNames : targetNames).contains(name)) { continue; }
				visitedEntities.insert(entity.id); addEntity(entity);
			}
		}
	}
	LevelMapDocument candidate = *document;
	if (!setLevelMapSelection(&candidate,selection,error)) { return false; }
	if (request.undoSelection) {
		recordLevelMapSelectionHistory(&candidate,document->selection,
			{document->openSceneGroups,document->activeSceneNode,document->sceneCreationBeforeOpen},
			byType ? Text::tr("Select All of Type") : Text::tr("Select Connected Entities"));
	}
	placementCheckpoint(LevelPlacementPhase::Finalizing,1,1);
	result.selection = std::move(selection);
	*document = std::move(candidate); if (report) { *report = std::move(result); } return true;
}
} // namespace vibestudio::detail
