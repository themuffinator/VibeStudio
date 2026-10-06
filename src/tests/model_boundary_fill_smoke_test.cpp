#include "core/model_boundary_fill.h"
#include "core/model_recovery.h"
#include "core/model_topology_health.h"
#include "tests/model_boundary_fill_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace
{
int checks = 0;
bool expect(bool condition, const char *message)
{
	++checks;
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return condition;
}
QByteArray bytes(const ModelMesh &mesh)
{
	return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact);
}
bool close(double a, double b)
{
	return std::abs(a - b) < 1e-6;
}
double volume(const ModelSurface &surface, int frame)
{
	double sum = 0;
	for (auto t : surface.triangles)
	{
		const auto a = surface.frames[frame].positions[t.a], b = surface.frames[frame].positions[t.b],
				   c = surface.frames[frame].positions[t.c];
		sum += double(a.x) * (double(b.y) * c.z - double(b.z) * c.y) + double(a.y) * (double(b.z) * c.x - double(b.x) * c.z) +
			   double(a.z) * (double(b.x) * c.y - double(b.y) * c.x);
	}
	return sum / 6;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root) || argc != 2)
	{
		return 1;
	}
	QTemporaryDir temporary(QDir(root).filePath("boundary-fill-XXXXXX"));
	if (!temporary.isValid())
	{
		return 1;
	}
	bool ok = true;
	QString error;
	auto original = tests::boundaryPrism();
	// A separate surface is deliberately retained, including all its attributes.
	original.surfaces << original.surfaces[0];
	original.surfaces[1].name = QStringLiteral("retained");
	for (auto &frame : original.surfaces[1].frames)
	{
		for (auto &p : frame.positions)
		{
			p.x += 30;
		}
	}
	updateEditableModelMetadata(&original);
	ModelDocument document;
	ok &= expect(validateEditableModel(original).isEmpty() && document.setMesh(original, &error), "open animated prism fixture");
	ModelEdit edit;
	edit.kind = ModelEditKind::FillBoundaryLoops;
	edit.selection.edges = {{4, 5}};
	document.setSelection(edit.selection);
	ok &= expect(document.edit(edit, &error), "one boundary edge expands to the complete hole");
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
		return 1;
	}
	const auto filled = bytes(document.mesh());
	ModelTopologyHealth health;
	ok &= expect(inspectModelTopology(document.mesh().surfaces[0], &health, &error) && health.boundaryEdges.isEmpty() &&
					 health.nonmanifoldEdges.isEmpty() && health.windingEdges.isEmpty() && health.disconnectedFans.isEmpty() &&
					 close(volume(document.mesh().surfaces[0], 0), 64),
				 "cap closes the prism with outward winding and the independent expected volume");
	ok &= expect(document.selection().faces == QSet<int>{10, 11} && document.selection().vertices.isEmpty() &&
					 document.selection().edges.isEmpty(),
				 "new cap faces become the complete selection");
	auto retained = document.mesh();
	retained.surfaces[0].triangles.resize(10);
	updateEditableModelMetadata(&retained);
	ok &= expect(bytes(retained) == bytes(original),
				 "all old geometry, poses, UVs, authored normals, seams, materials, tags, collision and other surfaces survive exactly");
	ok &= expect(document.undo() && bytes(document.mesh()) == bytes(original) && document.selection() == edit.selection &&
					 !document.canUndo(),
				 "fill is one atomic undo including its edge selection");
	ok &= expect(document.redo() && bytes(document.mesh()) == filled && document.selection().faces == QSet<int>{10, 11},
				 "redo restores cap and selected faces");
	ModelMesh decoded;
	ok &= expect(parseEditableModel(filled, &decoded, &error) && bytes(decoded) == filled, "cap round-trips through editable source");
	ModelRecoverySnapshot recovery{document.mesh(), document.selection(), 1, "Boundary fill", {}, {}};
	const auto recoveryPath = writeModelRecovery(recovery, temporary.path(), "5314c9fe-cf06-4cb5-beb4-6380a55a81dd", &error);
	ModelRecoverySnapshot restored;
	ok &= expect(!recoveryPath.isEmpty() && inspectModelRecovery(recoveryPath, &restored).isValid() && bytes(restored.mesh) == filled &&
					 restored.selection == document.selection(),
				 "recovery retains cap, poses and the finishing selection");
	const auto native = exportEditableModel(document.mesh(), "md3", 0, &error);
	ok &= expect(!native.isEmpty() && importEditableModel("filled.md3", native, &decoded, &error) && decoded.frameCount == 2 &&
					 decoded.tagCount == 1 && decoded.triangleCount == 22 && close(volume(decoded.surfaces[0], 0), 64),
				 "native MD3 export retains filled winding, poses and attachments");
	ModelSurface output;
	ModelBoundaryFillReport report;
	const auto surface = tests::boundaryPrism().surfaces[0];
	ok &= expect(fillModelBoundaryLoops(surface, {{4, 7}, {5, 6}}, 0, 131072, &output, &report, &error) && report.loopCount == 1 &&
					 report.boundaryEdges == QVector<ModelEdge>{{4, 5}, {4, 7}, {5, 6}, {6, 7}} && report.faces == QVector<int>{10, 11},
				 "partial redundant seeds expand once in deterministic boundary order");
	auto deterministic = tests::boundaryPrism();
	deterministic.surfaces[0] = output;
	updateEditableModelMetadata(&deterministic);
	auto single = tests::boundaryPrism();
	ok &= expect(applyModelEdit(&single, edit, nullptr, &error) && bytes(single) == bytes(deterministic),
				 "seed edge choice never changes triangulation");
	for (const auto &top : QVector<QVector<ModelVec3>>{{{-2, -2, 2}, {2, -2, 2}, {2, 0, 2}, {0, 0, 2}, {0, 2, 2}, {-2, 2, 2}},
													   {{-2, -2, 2}, {0, -2, 2}, {2, -2, 2}, {2, 2, 2}, {-2, 2, 2}},
													   {{-2, -2, 2}, {2, -2, 3}, {2, 2, 2.5f}, {-2, 2, 2}}})
	{
		auto mesh = tests::boundaryPrism(top, false);
		ModelEdit both = edit;
		both.selection.edges = {{0, 1}, {int(top.size()), int(top.size() + 1)}};
		const auto validation = validateEditableModel(mesh);
		const bool applied = validation.isEmpty() && applyModelEdit(&mesh, both, nullptr, &error);
		const auto fillError = error;
		const bool accepted =
			expect(applied && inspectModelTopology(mesh.surfaces[0], &health, &error) && health.boundaryEdges.isEmpty() &&
					   health.windingEdges.isEmpty() && health.nonmanifoldEdges.isEmpty() && mesh.triangleCount == 4 * top.size() - 4,
				   "concave, collinear-corner and nonplanar loops fill both ends without losing boundary vertices");
		ok &= accepted;
		if (!accepted)
		{
			std::cerr << "loop vertices=" << top.size() << " applied=" << applied << " triangles=" << mesh.triangleCount
					  << " boundary=" << health.boundaryEdges.size() << " winding=" << health.windingEdges.size()
					  << " nonmanifold=" << health.nonmanifoldEdges.size() << " validation=" << validation.join("; ").toStdString()
					  << " fill=" << fillError.toStdString() << '\n';
		}
	}
	for (double scale : {.001, 1000.0})
	{
		auto scaled = tests::boundaryPrism();
		for (auto &pose : scaled.surfaces[0].frames)
		{
			for (auto &p : pose.positions)
			{
				p = {float(p.x * scale), float(p.y * scale), float(p.z * scale)};
				if (scale > 1)
				{
					p.x += 100000;
					p.y -= 100000;
					p.z += 100000;
				}
			}
		}
		updateEditableModelMetadata(&scaled);
		ok &= expect(applyModelEdit(&scaled, edit, nullptr, &error), "small and large translated geometry keeps legal boundary contacts");
	}
	auto alias = surface;
	ok &= expect(fillModelBoundaryLoops(alias, {{4, 5}}, 0, 12, &alias, nullptr, &error) && alias.triangles.size() == 12,
				 "service can transactionally replace its own input surface");
	auto annulus = tests::boundaryPrism({{-1, -1, 2}, {1, -1, 2}, {1, 1, 2}, {-1, 1, 2}}, false);
	auto &annulusSurface = annulus.surfaces[0];
	annulusSurface.frames[0].positions = {{-2, -2, 2}, {2, -2, 2}, {2, 2, 2}, {-2, 2, 2}, {-1, -1, 2}, {1, -1, 2}, {1, 1, 2}, {-1, 1, 2}};
	annulusSurface.frames[1] = annulusSurface.frames[0];
	updateEditableModelMetadata(&annulus);
	ok &= expect(applyModelEdit(&annulus, edit, nullptr, &error) && annulus.triangleCount == 10 &&
					 inspectModelTopology(annulus.surfaces[0], &health, &error) &&
					 health.boundaryEdges == QVector<ModelEdge>{{0, 1}, {0, 3}, {1, 2}, {2, 3}},
				 "chosen inner hole fills a planar ring while its unselected outer perimeter stays open");
	auto changing = tests::boundaryChangingConcavity();
	const auto changingBefore = bytes(changing);
	ok &= expect(validateEditableModel(changing).isEmpty() && !applyModelEdit(&changing, edit, nullptr, &error) &&
					 error.contains("frame 1") && bytes(changing) == changingBefore,
				 "reference cap that folds in a later pose fails atomically");
	ModelEdit reference = edit;
	reference.sourceFrame = 1;
	ok &= expect(applyModelEdit(&changing, reference, nullptr, &error) && changing.triangleCount == 12,
				 "another reference pose can choose a triangulation valid for all poses");
	const auto refuse = [&](ModelMesh mesh, ModelEdit action, const char *message, const QString &diagnostic = {}) {
		const auto before = bytes(mesh);
		ModelSelection sentinel{0, {2}, {}, {}};
		const bool rejected = !applyModelEdit(&mesh, action, &sentinel, &error);
		const bool result = expect(rejected && bytes(mesh) == before && sentinel.vertices == QSet<int>{2} && !error.isEmpty() &&
									   (diagnostic.isEmpty() || error.contains(diagnostic)),
								   message);
		if (!result)
		{
			std::cerr << error.toStdString() << '\n';
		}
		return result;
	};
	for (int variant = 0; variant < 7; ++variant)
	{
		auto invalid = edit;
		if (variant == 0)
			invalid.selection.edges.clear();
		if (variant == 1)
			invalid.selection.edges = {{0, 1}};
		if (variant == 2)
			invalid.selection.edges = {{5, 4}};
		if (variant == 3)
			invalid.selection.faces = {0};
		if (variant == 4)
			invalid.selection.vertices = {0};
		if (variant == 5)
			invalid.frame = 0;
		if (variant == 6)
			invalid.sourceFrame = 2;
		ok &= refuse(tests::boundaryPrism(), invalid,
					 "empty, interior, reversed, mixed, single-frame and invalid reference selections are refused");
	}
	auto flipped = tests::boundaryPrism();
	std::swap(flipped.surfaces[0].triangles[1].b, flipped.surfaces[0].triangles[1].c);
	ok &= refuse(flipped, edit, "inconsistent boundary winding cannot produce a cap");
	auto branching = tests::boundaryPrism();
	branching.surfaces[0].triangles << ModelTriangle{4, 6, 0};
	ok &= refuse(branching, edit, "branching boundary incidence cannot produce a cap");
	auto crossing = tests::boundaryPrism({{-2, -2, 2}, {2, 2, 2}, {-2, 2, 2}, {2, -2, 2}}, false);
	ok &= refuse(crossing, edit, "self-crossing projected boundary is refused");
	auto flat = tests::boundaryPrism();
	flat.surfaces[0].triangles = {{4, 5, 6}, {4, 6, 7}};
	ok &= refuse(flat, edit, "an already filled sheet cannot be doubled by treating its perimeter as a hole", "intersects");
	// A disconnected coplanar triangle covers the proposed hole with separate
	// vertex indices, as can happen at a UV seam. A positive area overlap fails.
	for (const auto &obstacle : QVector<QVector<ModelVec3>>{{{-1, -1, 2}, {1, -1, 2}, {0, 1, 2}},
															{{0, -1, 1}, {0, 1, 1}, {0, 0, 3}},
															{{0, -1, 2}, {0, 1, 2}, {0, 0, 4}},
															{{-1, -1, 1.999999f}, {1, -1, 2.000001f}, {0, 1, 2.000001f}},
															{{-3, -3, 2}, {3, -3, 2}, {0, 4, 2}}})
	{
		auto blocked = tests::boundaryPrism();
		tests::appendBoundaryObstacle(&blocked, obstacle);
		ok &= refuse(blocked, edit, "coplanar, crossing and edge-through-interior obstructions are refused", "intersects");
	}
	auto later = tests::boundaryPrism();
	later.surfaces[0].frames[1] = later.surfaces[0].frames[0];
	tests::appendBoundaryObstacle(&later, {{-.5f, -.5f, 3}, {.5f, -.5f, 3}, {0, .5f, 3}});
	for (int v = 8; v < 11; ++v)
		later.surfaces[0].frames[1].positions[v].z = 2;
	updateEditableModelMetadata(&later);
	ok &= refuse(later, edit, "cap intersection unique to a later pose is refused", "frame 1");
	auto touching = tests::boundaryPrism();
	touching.surfaces[0].frames[1] = touching.surfaces[0].frames[0];
	tests::appendBoundaryObstacle(&touching, {{-2, -2, 2}, {2, -2, 2}, {0, -3, 3}});
	ok &=
		expect(applyModelEdit(&touching, edit, nullptr, &error), "legal shared geometric boundary contact survives different seam indices");
	auto partialTouch = tests::boundaryPrism();
	partialTouch.surfaces[0].frames[1] = partialTouch.surfaces[0].frames[0];
	tests::appendBoundaryObstacle(&partialTouch, {{-1, -2, 2}, {1, -2, 2}, {0, -3, 2.000001f}});
	ok &= expect(applyModelEdit(&partialTouch, edit, nullptr, &error),
				 "near-coplanar partial boundary contact remains legal without shared indices");
	auto disjoint = tests::boundaryPrism();
	tests::appendBoundaryObstacle(&disjoint, {{-1, -1, 20}, {1, -1, 20}, {0, 1, 20}});
	ok &= expect(applyModelEdit(&disjoint, edit, nullptr, &error), "unrelated open geometry does not block the chosen hole");
	// A triangular hole whose three vertices become collinear only in pose 1.
	auto collapsed = tests::boundaryPrism({{-2, -2, 2}, {2, -2, 2}, {0, 2, 2}}, false);
	collapsed.surfaces[0].frames[1] = collapsed.surfaces[0].frames[0];
	collapsed.surfaces[0].frames[1].positions[5] = {0, -2, 2};
	updateEditableModelMetadata(&collapsed);
	ModelEdit triangleEdit = edit;
	triangleEdit.selection.edges = {{3, 4}};
	ok &= refuse(collapsed, triangleEdit, "all-pose validation catches a cap collapsing beyond the displayed pose", "frame 1");
	const auto preserved = output.triangles.size();
	report.loopCount = 99;
	report.faces = {77};
	ok &= expect(!fillModelBoundaryLoops(surface, {{4, 5}}, 0, 11, &output, &report, &error) && output.triangles.size() == preserved &&
					 report.loopCount == 99 && report.faces == QVector<int>{77},
				 "triangle-capacity failure retains both output values");
	ModelWorkControl cancelled;
	cancelled.cancelled = [] { return true; };
	ok &= expect(!fillModelBoundaryLoops(surface, {{4, 5}}, 0, 131072, &output, &report, &error, cancelled) && report.loopCount == 99,
				 "pre-cancelled fill retains outputs");
	bool stop = false;
	ModelWorkControl during{[&] { return stop; },
							[&](ModelWorkPhase, qint64 done, qint64) {
								if (done >= 256)
									stop = true;
							}};
	QVector<ModelVec3> ring;
	for (int i = 0; i < 1025; ++i)
	{
		const double angle = i * 6.283185307179586 / 1025;
		ring << ModelVec3{float(10 * std::cos(angle)), float(10 * std::sin(angle)), 2};
	}
	auto large = tests::boundaryPrism(ring, false).surfaces[0];
	ok &= expect(!fillModelBoundaryLoops(large, {{1025, 1026}}, 0, 131072, &output, &report, &error, during) && stop &&
					 report.loopCount == 99,
				 "large boundary traversal responds to cancellation before publishing");
	ok &= expect(!fillModelBoundaryLoops(large, {{1025, 1026}}, 0, 131072, &output, &report, &error) && error.contains("1024") &&
					 report.loopCount == 99,
				 "oversized boundary loop refuses bounded work atomically");
	ring.clear();
	for (int i = 0; i < 1024; ++i)
	{
		const double angle = i * 6.283185307179586 / 1024;
		ring << ModelVec3{float(10 * std::cos(angle)), float(10 * std::sin(angle)), 2};
	}
	auto maximum = tests::boundaryPrism(ring, false);
	ModelEdit maximumEdit = edit;
	maximumEdit.selection.edges = {{1024, 1025}};
	ok &= expect(applyModelEdit(&maximum, maximumEdit, nullptr, &error) && maximum.triangleCount == 3070 &&
					 inspectModelTopology(maximum.surfaces[0], &health, &error) && health.boundaryEdges.size() == 1024 &&
					 health.nonmanifoldEdges.isEmpty() && health.windingEdges.isEmpty(),
				 "maximum supported loop fills in both poses within the work budget");
	if (maximum.triangleCount != 3070)
		std::cerr << error.toStdString() << '\n';
	ring.clear();
	for (int i = 0; i < 128; ++i)
	{
		const double angle = i * 6.283185307179586 / 128;
		ring << ModelVec3{float(10 * std::cos(angle)), float(10 * std::sin(angle)), 2};
	}
	stop = false;
	ModelWorkControl geometryCancel{[&] { return stop; },
									[&](ModelWorkPhase, qint64 done, qint64) {
										if (done >= 4096)
											stop = true;
									}};
	const auto medium = tests::boundaryPrism(ring, false).surfaces[0];
	ok &= expect(!fillModelBoundaryLoops(medium, {{128, 129}}, 0, 131072, &output, &report, &error, geometryCancel) && stop &&
					 report.loopCount == 99 && output.triangles.size() == preserved,
				 "inner geometric checks poll cancellation and retain outputs");
	// Valid disjoint obstacle faces force a deterministic workload refusal, not
	// an early geometric failure. Runtime is bounded even with many animation poses.
	auto budget = tests::boundaryPrism().surfaces[0];
	for (int i = 0; i < 25000; ++i)
	{
		budget.triangles << ModelTriangle{0, 2, 1};
	}
	budget.frames.fill(budget.frames[0], 340);
	QElapsedTimer elapsed;
	elapsed.start();
	ok &= expect(!fillModelBoundaryLoops(budget, {{4, 5}}, 0, 131072, &output, &report, &error) && error.contains("geometry-check limit") &&
					 report.loopCount == 99,
				 "geometry comparison budget bounds all-pose work without partial results");
	std::cout << "Geometry-budget refusal: " << elapsed.elapsed() << " ms\n";
	auto malformed = surface;
	malformed.triangles[0].a = 999;
	ok &= expect(!fillModelBoundaryLoops(malformed, {{4, 5}}, 0, 131072, &output, &report, &error),
				 "direct service checks triangle indices before use");
	malformed = surface;
	malformed.frames[1].positions.removeLast();
	ok &= expect(!fillModelBoundaryLoops(malformed, {{4, 5}}, 0, 131072, &output, &report, &error),
				 "direct service checks frame lengths before use");
	malformed = surface;
	malformed.frames[0].positions[4].x = std::numeric_limits<float>::quiet_NaN();
	ok &= expect(!fillModelBoundaryLoops(malformed, {{4, 5}}, 0, 131072, &output, &report, &error),
				 "direct service rejects nonfinite positions");
	const auto input = QDir(temporary.path()).filePath("hole.mesh.json"), target = QDir(temporary.path()).filePath("filled.mesh.json");
	ModelDocument fixture;
	ok &= expect(fixture.setMesh(tests::boundaryChangingConcavity(), &error) && fixture.save(input, false, &error),
				 "write CLI source fixture");
	const auto cli = [&](QStringList args, int expected) {
		QProcess process;
		process.setWorkingDirectory(temporary.path());
		args.prepend("--cli");
		args << "--json" << "--settings-file" << QDir(temporary.path()).filePath("settings.ini");
		process.start(app.arguments()[1], args);
		const bool finished = process.waitForStarted(10000) && process.waitForFinished(30000);
		const auto data = process.readAllStandardOutput();
		if (!finished || process.exitCode() != expected)
			std::cerr << data.constData() << process.readAllStandardError().constData();
		return finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected &&
			   QJsonDocument::fromJson(data).isObject();
	};
	const QStringList command{"model",			"edit", input,		"--operation", "fill-boundary-loops", "--edges", "4:5",
							  "--source-frame", "1",	"--output", target};
	ok &= expect(cli(command + QStringList{"--dry-run"}, 0) && !QFileInfo::exists(target),
				 "CLI dry-run validates every pose without writing");
	ok &= expect(cli(command, 0) && fixture.load(target, &error) && bytes(fixture.mesh()) == bytes(changing),
				 "CLI and document produce exactly the same all-pose cap");
	ok &= expect(cli(command, 1) && cli(command + QStringList{"--overwrite"}, 0), "CLI retains explicit overwrite protection");
	for (const auto &options : QVector<QStringList>{{"--faces", "0"}, {"--vertices", "0"}, {"--frame", "1"}})
	{
		ok &= expect(cli(command + options, 2), "CLI refuses ambiguous component and pose scope");
	}
	ok &= expect(cli({"model", "edit", input, "--operation", "fill-boundary-loops", "--output", target}, 2),
				 "CLI requires explicit edge selection");
	const auto rejected = QDir(temporary.path()).filePath("rejected.mesh.json");
	ok &= expect(cli({"model", "edit", input, "--operation", "fill-boundary-loops", "--edges", "4:5", "--output", rejected}, 4) &&
					 !QFileInfo::exists(rejected),
				 "invalid later-pose cap never writes CLI output");
	if (!ok)
		std::cerr << error.toStdString() << '\n';
	std::cout << checks << " boundary fill assertions\n";
	return ok ? 0 : 1;
}
