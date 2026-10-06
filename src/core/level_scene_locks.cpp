#include "core/level_scene_locks.h"
#include "core/level_scene.h"

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
	QVector<QHash<QString, QString>> remaps;
};
// Per-thread, per-document scopes avoid persisting a bypass flag in documents,
// recovery or copied plans. Nested public editing services share one candidate.
thread_local QVector<EditContext*> edits;
struct EditScope {
	EditContext context;
	explicit EditScope(const LevelMapDocument* candidate) {
		context.candidate = candidate;
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
bool validateEdit(const LevelMapDocument& before, const LevelMapDocument& after, const EditContext& edit, QString* error) {
	if (!organizationUnchanged(before, after.scene, edit, error)) {
		return false;
	}
	auto objects = protection(before);
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
			if (protectedEntities.contains(entity.id) && oldChildren.value(entity.id) != newChildren.value(entity.id)) {
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

bool validateLevelSceneOrganizationEdit(const LevelMapDocument& before, const LevelSceneState& after, QString* error) {
	if (!organizationUnchanged(before, after, {}, error)) {
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
		if (oldMemberships.value(object) != newMemberships.value(object)) {
			return fail(error, object);
		}
	}
	return true;
}

std::optional<bool> guardLevelSceneEdit(LevelMapDocument* document, QString* error,
										const std::function<bool(LevelMapDocument*)>& operation) {
	if (!document || std::any_of(edits.cbegin(), edits.cend(), [document](const auto* edit) { return edit->candidate == document; }) ||
		std::none_of(document->scene.nodes.cbegin(), document->scene.nodes.cend(), [](const auto& node) { return node.locked; })) {
		return std::nullopt;
	}
	LevelMapDocument candidate = *document;
	EditScope scope(&candidate);
	if (!operation(&candidate) || !validateEdit(*document, candidate, scope.context, error)) {
		return false;
	}
	*document = std::move(candidate);
	return true;
}

void recordLevelSceneEdit(const LevelMapDocument* document, const LevelMapUndoCommand& command) {
	for (auto* edit : edits) {
		if (edit->candidate == document) {
			if (!command.sceneObjectRemap.isEmpty()) {
				edit->remaps << command.sceneObjectRemap;
			}
			return;
		}
	}
	// New authoring entry points must use the same boundary. Scene operations
	// have their own hierarchy guard; undo/redo never enter pushUndo.
	Q_ASSERT(command.commandKind == QStringLiteral("scene") ||
			 std::none_of(document->scene.nodes.cbegin(), document->scene.nodes.cend(), [](const auto& node) { return node.locked; }));
}
} // namespace vibestudio
