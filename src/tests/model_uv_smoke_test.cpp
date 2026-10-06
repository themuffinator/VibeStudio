#include "core/model_recovery.h"
#include "tests/model_uv_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

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
QByteArray source(const ModelMesh &mesh) { return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact); }
bool equal(ModelTexCoord a, ModelTexCoord b) { return std::abs(a.u - b.u) < 1e-5 && std::abs(a.v - b.v) < 1e-5; }
bool equal(ModelVec3 a, ModelVec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
bool samePoses(const ModelMesh &a, const ModelMesh &b)
{
	if (a.frames.size() != b.frames.size() || a.surfaces[0].triangles.size() != b.surfaces[0].triangles.size())
	{
		return false;
	}
	const auto &sa = a.surfaces[0], &sb = b.surfaces[0];
	for (int frame = 0; frame < a.frames.size(); ++frame)
	{
		for (int face = 0; face < sa.triangles.size(); ++face)
		{
			const auto ta = sa.triangles[face], tb = sb.triangles[face];
			const int ia[]{ta.a, ta.b, ta.c}, ib[]{tb.a, tb.b, tb.c};
			for (int i = 0; i < 3; ++i)
			{
				if (!equal(sa.frames[frame].positions[ia[i]], sb.frames[frame].positions[ib[i]]) ||
					!equal(sa.frames[frame].normals[ia[i]], sb.frames[frame].normals[ib[i]]))
				{
					return false;
				}
			}
		}
	}
	return true;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true;
	QString error;
	const auto original = tests::uvSquare();
	auto mesh = original;
	ModelUvTopology topology;
	ok &= expect(buildModelUvTopology(mesh.surfaces[0], &topology, &error) && topology.islands.size() == 1 &&
					 topology.islands[0].faces == QVector<int>{0, 1} && topology.islands[0].vertices == QVector<int>{0, 1, 2, 3} &&
					 topology.edges.size() == 5 && equal(topology.islands[0].mins, {0, 0}) && equal(topology.islands[0].maxs, {1, 1}),
				 "shared indexed edge connects one deterministic UV island");
	ModelEdit mark;
	mark.kind = ModelEditKind::MarkUvSeams;
	mark.selection.edges = {{0, 2}};
	ModelDocument document;
	ok &= expect(document.setMesh(mesh, &error) && document.edit(mark, &error) && document.isModified() &&
					 document.mesh().surfaces[0].uvSeams == QSet<ModelEdge>{{0, 2}},
				 "mark seam through normal document history");
	mesh = document.mesh();
	ok &= expect(samePoses(mesh, original) && buildModelUvTopology(mesh.surfaces[0], &topology, &error) &&
					 topology.faceIsland == QVector<int>{0, 1},
				 "marked seam separates islands without changing poses");
	ok &= expect(document.undo() && !document.isModified() && document.mesh().surfaces[0].uvSeams.isEmpty() && document.redo(),
				 "seam undo and redo restore clean state and marks");
	const auto marked = source(mesh);
	ModelMesh parsed;
	ok &= expect(editableModelJson(mesh).value("version").toInt() == 3 && parseEditableModel(marked, &parsed, &error) &&
					 source(parsed) == marked,
				 "current schema round-trips seam metadata and animation");
	auto versionOne = editableModelJson(original);
	versionOne.insert("version", 1);
	versionOne.remove("md2SkinSize");
	auto surfaces = versionOne.value("surfaces").toArray();
	auto surfaceObject = surfaces[0].toObject();
	surfaceObject.remove("uvSeams");
	surfaces[0] = surfaceObject;
	versionOne.insert("surfaces", surfaces);
	ok &= expect(parseEditableModel(QJsonDocument(versionOne).toJson(), &parsed, &error) && parsed.surfaces[0].uvSeams.isEmpty(),
				 "schema one sources remain readable");
	for (const QJsonValue &bad : QList<QJsonValue>{
			 QJsonValue(), QStringLiteral("invalid"), QJsonArray{QJsonArray{2, 0}}, QJsonArray{QJsonArray{0, 2}, QJsonArray{0, 2}},
			 QJsonArray{QJsonArray{0, 2.5}}, QJsonArray{QJsonArray{1, 3}}, QJsonArray{QJsonArray{0, 99}}, QJsonArray{QJsonArray{0, 0}}})
	{
		auto invalid = editableModelJson(mesh);
		auto items = invalid.value("surfaces").toArray();
		auto item = items[0].toObject();
		item.insert("uvSeams", bad);
		items[0] = item;
		invalid.insert("surfaces", items);
		const auto before = source(parsed);
		ok &= expect(!parseEditableModel(QJsonDocument(invalid).toJson(), &parsed, &error) && source(parsed) == before && !error.isEmpty(),
					 "malformed seam data fails atomically");
	}
	auto downgrade = editableModelJson(mesh);
	downgrade.insert("version", 1);
	downgrade.remove("md2SkinSize");
	ok &= expect(!parseEditableModel(QJsonDocument(downgrade).toJson(), &parsed, &error),
				 "version one cannot silently carry new seam metadata");
	QSet<int> expanded;
	ok &= expect(expandModelUvIslands(mesh.surfaces[0], topology, {0}, {}, {}, &expanded, &error) && expanded == QSet<int>{0},
				 "face expands within marked island");
	ok &= expect(expandModelUvIslands(mesh.surfaces[0], topology, {}, {0}, {}, &expanded, &error) && expanded == QSet<int>{0, 1},
				 "shared vertex seeds both touched islands");
	ok &= expect(expandModelUvIslands(mesh.surfaces[0], topology, {}, {}, {{0, 2}}, &expanded, &error) && expanded == QSet<int>{0, 1},
				 "seam edge seeds both incident islands");
	ok &= expect(!expandModelUvIslands(mesh.surfaces[0], topology, {}, {}, {{1, 3}}, &expanded, &error) && expanded == QSet<int>{0, 1},
				 "nonexistent island seed edge is refused atomically");
	auto unusual = original.surfaces[0];
	unusual.triangles = {{0, 1, 2}, {0, 3, 4}};
	unusual.texCoords.append({-1, 0});
	unusual.vertexCount = 5;
	ok &= expect(buildModelUvTopology(unusual, &topology, &error) && topology.islands.size() == 2,
				 "point-only contact does not join UV charts");
	unusual.triangles = {{0, 1, 2}, {0, 2, 3}, {0, 2, 4}};
	ok &= expect(buildModelUvTopology(unusual, &topology, &error) && topology.islands.size() == 3,
				 "nonmanifold edge does not bridge islands");
	unusual = original.surfaces[0];
	unusual.texCoords.append({0, 0});
	unusual.texCoords.append({1, 1});
	unusual.vertexCount = 6;
	unusual.triangles[1] = {4, 5, 3};
	ok &= expect(buildModelUvTopology(unusual, &topology, &error) && topology.islands.size() == 2,
				 "coincident but split indices stay separate");
	int checks = 0;
	ModelWorkControl cancelled;
	cancelled.cancelled = [&] { return ++checks > 2; };
	topology.faceIsland = {42};
	ok &= expect(!buildModelUvTopology(tests::uvGrid(128), &topology, &error, cancelled) && topology.faceIsland == QVector<int>{42} &&
					 !error.isEmpty(),
				 "chart analysis cancels without publishing partial results");
	ModelEdit detach;
	detach.kind = ModelEditKind::DetachUv;
	detach.selection.faces = {0};
	ModelSelection selected;
	ok &= expect(applyModelEdit(&mesh, detach, &selected, &error) && mesh.vertexCount == 6 && samePoses(mesh, original) &&
					 mesh.surfaces[0].uvSeams.size() == 2 && buildModelUvTopology(mesh.surfaces[0], &topology, &error) &&
					 topology.islands.size() == 2,
				 "detach duplicates boundary corners and seam marks across every pose");
	for (int face = 0; face < 2; ++face)
	{
		const auto a = original.surfaces[0].triangles[face], b = mesh.surfaces[0].triangles[face];
		ok &= expect(equal(original.surfaces[0].texCoords[a.a], mesh.surfaces[0].texCoords[b.a]) &&
						 equal(original.surfaces[0].texCoords[a.b], mesh.surfaces[0].texCoords[b.b]) &&
						 equal(original.surfaces[0].texCoords[a.c], mesh.surfaces[0].texCoords[b.c]),
					 "detaching keeps face UV coordinates unchanged");
	}
	const auto detached = mesh;
	ModelEdit clear;
	clear.kind = ModelEditKind::ClearUvSeams;
	clear.selection.edges = mesh.surfaces[0].uvSeams;
	ok &= expect(applyModelEdit(&mesh, clear, nullptr, &error) && mesh.surfaces[0].uvSeams.isEmpty() && mesh.vertexCount == 6 &&
					 buildModelUvTopology(mesh.surfaces[0], &topology, &error) && topology.islands.size() == 2,
				 "clearing marks does not weld already detached topology");
	mesh = detached;
	ModelEdit weld;
	weld.kind = ModelEditKind::WeldVertices;
	weld.selection.vertices = {0, 1, 2, 3, 4, 5};
	weld.weldDistance = 0;
	ok &=
		expect(applyModelEdit(&mesh, weld, nullptr, &error) && mesh.vertexCount == 4 && mesh.surfaces[0].uvSeams == QSet<ModelEdge>{{0, 2}},
			   "weld remaps and deduplicates marked edges");
	mesh = document.mesh();
	ModelEdit split;
	split.kind = ModelEditKind::SplitEdges;
	split.selection.edges = {{0, 2}};
	ok &= expect(applyModelEdit(&mesh, split, nullptr, &error) && mesh.surfaces[0].uvSeams == QSet<ModelEdge>{{0, 4}, {2, 4}} &&
					 buildModelUvTopology(mesh.surfaces[0], &topology, &error) && topology.islands.size() == 2,
				 "edge split carries seam onto both children");
	mesh = document.mesh();
	split.kind = ModelEditKind::Subdivide;
	split.selection.edges.clear();
	split.selection.faces = {0};
	ok &= expect(applyModelEdit(&mesh, split, nullptr, &error) && mesh.surfaces[0].uvSeams.size() == 2 &&
					 validateEditableModel(mesh).isEmpty(),
				 "face subdivision preserves conforming marked children");
	mesh = document.mesh();
	ModelEdit duplicate;
	duplicate.kind = ModelEditKind::DuplicateFaces;
	duplicate.selection.faces = {0};
	duplicate.translation = {0, 0, 2};
	ok &= expect(applyModelEdit(&mesh, duplicate, &selected, &error) && mesh.surfaces[0].uvSeams.size() == 2,
				 "duplicated face copies marked boundaries");
	ModelEdit erase;
	erase.kind = ModelEditKind::DeleteFaces;
	erase.selection.faces = selected.faces;
	ok &= expect(applyModelEdit(&mesh, erase, nullptr, &error) && mesh.surfaces[0].uvSeams == QSet<ModelEdge>{{0, 2}},
				 "deletion compacts and drops only removed marks");
	mesh = original;
	ModelEdit transform;
	transform.kind = ModelEditKind::TransformUv;
	transform.selection.faces = {0};
	transform.uvIslands = true;
	transform.uvPivotMode = ModelUvPivot::SelectionCentre;
	transform.uvRotation = 90;
	transform.uvOffset = {0.07f, -0.07f};
	transform.uvTranslationGrid = 0.125;
	ok &= expect(applyModelEdit(&mesh, transform, &selected, &error) && mesh.vertexCount == 4 && selected.faces == QSet<int>{0, 1} &&
					 equal(mesh.surfaces[0].texCoords[0], {1.125f, -0.125f}) && equal(mesh.surfaces[0].texCoords[2], {0.125f, 0.875f}) &&
					 samePoses(mesh, original),
				 "whole-island rotation uses selection centre then snapped offset without geometry changes");
	mesh = original;
	transform = {};
	transform.kind = ModelEditKind::TransformUv;
	transform.selection.faces = {0, 1};
	transform.uvScale = {2, 2};
	transform.uvPivotMode = ModelUvPivot::Custom;
	transform.uvPivot = {2, 3};
	ok &= expect(applyModelEdit(&mesh, transform, nullptr, &error) && equal(mesh.surfaces[0].texCoords[0], {-2, -3}),
				 "custom UV pivot applies before scale");
	mesh = original;
	transform.uvPivotMode = ModelUvPivot::Origin;
	ok &= expect(applyModelEdit(&mesh, transform, nullptr, &error) && equal(mesh.surfaces[0].texCoords[0], {0, 0}) &&
					 equal(mesh.surfaces[0].texCoords[2], {2, 2}),
				 "origin mode ignores dormant custom pivot");
	mesh = original;
	transform.kind = ModelEditKind::ProjectUv;
	transform.uvScale = {0.1f, 0.1f};
	transform.uvPivotMode = ModelUvPivot::SelectionCentre;
	ok &= expect(applyModelEdit(&mesh, transform, nullptr, &error) && equal(mesh.surfaces[0].texCoords[0], {4.5f, -4.5f}),
				 "projection finds pivot in projected model coordinates");
	mesh = document.mesh();
	transform = {};
	transform.kind = ModelEditKind::TransformUv;
	transform.selection.faces = {0};
	transform.uvIslands = true;
	transform.uvOffset = {2, 0};
	ok &= expect(applyModelEdit(&mesh, transform, nullptr, &error) && mesh.vertexCount == 6 && samePoses(mesh, original) &&
					 equal(mesh.surfaces[0].texCoords[0], {0, 0}) &&
					 equal(mesh.surfaces[0].texCoords[mesh.surfaces[0].triangles[0].a], {2, 0}),
				 "moving a marked island isolates corners from its unselected neighbour");
	const auto nativeBytes = exportEditableModel(mesh, QStringLiteral("md3"), 0, &error);
	ok &= expect(!nativeBytes.isEmpty() && importEditableModel(QStringLiteral("uv.md3"), nativeBytes, &parsed, &error) &&
					 parsed.vertexCount == 6 && parsed.frameCount == 2 && parsed.surfaces[0].uvSeams.isEmpty() &&
					 equal(parsed.surfaces[0].texCoords[parsed.surfaces[0].triangles[0].a], {2, 0}),
				 "native MD3 preserves resolved corner splits and UVs across poses; marks remain source metadata");
	for (double grid : {-1., 1e-7, std::numeric_limits<double>::infinity()})
	{
		transform.uvTranslationGrid = grid;
		const auto before = source(mesh);
		ok &=
			expect(!applyModelEdit(&mesh, transform, nullptr, &error) && source(mesh) == before, "invalid UV grid rejects the entire edit");
	}
	transform.uvTranslationGrid = 0;
	transform.uvPivotMode = ModelUvPivot(9);
	ok &= expect(!applyModelEdit(&mesh, transform, nullptr, &error), "unknown UV pivot mode is refused");
	if (app.arguments().size() > 1)
	{
		const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
		if (root.isEmpty() || !QDir().mkpath(root))
		{
			return EXIT_FAILURE;
		}
		QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("mesh-uv-XXXXXX")));
		if (!temporary.isValid())
		{
			return EXIT_FAILURE;
		}
		const auto input = QDir(temporary.path()).filePath(QStringLiteral("source.mesh.json"));
		const auto output = QDir(temporary.path()).filePath(QStringLiteral("marked.mesh.json"));
		ModelRecoverySnapshot snapshot;
		snapshot.mesh = detached;
		snapshot.selection.faces = {0};
		const auto recovery = writeModelRecovery(snapshot, temporary.path(), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
		ModelRecoverySnapshot restored;
		ok &=
			expect(!recovery.isEmpty() && inspectModelRecovery(recovery, &restored).isValid() && source(restored.mesh) == source(detached),
				   "checksummed recovery retains seam marks and detached animation corners");
		ok &= expect(document.setMesh(original, &error) && document.save(input, false, &error), "save CLI UV fixture");
		QJsonObject result;
		const auto cli = [&](QStringList arguments, int expected)
		{
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			arguments.prepend("--cli");
			arguments << "--json";
			process.start(app.arguments()[1], arguments);
			const bool done = process.waitForStarted(10000) && process.waitForFinished(30000);
			const auto bytes = process.readAllStandardOutput();
			const auto json = QJsonDocument::fromJson(bytes);
			result = json.object();
			if (!done || process.exitCode() != expected)
			{
				std::cerr << bytes.constData() << process.readAllStandardError().constData();
			}
			return done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected && json.isObject();
		};
		ok &= expect(cli({"model", "uv", input}, 0) && result.value("islands").toArray().size() == 1, "CLI reports UV islands");
		const QStringList command{"model", "edit", input, "--operation", "uv-mark-seams", "--edges", "0:2", "--output", output};
		ok &= expect(cli(command + QStringList{"--dry-run"}, 0) && !QFileInfo::exists(output), "CLI seam dry-run writes nothing");
		ok &= expect(cli(command, 0) && cli({"model", "uv", output}, 0) && result.value("islands").toArray().size() == 2 &&
						 result.value("seams").toArray() == QJsonArray{QJsonArray{0, 2}},
					 "CLI mark and inspect share saved seam metadata");
		ok &= expect(cli({"model", "build", output, "--output", QDir(temporary.path()).filePath("seamed.md3"), "--dry-run"}, 0) &&
						 result.value("authoringSeams").toInt() == 1 && !result.value("seamMarksExported").toBool(true),
					 "native build diagnostics distinguish retained source marks from exported UV geometry");
		const auto moved = QDir(temporary.path()).filePath(QStringLiteral("moved.mesh.json"));
		ok &= expect(cli({"model", "edit", output, "--operation", "uv-transform", "--faces", "0", "--uv-islands", "--uv-pivot-mode",
						  "selection", "--uv-rotation", "90", "--uv-offset", "0.07,-0.07", "--uv-grid", "0.125", "--output", moved},
						 0) &&
						 document.load(moved, &error) && document.mesh().vertexCount == 6 && samePoses(document.mesh(), original),
					 "CLI island transform preserves all poses while splitting its boundary");
		ok &= expect(cli({"model", "edit", input, "--operation", "uv-detach", "--faces", "0", "--output", moved, "--overwrite"}, 0) &&
						 document.load(moved, &error) && document.mesh().vertexCount == 6,
					 "CLI detaches UV faces");
		for (const QStringList &options :
			 QList<QStringList>{{"--uv-grid", "nan"}, {"--uv-pivot-mode", "custom"}, {"--uv-pivot-mode", "selection", "--uv-pivot", "1,2"}})
		{
			ok &= expect(
				cli(QStringList{"model", "edit", input, "--operation", "uv-transform", "--faces", "0", "--output", moved} + options, 2),
				"CLI rejects incompatible pivot or grid options");
		}
		ok &= expect(cli({"model", "edit", input, "--operation", "transform", "--faces", "0", "--uv-islands", "--output", moved}, 2),
					 "CLI rejects island expansion for position transforms");
		ok &= expect(cli({"model", "uv", input, "--surface", "9"}, 2), "CLI invalid UV surface is usage error");
	}
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
