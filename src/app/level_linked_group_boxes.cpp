#include "app/level_linked_group_boxes.h"

#include "core/level_scene.h"

#include <QCoreApplication>

#include <algorithm>

namespace vibestudio {

namespace {

bool sameVec(const LevelMapVec3& left, const LevelMapVec3& right)
{
	return left.valid == right.valid && left.x == right.x && left.y == right.y && left.z == right.z;
}

} // namespace

bool operator==(const LevelLinkedGroupBox& left, const LevelLinkedGroupBox& right)
{
	return left.id == right.id && left.link == right.link && left.name == right.name && sameVec(left.mins, right.mins)
		&& sameVec(left.maxs, right.maxs) && left.outOfSync == right.outOfSync;
}

QVector<LevelLinkedGroupBox> levelLinkedGroupBoxes(const LevelMapDocument& document, const QVector<MapBrushGeometry>& brushes)
{
	QVector<LevelLinkedGroupBox> result;
	const LevelSceneState& scene = document.scene;
	// Linked groups belong to Quake-family maps.
	const bool doom = document.format == LevelMapFormat::DoomWad || !document.doomLinedefs.isEmpty() || !document.doomVertices.isEmpty();
	if (doom || std::none_of(scene.nodes.cbegin(), scene.nodes.cend(), [](const LevelSceneNode& node) { return !node.linkId.isEmpty(); })) {
		return result;
	}
	QHash<QString, QString> parents;
	QHash<QString, int> boxes;
	QHash<QString, QString> membership;
	for (const LevelSceneNode& node : scene.nodes) {
		parents.insert(node.id, node.parentId);
		for (const QString& object : node.objects) {
			membership.insert(object, node.id);
		}
		if (!node.linkId.isEmpty()) {
			boxes.insert(node.id, static_cast<int>(result.size()));
			result.push_back({node.id, node.linkId, node.name, {}, {}, node.linkOutOfSync});
		}
	}
	// A point grows the box of every linked group holding it, nested ones too.
	const auto grow = [&result, &boxes, &parents](QString holder, const LevelMapVec3& point) {
		if (!point.valid) {
			return;
		}
		for (int depth = 0; !holder.isEmpty() && depth < kLevelSceneMaxDepth; ++depth) {
			if (const auto it = boxes.constFind(holder); it != boxes.cend()) {
				LevelLinkedGroupBox& box = result[*it];
				if (!box.mins.valid) {
					box.mins = point;
					box.maxs = point;
				} else {
					box.mins = {std::min(box.mins.x, point.x), std::min(box.mins.y, point.y), std::min(box.mins.z, point.z), true};
					box.maxs = {std::max(box.maxs.x, point.x), std::max(box.maxs.y, point.y), std::max(box.maxs.z, point.z), true};
				}
			}
			holder = parents.value(holder);
		}
	};
	const auto holderOf = [&membership](const QString& object, int owner) {
		const QString holder = membership.value(object);
		return holder.isEmpty() && owner >= 0 ? membership.value(QStringLiteral("entity:%1").arg(owner)) : holder;
	};
	for (const LevelMapEntity& entity : std::as_const(document.entities)) {
		if (entity.origin.valid) {
			grow(membership.value(levelMapSelectionRefId({LevelMapSelectionKind::Entity, entity.id})), entity.origin);
		}
	}
	for (const MapBrushGeometry& geometry : brushes) {
		if (geometry.solved) {
			const QString holder = holderOf(levelMapSelectionRefId({LevelMapSelectionKind::QuakeBrush, geometry.brushId}), geometry.entityId);
			grow(holder, geometry.mins);
			grow(holder, geometry.maxs);
		}
	}
	for (const LevelMapPatch& patch : std::as_const(document.patches)) {
		const QString holder = holderOf(levelMapSelectionRefId({LevelMapSelectionKind::QuakePatch, patch.id}), patch.entityId);
		grow(holder, patch.mins);
		grow(holder, patch.maxs);
	}
	result.removeIf([](const LevelLinkedGroupBox& box) { return !box.mins.valid; });
	return result;
}

QSet<QString> levelLinkedGroupsInUse(const QVector<LevelLinkedGroupBox>& boxes, const QStringList& openGroups,
	const QHash<QString, QVector<LevelMapSelectionRef>>& groupSelections, const QVector<LevelMapSelectionRef>& selection)
{
	QSet<QString> inUse;
	if (boxes.isEmpty()) {
		return inUse;
	}
	QSet<QString> selected;
	for (const LevelMapSelectionRef& ref : selection) {
		selected.insert(levelMapSelectionRefId(ref));
	}
	for (const LevelLinkedGroupBox& box : boxes) {
		const auto members = groupSelections.value(box.id);
		if (openGroups.contains(box.id) || std::any_of(members.cbegin(), members.cend(), [&selected](const LevelMapSelectionRef& ref) {
				return selected.contains(levelMapSelectionRefId(ref));
			})) {
			inUse.insert(box.id);
		}
	}
	return inUse;
}

QSet<QString> levelLinkedSetsInUse(const QVector<LevelLinkedGroupBox>& boxes, const QSet<QString>& inUse)
{
	QSet<QString> links;
	for (const LevelLinkedGroupBox& box : boxes) {
		if (inUse.contains(box.id)) {
			links.insert(box.link);
		}
	}
	return links;
}

Qt::PenStyle levelLinkedGroupPenStyle(const LevelLinkedGroupBox& box, bool inUse)
{
	return box.outOfSync ? Qt::DashDotLine : (inUse ? Qt::SolidLine : Qt::DashLine);
}

QString levelLinkedGroupLabel(const LevelLinkedGroupBox& box)
{
	// Isolated, so a name in another script keeps its place in the sentence.
	return box.outOfSync
		? QCoreApplication::translate("VibeStudioLevelLinkedGroups", "%1, out of sync").arg(QChar(0x2068) + box.name + QChar(0x2069))
		: box.name;
}

QString levelLinkedGroupsDescription(const QVector<LevelLinkedGroupBox>& boxes, const QSet<QString>& inUse, bool showAll)
{
	QStringList sentences;
	if (!inUse.isEmpty()) {
		const QSet<QString> links = levelLinkedSetsInUse(boxes, inUse);
		int linked = 0;
		int outOfSync = 0;
		for (const LevelLinkedGroupBox& box : boxes) {
			if (links.contains(box.link)) {
				++linked;
				outOfSync += box.outOfSync ? 1 : 0;
			}
		}
		const int others = linked - static_cast<int>(inUse.size());
		sentences << QCoreApplication::translate("VibeStudioLevelLinkedGroups",
			"Arrows run from the selected linked group to the %n other group(s) it is linked with.", nullptr, others);
		if (outOfSync > 0) {
			sentences << QCoreApplication::translate("VibeStudioLevelLinkedGroups", "Out of sync: %n of them.", nullptr, outOfSync);
		}
	}
	if (showAll && !boxes.isEmpty()) {
		const int count = static_cast<int>(boxes.size());
		sentences << QCoreApplication::translate("VibeStudioLevelLinkedGroups", "Every linked group is boxed: %n in all.", nullptr, count);
	}
	return sentences.join(QLatin1Char(' '));
}

bool LevelLinkedGroupDisplay::drawsAnything() const
{
	return !boxes.isEmpty() && (showAll || !inUse.isEmpty());
}

bool operator==(const LevelLinkedGroupDisplay& left, const LevelLinkedGroupDisplay& right)
{
	return left.boxes == right.boxes && left.inUse == right.inUse && left.open == right.open && left.showAll == right.showAll;
}

LevelLinkedGroupDisplay levelLinkedGroupDisplay(const QVector<LevelLinkedGroupBox>& boxes, const QStringList& openGroups,
	const QHash<QString, QVector<LevelMapSelectionRef>>& groupSelections, const QVector<LevelMapSelectionRef>& selection, bool showAll)
{
	LevelLinkedGroupDisplay display;
	if (boxes.isEmpty()) {
		return display;
	}
	display.boxes = boxes;
	display.inUse = levelLinkedGroupsInUse(boxes, openGroups, groupSelections, selection);
	for (const QString& id : std::as_const(display.inUse)) {
		if (openGroups.contains(id)) {
			display.open.insert(id);
		}
	}
	display.showAll = showAll;
	return display;
}

} // namespace vibestudio
