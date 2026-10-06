#include "core/model_fingerprint.h"
#include "core/model_recovery.h"
#include "core/model_topology_health.h"
#include "tests/model_nonmanifold_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>

#include <array>
#include <iostream>
#include <random>

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
QByteArray bytes(const ModelMesh &mesh)
{
	return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact);
}
std::array<int, 3> corners(ModelTriangle t)
{
	return {t.a, t.b, t.c};
}
bool same(ModelVec3 a, ModelVec3 b)
{
	return a.x == b.x && a.y == b.y && a.z == b.z;
}
bool preservedCorners(const ModelSurface &before, const ModelSurface &after)
{
	if (before.triangles.size() != after.triangles.size() || before.frames.size() != after.frames.size() ||
		before.skinPaths != after.skinPaths)
		return false;
	for (int face = 0; face < before.triangles.size(); ++face)
	{
		const auto old = corners(before.triangles[face]), now = corners(after.triangles[face]);
		for (int c = 0; c < 3; ++c)
		{
			if (before.texCoords[old[c]].u != after.texCoords[now[c]].u || before.texCoords[old[c]].v != after.texCoords[now[c]].v)
				return false;
			for (int frame = 0; frame < before.frames.size(); ++frame)
				if (!same(before.frames[frame].positions[old[c]], after.frames[frame].positions[now[c]]) ||
					!same(before.frames[frame].normals[old[c]], after.frames[frame].normals[now[c]]))
					return false;
		}
	}
	return true;
}
bool manifold(const ModelSurface &surface)
{
	ModelTopologyHealth health;
	return inspectModelTopology(surface, &health) && health.nonmanifoldEdges.isEmpty();
}
bool unambiguousEdgesPreserved(const ModelSurface &before, const ModelSurface &after)
{
	ModelTopologyHealth health;
	if (!inspectModelTopology(before, &health))
		return false;
	for (const auto &edge : health.edges)
	{
		if (edge.faces.size() != 2)
			continue;
		ModelEdge mapped[2];
		for (int i = 0; i < 2; ++i)
		{
			const auto old = corners(before.triangles[edge.faces[i]]), now = corners(after.triangles[edge.faces[i]]);
			int a = -1, b = -1;
			for (int c = 0; c < 3; ++c)
			{
				if (old[c] == edge.vertices.first)
					a = now[c];
				if (old[c] == edge.vertices.second)
					b = now[c];
			}
			mapped[i] = modelEdge(a, b);
		}
		if (mapped[0] != mapped[1])
			return false;
	}
	return true;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root) || argc != 2)
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("nonmanifold-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	bool ok = true;
	QString error;
	auto original = tests::nonmanifoldBook();
	for (auto &frame : original.surfaces[0].frames)
		for (int v = 0; v < frame.normals.size(); ++v)
			frame.normals[v] = {.1f + v * .03f, .2f, .9f};
	original.surfaces << original.surfaces[0];
	original.surfaces[1].name = QStringLiteral("untouched");
	updateEditableModelMetadata(&original);
	ok &= expect(validateEditableModel(original).isEmpty(), "original animated branched surface is editable");
	ModelTopologyHealth health;
	ok &= expect(inspectModelTopology(original.surfaces[0], &health, &error) && health.nonmanifoldEdges == QVector<ModelEdge>{{0, 1}},
				 "exact branching spine is reported");
	ModelDocument document;
	ok &= expect(document.setMesh(original, &error), "open branching document");
	ModelSelection selected{0, {0, 5}, {1, 2}, {{0, 1}, {1, 4}}};
	document.setSelection(selected);
	ModelEdit cut;
	cut.kind = ModelEditKind::SplitNonmanifoldEdges;
	cut.selection = selected;
	ok &= expect(document.edit(cut, &error), "split branching spine through shared document");
	const auto repaired = bytes(document.mesh());
	const auto &surface = document.mesh().surfaces[0];
	ok &= expect(surface.vertexCount == 10 && surface.triangles.size() == 3 && manifold(surface),
				 "every page keeps independent spine endpoints");
	ok &= expect(corners(surface.triangles[0]) == std::array{0, 1, 2} && corners(surface.triangles[1]) == std::array{8, 6, 3} &&
					 corners(surface.triangles[2]) == std::array{7, 9, 4},
				 "lowest face keeps original indices and copies have deterministic order");
	ok &= expect(preservedCorners(original.surfaces[0], surface), "all poses, authored normals, UVs and face winding remain exact");
	ok &= expect(surface.uvSeams == QSet<ModelEdge>{{0, 1}, {6, 8}, {7, 9}, {0, 2}, {3, 6}},
				 "seams follow actual face copies without cross-products");
	ok &= expect(document.selection().vertices == QSet<int>{0, 5, 6, 7} && document.selection().faces == selected.faces &&
					 document.selection().edges == QSet<ModelEdge>{{0, 1}, {6, 8}, {7, 9}, {4, 9}},
				 "mixed selection expands to every surviving copy");
	auto untouched = document.mesh();
	untouched.surfaces[0] = original.surfaces[0];
	updateEditableModelMetadata(&untouched);
	ok &= expect(bytes(untouched) == bytes(original), "other surface, tags, clip FPS and metadata are untouched");
	ok &= expect(document.undo() && bytes(document.mesh()) == bytes(original) && document.selection() == selected && !document.canUndo(),
				 "single undo restores source and selection exactly");
	ok &= expect(document.redo() && bytes(document.mesh()) == repaired, "redo restores exact deterministic split");
	ModelRecoverySnapshot snapshot{document.mesh(), document.selection(), 1, "Nonmanifold repair", {}, {}};
	const auto recovery = writeModelRecovery(snapshot, temporary.path(), "0d8fda2b-fad5-433a-9451-edf77f21bbd0", &error);
	ModelRecoverySnapshot restored;
	ok &= expect(!recovery.isEmpty() && inspectModelRecovery(recovery, &restored).isValid() && bytes(restored.mesh) == repaired &&
					 restored.selection == snapshot.selection,
				 "recovery preserves repaired topology and expanded selection");
	ModelMesh decoded;
	ok &= expect(parseEditableModel(repaired, &decoded, &error) && bytes(decoded) == repaired, "editable source round trip preserves cuts");
	const auto md3 = exportEditableModel(document.mesh(), "md3", 0, &error);
	ok &= expect(!md3.isEmpty() && importEditableModel("repair.md3", md3, &decoded, &error) && manifold(decoded.surfaces[0]) &&
					 decoded.tagCount == original.tagCount && decoded.frameCount == 2,
				 "MD3 retains cuts, every pose and attachment tags");
	auto alias = document.mesh();
	alias.surfaces.removeLast();
	alias.tags.clear();
	updateEditableModelMetadata(&alias);
	const auto md2 = exportEditableModel(alias, "md2", 0, &error);
	ok &= expect(!md2.isEmpty() && importEditableModel("repair.md2", md2, &decoded, &error) && manifold(decoded.surfaces[0]) &&
					 decoded.frameCount == 2 && decoded.triangleCount == 3,
				 "MD2 retains split corner identities despite shared XYZ storage");
	ok &= expect(!document.edit(cut, &error) && bytes(document.mesh()) == repaired, "no findings is a nonmutating refusal");
	cut.frame = 0;
	auto invalid = original;
	ok &= expect(!applyModelEdit(&invalid, cut, nullptr, &error) && bytes(invalid) == bytes(original),
				 "single-pose repair refuses atomically");
	cut.frame = -1;
	// A closed tetrahedron and a fin share an overfull edge. The tetrahedron's
	// corners reconnect through other manifold edges; preserve that entire shell.
	auto shell = tests::nonmanifoldBook();
	auto &tetra = shell.surfaces[0];
	tetra.triangles = {{0, 1, 2}, {1, 0, 3}, {0, 2, 3}, {1, 3, 2}, {0, 1, 4}};
	updateEditableModelMetadata(&shell);
	const auto shellBefore = shell;
	cut.selection = {};
	ok &= expect(applyModelEdit(&shell, cut, nullptr, &error) && shell.vertexCount == 8 && manifold(shell.surfaces[0]) &&
					 preservedCorners(shellBefore.surfaces[0], shell.surfaces[0]) &&
					 unambiguousEdgesPreserved(shellBefore.surfaces[0], shell.surfaces[0]),
				 "reconnected shell retains all two-face adjacencies while the extra fin splits away");
	for (int face = 0; face < 4; ++face)
		ok &= expect(corners(shell.surfaces[0].triangles[face]) == corners(shellBefore.surfaces[0].triangles[face]),
					 "unambiguous shell face keeps indices");
	// Duplicate faces and disconnected fans elsewhere are separate author intent.
	auto duplicates = tests::nonmanifoldBook();
	duplicates.surfaces[0].triangles << duplicates.surfaces[0].triangles[0];
	updateEditableModelMetadata(&duplicates);
	const auto duplicateBefore = duplicates;
	ok &= expect(applyModelEdit(&duplicates, cut, nullptr, &error) && duplicates.triangleCount == 4 && manifold(duplicates.surfaces[0]) &&
					 preservedCorners(duplicateBefore.surfaces[0], duplicates.surfaces[0]),
				 "splitting never deletes duplicate or reversed faces");
	auto separateFan = tests::nonmanifoldBook();
	auto &fanSurface = separateFan.surfaces[0];
	fanSurface.triangles << ModelTriangle{5, 6, 7} << ModelTriangle{5, 8, 9};
	fanSurface.texCoords << ModelTexCoord{0, 0} << ModelTexCoord{1, 0} << ModelTexCoord{0, 1} << ModelTexCoord{1, 1};
	for (auto &pose : fanSurface.frames)
	{
		const auto centre = pose.positions[5];
		for (auto offset : {ModelVec3{1, 0, 0}, ModelVec3{0, 1, 0}, ModelVec3{-1, 0, 0}, ModelVec3{0, -1, 0}})
		{
			pose.positions << ModelVec3{centre.x + offset.x, centre.y + offset.y, centre.z};
			pose.normals << ModelVec3{0, 0, 1};
		}
	}
	updateEditableModelMetadata(&separateFan);
	ok &= expect(applyModelEdit(&separateFan, cut, nullptr, &error) && inspectModelTopology(separateFan.surfaces[0], &health, &error) &&
					 health.nonmanifoldEdges.isEmpty() && health.disconnectedFans.size() == 1 && health.disconnectedFans[0].vertex == 5 &&
					 corners(separateFan.surfaces[0].triangles[3]) == std::array{5, 6, 7} &&
					 corners(separateFan.surfaces[0].triangles[4]) == std::array{5, 8, 9},
				 "unrelated disconnected fan remains indexed exactly as authored");
	auto clean = tests::uvSquare();
	const auto cleanBytes = bytes(clean);
	ok &= expect(!applyModelEdit(&clean, cut, nullptr, &error) && bytes(clean) == cleanBytes,
				 "ordinary boundary and manifold edges remain unchanged");
	// Random indexed complexes exercise interacting branch edges and reconnecting
	// paths. The invariant oracle checks incidence and all original two-face edges.
	std::mt19937 random(17061);
	for (int scenario = 0; scenario < 24; ++scenario)
	{
		auto mesh = tests::nonmanifoldBook(18);
		auto &s = mesh.surfaces[0];
		for (int face = 0; face < 70; ++face)
		{
			int a = int(random() % 20), b = int(random() % 20), c = int(random() % 20);
			if (a == b || b == c || c == a)
			{
				--face;
				continue;
			}
			s.triangles << ModelTriangle{a, b, c};
		}
		ModelSurface result;
		QVector<QVector<int>> vertices;
		QVector<int> faces;
		ok &= expect(repairModelTopology(s, ModelTopologyRepair::SplitNonmanifoldEdges, 65536, &result, &vertices, &faces, &error) &&
						 manifold(result) && preservedCorners(s, result) && unambiguousEdgesPreserved(s, result) &&
						 faces.size() == s.triangles.size(),
					 "interacting branch-edge complex preserves all corners and manifold adjacencies");
	}
	ModelSurface result = tests::uvSquare().surfaces[0];
	QVector<QVector<int>> vertices{{77}};
	QVector<int> faces{88};
	const auto sentinel = result.frames[0].positions[0];
	const auto base = tests::nonmanifoldBook();
	ok &=
		expect(!repairModelTopology(base.surfaces[0], ModelTopologyRepair::SplitNonmanifoldEdges, 9, &result, &vertices, &faces, &error) &&
				   result.vertexCount == 4 && same(result.frames[0].positions[0], sentinel) && vertices == QVector<QVector<int>>{{77}} &&
				   faces == QVector<int>{88},
			   "insufficient aggregate vertex budget preserves every output");
	const auto crowded = tests::nonmanifoldBook(2000, 3, false);
	for (qint64 stopAt : {0, 2048, 7000, 25000})
	{
		bool stop = false;
		ModelWorkControl control;
		control.cancelled = [&] { return stop; };
		control.progress = [&](ModelWorkPhase phase, qint64 done, qint64) {
			if (phase == ModelWorkPhase::Editing && done >= stopAt)
				stop = true;
		};
		ok &= expect(!repairModelTopology(crowded.surfaces[0], ModelTopologyRepair::SplitNonmanifoldEdges, 65536, &result, &vertices,
										  &faces, &error, control) &&
						 stop && result.vertexCount == 4 && vertices == QVector<QVector<int>>{{77}} && faces == QVector<int>{88},
					 "cancellation during planning or all-pose copying leaves result and maps unchanged");
	}
	bool copyingStarted = false, postScanStopped = false;
	ModelWorkControl postScan;
	postScan.cancelled = [&] { return postScanStopped; };
	postScan.progress = [&](ModelWorkPhase phase, qint64 done, qint64) {
		if (phase == ModelWorkPhase::Editing && done >= 16000)
			copyingStarted = true;
		if (copyingStarted && phase == ModelWorkPhase::Validating && done >= 256)
			postScanStopped = true;
	};
	ok &= expect(!repairModelTopology(crowded.surfaces[0], ModelTopologyRepair::SplitNonmanifoldEdges, 65536, &result, &vertices, &faces,
									  &error, postScan) &&
					 copyingStarted && postScanStopped && result.vertexCount == 4 && vertices == QVector<QVector<int>>{{77}} &&
					 faces == QVector<int>{88},
				 "cancellation during final incidence validation publishes no candidate");
	const auto maximum = tests::nonmanifoldBook(21845, 16, false);
	QElapsedTimer elapsed;
	elapsed.start();
	ok &= expect(
		repairModelTopology(maximum.surfaces[0], ModelTopologyRepair::SplitNonmanifoldEdges, 65536, &result, &vertices, &faces, &error) &&
			result.vertexCount == 65535 && manifold(result) && preservedCorners(maximum.surfaces[0], result),
		"65535-vertex sixteen-pose split fits the frame-vertex ceiling without truncation");
	std::cout << "21845-page sixteen-pose edge split: " << elapsed.elapsed() << " ms, " << result.vertexCount * 16 << " frame vertices\n";
	const auto tooMany = tests::nonmanifoldBook(21846, 16, false);
	ok &= expect(
		!repairModelTopology(tooMany.surfaces[0], ModelTopologyRepair::SplitNonmanifoldEdges, 65536, &result, &vertices, &faces, &error) &&
			result.vertexCount == 65535,
		"one extra page refuses before publishing an oversized animated surface");
	const auto frameOverflow = tests::nonmanifoldBook(20561, 17, false);
	ok &= expect(!repairModelTopology(frameOverflow.surfaces[0], ModelTopologyRepair::SplitNonmanifoldEdges, 65536, &result, &vertices,
									  &faces, &error) &&
					 result.vertexCount == 65535,
				 "animation storage refusal also applies below the vertex-count ceiling");
	auto aggregate = maximum;
	ModelSurface extra;
	extra.name = QStringLiteral("reserved_budget");
	extra.triangles = {{0, 1, 2}};
	extra.texCoords = {{0, 0}, {1, 0}, {.5f, 1}};
	for (const auto &pose : maximum.surfaces[0].frames)
		extra.frames << ModelFrameGeometry{pose.positions.mid(0, 3), pose.normals.mid(0, 3)};
	aggregate.surfaces << extra;
	updateEditableModelMetadata(&aggregate);
	const auto aggregateBefore = modelStateFingerprint(aggregate);
	ok &= expect(!aggregateBefore.isEmpty() && validateEditableModel(aggregate).isEmpty() &&
					 !applyModelEdit(&aggregate, cut, nullptr, &error) && modelStateFingerprint(aggregate) == aggregateBefore,
				 "other surfaces reserve their share of the complete document vertex and frame budget");
	const auto inputPath = QDir(temporary.path()).filePath("book.mesh.json"),
			   outputPath = QDir(temporary.path()).filePath("repaired.mesh.json");
	ModelDocument input;
	ok &= expect(input.setMesh(original, &error) && input.save(inputPath, false, &error), "write CLI source fixture");
	const auto cli = [&](QStringList args, int expected, QJsonObject *json = nullptr) {
		QProcess process;
		process.setWorkingDirectory(temporary.path());
		args.prepend("--cli");
		args << "--json" << "--settings-file" << QDir(temporary.path()).filePath("settings.ini");
		process.start(app.arguments()[1], args);
		const bool finished = process.waitForStarted(10000) && process.waitForFinished(30000);
		const auto output = process.readAllStandardOutput();
		const auto parsed = QJsonDocument::fromJson(output);
		if (!finished || process.exitCode() != expected)
			std::cerr << output.constData() << process.readAllStandardError().constData();
		if (json)
			*json = parsed.object();
		return finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected && parsed.isObject();
	};
	const QStringList command{"model", "edit", inputPath, "--operation", "split-nonmanifold-edges", "--output", outputPath};
	ok &= expect(cli(command + QStringList{"--dry-run"}, 0) && !QFileInfo::exists(outputPath), "CLI dry-run validates without output");
	ok &= expect(cli(command, 0) && input.load(outputPath, &error) && bytes(input.mesh()) == repaired,
				 "CLI and shared document produce identical mesh sources");
	QJsonObject report;
	ok &= expect(cli({"model", "topology", outputPath}, 0, &report) && report["health"].toObject()["nonmanifoldEdges"].toArray().isEmpty(),
				 "CLI health report confirms the repaired surface");
	for (const QStringList &scope : {QStringList{"--frame", "0"}, QStringList{"--frame=1"}, QStringList{"--vertices", "all"},
									 QStringList{"--faces", "all"}, QStringList{"--edges", "0:1"}})
		ok &= expect(cli(command + scope, 2), "CLI refuses component or single-pose repair scope");
	ok &= expect(cli(command, 1), "CLI refuses accidental output overwrite");
	ok &= expect(
		cli({"model", "edit", inputPath, "--operation=split-nonmanifold-edges", "--frame=all", "--output", outputPath, "--overwrite"}, 0),
		"inline CLI operation and explicit all-frame scope match the spaced command");
	ok &= expect(cli({"model", "edit", outputPath, "--operation", "split-nonmanifold-edges", "--output", inputPath, "--overwrite"}, 4),
				 "no findings fails before touching another output");
	ModelDocument unchanged;
	ok &= expect(unchanged.load(inputPath, &error) && bytes(unchanged.mesh()) == bytes(original),
				 "source remains byte-equivalent after every CLI path");
	if (!ok)
		std::cerr << error.toStdString() << '\n';
	std::cout << checks << " nonmanifold core/CLI checks\n";
	return ok ? 0 : 1;
}
