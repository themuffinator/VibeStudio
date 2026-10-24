#include "core/level_scene_locks.h"
#include "core/level_scene.h"
#include "core/level_linked_groups.h"
#include "core/level_udmf.h"

#include <QCoreApplication>
#include <algorithm>

namespace vibestudio {
namespace {
QString ref(const char* kind, int id) { return QString::fromLatin1(kind) + QLatin1Char(':') + QString::number(id); }
bool fail(QString* error, const QString& object) {
	if (error) {
		*error = QCoreApplication::translate("LevelScene", "Unlock the layer or group protecting %1 before editing it.").arg(object);
	}
	return false;
}
struct EditContext {
	const LevelMapDocument* candidate = nullptr;
	// The document the guarded edit began with, unchanged until it publishes.
	const LevelMapDocument* original = nullptr;
	QVector<QHash<QString, QString>> remaps;
	std::optional<LevelMapDocument> contextBaseline;
	QString contextError;
};
// Per-thread, per-document scopes avoid persisting a bypass flag in documents,
// recovery or copied plans. Nested public editing services share one candidate.
thread_local QVector<EditContext*> edits;
struct EditScope {
	EditContext context;
	explicit EditScope(const LevelMapDocument* candidate, const LevelMapDocument* before) {
		context.candidate = candidate;
		context.original = before;
		if (!before->openSceneGroups.isEmpty()) { context.contextBaseline = *before; }
		edits << &context;
	}
	~EditScope() { edits.removeLast(); }
};
QString mapped(QString object, const EditContext& edit) {
	for (const auto& remap : edit.remaps) {
		if (object.isEmpty()) {
			break;
		}
		object = remap.value(object, object);
	}
	return object;
}
int mappedId(const char* kind, int id, const EditContext* edit) {
	if (!edit || edit->remaps.isEmpty() || id < 0) {
		return id;
	}
	const auto object = mapped(ref(kind, id), *edit);
	return object.isEmpty() ? -1 : object.mid(object.indexOf(':') + 1).toInt();
}
// Keep hot-path identities typed. Building selector strings for every native
// record is expensive in debug builds and adds no information to comparisons.
struct Protection {
	QSet<int> entities, brushes, patches, things, vertices, lines, sides, sectors;
	bool empty() const {
		return entities.isEmpty() && brushes.isEmpty() && patches.isEmpty() && things.isEmpty() && vertices.isEmpty() && lines.isEmpty() &&
			   sides.isEmpty() && sectors.isEmpty();
	}
};
void protectDoomBoundaries(const LevelMapDocument& document, Protection& result) {
	if (!result.sectors.isEmpty() || !result.lines.isEmpty()) {
		QSet<int> sectorSides;
		for (const auto& side : document.doomSidedefs) {
			if (result.sectors.contains(side.sector)) {
				sectorSides.insert(side.id);
				result.sides.insert(side.id);
			}
		}
		for (const auto& line : document.doomLinedefs) {
			if (result.lines.contains(line.id) || sectorSides.contains(line.frontSidedef) || sectorSides.contains(line.backSidedef)) {
				result.lines.insert(line.id);
				result.vertices.insert(line.startVertex);
				result.vertices.insert(line.endVertex);
				if (line.frontSidedef >= 0) {
					result.sides.insert(line.frontSidedef);
				}
				if (line.backSidedef >= 0) {
					result.sides.insert(line.backSidedef);
				}
			}
		}
	}
}
Protection protection(const LevelMapDocument& document) {
	Protection result;
	const auto nodes = levelSceneLockedNodes(document.scene);
	for (const auto& node : document.scene.nodes) {
		if (!nodes.contains(node.id)) {
			continue;
		}
		for (const auto& object : node.objects) {
			const QStringView view(object);
			const auto colon = view.indexOf(QLatin1Char(':'));
			if (colon < 0) {
				continue;
			}
			const auto kind = view.first(colon);
			const int id = view.sliced(colon + 1).toInt();
			if (kind == QLatin1StringView("entity")) {
				result.entities.insert(id);
			} else if (kind == QLatin1StringView("brush")) {
				result.brushes.insert(id);
			} else if (kind == QLatin1StringView("patch")) {
				result.patches.insert(id);
			} else if (kind == QLatin1StringView("thing")) {
				result.things.insert(id);
			} else if (kind == QLatin1StringView("vertex")) {
				result.vertices.insert(id);
			} else if (kind == QLatin1StringView("linedef")) {
				result.lines.insert(id);
			} else if (kind == QLatin1StringView("sector")) {
				result.sectors.insert(id);
			}
		}
	}
	if (!result.entities.isEmpty()) {
		for (const auto& brush : document.brushes) {
			if (result.entities.contains(brush.entityId)) {
				result.brushes.insert(brush.id);
			}
		}
		for (const auto& patch : document.patches) {
			if (result.entities.contains(patch.entityId)) {
				result.patches.insert(patch.id);
			}
		}
	}
	protectDoomBoundaries(document,result);
	return result;
}
bool same(const LevelMapVec3& a, const LevelMapVec3& b) { return a.valid == b.valid && a.x == b.x && a.y == b.y && a.z == b.z; }
bool same(const LevelMapEntity& a, const LevelMapEntity& b, const EditContext&) {
	if (a.className != b.className || !same(a.origin, b.origin) || a.properties.size() != b.properties.size()) {
		return false;
	}
	if (a.properties.constData() == b.properties.constData()) {
		return true;
	}
	for (int i = 0; i < a.properties.size(); ++i) {
		if (a.properties[i].key != b.properties[i].key || a.properties[i].value != b.properties[i].value) {
			return false;
		}
	}
	return true;
}
bool same(const LevelMapBrushFace& a, const LevelMapBrushFace& b) {
	return same(a.p0, b.p0) && same(a.p1, b.p1) && same(a.p2, b.p2) && a.textureName == b.textureName && a.shiftX == b.shiftX &&
		   a.shiftY == b.shiftY && a.rotation == b.rotation && a.scaleX == b.scaleX && a.scaleY == b.scaleY &&
		   a.explicitTextureAxes == b.explicitTextureAxes && same(a.uAxis, b.uAxis) && a.uOffset == b.uOffset && same(a.vAxis, b.vAxis) &&
		   a.vOffset == b.vOffset && a.contentFlags == b.contentFlags && a.surfaceFlags == b.surfaceFlags &&
		   a.surfaceValue == b.surfaceValue && a.explicitPlane == b.explicitPlane && same(a.planeNormal, b.planeNormal) &&
		   a.planeDistance == b.planeDistance && a.explicitTextureMatrix == b.explicitTextureMatrix && a.textureMatrix == b.textureMatrix;
}
bool same(const LevelMapBrush& a, const LevelMapBrush& b, const EditContext& edit) {
	if (mappedId("entity", a.entityId, &edit) != b.entityId || a.primitiveKind != b.primitiveKind || a.faces.size() != b.faces.size()) {
		return false;
	}
	if (a.faces.constData() == b.faces.constData()) {
		return true;
	}
	for (int i = 0; i < a.faces.size(); ++i) {
		if (!same(a.faces[i], b.faces[i])) {
			return false;
		}
	}
	return true;
}
bool same(const LevelMapPatch& a, const LevelMapPatch& b, const EditContext& edit) {
	if (mappedId("entity", a.entityId, &edit) != b.entityId || a.textureName != b.textureName || a.width != b.width ||
		a.height != b.height || a.fixedSubdivisions != b.fixedSubdivisions || a.subdivisionsX != b.subdivisionsX ||
		a.subdivisionsY != b.subdivisionsY || a.headerTail != b.headerTail || a.controlGridNormalized != b.controlGridNormalized ||
		a.controlU != b.controlU || a.controlV != b.controlV || a.controlPoints.size() != b.controlPoints.size()) {
		return false;
	}
	if (a.controlPoints.constData() == b.controlPoints.constData()) {
		return true;
	}
	for (int i = 0; i < a.controlPoints.size(); ++i) {
		if (!same(a.controlPoints[i], b.controlPoints[i])) {
			return false;
		}
	}
	return true;
}
bool same(const LevelMapDoomVertex& a, const LevelMapDoomVertex& b, const EditContext&) { return a.x == b.x && a.y == b.y; }
bool same(const LevelMapDoomThing& a, const LevelMapDoomThing& b, const EditContext&) {
	return a.x == b.x && a.y == b.y && a.z == b.z && a.angle == b.angle && a.type == b.type && a.flags == b.flags && a.tid == b.tid &&
		   a.special == b.special && a.args == b.args;
}
bool same(const LevelMapDoomLinedef& a, const LevelMapDoomLinedef& b, const EditContext& edit) {
	return mappedId("vertex", a.startVertex, &edit) == b.startVertex && mappedId("vertex", a.endVertex, &edit) == b.endVertex &&
		   mappedId("sidedef", a.frontSidedef, &edit) == b.frontSidedef && mappedId("sidedef", a.backSidedef, &edit) == b.backSidedef &&
		   a.flags == b.flags && a.special == b.special && a.tag == b.tag && a.args == b.args;
}
bool same(const LevelMapDoomSidedef& a, const LevelMapDoomSidedef& b, const EditContext& edit) {
	return mappedId("sector", a.sector, &edit) == b.sector && a.offsetX == b.offsetX && a.offsetY == b.offsetY &&
		   a.upperTexture == b.upperTexture && a.lowerTexture == b.lowerTexture && a.middleTexture == b.middleTexture;
}
bool same(const LevelMapDoomSector& a, const LevelMapDoomSector& b, const EditContext&) {
	return a.floorHeight == b.floorHeight && a.ceilingHeight == b.ceilingHeight && a.floorTexture == b.floorTexture &&
		   a.ceilingTexture == b.ceilingTexture && a.lightLevel == b.lightLevel && a.special == b.special && a.tag == b.tag;
}
template <typename Record>
bool unchanged(const QVector<Record>& before, const QVector<Record>& after, const char* kind, const QSet<int>& protectedIds,
			   const EditContext& edit, QString* error) {
	if (protectedIds.isEmpty()) {
		return true;
	}
	QHash<int, const Record*> records;
	for (const auto& value : after) {
		records.insert(value.id, &value);
	}
	// Compare native meaning. Selection and serialization caches are incidental;
	// unchanged implicitly shared face/control arrays need no allocation or scan.
	for (const auto& value : before) {
		if (!protectedIds.contains(value.id)) {
			continue;
		}
		const auto* next = records.value(mappedId(kind, value.id, &edit), nullptr);
		if (!next || !same(value, *next, edit)) {
			return fail(error, ref(kind, value.id));
		}
	}
	return true;
}
QSet<QString> nodePath(const LevelSceneState& scene, QString id) {
	QSet<QString> path;
	for (int depth = 0; depth < kLevelSceneMaxDepth && !id.isEmpty(); ++depth) {
		path.insert(id);
		const auto* node = levelSceneNode(scene, id);
		if (!node) {
			break;
		}
		id = node->parentId;
	}
	return path;
}
bool organizationUnchanged(const LevelMapDocument& before, const LevelSceneState& after, const EditContext& edit, QString* error) {
	const auto locked = levelSceneLockedNodes(before.scene);
	if (locked.isEmpty()) {
		return true;
	}
	QSet<QString> ancestors;
	for (const auto& id : locked) {
		ancestors.unite(nodePath(before.scene, id));
	}
	for (const auto& node : before.scene.nodes) {
		if (!ancestors.contains(node.id)) {
			continue;
		}
		const auto* next = levelSceneNode(after, node.id);
		if (!next || next->parentId != node.parentId || next->kind != node.kind) {
			return fail(error, node.name);
		}
		if (locked.contains(node.id)) {
			QSet<QString> expected;
			for (const auto& object : node.objects) {
				const auto nextObject = mapped(object, edit);
				if (nextObject.isEmpty()) {
					return fail(error, object);
				}
				expected.insert(nextObject);
			}
			if (expected != QSet<QString>(next->objects.cbegin(), next->objects.cend())) {
				return fail(error, node.name);
			}
		}
	}
	// Attaching an existing or new node beneath a lock is a membership change.
	for (const auto& node : after.nodes) {
		const auto* old = levelSceneNode(before.scene, node.id);
		if (!old || old->parentId != node.parentId) {
			if (nodePath(after, node.parentId).intersects(locked)) {
				return fail(error, node.name);
			}
		}
	}
	return true;
}
bool validateRecords(const LevelMapDocument& before, const LevelMapDocument& after, Protection objects,
	const EditContext& edit, QString* error, bool protectEntityChildren = true) {
	if (objects.empty()) {
		return true;
	}
	if (before.format != LevelMapFormat::DoomWad) {
		// A primitive's owner properties can move/change its game behavior even
		// when its own geometry is untouched. Do not propagate to sibling brushes
		// or global worldspawn properties.
		QSet<int> owners;
		const auto& protectedBrushes = objects.brushes;
		const auto& protectedPatches = objects.patches;
		for (const auto& brush : before.brushes) {
			if (protectedBrushes.contains(brush.id)) {
				owners.insert(brush.entityId);
			}
		}
		for (const auto& patch : before.patches) {
			if (protectedPatches.contains(patch.id)) {
				owners.insert(patch.entityId);
			}
		}
		const auto protectedEntities = objects.entities;
		using Children = QHash<int, QPair<QSet<int>, QSet<int>>>;
		const auto children = [&](const LevelMapDocument& document) {
			Children result;
			if (protectedEntities.isEmpty()) {
				return result;
			}
			for (const auto& brush : document.brushes) {
				if (protectedEntities.contains(brush.entityId)) {
					result[brush.entityId].first.insert(brush.id);
				}
			}
			for (const auto& patch : document.patches) {
				if (protectedEntities.contains(patch.entityId)) {
					result[patch.entityId].second.insert(patch.id);
				}
			}
			return result;
		};
		const auto oldChildren = children(before), newChildren = children(after);
		for (const auto& entity : before.entities) {
			if (owners.contains(entity.id) && entity.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) != 0) {
				objects.entities.insert(entity.id);
			}
			if (protectEntityChildren && protectedEntities.contains(entity.id) && oldChildren.value(entity.id) != newChildren.value(entity.id)) {
				return fail(error, ref("entity", entity.id));
			}
		}
		return unchanged(before.entities, after.entities, "entity", objects.entities, edit, error) &&
			   unchanged(before.brushes, after.brushes, "brush", objects.brushes, edit, error) &&
			   unchanged(before.patches, after.patches, "patch", objects.patches, edit, error);
	}
	// Protect additions to a locked sector too: existing records alone cannot
	// detect a new boundary that attaches itself to the protected sector.
	const auto& protectedSectors = objects.sectors;
	if (!protectedSectors.isEmpty()) {
		const auto boundaries = [](const LevelMapDocument& document, const QSet<int>& sectors, const EditContext* mapping) {
			QHash<int, int> sideSectors;
			QHash<int, QPair<QSet<int>, QSet<int>>> result;
			for (const auto& side : document.doomSidedefs) {
				if (sectors.contains(side.sector)) {
					sideSectors.insert(side.id, side.sector);
					result[side.sector].first.insert(mappedId("sidedef", side.id, mapping));
				}
			}
			for (const auto& line : document.doomLinedefs) {
				for (int side : {line.frontSidedef, line.backSidedef}) {
					if (sideSectors.contains(side)) {
						result[sideSectors.value(side)].second.insert(mappedId("linedef", line.id, mapping));
					}
				}
			}
			return result;
		};
		QSet<int> mappedSectors;
		for (int sector : protectedSectors) {
			mappedSectors.insert(mappedId("sector", sector, &edit));
		}
		const auto oldBoundaries = boundaries(before, protectedSectors, &edit), newBoundaries = boundaries(after, mappedSectors, nullptr);
		for (int sector : protectedSectors) {
			if (oldBoundaries.value(sector) != newBoundaries.value(mappedId("sector", sector, &edit))) {
				return fail(error, ref("sector", sector));
			}
		}
	}
	return unchanged(before.doomThings, after.doomThings, "thing", objects.things, edit, error) &&
		   unchanged(before.doomVertices, after.doomVertices, "vertex", objects.vertices, edit, error) &&
		   unchanged(before.doomLinedefs, after.doomLinedefs, "linedef", objects.lines, edit, error) &&
		   unchanged(before.doomSidedefs, after.doomSidedefs, "sidedef", objects.sides, edit, error) &&
		   unchanged(before.doomSectors, after.doomSectors, "sector", objects.sectors, edit, error);
}

bool validateEdit(const LevelMapDocument& before, const LevelMapDocument& after, const EditContext& edit, QString* error)
{
	return organizationUnchanged(before,after.scene,edit,error) && validateRecords(before,after,protection(before),edit,error);
}

bool unchangedUdmfContext(const LevelMapDocument& before, const LevelMapDocument& after, const Protection& outside, const EditContext& edit)
{
	if (!before.doomUdmf) { return true; }
	if (!after.doomUdmf || before.doomUdmf->nameSpace != after.doomUdmf->nameSpace) { return false; }
	const auto propertiesEqual = [](const auto& a, const auto& b) {
		if (a.size() != b.size()) { return false; }
		for (qsizetype i = 0; i < a.size(); ++i) {
			if (a[i].name != b[i].name || a[i].literal != b[i].literal || a[i].kind != b[i].kind) { return false; }
		}
		return true;
	};
	if (!propertiesEqual(before.doomUdmf->globals,after.doomUdmf->globals)) { return false; }
	QHash<QString,const LevelUdmfBlock*> next;
	for (const auto& block : after.doomUdmf->blocks) { next.insert(block.selector(),&block); }
	QSet<QString> existing;
	const auto known = [](const QString& type) { return type == "vertex" || type == "linedef" || type == "sidedef" || type == "sector" || type == "thing"; };
	for (const auto& block : before.doomUdmf->blocks) {
		const auto selector = mapped(block.selector(),edit); existing.insert(selector);
		const bool protectedBlock = !known(block.type) || (block.type == "vertex" && outside.vertices.contains(block.index))
			|| (block.type == "linedef" && outside.lines.contains(block.index)) || (block.type == "sidedef" && outside.sides.contains(block.index))
			|| (block.type == "sector" && outside.sectors.contains(block.index)) || (block.type == "thing" && outside.things.contains(block.index));
		if (protectedBlock) {
			const auto* result = next.value(selector);
			if (!result || !propertiesEqual(block.properties,result->properties)) { return false; }
		}
	}
	for (const auto& block : after.doomUdmf->blocks) {
		if (!known(block.type) && !existing.contains(block.selector())) { return false; }
	}
	return true;
}

bool validateContextEdit(const LevelMapDocument& before, const LevelMapDocument& after, const EditContext& edit, QString* error)
{
	if (before.openSceneGroups.isEmpty()) { return true; }
	const auto context = levelSceneSelectionContext(before);
	Protection outside;
	const auto collect = [&](const auto& records, LevelMapSelectionKind kind, QSet<int>* protectedIds) {
		for (const auto& item : records) {
			if (!context.allowedObjects.contains(levelMapSelectionRefId({kind,item.id}))) { protectedIds->insert(item.id); }
		}
	};
	collect(before.brushes,LevelMapSelectionKind::QuakeBrush,&outside.brushes);
	collect(before.patches,LevelMapSelectionKind::QuakePatch,&outside.patches);
	if (before.format != LevelMapFormat::DoomWad) { collect(before.entities,LevelMapSelectionKind::Entity,&outside.entities); }
	collect(before.doomThings,LevelMapSelectionKind::DoomThing,&outside.things);
	collect(before.doomVertices,LevelMapSelectionKind::DoomVertex,&outside.vertices);
	collect(before.doomLinedefs,LevelMapSelectionKind::DoomLinedef,&outside.lines);
	collect(before.doomSectors,LevelMapSelectionKind::DoomSector,&outside.sectors);
	for (const auto& side : before.doomSidedefs) {
		if (!context.allowedObjects.contains(levelMapSelectionRefId({LevelMapSelectionKind::DoomSector,side.sector}))) { outside.sides.insert(side.id); }
	}
	protectDoomBoundaries(before,outside);
	QString problem;
	if (!validateRecords(before,after,outside,edit,&problem,false) || !unchangedUdmfContext(before,after,outside,edit)) {
		if (error) { *error = QCoreApplication::translate("LevelSceneContext","This edit would change objects outside the open group. Close the group before continuing."); } return false;
	}
	const auto next = levelSceneSelectionContext(after);
	QSet<QString> existing;
	for (const auto& object : levelSceneObjects(before)) { existing.insert(mapped(object,edit)); }
	for (const auto& object : levelSceneObjects(after)) {
		if (!existing.contains(object) && !next.allowedObjects.contains(object)) {
			if (error) { *error = QCoreApplication::translate("LevelSceneContext","New objects must belong to the open group. Close it to create elsewhere."); } return false;
		}
	}
	return true;
}
} // namespace

QSet<QString> levelSceneLockedNodes(const LevelSceneState& scene) {
	QSet<QString> nodes;
	for (const auto& node : scene.nodes) {
		if (node.locked) {
			nodes.insert(node.id);
		}
	}
	if (nodes.isEmpty()) {
		return nodes;
	}
	for (int depth = 0; depth < kLevelSceneMaxDepth; ++depth) {
		const auto size = nodes.size();
		for (const auto& node : scene.nodes) {
			if (nodes.contains(node.parentId)) {
				nodes.insert(node.id);
			}
		}
		if (size == nodes.size()) {
			break;
		}
	}
	return nodes;
}

QSet<QString> levelSceneLockedObjects(const LevelMapDocument& document) {
	const auto protectedObjects = protection(document);
	QSet<QString> result;
	const auto append = [&](const char* kind, const QSet<int>& objects) {
		for (int id : objects) {
			result.insert(ref(kind, id));
		}
	};
	append("entity", protectedObjects.entities);
	append("brush", protectedObjects.brushes);
	append("patch", protectedObjects.patches);
	append("thing", protectedObjects.things);
	append("vertex", protectedObjects.vertices);
	append("linedef", protectedObjects.lines);
	append("sidedef", protectedObjects.sides);
	append("sector", protectedObjects.sectors);
	return result;
}

bool setLevelSceneLocked(LevelMapDocument* document, const QString& id, bool locked, QString* error) {
	if (error) {
		error->clear();
	}
	if (!document || !levelSceneNode(document->scene, id)) {
		if (error) {
			*error = QCoreApplication::translate("LevelScene", "Choose an existing layer or group to change its lock.");
		}
		return false;
	}
	auto state = document->scene;
	for (auto& node : state.nodes) {
		if (node.id == id) {
			node.locked = locked;
			break;
		}
	}
	return commitLevelSceneState(document, state, QCoreApplication::translate("LevelScene", "Change scene lock"), error);
}

bool validateLevelSceneOrganizationEdit(const LevelMapDocument& current, const LevelSceneState& after, QString* error) {
	// Inside a guarded edit, locks are checked against the document the edit
	// began with: records the edit made itself may pass through a locked
	// group on their way, as a linked update's copies of a locked nested
	// group's content do.
	const auto found = std::find_if(edits.cbegin(), edits.cend(), [&current](const auto* edit) { return edit->candidate == &current; });
	const EditContext* edit = found == edits.cend() ? nullptr : *found;
	const LevelMapDocument& before = edit && edit->original ? *edit->original : current;
	if (!organizationUnchanged(before, after, edit ? *edit : EditContext{}, error)) {
		return false;
	}
	const auto protectedObjects = levelSceneLockedObjects(before);
	if (protectedObjects.isEmpty()) {
		return true;
	}
	const auto memberships = [](const LevelSceneState& state) {
		QHash<QString, QString> result;
		for (const auto& node : state.nodes) {
			for (const auto& object : node.objects) {
				result.insert(object, node.id);
			}
		}
		return result;
	};
	const auto oldMemberships = memberships(before.scene), newMemberships = memberships(after);
	// Owned/shared records remain protected even when assigned outside the node
	// that supplies their lock. A scene assignment cannot be used to escape it.
	for (const auto& object : protectedObjects) {
		const auto now = edit ? mapped(object, *edit) : object;
		if (oldMemberships.value(object) != newMemberships.value(now)
			|| before.scene.protectedProperties.value(object) != after.protectedProperties.value(now)) {
			return fail(error, object);
		}
	}
	return true;
}

std::optional<bool> guardLevelSceneEdit(LevelMapDocument* document, QString* error,
										const std::function<bool(LevelMapDocument*)>& operation) {
	if (!document || std::any_of(edits.cbegin(), edits.cend(), [document](const auto* edit) { return edit->candidate == document; }) ||
		(document->openSceneGroups.isEmpty() && std::none_of(document->scene.nodes.cbegin(), document->scene.nodes.cend(), [](const auto& node) { return node.locked; }))) {
		return std::nullopt;
	}
	LevelMapDocument candidate = *document;
	EditScope scope(&candidate,document);
	if (!operation(&candidate) || !validateEdit(*document, candidate, scope.context, error)) {
		return false;
	}
	if (!scope.context.contextError.isEmpty()) { if (error) { *error = scope.context.contextError; } return false; }
	if (scope.context.contextBaseline && !validateContextEdit(*scope.context.contextBaseline,candidate,{},error)) { return false; }
	*document = std::move(candidate);
	return true;
}

void recordLevelSceneEdit(const LevelMapDocument* document, const LevelMapUndoCommand& command) {
	for (auto* edit : edits) {
		if (edit->candidate == document) {
			if (!command.sceneObjectRemap.isEmpty()) {
				edit->remaps << command.sceneObjectRemap;
			}
			if (edit->contextBaseline) {
				if (edit->contextError.isEmpty() && !levelLinkedGroupPropagationActive(document)) {
					EditContext step;
					if (!command.sceneObjectRemap.isEmpty()) { step.remaps << command.sceneObjectRemap; }
					validateContextEdit(*edit->contextBaseline,*document,step,&edit->contextError);
				}
				edit->contextBaseline = *document;
			}
			return;
		}
	}
	// New authoring entry points must use the same boundary. Scene operations
	// have their own hierarchy guard; undo/redo never enter pushUndo.
	// A paste may introduce the first locked nodes. The guard was decided from
	// the original scene, so assert against that snapshot, not the new locks.
	const auto& originalScene = command.hasSceneSnapshot ? command.sceneBefore : document->scene;
	Q_ASSERT(command.commandKind == QStringLiteral("scene") || levelLinkedGroupPropagationActive(document) ||
			 (document->openSceneGroups.isEmpty() && std::none_of(originalScene.nodes.cbegin(), originalScene.nodes.cend(), [](const auto& node) { return node.locked; })));
}
} // namespace vibestudio
