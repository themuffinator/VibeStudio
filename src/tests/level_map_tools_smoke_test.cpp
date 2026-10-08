// Checks the editing tools the Levels sidebar and menus add from the other
// editors: brush entities (tie and move to world), region selections, detail
// and structural brushes, drop to floor and CSG intersection, each as one
// undo step that undoes to the exact bytes it started from.

#include "core/level_document.h"
#include "core/level_doom_align.h"
#include "core/level_doom_shapes.h"
#include "core/level_map.h"

#include <QCoreApplication>
#include <QHash>
#include <QPointF>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <iostream>

using namespace vibestudio;

namespace {

bool expect(bool condition, const char* message, const QString& detail = QString())
{
	if (!condition) {
		std::cerr << "FAIL: " << message;
		if (!detail.isEmpty()) {
			std::cerr << " (" << detail.toStdString() << ")";
		}
		std::cerr << "\n";
	}
	return condition;
}

// A map built through the editor's own services, written and read back so
// every object owns its source lines as a file's would.
LevelMapDocument fixture(const QString& game)
{
	LevelMapDocument document;
	LevelMapCreateRequest create;
	create.game = game;
	create.starterRoom = false;
	QString error;
	createLevelMap(create, &document, &error);
	addLevelMapBoxBrush(&document, {-128, -128, -16, true}, {128, 128, 0, true}, QStringLiteral("studio/floor"), nullptr, &error);
	addLevelMapBoxBrush(&document, {0, 0, 0, true}, {64, 64, 64, true}, QStringLiteral("studio/a"), nullptr, &error);
	addLevelMapBoxBrush(&document, {32, 32, 32, true}, {96, 96, 96, true}, QStringLiteral("studio/b"), nullptr, &error);
	addLevelMapEntity(&document, QStringLiteral("light"), {16, 16, 200, true}, {}, nullptr, &error);
	addLevelMapEntity(&document, QStringLiteral("info_player_start"), {-64, -64, 100, true}, {}, nullptr, &error);
	const LevelMapSerialized written = serializeLevelMap(document);
	LevelMapDocument loaded;
	LevelMapLoadRequest request;
	request.path = QStringLiteral("tools-%1.map").arg(game);
	request.engineHint = game == QStringLiteral("quake3") ? QStringLiteral("idtech3") : QStringLiteral("idtech2");
	loadLevelMapBytes(request, written.bytes, &loaded, &error);
	return loaded;
}

QByteArray bytesOf(const LevelMapDocument& document)
{
	return serializeLevelMap(document).bytes;
}

LevelMapDocument reloaded(const LevelMapDocument& document)
{
	LevelMapDocument loaded;
	LevelMapLoadRequest request;
	request.path = document.sourcePath;
	request.engineHint = document.format == LevelMapFormat::Quake3Map ? QStringLiteral("idtech3") : QStringLiteral("idtech2");
	QString error;
	loadLevelMapBytes(request, bytesOf(document), &loaded, &error);
	return loaded;
}

int brushNamed(const LevelMapDocument& document, const QString& texture)
{
	for (const LevelMapBrush& brush : document.brushes) {
		if (!brush.faces.isEmpty() && brush.faces.first().textureName == texture) {
			return brush.id;
		}
	}
	return -1;
}

const LevelMapEntity* entityOfClass(const LevelMapDocument& document, const QString& className)
{
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.className == className) {
			return &entity;
		}
	}
	return nullptr;
}

int worldId(const LevelMapDocument& document)
{
	const LevelMapEntity* world = entityOfClass(document, QStringLiteral("worldspawn"));
	return world ? world->id : -1;
}

bool contains(const QVector<LevelMapSelectionRef>& refs, LevelMapSelectionKind kind, int id)
{
	return refs.contains(LevelMapSelectionRef {kind, id});
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true;
	QString error;

	// Brush entities: tie, undo to the same bytes, redo, save and reopen.
	{
		LevelMapDocument document = fixture(QStringLiteral("quake"));
		ok &= expect(document.brushes.size() == 3 && document.entities.size() == 3, "the fixture has three brushes and three entities");
		const QByteArray original = bytesOf(document);
		const int a = brushNamed(document, QStringLiteral("studio/a"));
		ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, a}}, &error), "select brush a", error);
		const int undoDepth = static_cast<int>(document.undoStack.size());
		int door = -1;
		ok &= expect(tieLevelMapSelectionToEntity(&document, QStringLiteral("func_door"), {{QStringLiteral("speed"), QStringLiteral("100"), 0}}, &door, &error),
			"tie brush a to a func_door", error);
		const LevelMapEntity* made = entityOfClass(document, QStringLiteral("func_door"));
		ok &= expect(made && made->id == door, "the door exists and is reported");
		int owned = 0;
		for (const LevelMapBrush& brush : document.brushes) {
			owned += brush.entityId == door ? 1 : 0;
		}
		ok &= expect(owned == 1 && document.brushes.size() == 3, "the door owns the one brush and no brush was lost");
		ok &= expect(document.undoStack.size() == undoDepth + 1, "tying is one undo step");
		ok &= expect(document.selection == QVector<LevelMapSelectionRef> {{LevelMapSelectionKind::Entity, door}}, "the new entity is selected");
		ok &= expect(levelMapUndoLines(document).join(QLatin1Char('\n')).contains(QStringLiteral("func_door")), "the step is named for the class");
		const QByteArray tied = bytesOf(document);
		ok &= expect(tied.contains("\"classname\" \"func_door\"") && tied.contains("\"speed\" \"100\""), "the saved text holds the door and its key");
		ok &= expect(undoLevelMapEdit(&document, &error) && bytesOf(document) == original, "undo restores the original bytes", error);
		ok &= expect(redoLevelMapEdit(&document, &error) && bytesOf(document) == tied, "redo makes the door again", error);
		const LevelMapDocument reopened = reloaded(document);
		const LevelMapEntity* door2 = entityOfClass(reopened, QStringLiteral("func_door"));
		ok &= expect(door2 && std::count_if(reopened.brushes.cbegin(), reopened.brushes.cend(), [&](const LevelMapBrush& brush) { return brush.entityId == door2->id; }) == 1,
			"the door and its brush survive a save and reopen");

		// Move to world: the door's brush goes back to worldspawn.
		ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::Entity, door}}, &error), "select the door", error);
		int moved = 0;
		ok &= expect(moveLevelMapSelectionToWorld(&document, &moved, &error) && moved == 1, "move the door's brush to the world", error);
		ok &= expect(!entityOfClass(document, QStringLiteral("func_door")), "the door is gone");
		ok &= expect(std::count_if(document.brushes.cbegin(), document.brushes.cend(), [&](const LevelMapBrush& brush) { return brush.entityId == worldId(document); }) == 3,
			"all three brushes are world brushes again");
		ok &= expect(undoLevelMapEdit(&document, &error) && bytesOf(document) == tied, "undo puts the door back", error);
		ok &= expect(setLevelMapSelection(&document, {}, &error) && !moveLevelMapSelectionToWorld(&document, nullptr, &error) && !error.isEmpty(),
			"moving nothing is refused with a reason");
		ok &= expect(!tieLevelMapSelectionToEntity(&document, QStringLiteral("bad name"), {}, nullptr, &error), "a class name with a space is refused");
	}

	// Region selections around brush a.
	{
		LevelMapDocument document = fixture(QStringLiteral("quake"));
		const int floor = brushNamed(document, QStringLiteral("studio/floor"));
		const int a = brushNamed(document, QStringLiteral("studio/a"));
		const int b = brushNamed(document, QStringLiteral("studio/b"));
		const int light = entityOfClass(document, QStringLiteral("light"))->id;
		const int start = entityOfClass(document, QStringLiteral("info_player_start"))->id;
		setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, a}});
		const auto inside = levelMapRegionSelection(document, LevelMapRegionSelection::Inside);
		ok &= expect(inside.isEmpty(), "nothing lies wholly inside brush a");
		const auto touching = levelMapRegionSelection(document, LevelMapRegionSelection::Touching);
		ok &= expect(contains(touching, LevelMapSelectionKind::QuakeBrush, b) && contains(touching, LevelMapSelectionKind::QuakeBrush, floor)
				&& !contains(touching, LevelMapSelectionKind::Entity, light) && !contains(touching, LevelMapSelectionKind::QuakeBrush, a),
			"touching finds the overlapping and abutting brushes, not the region itself");
		const auto completeTall = levelMapRegionSelection(document, LevelMapRegionSelection::CompleteTall, 2);
		ok &= expect(contains(completeTall, LevelMapSelectionKind::Entity, light) && !contains(completeTall, LevelMapSelectionKind::QuakeBrush, b)
				&& !contains(completeTall, LevelMapSelectionKind::Entity, start),
			"complete tall finds what stands wholly within the footprint at any height");
		const auto partialTall = levelMapRegionSelection(document, LevelMapRegionSelection::PartialTall, 2);
		ok &= expect(contains(partialTall, LevelMapSelectionKind::QuakeBrush, b) && contains(partialTall, LevelMapSelectionKind::Entity, light)
				&& contains(partialTall, LevelMapSelectionKind::QuakeBrush, floor),
			"partial tall finds what overlaps the footprint");
		setLevelMapSelection(&document, {});
		ok &= expect(levelMapRegionSelection(document, LevelMapRegionSelection::Inside, 2, &error).isEmpty() && !error.isEmpty(),
			"a region needs a selection");
	}

	// Detail in Quake (func_detail) and in Quake II (the content bit).
	{
		LevelMapDocument document = fixture(QStringLiteral("quake"));
		ok &= expect(!levelMapUsesFaceFlags(document), "a Quake map writes no face flags");
		const QByteArray original = bytesOf(document);
		setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, brushNamed(document, QStringLiteral("studio/b"))}});
		int changed = 0;
		ok &= expect(setLevelMapSelectionDetail(&document, true, &changed, &error) && changed == 1, "make brush b detail in Quake", error);
		ok &= expect(entityOfClass(document, QStringLiteral("func_detail")), "Quake detail is a func_detail entity");
		ok &= expect(!setLevelMapSelectionDetail(&document, true, nullptr, &error), "detail twice is refused");
		ok &= expect(setLevelMapSelectionDetail(&document, false, &changed, &error) && !entityOfClass(document, QStringLiteral("func_detail")),
			"make it structural again", error);
		ok &= expect(undoLevelMapEdit(&document, &error) && undoLevelMapEdit(&document, &error) && bytesOf(document) == original, "both undo back to the start", error);

		LevelMapDocument quake2 = fixture(QStringLiteral("quake2"));
		ok &= expect(levelMapUsesFaceFlags(quake2), "a Quake II map writes face flags");
		const QByteArray before = bytesOf(quake2);
		const int b = brushNamed(quake2, QStringLiteral("studio/b"));
		setLevelMapSelection(&quake2, {{LevelMapSelectionKind::QuakeBrush, b}});
		ok &= expect(setLevelMapSelectionDetail(&quake2, true, &changed, &error) && changed == 1, "make brush b detail in Quake II", error);
		const LevelMapDocument reopened = reloaded(quake2);
		const LevelMapBrush* detailBrush = nullptr;
		for (const LevelMapBrush& brush : reopened.brushes) {
			if (brush.faces.first().textureName == QStringLiteral("studio/b")) {
				detailBrush = &brush;
			}
		}
		ok &= expect(detailBrush && std::all_of(detailBrush->faces.cbegin(), detailBrush->faces.cend(), [](const LevelMapBrushFace& face) { return (face.contentFlags & 0x8000000) != 0; }),
			"every face of the detail brush carries the detail bit after a reopen");
		ok &= expect(undoLevelMapEdit(&quake2, &error) && bytesOf(quake2) == before, "undo clears the bit to the original bytes", error);
	}

	// Drop to floor, with and without definition bounds.
	{
		LevelMapDocument document = fixture(QStringLiteral("quake"));
		const QByteArray original = bytesOf(document);
		const int light = entityOfClass(document, QStringLiteral("light"))->id;
		const int start = entityOfClass(document, QStringLiteral("info_player_start"))->id;
		setLevelMapSelection(&document, {{LevelMapSelectionKind::Entity, light}, {LevelMapSelectionKind::Entity, start}});
		const auto bounds = [](const QString& className, LevelMapVec3* mins, LevelMapVec3* maxs) {
			if (className == QStringLiteral("info_player_start")) {
				*mins = {-16, -16, -24, true};
				*maxs = {16, 16, 32, true};
				return true;
			}
			return false;
		};
		int dropped = 0;
		const int depth = static_cast<int>(document.undoStack.size());
		ok &= expect(dropLevelMapSelectionToFloor(&document, bounds, &dropped, &error) && dropped == 2, "drop both entities", error);
		ok &= expect(document.undoStack.size() == depth + 1, "dropping is one undo step");
		const LevelMapEntity* lit = entityOfClass(document, QStringLiteral("light"));
		const LevelMapEntity* player = entityOfClass(document, QStringLiteral("info_player_start"));
		ok &= expect(lit && std::abs(lit->origin.z - 64.0) < 1e-6, "the light rests on brush a's top, the highest surface beneath it",
			lit ? QString::number(lit->origin.z) : QString());
		ok &= expect(player && std::abs(player->origin.z - 24.0) < 1e-6, "the player start stands its definition's height above the floor",
			player ? QString::number(player->origin.z) : QString());
		ok &= expect(document.selection.size() == 2, "the dropped entities stay selected");
		ok &= expect(undoLevelMapEdit(&document, &error) && bytesOf(document) == original, "undo lifts them back", error);
		ok &= expect(redoLevelMapEdit(&document, &error) && !dropLevelMapSelectionToFloor(&document, bounds, nullptr, &error), "dropped entities stay put");
	}

	// CSG intersection of brushes a and b.
	{
		LevelMapDocument document = fixture(QStringLiteral("quake"));
		const QByteArray original = bytesOf(document);
		const int a = brushNamed(document, QStringLiteral("studio/a"));
		const int b = brushNamed(document, QStringLiteral("studio/b"));
		setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, a}, {LevelMapSelectionKind::QuakeBrush, b}});
		ok &= expect(intersectLevelMapSelection(&document, &error), "intersect a and b", error);
		ok &= expect(document.brushes.size() == 2, "two brushes give way to one");
		const LevelMapBrush* piece = nullptr;
		for (const LevelMapBrush& brush : document.brushes) {
			if (document.selection.contains(LevelMapSelectionRef {LevelMapSelectionKind::QuakeBrush, brush.id})) {
				piece = &brush;
			}
		}
		ok &= expect(piece && piece->boundsSolved && std::abs(piece->mins.x - 32) < 1e-6 && std::abs(piece->maxs.x - 64) < 1e-6
				&& std::abs(piece->mins.z - 32) < 1e-6 && std::abs(piece->maxs.z - 64) < 1e-6,
			"the result is the overlap, 32 to 64 on every axis");
		const LevelMapDocument reopened = reloaded(document);
		ok &= expect(reopened.brushes.size() == 2, "the intersection survives a save and reopen");
		ok &= expect(undoLevelMapEdit(&document, &error) && bytesOf(document) == original, "undo restores both brushes", error);
		const int floor = brushNamed(document, QStringLiteral("studio/floor"));
		setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, floor}, {LevelMapSelectionKind::QuakeBrush, b}});
		ok &= expect(!intersectLevelMapSelection(&document, &error) && bytesOf(document) == original, "brushes that do not overlap are refused");
	}

	// Align lines objects up on the selection's edges and centre, as Hammer does.
	{
		LevelMapDocument document = fixture(QStringLiteral("quake"));
		const QByteArray original = bytesOf(document);
		const int a = brushNamed(document, QStringLiteral("studio/a"));
		const int b = brushNamed(document, QStringLiteral("studio/b"));
		const QVector<LevelMapSelectionRef> pair {{LevelMapSelectionKind::QuakeBrush, a}, {LevelMapSelectionKind::QuakeBrush, b}};
		setLevelMapSelection(&document, pair);
		const int depth = static_cast<int>(document.undoStack.size());
		int moved = 0;
		ok &= expect(alignLevelMapSelection(&document, 0, LevelMapAlignEdge::Minimum, &moved, &error) && moved == 1, "align left moves b", error);
		const LevelMapBrush* bAfter = nullptr;
		for (const LevelMapBrush& brush : document.brushes) {
			bAfter = brush.id == b ? &brush : bAfter;
		}
		ok &= expect(bAfter && std::abs(bAfter->mins.x) < 1e-6 && std::abs(bAfter->maxs.x - 64) < 1e-6, "b's left edge meets a's");
		ok &= expect(document.undoStack.size() == depth + 1 && document.selection == pair, "align is one step and keeps the selection");
		ok &= expect(undoLevelMapEdit(&document, &error) && bytesOf(document) == original, "undo puts b back", error);

		ok &= expect(alignLevelMapSelection(&document, 0, LevelMapAlignEdge::Centre, &moved, &error) && moved == 2, "centring moves both", error);
		for (const LevelMapBrush& brush : document.brushes) {
			if (brush.id == a || brush.id == b) {
				ok &= expect(std::abs((brush.mins.x + brush.maxs.x) / 2.0 - 48) < 1e-6, "both centres meet in the middle of the pair");
			}
		}
		ok &= expect(undoLevelMapEdit(&document, &error) && bytesOf(document) == original, "undo the centring", error);

		// A point entity is its origin; the brushes go up to the light.
		const LevelMapEntity* light = entityOfClass(document, QStringLiteral("light"));
		setLevelMapSelection(&document, {pair.at(0), pair.at(1), {LevelMapSelectionKind::Entity, light ? light->id : -1}});
		ok &= expect(alignLevelMapSelection(&document, 2, LevelMapAlignEdge::Maximum, &moved, &error) && moved == 2, "align top to the light", error);
		for (const LevelMapBrush& brush : document.brushes) {
			if (brush.id == a || brush.id == b) {
				ok &= expect(std::abs(brush.maxs.z - 200) < 1e-6, "the brushes' tops meet the light's height");
			}
		}
		ok &= expect(undoLevelMapEdit(&document, &error) && bytesOf(document) == original, "undo the top alignment", error);

		// A brush entity moves as a whole.
		setLevelMapSelection(&document, {pair.at(1)});
		int door = -1;
		ok &= expect(tieLevelMapSelectionToEntity(&document, QStringLiteral("func_wall"), {}, &door, &error), "b becomes a func_wall", error);
		setLevelMapSelection(&document, {pair.at(0), {LevelMapSelectionKind::Entity, door}});
		ok &= expect(alignLevelMapSelection(&document, 1, LevelMapAlignEdge::Minimum, &moved, &error) && moved == 1, "the brush entity aligns", error);
		for (const LevelMapBrush& brush : document.brushes) {
			if (brush.entityId == door) {
				ok &= expect(std::abs(brush.mins.y) < 1e-6, "the func_wall's brush moved with it");
			}
		}

		setLevelMapSelection(&document, {pair.at(0)});
		const QByteArray single = bytesOf(document);
		ok &= expect(!alignLevelMapSelection(&document, 0, LevelMapAlignEdge::Minimum, &moved, &error) && bytesOf(document) == single,
			"one object has nothing to align with", error);
	}

	// Shear slants a brush about the selection's centre, as TrenchBroom does.
	{
		LevelMapDocument document = fixture(QStringLiteral("quake"));
		const QByteArray original = bytesOf(document);
		const int a = brushNamed(document, QStringLiteral("studio/a"));
		setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, a}});
		ok &= expect(shearLevelMapSelection(&document, 0, 2, 0.5, {false, false}, &error), "shear a along x by its height", error);
		const LevelMapBrush* sheared = nullptr;
		for (const LevelMapBrush& brush : document.brushes) {
			sheared = brush.id == a ? &brush : sheared;
		}
		ok &= expect(sheared && sheared->boundsSolved && std::abs(sheared->mins.x + 16) < 1e-6 && std::abs(sheared->maxs.x - 80) < 1e-6
				&& std::abs(sheared->mins.z) < 1e-6 && std::abs(sheared->maxs.z - 64) < 1e-6,
			"the top slides 16 one way and the bottom 16 the other");
		const LevelMapDocument reopened = reloaded(document);
		bool closed = true;
		for (const LevelMapBrush& brush : reopened.brushes) {
			closed = closed && brush.boundsSolved;
		}
		ok &= expect(closed, "the sheared brush is still closed after a reload");
		ok &= expect(undoLevelMapEdit(&document, &error) && bytesOf(document) == original, "undo straightens it", error);
		ok &= expect(!shearLevelMapSelection(&document, 1, 1, 0.5, {false, false}, &error) && bytesOf(document) == original,
			"an axis cannot shear by itself", error);
		ok &= expect(!shearLevelMapSelection(&document, 0, 1, 0.0, {false, false}, &error), "a zero shear is refused", error);
		// About its bottom, as the Shear Tool's dragged top side does: the
		// bottom stays and the top slides the whole 32.
		ok &= expect(shearLevelMapSelectionAbout(&document, 0, 2, 0.5, 0.0, {false, false}, &error), "shear a about its bottom", error);
		for (const LevelMapBrush& brush : document.brushes) {
			sheared = brush.id == a ? &brush : sheared;
		}
		ok &= expect(sheared && std::abs(sheared->mins.x) < 1e-6 && std::abs(sheared->maxs.x - 96) < 1e-6,
			"the bottom stays and the top slides 32", sheared ? QStringLiteral("%1 .. %2").arg(sheared->mins.x).arg(sheared->maxs.x) : QString());
		ok &= expect(undoLevelMapEdit(&document, &error) && bytesOf(document) == original, "undo straightens it again", error);
	}

	// Curving a Doom linedef bends it into pieces along an arc.
	{
		LevelMapDocument doom;
		LevelMapCreateRequest create;
		create.game = QStringLiteral("doom");
		create.starterRoom = true;
		ok &= expect(createLevelMap(create, &doom, &error) && !doom.doomLinedefs.isEmpty(), "a Doom room", error);
		if (!doom.doomLinedefs.isEmpty()) {
			const QByteArray before = serializeLevelMap(doom).bytes;
			const LevelMapDoomLinedef line = doom.doomLinedefs.first();
			LevelMapDoomVertex from;
			LevelMapDoomVertex to;
			for (const LevelMapDoomVertex& vertex : std::as_const(doom.doomVertices)) {
				from = vertex.id == line.startVertex ? vertex : from;
				to = vertex.id == line.endVertex ? vertex : to;
			}
			const int lines = static_cast<int>(doom.doomLinedefs.size());
			const int vertices = static_cast<int>(doom.doomVertices.size());
			setLevelMapSelection(&doom, {{LevelMapSelectionKind::DoomLinedef, line.id}});
			int curved = 0;
			ok &= expect(curveLevelMapLinedefs(&doom, 4, 32, &curved, &error) && curved == 1, "curve the first wall", error);
			ok &= expect(doom.doomLinedefs.size() == lines + 3 && doom.doomVertices.size() == vertices + 3 && doom.selection.size() == 4,
				"four selected pieces and three new corners");
			// The middle corner bulges 32 units to the front, the right walking
			// from the start.
			const double chord = std::hypot(to.x - from.x, to.y - from.y);
			const double bulgeX = (from.x + to.x) / 2.0 + 32.0 * (to.y - from.y) / chord;
			const double bulgeY = (from.y + to.y) / 2.0 - 32.0 * (to.x - from.x) / chord;
			bool middle = false;
			for (const LevelMapDoomVertex& vertex : std::as_const(doom.doomVertices)) {
				middle = middle || std::hypot(vertex.x - bulgeX, vertex.y - bulgeY) <= 1.0;
			}
			ok &= expect(middle, "the middle corner lies on the bulge");
			// The second piece's front texture carries on from the first's.
			const LevelMapDoomLinedef* first = nullptr;
			const LevelMapDoomLinedef* second = nullptr;
			for (const LevelMapDoomLinedef& candidate : std::as_const(doom.doomLinedefs)) {
				first = candidate.id == line.id ? &candidate : first;
			}
			for (const LevelMapDoomLinedef& candidate : std::as_const(doom.doomLinedefs)) {
				second = first && candidate.startVertex == first->endVertex && candidate.id != line.id ? &candidate : second;
			}
			int firstOffset = 0;
			int secondOffset = -1;
			LevelMapDoomVertex corner;
			for (const LevelMapDoomVertex& vertex : std::as_const(doom.doomVertices)) {
				corner = first && vertex.id == first->endVertex ? vertex : corner;
			}
			for (const LevelMapDoomSidedef& side : std::as_const(doom.doomSidedefs)) {
				firstOffset = first && side.id == first->frontSidedef ? side.offsetX : firstOffset;
				secondOffset = second && side.id == second->frontSidedef ? side.offsetX : secondOffset;
			}
			ok &= expect(second && secondOffset == firstOffset + static_cast<int>(std::lround(std::hypot(corner.x - from.x, corner.y - from.y))),
				"each piece's front texture starts where the last one's ended");
			ok &= expect(undoLevelMapEdit(&doom, &error) && serializeLevelMap(doom).bytes == before, "undo straightens the wall", error);
			setLevelMapSelection(&doom, {{LevelMapSelectionKind::DoomLinedef, line.id}});
			ok &= expect(!curveLevelMapLinedefs(&doom, 1, 32, &curved, &error), "one segment is refused", error);
			ok &= expect(curveLevelMapLinedefs(&doom, 2, 0, &curved, &error) && doom.doomLinedefs.size() == lines + 1,
				"no bulge splits the wall straight", error);
		}
	}

	// Find and replace on a key, as Hammer's entity search and Radiant do.
	{
		LevelMapDocument document = fixture(QStringLiteral("quake"));
		const int lightId = entityOfClass(document, QStringLiteral("light"))->id;
		const int startId = entityOfClass(document, QStringLiteral("info_player_start"))->id;
		setLevelMapEntityProperty(&document, lightId, QStringLiteral("targetname"), QStringLiteral("door_lamp"), &error);
		setLevelMapEntityProperty(&document, startId, QStringLiteral("targetname"), QStringLiteral("door_spawn"), &error);
		const QByteArray named = bytesOf(document);
		const int depth = static_cast<int>(document.undoStack.size());
		ok &= expect(levelMapEntitiesWithValue(document, QStringLiteral("TargetName"), QStringLiteral("door_"), false, false).size() == 2,
			"both names hold the prefix, whatever the key's case");
		int replaced = 0;
		ok &= expect(replaceLevelMapEntityValues(&document, QStringLiteral("targetname"), QStringLiteral("door_"), QStringLiteral("gate_"), false, false,
						 &replaced, &error)
				&& replaced == 2 && document.undoStack.size() == depth + 1,
			"the prefix is renamed on both in one step", error);
		const auto valueOf = [&document](int id) {
			for (const LevelMapEntity& entity : std::as_const(document.entities)) {
				for (const LevelMapProperty& property : entity.properties) {
					if (entity.id == id && property.key == QStringLiteral("targetname")) {
						return property.value;
					}
				}
			}
			return QString();
		};
		ok &= expect(valueOf(lightId) == QStringLiteral("gate_lamp") && valueOf(startId) == QStringLiteral("gate_spawn"), "each keeps its own suffix");
		ok &= expect(undoLevelMapEdit(&document, &error) && bytesOf(document) == named, "undo puts the names back", error);
		setLevelMapSelection(&document, {{LevelMapSelectionKind::Entity, lightId}});
		ok &= expect(replaceLevelMapEntityValues(&document, QStringLiteral("targetname"), QStringLiteral("door_lamp"), QStringLiteral("lamp"), true, true,
						 &replaced, &error)
				&& replaced == 1 && valueOf(startId) == QStringLiteral("door_spawn"),
			"a whole-value replace on the selection leaves the rest", error);
		ok &= expect(!replaceLevelMapEntityValues(&document, QStringLiteral("targetname"), QStringLiteral("nothing"), QStringLiteral("x"), true, false,
			&replaced, &error), "no match is refused");
	}

	// Doom Builder's stair builder and grid drawing, through Draw Sector.
	{
		LevelMapDocument doom;
		LevelMapCreateRequest create;
		create.game = QStringLiteral("doom");
		create.starterRoom = true;
		ok &= expect(createLevelMap(create, &doom, &error), "a Doom room for stairs", error);
		const QByteArray before = serializeLevelMap(doom).bytes;
		const int sectors = static_cast<int>(doom.doomSectors.size());
		const int depth = static_cast<int>(doom.undoStack.size());
		LevelDoomStairsRequest stairs;
		stairs.minX = -128;
		stairs.minY = -64;
		stairs.maxX = 128;
		stairs.maxY = 64;
		stairs.steps = 4;
		stairs.stepHeight = 16;
		QVector<int> ids;
		ok &= expect(drawLevelMapDoomStairs(&doom, stairs, &ids, &error) && ids.size() == 4, "four steps drawn inside the room", error);
		ok &= expect(doom.doomSectors.size() == sectors + 4 && doom.undoStack.size() == depth + 1 && doom.selection.size() == 4,
			"four selected sectors in one undo step");
		QVector<int> floors;
		for (const int id : std::as_const(ids)) {
			for (const LevelMapDoomSector& sector : std::as_const(doom.doomSectors)) {
				if (sector.id == id) {
					floors << sector.floorHeight;
				}
			}
		}
		ok &= expect(floors == QVector<int>({16, 32, 48, 64}), "each step rises 16 above the last");
		ok &= expect(undoLevelMapEdit(&doom, &error) && serializeLevelMap(doom).bytes == before, "undo takes the stairs away", error);
		ok &= expect(drawLevelMapDoomGrid(&doom, 512, 0, 768, 256, 2, 2, &ids, &error) && ids.size() == 4, "a two by two grid in the void", error);
		ok &= expect(undoLevelMapEdit(&doom, &error) && serializeLevelMap(doom).bytes == before, "undo takes the grid away", error);
		stairs.steps = 1;
		ok &= expect(!drawLevelMapDoomStairs(&doom, stairs, &ids, &error) && serializeLevelMap(doom).bytes == before, "one step is refused", error);
		LevelMapDocument quake = fixture(QStringLiteral("quake"));
		stairs.steps = 4;
		ok &= expect(!drawLevelMapDoomStairs(&quake, stairs, &ids, &error), "Quake maps have no sectors to draw", error);
	}

	// A region keeps what touches its box and writes a sealed map of it.
	{
		const LevelMapDocument document = fixture(QStringLiteral("quake"));
		const LevelMapVec3 low {-16, -16, -16, true};
		const LevelMapVec3 high {40, 40, 40, true};
		const QVector<LevelMapSelectionRef> kept = levelMapRegionObjects(document, low, high);
		ok &= expect(kept.size() == 3, "the floor, a and b touch the region", QString::number(kept.size()));
		const QVector<LevelMapSelectionRef> outside = levelMapOutsideRegionObjects(document, low, high);
		ok &= expect(outside.size() == 2 && outside.first().kind == LevelMapSelectionKind::Entity, "the light and the player start lie outside");
		LevelMapDocument region;
		LevelMapRegionReport report;
		ok &= expect(levelMapRegionDocument(document, low, high, {0, 0, 8, true}, QStringLiteral("studio/seal"), &region, &report, &error),
			"the region map builds", error);
		ok &= expect(report.kept == 3 && report.removed == 2 && report.sealBrushes == 6 && report.playerStartAdded, "the region report adds up");
		const LevelMapDocument reopened = reloaded(region);
		int sealed = 0;
		bool closed = true;
		for (const LevelMapBrush& brush : reopened.brushes) {
			closed = closed && brush.boundsSolved;
			sealed += !brush.faces.isEmpty() && brush.faces.first().textureName == QStringLiteral("studio/seal") ? 1 : 0;
		}
		const LevelMapEntity* start = entityOfClass(reopened, QStringLiteral("info_player_start"));
		ok &= expect(reopened.brushes.size() == 9 && sealed == 6 && closed, "the kept brushes and six closed walls are saved",
			QString::number(reopened.brushes.size()));
		ok &= expect(start && start->origin.valid && std::abs(start->origin.z - 8) < 1e-6 && !entityOfClass(reopened, QStringLiteral("light")),
			"the player start stands where asked and the light is gone");
		LevelMapDocument doom;
		LevelMapCreateRequest create;
		create.game = QStringLiteral("doom");
		createLevelMap(create, &doom, &error);
		ok &= expect(!levelMapRegionDocument(doom, low, high, {}, QString(), &region, &report, &error), "Doom maps have no regions");
		ok &= expect(!levelMapRegionDocument(document, high, low, {}, QString(), &region, &report, &error), "an inside-out box is refused");
	}

	// Doom Builder's auto-align: textures run on across joined walls, and
	// rows stay level where ceilings differ.
	{
		LevelMapCreateRequest create;
		create.game = QStringLiteral("doom");
		LevelMapDocument doom;
		ok &= expect(createLevelMap(create, &doom, &error), "Doom starter room", error);
		const auto side = [&doom](int id) -> const LevelMapDoomSidedef* {
			for (const LevelMapDoomSidedef& each : doom.doomSidedefs) {
				if (each.id == id) {
					return &each;
				}
			}
			return nullptr;
		};
		const int start = doom.doomLinedefs.isEmpty() ? -1 : doom.doomLinedefs.first().frontSidedef;
		ok &= expect(start >= 0 && setLevelMapSidedefProperty(&doom, start, QStringLiteral("offsetx"), QStringLiteral("5"), &error)
				&& setLevelMapSidedefProperty(&doom, start, QStringLiteral("offsety"), QStringLiteral("3"), &error),
			"offset the first wall", error);
		LevelDoomAlignRequest request;
		request.sidedef = start;
		request.widths.insert(doom.doomSidedefs.first().middleTexture.toUpper(), 64);
		int aligned = 0;
		const int steps = static_cast<int>(doom.undoStack.size());
		ok &= expect(alignLevelMapDoomWallTextures(&doom, request, &aligned, &error) && aligned == 3 && doom.undoStack.size() == steps + 1,
			"the three other walls align in one step", error);
		bool same = true;
		for (const LevelMapDoomSidedef& each : doom.doomSidedefs) {
			same &= std::lround(each.offsetX) == 5 && std::lround(each.offsetY) == 3;
		}
		ok &= expect(same, "512-unit walls of a 64-wide texture all start at offset 5, rows level");
		ok &= expect(undoLevelMapEdit(&doom, &error) && side(doom.doomSidedefs.last().id) && std::lround(doom.doomSidedefs.last().offsetX) == 0,
			"one undo restores every wall", error);
		// Without a width the length walked is kept: 517, 1029 and 1541.
		request.widths.clear();
		request.alignY = false;
		ok &= expect(alignLevelMapDoomWallTextures(&doom, request, &aligned, &error), "align without a width", error);
		QList<long> offsets;
		for (const LevelMapDoomSidedef& each : doom.doomSidedefs) {
			offsets << std::lround(each.offsetX);
		}
		std::sort(offsets.begin(), offsets.end());
		ok &= expect(offsets == QList<long>({5, 517, 1029, 1541}), "offsets run on by each wall's length",
			QStringLiteral("%1 %2 %3 %4").arg(offsets.value(0)).arg(offsets.value(1)).arg(offsets.value(2)).arg(offsets.value(3)));
		ok &= expect(std::lround(side(doom.doomSidedefs.last().id)->offsetY) == 0, "X alone leaves Y as it was");
		undoLevelMapEdit(&doom, &error);

		// Two rooms side by side, the second's ceiling 64 higher: walls joined
		// across them keep their rows level, 64 lower in the taller room.
		LevelMapDocument rooms;
		create.starterRoom = false;
		ok &= expect(createLevelMap(create, &rooms, &error)
				&& drawLevelMapDoomSector(&rooms, {{0, 0, 0, true}, {0, 128, 0, true}, {128, 128, 0, true}, {128, 0, 0, true}}, nullptr, &error)
				&& drawLevelMapDoomSector(&rooms, {{128, 0, 0, true}, {128, 128, 0, true}, {256, 128, 0, true}, {256, 0, 0, true}}, nullptr, &error),
			"two rooms", error);
		ok &= expect(rooms.doomSectors.size() == 2
				&& setLevelMapSectorProperty(&rooms, rooms.doomSectors.last().id, QStringLiteral("ceilingheight"),
					QString::number(rooms.doomSectors.first().ceilingHeight + 64), &error),
			"raise the second room's ceiling", error);
		QHash<int, QPointF> at;
		for (const LevelMapDoomVertex& vertex : rooms.doomVertices) {
			at.insert(vertex.id, {vertex.x, vertex.y});
		}
		int lowSide = -1;
		int highSide = -1;
		for (const LevelMapDoomLinedef& line : rooms.doomLinedefs) {
			const QPointF a = at.value(line.startVertex);
			const QPointF b = at.value(line.endVertex);
			if (line.backSidedef >= 0 || std::abs(a.y()) > 1e-6 || std::abs(b.y()) > 1e-6) {
				continue;
			}
			(std::max(a.x(), b.x()) <= 128.0 ? lowSide : highSide) = line.frontSidedef;
		}
		for (const LevelMapDoomSidedef& each : rooms.doomSidedefs) {
			setLevelMapSidedefProperty(&rooms, each.id, QStringLiteral("middle"), QStringLiteral("STARTAN"), &error);
		}
		LevelDoomAlignRequest across;
		across.sidedef = lowSide;
		ok &= expect(lowSide >= 0 && highSide >= 0 && alignLevelMapDoomWallTextures(&rooms, across, &aligned, &error), "align across the rooms",
			error);
		long highY = -1;
		for (const LevelMapDoomSidedef& each : rooms.doomSidedefs) {
			highY = each.id == highSide ? std::lround(each.offsetY) : highY;
		}
		ok &= expect(highY == -64, "the taller room's wall drops its texture by the 64 units its ceiling rises", QString::number(highY));

		// Refusals: a side without the texture, and Quake maps.
		LevelDoomAlignRequest none;
		none.sidedef = 99999;
		ok &= expect(!alignLevelMapDoomWallTextures(&doom, none, &aligned, &error), "an unknown side is refused");
		LevelMapDocument quake = fixture(QStringLiteral("quake"));
		ok &= expect(!alignLevelMapDoomWallTextures(&quake, request, &aligned, &error), "Quake maps have no walls to align");
	}

	// Doom Builder's Make Sectors: the lines around a point become a sector.
	{
		LevelMapCreateRequest create;
		create.game = QStringLiteral("doom");
		LevelMapDocument doom;
		int inner = -1;
		ok &= expect(createLevelMap(create, &doom, &error)
				&& drawLevelMapDoomSector(&doom, {{-64, -64, 0, true}, {-64, 64, 0, true}, {64, 64, 0, true}, {64, -64, 0, true}}, &inner, &error),
			"a room with a sector drawn inside it", error);
		ok &= expect(setLevelMapSectorProperty(&doom, inner, QStringLiteral("floorheight"), QStringLiteral("24"), &error)
				&& deleteLevelMapObjects(&doom, {{LevelMapSelectionKind::DoomSector, inner}}, &error) && doom.doomSectors.size() == 1,
			"raise the inner sector, then delete it, leaving its lines as walls", error);
		const auto twoSided = [](const LevelMapDocument& map) {
			int count = 0;
			for (const LevelMapDoomLinedef& linedef : map.doomLinedefs) {
				count += linedef.frontSidedef >= 0 && linedef.backSidedef >= 0 ? 1 : 0;
			}
			return count;
		};
		ok &= expect(twoSided(doom) == 0, "the inner lines face only the room");
		const QByteArray walls = serializeLevelMap(doom).bytes;
		int made = -1;
		ok &= expect(makeLevelMapDoomSectorAt(&doom, 0, 0, &made, &error) && doom.doomSectors.size() == 2 && made >= 0
				&& doom.selection.size() == 1 && doom.selection.first().objectId == made,
			"clicking inside the lines makes a sector there, selected", error);
		ok &= expect(twoSided(doom) == 4, "the four lines open onto the new sector", QString::number(twoSided(doom)));
		int insideSides = 0;
		for (const LevelMapDoomSidedef& side : doom.doomSidedefs) {
			insideSides += side.sector == made ? 1 : 0;
		}
		ok &= expect(insideSides == 4, "each of the four lines has a side facing the new sector", QString::number(insideSides));
		ok &= expect(undoLevelMapEdit(&doom, &error) && serializeLevelMap(doom).bytes == walls, "one undo takes it back exactly", error);

		// Around the walls, in the room: the room's area with the walls as an
		// island becomes a new sector taking the room's place.
		ok &= expect(makeLevelMapDoomSectorAt(&doom, -200, -200, &made, &error) && doom.doomSectors.size() == 1, "the room's area made anew", error);
		int roomSides = 0;
		for (const LevelMapDoomSidedef& side : doom.doomSidedefs) {
			roomSides += side.sector == made ? 1 : 0;
		}
		ok &= expect(roomSides == 8, "the outer walls and the island's walls all face it", QString::number(roomSides));
		undoLevelMapEdit(&doom, &error);

		// Outside every shape there is nothing to make.
		ok &= expect(!makeLevelMapDoomSectorAt(&doom, 9000, 9000, &made, &error) && serializeLevelMap(doom).bytes == walls,
			"a point in the void is refused, changing nothing", error);
		LevelMapDocument quake = fixture(QStringLiteral("quake"));
		ok &= expect(!makeLevelMapDoomSectorAt(&quake, 0, 0, &made, &error), "Quake maps have no sectors");
	}

	// Folding steps keeps the saved state honest.
	{
		LevelMapDocument document = fixture(QStringLiteral("quake"));
		addLevelMapEntity(&document, QStringLiteral("info_null"), {0, 0, 0, true});
		markLevelMapSaved(&document);
		addLevelMapEntity(&document, QStringLiteral("info_null"), {8, 0, 0, true});
		addLevelMapEntity(&document, QStringLiteral("info_null"), {16, 0, 0, true});
		const int depth = static_cast<int>(document.undoStack.size());
		ok &= expect(collapseLevelMapUndoSteps(&document, 2, QStringLiteral("Two nulls"), QStringLiteral("Remove two nulls"), &error), "fold two steps", error);
		ok &= expect(document.undoStack.size() == depth - 1 && document.savedUndoDepth == depth - 2, "the save point stays below the batch");
		ok &= expect(undoLevelMapEdit(&document, &error) && document.editState == QStringLiteral("saved"), "one undo returns to the saved map", error);
		ok &= expect(!collapseLevelMapUndoSteps(&document, 99, QString(), QString(), &error), "folding more steps than exist is refused");
	}

	std::cout << (ok ? "level map tools smoke passed" : "level map tools smoke failed") << "\n";
	return ok ? 0 : 1;
}
