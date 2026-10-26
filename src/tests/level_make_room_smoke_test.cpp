// Make Room's caulk and open-face options in the core and on the command
// line (NetRadiant Custom's CSG_MakeRoom): every wall face but the one the
// room sees takes the game's caulk, picked faces get no wall and the walls
// beside them stop flush, keeping the open face's material where they line
// the opening. The same options in the Hollow and Hollow Touching modes, the
// game caulk names, refusals that change nothing, scene locks, exact undo
// and redo, a reload, `map hollow --caulk/--caulk-texture/--open-face`, and
// the saved Make Room options (their checks, settings and `editor make-room`).

#include "core/level_document.h"
#include "core/level_hollow.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/map_geometry.h"
#include "core/studio_settings.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>

using namespace vibestudio;

namespace {

int failures = 0;

bool expect(bool condition, const char* message, const QString& detail = {})
{
	if (!condition) {
		++failures;
		std::cerr << "FAIL: " << message;
		if (!detail.isEmpty()) {
			std::cerr << " (" << qPrintable(detail) << ")";
		}
		std::cerr << "\n";
	}
	return condition;
}

bool near(double a, double b, double tolerance = 1e-4) { return std::abs(a - b) <= tolerance; }

QByteArray bytesOf(const LevelMapDocument& document) { return serializeLevelMap(document).bytes; }

LevelMapDocument reloaded(const LevelMapDocument& document)
{
	LevelMapDocument loaded;
	QString error;
	loadLevelMapBytes({QStringLiteral("make-room.map"), {}, document.format == LevelMapFormat::Quake3Map ? QStringLiteral("idtech3") : QString()},
		bytesOf(document), &loaded, &error);
	return loaded;
}

LevelMapDocument mapWith(const QString& game, const QVector<std::pair<LevelMapVec3, LevelMapVec3>>& boxes, const QString& texture)
{
	LevelMapDocument document;
	LevelMapCreateRequest create;
	create.game = game;
	create.starterRoom = false;
	QString error;
	createLevelMap(create, &document, &error);
	for (const auto& [mins, maxs] : boxes) {
		addLevelMapBoxBrush(&document, mins, maxs, texture, nullptr, &error);
	}
	return reloaded(document);
}

// The zero-based face of `brush` whose outward normal is `axis` (0..2) with
// `sign`.
int faceAlong(const LevelMapBrush& brush, int axis, int sign)
{
	const MapBrushGeometry geometry = solveBrushGeometry(brush.faces, brush.id, brush.entityId);
	for (int face = 0; face < geometry.faces.size(); ++face) {
		const MapPlane& plane = geometry.faces.at(face).plane;
		const double along = axis == 0 ? plane.normalX : (axis == 1 ? plane.normalY : plane.normalZ);
		if (geometry.faces.at(face).isValid() && near(along, sign)) {
			return face;
		}
	}
	return -1;
}

// What the walls of a room hollowed out of a box centred on the origin show:
// faces facing the room (their normal points back at the origin), faces on
// the open top plane, caulk faces and anything else.
struct WallFaces {
	int roomFacing = 0;
	int lining = 0;
	int caulked = 0;
	int other = 0;
	int wallsWithOneRoomFace = 0;
	QStringList problems;
};

WallFaces wallFaces(const LevelMapDocument& document, const QString& caulk, const QString& roomTexture, const QString& liningTexture, double top)
{
	WallFaces result;
	for (const LevelMapBrush& wall : document.brushes) {
		const MapBrushGeometry geometry = solveBrushGeometry(wall.faces, wall.id, wall.entityId);
		int roomFaces = 0;
		for (int face = 0; face < geometry.faces.size() && face < wall.faces.size(); ++face) {
			const MapFacePolygon& polygon = geometry.faces.at(face);
			if (!polygon.isValid()) {
				continue;
			}
			double cx = 0, cy = 0, cz = 0;
			for (const LevelMapVec3& point : polygon.points) {
				cx += point.x;
				cy += point.y;
				cz += point.z;
			}
			cx /= polygon.points.size();
			cy /= polygon.points.size();
			cz /= polygon.points.size();
			const double towards = polygon.plane.normalX * cx + polygon.plane.normalY * cy + polygon.plane.normalZ * cz;
			const QString& texture = wall.faces.at(face).textureName;
			if (towards < -1e-3) {
				++roomFaces;
				++result.roomFacing;
				if (texture != roomTexture) {
					result.problems << QStringLiteral("brush %1 room face %2 is %3").arg(wall.id).arg(face).arg(texture);
				}
			} else if (near(cz, top) && near(polygon.plane.normalZ, 1.0)) {
				++result.lining;
				if (texture != liningTexture) {
					result.problems << QStringLiteral("brush %1 lining face %2 is %3").arg(wall.id).arg(face).arg(texture);
				}
			} else if (texture == caulk) {
				++result.caulked;
			} else {
				++result.other;
				result.problems << QStringLiteral("brush %1 face %2 is %3").arg(wall.id).arg(face).arg(texture);
			}
		}
		result.wallsWithOneRoomFace += roomFaces == 1;
	}
	return result;
}

void checkGameCaulk()
{
	const LevelMapDocument quake3 = mapWith(QStringLiteral("quake3"), {{{-64, -64, -64, true}, {64, 64, 64, true}}}, QStringLiteral("studio/wall"));
	const LevelMapDocument quake = mapWith(QStringLiteral("quake"), {{{-64, -64, -64, true}, {64, 64, 64, true}}}, QStringLiteral("wall"));
	const LevelMapDocument quake2 = mapWith(QStringLiteral("quake2"), {{{-64, -64, -64, true}, {64, 64, 64, true}}}, QStringLiteral("e1u1/wall"));
	LevelMapDocument doom;
	LevelMapCreateRequest create;
	create.game = QStringLiteral("doom");
	createLevelMap(create, &doom);
	expect(levelMapCaulkTexture(quake3) == QLatin1String("common/caulk"), "Quake III caulks with common/caulk", levelMapCaulkTexture(quake3));
	expect(levelMapCaulkTexture(quake) == QLatin1String("skip"), "Quake caulks with skip", levelMapCaulkTexture(quake));
	expect(levelMapCaulkTexture(quake2).isEmpty() && levelMapCaulkTexture(doom).isEmpty(), "Quake II and Doom have no stock caulk");
	LevelRoomPreferences preferences;
	expect(preferences.caulk && preferences.openPickedFaces && preferences.caulkTexture.isEmpty(), "Make Room starts as NetRadiant Custom's: caulk, open picks");
	expect(levelRoomCaulkTexture(quake3, preferences) == QLatin1String("common/caulk"), "the default preferences use the game's caulk");
	preferences.caulkTexture = QStringLiteral(" studio/nodraw ");
	expect(levelRoomCaulkTexture(quake2, preferences) == QLatin1String("studio/nodraw"), "a named material wins, trimmed");
	preferences.caulk = false;
	expect(levelRoomCaulkTexture(quake3, preferences).isEmpty(), "caulk off uses none");
}

// The shared checks Make Room Options and editor make-room apply.
void checkPreferences()
{
	QString error;
	expect(levelRoomPreferencesProblem({}).isEmpty() && levelRoomPreferencesProblem({true, QStringLiteral("common/nodraw"), false}).isEmpty(),
		"the defaults and a plain material name can be saved");
	expect(!levelRoomPreferencesProblem({true, QStringLiteral("bad name"), true}).isEmpty(), "a material name with a space cannot");
	expect(!levelRoomPreferencesProblem({true, QString(1025, QLatin1Char('a')), true}).isEmpty(), "nor one over 1024 characters");
	const QJsonObject json = levelRoomPreferencesJson({});
	expect(json.value(QStringLiteral("caulk")).toBool() && json.value(QStringLiteral("openPickedFaces")).toBool()
			&& json.value(QStringLiteral("caulkTexture")).toString().isEmpty() && json.size() == 3,
		"the JSON form names every option");
	LevelRoomPreferences preferences;
	expect(updateLevelRoomPreferences({{QStringLiteral("caulkTexture"), QStringLiteral(" studio/hidden ")}, {QStringLiteral("openPickedFaces"), false}},
			   &preferences, &error)
			&& preferences.caulk && preferences.caulkTexture == QLatin1String("studio/hidden") && !preferences.openPickedFaces,
		"updating changes only the fields given", error);
	const LevelRoomPreferences before = preferences;
	expect(!updateLevelRoomPreferences({{QStringLiteral("caulk"), false}, {QStringLiteral("colour"), true}}, &preferences, &error) && !error.isEmpty()
			&& preferences == before,
		"an unknown field changes nothing", error);
	expect(!updateLevelRoomPreferences({{QStringLiteral("caulk"), QStringLiteral("yes")}}, &preferences, &error) && preferences == before,
		"caulk takes a boolean", error);
	expect(!updateLevelRoomPreferences({{QStringLiteral("caulkTexture"), QStringLiteral("a{b")}}, &preferences, &error) && preferences == before,
		"a material name with a brace is refused", error);
	// Settings refuse such a name, and a hand-edited one falls back to the
	// game's caulk instead of failing every Make Room.
	QTemporaryDir temp;
	if (expect(temp.isValid(), "temporary settings folder")) {
		const QString path = temp.filePath(QStringLiteral("settings.ini"));
		{
			StudioSettings writer(path);
			expect(!writer.setLevelRoomPreferences({true, QStringLiteral("bad name"), true}), "settings refuse a name no map could hold");
			expect(writer.setLevelRoomPreferences({false, QStringLiteral("studio/hidden"), false}), "settings save a plain name");
		}
		{
			QSettings raw(path, QSettings::IniFormat);
			raw.setValue(QStringLiteral("levels/makeRoom/caulkTexture"), QStringLiteral("bad name"));
			raw.sync();
		}
		const StudioSettings reader(path, StudioSettings::AccessMode::ReadOnly);
		const LevelRoomPreferences read = reader.levelRoomPreferences();
		expect(!read.caulk && !read.openPickedFaces && read.caulkTexture.isEmpty(), "a hand-edited bad name reads as the game's caulk", read.caulkTexture);
	}
}

void checkRoomCaulk()
{
	QString error;
	LevelMapDocument document = mapWith(QStringLiteral("quake3"), {{{-64, -64, -64, true}, {64, 64, 64, true}}}, QStringLiteral("studio/wall"));
	const QByteArray original = bytesOf(document);
	setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, document.brushes.first().id}});
	LevelMapHollowOptions options;
	options.caulkTexture = levelMapCaulkTexture(document);
	LevelMapHollowReport report;
	if (!expect(hollowLevelMapSelection(&document, 16, LevelMapHollowMode::Room, options, &report, &error), "Make Room with caulk", error)) {
		return;
	}
	expect(report.hollowed == 1 && report.walls == 6 && report.openFaces == 0 && document.brushes.size() == 6 && document.undoStack.size() == 1,
		"six caulked walls in one undo step", QString::number(report.walls));
	const WallFaces faces = wallFaces(document, QStringLiteral("common/caulk"), QStringLiteral("studio/wall"), QStringLiteral("studio/wall"), 1e9);
	expect(faces.problems.isEmpty() && faces.other == 0, "every wall face is the room's or caulk", faces.problems.join(QLatin1Char('|')));
	expect(faces.roomFacing == 6 && faces.wallsWithOneRoomFace == 6, "each wall keeps exactly one face the room sees, textured as before");
	expect(faces.caulked == 30 && report.caulkedFaces == 30, "the other five faces of each wall are caulk", QString::number(report.caulkedFaces));
	double low = 1e9, high = -1e9;
	for (const LevelMapBrush& wall : document.brushes) {
		low = std::min(low, wall.mins.x);
		high = std::max(high, wall.maxs.z);
	}
	expect(near(low, -80) && near(high, 80), "the caulked walls still fill the grown box's corners");
	const QByteArray after = bytesOf(document);
	const LevelMapDocument back = reloaded(document);
	const WallFaces reread = wallFaces(back, QStringLiteral("common/caulk"), QStringLiteral("studio/wall"), QStringLiteral("studio/wall"), 1e9);
	expect(back.brushes.size() == 6 && reread.caulked == 30 && reread.roomFacing == 6, "the caulked walls survive a reload");
	expect(undoLevelMapEdit(&document, &error) && bytesOf(document) == original, "undo brings the brush back exactly", error);
	expect(redoLevelMapEdit(&document, &error) && bytesOf(document) == after, "redo rebuilds the same caulked walls", error);
}

void checkOpenFaces()
{
	QString error;
	for (const LevelMapHollowMode mode : {LevelMapHollowMode::Room, LevelMapHollowMode::Overlap, LevelMapHollowMode::Touch}) {
		const QString label = levelMapHollowModeId(mode);
		LevelMapDocument document = mapWith(QStringLiteral("quake3"), {{{-64, -64, -64, true}, {64, 64, 64, true}}}, QStringLiteral("studio/wall"));
		const int top = faceAlong(document.brushes.first(), 2, 1);
		if (!expect(top >= 0 && setLevelMapBrushFaceProperty(&document, document.brushes.first().id, top, QStringLiteral("texture"), QStringLiteral("studio/door"),
								  &error),
				"a door material on the top face", error)) {
			continue;
		}
		document = reloaded(document);
		const QByteArray original = bytesOf(document);
		const int id = document.brushes.first().id;
		setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, id}});
		LevelMapHollowOptions options;
		options.caulkTexture = QStringLiteral("common/caulk");
		options.openFaces = {{id, top}};
		LevelMapHollowReport report;
		if (!expect(hollowLevelMapSelection(&document, 16, mode, options, &report, &error), "hollow with an open face", label + QLatin1Char(' ') + error)) {
			continue;
		}
		expect(report.walls == 5 && report.openFaces == 1 && document.brushes.size() == 5, "the open face gets no wall", label);
		double highest = -1e9;
		for (const LevelMapBrush& wall : document.brushes) {
			highest = std::max(highest, wall.maxs.z);
		}
		expect(near(highest, 64), "the walls beside the opening stop flush with it", label + QLatin1Char(' ') + QString::number(highest));
		const WallFaces faces = wallFaces(document, QStringLiteral("common/caulk"), QStringLiteral("studio/wall"), QStringLiteral("studio/door"), 64);
		expect(faces.problems.isEmpty() && faces.other == 0, "faces are the room's, lining or caulk", label + QLatin1Char(' ') + faces.problems.join(QLatin1Char('|')));
		expect(faces.roomFacing == 5 && faces.wallsWithOneRoomFace == 5, "each wall keeps one room face", label);
		expect(faces.lining == 4, "the four side walls line the opening with the open face's material", label + QLatin1Char(' ') + QString::number(faces.lining));
		expect(faces.caulked == report.caulkedFaces && faces.caulked > 0, "the report counts the caulk faces", label);
		expect(undoLevelMapEdit(&document, &error) && bytesOf(document) == original, "undo restores the brush and its door face", label + error);
	}
	// Without caulk, the walls keep the brush's materials; the opening is the
	// same.
	LevelMapDocument plain = mapWith(QStringLiteral("quake3"), {{{-64, -64, -64, true}, {64, 64, 64, true}}}, QStringLiteral("studio/wall"));
	const int id = plain.brushes.first().id;
	const int bottom = faceAlong(plain.brushes.first(), 2, -1);
	setLevelMapSelection(&plain, {{LevelMapSelectionKind::QuakeBrush, id}});
	LevelMapHollowOptions open;
	open.openFaces = {{id, bottom}};
	LevelMapHollowReport report;
	expect(hollowLevelMapSelection(&plain, 8, LevelMapHollowMode::Room, open, &report, &error) && report.walls == 5 && report.caulkedFaces == 0,
		"open faces without caulk", error);
	bool kept = true;
	for (const LevelMapBrush& wall : plain.brushes) {
		for (const LevelMapBrushFace& face : wall.faces) {
			kept = kept && face.textureName == QLatin1String("studio/wall");
		}
	}
	expect(kept, "walls without caulk keep the brush's material");
}

void checkRefusals()
{
	QString error;
	LevelMapDocument document = mapWith(QStringLiteral("quake3"), {{{-64, -64, -64, true}, {64, 64, 64, true}}, {{256, 0, 0, true}, {320, 64, 64, true}}},
		QStringLiteral("studio/wall"));
	const int first = document.brushes.at(0).id;
	const int second = document.brushes.at(1).id;
	setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, first}});
	const QByteArray original = bytesOf(document);
	LevelMapHollowOptions options;
	options.caulkTexture = QStringLiteral("bad name");
	LevelMapDocument copy = document;
	expect(!hollowLevelMapSelection(&copy, 16, LevelMapHollowMode::Room, options, nullptr, &error) && !error.isEmpty() && bytesOf(copy) == original,
		"a caulk name the map cannot hold is refused", error);
	options.caulkTexture.clear();
	options.openFaces = {{first, 6}};
	copy = document;
	expect(!hollowLevelMapSelection(&copy, 16, LevelMapHollowMode::Room, options, nullptr, &error) && error.contains(QStringLiteral("face 7"))
			&& bytesOf(copy) == original,
		"an open face outside the brush is refused", error);
	options.openFaces.clear();
	for (int face = 0; face < 6; ++face) {
		options.openFaces.append({first, face});
	}
	copy = document;
	expect(!hollowLevelMapSelection(&copy, 16, LevelMapHollowMode::Room, options, nullptr, &error) && error.contains(QStringLiteral("every face open"))
			&& bytesOf(copy) == original,
		"a brush with every face open is left as it was", error);
	// Faces of brushes the edit does not reach are ignored.
	options.openFaces = {{second, 0}};
	LevelMapHollowReport report;
	copy = document;
	expect(hollowLevelMapSelection(&copy, 16, LevelMapHollowMode::Room, options, &report, &error) && report.walls == 6 && report.openFaces == 0,
		"open faces of other brushes are ignored", error);
	// A locked brush cannot be made into a room.
	LevelMapDocument locked = document;
	QString layer;
	if (expect(createLevelSceneNode(&locked, LevelSceneNodeKind::Layer, QStringLiteral("Protected"), {}, &layer, &error), "a layer to lock", error)
		&& expect(assignLevelSceneObjects(&locked, layer, {QStringLiteral("brush:%1").arg(first)}, &error), "the brush joins it", error)
		&& expect(setLevelSceneLocked(&locked, layer, true, &error), "lock the layer", error)) {
		const QByteArray before = bytesOf(locked);
		options.openFaces.clear();
		options.caulkTexture = QStringLiteral("common/caulk");
		expect(!hollowLevelMapSelection(&locked, 16, LevelMapHollowMode::Room, options, nullptr, &error) && bytesOf(locked) == before
				&& error.contains(QStringLiteral("Unlock")),
			"a locked brush is refused, changing nothing", error);
	}
}

// map hollow's --caulk, --caulk-texture and --open-face.
void checkCli(const QString& binary)
{
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "temporary folder")) {
		return;
	}
	const auto write = [&](const QString& name, const LevelMapDocument& document) {
		QFile file(temp.filePath(name));
		return file.open(QIODevice::WriteOnly) && file.write(bytesOf(document)) >= 0;
	};
	const auto run = [&](const QStringList& arguments, int expected) {
		QProcess process;
		process.setWorkingDirectory(temp.path());
		process.start(binary, QStringList {QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--settings-file"), temp.filePath(QStringLiteral("settings.ini"))}
				+ arguments);
		const bool finished = process.waitForStarted() && process.waitForFinished(60000);
		const QByteArray out = process.readAllStandardOutput();
		const QByteArray err = process.readAllStandardError();
		if (!finished) {
			process.kill();
			process.waitForFinished();
		}
		expect(finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected, "CLI exit code",
			arguments.join(QLatin1Char(' ')) + QStringLiteral(" -> ") + QString::number(process.exitCode()) + QLatin1Char(' ') + QString::fromUtf8(out + err));
		return QJsonDocument::fromJson(out).object();
	};
	LevelMapDocument quake3 = mapWith(QStringLiteral("quake3"), {{{-64, -64, -64, true}, {64, 64, 64, true}}}, QStringLiteral("studio/wall"));
	const int id = quake3.brushes.first().id;
	const int top = faceAlong(quake3.brushes.first(), 2, 1);
	const LevelMapDocument quake2 = mapWith(QStringLiteral("quake2"), {{{-64, -64, -64, true}, {64, 64, 64, true}}}, QStringLiteral("e1u1/wall"));
	if (!expect(write(QStringLiteral("q3.map"), quake3) && write(QStringLiteral("q2.map"), quake2), "write CLI fixtures")) {
		return;
	}
	const QString object = QStringLiteral("brush:%1").arg(id);
	const QString openFace = QStringLiteral("face:%1:%2").arg(id).arg(top + 1);
	QJsonObject result = run({QStringLiteral("map"), QStringLiteral("hollow"), QStringLiteral("q3.map"), QStringLiteral("--object"), object,
								 QStringLiteral("--mode"), QStringLiteral("room"), QStringLiteral("--thickness"), QStringLiteral("16"), QStringLiteral("--caulk"),
								 QStringLiteral("--open-face"), openFace, QStringLiteral("--output"), QStringLiteral("room.map")},
		0);
	const QJsonObject details = result.value(QStringLiteral("details")).toObject().isEmpty() ? result : result.value(QStringLiteral("details")).toObject();
	const auto field = [&](const char* key) { return details.contains(QLatin1String(key)) ? details.value(QLatin1String(key)) : result.value(QLatin1String(key)); };
	expect(field("caulkTexture").toString() == QLatin1String("common/caulk") && field("openFaceCount").toInt() == 1 && field("wallCount").toInt() == 5
			&& field("caulkedFaces").toInt() > 0 && field("openFaces").toArray() == QJsonArray {openFace},
		"map hollow reports the caulk and the open face", QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)));
	LevelMapDocument saved;
	QString error;
	if (expect(loadLevelMap({temp.filePath(QStringLiteral("room.map")), {}, QStringLiteral("idtech3")}, &saved, &error), "load the CLI's room", error)) {
		const WallFaces faces = wallFaces(saved, QStringLiteral("common/caulk"), QStringLiteral("studio/wall"), QStringLiteral("studio/wall"), 64);
		expect(saved.brushes.size() == 5 && faces.problems.isEmpty() && faces.roomFacing == 5 && faces.lining == 4,
			"the CLI's walls match the core's", faces.problems.join(QLatin1Char('|')));
	}
	run({QStringLiteral("map"), QStringLiteral("hollow"), QStringLiteral("q3.map"), QStringLiteral("--object"), object, QStringLiteral("--mode"),
			QStringLiteral("room"), QStringLiteral("--thickness"), QStringLiteral("16"), QStringLiteral("--caulk-texture"), QStringLiteral("studio/hidden"),
			QStringLiteral("--output"), QStringLiteral("custom.map")},
		0);
	QFile custom(temp.filePath(QStringLiteral("custom.map")));
	expect(custom.open(QIODevice::ReadOnly) && custom.readAll().count("studio/hidden") == 30, "--caulk-texture names the material");
	// Usage errors change nothing.
	const QStringList base {QStringLiteral("map"), QStringLiteral("hollow"), QStringLiteral("q3.map"), QStringLiteral("--object"), object,
		QStringLiteral("--mode"), QStringLiteral("room"), QStringLiteral("--thickness"), QStringLiteral("16"), QStringLiteral("--output"),
		QStringLiteral("bad.map")};
	run(base + QStringList {QStringLiteral("--open-face"), QStringLiteral("face:%1:9").arg(id)}, 2);
	run(base + QStringList {QStringLiteral("--open-face"), QStringLiteral("face:%1:1").arg(id + 40)}, 2);
	run(base + QStringList {QStringLiteral("--open-face"), QStringLiteral("brush:%1").arg(id)}, 2);
	run(base + QStringList {QStringLiteral("--caulk-texture"), QStringLiteral("bad\"name")}, 2);
	expect(!QFile::exists(temp.filePath(QStringLiteral("bad.map"))), "refused commands write nothing");
	run({QStringLiteral("map"), QStringLiteral("hollow"), QStringLiteral("q2.map"), QStringLiteral("--object"), QStringLiteral("brush:%1").arg(quake2.brushes.first().id),
			QStringLiteral("--mode"), QStringLiteral("room"), QStringLiteral("--thickness"), QStringLiteral("16"), QStringLiteral("--caulk"),
			QStringLiteral("--output"), QStringLiteral("q2-room.map")},
		2);
	expect(!QFile::exists(temp.filePath(QStringLiteral("q2-room.map"))), "Quake II needs a named caulk material");

	// editor make-room: the options Make Room Options saves.
	const auto values = [](const QJsonObject& output) { return output.value(QStringLiteral("values")).toObject(); };
	const QStringList editor {QStringLiteral("editor"), QStringLiteral("make-room")};
	QJsonObject shown = run(editor, 0);
	expect(values(shown) == levelRoomPreferencesJson({}) && !shown.value(QStringLiteral("applied")).toBool(), "editor make-room shows the defaults",
		QString::fromUtf8(QJsonDocument(shown).toJson(QJsonDocument::Compact)));
	const QStringList change {QStringLiteral("--set"), QStringLiteral("caulkTexture=studio/hidden"), QStringLiteral("--set"), QStringLiteral("openPickedFaces=off")};
	shown = run(editor + change + QStringList {QStringLiteral("--dry-run")}, 0);
	expect(values(shown).value(QStringLiteral("caulkTexture")).toString() == QLatin1String("studio/hidden") && shown.value(QStringLiteral("dryRun")).toBool()
			&& !shown.value(QStringLiteral("applied")).toBool() && values(run(editor, 0)) == levelRoomPreferencesJson({}),
		"--dry-run previews without saving");
	shown = run(editor + change, 0);
	const QJsonObject expected = levelRoomPreferencesJson({true, QStringLiteral("studio/hidden"), false});
	expect(shown.value(QStringLiteral("applied")).toBool() && values(shown) == expected && values(run(editor, 0)) == expected,
		"--set saves the options", QString::fromUtf8(QJsonDocument(shown).toJson(QJsonDocument::Compact)));
	{
		// The Levels editor reads what the command line wrote.
		const StudioSettings settings(temp.filePath(QStringLiteral("settings.ini")), StudioSettings::AccessMode::ReadOnly);
		const LevelRoomPreferences saved = settings.levelRoomPreferences();
		expect(saved.caulk && saved.caulkTexture == QLatin1String("studio/hidden") && !saved.openPickedFaces, "Make Room reads the saved options",
			saved.caulkTexture);
	}
	for (const QStringList& bad : {QStringList {QStringLiteral("--set"), QStringLiteral("caulkTexture=bad name")},
			 QStringList {QStringLiteral("--set"), QStringLiteral("caulk=maybe")}, QStringList {QStringLiteral("--set"), QStringLiteral("colour=on")},
			 QStringList {QStringLiteral("--set"), QStringLiteral("caulk=on"), QStringLiteral("--set"), QStringLiteral("caulk=off")},
			 QStringList {QStringLiteral("--reset"), QStringLiteral("--set"), QStringLiteral("caulk=off")}, QStringList {QStringLiteral("--dry-run")},
			 QStringList {QStringLiteral("--thickness"), QStringLiteral("8")}}) {
		run(editor + bad, 2);
	}
	expect(values(run(editor, 0)) == expected, "refused changes keep the saved options");
	shown = run(editor + QStringList {QStringLiteral("--set"), QStringLiteral("caulkTexture="), QStringLiteral("--set"), QStringLiteral("caulk=off")}, 0);
	expect(values(shown) == levelRoomPreferencesJson({false, {}, false}), "an empty caulkTexture goes back to the game's own caulk");
	shown = run(editor + QStringList {QStringLiteral("--reset")}, 0);
	expect(shown.value(QStringLiteral("applied")).toBool() && values(run(editor, 0)) == levelRoomPreferencesJson({}), "--reset restores the defaults");
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	checkGameCaulk();
	checkPreferences();
	checkRoomCaulk();
	checkOpenFaces();
	checkRefusals();
	if (argc > 1) {
		checkCli(QString::fromLocal8Bit(argv[1]));
	}
	if (failures > 0) {
		std::cerr << failures << " Make Room check(s) failed.\n";
		return 1;
	}
	std::cout << "Make Room options: game caulk, saved options, caulked walls, open faces in every mode, refusals, locks"
			  << (argc > 1 ? ", map hollow and editor make-room" : "") << " passed.\n";
	return 0;
}
