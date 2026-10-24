#include "core/level_scene.h"
#include "core/level_history_p.h"
#include <QCoreApplication>
#include <algorithm>

namespace vibestudio {
namespace {
using NodeIndex = QHash<QString,const LevelSceneNode*>;
NodeIndex nodeIndex(const LevelSceneState& scene)
{
	NodeIndex nodes;
	for (const auto& node : scene.nodes) { nodes.insert(node.id,&node); }
	return nodes;
}
QStringList groupPath(const NodeIndex& nodes, QString id)
{
	QStringList path; QSet<QString> seen;
	while (!id.isEmpty() && path.size() < kLevelSceneMaxDepth && !seen.contains(id)) {
		seen.insert(id); const auto* node = nodes.value(id);
		if (!node || node->kind != LevelSceneNodeKind::Group) { break; }
		path.prepend(id); id = node->parentId;
	}
	return path;
}
QStringList groupPath(const LevelSceneState& scene, const QString& id) { return groupPath(nodeIndex(scene),id); }
LevelMapSelectionRef reference(const QString& object)
{
	const auto colon = object.indexOf(':');
	return {levelMapSelectionKindFromId(object.left(colon)),object.mid(colon+1).toInt()};
}
void contextChanged(LevelMapDocument* document)
{
	++document->revision;
	const auto context = levelSceneSelectionContext(*document);
	setLevelMapSelection(document,pickLevelSceneObjects(context,{},document->selection,LevelScenePick::Replace));
}
}

// Original selection/context implementation. Behaviour reference: TrenchBroom
// 2026.2 manual, Groups, reviewed 2026-10-18; see docs/CREDITS.md.
LevelSceneSelectionContext levelSceneSelectionContext(const LevelMapDocument& document)
{
	LevelSceneSelectionContext result;
	if (!document.openSceneGroups.isEmpty()) { result.openGroup = document.openSceneGroups.last(); }
	QHash<QString, QString> membership;
	QHash<QString, QStringList> paths;
	const auto nodes = nodeIndex(document.scene);
	for (const auto& node : document.scene.nodes) {
		paths.insert(node.id,groupPath(nodes,node.id));
		for (const auto& object : node.objects) { membership.insert(object,node.id); }
	}
	QHash<QString, QString> owners;
	QSet<int> world;
	for (const auto& entity : document.entities) {
		if (entity.className.compare(QStringLiteral("worldspawn"),Qt::CaseInsensitive) == 0) {
			world.insert(entity.id);
			// World properties are inspectable in map context, although worldspawn
			// cannot itself be assigned to a group or layer.
			if (result.openGroup.isEmpty()) { result.allowedObjects.insert(levelMapSelectionRefId({LevelMapSelectionKind::Entity,entity.id})); }
		}
	}
	for (const auto& brush : document.brushes) {
		if (!world.contains(brush.entityId)) { owners.insert(levelMapSelectionRefId({LevelMapSelectionKind::QuakeBrush,brush.id}),levelMapSelectionRefId({LevelMapSelectionKind::Entity,brush.entityId})); }
	}
	for (const auto& patch : document.patches) {
		if (!world.contains(patch.entityId)) { owners.insert(levelMapSelectionRefId({LevelMapSelectionKind::QuakePatch,patch.id}),levelMapSelectionRefId({LevelMapSelectionKind::Entity,patch.entityId})); }
	}
	if (document.format == LevelMapFormat::DoomWad) {
		// Unassigned boundaries inherit a sector's context only when every
		// incident sector agrees. Shared boundaries stay outside a narrower
		// context, so editing one room cannot deform its neighbour unnoticed.
		QHash<int,int> sectors;
		for (const auto& side : document.doomSidedefs) { sectors.insert(side.id,side.sector); }
		QHash<int,QSet<QString>> vertexMemberships;
		for (const auto& line : document.doomLinedefs) {
			const auto object = levelMapSelectionRefId({LevelMapSelectionKind::DoomLinedef,line.id});
			if (!membership.contains(object)) {
				QSet<QString> incident;
				for (int side : {line.frontSidedef,line.backSidedef}) {
					if (sectors.contains(side)) { incident.insert(membership.value(levelMapSelectionRefId({LevelMapSelectionKind::DoomSector,sectors.value(side)}))); }
				}
				if (incident.size() == 1 && !incident.cbegin()->isEmpty()) { membership.insert(object,*incident.cbegin()); }
			}
			for (int vertex : {line.startVertex,line.endVertex}) { vertexMemberships[vertex].insert(membership.value(object)); }
		}
		for (auto it = vertexMemberships.cbegin(); it != vertexMemberships.cend(); ++it) {
			const auto object = levelMapSelectionRefId({LevelMapSelectionKind::DoomVertex,it.key()});
			if (!membership.contains(object) && it->size() == 1 && !it->cbegin()->isEmpty()) { membership.insert(object,*it->cbegin()); }
		}
	}
	for (const auto& object : levelSceneObjects(document)) {
		const auto node = membership.value(object,membership.value(owners.value(object)));
		const auto path = paths.value(node);
		const int opened = result.openGroup.isEmpty() ? -1 : static_cast<int>(path.indexOf(result.openGroup));
		if (!result.openGroup.isEmpty() && opened < 0) { continue; }
		result.allowedObjects.insert(object);
		if (opened+1 < path.size()) {
			const auto group = path[opened+1]; result.closedGroups.insert(object,group);
			result.groupSelections[group].append(reference(object));
		}
	}
	// Selecting an entity already selects its owned geometry for transforms.
	for (auto it = result.groupSelections.begin(); it != result.groupSelections.end(); ++it) {
		QSet<QString> included;
		for (const auto& ref : std::as_const(it.value())) { included.insert(levelMapSelectionRefId(ref)); }
		it.value().removeIf([&](const auto& ref) { return included.contains(owners.value(levelMapSelectionRefId(ref))); });
	}
	return result;
}

QVector<LevelMapSelectionRef> pickLevelSceneObjects(const LevelSceneSelectionContext& context,
	const QVector<LevelMapSelectionRef>& selected, const QVector<LevelMapSelectionRef>& hits, LevelScenePick mode)
{
	QVector<LevelMapSelectionRef> result = mode == LevelScenePick::Replace ? QVector<LevelMapSelectionRef>{}
		: pickLevelSceneObjects(context,{},selected,LevelScenePick::Replace);
	QHash<QString,qsizetype> positions;
	for (qsizetype i = 0; i < result.size(); ++i) {
		const auto object = levelMapSelectionRefId(result[i]);
		if (positions.contains(object)) { result[positions.value(object)] = {}; }
		positions.insert(object,i);
	}
	QSet<QString> visited;
	for (const auto& hit : hits) {
		const auto object = levelMapSelectionRefId(hit);
		if (!context.allowedObjects.contains(object)) { continue; }
		const auto group = context.closedGroups.value(object);
		const auto unit = group.isEmpty() ? object : group;
		if (visited.contains(unit)) { continue; } visited.insert(unit);
		const auto members = group.isEmpty() ? QVector<LevelMapSelectionRef>{hit} : context.groupSelections.value(group);
		const bool remove = mode == LevelScenePick::Toggle && std::all_of(members.cbegin(),members.cend(),[&](const auto& ref) { return positions.contains(levelMapSelectionRefId(ref)); });
		for (const auto& ref : members) {
			const auto key = levelMapSelectionRefId(ref);
			if (positions.contains(key)) { result[positions.take(key)] = {}; }
			if (!remove) { positions.insert(key,result.size()); result.append(ref); }
		}
	}
	result.removeIf([&](const auto& ref) { return !context.allowedObjects.contains(levelMapSelectionRefId(ref)); });
	return result;
}

bool openLevelSceneGroup(LevelMapDocument* document, const QString& id, QString* error)
{
	if (error) { error->clear(); }
	const auto* node = document ? levelSceneNode(document->scene,id) : nullptr;
	if (!node || node->kind != LevelSceneNodeKind::Group || !document->scene.opaqueMetadata.isEmpty() || !document->scene.problem.isEmpty()) {
		if (error) { *error = QCoreApplication::translate("LevelSceneContext","Choose an existing group with recognised scene metadata."); } return false;
	}
	const auto path = groupPath(document->scene,id);
	if (document->openSceneGroups == path) { return true; }
	if (document->openSceneGroups.isEmpty()) { document->sceneCreationBeforeOpen = document->activeSceneNode; }
	document->openSceneGroups = path; document->activeSceneNode = id; setLevelMapSelection(document,{});
	contextChanged(document); return true;
}

bool closeLevelSceneGroup(LevelMapDocument* document, bool all, QString* error)
{
	if (error) { error->clear(); }
	if (!document || document->openSceneGroups.isEmpty()) {
		if (error) { *error = QCoreApplication::translate("LevelSceneContext","No group is open."); } return false;
	}
	const auto closed = all ? document->openSceneGroups.first() : document->openSceneGroups.last();
	if (all) { document->openSceneGroups.clear(); } else { document->openSceneGroups.removeLast(); }
	if (document->openSceneGroups.isEmpty()) {
		document->activeSceneNode = levelSceneNode(document->scene,document->sceneCreationBeforeOpen) ? document->sceneCreationBeforeOpen : QString{};
		document->sceneCreationBeforeOpen.clear();
	} else { document->activeSceneNode = document->openSceneGroups.last(); }
	setLevelMapSelection(document,levelSceneSelection(*document,closed)); contextChanged(document); return true;
}

void reconcileLevelSceneContext(LevelMapDocument* document)
{
	if (!document || document->openSceneGroups.isEmpty()) { return; }
	for (auto it = document->openSceneGroups.crbegin(); it != document->openSceneGroups.crend(); ++it) {
		const auto* node = levelSceneNode(document->scene,*it);
		if (!node || node->kind != LevelSceneNodeKind::Group) { continue; }
		const auto path = groupPath(document->scene,*it);
		if (path == document->openSceneGroups) { return; }
		document->openSceneGroups = path; document->activeSceneNode = path.last(); contextChanged(document); return;
	}
	closeLevelSceneGroup(document,true);
}

bool changeLevelSceneGroupContext(LevelMapDocument* document, const QString& id, bool closeAll, QString* error)
{
	if (!document) {
		if (error) { *error = QCoreApplication::translate("LevelSceneContext", "Choose an existing group with recognised scene metadata."); }
		return false;
	}
	auto candidate = *document;
	if (!(id.isEmpty() ? closeLevelSceneGroup(&candidate, closeAll, error) : openLevelSceneGroup(&candidate, id, error))) { return false; }
	detail::recordLevelMapSelectionHistory(&candidate, document->selection,
		{document->openSceneGroups, document->activeSceneNode, document->sceneCreationBeforeOpen},
		id.isEmpty() ? QCoreApplication::translate("LevelSceneContext", "Close group") : QCoreApplication::translate("LevelSceneContext", "Open group"));
	*document = std::move(candidate);
	return true;
}
} // namespace vibestudio
