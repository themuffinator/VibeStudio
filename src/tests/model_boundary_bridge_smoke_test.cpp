#include "core/model_boundary_bridge.h"
#include "core/model_recovery.h"
#include "core/model_topology_health.h"
#include "tests/model_boundary_bridge_test_helpers.h"
#include "tests/model_boundary_fill_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <algorithm>
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
		std::cerr << "FAIL: " << message << '\n';
	return condition;
}
QByteArray bytes(const ModelMesh &mesh) { return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact); }
bool sameTriangles(const QVector<ModelTriangle> &a, const QVector<ModelTriangle> &b)
{
	return std::equal(a.cbegin(), a.cend(), b.cbegin(), b.cend(),
		[](auto x, auto y) { return x.a == y.a && x.b == y.b && x.c == y.c; });
}
double volume(const ModelSurface &surface, int frame)
{
	double value = 0;
	for (auto t : surface.triangles)
	{
		const auto a = surface.frames[frame].positions[t.a], b = surface.frames[frame].positions[t.b], c = surface.frames[frame].positions[t.c];
		value += double(a.x) * (double(b.y) * c.z - double(b.z) * c.y) + double(a.y) * (double(b.z) * c.x - double(b.x) * c.z) +
			double(a.z) * (double(b.x) * c.y - double(b.y) * c.x);
	}
	return value / 6;
}
bool closed(const ModelSurface &surface)
{
	ModelTopologyHealth health;
	return inspectModelTopology(surface, &health) && health.faceComponents == 1 && health.boundaryEdges.isEmpty() &&
		health.nonmanifoldEdges.isEmpty() && health.windingEdges.isEmpty() && health.duplicateFaces.isEmpty() && health.disconnectedFans.isEmpty();
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root) || argc != 2)
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("boundary-bridge-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	bool ok = true;
	QString error;
	auto original = tests::bridgeDisks();
	original.surfaces << original.surfaces[0];
	original.surfaces[1].name = "retained";
	for (auto &pose : original.surfaces[1].frames)
		for (auto &p : pose.positions)
			p.x += 30;
	updateEditableModelMetadata(&original);
	ModelDocument document;
	ModelEdit edit;
	edit.kind = ModelEditKind::BridgeBoundaryLoops;
	edit.selection.edges = {{0, 1}, {4, 5}};
	ok &= expect(validateEditableModel(original).isEmpty() && document.setMesh(original, &error), "open animated disjoint-disk fixture");
	document.setSelection(edit.selection);
	ok &= expect(document.edit(edit, &error), "two boundary seeds bridge complete loops");
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
		return 1;
	}
	const auto bridged = bytes(document.mesh());
	ok &= expect(closed(document.mesh().surfaces[0]) && std::abs(volume(document.mesh().surfaces[0], 0) - 64) < 1e-6,
		"bridge closes a manifold cube with independently expected outward volume");
	ok &= expect(std::abs(volume(document.mesh().surfaces[0], 1) - 64 * 1.1 * .8) < 1e-4,
		"same bridge indices retain the second pose's affine volume");
	const QSet<int> newFaces{4, 5, 6, 7, 8, 9, 10, 11};
	ok &= expect(document.selection().faces == newFaces && document.selection().edges.isEmpty() && document.selection().vertices.isEmpty(),
		"new faces alone become the finishing selection");
	auto retained = document.mesh();
	retained.surfaces[0].triangles.resize(4);
	updateEditableModelMetadata(&retained);
	ok &= expect(bytes(retained) == bytes(original), "all original attributes, materials, animation rates, tags, collision and other surfaces remain exact");
	ok &= expect(document.undo() && bytes(document.mesh()) == bytes(original) && document.selection() == edit.selection && !document.canUndo(),
		"one undo restores complete source and seeded selection");
	ok &= expect(document.redo() && bytes(document.mesh()) == bridged && document.selection().faces == newFaces,
		"redo restores exact geometry and finishing selection");
	ModelMesh decoded;
	ok &= expect(parseEditableModel(bridged, &decoded, &error) && bytes(decoded) == bridged, "editable save round trip preserves the bridge");
	ModelRecoverySnapshot snapshot{document.mesh(), document.selection(), 1, "Bridge", {}, {}};
	const auto recoveryPath = writeModelRecovery(snapshot, temporary.path(), "486d9fa7-f5b7-4fd7-99fc-40a177912e54", &error);
	ModelRecoverySnapshot recovered;
	ok &= expect(!recoveryPath.isEmpty() && inspectModelRecovery(recoveryPath, &recovered).isValid() && bytes(recovered.mesh) == bridged &&
		recovered.selection == document.selection(), "recovery preserves every pose and new-face selection");
	const auto native = exportEditableModel(document.mesh(), "md3", 0, &error);
	ok &= expect(!native.isEmpty() && importEditableModel("bridge.md3", native, &decoded, &error) && closed(decoded.surfaces[0]) &&
		decoded.frameCount == 2 && decoded.tagCount == 1, "MD3 export retains bridge topology, animation and attachments");
	for (const auto &counts : {QPair<int, int>{3, 4}, {4, 7}, {9, 3}, {8, 8}, {17, 31}})
	{
		auto mesh = tests::bridgeDisks(counts.first, counts.second);
		ModelEdit unequal = edit;
		unequal.selection.edges = {{0, 1}, {counts.first, counts.first + 1}};
		const bool success = applyModelEdit(&mesh, unequal, nullptr, &error);
		ok &= expect(success && closed(mesh.surfaces[0]) && mesh.triangleCount == 2 * (counts.first + counts.second) - 4,
			"unequal and equal loops form exactly one closed manifold without new vertices");
		if (!success)
			std::cerr << "counts=" << counts.first << ',' << counts.second << ' ' << error.toStdString() << '\n';
	}
	auto square = tests::bridgeDisks();
	ModelSurface output;
	ModelBoundaryBridgeReport receipt;
	ok &= expect(bridgeModelBoundaryLoops(square.surfaces[0], edit.selection.edges, 0, 0, 12, &output, &receipt, &error) &&
		receipt.faces.size() == 8 && receipt.boundaryEdges.size() == 8 && receipt.firstAnchor == 0 && receipt.secondAnchor == 4,
		"closest-pair alignment and full boundary receipt are deterministic");
	auto deterministic = square;
	deterministic.surfaces[0] = output;
	updateEditableModelMetadata(&deterministic);
	ModelEdit reordered = edit;
	reordered.selection.edges = {{0, 3}, {1, 2}, {5, 6}, {4, 7}};
	ok &= expect(applyModelEdit(&square, reordered, nullptr, &error) && bytes(square) == bytes(deterministic),
		"redundant seed choices and hash order cannot change the strip");
	auto twisted = tests::bridgeDisks();
	ModelEdit twist = edit;
	twist.bridgeTwist = 1;
	ok &= expect(applyModelEdit(&twisted, twist, nullptr, &error) && closed(twisted.surfaces[0]), "explicit twist produces a valid alternate bridge");
	for (int offset : {-3, 5})
	{
		auto equivalent = tests::bridgeDisks();
		twist.bridgeTwist = offset;
		ok &= expect(applyModelEdit(&equivalent, twist, nullptr, &error) && bytes(equivalent) == bytes(twisted),
			"positive and negative twist wrap around the second loop");
	}
	for (double scale : {.001, 1000.0})
	{
		auto mesh = tests::bridgeDisks();
		for (auto &pose : mesh.surfaces[0].frames)
			for (auto &p : pose.positions)
				p = {float(scale * p.x + (scale > 1 ? 100000 : 0)), float(scale * p.y), float(scale * p.z)};
		updateEditableModelMetadata(&mesh);
		ok &= expect(applyModelEdit(&mesh, edit, nullptr, &error) && closed(mesh.surfaces[0]), "small and large translated bridges preserve legal contacts");
	}
	const auto refuse = [&](ModelMesh mesh, ModelEdit action, const char *message, const QString &detail = {}) {
		const auto before = bytes(mesh);
		ModelSelection selection{0, {2}, {}, {}};
		const bool result = !applyModelEdit(&mesh, action, &selection, &error) && bytes(mesh) == before && selection.vertices == QSet<int>{2} &&
			(detail.isEmpty() || error.contains(detail));
		if (!result)
			std::cerr << error.toStdString() << '\n';
		return expect(result, message);
	};
	ok &= refuse(tests::collapsingBridge(), edit, "collapse only in a later pose rejects the whole edit", "frame 1");
	auto obstructed = tests::bridgeDisks();
	tests::appendBoundaryObstacle(&obstructed, {{-3, 0, 0}, {3, 0, 0}, {0, 3, 0}});
	ok &= refuse(obstructed, edit, "new bridge intersections with existing geometry fail atomically", "intersects");
	ModelEdit bad = edit;
	bad.selection.edges = {{0, 1}};
	ok &= refuse(tests::bridgeDisks(), bad, "one boundary cannot masquerade as a bridge", "exactly two");
	bad.selection.edges = {{0, 2}, {4, 5}};
	ok &= refuse(tests::bridgeDisks(), bad, "interior-edge seeds are refused", "not a boundary");
	bad = edit;
	bad.selection.vertices = {0};
	ok &= refuse(tests::bridgeDisks(), bad, "mixed edge and vertex selection is refused");
	bad = edit;
	bad.frame = 0;
	ok &= refuse(tests::bridgeDisks(), bad, "single-frame topology changes are refused");
	bad = edit;
	bad.sourceFrame = 2;
	ok &= refuse(tests::bridgeDisks(), bad, "missing reference pose is refused");
	bad = edit;
	bad.bridgeTwist = 1024;
	ok &= refuse(tests::bridgeDisks(), bad, "out-of-range twist is refused");
	bad.kind = ModelEditKind::FillBoundaryLoops;
	bad.bridgeTwist = 1;
	ok &= refuse(tests::bridgeDisks(), bad, "inapplicable bridge controls are refused");
	const auto preserved = output.triangles;
	receipt.firstAnchor = 99;
	receipt.faces = {77};
	ok &= expect(!bridgeModelBoundaryLoops(tests::bridgeDisks().surfaces[0], edit.selection.edges, 0, 0, 11, &output, &receipt, &error) &&
		sameTriangles(output.triangles, preserved) && receipt.firstAnchor == 99 && receipt.faces == QVector<int>{77},
		"capacity failure retains result and receipt");
	const auto source = tests::bridgeDisks().surfaces[0];
	ModelWorkControl cancelled{[] { return true; }, {}};
	ok &= expect(!bridgeModelBoundaryLoops(source, edit.selection.edges, 0, 0, 12, &output, &receipt, &error, cancelled) &&
		sameTriangles(output.triangles, preserved) && receipt.firstAnchor == 99, "pre-cancellation leaves both outputs intact");
	const auto medium = tests::bridgeDisks(48, 65).surfaces[0];
	bool stop = false;
	ModelWorkControl during{[&] { return stop; }, [&](ModelWorkPhase, qint64 done, qint64) { if (done >= 4096) stop = true; }};
	ok &= expect(!bridgeModelBoundaryLoops(medium, {{0, 1}, {48, 49}}, 0, 131072, 131072, &output, &receipt, &error, during),
		"invalid twist is rejected before traversal");
	ok &= expect(!bridgeModelBoundaryLoops(medium, {{0, 1}, {48, 49}}, 0, 0, 131072, &output, &receipt, &error, during) && stop &&
		sameTriangles(output.triangles, preserved) && receipt.firstAnchor == 99, "cancellation during alignment/triangulation cannot publish a partial bridge");
	auto maximum = tests::bridgeDisks(1024, 1024).surfaces[0];
	maximum.frames.fill(maximum.frames[0], 12);
	QElapsedTimer elapsed;
	elapsed.start();
	ok &= expect(!bridgeModelBoundaryLoops(maximum, {{0, 1}, {1024, 1025}}, 0, 0, 131072, &output, &receipt, &error) &&
		error.contains("geometry-check limit") && receipt.firstAnchor == 99, "work budget bounds large all-pose validation");
	std::cout << "Bridge budget refusal: " << elapsed.elapsed() << " ms\n";
	const auto oversized = tests::bridgeDisks(1025, 3).surfaces[0];
	ok &= expect(!bridgeModelBoundaryLoops(oversized, {{0, 1}, {1025, 1026}}, 0, 0, 131072, &output, &receipt, &error) && error.contains("1024"),
		"oversized loops fail before dynamic-programming allocation");
	auto malformed = source;
	malformed.triangles[0].a = 999;
	ok &= expect(!bridgeModelBoundaryLoops(malformed, edit.selection.edges, 0, 0, 131072, &output, &receipt, &error), "bad source indices are bounded before use");
	malformed = source;
	malformed.frames[1].positions.removeLast();
	ok &= expect(!bridgeModelBoundaryLoops(malformed, edit.selection.edges, 0, 0, 131072, &output, &receipt, &error), "bad pose length is refused");
	malformed = source;
	malformed.frames[0].positions[0].x = std::numeric_limits<float>::quiet_NaN();
	ok &= expect(!bridgeModelBoundaryLoops(malformed, edit.selection.edges, 0, 0, 131072, &output, &receipt, &error), "nonfinite positions are refused");
	auto alias = source;
	ok &= expect(bridgeModelBoundaryLoops(alias, edit.selection.edges, 0, 0, 12, &alias, nullptr, &error) && closed(alias),
		"successful publication can alias the source");
	if (app.arguments()[1] != "--core-only")
	{
		const auto input = QDir(temporary.path()).filePath("input.mesh.json"), target = QDir(temporary.path()).filePath("bridge.mesh.json");
		ModelDocument fixture;
		ok &= expect(fixture.setMesh(tests::bridgeDisks(), &error) && fixture.save(input, false, &error), "write CLI authoring source");
		const auto cli = [&](QStringList args, int code) {
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			args.prepend("--cli");
			args << "--json" << "--settings-file" << QDir(temporary.path()).filePath("settings.ini");
			process.start(app.arguments()[1], args);
			const bool finished = process.waitForStarted(10000) && process.waitForFinished(30000);
			const auto data = process.readAllStandardOutput();
			if (!finished || process.exitCode() != code)
				std::cerr << data.constData() << process.readAllStandardError().constData();
			return finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == code && QJsonDocument::fromJson(data).isObject();
		};
		const QStringList command{"model", "edit", input, "--operation", "bridge-boundary-loops", "--edges", "0:1,4:5", "--output", target};
		ok &= expect(cli(command + QStringList{"--dry-run"}, 0) && !QFileInfo::exists(target), "CLI dry run validates without publication");
		ok &= expect(cli(command, 0) && fixture.load(target, &error) && bytes(fixture.mesh()) == bytes(deterministic), "CLI output matches shared document bridge exactly");
		ok &= expect(cli(command, 1) && cli(command + QStringList{"--overwrite"}, 0), "CLI requires explicit derivative overwrite");
		for (const auto &options : QVector<QStringList>{{"--faces", "0"}, {"--vertices", "0"}, {"--frame", "1"}, {"--bridge-twist", "1.5"},
			{"--bridge-twist", "1024"}, {"--bridge-twist", "1", "--bridge-twist", "2"}, {"--source-frame", "0", "--source-frame", "1"}})
			ok &= expect(cli(command + options, 2), "CLI rejects ambiguous selection, frame scope and repeated or malformed controls");
		ok &= expect(cli(command + QStringList{"--bridge-twist", "1", "--overwrite"}, 0) && fixture.load(target, &error) && bytes(fixture.mesh()) == bytes(twisted),
			"CLI twist matches GUI/core alignment");
		ok &= expect(cli({"model", "edit", input, "--operation", "bridge-boundary-loops", "--output", target}, 2), "CLI requires edge selection");
		ok &= expect(cli({"model", "edit", input, "--operation", "bridge-boundary-loops", "--edges", "0:1", "--output", target, "--overwrite"}, 4),
			"CLI reports geometric selection refusal");
		ok &= expect(cli({"model", "edit", input, "--operation", "fill-boundary-loops", "--edges", "0:1", "--bridge-twist", "0", "--output", target}, 2),
			"CLI rejects twist on unrelated operations");
		ok &= expect(fixture.load(input, &error) && bytes(fixture.mesh()) == bytes(tests::bridgeDisks()), "CLI input remains byte-equivalent after all operations");
		const QStringList inPlace{"model", "edit", input, "--operation", "bridge-boundary-loops", "--edges", "0:1,4:5", "--output", input};
		ok &= expect(cli(inPlace, 1) && fixture.load(input, &error) && bytes(fixture.mesh()) == bytes(tests::bridgeDisks()),
			"in-place authoring requires explicit overwrite");
		ok &= expect(cli(inPlace + QStringList{"--overwrite"}, 0) && fixture.load(input, &error) && bytes(fixture.mesh()) == bytes(deterministic),
			"explicit in-place authoring uses the document's fingerprint-checked save");
	}
	if (!ok)
		std::cerr << error.toStdString() << '\n';
	std::cout << checks << " boundary bridge core/CLI checks\n";
	return ok ? 0 : 1;
}
