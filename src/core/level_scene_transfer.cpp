#include "core/level_scene.h"
#include "core/level_linked_groups.h"
#include "core/level_linked_objects.h"
#include <QCoreApplication>
#include <QUuid>

namespace vibestudio {
namespace {
QString freshId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QString uniqueName(const LevelSceneState& state, const QString& parent, const QString& name)
{
	QSet<QString> used;
	for (const auto& node : state.nodes) { if (node.parentId == parent) { used.insert(node.name.toCaseFolded()); } }
	QString result = name;
	for (int suffix = 2; used.contains(result.toCaseFolded()); ++suffix) {
		const auto tail = QStringLiteral(" %1").arg(suffix);
		auto base = name.left(128-tail.size());
		if (!base.isEmpty() && base.back().isHighSurrogate()) { base.chop(1); }
		result = base + tail;
	}
	return result;
}
}

// Original implementation; group copy semantics follow the TrenchBroom 2026.2
// manual's Groups section. No upstream code is incorporated; see docs/CREDITS.md.
LevelSceneState captureLevelSceneCopy(const LevelMapDocument& source, const QSet<QString>& objects, const QSet<QString>& partialObjects)
{
	LevelSceneState result;
	const auto context = levelSceneSelectionContext(source);
	QSet<QString> included;
	for (auto it = context.groupSelections.cbegin(); it != context.groupSelections.cend(); ++it) {
		bool complete = !it->isEmpty();
		for (const auto& ref : it.value()) { complete &= objects.contains(levelMapSelectionRefId(ref)) && !partialObjects.contains(levelMapSelectionRefId(ref)); }
		if (complete) { included.insert(it.key()); }
	}
	// Nodes are not required to be stored in parent-first order.
	for (int depth = 0; depth < kLevelSceneMaxDepth; ++depth) {
		bool changed = false;
		for (const auto& node : source.scene.nodes) {
			if (included.contains(node.parentId) && !included.contains(node.id)) { included.insert(node.id); changed = true; }
		}
		if (!changed) { break; }
	}
	for (const auto& node : source.scene.nodes) { if (included.contains(node.id)) { result.nodes << node; } }
	for (auto it = source.scene.protectedProperties.cbegin(); it != source.scene.protectedProperties.cend(); ++it) {
		if (objects.contains(it.key())) { result.protectedProperties.insert(it.key(), it.value()); }
	}
	return result;
}

bool mergeLevelSceneCopy(const LevelMapDocument& source, const LevelMapDocument& target,
	const QHash<QString, QString>& objects, bool sameDocument, LevelSceneState* state, QString* error,
	const QSet<QString>& partialObjects, QHash<QString,QString>* copiedNodes)
{
	if (!state) { return false; }
	const QSet<QString> copied(objects.keyBegin(),objects.keyEnd());
	auto captured = captureLevelSceneCopy(source,copied,partialObjects);
	if (captured.nodes.isEmpty() && captured.protectedProperties.isEmpty()) { if (copiedNodes) { copiedNodes->clear(); } return true; }
	if (!validateLevelScene(source,source.scene,error)) { return false; }
	if (state->nodes.size()+captured.nodes.size() > kLevelSceneMaxNodes) {
		if (error) { *error = QCoreApplication::translate("LevelScene", "Copying these groups would exceed the limit of %1 scene nodes.").arg(kLevelSceneMaxNodes); }
		return false;
	}
	LevelSceneState result = *state;
	for (auto it = captured.protectedProperties.cbegin(); it != captured.protectedProperties.cend(); ++it) {
		result.protectedProperties.insert(objects.value(it.key()), it.value());
	}
	QHash<QString,QString> ids, links;
	QHash<QString,int> linkCounts;
	QSet<QString> transferred;
	for (const auto& node : captured.nodes) {
		ids.insert(node.id,freshId());
		if (!node.linkId.isEmpty()) { ++linkCounts[node.linkId]; }
		for (const auto& object : node.objects) { transferred.insert(object); }
	}
	// Owned geometry inherits its entity's group. Remove the temporary creation
// assignment made by paste, without erasing explicit per-primitive overrides.
	const auto inherit = [&](const auto& primitive, LevelMapSelectionKind kind) {
		const auto object = levelMapSelectionRefId({kind,primitive.id});
		if (transferred.contains(levelMapSelectionRefId({LevelMapSelectionKind::Entity,primitive.entityId})) &&
			levelSceneMembership(source.scene,object).isEmpty()) { transferred.insert(object); }
	};
	for (const auto& brush : source.brushes) { inherit(brush,LevelMapSelectionKind::QuakeBrush); }
	for (const auto& patch : source.patches) { inherit(patch,LevelMapSelectionKind::QuakePatch); }
	QSet<QString> assigned;
	for (const auto& object : transferred) { if (objects.contains(object)) { assigned.insert(objects.value(object)); } }
	for (auto& node : result.nodes) { node.objects.removeIf([&](const auto& object) { return assigned.contains(object); }); }
	LevelSceneState additions;
	for (auto node : captured.nodes) {
		node.id = ids.value(node.id);
		node.parentId = ids.contains(node.parentId) ? ids.value(node.parentId) : (sameDocument ? node.parentId : target.activeSceneNode);
		node.name = uniqueName(result,node.parentId,node.name);
		for (auto& object : node.objects) {
			if (!objects.contains(object)) {
				if (error) { *error = QCoreApplication::translate("LevelScene", "A copied group is missing one of its native objects."); }
				return false;
			}
			object = objects.value(object);
		}
		QMap<QString, QString> identities;
		for (auto it = node.linkObjects.cbegin(); it != node.linkObjects.cend(); ++it) {
			if (!objects.contains(it.key())) {
				if (error) { *error = QCoreApplication::translate("LevelScene", "A copied linked group is missing one of its corresponding objects."); }
				return false;
			}
			identities.insert(objects.value(it.key()), it.value());
		}
		node.linkObjects = identities;
		if (!sameDocument && !node.linkId.isEmpty()) {
			if (linkCounts.value(node.linkId) < 2) {
				node.linkId.clear(); node.linkObjects.clear(); node.linkFrame = {1, 0, 0, 0, 1, 0, 0, 0, 1}; node.linkOutOfSync = false;
			}
			else {
				if (!links.contains(node.linkId)) { links.insert(node.linkId,freshId()); }
				node.linkId = links.value(node.linkId);
			}
		}
		result.nodes << node;
		additions.nodes << node;
	}
	// A copied linked group's frame moves with its content. Copies and pastes
	// move content whole, so the centres of its bounds give the move.
	QHash<QString,QString> originals;
	for (auto it = ids.cbegin(); it != ids.cend(); ++it) { originals.insert(it.value(), it.key()); }
	for (auto& node : additions.nodes) {
		if (node.linkId.isEmpty()) { continue; }
		const auto before = levelLinkedGroupContent(source, source.scene, originals.value(node.id));
		const auto after = levelLinkedGroupContent(target, result, node.id);
		if (before.hasPoints && after.hasPoints) {
			node.linkFrame = levelLinkFrameAfter({1, 0, 0, after.anchor.x - before.anchor.x, 0, 1, 0, after.anchor.y - before.anchor.y,
				0, 0, 1, after.anchor.z - before.anchor.z}, node.linkFrame);
		}
	}
	// Baseline only the imported nodes. Rebaselining existing linked instances
// here would conceal edits to an open linked group from normal propagation.
	refreshLevelLinkBaselines(target,&additions);
	for (qsizetype i = 0; i < additions.nodes.size(); ++i) {
		result.nodes[result.nodes.size()-additions.nodes.size()+i] = additions.nodes[i];
	}
	reconcileLevelLinkedObjects(target, &result);
	if (!validateLevelScene(target,result,error)) { return false; }
	*state = result;
	if (copiedNodes) { *copiedNodes = std::move(ids); }
	return true;
}
} // namespace vibestudio
