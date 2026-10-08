// Checks the brush shapes of the Levels Shapes tab and `map add-shape`: the
// arch, ring, stairs and room geometry fitted to a box, the errors for shapes
// that cannot be built, and that adding one is a single undo step that undoes
// to the exact bytes it started from and survives a save and reload.

#include "core/level_document.h"
#include "core/level_map.h"
#include "core/level_shapes.h"

#include <QCoreApplication>

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

LevelMapDocument fixture(const QString& game)
{
	LevelMapDocument document;
	LevelMapCreateRequest create;
	create.game = game;
	create.starterRoom = false;
	QString error;
	createLevelMap(create, &document, &error);
	addLevelMapBoxBrush(&document, {-128, -128, -16, true}, {128, 128, 0, true}, QStringLiteral("studio/floor"), nullptr, &error);
	const LevelMapSerialized written = serializeLevelMap(document);
	LevelMapDocument loaded;
	LevelMapLoadRequest request;
	request.path = QStringLiteral("shapes-%1.map").arg(game);
	request.engineHint = game == QStringLiteral("quake3") ? QStringLiteral("idtech3") : QStringLiteral("idtech2");
	loadLevelMapBytes(request, written.bytes, &loaded, &error);
	return loaded;
}

LevelMapDocument reloaded(const LevelMapDocument& document)
{
	LevelMapDocument loaded;
	LevelMapLoadRequest request;
	request.path = document.sourcePath;
	request.engineHint = document.format == LevelMapFormat::Quake3Map ? QStringLiteral("idtech3") : QStringLiteral("idtech2");
	QString error;
	loadLevelMapBytes(request, serializeLevelMap(document).bytes, &loaded, &error);
	return loaded;
}

struct Extent {
	LevelMapVec3 mins {1e9, 1e9, 1e9, true};
	LevelMapVec3 maxs {-1e9, -1e9, -1e9, true};
};

Extent extentOf(const QVector<QVector<LevelMapVec3>>& hulls)
{
	Extent extent;
	for (const auto& hull : hulls) {
		for (const LevelMapVec3& p : hull) {
			extent.mins = {std::min(extent.mins.x, p.x), std::min(extent.mins.y, p.y), std::min(extent.mins.z, p.z), true};
			extent.maxs = {std::max(extent.maxs.x, p.x), std::max(extent.maxs.y, p.y), std::max(extent.maxs.z, p.z), true};
		}
	}
	return extent;
}

bool same(const LevelMapVec3& a, double x, double y, double z)
{
	return std::abs(a.x - x) < 1e-9 && std::abs(a.y - y) < 1e-9 && std::abs(a.z - z) < 1e-9;
}

bool contains(const QVector<LevelMapVec3>& hull, double x, double y, double z)
{
	for (const LevelMapVec3& p : hull) {
		if (same(p, x, y, z)) {
			return true;
		}
	}
	return false;
}

bool checkCatalogue()
{
	bool ok = true;
	const QStringList ids = levelShapeIds();
	ok &= expect(ids.size() == 9 && ids.first() == QStringLiteral("box") && ids.contains(QStringLiteral("arch")) && ids.contains(QStringLiteral("room")),
		"The shapes are the five primitives and the four compound shapes", ids.join(QLatin1Char(',')));
	for (const QString& id : ids) {
		ok &= expect(!levelShapeLabel(id).isEmpty() && levelShapeLabel(id) != id && !levelShapeDescription(id).isEmpty(),
			"Every shape has a label and a description", id);
		ok &= expect(!levelShapeIconName(id).isEmpty(), "Every shape has a glyph", id);
	}
	ok &= expect(!levelShapeIsCompound(QStringLiteral("cylinder")) && levelShapeIsCompound(QStringLiteral("stairs")),
		"Cylinders are one brush and stairs are several");
	ok &= expect(levelShapeParameters(QStringLiteral("arch")).contains(QStringLiteral("arc"))
			&& !levelShapeParameters(QStringLiteral("ring")).contains(QStringLiteral("arc"))
			&& levelShapeParameters(QStringLiteral("stairs")) == QStringList({QStringLiteral("steps"), QStringLiteral("rise")})
			&& levelShapeParameters(QStringLiteral("box")).isEmpty(),
		"Each shape reads only its own settings");
	ok &= expect(!isLevelShape(QStringLiteral("torus")) && isLevelShape(QStringLiteral("ring")), "Unknown shapes are refused");
	return ok;
}

bool checkArch()
{
	bool ok = true;
	// A doorway drawn in the front view: Y is the depth, X the right and Z
	// the up of that view, so the default half circle stands over the box.
	LevelShapeRequest request;
	request.shape = QStringLiteral("arch");
	request.axis = 1;
	request.mins = {-64, -16, 0, true};
	request.maxs = {64, 16, 64, true};
	request.sides = 8;
	request.thickness = 16;
	QVector<QVector<LevelMapVec3>> hulls;
	QString error;
	ok &= expect(levelShapeHulls(request, &hulls, &error), "A doorway arch builds", error);
	ok &= expect(hulls.size() == 8, "An arch has one brush per segment", QString::number(hulls.size()));
	const Extent extent = extentOf(hulls);
	ok &= expect(same(extent.mins, -64, -16, 0) && same(extent.maxs, 64, 16, 64), "The arch fills the box it was drawn in");
	if (!hulls.isEmpty()) {
		ok &= expect(hulls.first().size() == 8 && contains(hulls.first(), 64, -16, 0) && contains(hulls.first(), 48, 16, 0),
			"The first segment starts at the right with its wall thickness inside");
		ok &= expect(contains(hulls.last(), -64, -16, 0) && contains(hulls.last(), -48, 16, 0), "The last segment ends at the left");
	}
	// Neighbouring segments share their corners exactly.
	bool shared = true;
	for (int segment = 1; segment < hulls.size(); ++segment) {
		int common = 0;
		for (const LevelMapVec3& p : hulls.at(segment)) {
			common += contains(hulls.at(segment - 1), p.x, p.y, p.z) ? 1 : 0;
		}
		shared = shared && common == 4;
	}
	ok &= expect(shared, "Each segment meets the next along a whole face");

	// Walls as thick as the radius make slices of a pie.
	request.thickness = 200;
	ok &= expect(levelShapeHulls(request, &hulls, &error) && hulls.size() == 8 && hulls.first().size() == 6 && contains(hulls.first(), 0, -16, 0),
		"A wall thicker than the radius closes the arch into a half disc", error);

	// The arc's own extent fills the box: a quarter arc's centre sits at a
	// corner of it.
	request.thickness = 8;
	request.arc = 90;
	request.startAngle = 90;
	request.sides = 4;
	request.axis = 2;
	request.mins = {0, 0, 0, true};
	request.maxs = {64, 64, 32, true};
	ok &= expect(levelShapeHulls(request, &hulls, &error), "A quarter arch builds", error);
	if (!hulls.isEmpty()) {
		ok &= expect(contains(hulls.first(), 64, 64, 0) && contains(hulls.last(), 0, 0, 0) && contains(hulls.last(), 8, 0, 32),
			"A quarter arch from the top turns towards the left about the box's lower right corner");
	}

	request.arc = 360;
	request.sides = 2;
	ok &= expect(!levelShapeHulls(request, &hulls, &error) && hulls.isEmpty() && error.contains(QStringLiteral("180")),
		"A segment turning half a circle or more is refused", error);
	request.sides = 12;
	request.thickness = 0.5;
	ok &= expect(!levelShapeHulls(request, &hulls, &error), "Walls under a unit are refused", error);
	request.thickness = 8;
	request.texture = QStringLiteral("bad name");
	ok &= expect(!levelShapeHulls(request, &hulls, &error), "Materials with spaces are refused", error);
	return ok;
}

bool checkRing()
{
	bool ok = true;
	LevelShapeRequest request;
	request.shape = QStringLiteral("ring");
	request.mins = {-128, -128, 0, true};
	request.maxs = {128, 128, 96, true};
	request.sides = 12;
	request.thickness = 24;
	request.arc = 45; // Rings ignore the sweep.
	QVector<QVector<LevelMapVec3>> hulls;
	QString error;
	ok &= expect(levelShapeHulls(request, &hulls, &error) && hulls.size() == 12, "A ring has one brush per segment", error);
	const Extent extent = extentOf(hulls);
	ok &= expect(same(extent.mins, -128, -128, 0) && same(extent.maxs, 128, 128, 96), "The ring fills the box");
	if (hulls.size() == 12) {
		int common = 0;
		for (const LevelMapVec3& p : hulls.last()) {
			common += contains(hulls.first(), p.x, p.y, p.z) ? 1 : 0;
		}
		ok &= expect(common == 4, "The ring closes on its first segment");
	}
	request.sides = 2;
	ok &= expect(!levelShapeHulls(request, &hulls, &error), "A ring needs three segments", error);
	return ok;
}

bool checkStairs()
{
	bool ok = true;
	LevelShapeRequest request;
	request.shape = QStringLiteral("stairs");
	request.mins = {0, 0, 0, true};
	request.maxs = {128, 64, 64, true};
	request.steps = 4;
	QVector<QVector<LevelMapVec3>> hulls;
	QString error;
	ok &= expect(levelShapeHulls(request, &hulls, &error) && hulls.size() == 4, "Stairs have one brush per step", error);
	if (hulls.size() == 4) {
		bool climbing = true;
		for (int step = 0; step < 4; ++step) {
			const Extent extent = extentOf({hulls.at(step)});
			climbing = climbing && same(extent.mins, 32.0 * step, 0, 0) && same(extent.maxs, 32.0 * (step + 1), 64, 16.0 * (step + 1));
		}
		ok &= expect(climbing, "Automatic stairs climb solidly along the longer side towards +X");
	}
	request.rise = QStringLiteral("-y");
	ok &= expect(levelShapeHulls(request, &hulls, &error) && hulls.size() == 4, "Stairs climbing towards -Y build", error);
	if (hulls.size() == 4) {
		const Extent first = extentOf({hulls.first()});
		const Extent last = extentOf({hulls.last()});
		ok &= expect(same(first.mins, 0, 48, 0) && same(first.maxs, 128, 64, 16) && same(last.mins, 0, 0, 0) && same(last.maxs, 128, 16, 64),
			"Stairs climbing towards -Y start at the +Y end");
	}
	request.rise = QStringLiteral("up");
	ok &= expect(!levelShapeHulls(request, &hulls, &error), "An unknown direction is refused", error);
	request.rise = QStringLiteral("auto");
	request.steps = 65;
	ok &= expect(!levelShapeHulls(request, &hulls, &error), "More than 64 steps are refused", error);
	request.steps = 40;
	request.maxs = {128, 64, 32, true};
	ok &= expect(!levelShapeHulls(request, &hulls, &error), "Steps under a unit high are refused", error);
	return ok;
}

bool checkRoom()
{
	bool ok = true;
	LevelShapeRequest request;
	request.shape = QStringLiteral("room");
	request.mins = {0, 0, 0, true};
	request.maxs = {256, 192, 128, true};
	request.thickness = 16;
	QVector<QVector<LevelMapVec3>> hulls;
	QString error;
	ok &= expect(levelShapeHulls(request, &hulls, &error) && hulls.size() == 6, "A room has six walls", error);
	double volume = 0.0;
	for (const auto& hull : hulls) {
		const Extent extent = extentOf({hull});
		volume += (extent.maxs.x - extent.mins.x) * (extent.maxs.y - extent.mins.y) * (extent.maxs.z - extent.mins.z);
	}
	ok &= expect(std::abs(volume - (256.0 * 192 * 128 - 224.0 * 160 * 96)) < 1e-6, "The walls fill the box but its inside, without overlapping",
		QString::number(volume));
	request.thickness = 64;
	ok &= expect(!levelShapeHulls(request, &hulls, &error), "Walls that leave no inside are refused", error);
	return ok;
}

bool checkDocument(const QString& game)
{
	bool ok = true;
	LevelMapDocument document = fixture(game);
	const QByteArray before = serializeLevelMap(document).bytes;
	const int brushes = static_cast<int>(document.brushes.size());
	const int steps = static_cast<int>(document.undoStack.size());
	LevelShapeRequest request;
	request.shape = QStringLiteral("arch");
	request.axis = 1;
	request.mins = {-64, -16, 0, true};
	request.maxs = {64, 16, 96, true};
	request.sides = 6;
	request.thickness = 16;
	request.texture = QStringLiteral("studio/arch");
	QVector<int> ids;
	QString error;
	ok &= expect(addLevelMapShape(&document, request, &ids, &error), "An arch is added to the map", game + QStringLiteral(": ") + error);
	ok &= expect(ids.size() == 6 && document.brushes.size() == brushes + 6, "Each segment is a new brush", game);
	ok &= expect(document.undoStack.size() == steps + 1, "The arch is one undo step", game);
	ok &= expect(document.selection.size() == 6, "The new brushes are selected", game);
	const LevelMapDocument saved = reloaded(document);
	int closed = 0;
	for (const LevelMapBrush& brush : saved.brushes) {
		closed += brush.boundsSolved && !brush.faces.isEmpty() && brush.faces.first().textureName == QStringLiteral("studio/arch") ? 1 : 0;
	}
	ok &= expect(closed == 6, "The arch's brushes are closed after a save and reload", QStringLiteral("%1 %2").arg(game).arg(closed));
	ok &= expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == before, "Undo restores the exact bytes", game);
	ok &= expect(redoLevelMapEdit(&document, &error) && document.brushes.size() == brushes + 6, "Redo brings the arch back", game);

	request.shape = QStringLiteral("room");
	request.mins = {256, 0, 0, true};
	request.maxs = {512, 256, 128, true};
	ok &= expect(addLevelMapShape(&document, request, &ids, &error) && ids.size() == 6, "A room is added", error);
	request.shape = QStringLiteral("cylinder");
	request.axis = 2;
	request.sides = 12;
	ok &= expect(addLevelMapShape(&document, request, &ids, &error) && ids.size() == 1, "A one-brush shape is added through the primitives", error);

	// Radiant's arbitrary-sided brush: the cylinder becomes stairs filling
	// its bounds, as one step that undoes to the cylinder.
	const QVector<int> cylinder = ids;
	const QByteArray withCylinder = serializeLevelMap(document).bytes;
	const int stepsBefore = static_cast<int>(document.undoStack.size());
	request.shape = QStringLiteral("stairs");
	request.steps = 4;
	request.mins = {0, 0, 0, true};
	request.maxs = {1, 1, 1, true};
	QVector<int> stairs;
	ok &= expect(replaceLevelMapBrushesWithShape(&document, cylinder, request, &stairs, &error) && stairs.size() == 4,
		"Brushes are replaced by a shape", error);
	ok &= expect(document.undoStack.size() == stepsBefore + 1 && document.selection.size() == 4, "The replacement is one step that selects the shape");
	bool gone = true;
	LevelMapVec3 low {1e9, 1e9, 1e9, true};
	LevelMapVec3 high {-1e9, -1e9, -1e9, true};
	for (const LevelMapBrush& brush : document.brushes) {
		gone = gone && brush.id != cylinder.value(0);
		if (stairs.contains(brush.id)) {
			low = {std::min(low.x, brush.mins.x), std::min(low.y, brush.mins.y), std::min(low.z, brush.mins.z), true};
			high = {std::max(high.x, brush.maxs.x), std::max(high.y, brush.maxs.y), std::max(high.z, brush.maxs.z), true};
		}
	}
	ok &= expect(gone && same(low, 256, 0, 0) && same(high, 512, 256, 128), "The shape fills the replaced brush's bounds, not the request's");
	ok &= expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == withCylinder, "Undo brings the replaced brush back", error);
	int tiedEntity = -1;
	ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, cylinder.value(0)}}, &error)
			&& tieLevelMapSelectionToEntity(&document, QStringLiteral("func_wall"), {}, &tiedEntity, &error),
		"The cylinder becomes a func_wall", error);
	const QByteArray tied = serializeLevelMap(document).bytes;
	ok &= expect(!replaceLevelMapBrushesWithShape(&document, cylinder, request, &stairs, &error) && serializeLevelMap(document).bytes == tied,
		"A brush entity's brushes are not replaced", error);

	request.shape = QStringLiteral("stairs");
	request.steps = 200;
	request.mins = {256, 0, 0, true};
	request.maxs = {512, 256, 128, true};
	const QByteArray unchanged = serializeLevelMap(document).bytes;
	ok &= expect(!addLevelMapShape(&document, request, &ids, &error) && serializeLevelMap(document).bytes == unchanged,
		"A shape that cannot be built leaves the map unchanged", error);
	return ok;
}

} // namespace

int main(int argc, char* argv[])
{
	QCoreApplication app(argc, argv);
	bool ok = true;
	ok &= checkCatalogue();
	ok &= checkArch();
	ok &= checkRing();
	ok &= checkStairs();
	ok &= checkRoom();
	ok &= checkDocument(QStringLiteral("quake"));
	ok &= checkDocument(QStringLiteral("quake3"));
	if (ok) {
		std::cout << "Level shapes smoke test passed.\n";
	}
	return ok ? 0 : 1;
}
