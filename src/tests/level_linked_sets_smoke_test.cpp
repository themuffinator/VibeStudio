// Link sets beyond quarter turns: a linked group turned by any angle, mirrored
// or scaled whole keeps that frame and takes later edits in it; an edit inside
// an open linked group reaches the others even when it moves all of it; groups
// nest in linked groups and are linked with their copies; members changed in
// different ways at once, or locked, are marked out of sync and say so;
// Separate Linked Groups keeps several chosen members linked with each other;
// Select Linked Groups selects the whole set; and frames saved as quarter
// turns and mirrors by earlier versions still open.

#include "core/level_document.h"
#include "core/level_linked_groups.h"
#include "core/level_protected_properties.h"
#include "core/level_map.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>

using namespace vibestudio;

namespace {

bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		std::cerr << "FAIL: " << label << ": " << detail.toStdString() << '\n';
	}
	return value;
}

const LevelSceneNode* node(const LevelMapDocument& document, const QString& id)
{
	return levelSceneNode(document.scene, id);
}

// The brush of a group, nested groups included, whose first face has `texture`.
const LevelMapBrush* brushIn(const LevelMapDocument& document, const QString& group, const QString& texture)
{
	for (const QString& member : levelLinkedGroupMembers(document.scene, group)) {
		if (!member.startsWith(QStringLiteral("brush:"))) {
			continue;
		}
		const int id = member.section(QLatin1Char(':'), 1).toInt();
		for (const LevelMapBrush& brush : document.brushes) {
			if (brush.id == id && !brush.faces.isEmpty() && brush.faces.first().textureName == texture) {
				return &brush;
			}
		}
	}
	return nullptr;
}

const LevelMapEntity* entityIn(const LevelMapDocument& document, const QString& group)
{
	for (const QString& member : levelLinkedGroupMembers(document.scene, group)) {
		if (member.startsWith(QStringLiteral("entity:"))) {
			const int id = member.section(QLatin1Char(':'), 1).toInt();
			for (const LevelMapEntity& entity : document.entities) {
				if (entity.id == id) {
					return &entity;
				}
			}
		}
	}
	return nullptr;
}

QString property(const LevelMapEntity* entity, const QString& key)
{
	if (entity) {
		for (const LevelMapProperty& each : entity->properties) {
			if (each.key == key) {
				return each.value;
			}
		}
	}
	return {};
}

LevelMapVec3 centre(const LevelMapBrush* brush)
{
	return brush ? LevelMapVec3 {(brush->mins.x + brush->maxs.x) / 2, (brush->mins.y + brush->maxs.y) / 2, (brush->mins.z + brush->maxs.z) / 2, true}
				 : LevelMapVec3 {1e9, 1e9, 1e9, false};
}

bool near(double a, double b)
{
	return std::abs(a - b) < 0.01;
}

bool moved(const LevelMapVec3& before, const LevelMapVec3& after, double x, double y, double z)
{
	return near(after.x - before.x, x) && near(after.y - before.y, y) && near(after.z - before.z, z);
}

QString text(const LevelMapVec3& value)
{
	return QStringLiteral("%1 %2 %3").arg(value.x).arg(value.y).arg(value.z);
}

QVector<LevelMapSelectionRef> one(const LevelMapBrush* brush)
{
	return brush ? QVector<LevelMapSelectionRef> {{LevelMapSelectionKind::QuakeBrush, brush->id}} : QVector<LevelMapSelectionRef> {};
}

// An L of a long and a short block and a player start facing 90 degrees, as
// one group.
bool makeEll(LevelMapDocument* map, QString* group, QString* error)
{
	LevelMapCreateRequest create;
	create.game = QStringLiteral("quake");
	create.starterRoom = false;
	return createLevelMap(create, map, error)
		&& addLevelMapBoxBrush(map, {0, 0, 0, true}, {96, 32, 32, true}, QStringLiteral("studio/long"), nullptr, error)
		&& addLevelMapBoxBrush(map, {0, 32, 0, true}, {16, 64, 32, true}, QStringLiteral("studio/short"), nullptr, error)
		&& addLevelMapEntity(map, QStringLiteral("info_player_start"), {48, 16, 64, true}, {{QStringLiteral("angle"), QStringLiteral("90"), 0}},
			nullptr, error)
		&& createLevelSceneNode(map, LevelSceneNodeKind::Group, QStringLiteral("Ell"), {}, group, error)
		&& assignLevelSceneObjects(map, *group,
			{QStringLiteral("brush:%1").arg(map->brushes.at(map->brushes.size() - 2).id), QStringLiteral("brush:%1").arg(map->brushes.last().id),
				QStringLiteral("entity:%1").arg(map->entities.last().id)},
			error);
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true;
	QString error;

	// Any angle, and scaling: the group turned or scaled whole keeps that
	// frame, and edits to the other arrive in it.
	{
		LevelMapDocument map;
		QString a;
		ok &= expect(makeEll(&map, &a, &error), "ell", error);
		QString b;
		QString c;
		ok &= expect(createLinkedLevelGroup(&map, a, {256, 0, 0, true}, &b, &error)
				&& createLinkedLevelGroup(&map, a, {0, 512, 0, true}, &c, &error) && levelLinkedGroupNodes(map.scene, a).size() == 3,
			"three linked groups", error);
		const auto chosen = levelSceneSelection(map, c);
		ok &= expect(map.selection.size() == chosen.size()
				&& std::all_of(chosen.cbegin(), chosen.cend(), [&map](const LevelMapSelectionRef& ref) { return map.selection.contains(ref); }),
			"a new linked duplicate is selected");
		const LevelMapVec3 aLong = centre(brushIn(map, a, QStringLiteral("studio/long")));
		LevelMapRotationRequest turn;
		turn.axis = 2;
		turn.degrees = 30;
		turn.textureLock = false;
		ok &= expect(setLevelMapSelection(&map, levelSceneSelection(map, b), &error) && rotateLevelMapSelection(&map, turn, &error),
			"turn one group 30 degrees", error);
		ok &= expect(levelLinkFrameText(node(map, b)->linkFrame) == QStringLiteral("Turned 30 degrees")
				&& levelLinkFrameText(node(map, a)->linkFrame).isEmpty() && levelLinkFrameText(node(map, c)->linkFrame).isEmpty()
				&& moved(aLong, centre(brushIn(map, a, QStringLiteral("studio/long"))), 0, 0, 0),
			"a group turned whole by any angle turns alone", levelLinkFrameText(node(map, b)->linkFrame));
		ok &= expect(property(entityIn(map, b), QStringLiteral("angle")) == QStringLiteral("120"), "its entity turns with it",
			property(entityIn(map, b), QStringLiteral("angle")));
		LevelMapVec3 low;
		LevelMapVec3 high;
		ok &= expect(setLevelMapSelection(&map, levelSceneSelection(map, c), &error) && levelMapSelectionBounds(map, &low, &high)
				&& resizeLevelMapSelection(&map, low, {low.x + (high.x - low.x) * 2, high.y, high.z, true}, &error),
			"scale another group twice as wide", error);
		ok &= expect(levelLinkFrameText(node(map, c)->linkFrame) == QStringLiteral("Scaled or sheared") && node(map, c)->linkFrame[0] == 2.0,
			"a group scaled whole keeps the scale as its frame", levelLinkFrameText(node(map, c)->linkFrame));
		const LevelMapVec3 bShort = centre(brushIn(map, b, QStringLiteral("studio/short")));
		const LevelMapVec3 cShort = centre(brushIn(map, c, QStringLiteral("studio/short")));
		const LevelMapVec3 bLong = centre(brushIn(map, b, QStringLiteral("studio/long")));
		ok &= expect(setLevelMapSelection(&map, one(brushIn(map, a, QStringLiteral("studio/short"))), &error)
				&& moveLevelMapSelection(&map, 16, 16, 0, &error),
			"move one block of the first group", error);
		const double radians = 30.0 * std::numbers::pi / 180.0;
		ok &= expect(moved(bShort, centre(brushIn(map, b, QStringLiteral("studio/short"))), 16 * std::cos(radians) - 16 * std::sin(radians),
						 16 * std::sin(radians) + 16 * std::cos(radians), 0)
				&& moved(bLong, centre(brushIn(map, b, QStringLiteral("studio/long"))), 0, 0, 0),
			"the edit arrives turned 30 degrees", text(centre(brushIn(map, b, QStringLiteral("studio/short")))));
		ok &= expect(moved(cShort, centre(brushIn(map, c, QStringLiteral("studio/short"))), 32, 16, 0), "and scaled in the scaled group",
			text(centre(brushIn(map, c, QStringLiteral("studio/short")))));
		ok &= expect(setLevelMapEntityProperty(&map, entityIn(map, a)->id, QStringLiteral("angle"), QStringLiteral("0"), &error)
				&& property(entityIn(map, b), QStringLiteral("angle")) == QStringLiteral("30"),
			"a heading arrives turned too", property(entityIn(map, b), QStringLiteral("angle")));
		// Inside an open group every edit is to the shared content, even one
		// that moves all of it.
		const LevelMapVec3 bBefore = centre(brushIn(map, b, QStringLiteral("studio/long")));
		ok &= expect(openLevelSceneGroup(&map, a, &error) && setLevelMapSelection(&map, levelSceneSelection(map, a), &error)
				&& moveLevelMapSelection(&map, 0, 0, 8, &error) && closeLevelSceneGroup(&map, true, &error),
			"move everything inside the open group", error);
		ok &= expect(moved(bBefore, centre(brushIn(map, b, QStringLiteral("studio/long"))), 0, 0, 8)
				&& levelLinkFrameText(node(map, a)->linkFrame).isEmpty(),
			"the others' content moves too, and no frame changes", text(centre(brushIn(map, b, QStringLiteral("studio/long")))));
		// Frames are saved.
		LevelMapDocument again;
		ok &= expect(loadLevelMapBytes({QStringLiteral("frames.map"), {}, {}}, serializeLevelMap(map).bytes, &again, &error)
				&& node(again, b) && node(again, b)->linkFrame == node(map, b)->linkFrame && node(again, c)->linkFrame == node(map, c)->linkFrame,
			"frames save and load", error);
		// Select Linked Groups selects every member.
		const auto all = levelLinkedGroupsSelection(map, {a});
		ok &= expect(all.size() == 9, "Select Linked Groups selects all three groups", QString::number(all.size()));
	}

	// Duplicate on a linked group adds a member to its set, and the frame moves
	// with the duplicate: later edits land in its place, also after the source
	// was turned.
	{
		LevelMapDocument map;
		QString a;
		QString b;
		ok &= expect(makeEll(&map, &a, &error) && createLinkedLevelGroup(&map, a, {256, 0, 0, true}, &b, &error), "pair to duplicate", error);
		LevelMapRotationRequest turn;
		turn.axis = 2;
		turn.degrees = 90;
		turn.textureLock = false;
		ok &= expect(setLevelMapSelection(&map, levelSceneSelection(map, b), &error) && rotateLevelMapSelection(&map, turn, &error)
				&& setLevelMapSelection(&map, levelSceneSelection(map, b), &error) && duplicateLevelMapSelection(&map, 0, 512, 0, &error),
			"turn the linked duplicate, then duplicate it", error);
		QString d;
		for (const QString& id : levelLinkedGroupNodes(map.scene, a)) {
			d = id != a && id != b ? id : d;
		}
		const auto frame = [&map](const QString& id) { return node(map, id)->linkFrame; };
		ok &= expect(!d.isEmpty() && levelLinkedGroupNodes(map.scene, a).size() == 3
				&& levelLinkFrameText(frame(d)) == QStringLiteral("Turned 90 degrees") && near(frame(d)[3], frame(b)[3])
				&& near(frame(d)[7], frame(b)[7] + 512) && near(frame(d)[11], frame(b)[11]),
			"Duplicate adds a member whose frame moved with it", error);
		const LevelMapVec3 bShort = centre(brushIn(map, b, QStringLiteral("studio/short")));
		const LevelMapVec3 dShort = centre(brushIn(map, d, QStringLiteral("studio/short")));
		ok &= expect(setLevelMapSelection(&map, one(brushIn(map, a, QStringLiteral("studio/short"))), &error)
				&& moveLevelMapSelection(&map, 8, 0, 0, &error)
				&& moved(bShort, centre(brushIn(map, b, QStringLiteral("studio/short"))), 0, 8, 0)
				&& moved(dShort, centre(brushIn(map, d, QStringLiteral("studio/short"))), 0, 8, 0),
			"an edit to the source lands turned in both, each in its place", text(centre(brushIn(map, d, QStringLiteral("studio/short")))));
	}

	// Members changed in different ways at once are out of sync; changed the
	// same way, they are not; a locked one is passed by and says so.
	{
		LevelMapDocument map;
		QString a;
		QString b;
		ok &= expect(makeEll(&map, &a, &error) && createLinkedLevelGroup(&map, a, {256, 0, 0, true}, &b, &error), "pair", error);
		ok &= expect(replaceLevelMapTexture(&map, QStringLiteral("studio/long"), QStringLiteral("studio/wall"), false, nullptr, &error)
				&& map.linkedGroupNotices.isEmpty() && !node(map, a)->linkOutOfSync && !node(map, b)->linkOutOfSync
				&& brushIn(map, b, QStringLiteral("studio/wall")),
			"the same change to both keeps them in sync", map.linkedGroupNotices.join(QLatin1Char(' ')));
		QVector<LevelMapSelectionRef> both = one(brushIn(map, a, QStringLiteral("studio/short")));
		both += one(brushIn(map, b, QStringLiteral("studio/wall")));
		ok &= expect(setLevelMapSelection(&map, both, &error) && moveLevelMapSelection(&map, 0, 0, 8, &error), "change both differently", error);
		ok &= expect(node(map, a)->linkOutOfSync && node(map, b)->linkOutOfSync && map.linkedGroupNotices.size() == 1
				&& near(centre(brushIn(map, a, QStringLiteral("studio/short"))).z, 24) && near(centre(brushIn(map, b, QStringLiteral("studio/wall"))).z, 24),
			"both edits stand and the set is out of sync", map.linkedGroupNotices.join(QLatin1Char(' ')));
		const auto warnings = serializeLevelMap(map).warnings;
		ok &= expect(warnings.size() == 2, "saving names the groups out of sync", warnings.join(QLatin1Char(' ')));
		map.linkedGroupNotices.clear();
		int updated = 0;
		ok &= expect(updateLinkedLevelGroups(&map, a, &updated, &error) && updated == 1 && !node(map, a)->linkOutOfSync
				&& !node(map, b)->linkOutOfSync && near(centre(brushIn(map, b, QStringLiteral("studio/short"))).z, 24)
				&& near(centre(brushIn(map, b, QStringLiteral("studio/wall"))).z, 16),
			"Update Linked Groups copies one over the rest", error);
		ok &= expect(undoLevelMapEdit(&map, &error) && node(map, b)->linkOutOfSync, "one undo restores the out-of-sync state", error);
		ok &= expect(redoLevelMapEdit(&map, &error) && !node(map, b)->linkOutOfSync, "redo updates again", error);
		ok &= expect(setLevelSceneLocked(&map, b, true, &error) && setLevelMapSelection(&map, one(brushIn(map, a, QStringLiteral("studio/short"))), &error)
				&& moveLevelMapSelection(&map, 8, 0, 0, &error),
			"edit beside a locked group", error);
		ok &= expect(node(map, b)->linkOutOfSync && !node(map, a)->linkOutOfSync && map.linkedGroupNotices.size() == 1
				&& map.linkedGroupNotices.first().contains(QStringLiteral("locked")),
			"a locked group keeps its content and is out of sync", map.linkedGroupNotices.join(QLatin1Char(' ')));
		map.linkedGroupNotices.clear();
		ok &= expect(setLevelSceneLocked(&map, b, false, &error) && setLevelMapSelection(&map, one(brushIn(map, a, QStringLiteral("studio/short"))), &error)
				&& moveLevelMapSelection(&map, -8, 0, 0, &error) && !node(map, b)->linkOutOfSync
				&& near(centre(brushIn(map, b, QStringLiteral("studio/short"))).x, centre(brushIn(map, a, QStringLiteral("studio/short"))).x + 256),
			"unlocked, the next edit brings it back in sync", error);
	}

	// Separate Linked Groups: several chosen stay linked with each other; one
	// left on its own is no longer linked; all of a set cannot be separated.
	{
		LevelMapDocument map;
		QString a;
		QString b;
		QString c;
		ok &= expect(makeEll(&map, &a, &error) && createLinkedLevelGroup(&map, a, {256, 0, 0, true}, &b, &error)
				&& createLinkedLevelGroup(&map, a, {512, 0, 0, true}, &c, &error),
			"three linked groups to separate", error);
		const QString link = node(map, a)->linkId;
		int separated = 0;
		ok &= expect(separateLinkedLevelGroups(&map, {b, c}, &separated, &error) && separated == 2 && node(map, a)->linkId.isEmpty()
				&& !node(map, b)->linkId.isEmpty() && node(map, b)->linkId == node(map, c)->linkId && node(map, b)->linkId != link,
			"two separated together stay linked; the one left is not", error);
		ok &= expect(!separateLinkedLevelGroups(&map, {b, c}, nullptr, &error) && !error.isEmpty(), "a whole set cannot be separated", error);
		ok &= expect(setLevelMapSelection(&map, one(brushIn(map, b, QStringLiteral("studio/short"))), &error)
				&& moveLevelMapSelection(&map, 0, 8, 0, &error)
				&& near(centre(brushIn(map, c, QStringLiteral("studio/short"))).y - centre(brushIn(map, a, QStringLiteral("studio/short"))).y, 8),
			"the new set still shares edits, apart from the old", error);
	}

	// Nesting: groups in a linked group are copied and linked with their
	// copies; an edit inside one reaches every copy; a group cannot hold a
	// group it is linked with.
	{
		LevelMapCreateRequest create;
		create.game = QStringLiteral("quake");
		create.starterRoom = false;
		LevelMapDocument map;
		QString outer;
		QString inner;
		int outerBrush = -1;
		int innerBrush = -1;
		ok &= expect(createLevelMap(create, &map, &error)
				&& addLevelMapBoxBrush(&map, {0, 0, 0, true}, {64, 64, 16, true}, QStringLiteral("studio/outer"), &outerBrush, &error)
				&& addLevelMapBoxBrush(&map, {16, 16, 16, true}, {48, 48, 48, true}, QStringLiteral("studio/inner"), &innerBrush, &error)
				&& createLevelSceneNode(&map, LevelSceneNodeKind::Group, QStringLiteral("Outer"), {}, &outer, &error)
				&& createLevelSceneNode(&map, LevelSceneNodeKind::Group, QStringLiteral("Inner"), outer, &inner, &error)
				&& assignLevelSceneObjects(&map, outer, {QStringLiteral("brush:%1").arg(outerBrush)}, &error)
				&& assignLevelSceneObjects(&map, inner, {QStringLiteral("brush:%1").arg(innerBrush)}, &error),
			"a group nested in a group", error);
		// A group made before its parent sits ahead of it in scene order.
		QString deep;
		QString middle;
		int deepBrush = -1;
		ok &= expect(addLevelMapBoxBrush(&map, {24, 24, 48, true}, {40, 40, 56, true}, QStringLiteral("studio/deep"), &deepBrush, &error)
				&& createLevelSceneNode(&map, LevelSceneNodeKind::Group, QStringLiteral("Deep"), {}, &deep, &error)
				&& assignLevelSceneObjects(&map, deep, {QStringLiteral("brush:%1").arg(deepBrush)}, &error)
				&& createLevelSceneNode(&map, LevelSceneNodeKind::Group, QStringLiteral("Middle"), inner, &middle, &error)
				&& reparentLevelSceneNode(&map, deep, middle, &error),
			"a group three levels down, ahead of its parent", error);
		QString outerCopy;
		ok &= expect(createLinkedLevelGroup(&map, outer, {256, 0, 0, true}, &outerCopy, &error), "link the outer group", error);
		const QStringList nested = levelLinkedGroupDescendants(map.scene, outerCopy);
		ok &= expect(nested.size() == 3 && !node(map, inner)->linkId.isEmpty() && node(map, nested.first())->linkId == node(map, inner)->linkId
				&& node(map, nested.at(1))->linkId == node(map, middle)->linkId && node(map, nested.at(1))->parentId == nested.first()
				&& node(map, nested.last())->linkId == node(map, deep)->linkId && node(map, nested.last())->parentId == nested.at(1)
				&& node(map, outerCopy)->linkId == node(map, outer)->linkId && brushIn(map, outerCopy, QStringLiteral("studio/inner"))
				&& brushIn(map, outerCopy, QStringLiteral("studio/deep")),
			"nested groups are copied, each under its own parent's copy, and linked with their copies", error);
		ok &= expect(setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, innerBrush}}, &error)
				&& applyLevelMapTexture(&map, QStringLiteral("studio/changed"), nullptr, &error)
				&& brushIn(map, outerCopy, QStringLiteral("studio/changed")) && !brushIn(map, outerCopy, QStringLiteral("studio/inner")),
			"an edit inside the nested group reaches its copy", error);
		QString third;
		ok &= expect(createLinkedLevelGroup(&map, inner, {0, 128, 0, true}, &third, &error) && node(map, third)->parentId == outer
				&& levelLinkedGroupNodes(map.scene, inner).size() == 4 && levelLinkedGroupNodes(map.scene, deep).size() == 4
				&& levelLinkedGroupDescendants(map.scene, outerCopy).size() == 6,
			"a linked duplicate made inside a linked group reaches its copies too",
			QString::number(levelLinkedGroupNodes(map.scene, inner).size()));
		ok &= expect(!reparentLevelSceneNode(&map, outerCopy, inner, &error), "a group cannot hold a group it is linked with", error);
		LevelMapDocument again;
		ok &= expect(loadLevelMapBytes({QStringLiteral("nested.map"), {}, {}}, serializeLevelMap(map).bytes, &again, &error)
				&& again.scene.problem.isEmpty() && levelLinkedGroupNodes(again.scene, inner).size() == 4
				&& levelLinkedGroupNodes(again.scene, outer).size() == 2,
			"nested links save and load", error + again.scene.problem);
		ok &= expect(undoLevelMapEdit(&map, &error) && levelLinkedGroupNodes(map.scene, inner).size() == 2
				&& levelLinkedGroupDescendants(map.scene, outerCopy).size() == 3,
			"one undo removes the nested duplicate and its copies", error);
	}

	// Two members of one set inside each member of an outer set: an edit to one
	// reaches all four, the outer update's copies included, and later edits do
	// too; two members changed the same way at once agree whatever their
	// protected values.
	{
		const auto build = [&error](LevelMapDocument* map, QString* outer, QString* inner, QString* innerCopy, int* innerBrush, int* marker) {
			LevelMapCreateRequest create;
			create.game = QStringLiteral("quake");
			create.starterRoom = false;
			int outerBrush = -1;
			QString outerCopy;
			return createLevelMap(create, map, &error)
				&& addLevelMapBoxBrush(map, {0, 0, 0, true}, {64, 64, 16, true}, QStringLiteral("studio/outer"), &outerBrush, &error)
				&& addLevelMapBoxBrush(map, {16, 16, 16, true}, {48, 48, 48, true}, QStringLiteral("studio/inner"), innerBrush, &error)
				&& addLevelMapEntity(map, QStringLiteral("info_null"), {32, 32, 64, true}, {}, marker, &error)
				&& createLevelSceneNode(map, LevelSceneNodeKind::Group, QStringLiteral("Outer"), {}, outer, &error)
				&& createLevelSceneNode(map, LevelSceneNodeKind::Group, QStringLiteral("Inner"), *outer, inner, &error)
				&& assignLevelSceneObjects(map, *outer, {QStringLiteral("brush:%1").arg(outerBrush)}, &error)
				&& assignLevelSceneObjects(map, *inner, {QStringLiteral("brush:%1").arg(*innerBrush), QStringLiteral("entity:%1").arg(*marker)}, &error)
				&& createLinkedLevelGroup(map, *inner, {0, 128, 0, true}, innerCopy, &error)
				&& createLinkedLevelGroup(map, *outer, {256, 0, 0, true}, &outerCopy, &error);
		};
		const auto members = [](const LevelMapDocument& map, const QString& link) {
			QStringList result;
			for (const LevelSceneNode& each : map.scene.nodes) {
				if (each.linkId == link) {
					result << each.id;
				}
			}
			return result;
		};
		// How many members hold `texture`, and how many are out of sync.
		const auto count = [&members](const LevelMapDocument& map, const QString& link, const QString& texture, bool outOfSync) {
			const QStringList ids = members(map, link);
			return static_cast<int>(std::count_if(ids.cbegin(), ids.cend(), [&](const QString& id) {
				return outOfSync ? node(map, id)->linkOutOfSync : brushIn(map, id, texture) != nullptr;
			}));
		};
		LevelMapDocument map;
		QString outer;
		QString inner;
		QString innerCopy;
		int innerBrush = -1;
		int marker = -1;
		ok &= expect(build(&map, &outer, &inner, &innerCopy, &innerBrush, &marker), "two linked groups in each of two linked groups", error);
		const QString link = node(map, inner)->linkId;
		ok &= expect(members(map, link).size() == 4 && levelLinkedGroupNodes(map.scene, outer).size() == 2, "four members of the inner set");
		ok &= expect(setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, innerBrush}}, &error)
				&& applyLevelMapTexture(&map, QStringLiteral("studio/changed"), nullptr, &error),
			"edit one of the four", error);
		ok &= expect(count(map, link, QStringLiteral("studio/changed"), false) == 4 && count(map, link, {}, true) == 0
				&& map.linkedGroupNotices.isEmpty(),
			"every member takes the edit, the outer update's copies too", map.linkedGroupNotices.join(QLatin1Char(' ')));
		ok &= expect(undoLevelMapEdit(&map, &error) && count(map, link, QStringLiteral("studio/inner"), false) == 4,
			"one undo restores all four", error);
		ok &= expect(redoLevelMapEdit(&map, &error) && count(map, link, QStringLiteral("studio/changed"), false) == 4
				&& count(map, link, {}, true) == 0,
			"redo brings the edit back to all four", error);
		QString second;
		for (const QString& id : members(map, link)) {
			second = node(map, id)->parentId != outer ? id : second;
		}
		ok &= expect(setLevelMapSelection(&map, one(brushIn(map, second, QStringLiteral("studio/changed"))), &error)
				&& applyLevelMapTexture(&map, QStringLiteral("studio/again"), nullptr, &error)
				&& count(map, link, QStringLiteral("studio/again"), false) == 4 && count(map, link, {}, true) == 0
				&& map.linkedGroupNotices.isEmpty(),
			"an edit to a member in the other outer group reaches all four too", map.linkedGroupNotices.join(QLatin1Char(' ')));
		LevelMapDocument local;
		ok &= expect(build(&local, &outer, &inner, &innerCopy, &innerBrush, &marker)
				&& setLevelProtectedEntityProperty(&local, {marker}, QStringLiteral("message"), {QStringLiteral("here")}, &error),
			"a protected value in one member", error);
		const LevelMapEntity* copied = entityIn(local, innerCopy);
		ok &= expect(copied && setLevelProtectedEntityProperty(&local, {copied->id}, QStringLiteral("message"), {QStringLiteral("there")}, &error),
			"another in its neighbour", error);
		const QString localLink = node(local, inner)->linkId;
		QVector<LevelMapSelectionRef> both = one(brushIn(local, inner, QStringLiteral("studio/inner")));
		both += one(brushIn(local, innerCopy, QStringLiteral("studio/inner")));
		ok &= expect(local.linkedGroupNotices.isEmpty() && both.size() == 2 && setLevelMapSelection(&local, both, &error)
				&& moveLevelMapSelection(&local, 0, 0, 8, &error),
			"move both members' blocks the same way at once", error);
		const QStringList ids = members(local, localLink);
		ok &= expect(count(local, localLink, {}, true) == 0 && local.linkedGroupNotices.isEmpty()
				&& std::all_of(ids.cbegin(), ids.cend(), [&local](const QString& id) {
					   return near(centre(brushIn(local, id, QStringLiteral("studio/inner"))).z, 40);
				   })
				&& property(entityIn(local, inner), QStringLiteral("message")) == QStringLiteral("here")
				&& property(entityIn(local, innerCopy), QStringLiteral("message")) == QStringLiteral("there"),
			"the same change agrees whatever the protected values", local.linkedGroupNotices.join(QLatin1Char(' ')));
		// What a locked group holds is copied like anything else: the outer
		// update reaches the other outer group, while the locked member and the
		// copy of it that the update made keep their content, out of sync.
		LevelMapDocument guarded;
		ok &= expect(build(&guarded, &outer, &inner, &innerCopy, &innerBrush, &marker) && setLevelSceneLocked(&guarded, innerCopy, true, &error),
			"lock one member inside the outer group", error);
		const QString guardedLink = node(guarded, inner)->linkId;
		const QString outerLink = node(guarded, outer)->linkId;
		ok &= expect(setLevelMapSelection(&guarded, {{LevelMapSelectionKind::QuakeBrush, innerBrush}}, &error)
				&& applyLevelMapTexture(&guarded, QStringLiteral("studio/changed"), nullptr, &error),
			"edit beside the locked member", error);
		ok &= expect(count(guarded, guardedLink, QStringLiteral("studio/changed"), false) == 2
				&& count(guarded, guardedLink, QStringLiteral("studio/inner"), false) == 2 && count(guarded, guardedLink, {}, true) == 2
				&& node(guarded, innerCopy)->locked && node(guarded, innerCopy)->linkOutOfSync
				&& brushIn(guarded, innerCopy, QStringLiteral("studio/inner")) && count(guarded, outerLink, {}, true) == 0
				&& guarded.linkedGroupNotices.size() == 1 && guarded.linkedGroupNotices.first().contains(QStringLiteral("locked")),
			"the outer update copies the locked member, which keeps its content with its copy, out of sync",
			guarded.linkedGroupNotices.join(QLatin1Char(' ')));
		guarded.linkedGroupNotices.clear();
		ok &= expect(setLevelSceneLocked(&guarded, innerCopy, false, &error)
				&& setLevelMapSelection(&guarded, {{LevelMapSelectionKind::QuakeBrush, innerBrush}}, &error)
				&& applyLevelMapTexture(&guarded, QStringLiteral("studio/again"), nullptr, &error)
				&& count(guarded, guardedLink, QStringLiteral("studio/again"), false) == 4 && count(guarded, guardedLink, {}, true) == 0,
			"unlocked, the next edit brings all four back in sync", error);
		// Update Linked Groups and Create Linked Duplicate copy it too.
		int updated = 0;
		ok &= expect(setLevelSceneLocked(&guarded, innerCopy, true, &error) && updateLinkedLevelGroups(&guarded, outer, &updated, &error)
				&& updated == 1 && count(guarded, outerLink, {}, true) == 0 && node(guarded, innerCopy)->locked,
			"Update Linked Groups copies an outer group holding a locked one", error);
		QString third;
		ok &= expect(createLinkedLevelGroup(&guarded, outer, {512, 0, 0, true}, &third, &error)
				&& levelLinkedGroupNodes(guarded.scene, outer).size() == 3 && members(guarded, guardedLink).size() == 6
				&& node(guarded, innerCopy)->locked && brushIn(guarded, innerCopy, QStringLiteral("studio/again")),
			"Create Linked Duplicate copies an outer group holding a locked one", error);
		ok &= expect(undoLevelMapEdit(&guarded, &error) && levelLinkedGroupNodes(guarded.scene, outer).size() == 2
				&& members(guarded, guardedLink).size() == 4,
			"one undo removes that duplicate", error);
	}

	// Frames saved by earlier versions as quarter turns and a mirror.
	{
		LevelMapDocument map;
		QString a;
		QString b;
		ok &= expect(makeEll(&map, &a, &error) && createLinkedLevelGroup(&map, a, {256, 0, 0, true}, &b, &error), "pair to save", error);
		QByteArray bytes = serializeLevelMap(map).bytes;
		const QByteArray marker("// VibeStudioScene: ");
		const qsizetype at = bytes.lastIndexOf(marker);
		QJsonObject json = QJsonDocument::fromJson(QByteArray::fromBase64(bytes.mid(at + marker.size()).trimmed())).object();
		json.insert(QStringLiteral("version"), 5);
		QJsonArray nodes = json.value(QStringLiteral("nodes")).toArray();
		for (auto it = nodes.begin(); it != nodes.end(); ++it) {
			QJsonObject each = it->toObject();
			const bool copy = each.value(QStringLiteral("id")).toString() == b;
			each.remove(QStringLiteral("frame"));
			each.remove(QStringLiteral("outOfSync"));
			each.insert(QStringLiteral("turn"), copy ? 1 : 0);
			each.insert(QStringLiteral("mirror"), copy);
			*it = each;
		}
		json.insert(QStringLiteral("nodes"), nodes);
		bytes = bytes.left(at + marker.size()) + QJsonDocument(json).toJson(QJsonDocument::Compact).toBase64() + '\n';
		LevelMapDocument legacy;
		ok &= expect(loadLevelMapBytes({QStringLiteral("legacy.map"), {}, {}}, bytes, &legacy, &error) && legacy.scene.problem.isEmpty(),
			"a version 5 scene opens", error + legacy.scene.problem);
		// Mirrored, (x, y) -> (x, -y), then turned a quarter: (x, y) -> (y, x),
		// placed at the centre of the group's content.
		const auto anchor = levelLinkedGroupContent(legacy, b).anchor;
		ok &= expect(node(legacy, b)
				&& node(legacy, b)->linkFrame == std::array<double, 12> {0, 1, 0, anchor.x, 1, 0, 0, anchor.y, 0, 0, 1, anchor.z},
			"a version 5 mirror and quarter turn read as one frame at the content's centre");
		ok &= expect(setLevelMapSelection(&legacy, one(brushIn(legacy, a, QStringLiteral("studio/short"))), &error)
				&& moveLevelMapSelection(&legacy, 8, 0, 0, &error),
			"edit a legacy linked group", error);
		// The short block sits in the copy as in the original, x and y swapped.
		const auto aAnchor = levelLinkedGroupContent(legacy, a).anchor;
		const auto bAnchor = levelLinkedGroupContent(legacy, b).anchor;
		const auto aShort = centre(brushIn(legacy, a, QStringLiteral("studio/short")));
		const auto bShort = centre(brushIn(legacy, b, QStringLiteral("studio/short")));
		ok &= expect(near(bShort.x - bAnchor.x, aShort.y - aAnchor.y) && near(bShort.y - bAnchor.y, aShort.x - aAnchor.x),
			"edits to a legacy set arrive mirrored and turned", text(bShort));
	}

	if (ok) {
		std::cout << "Level link sets: frames, nesting, out of sync and separation passed.\n";
	}
	return ok ? 0 : 1;
}
