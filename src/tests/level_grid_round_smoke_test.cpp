// Round Selection to Grid (Radiant's Snap To Grid) and the fitted component UV
// lock: core service checks, then the CLI routes `map round-to-grid` and
// `map transform-components --uv-lock fit` through the same services.
#include "core/level_brush.h"
#include "core/level_document.h"
#include "core/level_grid_round.h"
#include "core/level_patch.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_visibility.h"
#include "tests/level_geometry_test_helpers.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <numeric>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << label << ": " << detail.toStdString() << '\n';
	}
	return value;
}
bool onGrid(double value, double grid)
{
	return std::abs(value / grid - std::round(value / grid)) < 1e-9;
}
bool brushOnGrid(const LevelMapBrush& brush, double grid)
{
	LevelBrushTopology topology;
	if (!levelBrushTopology(brush, &topology)) {
		return false;
	}
	return std::all_of(topology.vertices.cbegin(), topology.vertices.cend(),
		[grid](const LevelMapVec3& v) { return onGrid(v.x, grid) && onGrid(v.y, grid) && onGrid(v.z, grid); });
}
const LevelMapBrush* brushById(const LevelMapDocument& document, int id)
{
	const auto found = std::find_if(document.brushes.cbegin(), document.brushes.cend(), [id](const auto& b) { return b.id == id; });
	return found == document.brushes.cend() ? nullptr : &*found;
}
const LevelMapEntity* entityById(const LevelMapDocument& document, int id)
{
	const auto found = std::find_if(document.entities.cbegin(), document.entities.cend(), [id](const auto& e) { return e.id == id; });
	return found == document.entities.cend() ? nullptr : &*found;
}
QString originKey(const LevelMapEntity& entity)
{
	for (const auto& property : entity.properties) {
		if (property.key == QLatin1String("origin")) {
			return property.value;
		}
	}
	return {};
}
QVector<int> allVertices(const LevelMapBrush& brush)
{
	LevelBrushTopology topology;
	levelBrushTopology(brush, &topology);
	QVector<int> ids(topology.vertices.size());
	std::iota(ids.begin(), ids.end(), 0);
	return ids;
}

// One cube; its top face scaled sideways turns the ±Y sides into trapezoids,
// which no single planar projection can keep exact.
void checkFittedUvLock()
{
	for (const bool valve : {true, false}) {
		LevelMapDocument cube;
		QString error;
		tests::createGeometryFixture(1, &cube, &error);
		const auto before = cube.brushes.first();
		const auto bytes = serializeLevelMap(cube).bytes;
		LevelBrushTopology topology;
		levelBrushTopology(before, &topology);
		int upper = -1;
		for (int i = 0; i < topology.faces.size(); ++i) {
			if (topology.geometry.faces[i].plane.normalZ > .99) {
				upper = i;
			}
		}
		const QVector<LevelMapComponentSet> top{{{LevelMapSelectionKind::QuakeBrush, 0}, LevelBrushComponent::Face, {upper}}};
		LevelComponentTransformRequest taper;
		taper.operation = LevelComponentTransformKind::Scale;
		taper.scale = {1.25, 1, 1, true};
		taper.textureLock = true;
		taper.allowValve220 = valve;
		expect(!transformLevelMapComponents(&cube, top, taper, nullptr, &error) && error.contains(QStringLiteral("planar projection"))
				&& serializeLevelMap(cube).bytes == bytes,
			"exact lock still refuses a trapezoid", error);
		taper.fitUvLock = true;
		LevelMapComponentReport report;
		if (!expect(transformLevelMapComponents(&cube, top, taper, &report, &error) && report.brushes == 1, "fitted lock applies the taper", error)) {
			continue;
		}
		expect(report.fittedFaces == 2, "the two trapezoid sides are fitted", QString::number(report.fittedFaces));
		expect(cube.undoStack.size() == 1, "fitted taper is one undo step");
		const auto& after = cube.brushes.first();
		bool explicitAxes = false;
		for (const auto& face : after.faces) {
			explicitAxes = explicitAxes || face.explicitTextureAxes;
		}
		expect(explicitAxes == valve, valve ? "Valve 220 allowed: fitted sides take explicit axes" : "classic faces stay classic with the nearest mapping");
		if (valve) {
			// Each trapezoid keeps the UVs of its two unmoved bottom corners
			// exactly; at least one top corner drifts.
			for (int i = 0; i < before.faces.size(); ++i) {
				const auto plane = topology.geometry.faces[i].plane;
				if (std::abs(plane.normalY) < .99) {
					continue;
				}
				const auto oldProjection = levelTextureProjection(before.faces[i]);
				const auto match = std::find_if(after.faces.cbegin(), after.faces.cend(), [&](const auto& face) {
					return face.line == before.faces[i].line;
				});
				if (!expect(match != after.faces.cend(), "trapezoid face keeps its source line")) {
					continue;
				}
				const auto newProjection = levelTextureProjection(*match);
				int exact = 0, drifted = 0;
				for (int vertex : topology.faces[i]) {
					const auto p = topology.vertices[vertex];
					const auto moved = p.z > 0 ? LevelMapVec3{p.x * 1.25, p.y, p.z, true} : p;
					const auto want = oldProjection.at(p), got = newProjection.at(moved);
					const bool same = std::abs(want.x() - got.x()) < 1e-4 && std::abs(want.y() - got.y()) < 1e-4;
					exact += same ? 1 : 0;
					drifted += !same ? 1 : 0;
					if (p.z < 0) {
						expect(same, "an unmoved bottom corner keeps its UVs", QStringLiteral("%1 %2").arg(want.x()).arg(got.x()));
					}
				}
				expect(exact == 3 && drifted == 1, "three pinned corners and one drifting", QStringLiteral("%1/%2").arg(exact).arg(drifted));
			}
		}
		const auto changed = serializeLevelMap(cube).bytes;
		expect(undoLevelMapEdit(&cube, &error) && serializeLevelMap(cube).bytes == bytes && redoLevelMapEdit(&cube, &error)
				&& serializeLevelMap(cube).bytes == changed,
			"fitted lock exact undo and redo", error);
	}
	// An edit an exact lock can hold reports no fitted faces.
	LevelMapDocument cube;
	QString error;
	tests::createGeometryFixture(1, &cube, &error);
	LevelComponentTransformRequest move;
	move.delta = {8, 0, 0, true};
	move.textureLock = move.fitUvLock = true;
	LevelMapComponentReport report;
	expect(transformLevelMapComponents(&cube, {{{LevelMapSelectionKind::QuakeBrush, 0}, LevelBrushComponent::Vertex, allVertices(cube.brushes.first())}}, move,
			   &report, &error)
			&& report.fittedFaces == 0,
		"an exact lock is not counted as fitted", error);
}

struct Fixture {
	LevelMapDocument document;
	int offBrush = -1, tinyBrush = -1, entityBrush = -1, pointEntity = -1, brushEntity = -1, patch = -1;
};

bool makeFixture(Fixture* fixture)
{
	QString error;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	auto& map = fixture->document;
	if (!expect(createLevelMap(create, &map, &error), "fixture map", error)
		|| !expect(addLevelMapBoxBrush(&map, {0.3, -0.2, 0.4, true}, {63.6, 64.3, 31.7, true}, QStringLiteral("studio/round"), &fixture->offBrush, &error), "off-grid brush", error)
		|| !expect(addLevelMapBoxBrush(&map, {200.2, 0.2, 0.2, true}, {201.4, 1.4, 1.4, true}, QStringLiteral("studio/tiny"), &fixture->tinyBrush, &error), "tiny brush", error)
		|| !expect(addLevelMapBoxBrush(&map, {300.4, 0.1, 0.3, true}, {363.8, 32.2, 64.1, true}, QStringLiteral("studio/door"), &fixture->entityBrush, &error), "door brush", error)
		|| !expect(setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, fixture->entityBrush}}, &error)
				&& tieLevelMapSelectionToEntity(&map, QStringLiteral("func_door"), {{QStringLiteral("origin"), QStringLiteral("3.5 4.5 5.5"), 0}}, &fixture->brushEntity, &error),
			"brush entity", error)
		|| !expect(addLevelMapEntity(&map, QStringLiteral("info_player_start"), {10.4, 20.6, 30.2, true}, {}, &fixture->pointEntity, &error), "point entity", error)) {
		return false;
	}
	LevelPatchCreateRequest request;
	request.center = {100.3, 0.4, 40.2, true};
	request.size = {64.5, 63.3, 8, true};
	LevelMapPatch patch;
	if (!expect(createLevelPatch(request, &patch, &error) && addLevelMapPatch(&map, patch, &fixture->patch, &error), "off-grid patch", error)) {
		return false;
	}
	// Tying a brush to an entity may renumber brushes; find them by material.
	for (const auto& brush : std::as_const(map.brushes)) {
		if (brush.textureNames.contains(QStringLiteral("studio/round"))) {
			fixture->offBrush = brush.id;
		} else if (brush.textureNames.contains(QStringLiteral("studio/tiny"))) {
			fixture->tinyBrush = brush.id;
		} else if (brush.textureNames.contains(QStringLiteral("studio/door"))) {
			fixture->entityBrush = brush.id;
		}
	}
	setLevelMapSelection(&map, {}, &error);
	map.undoStack.clear();
	map.redoStack.clear();
	return expect(brushById(map, fixture->entityBrush) && brushById(map, fixture->entityBrush)->entityId == fixture->brushEntity, "door brush belongs to its entity");
}

void checkRounding()
{
	Fixture fixture;
	if (!makeFixture(&fixture)) {
		return;
	}
	QString error;
	auto map = fixture.document;
	const auto patchBefore = map.patches.first();
	const QVector<LevelMapSelectionRef> selection{{LevelMapSelectionKind::QuakeBrush, fixture.offBrush}, {LevelMapSelectionKind::QuakePatch, fixture.patch},
		{LevelMapSelectionKind::Entity, fixture.pointEntity}, {LevelMapSelectionKind::Entity, fixture.brushEntity}};
	expect(setLevelMapSelection(&map, selection, &error), "select fixture objects", error);
	const auto bytes = serializeLevelMap(map).bytes;
	const auto history = map.undoStack.size();
	LevelMapGridRoundRequest request;
	request.grid = 8;
	LevelMapGridRoundReport report;
	if (!expect(roundLevelMapSelectionToGrid(&map, request, &report, &error), "round selection", error)) {
		return;
	}
	expect(report.brushes == 2 && report.patches == 1 && report.entities == 1 && report.skipped.isEmpty(), "brush, entity brush, patch and point origin round",
		QStringLiteral("%1 %2 %3 %4").arg(report.brushes).arg(report.patches).arg(report.entities).arg(report.skipped.join(';')));
	expect(map.undoStack.size() == history + 1, "one undo step for every object");
	expect(brushOnGrid(*brushById(map, fixture.offBrush), 8) && brushOnGrid(*brushById(map, fixture.entityBrush), 8), "brush vertices lie on the grid");
	const auto& patch = map.patches.first();
	expect(std::all_of(patch.controlPoints.cbegin(), patch.controlPoints.cend(),
			   [](const auto& p) { return onGrid(p.x, 8) && onGrid(p.y, 8) && onGrid(p.z, 8); })
			&& patch.controlU == patchBefore.controlU,
		"patch control points round and keep their UVs");
	const auto* point = entityById(map, fixture.pointEntity);
	expect(point && originKey(*point) == QLatin1String("8 24 32") && point->origin.x == 8 && point->origin.y == 24 && point->origin.z == 32,
		"point entity origin rounds", point ? originKey(*point) : QString());
	const auto* door = entityById(map, fixture.brushEntity);
	expect(door && originKey(*door) == QLatin1String("3.5 4.5 5.5"), "brush entity keeps its origin key", door ? originKey(*door) : QString());
	const auto* tiny = brushById(map, fixture.tinyBrush);
	expect(tiny && serializeLevelMap(map).bytes.contains("200.2"), "unselected brushes are untouched");
	const auto changed = serializeLevelMap(map).bytes;
	expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == bytes && redoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == changed,
		"round exact undo and redo", error);
	expect(roundLevelMapSelectionToGrid(&map, request, &report, &error) && !report.changed() && map.undoStack.size() == history + 1,
		"an on-grid selection adds no history", error);

	// A brush the grid would collapse is left as it was, with its reason.
	map = fixture.document;
	setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, fixture.tinyBrush}, {LevelMapSelectionKind::QuakeBrush, fixture.offBrush}}, &error);
	expect(roundLevelMapSelectionToGrid(&map, request, &report, &error) && report.brushes == 1 && report.skipped.size() == 1
			&& report.skipped.first().contains(QString::number(fixture.tinyBrush)),
		"collapsing brush skipped with its reason", report.skipped.join(';') + error);
	map = fixture.document;
	setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, fixture.tinyBrush}}, &error);
	const auto tinyBytes = serializeLevelMap(map).bytes;
	expect(!roundLevelMapSelectionToGrid(&map, request, &report, &error) && !error.isEmpty() && serializeLevelMap(map).bytes == tinyBytes
			&& map.undoStack.isEmpty(),
		"every object refused fails without change", error);

	// Picked components only.
	map = fixture.document;
	const auto offBefore = *brushById(map, fixture.offBrush);
	LevelBrushTopology topology;
	levelBrushTopology(offBefore, &topology);
	request.components = {{{LevelMapSelectionKind::QuakeBrush, fixture.offBrush}, LevelBrushComponent::Vertex, {0}}};
	if (expect(roundLevelMapSelectionToGrid(&map, request, &report, &error) && report.brushes == 1 && map.undoStack.size() == 1, "round one picked vertex", error)) {
		LevelBrushTopology after;
		levelBrushTopology(*brushById(map, fixture.offBrush), &after);
		int moved = 0, kept = 0;
		for (const auto& v : topology.vertices) {
			const bool same = std::any_of(after.vertices.cbegin(), after.vertices.cend(), [&](const auto& q) { return std::abs(q.x - v.x) < 1e-3 && std::abs(q.y - v.y) < 1e-3 && std::abs(q.z - v.z) < 1e-3; });
			kept += same ? 1 : 0;
			moved += same ? 0 : 1;
		}
		const auto rounded = snapLevelMapPosition(topology.vertices[0], 8);
		expect(moved == 1 && kept == 7
				&& std::any_of(after.vertices.cbegin(), after.vertices.cend(), [&](const auto& q) { return std::abs(q.x - rounded.x) < 1e-3 && std::abs(q.y - rounded.y) < 1e-3 && std::abs(q.z - rounded.z) < 1e-3; }),
			"only the picked vertex rounds", QStringLiteral("%1/%2").arg(moved).arg(kept));
	}
	request.components.clear();

	// Scene locks refuse the whole edit; hidden objects are skipped.
	map = fixture.document;
	QString group;
	expect(createLevelSceneNode(&map, LevelSceneNodeKind::Group, QStringLiteral("Locked"), {}, &group, &error)
			&& assignLevelSceneObjects(&map, group, {levelMapSelectionRefId({LevelMapSelectionKind::QuakeBrush, fixture.offBrush})}, &error)
			&& setLevelSceneLocked(&map, group, true, &error),
		"lock fixture", error);
	setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, fixture.offBrush}}, &error);
	const auto lockedBytes = serializeLevelMap(map).bytes;
	const auto lockedRevision = map.revision;
	expect(!roundLevelMapSelectionToGrid(&map, request, &report, &error) && serializeLevelMap(map).bytes == lockedBytes && map.revision == lockedRevision,
		"locked object refuses atomically", error);
	map = fixture.document;
	setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, fixture.offBrush}}, &error);
	LevelVisibilityRequest hide;
	expect(changeLevelMapVisibility(&map, hide, nullptr, &error), "hide fixture", error);
	map.selection = {{LevelMapSelectionKind::QuakeBrush, fixture.offBrush}, {LevelMapSelectionKind::Entity, fixture.pointEntity}};
	if (expect(levelMapHiddenObjects(map).contains(levelMapSelectionRefId({LevelMapSelectionKind::QuakeBrush, fixture.offBrush})), "brush hidden")) {
		expect(roundLevelMapSelectionToGrid(&map, request, &report, &error) && report.entities == 1 && report.brushes == 0 && report.skipped.size() == 1
				&& report.skipped.first().contains(QStringLiteral("hidden")),
			"hidden brush skipped with its reason", report.skipped.join(';') + error);
	}

	// Doom-format maps and bad grids are refused.
	map = fixture.document;
	setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, fixture.offBrush}}, &error);
	for (const double grid : {0.0, -8.0, 8192.0, std::nan("")}) {
		request.grid = grid;
		expect(!roundLevelMapSelectionToGrid(&map, request, &report, &error) && map.undoStack.isEmpty(), "invalid grid refused", QString::number(grid));
	}
	request.grid = 8;
	LevelMapDocument doom;
	LevelMapCreateRequest doomRequest;
	doomRequest.game = QStringLiteral("doom");
	if (createLevelMap(doomRequest, &doom, &error)) {
		expect(!roundLevelMapSelectionToGrid(&doom, request, &report, &error) && error.contains(QStringLiteral("Quake")), "Doom map refused", error);
	}
}

int runCli(const QString& binary, const QTemporaryDir& temp, const QStringList& arguments, QJsonObject* json)
{
	QProcess process;
	process.setWorkingDirectory(temp.path());
	process.start(binary, QStringList{"--cli", "--json", "--settings-file", temp.filePath("settings.ini"), "map"} + arguments);
	if (!process.waitForStarted() || !process.waitForFinished(60000)) {
		process.kill();
		process.waitForFinished();
		return -1;
	}
	const auto output = process.readAllStandardOutput();
	if (json) {
		*json = QJsonDocument::fromJson(output).object();
	}
	if (process.exitCode() != 0) {
		std::cerr << "(cli) " << QString::fromUtf8(output + process.readAllStandardError()).left(400).toStdString() << '\n';
	}
	return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
}

void checkCli(const QString& binary)
{
	QTemporaryDir temp;
	Fixture fixture;
	if (!expect(temp.isValid(), "temporary folder") || !makeFixture(&fixture)) {
		return;
	}
	const auto read = [](const QString& path) {
		QFile file(path);
		return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
	};
	const auto input = temp.filePath("input.map"), output = temp.filePath("rounded.map");
	expect(saveLevelMapAs(fixture.document, input).succeeded(), "save CLI fixture");
	const auto original = read(input);
	const QString brush = QStringLiteral("brush:%1").arg(fixture.offBrush), entity = QStringLiteral("entity:%1").arg(fixture.pointEntity);
	QJsonObject json;
	expect(runCli(binary, temp, {"round-to-grid", input, "--object", brush, "--object", entity, "--grid", "8", "--output", output, "--dry-run"}, &json) == 0
			&& !QFile::exists(output) && json.value("brushes").toInt() == 1 && json.value("entities").toInt() == 1,
		"dry run reports without writing");
	expect(runCli(binary, temp, {"round-to-grid", input, "--object", brush + "," + entity, "--grid", "8", "--output", output}, &json) == 0
			&& json.value("mode").toString() == QLatin1String("objects") && json.value("written").toBool(),
		"object rounding writes the output");
	LevelMapDocument loaded;
	QString error;
	expect(loadLevelMap({output, {}, "idtech3"}, &loaded, &error) && brushById(loaded, fixture.offBrush) && brushOnGrid(*brushById(loaded, fixture.offBrush), 8)
			&& entityById(loaded, fixture.pointEntity) && originKey(*entityById(loaded, fixture.pointEntity)) == QLatin1String("8 24 32"),
		"rounded output reloads on the grid", error);
	const auto written = read(output);
	expect(runCli(binary, temp, {"round-to-grid", input, "--set", brush + "/vertex/0", "--grid", "16", "--output", output, "--overwrite"}, &json) == 0
			&& json.value("mode").toString() == QLatin1String("components") && json.value("brushes").toInt() == 1,
		"component rounding through --set");
	for (const auto& invalid : QVector<QStringList>{{"--grid", "0"}, {"--grid", "nan"}, {"--grid", "8192"}, {}, {"--grid", "8", "--uv-lock", "maybe"},
			 {"--grid", "8", "--allow-valve220"}, {"--grid", "8", "--allow-collapse"}, {"--grid", "8", "--set", brush + "/vertex/0"}, {"--grid", "8", "--typo"},
			 {"--grid", "8", "--object", "nothing:1"}, {"--grid", "8", "--grid", "16"}}) {
		const auto before = read(output);
		expect(runCli(binary, temp, QStringList{"round-to-grid", input, "--object", brush, "--output", output, "--overwrite"} + invalid, nullptr) == 2
				&& read(output) == before && read(input) == original,
			"invalid arguments are usage errors that leave files intact", invalid.join(' '));
	}
	expect(runCli(binary, temp, {"round-to-grid", input, "--object", brush, "--grid", "8"}, nullptr) == 2, "--output is required");
	Q_UNUSED(written);

	// transform-components --uv-lock fit takes the trapezoid the exact lock refuses.
	LevelMapDocument cube;
	tests::createGeometryFixture(1, &cube, &error);
	LevelBrushTopology topology;
	levelBrushTopology(cube.brushes.first(), &topology);
	int upper = -1;
	for (int i = 0; i < topology.faces.size(); ++i) {
		if (topology.geometry.faces[i].plane.normalZ > .99) {
			upper = i;
		}
	}
	const auto cubePath = temp.filePath("cube.map"), tapered = temp.filePath("tapered.map");
	expect(saveLevelMapAs(cube, cubePath).succeeded(), "save cube");
	const QStringList taper{"transform-components", cubePath, "--set", QStringLiteral("brush:0/face/%1").arg(upper), "--operation", "scale", "--factors", "1.25,1,1",
		"--output", tapered, "--overwrite"};
	expect(runCli(binary, temp, taper + QStringList{"--uv-lock", "on"}, nullptr) == 1 && !QFile::exists(tapered), "exact CLI lock refuses the trapezoid");
	expect(runCli(binary, temp, taper + QStringList{"--uv-lock", "fit"}, &json) == 0 && json.value("fittedFaces").toInt() == 2
			&& json.value("uvLockMode").toString() == QLatin1String("fit") && QFile::exists(tapered),
		"fitted CLI lock applies the trapezoid and reports it");
	expect(runCli(binary, temp, taper + QStringList{"--uv-lock", "sometimes"}, nullptr) == 2, "unknown UV lock mode is a usage error");
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	checkFittedUvLock();
	checkRounding();
	if (argc > 1) {
		checkCli(QString::fromLocal8Bit(argv[1]));
	}
	return ok ? 0 : 1;
}
