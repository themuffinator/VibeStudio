#include "core/level_scene_layers.h"

#include "core/level_history_p.h"
#include "core/level_placement_control_p.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_visibility.h"

#include <QCoreApplication>

#include <algorithm>
#include <utility>

// Workflow reference: NRC radiant/layerswindow.cpp Scene_ExpandSelectionToLayers,
// context_moveto_menu and Layers_registerCommands at 449778b3 (GPL-2.0-or-later),
// and TB MapViewBase's Move to Layer menu at 4c756be1 (GPL-3.0-or-later).
// Original Qt implementation over VibeStudio's scene tree; see docs/CREDITS.md.

namespace vibestudio {
namespace {
struct Text {
	Q_DECLARE_TR_FUNCTIONS(LevelSceneLayers)
};

QString objectKey(LevelMapSelectionKind kind, int id) { return levelMapSelectionRefId({kind, id}); }

LevelMapSelectionRef reference(const QString& object)
{
	const auto colon = object.indexOf(QLatin1Char(':'));
	return {levelMapSelectionKindFromId(object.left(colon)), object.mid(colon + 1).toInt()};
}

bool sceneRecognised(const LevelMapDocument& document)
{
	return document.scene.opaqueMetadata.isEmpty() && document.scene.problem.isEmpty();
}

// Each node's containing layer, resolved once per scene.
QHash<QString, QString> nodeLayers(const LevelSceneState& scene)
{
	QHash<QString, const LevelSceneNode*> nodes;
	for (const auto& node : scene.nodes) {
		nodes.insert(node.id, &node);
	}
	QHash<QString, QString> result;
	for (const auto& node : scene.nodes) {
		QString at = node.id, layer;
		for (int depth = 0; !at.isEmpty() && depth <= kLevelSceneMaxDepth; ++depth) {
			const auto* current = nodes.value(at);
			if (!current) {
				break;
			}
			if (current->kind == LevelSceneNodeKind::Layer) {
				layer = current->id;
				break;
			}
			at = current->parentId;
		}
		result.insert(node.id, layer);
	}
	return result;
}

// Brushes and patches of non-worldspawn entities, keyed by primitive.
QHash<QString, QString> primitiveOwners(const LevelMapDocument& document)
{
	QSet<int> world;
	for (const auto& entity : document.entities) {
		if (entity.className.compare(QLatin1String("worldspawn"), Qt::CaseInsensitive) == 0) {
			world.insert(entity.id);
		}
	}
	QHash<QString, QString> owners;
	for (const auto& brush : document.brushes) {
		if (!world.contains(brush.entityId)) {
			owners.insert(objectKey(LevelMapSelectionKind::QuakeBrush, brush.id), objectKey(LevelMapSelectionKind::Entity, brush.entityId));
		}
	}
	for (const auto& patch : document.patches) {
		if (!world.contains(patch.entityId)) {
			owners.insert(objectKey(LevelMapSelectionKind::QuakePatch, patch.id), objectKey(LevelMapSelectionKind::Entity, patch.entityId));
		}
	}
	return owners;
}

QString layerName(const LevelMapDocument& document, const QString& layerId)
{
	if (layerId.isEmpty()) {
		return Text::tr("Default");
	}
	const auto* node = levelSceneNode(document.scene, layerId);
	return node ? node->name : layerId;
}

struct MovePlan {
	LevelSceneState state;
	LevelSceneLayerMoveReport report;
};

// What the selection moves, worked out once for any number of layers.
struct MoveUnits {
	QStringList assign, release, groups;
	int worldspawn = 0;
	QHash<QString, QString> membership;
	QSet<QString> lockedNodes, lockedObjects;
};

bool collectLayerMoveUnits(const LevelMapDocument& document, MoveUnits* units, QString* error)
{
	const auto fail = [&](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (document.format == LevelMapFormat::Unknown) {
		return fail(Text::tr("Open a map before moving objects to a layer."));
	}
	if (document.doomFormat == LevelMapDoomFormat::Udmf && !document.doomUdmf) {
		return fail(Text::tr("UDMF scene editing is not supported yet."));
	}
	if (!sceneRecognised(document)) {
		return fail(Text::tr("Reset the unrecognised or stale scene metadata before moving objects to a layer."));
	}
	if (!document.openSceneGroups.isEmpty()) {
		return fail(Text::tr("Close the open group before moving objects to a layer; layers hold whole groups."));
	}
	if (document.selection.isEmpty()) {
		return fail(Text::tr("Select objects to move to a layer."));
	}
	const auto all = levelSceneObjects(document);
	const QSet<QString> available(all.cbegin(), all.cend());
	const auto context = levelSceneSelectionContext(document);
	const auto owners = primitiveOwners(document);
	for (const auto& node : document.scene.nodes) {
		for (const auto& object : node.objects) {
			units->membership.insert(object, node.id);
		}
	}
	QSet<QString> seen;
	const auto add = [&seen](QStringList* list, const QString& value) {
		if (!seen.contains(value)) {
			seen.insert(value);
			list->append(value);
		}
	};
	for (const auto& ref : document.selection) {
		const auto key = levelSceneCanonicalObject(document, levelMapSelectionRefId(ref));
		if (!available.contains(key)) {
			if (ref.kind == LevelMapSelectionKind::Entity && levelMapObjectExists(document, ref)) {
				++units->worldspawn;
				continue;
			}
			return fail(Text::tr("The selection names an object that no longer exists: %1.").arg(key));
		}
		const auto group = context.closedGroups.value(key);
		if (!group.isEmpty()) {
			add(&units->groups, group);
			continue;
		}
		// TB moves a brush entity whole when one of its brushes is selected. Its
		// brushes then follow it unless assigned elsewhere on purpose.
		const auto owner = owners.value(key);
		if (!owner.isEmpty() && context.closedGroups.value(owner).isEmpty()) {
			add(&units->assign, owner);
			if (units->membership.contains(key)) {
				add(&units->release, key);
			}
			continue;
		}
		add(&units->assign, key);
	}
	units->lockedNodes = levelSceneLockedNodes(document.scene);
	units->lockedObjects = levelSceneLockedObjects(document);
	return true;
}

// The whole Move to Layer decision for one layer, shared by the command and by
// menus that enable one entry per layer.
bool planLayerMove(const LevelMapDocument& document, const MoveUnits& units, const QString& layerId, MovePlan* plan, QString* error)
{
	const auto fail = [&](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!layerId.isEmpty()) {
		const auto* node = levelSceneNode(document.scene, layerId);
		if (!node || node->kind != LevelSceneNodeKind::Layer) {
			return fail(Text::tr("Choose an existing layer, or Default."));
		}
	}
	if (units.lockedNodes.contains(layerId)) {
		return fail(Text::tr("Unlock layer %1 before moving objects into it.").arg(layerName(document, layerId)));
	}
	plan->report = {};
	plan->report.layer = layerId;
	plan->report.worldspawn = units.worldspawn;
	const auto locked = [&](const QString& name) {
		return fail(Text::tr("Unlock the layer or group protecting %1 before moving it to another layer.").arg(name));
	};
	QStringList movedObjects, releasedObjects, movedGroups;
	for (const auto& object : units.assign) {
		if (units.membership.value(object) == layerId) {
			++plan->report.alreadyThere;
			continue;
		}
		if (units.lockedObjects.contains(object)) {
			return locked(object);
		}
		movedObjects << object;
	}
	for (const auto& object : units.release) {
		if (units.membership.value(object) == layerId) {
			continue;
		}
		if (units.lockedObjects.contains(object)) {
			return locked(object);
		}
		releasedObjects << object;
	}
	for (const auto& group : units.groups) {
		const auto* node = levelSceneNode(document.scene, group);
		if (!node) {
			return fail(Text::tr("The scene node no longer exists."));
		}
		if (node->parentId == layerId) {
			++plan->report.alreadyThere;
			continue;
		}
		if (units.lockedNodes.contains(group)) {
			return locked(node->name);
		}
		movedGroups << group;
	}
	auto state = document.scene;
	QSet<QString> changed(movedObjects.cbegin(), movedObjects.cend());
	changed.unite(QSet<QString>(releasedObjects.cbegin(), releasedObjects.cend()));
	const QSet<QString> groups(movedGroups.cbegin(), movedGroups.cend());
	for (auto& node : state.nodes) {
		node.objects.removeIf([&](const QString& object) { return changed.contains(object); });
		if (groups.contains(node.id)) {
			node.parentId = layerId;
		}
		if (!layerId.isEmpty() && node.id == layerId) {
			QStringList ordered = movedObjects;
			ordered.sort();
			node.objects += ordered;
		}
	}
	plan->state = std::move(state);
	plan->report.objects = movedObjects;
	plan->report.released = releasedObjects;
	plan->report.groups = movedGroups;
	plan->report.changed = !movedObjects.isEmpty() || !releasedObjects.isEmpty() || !movedGroups.isEmpty();
	return true;
}

QString blockedReason(const LevelMapDocument& document, const MoveUnits& units, const QString& layerId)
{
	MovePlan plan;
	QString error;
	if (!planLayerMove(document, units, layerId, &plan, &error)) {
		return error;
	}
	return plan.report.changed ? QString() : Text::tr("The selection is already in %1.").arg(layerName(document, layerId));
}
} // namespace

QHash<QString, QString> levelSceneNodeLayers(const LevelSceneState& scene)
{
	return nodeLayers(scene);
}

QHash<QString, QString> levelSceneObjectLayers(const LevelMapDocument& document, const LevelSceneState& scene)
{
	const auto layers = nodeLayers(scene);
	QHash<QString, QString> membership;
	for (const auto& node : scene.nodes) {
		for (const auto& object : node.objects) {
			membership.insert(object, node.id);
		}
	}
	const auto owners = primitiveOwners(document);
	QHash<QString, QString> result;
	for (const auto& object : levelSceneObjects(document)) {
		detail::placementCancellationCheckpoint();
		// Explicit primitive membership comes before the owner's, matching the
		// selection context and scene visibility.
		const auto node = membership.value(object, membership.value(owners.value(object)));
		result.insert(object, layers.value(node));
	}
	return result;
}

bool expandLevelMapSelectionToLayers(LevelMapDocument* document, const LevelSceneLayerSelectionRequest& request,
	LevelSceneLayerSelectionReport* report, QString* error)
{
	if (error) {
		error->clear();
	}
	const auto fail = [&](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!document || document->format == LevelMapFormat::Unknown) {
		return fail(Text::tr("Open a map before expanding the selection to layers."));
	}
	if (!sceneRecognised(*document)) {
		return fail(Text::tr("Resolve the map's unrecognised scene metadata before expanding the selection to layers."));
	}
	if (document->selection.isEmpty()) {
		return fail(Text::tr("Select objects before expanding the selection to layers."));
	}
	detail::placementCheckpoint(LevelPlacementPhase::SelectingObjects);
	const auto layers = levelSceneObjectLayers(*document, document->scene);
	QSet<QString> chosen;
	for (const auto& ref : document->selection) {
		const auto found = layers.constFind(levelSceneCanonicalObject(*document, levelMapSelectionRefId(ref)));
		if (found != layers.cend()) {
			chosen.insert(*found);
		}
	}
	if (chosen.isEmpty()) {
		return fail(Text::tr("Worldspawn belongs to no layer. Select other objects to expand the selection to their layers."));
	}
	const auto context = levelSceneSelectionContext(*document);
	const auto owners = primitiveOwners(*document);
	QHash<QString, QStringList> children;
	for (auto it = owners.cbegin(); it != owners.cend(); ++it) {
		children[it.value()].append(it.key());
	}
	auto excluded = levelMapHiddenObjects(*document) | levelSceneLockedObjects(*document) | request.excludedObjects;
	for (auto it = owners.cbegin(); it != owners.cend(); ++it) {
		if (excluded.contains(it.value())) {
			excluded.insert(it.key());
		}
	}
	// A closed group is one selection unit, so one excluded member leaves the
	// whole group out. Its entity wrappers would also carry excluded or
	// separately assigned primitives along, as Select Siblings guards.
	QSet<QString> excludedGroups;
	for (const auto& object : std::as_const(excluded)) {
		const auto group = context.closedGroups.value(object);
		if (!group.isEmpty()) {
			excludedGroups.insert(group);
		}
	}
	for (auto it = context.groupSelections.cbegin(); it != context.groupSelections.cend(); ++it) {
		for (const auto& ref : it.value()) {
			for (const auto& child : children.value(levelMapSelectionRefId(ref))) {
				if (excluded.contains(child) || !context.allowedObjects.contains(child) || context.closedGroups.value(child) != it.key()) {
					excludedGroups.insert(it.key());
					break;
				}
			}
		}
	}
	const auto current = pickLevelSceneObjects(context, {}, document->selection, LevelScenePick::Replace);
	QSet<QString> selected;
	for (const auto& ref : current) {
		selected.insert(levelMapSelectionRefId(ref));
	}
	const auto objects = levelSceneObjects(*document);
	QVector<LevelMapSelectionRef> hits;
	QSet<QString> skipped;
	qint64 completed = 0;
	for (const auto& object : objects) {
		detail::placementCheckpoint(LevelPlacementPhase::SelectingObjects, completed++, objects.size());
		if (!chosen.contains(layers.value(object)) || !context.allowedObjects.contains(object)) {
			continue;
		}
		const auto group = context.closedGroups.value(object);
		if (!group.isEmpty() && excludedGroups.contains(group)) {
			skipped.insert(group);
			continue;
		}
		if (excluded.contains(object)) {
			continue;
		}
		if (group.isEmpty() && (children.contains(object) || selected.contains(owners.value(object)))) {
			// A brush entity counts through its brushes; a selected entity
			// already carries its brushes.
			continue;
		}
		hits.append(reference(object));
	}
	const auto selection = pickLevelSceneObjects(context, document->selection, hits, LevelScenePick::Add);
	LevelMapDocument candidate = *document;
	if (!setLevelMapSelection(&candidate, selection, error)) {
		return false;
	}
	if (request.undoSelection) {
		detail::recordLevelMapSelectionHistory(&candidate, document->selection,
			{document->openSceneGroups, document->activeSceneNode, document->sceneCreationBeforeOpen}, Text::tr("Expand Selection to Layers"));
	}
	detail::placementCheckpoint(LevelPlacementPhase::Finalizing, 1, 1);
	const int before = static_cast<int>(document->selection.size());
	*document = std::move(candidate);
	if (report) {
		*report = {};
		if (chosen.contains(QString())) {
			report->layers << QString();
		}
		for (const auto& node : document->scene.nodes) {
			if (node.kind == LevelSceneNodeKind::Layer && chosen.contains(node.id)) {
				report->layers << node.id;
			}
		}
		report->selection = document->selection;
		report->added = std::max(0, static_cast<int>(document->selection.size()) - before);
		report->skippedGroups = static_cast<int>(skipped.size());
	}
	return true;
}

QString levelMapSelectionLayerMoveBlockedReason(const LevelMapDocument& document, const QString& layerId)
{
	return levelMapSelectionLayerMoveBlockedReasons(document, {layerId}).value(0);
}

QStringList levelMapSelectionLayerMoveBlockedReasons(const LevelMapDocument& document, const QStringList& layerIds)
{
	MoveUnits units;
	QString error;
	const bool collected = collectLayerMoveUnits(document, &units, &error);
	QStringList reasons;
	for (const auto& layerId : layerIds) {
		reasons << (collected ? blockedReason(document, units, layerId) : error);
	}
	return reasons;
}

bool moveLevelMapSelectionToLayer(LevelMapDocument* document, const QString& layerId, LevelSceneLayerMoveReport* report, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!document) {
		if (error) {
			*error = Text::tr("Open a map before moving objects to a layer.");
		}
		return false;
	}
	MoveUnits units;
	MovePlan plan;
	if (!collectLayerMoveUnits(*document, &units, error) || !planLayerMove(*document, units, layerId, &plan, error)) {
		return false;
	}
	if (plan.report.changed) {
		const auto before = document->selection.size();
		const auto description = Text::tr("Move selection to layer %1").arg(layerName(*document, layerId));
		if (!commitLevelSceneState(document, plan.state, description, error)) {
			return false;
		}
		plan.report.deselected = static_cast<int>(before - document->selection.size());
	}
	if (report) {
		*report = plan.report;
	}
	return true;
}
} // namespace vibestudio
