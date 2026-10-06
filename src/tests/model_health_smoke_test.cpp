#include "core/model_recovery.h"
#include "core/model_topology_health.h"
#include "tests/model_health_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

using namespace vibestudio;
namespace
{
bool expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return condition;
}
QByteArray bytes(const ModelMesh &mesh) { return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact); }
bool same(ModelVec3 a, ModelVec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
bool attributes(const ModelMesh &before, const ModelMesh &after, const QVector<int> &origins)
{
	const auto &a = before.surfaces[0], &b = after.surfaces[0];
	if (a.skinPaths != b.skinPaths || b.texCoords.size() != origins.size() || a.frames.size() != b.frames.size())
	{
		return false;
	}
	for (int i = 0; i < origins.size(); ++i)
	{
		const int old = origins[i];
		if (a.texCoords[old].u != b.texCoords[i].u || a.texCoords[old].v != b.texCoords[i].v)
		{
			return false;
		}
		for (int f = 0; f < a.frames.size(); ++f)
		{
			if (!same(a.frames[f].positions[old], b.frames[f].positions[i]) || !same(a.frames[f].normals[old], b.frames[f].normals[i]))
			{
				return false;
			}
		}
	}
	return editableModelJson(before).value("tags") == editableModelJson(after).value("tags");
}
} // namespace

int main(int argc, char **argv)
{
#if defined(_MSC_VER) && defined(_DEBUG)
	for (int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT})
	{
		_CrtSetReportMode(type, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
	}
#endif
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath("mesh-health-XXXXXX"));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	bool ok = true;
	QString error;
	const auto original = tests::healthFixture();
	ModelTopologyHealth health;
	ok &= expect(validateEditableModel(original).isEmpty() && inspectModelTopology(original.surfaces[0], &health, &error) &&
					 health.duplicateFaces == QVector<QVector<int>>{{0, 2}} && health.unusedVertices == QVector<int>{6} &&
					 health.nonmanifoldEdges == QVector<ModelEdge>{{0, 2}} && health.boundaryEdges.size() == 5 &&
					 health.faceComponents == 2 && health.disconnectedFans.size() == 1 && health.disconnectedFans[0].vertex == 0 &&
					 health.disconnectedFans[0].faces == QVector<QVector<int>>{{0, 1, 2}, {3}},
				 "report exact indexed topology and disconnected fans");
	ModelDocument document;
	ok &= expect(document.setMesh(original, &error), "open health fixture");
	ModelSelection selected{0, {0, 6}, {2}, {{0, 2}, {0, 4}}};
	document.setSelection(selected);
	const QList kinds{ModelEditKind::RemoveDuplicateFaces, ModelEditKind::RemoveUnusedVertices, ModelEditKind::SplitDisconnectedFans,
					  ModelEditKind::OrientFaces};
	for (auto kind : kinds)
	{
		ModelEdit edit;
		edit.kind = kind;
		edit.selection = document.selection();
		ok &= expect(document.edit(edit, &error) && inspectModelTopology(document.mesh().surfaces[0], &health, &error),
					 "repair through document history");
		if (kind == ModelEditKind::RemoveDuplicateFaces)
		{
			ok &= expect(health.duplicateFaces.isEmpty() && health.nonmanifoldEdges.isEmpty() &&
							 health.windingEdges == QVector<ModelEdge>{{0, 2}} && document.selection().faces == QSet<int>{0} &&
							 document.mesh().vertexCount == 7,
						 "duplicate removal maps selection to survivor without silently compacting vertices");
		}
		if (kind == ModelEditKind::RemoveUnusedVertices)
		{
			ok &=
				expect(health.unusedVertices.isEmpty() && document.mesh().vertexCount == 6 && document.selection().vertices == QSet<int>{0},
					   "remove unused samples and selection");
		}
		if (kind == ModelEditKind::SplitDisconnectedFans)
		{
			ok &= expect(health.disconnectedFans.isEmpty() && document.mesh().vertexCount == 7 &&
							 document.selection().vertices == QSet<int>{0, 6} &&
							 document.selection().edges == QSet<ModelEdge>{{0, 2}, {4, 6}} &&
							 document.mesh().surfaces[0].uvSeams == QSet<ModelEdge>{{0, 2}, {4, 6}},
						 "fan splitting remaps all vertex copies, selected edges and seam marks");
		}
	}
	ok &= expect(health.windingEdges.isEmpty() && health.faceComponents == 2 && health.boundaryEdges.size() == 7 &&
					 attributes(original, document.mesh(), {0, 1, 2, 3, 4, 5, 0}),
				 "repair sequence preserves poses, authored normals, UVs, materials and attachments");
	const auto repaired = bytes(document.mesh());
	const auto &oriented = document.mesh().surfaces[0].triangles;
	ok &= expect(oriented[0].a == 0 && oriented[0].b == 1 && oriented[0].c == 2 && oriented[1].a == 0 && oriented[1].b == 2 &&
					 oriented[1].c == 3 && oriented[2].a == 6 && oriented[2].b == 4 && oriented[2].c == 5,
				 "orientation keeps each component seed and flips only the required neighbour");
	for (int i = 0; i < kinds.size(); ++i)
	{
		ok &= expect(document.undo(), "undo each repair");
	}
	ok &= expect(bytes(document.mesh()) == bytes(original) && document.selection().vertices == selected.vertices &&
					 document.selection().edges == selected.edges && document.selection().faces == selected.faces,
				 "undo restores exact source and original mixed selection");
	for (int i = 0; i < kinds.size(); ++i)
	{
		ok &= expect(document.redo(), "redo each repair");
	}
	ok &= expect(bytes(document.mesh()) == repaired, "redo restores exact repaired source");
	ModelRecoverySnapshot snapshot{document.mesh(), document.selection(), 1, "Health repair", {}, {}};
	const auto recovery = writeModelRecovery(snapshot, temporary.path(), "8ee70bf5-af99-4eb4-bf07-05724bd3ef23", &error);
	ModelRecoverySnapshot restored;
	ok &= expect(!recovery.isEmpty() && inspectModelRecovery(recovery, &restored).isValid() && bytes(restored.mesh) == repaired &&
					 restored.selection.vertices == snapshot.selection.vertices && restored.selection.edges == snapshot.selection.edges,
				 "repair and remapped selection survive recovery");
	ModelMesh decoded;
	ok &= expect(parseEditableModel(repaired, &decoded, &error) && bytes(decoded) == repaired, "repaired editable source round-trips");
	const auto md3 = exportEditableModel(document.mesh(), "md3", 0, &error);
	ok &= expect(!md3.isEmpty() && importEditableModel("health.md3", md3, &decoded, &error) && decoded.frameCount == 2 &&
					 decoded.tagCount == 1,
				 "repaired geometry exports and imports as animated MD3 with attachment");
	ModelEdit orient;
	orient.kind = ModelEditKind::OrientFaces;
	for (auto kind : kinds)
	{
		auto mesh = document.mesh();
		ModelEdit edit;
		edit.kind = kind;
		ok &= expect(!applyModelEdit(&mesh, edit, nullptr, &error) && bytes(mesh) == repaired,
					 "repair with no matching findings is a nonmutating refusal");
	}
	auto compacted = tests::uvSquare();
	auto &sparse = compacted.surfaces[0];
	sparse.texCoords.insert(1, {13, 14});
	for (auto &frame : sparse.frames)
	{
		frame.positions.insert(1, {99, 98, 97});
		frame.normals.insert(1, {1, 0, 0});
	}
	sparse.triangles = {{0, 2, 3}, {0, 3, 4}};
	sparse.uvSeams = {{0, 3}};
	updateEditableModelMetadata(&compacted);
	ModelEdit compactEdit;
	compactEdit.kind = ModelEditKind::RemoveUnusedVertices;
	compactEdit.selection = {0, {1, 4}, {1}, {{0, 3}, {3, 4}}};
	ModelSelection compactSelection;
	ok &= expect(applyModelEdit(&compacted, compactEdit, &compactSelection, &error) && compacted.vertexCount == 4 &&
					 compactSelection.vertices == QSet<int>{3} && compactSelection.edges == QSet<ModelEdge>{{0, 2}, {2, 3}} &&
					 compacted.surfaces[0].uvSeams == QSet<ModelEdge>{{0, 2}} && attributes(tests::uvSquare(), compacted, {0, 1, 2, 3}),
				 "middle-index compaction remaps selection and seams while preserving all attributes");
	auto malformed = original;
	ok &= expect(!applyModelEdit(&malformed, orient, nullptr, &error) && bytes(malformed) == bytes(original),
				 "orientation refuses duplicates or branching edges atomically");
	malformed = tests::uvSquare();
	// A triangulated three-segment strip whose final pair is connected with a twist.
	auto &strip = malformed.surfaces[0];
	strip.triangles = {{0, 2, 3}, {0, 3, 1}, {2, 4, 5}, {2, 5, 3}, {4, 1, 0}, {4, 0, 5}};
	strip.texCoords << ModelTexCoord{2, 0} << ModelTexCoord{2, 1};
	for (auto &frame : strip.frames)
	{
		frame.positions << ModelVec3{20, 3, 4} << ModelVec3{20, 8, 2};
		frame.normals << ModelVec3{0, 0, 1} << ModelVec3{0, 0, 1};
	}
	updateEditableModelMetadata(&malformed);
	const auto twisted = bytes(malformed);
	ok &= expect(!twisted.isEmpty() && !applyModelEdit(&malformed, orient, nullptr, &error) && error.contains("cannot be oriented") &&
					 bytes(malformed) == twisted,
				 "nonorientable component fails without partial face flips");
	ModelSurface sentinel = tests::uvSquare().surfaces[0], result = sentinel;
	QVector<QVector<int>> vertices{{77}};
	QVector<int> faces{88};
	ok &= expect(
		!repairModelTopology(original.surfaces[0], ModelTopologyRepair::SplitDisconnectedFans, 7, &result, &vertices, &faces, &error) &&
			result.vertexCount == sentinel.vertexCount && vertices == QVector<QVector<int>>{{77}} && faces == QVector<int>{88},
		"fan vertex budget refusal leaves all outputs untouched");
	ModelWorkControl cancelled;
	cancelled.cancelled = [] { return true; };
	health.faceComponents = 99;
	ok &= expect(!inspectModelTopology(original.surfaces[0], &health, &error, cancelled) && health.faceComponents == 99 &&
					 !repairModelTopology(original.surfaces[0], ModelTopologyRepair::RemoveDuplicateFaces, 65536, &result, &vertices,
										  &faces, &error, cancelled) &&
					 vertices == QVector<QVector<int>>{{77}},
				 "inspection and repair cancellation preserve previous results");
	for (auto kind : kinds)
	{
		auto mesh = original;
		if (kind == ModelEditKind::OrientFaces)
		{
			mesh = tests::uvSquare();
			std::swap(mesh.surfaces[0].triangles[1].b, mesh.surfaces[0].triangles[1].c);
		}
		const auto before = bytes(mesh);
		ModelEdit edit;
		edit.kind = kind;
		bool stop = false;
		ModelWorkControl control;
		control.cancelled = [&] { return stop; };
		control.progress = [&](ModelWorkPhase phase, qint64 done, qint64)
		{
			if (phase == ModelWorkPhase::Editing && done > 0)
			{
				stop = true;
			}
		};
		ok &= expect(!applyModelEdit(&mesh, edit, nullptr, &error, control) && stop && bytes(mesh) == before,
					 "cancelled repair does not mutate any pose");
	}
	// High incidence stays linear: do not compare every pair in a crowded edge/fan.
	auto crowded = original.surfaces[0];
	crowded.triangles.fill({0, 1, 2}, 131072);
	QElapsedTimer elapsed;
	elapsed.start();
	ok &= expect(inspectModelTopology(crowded, &health, &error) && health.duplicateFaces.size() == 1 &&
					 health.duplicateFaces[0].size() == 131072 && health.nonmanifoldEdges.size() == 3 && health.disconnectedFans.isEmpty(),
				 "maximum triangle-count crowded incidence is bounded and deterministic");
	std::cout << "131072-face indexed topology inspection: " << elapsed.elapsed() << " ms\n";
	const auto grid = tests::uvGrid(254);
	elapsed.restart();
	ok &= expect(inspectModelTopology(grid, &health, &error) && health.faceComponents == 1 && health.boundaryEdges.size() == 1016 &&
					 health.unusedVertices.isEmpty() && health.duplicateFaces.isEmpty() && health.nonmanifoldEdges.isEmpty() &&
					 health.disconnectedFans.isEmpty() && health.windingEdges.isEmpty(),
				 "large distinct-edge surface has no false health findings");
	std::cout << "129032-face 65025-vertex grid inspection: " << elapsed.elapsed() << " ms\n";
	bool stop = false;
	ModelWorkControl during;
	during.cancelled = [&] { return stop; };
	during.progress = [&](ModelWorkPhase, qint64 done, qint64)
	{
		if (done >= 1024)
		{
			stop = true;
		}
	};
	health.faceComponents = 97;
	ok &= expect(!inspectModelTopology(crowded, &health, &error, during) && stop && health.faceComponents == 97,
				 "dense incidence inspection polls cancellation before publishing");
	if (app.arguments().size() > 1)
	{
		const auto path = QDir(temporary.path()).filePath("health.mesh.json"),
				   output = QDir(temporary.path()).filePath("repaired.mesh.json");
		ModelDocument input;
		ok &= expect(input.setMesh(original, &error) && input.save(path, false, &error), "write CLI fixture");
		const auto cli = [&](QStringList args, int expected, QJsonObject *json = nullptr)
		{
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			args.prepend("--cli");
			args << "--json" << "--settings-file" << QDir(temporary.path()).filePath("settings.ini");
			process.start(app.arguments()[1], args);
			const bool finished = process.waitForStarted(10000) && process.waitForFinished(30000);
			const auto data = process.readAllStandardOutput();
			const auto parsed = QJsonDocument::fromJson(data);
			if (!finished || process.exitCode() != expected)
			{
				std::cerr << data.constData() << process.readAllStandardError().constData();
			}
			if (json)
			{
				*json = parsed.object();
			}
			return finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected && parsed.isObject();
		};
		QJsonObject report;
		ok &= expect(cli({"model", "topology", path}, 0, &report) &&
						 report["health"].toObject()["unusedVertices"].toArray() == QJsonArray{6} &&
						 report["health"].toObject()["disconnectedFans"].toArray().size() == 1,
					 "CLI publishes exact actionable health indices");
		ok &= expect(cli({"model", "edit", path, "--operation", "remove-duplicate-faces", "--output", output, "--dry-run"}, 0) &&
						 !QFileInfo::exists(output),
					 "repair dry-run does not write");
		const QStringList operations{"remove-duplicate-faces", "remove-unused-vertices", "split-disconnected-fans", "orient-faces"};
		QString current = path;
		for (int i = 0; i < operations.size(); ++i)
		{
			const auto target = QDir(temporary.path()).filePath(QString("repaired-%1.mesh.json").arg(i));
			ok &=
				expect(cli({"model", "edit", current, "--operation", operations[i], "--output", target}, 0), "CLI executes surface repair");
			current = target;
		}
		ok &= expect(input.load(current, &error) && bytes(input.mesh()) == repaired, "CLI repair sequence matches GUI/core source exactly");
		ok &= expect(cli({"model", "edit", path, "--operation", "remove-duplicate-faces", "--output", current}, 1),
					 "repair output retains overwrite protection");
		ok &= expect(cli({"model", "edit", path, "--operation", "orient-faces", "--output", output}, 4) && !QFileInfo::exists(output),
					 "CLI invalid repair remains atomic");
		ok &= expect(cli({"model", "edit", path, "--operation", "remove-duplicate-faces", "--faces", "2", "--output", output}, 2) &&
						 cli({"model", "edit", path, "--operation", "remove-duplicate-faces", "--frame", "0", "--output", output}, 2) &&
						 !QFileInfo::exists(output),
					 "CLI refuses ambiguous partial-surface or single-frame repair scope");
	}
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
