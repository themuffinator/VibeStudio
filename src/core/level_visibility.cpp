#include "core/level_visibility.h"
#include "core/level_history_p.h"
#include "core/level_placement_control_p.h"
#include "core/level_scene.h"
#include "core/level_scene_layers.h"
#include <QCoreApplication>

namespace vibestudio {
namespace {
struct Text { Q_DECLARE_TR_FUNCTIONS(LevelVisibility) };
QString key(LevelMapSelectionKind kind, int id) { return levelMapSelectionRefId({kind,id}); }
QSet<QString> objectSet(const LevelMapDocument& document)
{
	const auto objects = levelSceneObjects(document);
	return {objects.cbegin(),objects.cend()};
}
// Original hierarchy resolver, informed by TB Node::visible/ensureVisible
// (4c756be1, GPL-3.0-or-later). Saved scene hiding remains a separate mask.
QString groupKey(const QString& id) { return QStringLiteral("group:") + id; }
struct VisibilityGraph {
	QHash<QString,QStringList> parents, children;
	QSet<QString> objects, targets;
	explicit VisibilityGraph(const LevelMapDocument& document) : objects(objectSet(document)), targets(objects) {
		const auto link = [&](const QString& child, const QString& parent) {
			if (parent.isEmpty() || parents.value(child).contains(parent)) { return; }
			parents[child].append(parent); children[parent].append(child);
		};
		for (const auto& node : document.scene.nodes) {
			const auto id = groupKey(node.id);
			if (node.kind == LevelSceneNodeKind::Group) { targets.insert(id); }
			if (!node.parentId.isEmpty()) { link(id,groupKey(node.parentId)); }
			for (const auto& object : node.objects) { link(object,id); }
		}
		const auto owners = [&](const auto& records, LevelMapSelectionKind kind) {
			for (const auto& record : records) {
				const auto owner = key(LevelMapSelectionKind::Entity,record.entityId);
				if (objects.contains(owner)) { link(key(kind,record.id),owner); }
			}
		};
		owners(document.brushes,LevelMapSelectionKind::QuakeBrush);
		owners(document.patches,LevelMapSelectionKind::QuakePatch);
	}
	QSet<QString> closure(QSet<QString> initial, bool ancestors) const {
		const auto& edges = ancestors ? parents : children;
		QVector<QString> pending(initial.cbegin(),initial.cend());
		for (qsizetype i = 0; i < pending.size(); ++i) {
			detail::placementCancellationCheckpoint();
			for (const auto& next : edges.value(pending[i])) {
				if (!initial.contains(next)) { initial.insert(next); pending.append(next); }
			}
		}
		return initial;
	}
	QSet<QString> hidden(const LevelMapDocument& document) const {
		QHash<QString,bool> cached; QSet<QString> visiting;
		const auto invisible = [&](auto&& self, const QString& id) -> bool {
			if (document.hiddenObjects.contains(id)) { return true; }
			if (document.shownObjects.contains(id)) { return false; }
			const auto known = cached.constFind(id); if (known != cached.cend()) { return *known; }
			if (visiting.contains(id)) { return true; } // Fail closed for invalid caller metadata.
			visiting.insert(id); bool result = false;
			for (const auto& parent : parents.value(id)) { if (self(self,parent)) { result = true; break; } }
			visiting.remove(id); cached.insert(id,result); return result;
		};
		QSet<QString> result;
		for (const auto& id : targets) {
			detail::placementCancellationCheckpoint();
			if (invisible(invisible,id)) { result.insert(id); }
		}
		return result;
	}
};
QSet<QString> selectionKeys(const LevelMapDocument& document, const QVector<LevelMapSelectionRef>& selection)
{
	QSet<QString> result;
	for (const auto& ref : selection) { result.insert(levelSceneCanonicalObject(document,levelMapSelectionRefId(ref))); }
	return result;
}
// A visible selected child requires its owner to be visible, but does not
// force its siblings visible. Selecting the owner includes all its children.
void includeOwnersAndChildren(const LevelMapDocument& document, QSet<QString>* selected)
{
	const auto original = *selected;
	const auto append = [&](const auto& records, LevelMapSelectionKind kind) {
		for (const auto& record : records) {
			detail::placementCancellationCheckpoint();
			const auto child = key(kind,record.id), owner = key(LevelMapSelectionKind::Entity,record.entityId);
			if (original.contains(owner)) { selected->insert(child); }
			if (original.contains(child)) { selected->insert(owner); }
		}
	};
	append(document.brushes,LevelMapSelectionKind::QuakeBrush);
	append(document.patches,LevelMapSelectionKind::QuakePatch);
	if (document.format != LevelMapFormat::DoomWad) { return; }
	QHash<int,int> sectors;
	for (const auto& side : document.doomSidedefs) { sectors.insert(side.id,side.sector); }
	for (const auto& line : document.doomLinedefs) {
		detail::placementCancellationCheckpoint();
		bool keep = original.contains(key(LevelMapSelectionKind::DoomLinedef,line.id))
			|| original.contains(key(LevelMapSelectionKind::DoomVertex,line.startVertex))
			|| original.contains(key(LevelMapSelectionKind::DoomVertex,line.endVertex));
		for (int side : {line.frontSidedef,line.backSidedef}) {
			keep |= sectors.contains(side) && original.contains(key(LevelMapSelectionKind::DoomSector,sectors.value(side)));
		}
		if (!keep) { continue; }
		selected->insert(key(LevelMapSelectionKind::DoomLinedef,line.id));
		selected->insert(key(LevelMapSelectionKind::DoomVertex,line.startVertex));
		selected->insert(key(LevelMapSelectionKind::DoomVertex,line.endVertex));
		for (int side : {line.frontSidedef,line.backSidedef}) {
			if (sectors.contains(side)) { selected->insert(key(LevelMapSelectionKind::DoomSector,sectors.value(side))); }
		}
	}
}
}
QSet<QString> levelMapVisibilityTargets(const LevelMapDocument& document)
{
	auto result = objectSet(document);
	for (const auto& node : document.scene.nodes) {
		if (node.kind == LevelSceneNodeKind::Group) { result.insert(groupKey(node.id)); }
	}
	return result;
}
void setLevelMapInsertionVisibility(LevelMapDocument* document, LevelInsertionVisibility policy)
{
	if (document && document->insertionVisibility != policy) {
		document->insertionVisibility = policy; ++document->historyRevision;
	}
}
QSet<QString> levelMapHiddenObjects(const LevelMapDocument& document)
{
	QSet<QString> temporary;
	if (!document.hiddenObjects.isEmpty()) {
		const VisibilityGraph graph(document); temporary = graph.hidden(document) & graph.objects;
		// Doom sectors/lines/vertices have rendering dependencies, not a native
		// entity tree; retain their established closure after resolving groups.
		if (document.format == LevelMapFormat::DoomWad) { temporary = expandLevelSceneHiddenObjects(document,std::move(temporary)); }
	}
	auto hidden = levelSceneHiddenObjects(document) | temporary;
	// NRC suppresses a brush-entity label when every child is hidden. Do not
	// store this derived state: newly created children must remain visible.
	QSet<int> owners, visibleOwners;
	const auto collect = [&](const auto& records, LevelMapSelectionKind kind) {
		for (const auto& record : records) {
			owners.insert(record.entityId);
			if (!hidden.contains(key(kind,record.id))) { visibleOwners.insert(record.entityId); }
		}
	};
	if (!hidden.isEmpty()) {
		collect(document.brushes,LevelMapSelectionKind::QuakeBrush); collect(document.patches,LevelMapSelectionKind::QuakePatch);
		for (const auto& entity : document.entities) {
			if (owners.contains(entity.id) && !visibleOwners.contains(entity.id) && entity.className.compare(QLatin1String("worldspawn"),Qt::CaseInsensitive) != 0) {
				hidden.insert(key(LevelMapSelectionKind::Entity,entity.id));
			}
		}
	}
	return hidden;
}

// Original implementation informed by TB v2026.2 Map_NodeVisibility and
// SetVisibilityCommand (GPL-3.0-or-later), and NRC 449778b radiant/select.cpp
// (GPL-2.0-or-later). Behavioural references and limits are in CREDITS.md.
bool changeLevelMapVisibility(LevelMapDocument* document, const LevelVisibilityRequest& request,
	LevelVisibilityReport* report, QString* error)
{
	if (error) { error->clear(); }
	const auto fail = [&](const QString& message) { if (error) { *error = message; } return false; };
	if (!document || document->format == LevelMapFormat::Unknown) { return fail(Text::tr("Open a map before changing visibility.")); }
	if (request.operation != LevelVisibilityOperation::HideSelected && request.operation != LevelVisibilityOperation::ShowAll
		&& request.operation != LevelVisibilityOperation::Isolate) { return fail(Text::tr("Unknown visibility operation.")); }
	detail::placementCheckpoint(LevelPlacementPhase::Visibility);
	auto candidate = *document;
	QString description;
	if (request.operation == LevelVisibilityOperation::ShowAll) {
		candidate.hiddenObjects.clear(); candidate.shownObjects.clear(); description = Text::tr("Show all temporarily hidden objects");
	} else {
		if (document->selection.isEmpty()) { return fail(Text::tr("Select objects before hiding or isolating them.")); }
		if (!document->scene.problem.isEmpty() || !document->scene.opaqueMetadata.isEmpty()) {
			return fail(Text::tr("Resolve the map's unrecognised scene metadata before hiding or isolating objects."));
		}
		const auto available = objectSet(*document);
		const auto context = levelSceneSelectionContext(*document);
		const auto selection = pickLevelSceneObjects(context,{},document->selection,LevelScenePick::Replace);
		const auto requested = selectionKeys(*document,document->selection);
		for (const auto& object : requested) {
			if (!available.contains(object) || !context.allowedObjects.contains(object)) {
				return fail(Text::tr("The selection contains a missing object, worldspawn or an object outside the open group."));
			}
		}
		auto selected = selectionKeys(*document,selection);
		if (request.operation == LevelVisibilityOperation::HideSelected) {
			const VisibilityGraph graph(*document);
			if (request.inheritGroups) {
				QSet<QString> targets;
				for (const auto& object : selected) {
					const auto group = context.closedGroups.value(object);
					targets.insert(group.isEmpty() ? object : groupKey(group));
				}
				selected = std::move(targets);
			}
			// Hiding a container resets forced-shown descendants, preserving
			// independent Hidden states for a later reveal of that container.
			candidate.shownObjects.subtract(graph.closure(selected,false));
			candidate.hiddenObjects.unite(selected);
			setLevelMapSelection(&candidate,{}); description = Text::tr("Hide selected objects");
		} else {
			includeOwnersAndChildren(*document,&selected);
			// Isolate the whole scene, including locked, filtered and out-of-group
			// objects. Inverting the selectable set would leave those visible.
			const VisibilityGraph graph(*document);
			const auto targets = request.inheritGroups ? graph.targets : available;
			if (request.inheritGroups) {
				QSet<QString> groups;
				for (const auto& ref : selection) {
					const auto group = context.closedGroups.value(levelSceneCanonicalObject(*document,levelMapSelectionRefId(ref)));
					if (!group.isEmpty()) { groups.insert(groupKey(group)); }
				}
				selected.unite(graph.closure(groups,false));
				selected = graph.closure(selected,true) & targets;
			}
			candidate.hiddenObjects.clear(); candidate.shownObjects.clear(); qint64 done = 0;
			for (const auto& object : targets) {
				detail::placementCheckpoint(LevelPlacementPhase::Visibility,done++,targets.size());
				if (!selected.contains(object)) { candidate.hiddenObjects.insert(object); }
				else if (request.inheritGroups) { candidate.shownObjects.insert(object); }
			}
			setLevelMapSelection(&candidate,selection); description = Text::tr("Isolate selected objects");
		}
	}
	LevelVisibilityReport next;
	next.hidden = static_cast<int>((candidate.hiddenObjects - document->hiddenObjects).size());
	next.shown = static_cast<int>((document->hiddenObjects - candidate.hiddenObjects).size());
	next.changed = candidate.hiddenObjects != document->hiddenObjects || candidate.shownObjects != document->shownObjects || candidate.selection != document->selection;
	if (next.changed) {
		if (request.undoable) { detail::recordLevelMapVisibilityHistory(&candidate,*document,description); }
		else { ++candidate.historyRevision; }
	}
	detail::placementCheckpoint(LevelPlacementPhase::Finalizing);
	*document = std::move(candidate); if (report) { *report = next; }
	return true;
}

bool selectAndRevealLevelMapObjects(LevelMapDocument* document, const QVector<LevelMapSelectionRef>& selection, bool undoable, QString* error)
{
	if (error) { error->clear(); }
	if (!document || document->format == LevelMapFormat::Unknown) {
		if (error) { *error = Text::tr("Open a map before selecting objects."); } return false;
	}
	auto candidate = *document;
	if (!setLevelMapSelection(&candidate,selection,error)) { return false; }
	if (!candidate.hiddenObjects.isEmpty()) {
		auto reveal = selectionKeys(candidate,candidate.selection);
		if (candidate.format == LevelMapFormat::DoomWad) {
			QHash<int,int> sectors;
			for (const auto& side : candidate.doomSidedefs) { sectors.insert(side.id,side.sector); }
			const auto picked = reveal;
			for (const auto& line : candidate.doomLinedefs) {
				if (!picked.contains(key(LevelMapSelectionKind::DoomLinedef,line.id))
					&& !picked.contains(key(LevelMapSelectionKind::DoomVertex,line.startVertex))
					&& !picked.contains(key(LevelMapSelectionKind::DoomVertex,line.endVertex))) { continue; }
				reveal.insert(key(LevelMapSelectionKind::DoomLinedef,line.id));
				for (int side : {line.frontSidedef,line.backSidedef}) {
					if (sectors.contains(side)) { reveal.insert(key(LevelMapSelectionKind::DoomSector,sectors.value(side))); }
				}
			}
		}
		candidate.hiddenObjects.subtract(reveal);
		const auto inherited = VisibilityGraph(candidate).hidden(candidate);
		candidate.shownObjects.unite(reveal & inherited);
	}
	if (candidate.hiddenObjects != document->hiddenObjects || candidate.shownObjects != document->shownObjects) {
		if (undoable) { detail::recordLevelMapVisibilityHistory(&candidate,*document,Text::tr("Select and reveal objects")); }
		else { ++candidate.historyRevision; }
	} else if (undoable) {
		detail::recordLevelMapSelectionHistory(&candidate,document->selection,
			{document->openSceneGroups,document->activeSceneNode,document->sceneCreationBeforeOpen},Text::tr("Change object selection"));
	}
	*document = std::move(candidate); return true;
}

namespace {
// Layer membership follows explicit primitive assignments before owner
// membership, matching VibeStudio's selection and scene editing context.
QHash<QString,QString> visibilityLayers(const LevelMapDocument& document, const LevelSceneState& scene)
{
	// Shared with the layer commands (core/level_scene_layers.h).
	auto result = levelSceneObjectLayers(document,scene);
	const auto layers = levelSceneNodeLayers(scene);
	for (const auto& node : scene.nodes) {
		if (node.kind == LevelSceneNodeKind::Group) { result.insert(groupKey(node.id),layers.value(node.id)); }
	}
	return result;
}
}

// TB Map_Nodes::reparentNodes downgrades Shown descendants when their
// containing layer changes (4c756be1, GPL-3.0-or-later; see CREDITS.md).
void detail::prepareLevelMapSceneVisibility(const LevelMapDocument& before, LevelMapUndoCommand* command)
{
	QHash<QString,const LevelSceneNode*> oldNodes;
	for (const auto& node : before.scene.nodes) { oldNodes.insert(node.id,&node); }
	bool topologyChanged = oldNodes.size() != command->sceneAfter.nodes.size();
	for (const auto& node : command->sceneAfter.nodes) {
		const auto* old = oldNodes.value(node.id);
		topologyChanged |= !old || old->parentId != node.parentId || old->kind != node.kind || old->objects != node.objects;
	}
	if (!topologyChanged) { return; }
	const auto old = visibilityLayers(before,before.scene), next = visibilityLayers(before,command->sceneAfter);
	for (auto it = next.cbegin(); it != next.cend(); ++it) {
		if (old.contains(it.key()) && old.value(it.key()) != it.value()) { command->visibilityLayerTransfers.insert(it.key()); }
	}
}
void detail::recordLevelMapCopiedVisibility(const LevelMapDocument& source, const QHash<QString,QString>& newToSource, LevelMapUndoCommand* command)
{
	if (source.insertionVisibility == LevelInsertionVisibility::FreshInherited) { return; }
	for (auto it = newToSource.cbegin(); it != newToSource.cend(); ++it) {
		detail::placementCancellationCheckpoint();
		if (source.hiddenObjects.contains(it.value())) { command->initialHiddenObjects.insert(it.key()); }
		else if (source.shownObjects.contains(it.value())) { command->initialShownObjects.insert(it.key()); }
	}
}

void detail::reconcileLevelMapVisibility(LevelMapDocument* document, LevelMapUndoCommand* command)
{
	command->remapSessionVisibility = true;
	if (document->hiddenObjects.isEmpty() && document->shownObjects.isEmpty()
		&& command->initialHiddenObjects.isEmpty() && command->initialShownObjects.isEmpty()) { return; }
	const auto available = levelMapVisibilityTargets(*document);
	const auto remap = [&](QSet<QString>* state, QSet<QString>* removed) {
		QSet<QString> carried;
		for (const auto& old : std::as_const(*state)) {
			const auto now = command->sceneObjectRemap.value(old,old);
			if (available.contains(now)) { carried.insert(now); } else { removed->insert(old); }
		}
		*state = std::move(carried);
	};
	remap(&document->hiddenObjects,&command->removedHiddenObjects);
	remap(&document->shownObjects,&command->removedShownObjects);
	// NRC Node/BrushNode/PatchNode/entity copy constructors start with fresh
	// exclusion flags (449778b, GPL-2.0-or-later; see CREDITS.md). Existing
	// identities still remap, but new objects neither clone states nor force
	// visibility through a hidden parent. Linked replacement always inherits.
	if (command->inheritInsertedVisibility || document->insertionVisibility == LevelInsertionVisibility::FreshInherited) { return; }
	document->hiddenObjects.unite(command->initialHiddenObjects & available);
	document->shownObjects.unite(command->initialShownObjects & available);
	QSet<QString> added(command->sceneAddedObjects.cbegin(),command->sceneAddedObjects.cend());
	if (command->hasSceneSnapshot) {
		QSet<QString> oldNodes;
		for (const auto& node : command->sceneBefore.nodes) { oldNodes.insert(node.id); }
		for (const auto& node : command->sceneAfter.nodes) {
			if (!oldNodes.contains(node.id) && node.kind == LevelSceneNodeKind::Group) { added.insert(groupKey(node.id)); }
		}
	}
	added.intersect(available);
	if (added.isEmpty()) { return; }
	const VisibilityGraph graph(*document);
	auto roots = added;
	for (const auto& object : added) {
		for (const auto& parent : graph.parents.value(object)) { if (added.contains(parent)) { roots.remove(object); break; } }
	}
	// TB clones attributes recursively, then ensures only inserted roots are
	// visible. Hidden descendants retain their own states and remain hidden.
	const auto hidden = graph.hidden(*document);
	const auto shownRoots = roots & hidden;
	document->hiddenObjects.subtract(shownRoots); document->shownObjects.unite(shownRoots);
	// Explicit primitive membership can add a second parent in VibeStudio.
	// Reveal external-parent hiding without erasing copied Hidden descendants.
	const auto protectedHidden = graph.closure(document->hiddenObjects & added,false);
	document->shownObjects.unite((graph.hidden(*document) & added) - protectedHidden);

}
void detail::replayLevelMapVisibilityRemap(LevelMapDocument* document, const LevelMapUndoCommand& command, bool forward)
{
	if (!command.visibilityLayerTransfers.isEmpty()) {
		if (forward) {
			command.shownBeforeLayerTransfer = document->shownObjects & command.visibilityLayerTransfers;
			document->shownObjects.subtract(command.visibilityLayerTransfers);
		} else {
			// A later transient Hide wins over restoring an earlier shown state.
			document->shownObjects.unite(command.shownBeforeLayerTransfer - document->hiddenObjects);
		}
	}
	if (!command.remapSessionVisibility) { return; }
	QHash<QString,QString> remap = command.sceneObjectRemap;
	if (!forward) {
		remap.clear();
		for (auto it = command.sceneObjectRemap.cbegin(); it != command.sceneObjectRemap.cend(); ++it) {
			if (!it.value().isEmpty()) { remap.insert(it.value(),it.key()); }
		}
	}
	const auto available = levelMapVisibilityTargets(*document);
	const auto carry = [&](QSet<QString>* state, QSet<QString>& removed, const QSet<QString>& restored) {
		removed.clear(); QSet<QString> carried;
		for (const auto& old : std::as_const(*state)) {
			// Reused numeric ids must not inherit the removed object's state.
			if (!forward && (command.sceneAddedObjects.contains(old)
				|| (command.sceneObjectRemap.contains(old) && !remap.contains(old)))) { removed.insert(old); continue; }
			const auto now = remap.value(old,old);
			if (available.contains(now)) { carried.insert(now); } else { removed.insert(old); }
		}
		carried.unite(restored & available); *state = std::move(carried);
	};
	carry(&document->hiddenObjects,forward ? command.removedHiddenObjects : command.hiddenObjectsRemovedOnUndo,
		forward ? command.hiddenObjectsRemovedOnUndo : command.removedHiddenObjects);
	carry(&document->shownObjects,forward ? command.removedShownObjects : command.shownObjectsRemovedOnUndo,
		forward ? command.shownObjectsRemovedOnUndo : command.removedShownObjects);
}
} // namespace vibestudio
