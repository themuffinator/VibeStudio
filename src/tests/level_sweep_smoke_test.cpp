#include "core/level_linked_groups.h"
#include "core/level_placement.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_sweep.h"
#include "tests/level_geometry_test_helpers.h"
#include "tests/level_surface_test_helpers.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace
{
bool ok = true;
bool expect(bool value, const char* label, const QString& error = {})
{
	if (!value) {
		ok = false;
		std::cerr << label << ": " << error.toStdString() << '\n';
	}
	return value;
}
QByteArray bytes(const LevelMapDocument& map)
{
	return serializeLevelMap(map).bytes;
}
bool close(LevelMapVec3 a, LevelMapVec3 b, double tolerance = 1e-6)
{
	return std::max({std::abs(a.x - b.x), std::abs(a.y - b.y), std::abs(a.z - b.z)}) < tolerance;
}
bool contains(const QVector<LevelMapVec3>& points, LevelMapVec3 target, double tolerance = .02)
{
	return std::any_of(points.cbegin(), points.cend(), [&](auto p) { return close(p, target, tolerance); });
}
LevelMapComponentSet face(const LevelMapDocument& map, int brushId, int axis = 2, int direction = 1)
{
	for (const auto& brush : map.brushes) {
		if (brush.id == brushId) {
			LevelBrushTopology t;
			levelBrushTopology(brush, &t);
			for (int i = 0; i < t.faces.size(); ++i) {
				const auto p = t.geometry.faces[i].plane;
				const double n[]{p.normalX, p.normalY, p.normalZ};
				if (n[axis] * direction > .99) {
					return {{LevelMapSelectionKind::QuakeBrush, brushId}, LevelBrushComponent::Face, {i}};
				}
			}
		}
	}
	return {};
}
LevelSweepRequest request(const LevelMapDocument& map)
{
	LevelSweepRequest r;
	r.faces = {face(map, 0)};
	r.texture = "studio/sweep";
	r.translation = {0, 0, 64, true};
	r.segments = 4;
	r.iterations = 2;
	return r;
}
} // namespace
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid() || argc != 2) {
		return 1;
	}
	QString error;
	for (const auto& dialect : {QString("classic"), QString("valve220"), QString("brushDef"), QString("brushDef3")}) {
		LevelMapDocument source;
		if (!expect(loadLevelMapBytes({temp.filePath(dialect + ".map"), {}, "idtech3"}, tests::surfaceFixture(dialect), &source, &error),
		            "load dialect", error)) {
			continue;
		}
		source.undoLimit = 1;
		const auto original = bytes(source);
		auto map = source;
		auto r = request(source);
		LevelSweepReport report;
		if (!expect(sweepLevelMap(&map, r, &report, &error), "sweep in native dialect", dialect + error)) {
			continue;
		}
		expect(report.attempted == 8 && report.brushIds.size() == 8 && report.skipped.isEmpty() && map.brushes.size() == 9 &&
		           map.selection.size() == 8,
		       "section count and new selection", dialect);
		for (int i = 0; i < report.brushIds.size(); ++i) {
			const auto& b = map.brushes[i + 1];
			LevelBrushTopology t;
			expect(levelBrushTopology(b, &t, &error), "valid persisted solid", error);
			expect(close(b.mins, {-32, 16, 56 + 16.0 * i, true}, .02) && close(b.maxs, {96, 80, 72 + 16.0 * i, true}, .02),
			       "independent straight bounds oracle", dialect);
			for (const auto& f : b.faces) {
				expect(f.textureName == r.texture && f.contentFlags == 0 && f.surfaceFlags == 0 && f.surfaceValue == 0,
				       "current sweep material and default flags", dialect);
			}
		}
		const auto swept = bytes(map);
		LevelMapDocument reopened;
		expect(loadLevelMapBytes({temp.filePath("roundtrip.map"), {}, "idtech3"}, swept, &reopened, &error) &&
		           reopened.brushes.size() == 9 && levelMapBrushDialect(reopened) == levelMapBrushDialect(source),
		       "roundtrip count and dialect", error);
		expect(map.undoStack.size() == 1 && undoLevelMapEdit(&map, &error) && bytes(map) == original, "single exact undo at history limit",
		       error);
		expect(redoLevelMapEdit(&map, &error) && bytes(map) == swept, "exact sweep redo", error);
	}
	LevelMapDocument source;
	expect(loadLevelMapBytes({temp.filePath("source.map"), {}, "idtech3"}, tests::surfaceFixture("classic"), &source, &error),
	       "load math source", error);
	auto r = request(source);
	r.integerAlignment = false;
	r.segments = 4;
	r.iterations = 1;
	r.faces = {face(source, 0, 0)};
	r.translation = {64, 64, 32, true};
	r.angle = 90;
	LevelSweepGeometry arc;
	expect(buildLevelSweep(source, r, &arc, &error) && arc.hulls.size() == 4, "arc sections", error);
	if (arc.hulls.size() == 4) {
		// Half of a quarter-circle of radius 64, plus 16 units of axial rise.
		expect(contains(arc.hulls[1].vertices, {163.882250994, 44.117749006, 8, true}), "analytic helical arc midpoint");
		expect(contains(arc.destinationCaps.last(), {192, 112, 24, true}), "analytic rotated destination cap");
	}
	r.path = LevelSweepPath::SBend;
	LevelSweepGeometry bend;
	expect(buildLevelSweep(source, r, &bend, &error) && bend.hulls.size() == 4, "S-bend sections", error);
	if (!bend.hulls.isEmpty()) {
		// At t=1/4, centre=(119.5,53.5,29); rotate local (0,-32,-32) by 22.5 degrees.
		expect(contains(bend.hulls.first().vertices, {131.745869836, 23.93585496, -3, true}), "analytic Hermite quarter station");
	}
	r.path = LevelSweepPath::Straight;
	r.faces = {face(source, 0)};
	r.translation = {0, 0, 64, true};
	r.scale = {2, .5, 1, true};
	r.iterations = 2;
	LevelSweepGeometry repeated;
	expect(buildLevelSweep(source, r, &repeated, &error), "repeated noncommuting transform", error);
	if (repeated.destinationCaps.size() == 2) {
		expect(contains(repeated.destinationCaps.last(), {96, 80, 184, true}), "affine power, not multiplied translation");
	} else {
		expect(false, "two destination caps");
	}
	auto opposite = r;
	opposite.iterations = 1;
	opposite.scale = {1, 1, 1, true};
	opposite.angle = 330;
	LevelSweepGeometry longTurn, shortTurn;
	auto shorter = opposite;
	shorter.angle = -30;
	expect(buildLevelSweep(source, opposite, &longTurn, &error) && buildLevelSweep(source, shorter, &shortTurn, &error),
	       "shortest rotation", error);
	if (!longTurn.destinationCaps.isEmpty() && !shortTurn.destinationCaps.isEmpty()) {
		for (int i = 0; i < longTurn.destinationCaps[0].size(); ++i) {
			expect(close(longTurn.destinationCaps[0][i], shortTurn.destinationCaps[0][i]), "330 degrees equals minus 30");
		}
	}
	auto cancelling = request(source);
	cancelling.faces = {face(source, 0, 2, 1), face(source, 0, 2, -1)};
	cancelling.iterations = 1;
	cancelling.integerAlignment = false;
	cancelling.path = LevelSweepPath::SBend;
	LevelSweepGeometry cancelledNormals;
	expect(buildLevelSweep(source, cancelling, &cancelledNormals, &error) && close(cancelledNormals.source.normal, {0, 0, 0, true}),
	       "opposite normals cancel, S-bend uses straight", error);
	// Integer snapping can intentionally omit individual zero-volume sections.
	LevelMapDocument fractional;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	createLevelMap(create, &fractional, &error);
	addLevelMapBoxBrush(&fractional, {.25, .25, .25, true}, {16.25, 16.25, 16.25, true}, "studio/original", nullptr, &error);
	auto snapped = request(fractional);
	snapped.segments = 8;
	snapped.iterations = 1;
	snapped.translation = {0, 0, 2, true};
	LevelSweepGeometry snappedGeometry;
	expect(buildLevelSweep(fractional, snapped, &snappedGeometry, &error) && snappedGeometry.hulls.size() == 2 &&
	           snappedGeometry.report.skipped.size() == 6,
	       "explicit degenerate-section report", error);
	if (!snappedGeometry.hulls.isEmpty()) {
		expect(contains(snappedGeometry.hulls.first().vertices, {.25, .25, 16.25, true}), "original off-grid station remains exact");
	}
	// A malformed request never partially inserts or changes selection/history.
	const auto original = bytes(source);
	const auto depth = source.undoStack.size();
	for (int bad = 0; bad < 7; ++bad) {
		auto invalid = request(source);
		if (bad == 0) {
			invalid.segments = 0;
		}
		if (bad == 1) {
			invalid.scale.x = 0;
		}
		if (bad == 2) {
			invalid.angle = std::numeric_limits<double>::infinity();
		}
		if (bad == 3) {
			invalid.faces[0].components = {100};
		}
		if (bad == 4) {
			invalid.rotationAxis = {0, 0, 0, true};
			invalid.angle = 90;
		}
		if (bad == 5) {
			invalid.texture = "bad material";
		}
		if (bad == 6) {
			invalid.translation = {0, 0, 0, true};
		}
		expect(!sweepLevelMap(&source, invalid, nullptr, &error) && bytes(source) == original && source.undoStack.size() == depth,
		       "invalid sweep is atomic", error);
	}
	LevelPlacementRequest work;
	work.operation = LevelPlacementOperation::Sweep;
	work.sweep = request(source);
	bool cancel = false;
	const auto cancelled = prepareLevelPlacement(source, work,
	                                             {[&] { return cancel; },
	                                              [&](const auto& p) {
		                                              if (p.phase == LevelPlacementPhase::Building && p.completed >= 2) {
			                                              cancel = true;
		                                              }
	                                              }});
	expect(cancelled.cancelled && !cancelled.succeeded && bytes(source) == original, "cancelled worker preserves source", cancelled.error);
	auto detailed = source;
	auto many = request(source);
	many.segments = 64;
	many.iterations = 8;
	LevelSweepReport manyReport;
	expect(sweepLevelMap(&detailed, many, &manyReport, &error) && manyReport.brushIds.size() == 512 && manyReport.skipped.isEmpty() &&
	           detailed.undoStack.size() == 1,
	       "512-section bounded workload", error);
	LevelMapDocument overBudget;
	expect(tests::createGeometryFixture(2, &overBudget, &error), "section limit fixture", error);
	auto excessive = many;
	excessive.faces = {{{LevelMapSelectionKind::QuakeBrush, 0}, LevelBrushComponent::Face, {0, 1, 2, 3, 4, 5}},
	                   {{LevelMapSelectionKind::QuakeBrush, 1}, LevelBrushComponent::Face, {0, 1, 2}}};
	const auto budgetBefore = bytes(overBudget);
	expect(!sweepLevelMap(&overBudget, excessive, nullptr, &error) && bytes(overBudget) == budgetBefore && error.contains("4096"),
	       "section budget rejects before insertion", error);
	// Different native owners and scene groups share one transaction.
	const auto& sourceBrush = source.brushes[0];
	const auto brushText = source.textLines.mid(sourceBrush.startLine - 1, sourceBrush.endLine - sourceBrush.startLine + 1).join('\n');
	const auto ownerText =
	    QString("{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"func_wall\"\n%1\n}\n{\n\"classname\" \"func_detail\"\n%1\n}\n")
	        .arg(brushText);
	LevelMapDocument owners;
	expect(loadLevelMapBytes({temp.filePath("owners.map"), {}, "idtech3"}, ownerText.toUtf8(), &owners, &error), "multi-owner fixture",
	       error);
	expect(owners.brushes.size() == 2, "two native owner brushes");
	QString group;
	createLevelSceneNode(&owners, LevelSceneNodeKind::Group, "source", {}, &group, &error);
	assignLevelSceneObjects(&owners, group, {"brush:0"}, &error);
	owners.undoLimit = 1;
	auto ownerRequest = request(owners);
	ownerRequest.faces << face(owners, 1);
	ownerRequest.segments = 1;
	ownerRequest.iterations = 1;
	const auto ownerBefore = bytes(owners);
	LevelSweepReport ownerReport;
	expect(sweepLevelMap(&owners, ownerRequest, &ownerReport, &error) && ownerReport.brushIds.size() == 2, "multi-owner sweep", error);
	if (owners.brushes.size() == 4) {
		expect(owners.brushes[2].entityId == 1 && owners.brushes[3].entityId == 2 &&
		           levelSceneNode(owners.scene, group)->objects.contains("brush:2") &&
		           !levelSceneNode(owners.scene, group)->objects.contains("brush:3"),
		       "owner and membership preserved");
	}
	expect(owners.undoStack.size() == 1 && undoLevelMapEdit(&owners, &error) && bytes(owners) == ownerBefore, "multi-owner exact undo",
	       error);
	setLevelSceneLocked(&owners, group, true, &error);
	const auto lockedBefore = bytes(owners);
	expect(!sweepLevelMap(&owners, ownerRequest, nullptr, &error) && bytes(owners) == lockedBefore,
	       "locked destination rejects whole sweep", error);
	// One inserted batch reaches a linked copy only after every section exists.
	auto linked = source;
	QString linkedGroup, linkedCopy;
	expect(createLevelSceneNode(&linked, LevelSceneNodeKind::Group, "sweep source", {}, &linkedGroup, &error) &&
	           assignLevelSceneObjects(&linked, linkedGroup, {"brush:0"}, &error) &&
	           createLinkedLevelGroup(&linked, linkedGroup, {256, 0, 0, true}, &linkedCopy, &error),
	       "linked sweep fixture", error);
	linked.undoLimit = 1;
	const auto linkedBefore = bytes(linked);
	LevelSweepReport linkedReport;
	expect(sweepLevelMap(&linked, request(linked), &linkedReport, &error), "linked sweep", error);
	const auto* originalGroup = levelSceneNode(linked.scene, linkedGroup);
	const auto* copyGroup = levelSceneNode(linked.scene, linkedCopy);
	expect(originalGroup && copyGroup && originalGroup->objects.size() == 9 && copyGroup->objects.size() == 9 &&
	           linked.brushes.size() == 18,
	       "all sweep sections propagate once to linked copy");
	if (originalGroup && copyGroup) {
		LevelMapVec3 originalMin, originalMax, copyMin, copyMax;
		expect(levelLinkedGroupBounds(linked, originalGroup->objects, &originalMin, &originalMax) &&
		           levelLinkedGroupBounds(linked, copyGroup->objects, &copyMin, &copyMax) &&
		           close(copyMin, {originalMin.x + 256, originalMin.y, originalMin.z, true}) &&
		           close(copyMax, {originalMax.x + 256, originalMax.y, originalMax.z, true}),
		       "linked copy retains its placement");
	}
	const auto linkedAfter = bytes(linked);
	expect(linked.undoStack.size() == 1 && undoLevelMapEdit(&linked, &error) && bytes(linked) == linkedBefore,
	       "linked sweep has one exact undo", error);
	expect(redoLevelMapEdit(&linked, &error) && bytes(linked) == linkedAfter, "linked sweep has one exact redo", error);
	// CLI uses the same geometry and protects the original/output on errors.
	const auto input = temp.filePath("input.map"), output = temp.filePath("swept.map");
	expect(saveLevelMapAs(source, input).succeeded(), "save CLI fixture");
	const auto run = [&](QStringList args, bool success = true) {
		QProcess p;
		p.setWorkingDirectory(temp.path());
		p.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "--json", "--settings-file", temp.filePath("settings.ini")} + args);
		const bool finished = p.waitForStarted() && p.waitForFinished(30000);
		const auto text = p.readAllStandardOutput();
		expect(finished && p.exitStatus() == QProcess::NormalExit && (p.exitCode() == 0) == success, "CLI result",
		       QString::fromUtf8(text + p.readAllStandardError()));
		if (!finished) {
			p.kill();
			p.waitForFinished();
		}
		return QJsonDocument::fromJson(text).object();
	};
	const auto id = face(source, 0).components.first();
	const QStringList args{"map",     "sweep",     input,          "--set",    QString("brush:0/face/%1").arg(id),
	                       "--delta", "0,0,64",    "--segments",   "4",        "--iterations",
	                       "2",       "--texture", "studio/sweep", "--output", output};
	const auto dry = run(args + QStringList{"--dry-run"});
	expect(!QFile::exists(output), "CLI dry run does not write");
	const auto done = run(args);
	expect(done.value("created").toInt() == 8, "CLI report count");
	LevelMapDocument loaded;
	expect(loadLevelMap({output, {}, "idtech3"}, &loaded, &error) && loaded.brushes.size() == 9, "CLI saved geometry", error);
	QFile file(output);
	expect(file.open(QIODevice::ReadOnly), "read sweep output");
	const auto saved = file.readAll();
	file.close();
	for (const auto& bad : {QStringList{"--segments", "0"}, QStringList{"--axis", "nan,0,1"}, QStringList{"--alignment", "grid"},
	                        QStringList{"--unknown"}, QStringList{"--path=arc", "--path=straight"}}) {
		run(args + bad + QStringList{"--overwrite"}, false);
		expect(file.open(QIODevice::ReadOnly), "reread sweep output");
		expect(file.readAll() == saved, "invalid CLI preserves existing output");
		file.close();
	}
	const auto fractionalPath = temp.filePath("fractional.map");
	expect(saveLevelMapAs(fractional, fractionalPath).succeeded(), "save fractional CLI fixture");
	const QStringList omittedArgs{"map",
	                              "sweep",
	                              fractionalPath,
	                              "--set",
	                              QString("brush:0/face/%1").arg(face(fractional, 0).components.first()),
	                              "--delta",
	                              "0,0,2",
	                              "--texture",
	                              "studio/sweep",
	                              "--output",
	                              temp.filePath("omitted.map"),
	                              "--dry-run"};
	const auto omissions = run(omittedArgs);
	expect(omissions.value("created").toInt() == 2 && omissions.value("skipped").toArray().size() == 6,
	       "CLI JSON reports omitted sections");
	QProcess textRun;
	textRun.setWorkingDirectory(temp.path());
	textRun.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "--settings-file", temp.filePath("settings.ini")} + omittedArgs);
	const bool textFinished = textRun.waitForStarted() && textRun.waitForFinished(30000);
	const auto textOutput = textRun.readAllStandardOutput();
	expect(textFinished && textRun.exitCode() == 0 && textOutput.contains("6 sections omitted") &&
	           textOutput.contains("iteration 1, segment"),
	       "CLI text reports omitted sections", QString::fromUtf8(textOutput));
	if (!textFinished) {
		textRun.kill();
		textRun.waitForFinished();
	}
	if (ok) {
		std::cout << "Sweep geometry, ownership, history, cancellation and CLI checks passed.\n";
	}
	return ok ? 0 : 1;
}
