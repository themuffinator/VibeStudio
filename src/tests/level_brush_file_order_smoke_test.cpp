// Regression test: brushes rewritten by component, clip, extrude and CSG edits
// keep their place in the .map file, so file order and the brush numbers a
// reopen (or a compiler) assigns survive Save. Core and CLI share the writer.
#include "core/level_brush.h"
#include "core/level_document.h"
#include "core/level_scene.h"
#include "core/level_selection_tools.h"
#include "core/level_texture_mapping.h"
#include "core/map_geometry.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>

using namespace vibestudio;

namespace {

QString number(double value)
{
	return QString::number(value, 'g', 15);
}

// One box brush written in `dialect`, braces on lines of their own.
QStringList boxLines(const QString& dialect, const LevelMapVec3& mins, const LevelMapVec3& maxs)
{
	LevelMapDocument scratch;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	createLevelMap(create, &scratch);
	addLevelMapBoxBrush(&scratch, mins, maxs, QStringLiteral("studio/grid"));
	const auto point = [](const LevelMapVec3& p) { return QStringLiteral("( %1 %2 %3 )").arg(number(p.x), number(p.y), number(p.z)); };
	const bool matrix = dialect.startsWith(QStringLiteral("brushDef"));
	QStringList lines{QStringLiteral("{")};
	if (matrix) {
		lines << dialect << QStringLiteral("{");
	}
	for (const auto& face : scratch.brushes.first().faces) {
		const auto plane = planeFromPoints(face.p0, face.p1, face.p2);
		const QString geometry = dialect == QStringLiteral("brushDef3")
			? QStringLiteral("( %1 %2 %3 %4 )").arg(number(plane.normalX), number(plane.normalY), number(plane.normalZ), number(-plane.distance))
			: point(face.p0) + QLatin1Char(' ') + point(face.p1) + QLatin1Char(' ') + point(face.p2);
		if (matrix) {
			lines << geometry + QStringLiteral(" ( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) ) \"studio/grid\" 0 0 0");
		} else if (dialect == QStringLiteral("valve220")) {
			const auto projection = levelTextureProjection(face);
			lines << geometry + QStringLiteral(" studio/grid [ %1 %2 %3 0 ] [ %4 %5 %6 0 ] 0 1 1")
				.arg(number(projection.u.x), number(projection.u.y), number(projection.u.z), number(projection.v.x), number(projection.v.y), number(projection.v.z));
		} else {
			lines << geometry + QStringLiteral(" studio/grid 0 0 0 1 1 // kept face comment");
		}
	}
	if (matrix) {
		lines << QStringLiteral("}");
	}
	lines << QStringLiteral("}");
	return lines;
}

// Four world brushes and a two-brush func_wall, each headed by the
// TrenchBroom-style "// brush N" marker, then a point entity.
QByteArray fixture(const QString& dialect)
{
	QStringList lines{QStringLiteral("// brush order fixture"), QStringLiteral("// entity 0"), QStringLiteral("{"), QStringLiteral("\"classname\" \"worldspawn\"")};
	for (int i = 0; i < 4; ++i) {
		lines << QStringLiteral("// brush %1").arg(i);
		lines << boxLines(dialect, {i * 128.0, 0, 0, true}, {i * 128.0 + 64, 64, 64, true});
	}
	lines << QStringLiteral("}") << QStringLiteral("// entity 1") << QStringLiteral("{") << QStringLiteral("\"classname\" \"func_wall\"");
	for (int i = 0; i < 2; ++i) {
		lines << QStringLiteral("// brush %1").arg(i);
		lines << boxLines(dialect, {i * 128.0, 256, 0, true}, {i * 128.0 + 64, 320, 64, true});
	}
	lines << QStringLiteral("}") << QStringLiteral("{") << QStringLiteral("\"classname\" \"light\"") << QStringLiteral("\"origin\" \"0 0 128\"")
		  << QStringLiteral("}") << QString();
	const QString ending = dialect == QStringLiteral("valve220") ? QStringLiteral("\r\n") : QStringLiteral("\n");
	return lines.join(ending).toUtf8();
}

const LevelMapBrush* brushWithId(const LevelMapDocument& document, int id)
{
	for (const auto& brush : document.brushes) {
		if (brush.id == id) {
			return &brush;
		}
	}
	return nullptr;
}

QString signature(const LevelMapBrush* brush)
{
	if (!brush) {
		return QStringLiteral("missing");
	}
	const auto r = [](double value) {
		const double rounded = std::round(value * 1000.0) / 1000.0;
		return QString::number(rounded == 0 ? 0.0 : rounded, 'f', 3);
	};
	return QStringLiteral("%1 %2 %3 / %4 %5 %6 / %7 faces")
		.arg(r(brush->mins.x), r(brush->mins.y), r(brush->mins.z), r(brush->maxs.x), r(brush->maxs.y), r(brush->maxs.z))
		.arg(brush->faces.size());
}

int topFace(const LevelMapBrush* brush)
{
	LevelBrushTopology topology;
	if (!brush || !levelBrushTopology(*brush, &topology)) {
		return -1;
	}
	for (int face = 0; face < topology.geometry.faces.size(); ++face) {
		if (topology.geometry.faces[face].plane.normalZ > 0.99) {
			return face;
		}
	}
	return -1;
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid() || argc != 2) {
		std::cerr << "usage: level_brush_file_order_smoke_test <vibestudio>\n";
		return 1;
	}
	bool ok = true;
	QString error;
	const auto expect = [&](bool value, const char* label, const QString& detail = {}) {
		if (!value) {
			ok = false;
			std::cerr << label << ": " << detail.toStdString() << '\n';
		}
		return value;
	};
	const auto bytes = [](const LevelMapDocument& document) { return serializeLevelMap(document).bytes; };
	const auto load = [&](const QByteArray& data, LevelMapDocument* document) {
		return loadLevelMapBytes({temp.filePath(QStringLiteral("reload.map")), {}, {}}, data, document, &error);
	};
	const auto put = [](const QString& path, const QByteArray& data) {
		QFile file(path);
		return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
	};
	const auto read = [](const QString& path) {
		QFile file(path);
		return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
	};
	const auto run = [&](const QStringList& args) {
		QProcess process;
		process.setWorkingDirectory(temp.path());
		process.start(QString::fromLocal8Bit(argv[1]), QStringList{QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--settings-file"), temp.filePath(QStringLiteral("settings.ini"))} + args);
		const bool finished = process.waitForStarted() && process.waitForFinished(60000);
		const QByteArray output = process.readAllStandardOutput();
		expect(finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0, "CLI status", QString::fromUtf8(output + process.readAllStandardError()));
		if (!finished) {
			process.kill();
			process.waitForFinished();
		}
		return QJsonDocument::fromJson(output).object();
	};
	// Every brush is written at the entity and primitive numbers the document
	// reports for it, so the reopened map holds the same brush there.
	const auto numberedLikeReload = [&](const LevelMapDocument& document, const LevelMapDocument& reloaded, const QString& step) {
		if (!expect(document.brushes.size() == reloaded.brushes.size(), "reload keeps every brush", step)) {
			return;
		}
		for (const auto& brush : document.brushes) {
			int entity = -1;
			int primitive = -1;
			LevelMapSelectionRef found;
			const bool numbered = levelMapObjectNumber(document, {LevelMapSelectionKind::QuakeBrush, brush.id}, &entity, &primitive)
				&& levelMapObjectAtNumber(reloaded, entity, primitive, &found);
			expect(numbered && signature(&brush) == signature(brushWithId(reloaded, found.objectId)), "brush number predicts the reopened brush",
				QStringLiteral("%1: brush %2 at entity %3 brush %4").arg(step).arg(brush.id).arg(entity).arg(primitive));
		}
	};
	const auto sameIds = [&](const LevelMapDocument& document, const LevelMapDocument& reloaded, const QVector<int>& ids, const QString& step) {
		for (const int id : ids) {
			expect(signature(brushWithId(document, id)) == signature(brushWithId(reloaded, id)), "brush keeps its id through save and reopen",
				QStringLiteral("%1: brush %2 %3 vs %4").arg(step).arg(id).arg(signature(brushWithId(document, id)), signature(brushWithId(reloaded, id))));
		}
	};

	for (const QString& dialect : {QStringLiteral("classic"), QStringLiteral("valve220"), QStringLiteral("brushDef"), QStringLiteral("brushDef3")}) {
		const QByteArray original = fixture(dialect);
		LevelMapDocument document;
		if (!expect(load(original, &document) && document.brushes.size() == 6, "load fixture", dialect + error)) {
			continue;
		}
		expect(bytes(document) == original, "unedited fixture saves byte for byte", dialect);
		const int brush1Line = brushWithId(document, 1)->startLine;
		QString group;
		expect(createLevelSceneNode(&document, LevelSceneNodeKind::Group, QStringLiteral("Tracked"), {}, &group, &error)
			&& assignLevelSceneObjects(&document, group, {QStringLiteral("brush:1"), QStringLiteral("brush:5")}, &error), "track brushes in a group", dialect + error);
		const QByteArray grouped = bytes(document);
		const qsizetype groupedDepth = document.undoStack.size();

		// A component edit rewrites brush 1 where it stood, its marker intact.
		LevelMapBrush raised = *brushWithId(document, 1);
		LevelBrushEditReport change;
		expect(moveLevelBrushComponents(&raised, LevelBrushComponent::Face, {topFace(&raised)}, {0, 0, 16, true}, 0, false, &change, &error)
			&& replaceLevelMapBrushGeometry(&document, 1, raised, &error), "component edit", dialect + error);
		const QByteArray componentBytes = bytes(document);
		LevelMapDocument reloaded;
		expect(load(componentBytes, &reloaded), "reload component edit", dialect + error);
		sameIds(document, reloaded, {0, 1, 2, 3, 4, 5}, dialect + QStringLiteral(" component"));
		numberedLikeReload(document, reloaded, dialect + QStringLiteral(" component"));
		expect(brushWithId(document, 1)->startLine == 0 && levelMapBrushSaveLine(document, *brushWithId(document, 1)) == brush1Line,
			"rewritten brush saves at its source line", dialect);
		const QStringList savedLines = QString::fromUtf8(componentBytes).split(QLatin1Char('\n'));
		expect(savedLines.value(brush1Line - 2).trimmed() == QStringLiteral("// brush 1") && savedLines.value(brush1Line - 1).trimmed() == QStringLiteral("{"),
			"marker still heads the rewritten brush", dialect);
		if (dialect == QStringLiteral("classic")) {
			expect(componentBytes.count("// kept face comment") == original.count("// kept face comment"), "face comments kept once", dialect);
		}

		// Copies are new brushes: they join the end of the entity.
		LevelMapBrush copy = *brushWithId(document, 1);
		copy.id = 99;
		expect(levelMapBrushSaveLine(document, copy) == 0, "a copy that inherits the place does not take it", dialect);
		copy = *brushWithId(document, 1);
		copy.entityId = brushWithId(document, 4)->entityId;
		expect(levelMapBrushSaveLine(document, copy) == 0, "a brush moved to another entity leaves the old place", dialect);

		// Copied text follows file order: the rewritten brush comes before brush 3.
		expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 3}, {LevelMapSelectionKind::QuakeBrush, 1}}, &error), "select two brushes", error);
		LevelMapDocument copied;
		const QString copiedText = levelMapSelectionText(document, &error);
		// World brushes are copied bare, as TrenchBroom's clipboard holds them.
		const QString copiedMap = copiedText.contains(QStringLiteral("\"classname\""))
			? copiedText : QStringLiteral("{\n\"classname\" \"worldspawn\"\n%1}\n").arg(copiedText);
		const bool copiedLoaded = !copiedText.isEmpty() && load(copiedMap.toUtf8(), &copied) && copied.brushes.size() == 2;
		expect(copiedLoaded && signature(&copied.brushes[0]) == signature(brushWithId(document, 1)) && signature(&copied.brushes[1]) == signature(brushWithId(document, 3)),
			"copied brushes follow file order", dialect + error + QLatin1Char('\n') + copiedText);

		// A second edit of the rewritten brush (now startLine 0) keeps the place.
		expect(moveLevelMapComponents(&document, {{{LevelMapSelectionKind::QuakeBrush, 1}, LevelBrushComponent::Face, {topFace(brushWithId(document, 1))}}},
			{0, 0, 8, true}, false, nullptr, &error), "second component edit", dialect + error);
		expect(load(bytes(document), &reloaded), "reload second edit", dialect + error);
		sameIds(document, reloaded, {0, 1, 2, 3, 4, 5}, dialect + QStringLiteral(" second edit"));
		expect(std::abs(brushWithId(reloaded, 1)->maxs.z - 88) < 0.01, "second edit saved", dialect);

		// Clip brush 2 in two: the first piece takes its place, the second joins
		// the end of worldspawn, and every other brush keeps its id.
		expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 2}}, &error)
			&& clipLevelMapSelection(&document, {288, 0, 0, true}, {288, 64, 0, true}, {288, 0, 64, true}, LevelMapClipKeep::Both, nullptr, &error),
			"clip brush 2", dialect + error);
		expect(document.brushes.size() == 7 && brushWithId(document, 6) && brushWithId(document, 7), "clip makes pieces 6 and 7", dialect);
		expect(load(bytes(document), &reloaded), "reload clip", dialect + error);
		numberedLikeReload(document, reloaded, dialect + QStringLiteral(" clip"));
		sameIds(document, reloaded, {0, 1, 3}, dialect + QStringLiteral(" clip"));
		expect(signature(brushWithId(reloaded, 2)) == signature(brushWithId(document, 6)) && signature(brushWithId(reloaded, 4)) == signature(brushWithId(document, 7)),
			"first clip piece in place, second at the entity end", dialect);

		// Split extrusion keeps brush 3's identity, and so its place.
		expect(extrudeLevelMapFaces(&document, {{{LevelMapSelectionKind::QuakeBrush, 3}, LevelBrushComponent::Face, {topFace(brushWithId(document, 3))}}},
			16, true, true, nullptr, &error), "split extrusion", dialect + error);
		expect(load(bytes(document), &reloaded), "reload extrusion", dialect + error);
		numberedLikeReload(document, reloaded, dialect + QStringLiteral(" extrusion"));
		sameIds(document, reloaded, {0, 1, 3}, dialect + QStringLiteral(" extrusion"));

		// CSG: a world carver cuts a corner from the func_wall's first brush.
		int carver = -1;
		expect(addLevelMapBoxBrush(&document, {32, 288, 32, true}, {96, 352, 96, true}, QStringLiteral("studio/carve"), &carver, &error), "add carver", error);
		int carved = 0;
		expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, carver}}, &error) && carveLevelMapSelection(&document, &carved, &error)
			&& carved == 1 && !brushWithId(document, 4), "carve brush 4", dialect + error);
		const QByteArray carvedBytes = bytes(document);
		expect(load(carvedBytes, &reloaded), "reload carve", dialect + error);
		numberedLikeReload(document, reloaded, dialect + QStringLiteral(" carve"));
		LevelMapSelectionRef wallFirst;
		LevelMapSelectionRef wallSecond;
		expect(levelMapObjectAtNumber(reloaded, 1, 0, &wallFirst) && levelMapObjectAtNumber(reloaded, 1, 1, &wallSecond)
				&& signature(brushWithId(reloaded, wallSecond.objectId)) == signature(brushWithId(document, 5)),
			"untouched func_wall brush stays second", dialect);
		const QVector<LevelMapSelectionRef> tracked = levelSceneSelection(reloaded, group);
		LevelMapSelectionRef wall5;
		int entity5 = -1;
		int primitive5 = -1;
		expect(levelMapObjectNumber(document, {LevelMapSelectionKind::QuakeBrush, 5}, &entity5, &primitive5)
				&& levelMapObjectAtNumber(reloaded, entity5, primitive5, &wall5)
				&& tracked.contains(LevelMapSelectionRef{LevelMapSelectionKind::QuakeBrush, 1}) && tracked.contains(wall5) && tracked.size() == 2,
			"group membership follows the brushes through save and reopen", dialect);

		// A duplicate of the rewritten brush is appended, not spliced.
		expect(duplicateLevelMapObjects(&document, {{LevelMapSelectionKind::QuakeBrush, 1}}, 0, 128, 0, &error), "duplicate rewritten brush", dialect + error);
		expect(load(bytes(document), &reloaded), "reload duplicate", dialect + error);
		numberedLikeReload(document, reloaded, dialect + QStringLiteral(" duplicate"));
		sameIds(document, reloaded, {0, 1, 3}, dialect + QStringLiteral(" duplicate"));
		const QByteArray finalBytes = bytes(document);

		// Exact undo back to the source, exact redo forward again.
		while (!document.undoStack.isEmpty()) {
			if (!expect(undoLevelMapEdit(&document, &error), "undo", dialect + error)) {
				break;
			}
			if (document.undoStack.size() == groupedDepth) {
				expect(bytes(document) == grouped, "undo restores the grouped source", dialect);
			}
		}
		expect(bytes(document) == original, "undo restores the source byte for byte", dialect);
		while (!document.redoStack.isEmpty()) {
			if (!expect(redoLevelMapEdit(&document, &error), "redo", dialect + error)) {
				break;
			}
		}
		expect(bytes(document) == finalBytes, "redo restores every edit byte for byte", dialect);

		// The CLI uses the same writer: identical bytes and the same order.
		const QString input = temp.filePath(dialect + QStringLiteral(".map"));
		expect(put(input, original), "write CLI source", dialect);
		LevelMapDocument fresh;
		expect(load(original, &fresh), "reload source", error);
		LevelMapBrush cliRaised = *brushWithId(fresh, 1);
		const int face = topFace(&cliRaised);
		expect(moveLevelBrushComponents(&cliRaised, LevelBrushComponent::Face, {face}, {0, 0, 16, true}, 0, false, &change, &error)
			&& replaceLevelMapBrushGeometry(&fresh, 1, cliRaised, &error), "core component edit", dialect + error);
		const QString moved = temp.filePath(dialect + QStringLiteral("-moved.map"));
		run({QStringLiteral("map"), QStringLiteral("move-components"), input, QStringLiteral("--brush"), QStringLiteral("1"), QStringLiteral("--kind"), QStringLiteral("face"),
			QStringLiteral("--component"), QString::number(face), QStringLiteral("--delta"), QStringLiteral("0,0,16"), QStringLiteral("--output"), moved});
		expect(read(moved) == bytes(fresh), "CLI component edit matches core and keeps file order", dialect);

		expect(load(original, &fresh) && setLevelMapSelection(&fresh, {{LevelMapSelectionKind::QuakeBrush, 2}}, &error)
			&& clipLevelMapPoints(&fresh, {{288, 0, 0, true}, {288, 64, 0, true}, {288, 0, 64, true}}, 2, 1, LevelMapClipKeep::Both, {}, nullptr, &error),
			"core clip", dialect + error);
		const QString clipped = temp.filePath(dialect + QStringLiteral("-clipped.map"));
		run({QStringLiteral("map"), QStringLiteral("clip"), input, QStringLiteral("--object"), QStringLiteral("brush:2"), QStringLiteral("--points"),
			QStringLiteral("288,0,0 288,64,0 288,0,64"), QStringLiteral("--keep"), QStringLiteral("both"), QStringLiteral("--output"), clipped});
		expect(read(clipped) == bytes(fresh), "CLI clip matches core", dialect);
		expect(load(read(clipped), &reloaded), "reload CLI clip", dialect + error);
		numberedLikeReload(fresh, reloaded, dialect + QStringLiteral(" CLI clip"));

		// Carve from a saved map that already holds the carver.
		expect(load(original, &fresh) && addLevelMapBoxBrush(&fresh, {32, 288, 32, true}, {96, 352, 96, true}, QStringLiteral("studio/carve"), nullptr, &error),
			"core carver", dialect + error);
		const QByteArray withCarver = bytes(fresh);
		const QString carveInput = temp.filePath(dialect + QStringLiteral("-carver.map"));
		expect(put(carveInput, withCarver) && load(withCarver, &fresh), "write carver source", dialect + error);
		LevelMapSelectionRef carverRef;
		expect(levelMapObjectAtNumber(fresh, 0, 4, &carverRef), "find carver by number", error);
		expect(setLevelMapSelection(&fresh, {carverRef}, &error) && carveLevelMapSelection(&fresh, &carved, &error), "core carve", dialect + error);
		const QString carvedPath = temp.filePath(dialect + QStringLiteral("-carved.map"));
		run({QStringLiteral("map"), QStringLiteral("carve"), carveInput, QStringLiteral("--object"), levelMapSelectionRefId(carverRef), QStringLiteral("--output"), carvedPath});
		expect(read(carvedPath) == bytes(fresh), "CLI carve matches core", dialect);
		expect(load(read(carvedPath), &reloaded), "reload CLI carve", dialect + error);
		numberedLikeReload(fresh, reloaded, dialect + QStringLiteral(" CLI carve"));
	}
	if (ok) {
		std::cout << "level brush file order smoke test passed\n";
	}
	return ok ? 0 : 1;
}
