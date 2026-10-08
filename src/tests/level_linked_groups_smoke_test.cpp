// Linked groups: a copy made with createLinkedLevelGroup takes on every edit
// to its original's content, and the original every edit to the copy's, each
// keeping its place; moving a whole copy moves only it; each edit and its
// propagation are one undo step; links survive saving; locked copies are left
// alone; copies changed in different ways at once are left for Update Linked
// Copies; and the CLI's editor scene link, update-links and unlink do the same.
// With the CLI path as the argument the CLI is run too.

#include "core/level_document.h"
#include "core/level_linked_groups.h"
#include "core/level_map.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_surface.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointF>
#include <QProcess>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>

using namespace vibestudio;

namespace {

bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		std::cerr << "FAIL: " << label << ": " << detail.toStdString() << '\n';
	}
	return value;
}

const LevelMapBrush* brushById(const LevelMapDocument& document, int id)
{
	for (const LevelMapBrush& brush : document.brushes) {
		if (brush.id == id) {
			return &brush;
		}
	}
	return nullptr;
}

const LevelMapEntity* entityById(const LevelMapDocument& document, int id)
{
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.id == id) {
			return &entity;
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

int idOf(const QStringList& members, const QString& kind)
{
	for (const QString& member : members) {
		if (member.startsWith(kind + QLatin1Char(':'))) {
			return member.section(QLatin1Char(':'), 1).toInt();
		}
	}
	return -1;
}

const LevelSceneNode* node(const LevelMapDocument& document, const QString& id)
{
	return levelSceneNode(document.scene, id);
}

QString vec(const LevelMapVec3& value)
{
	return QStringLiteral("%1 %2 %3").arg(value.x).arg(value.y).arg(value.z);
}

bool near(double a, double b)
{
	return std::abs(a - b) < 0.001;
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true;
	QString error;

	// A floor outside any group, and a pillar group of a brush and a light.
	LevelMapCreateRequest create;
	create.game = QStringLiteral("quake");
	create.starterRoom = false;
	LevelMapDocument document;
	ok &= expect(createLevelMap(create, &document, &error), "map", error);
	int floor = -1;
	int pillar = -1;
	int light = -1;
	ok &= expect(addLevelMapBoxBrush(&document, {-512, -512, -16, true}, {512, 512, 0, true}, QStringLiteral("studio/floor"), &floor, &error),
		"floor", error);
	ok &= expect(addLevelMapBoxBrush(&document, {0, 0, 0, true}, {32, 32, 128, true}, QStringLiteral("studio/pillar"), &pillar, &error),
		"pillar", error);
	ok &= expect(addLevelMapEntity(&document, QStringLiteral("light"), {16, 16, 160, true}, {{QStringLiteral("light"), QStringLiteral("200"), 0}},
					 &light, &error),
		"light", error);
	QString group;
	ok &= expect(createLevelSceneNode(&document, LevelSceneNodeKind::Group, QStringLiteral("Pillar"), {}, &group, &error), "group", error);
	ok &= expect(assignLevelSceneObjects(&document, group, {QStringLiteral("brush:%1").arg(pillar), QStringLiteral("entity:%1").arg(light)}, &error),
		"assign", error);
	if (!ok) {
		return 1;
	}

	// A linked copy 128 units along x: one undo step, a sibling named after it.
	const int steps = static_cast<int>(document.undoStack.size());
	const int brushes = static_cast<int>(document.brushes.size());
	QString copy;
	ok &= expect(createLinkedLevelGroup(&document, group, {128, 0, 0, true}, &copy, &error), "linked copy", error);
	const LevelSceneNode* original = node(document, group);
	const LevelSceneNode* copied = node(document, copy);
	ok &= expect(original && copied && !original->linkId.isEmpty() && original->linkId == copied->linkId && copied->name == QStringLiteral("Pillar 2")
			&& copied->objects.size() == 2 && document.brushes.size() == brushes + 1 && document.undoStack.size() == steps + 1,
		"the copy is a linked sibling holding a copy of each member", copied ? copied->objects.join(QLatin1Char(',')) : QString());
	if (!ok) {
		return 1;
	}
	// The copy's records are rebuilt as edits reach it, so they are found
	// through its group each time.
	const auto copyBrush = [&]() { return brushById(document, idOf(node(document, copy)->objects, QStringLiteral("brush"))); };
	const auto copyEntity = [&]() { return entityById(document, idOf(node(document, copy)->objects, QStringLiteral("entity"))); };
	// Edits to a copy rebuild the others, so the original is found the same way.
	const auto originalBrush = [&]() { return brushById(document, idOf(node(document, group)->objects, QStringLiteral("brush"))); };
	const auto originalEntity = [&]() { return entityById(document, idOf(node(document, group)->objects, QStringLiteral("entity"))); };
	ok &= expect(copyBrush() && near(copyBrush()->mins.x, 128) && copyEntity() && near(copyEntity()->origin.x, 144),
		"the copy sits 128 units along", copyBrush() ? vec(copyBrush()->mins) : QString());
	ok &= expect(levelLinkedGroupNodes(document.scene, group) == QStringList({group, copy}), "the link lists both copies in scene order");
	ok &= expect(undoLevelMapEdit(&document, &error) && document.scene.nodes.size() == 1 && document.brushes.size() == brushes
			&& node(document, group)->linkId.isEmpty(),
		"undo takes the copy and the link back", error);
	ok &= expect(redoLevelMapEdit(&document, &error) && document.scene.nodes.size() == 2 && node(document, copy), "redo links it again", error);

	// Raising the original's pillar raises the copy's, inside the same undo step.
	const int before = static_cast<int>(document.undoStack.size());
	ok &= expect(moveLevelMapObject(&document, QStringLiteral("brush"), originalBrush()->id, 0, 0, 16, &error), "move the pillar", error);
	ok &= expect(copyBrush() && near(copyBrush()->mins.z, 16) && near(copyBrush()->mins.x, 128),
		"an edit inside one copy reaches the other, in its own place", copyBrush() ? vec(copyBrush()->mins) : QString());
	ok &= expect(document.undoStack.size() == before + 1 && !document.undoStack.last().followers.empty()
			&& document.undoStack.last().commandKind != QStringLiteral("batch"),
		"the edit and its propagation are one undo step, still the edit's own", QString::number(document.undoStack.size() - before));
	ok &= expect(undoLevelMapEdit(&document, &error) && copyBrush() && near(copyBrush()->mins.z, 0) && near(originalBrush()->mins.z, 0),
		"undo lowers both pillars", error);
	ok &= expect(redoLevelMapEdit(&document, &error) && copyBrush() && near(copyBrush()->mins.z, 16), "redo raises both again", error);

	// Moving the whole copy moves only it, and later edits keep it there.
	QVector<LevelMapSelectionRef> copyMembers;
	for (const QString& member : node(document, copy)->objects) {
		copyMembers.push_back({levelMapSelectionKindFromId(member.section(QLatin1Char(':'), 0, 0)), member.section(QLatin1Char(':'), 1).toInt()});
	}
	ok &= expect(setLevelMapSelection(&document, copyMembers, &error) && moveLevelMapSelection(&document, 0, 64, 0, &error), "move the copy", error);
	ok &= expect(near(originalBrush()->mins.y, 0) && copyBrush() && near(copyBrush()->mins.y, 64),
		"a whole copy moved leaves the original where it was");
	ok &= expect(setLevelMapEntityProperty(&document, originalEntity()->id, QStringLiteral("light"), QStringLiteral("300"), &error), "light level", error);
	ok &= expect(property(copyEntity(), QStringLiteral("light")) == QStringLiteral("300") && copyBrush() && near(copyBrush()->mins.y, 64)
			&& near(copyBrush()->mins.x, 128),
		"a key edit reaches the moved copy, which stays where it was put", property(copyEntity(), QStringLiteral("light")));

	// An edit to the copy reaches the original just the same.
	ok &= expect(setLevelMapEntityProperty(&document, copyEntity()->id, QStringLiteral("style"), QStringLiteral("2"), &error), "style", error);
	ok &= expect(property(originalEntity(), QStringLiteral("style")) == QStringLiteral("2")
			&& near(originalBrush()->mins.y, 0),
		"the original takes on the copy's edit in its own place");

	// A texture shift, with no change of shape, is copied too: each face of the
	// copy moves its texture as far as the original's face did.
	QVector<double> originalShifts;
	QVector<double> copyShifts;
	for (const LevelMapBrushFace& face : originalBrush()->faces) {
		originalShifts << face.shiftX;
	}
	for (const LevelMapBrushFace& face : copyBrush()->faces) {
		copyShifts << face.shiftX;
	}
	LevelSurfaceRequest shift;
	shift.operation = LevelSurfaceOperation::Shift;
	shift.x = 8;
	shift.y = 0;
	QVector<LevelSurfaceFace> faces;
	for (int index = 0; index < originalBrush()->faces.size(); ++index) {
		faces.push_back({originalBrush()->id, index});
	}
	LevelSurfaceEditPlan plan;
	ok &= expect(prepareLevelSurfaceEdit(document, faces, shift, {{QStringLiteral("studio/pillar"), QSize(64, 64)}}, &plan, &error)
			&& commitLevelSurfaceEdit(&document, plan, &error),
		"shift the pillar's texture", error);
	{
		const auto wrap = [](double value) { return std::fmod(std::fmod(value, 64.0) + 64.0, 64.0); };
		QStringList differences;
		const LevelMapBrush* shifted = originalBrush();
		const LevelMapBrush* follower = copyBrush();
		for (int index = 0; shifted && follower && index < shifted->faces.size() && index < follower->faces.size(); ++index) {
			const double moved = wrap(shifted->faces.at(index).shiftX - originalShifts.value(index));
			const double followed = wrap(follower->faces.at(index).shiftX - copyShifts.value(index));
			if (!near(moved, followed)) {
				differences << QStringLiteral("face %1: %2 vs %3").arg(index).arg(moved).arg(followed);
			}
		}
		ok &= expect(shifted && follower && differences.isEmpty() && !originalShifts.isEmpty(), "a texture shift reaches the copy",
			differences.join(QStringLiteral("; ")));
		ok &= expect(node(document, copy)->linkSurface == levelLinkedContent(document, node(document, copy)->objects).surface,
			"the copy's texture digest is kept up to date");
	}

	// Deleting the original's light deletes the copy's.
	ok &= expect(deleteLevelMapObjects(&document, {{LevelMapSelectionKind::Entity, originalEntity()->id}}, &error), "delete the light", error);
	ok &= expect(node(document, copy)->objects.size() == 1 && idOf(node(document, copy)->objects, QStringLiteral("entity")) < 0,
		"a deletion in one copy deletes in the others", node(document, copy)->objects.join(QLatin1Char(',')));
	ok &= expect(undoLevelMapEdit(&document, &error) && node(document, copy)->objects.size() == 2 && originalEntity(),
		"undo brings both lights back", error);

	// The link survives saving and opening, and still works.
	const QByteArray saved = serializeLevelMap(document).bytes;
	LevelMapDocument reopened;
	ok &= expect(loadLevelMapBytes({QStringLiteral("linked.map"), {}, {}}, saved, &reopened, &error) && reopened.scene.problem.isEmpty(),
		"reopen", error + reopened.scene.problem);
	ok &= expect(saved.contains("VibeStudioScene") && levelLinkedGroupNodes(reopened.scene, group).size() == 2
			&& !node(reopened, group)->linkShape.isEmpty(),
		"links are saved and compared again on opening");
	const int reopenedLight = idOf(node(reopened, group)->objects, QStringLiteral("entity"));
	ok &= expect(setLevelMapEntityProperty(&reopened, reopenedLight, QStringLiteral("light"), QStringLiteral("150"), &error)
			&& property(entityById(reopened, idOf(node(reopened, copy)->objects, QStringLiteral("entity"))), QStringLiteral("light"))
				== QStringLiteral("150"),
		"after opening, edits still reach every copy", error);
	{
		// A scene without links keeps writing the version it always has.
		LevelMapDocument plain = document;
		ok &= expect(unlinkLevelGroup(&plain, group, &error), "unlink", error);
		const QByteArray bytes = serializeLevelMap(plain).bytes;
		const QByteArray marker("\n// VibeStudioScene: ");
		const auto at = bytes.lastIndexOf(marker);
		const auto json = QJsonDocument::fromJson(QByteArray::fromBase64(bytes.mid(at + marker.size()).trimmed())).object();
		ok &= expect(at >= 0 && json.value(QStringLiteral("version")).toInt() == 2, "unlinked scenes stay at version 2");
		ok &= expect(node(plain, group)->linkId.isEmpty() && node(plain, copy)->linkId.isEmpty(), "unlinking one of two unlinks both");
		ok &= expect(setLevelMapEntityProperty(&plain, idOf(node(plain, group)->objects, QStringLiteral("entity")), QStringLiteral("light"), QStringLiteral("50"), &error)
				&& property(entityById(plain, idOf(node(plain, copy)->objects, QStringLiteral("entity"))), QStringLiteral("light"))
					!= QStringLiteral("50"),
			"unlinked copies go their own ways", error);
	}

	// A locked copy is left alone, and catches up once unlocked.
	ok &= expect(setLevelSceneLocked(&document, copy, true, &error), "lock the copy", error);
	ok &= expect(setLevelMapEntityProperty(&document, originalEntity()->id, QStringLiteral("light"), QStringLiteral("400"), &error), "edit beside a lock", error);
	ok &= expect(property(copyEntity(), QStringLiteral("light")) != QStringLiteral("400"), "a locked copy keeps what it has");
	ok &= expect(setLevelSceneLocked(&document, copy, false, &error), "unlock", error);
	ok &= expect(setLevelMapEntityProperty(&document, originalEntity()->id, QStringLiteral("delay"), QStringLiteral("1"), &error)
			&& property(copyEntity(), QStringLiteral("light")) == QStringLiteral("400")
			&& property(copyEntity(), QStringLiteral("delay")) == QStringLiteral("1"),
		"the next edit brings it up to date", error);

	// Both copies changed in different ways at once: neither is copied over the
	// other until Update Linked Copies.
	ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, originalBrush()->id}, {LevelMapSelectionKind::Entity, copyEntity()->id}}, &error)
			&& moveLevelMapSelection(&document, 0, 0, 8, &error),
		"move parts of both copies", error);
	ok &= expect(levelLinkedContent(document, node(document, group)->objects).shape
			!= levelLinkedContent(document, node(document, copy)->objects).shape,
		"copies changed differently at once are left as they are");
	int updated = 0;
	ok &= expect(updateLinkedLevelGroups(&document, group, &updated, &error) && updated == 1
			&& levelLinkedContent(document, node(document, group)->objects).shape
				== levelLinkedContent(document, node(document, copy)->objects).shape,
		"Update Linked Copies makes the others match", error);

	// Emptying a copy takes it out of the link instead of emptying the rest.
	{
		LevelMapDocument emptied = document;
		QVector<LevelMapSelectionRef> members;
		for (const QString& member : node(emptied, copy)->objects) {
			members.push_back({levelMapSelectionKindFromId(member.section(QLatin1Char(':'), 0, 0)), member.section(QLatin1Char(':'), 1).toInt()});
		}
		ok &= expect(deleteLevelMapObjects(&emptied, members, &error), "empty the copy", error);
		ok &= expect(node(emptied, copy)->linkId.isEmpty() && node(emptied, group)->linkId.isEmpty() && node(emptied, group)->objects.size() == 2,
			"an emptied copy leaves its link and the original keeps its content");
	}

	// Refusals.
	ok &= expect(!createLinkedLevelGroup(&document, QStringLiteral("00000000-0000-0000-0000-000000000000"), {64, 0, 0, true}, nullptr, &error),
		"an unknown group is refused");
	QString layer;
	ok &= expect(createLevelSceneNode(&document, LevelSceneNodeKind::Layer, QStringLiteral("Details"), {}, &layer, &error)
			&& !createLinkedLevelGroup(&document, layer, {64, 0, 0, true}, nullptr, &error),
		"a layer cannot be linked", error);
	QString empty;
	ok &= expect(createLevelSceneNode(&document, LevelSceneNodeKind::Group, QStringLiteral("Nothing"), {}, &empty, &error)
			&& !createLinkedLevelGroup(&document, empty, {64, 0, 0, true}, nullptr, &error),
		"an empty group cannot be linked", error);
	{
		LevelMapCreateRequest doom;
		doom.game = QStringLiteral("doom");
		LevelMapDocument wad;
		ok &= expect(createLevelMap(doom, &wad, &error), "doom map", error);
		QString sectors;
		ok &= expect(createLevelSceneNode(&wad, LevelSceneNodeKind::Group, QStringLiteral("Room"), {}, &sectors, &error)
				&& assignLevelSceneObjects(&wad, sectors, {QStringLiteral("sector:0")}, &error)
				&& !createLinkedLevelGroup(&wad, sectors, {64, 0, 0, true}, nullptr, &error) && error.contains(QStringLiteral("Quake")),
			"Doom maps cannot link groups", error);
	}

	// Copies turn and mirror on their own, and edits reach them turned.
	{
		LevelMapDocument ell;
		ok &= expect(createLevelMap(create, &ell, &error), "map for turns", error);
		int longArm = -1;
		int shortArm = -1;
		ok &= expect(addLevelMapBoxBrush(&ell, {0, 0, 0, true}, {96, 32, 32, true}, QStringLiteral("studio/long"), &longArm, &error)
				&& addLevelMapBoxBrush(&ell, {0, 32, 0, true}, {16, 64, 32, true}, QStringLiteral("studio/short"), &shortArm, &error),
			"an L of two blocks", error);
		QString source;
		ok &= expect(createLevelSceneNode(&ell, LevelSceneNodeKind::Group, QStringLiteral("Ell"), {}, &source, &error)
				&& assignLevelSceneObjects(&ell, source, {QStringLiteral("brush:%1").arg(longArm), QStringLiteral("brush:%1").arg(shortArm)}, &error),
			"the L as a group", error);
		QString turned;
		ok &= expect(createLinkedLevelGroup(&ell, source, {256, 0, 0, true}, &turned, &error), "a linked copy of the L", error);
		const auto members = [&ell](const QString& id) {
			QVector<LevelMapSelectionRef> refs;
			for (const QString& member : node(ell, id)->objects) {
				refs.push_back({levelMapSelectionKindFromId(member.section(QLatin1Char(':'), 0, 0)), member.section(QLatin1Char(':'), 1).toInt()});
			}
			return refs;
		};
		const auto bounds = [&ell](const QString& id) {
			LevelMapVec3 low;
			LevelMapVec3 high;
			levelLinkedGroupBounds(ell, node(ell, id)->objects, &low, &high);
			return std::pair<LevelMapVec3, LevelMapVec3>(low, high);
		};
		// The short arm's centre relative to its group's centre.
		const auto armOffset = [&ell, &bounds](const QString& id, const QString& texture) {
			const auto [low, high] = bounds(id);
			const QPointF middle((low.x + high.x) / 2.0, (low.y + high.y) / 2.0);
			for (const QString& member : node(ell, id)->objects) {
				const LevelMapBrush* brush = brushById(ell, member.section(QLatin1Char(':'), 1).toInt());
				if (brush && !brush->faces.isEmpty() && brush->faces.first().textureName == texture) {
					return QPointF((brush->mins.x + brush->maxs.x) / 2.0, (brush->mins.y + brush->maxs.y) / 2.0) - middle;
				}
			}
			return QPointF(1e9, 1e9);
		};
		const auto framed = [](const LevelSceneNode* frame, QPointF point) {
			// Mirror across x first, then quarter turns anticlockwise.
			if (frame->linkMirror) {
				point.setY(-point.y());
			}
			for (int turn = 0; turn < frame->linkTurn; ++turn) {
				point = QPointF(-point.y(), point.x());
			}
			return point;
		};
		const auto samePoint = [](QPointF a, QPointF b) { return std::abs(a.x() - b.x()) < 0.01 && std::abs(a.y() - b.y()) < 0.01; };
		const auto sourceBounds = bounds(source);
		ok &= expect(setLevelMapSelection(&ell, members(turned), &error) && rotateLevelMapSelection(&ell, 2, 1, &error), "turn the copy", error);
		const auto turnedBounds = bounds(turned);
		ok &= expect(node(ell, turned)->linkTurn != 0 && node(ell, source)->linkTurn == 0
				&& std::abs((turnedBounds.second.x - turnedBounds.first.x) - 64) < 0.01 && std::abs((turnedBounds.second.y - turnedBounds.first.y) - 96) < 0.01
				&& std::abs(bounds(source).first.x - sourceBounds.first.x) < 0.01 && std::abs(bounds(source).second.y - sourceBounds.second.y) < 0.01,
			"a whole copy turned a quarter turn turns alone", QString::number(node(ell, turned)->linkTurn));
		// An edit to the original reaches the turned copy turned.
		ok &= expect(moveLevelMapObject(&ell, QStringLiteral("brush"), shortArm, 0, 16, 0, &error), "lengthen the L", error);
		ok &= expect(samePoint(armOffset(turned, QStringLiteral("studio/short")),
					 framed(node(ell, turned), armOffset(source, QStringLiteral("studio/short")))),
			"the edit arrives turned into the copy's frame");
		// Mirrored as a whole, the copy mirrors alone; an edit then arrives mirrored.
		ok &= expect(setLevelMapSelection(&ell, members(turned), &error) && flipLevelMapSelection(&ell, 0, &error), "mirror the copy", error);
		ok &= expect(node(ell, turned)->linkMirror && !node(ell, source)->linkMirror, "a whole copy mirrored mirrors alone");
		ok &= expect(moveLevelMapObject(&ell, QStringLiteral("brush"), longArm, 8, 0, 0, &error), "shift the long arm", error);
		ok &= expect(samePoint(armOffset(turned, QStringLiteral("studio/short")),
					 framed(node(ell, turned), armOffset(source, QStringLiteral("studio/short"))))
				&& samePoint(armOffset(turned, QStringLiteral("studio/long")), framed(node(ell, turned), armOffset(source, QStringLiteral("studio/long")))),
			"edits arrive mirrored and turned");
		// The frame is saved with the link.
		LevelMapDocument again;
		ok &= expect(loadLevelMapBytes({QStringLiteral("ell.map"), {}, {}}, serializeLevelMap(ell).bytes, &again, &error)
				&& node(again, turned)->linkTurn == node(ell, turned)->linkTurn && node(again, turned)->linkMirror,
			"turns and mirrors are saved", error);
		// One undo of the last edit puts both copies back.
		const auto shortBefore = armOffset(source, QStringLiteral("studio/long"));
		ok &= expect(undoLevelMapEdit(&ell, &error) && !samePoint(armOffset(source, QStringLiteral("studio/long")), shortBefore)
				&& samePoint(armOffset(turned, QStringLiteral("studio/long")), framed(node(ell, turned), armOffset(source, QStringLiteral("studio/long")))),
			"undo restores every copy in its frame", error);
	}

	// The CLI does the same, through the same service.
	if (argc > 1) {
		QTemporaryDir temp;
		const QString cli = QString::fromLocal8Bit(argv[1]);
		const QString input = temp.filePath(QStringLiteral("input.map"));
		LevelMapDocument fresh;
		ok &= expect(createLevelMap(create, &fresh, &error)
				&& addLevelMapBoxBrush(&fresh, {0, 0, 0, true}, {32, 32, 64, true}, QStringLiteral("studio/pillar"), nullptr, &error),
			"CLI fixture", error);
		QString cliGroup;
		ok &= expect(createLevelSceneNode(&fresh, LevelSceneNodeKind::Group, QStringLiteral("Column"), {}, &cliGroup, &error)
				&& assignLevelSceneObjects(&fresh, cliGroup, {QStringLiteral("brush:%1").arg(fresh.brushes.last().id)}, &error),
			"CLI group", error);
		QFile file(input);
		ok &= file.open(QIODevice::WriteOnly) && file.write(serializeLevelMap(fresh).bytes) > 0;
		file.close();
		const auto run = [&](const QStringList& arguments) {
			QProcess process;
			process.start(cli, QStringList {QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--settings-file"),
								   temp.filePath(QStringLiteral("cli.ini")), QStringLiteral("editor"), QStringLiteral("scene")}
					+ arguments);
			const bool ended = process.waitForStarted() && process.waitForFinished(60000);
			const QByteArray out = process.readAllStandardOutput();
			ok &= expect(ended && process.exitCode() == 0, "CLI run", QString::fromUtf8(out + process.readAllStandardError()));
			return QJsonDocument::fromJson(out).object();
		};
		const QString linked = temp.filePath(QStringLiteral("linked.map"));
		const QJsonObject made = run({QStringLiteral("link"), input, QStringLiteral("--id"), cliGroup, QStringLiteral("--offset"),
			QStringLiteral("64,0,0"), QStringLiteral("--output"), linked});
		const QString cliCopy = made.value(QStringLiteral("createdId")).toString();
		LevelMapDocument result;
		ok &= expect(!cliCopy.isEmpty() && loadLevelMap({linked, {}, {}}, &result, &error)
				&& levelLinkedGroupNodes(result.scene, cliGroup).size() == 2,
			"editor scene link makes a linked copy", error);
		const QJsonArray nodes = made.value(QStringLiteral("scene")).toObject().value(QStringLiteral("nodes")).toArray();
		ok &= expect(nodes.size() == 2 && !nodes.at(0).toObject().value(QStringLiteral("link")).toString().isEmpty(),
			"the JSON report names the link");
		const QString unlinked = temp.filePath(QStringLiteral("unlinked.map"));
		run({QStringLiteral("unlink"), linked, QStringLiteral("--id"), cliCopy, QStringLiteral("--output"), unlinked});
		LevelMapDocument separate;
		ok &= expect(loadLevelMap({unlinked, {}, {}}, &separate, &error) && levelLinkedGroupNodes(separate.scene, cliGroup).isEmpty(),
			"editor scene unlink separates the copies", error);
		const QString refreshed = temp.filePath(QStringLiteral("refreshed.map"));
		const QJsonObject refresh = run({QStringLiteral("update-links"), linked, QStringLiteral("--id"), cliGroup, QStringLiteral("--output"), refreshed});
		ok &= expect(refresh.value(QStringLiteral("updated")).toInt() == 1, "editor scene update-links reports the copies it updated");
	}

	if (ok) {
		std::cout << "Level linked groups smoke test passed.\n";
	}
	return ok ? 0 : 1;
}
