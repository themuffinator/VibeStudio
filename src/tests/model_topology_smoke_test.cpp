#include "core/model_design.h"
#include "core/model_document.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

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
QByteArray source(const ModelMesh &mesh) { return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact); }
ModelMesh meshOf(const QVector<ModelVec3> &positions, const QVector<ModelTriangle> &triangles)
{
	ModelDesign design;
	ModelDesignPart part;
	part.primitive = QStringLiteral("plane");
	design.parts << part;
	auto mesh = buildModelDesignMesh(design);
	mesh.frames << mesh.frames[0];
	mesh.frames[1].name = QStringLiteral("pose2");
	mesh.animations = {{QStringLiteral("poses"), 0, 2}};
	auto &surface = mesh.surfaces[0];
	surface.triangles = triangles;
	surface.texCoords.fill({0, 0}, positions.size());
	ModelFrameGeometry frame;
	frame.positions = positions;
	frame.normals.fill({0, 0, 1}, positions.size());
	surface.frames = {frame, frame};
	for (auto &point : surface.frames[1].positions)
	{
		point.z += 8;
	}
	ModelTag tag;
	tag.name = QStringLiteral("tag_mount");
	tag.origin = {1, 2, 3};
	mesh.tags << tag;
	tag.frameIndex = 1;
	tag.origin.z += 8;
	mesh.tags << tag;
	updateEditableModelMetadata(&mesh);
	return mesh;
}
ModelMesh square()
{
	auto mesh = meshOf({{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}}, {{0, 1, 2}, {0, 2, 3}});
	mesh.surfaces[0].texCoords = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
	return mesh;
}
ModelMesh seamed()
{
	auto mesh = meshOf({{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}, {0, 0, 0}, {10, 10, 0}}, {{0, 1, 2}, {4, 5, 3}});
	mesh.surfaces[0].texCoords = {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0.25f, 0}, {1.25f, 1}};
	return mesh;
}
bool positiveFaces(const ModelMesh &mesh)
{
	for (const auto &frame : mesh.surfaces[0].frames)
	{
		for (const auto &t : mesh.surfaces[0].triangles)
		{
			const auto a = frame.positions[t.a], b = frame.positions[t.b], c = frame.positions[t.c];
			if ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x) <= 0)
			{
				return false;
			}
		}
	}
	return true;
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
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("mesh-topology-XXXXXX")));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	bool ok = true;
	QString error;
	const auto original = square();
	const auto edges = modelSurfaceEdges(original.surfaces[0]);
	ok &= expect(validateEditableModel(original).isEmpty() && edges == QVector<ModelEdge>{{0, 1}, {0, 2}, {0, 3}, {1, 2}, {2, 3}},
				 "indexed edges are unique, canonical and deterministically sorted");
	ok &= expect(modelSurfaceEdges(seamed().surfaces[0]).size() == 6, "coincident seam endpoints remain distinct indexed edges");
	{
		ModelDocument document;
		ok &= expect(document.setMesh(original, &error), "document prepares topology with its mesh");
		const auto cached = document.surfaceTopology(0);
		ok &= expect(cached.edges == edges && cached.faceUses.value({0, 2}) == 2 && cached.faceUses.value({0, 1}) == 1 &&
						 document.surfaceTopology(-1).edges.isEmpty(),
					 "prepared topology preserves edge order and incidence");
		ok &= expect(cached.allEdges == QSet<ModelEdge>(edges.cbegin(), edges.cend()) && cached.edgeVertices == QSet<int>{0, 1, 2, 3},
					 "complete edge selection retains exactly its connected vertices");
		document.setSelection({0, {}, {}, cached.allEdges});
		const auto allSelection = document.selection();
		auto invalidComplete = cached.allEdges;
		invalidComplete.remove({0, 1});
		invalidComplete.insert({1, 3});
		document.setSelection({0, {}, {}, invalidComplete});
		ok &= expect(document.selection() == allSelection, "equal-sized edge sets cannot bypass exact membership validation");
		auto withoutSets = cached;
		withoutSets.allEdges.clear();
		withoutSets.edgeVertices.clear();
		ok &= expect(cached.storageBytes() > withoutSets.storageBytes(), "complete selection caches contribute to history accounting");
		auto loose = original.surfaces[0];
		++loose.vertexCount;
		loose.texCoords.append({0, 0});
		for (auto &pose : loose.frames)
		{
			pose.positions.append({100, 100, 100});
			pose.normals.append({0, 0, 1});
		}
		ModelSurfaceTopology looseTopology;
		ok &= expect(prepareModelSurfaceTopology(loose, &looseTopology, &error) && looseTopology.allEdges == cached.allEdges &&
						 looseTopology.edgeVertices == cached.edgeVertices,
					 "complete edge selection excludes vertices with no incident edges");
		document.setSelection({0, {}, {}, {{0, 2}}});
		const auto goodSelection = document.selection();
		document.setSelection({0, {}, {}, {{1, 3}}});
		ok &= expect(document.selection() == goodSelection, "cached edge validation rejects an absent diagonal");
		ModelEdit move;
		move.selection.vertices = {0};
		move.translation = {0, 0, 2};
		ok &= expect(document.edit(move, &error) && document.surfaceTopology(0).edges.constData() == cached.edges.constData(),
					 "position edits reuse immutable topology");
		ModelEdit splitEdge;
		splitEdge.kind = ModelEditKind::SplitEdges;
		splitEdge.selection.edges = {{0, 2}};
		ok &= expect(document.edit(splitEdge, &error) && !document.surfaceTopology(0).faceUses.contains({0, 2}) &&
						 document.surfaceTopology(0).faceUses.contains({0, 4}),
					 "topology edits replace the index before adoption");
		const auto changed = document.surfaceTopology(0);
		ok &= expect(document.undo() && document.surfaceTopology(0).edges.constData() == cached.edges.constData() && document.redo() &&
						 document.surfaceTopology(0).edges.constData() == changed.edges.constData(),
					 "undo and redo restore the matching topology snapshot");
		ModelWorkControl control;
		control.cancelled = [] { return true; };
		auto preserved = cached;
		ok &= expect(!prepareModelSurfaceTopology(document.mesh().surfaces[0], &preserved, &error, control) &&
						 preserved.edges.constData() == cached.edges.constData(),
					 "cancelled topology preparation leaves the old snapshot intact");
	}
	ModelEdit split;
	split.kind = ModelEditKind::SplitEdges;
	split.selection.edges = {{0, 2}};
	ModelSelection selected;
	ModelMesh mesh = original;
	ok &= expect(applyModelEdit(&mesh, split, &selected, &error) && mesh.vertexCount == 5 && mesh.triangleCount == 4 &&
					 selected.edges == QSet<ModelEdge>{{0, 4}, {2, 4}} && positiveFaces(mesh),
				 "a shared-edge split conforms both neighbours and selects both child edges");
	if (mesh.vertexCount == 5)
	{
		const auto &surface = mesh.surfaces[0];
		ok &= expect(surface.frames[0].positions[4].x == 5 && surface.frames[0].positions[4].y == 5 &&
						 surface.frames[1].positions[4].z == 8 && surface.texCoords[4].u == 0.5f && surface.texCoords[4].v == 0.5f &&
						 surface.frames[1].normals[4].z == 1 && mesh.tags[1].origin.z == original.tags[1].origin.z &&
						 surface.skinPaths == original.surfaces[0].skinPaths,
					 "split interpolates all poses and UVs while preserving materials and animated tags");
	}
	for (const auto &selection :
		 {QSet<ModelEdge>{{0, 1}, {1, 2}}, QSet<ModelEdge>{{0, 1}, {1, 2}, {0, 2}}, QSet<ModelEdge>(edges.cbegin(), edges.cend())})
	{
		mesh = original;
		split.selection.edges = selection;
		ok &= expect(applyModelEdit(&mesh, split, &selected, &error) && mesh.vertexCount == 4 + selection.size() &&
						 selected.edges.size() == 2 * selection.size() && positiveFaces(mesh),
					 "two- and three-edge triangle cases preserve winding without duplicated midpoints");
	}
	for (const auto &selection : {QSet<ModelEdge>{}, QSet<ModelEdge>{{2, 0}}, QSet<ModelEdge>{{1, 3}}, QSet<ModelEdge>{{0, 99}}})
	{
		mesh = original;
		split.selection.edges = selection;
		selected.edges = {{0, 1}};
		ok &= expect(!applyModelEdit(&mesh, split, &selected, &error) && source(mesh) == source(original) &&
						 selected.edges == QSet<ModelEdge>{{0, 1}},
					 "invalid or empty edge selection fails atomically including the result selection");
	}
	split.selection.edges = {{0, 2}};
	ModelDocument document;
	ok &= expect(document.setMesh(original, &error), "prepare selection-aware history");
	document.setSelection(split.selection);
	const auto selectedBytes = document.historyBytes();
	ok &= expect(document.edit(split, &error) && document.undo() && source(document.mesh()) == source(original) &&
					 document.selection().edges == split.selection.edges && document.redo() &&
					 document.selection().edges == QSet<ModelEdge>{{0, 4}, {2, 4}} && document.historyBytes() > selectedBytes,
				 "undo and redo preserve edge selection and account for its retained history storage");
	ModelSelection invalidSelection;
	invalidSelection.edges = {{1, 3}};
	const auto beforeSelection = document.selection();
	document.setSelection(invalidSelection);
	ok &= expect(document.selection().edges == beforeSelection.edges, "document selection rejects nonexistent edges");
	ModelWorkControl cancel;
	bool editing = false;
	cancel.progress = [&](ModelWorkPhase phase, qint64, qint64) { editing = phase == ModelWorkPhase::Editing; };
	cancel.cancelled = [&] { return editing; };
	mesh = original;
	ok &= expect(!applyModelEdit(&mesh, split, nullptr, &error, cancel) && source(mesh) == source(original),
				 "cancelled topology editing leaves all poses untouched");
	mesh = original;
	auto &large = mesh.surfaces[0];
	large.texCoords.resize(modelDocumentMaxVertices);
	for (auto &frame : large.frames)
	{
		frame.positions.resize(modelDocumentMaxVertices);
		frame.normals.resize(modelDocumentMaxVertices, {0, 0, 1});
	}
	updateEditableModelMetadata(&mesh);
	const auto capacitySource = source(mesh);
	ok &= expect(!applyModelEdit(&mesh, split, nullptr, &error) && source(mesh) == capacitySource,
				 "edge splitting enforces working vertex limits before adoption");

	ModelEdit weld;
	weld.kind = ModelEditKind::WeldVertices;
	weld.selection.vertices = {0, 2, 4, 5};
	weld.weldDistance = 0;
	mesh = seamed();
	const auto seamSource = source(mesh);
	ok &= expect(!applyModelEdit(&mesh, weld, nullptr, &error) && source(mesh) == seamSource,
				 "default welding protects different UV coordinates");
	weld.preserveSeams = false;
	ok &=
		expect(applyModelEdit(&mesh, weld, &selected, &error) && mesh.vertexCount == 4 && mesh.triangleCount == 2 && positiveFaces(mesh) &&
				   mesh.surfaces[0].texCoords[0].u == 0 && mesh.surfaces[0].texCoords[2].u == 1 && selected.vertices == QSet<int>{0, 2},
			   "explicit seam merge retains lowest-index anchor attributes and remaps selection");
	mesh = seamed();
	mesh.surfaces[0].texCoords[4] = mesh.surfaces[0].texCoords[0];
	mesh.surfaces[0].texCoords[5] = mesh.surfaces[0].texCoords[2];
	for (auto &frame : mesh.surfaces[0].frames)
	{
		frame.normals[4] = frame.normals[5] = {0, 1, 0};
	}
	weld.preserveSeams = true;
	ok &= expect(!applyModelEdit(&mesh, weld, nullptr, &error), "default welding also preserves hard-normal seams in every pose");
	mesh = seamed();
	mesh.surfaces[0].frames[1].positions[4].x += 1;
	updateEditableModelMetadata(&mesh);
	weld.selection.vertices = {0, 4};
	weld.preserveSeams = false;
	weld.weldDistance = 0.01;
	const auto diverged = source(mesh);
	ok &= expect(!applyModelEdit(&mesh, weld, nullptr, &error) && source(mesh) == diverged,
				 "vertices coincident in one pose cannot weld when another pose exceeds the distance");
	mesh = seamed();
	weld.selection.vertices = {0, 4};
	weld.weldDistance = 0.001;
	for (auto &frame : mesh.surfaces[0].frames)
	{
		frame.positions[4].x = 0.0005f;
	}
	updateEditableModelMetadata(&mesh);
	ok &= expect(applyModelEdit(&mesh, weld, &selected, &error) && mesh.vertexCount == 5 && mesh.surfaces[0].frames[0].positions[0].x == 0,
				 "distance welding keeps the lowest anchor position instead of averaging across poses");
	mesh = meshOf({{0, 0, 0}, {10, 0, 0}, {0, 10, 0}, {0.6f, 0, 0}, {1.2f, 0, 0}}, {{0, 1, 2}});
	QVector<int> vertexMap, faceMap;
	ok &= expect(weldModelSurface(&mesh.surfaces[0], {0, 3, 4}, 0.7, true, &vertexMap, &faceMap, &error) && vertexMap[3] == 0 &&
					 vertexMap[4] == 4,
				 "weld distance is measured from fixed anchors, preventing transitive chain drift");
	mesh = meshOf({{0, 0, 0}, {0.00001f, 0, 0}, {10, 10, 0}, {0, 10, 0}}, {{0, 1, 2}, {0, 2, 3}});
	weld = {};
	weld.kind = ModelEditKind::WeldVertices;
	weld.selection.faces = {0, 1};
	weld.selection.edges = {{0, 1}, {1, 2}};
	ok &= expect(applyModelEdit(&mesh, weld, &selected, &error) && mesh.triangleCount == 1 && mesh.vertexCount == 3 &&
					 selected.faces == QSet<int>{0} && selected.edges == QSet<ModelEdge>{{0, 1}},
				 "collapsed weld faces are removed and surviving face/edge selections are compacted");
	for (auto invalid :
		 {meshOf({{0, 0, 0}, {10, 0, 0}, {0, 10, 0}, {0, 0, 0}}, {{0, 1, 2}, {3, 1, 2}}),
		  meshOf({{0, 0, 0}, {10, 0, 0}, {0, 10, 0}, {0, 0, 0}, {0, -10, 0}}, {{0, 1, 2}, {3, 1, 4}}),
		  meshOf({{0, 0, 0}, {10, 0, 0}, {0, 10, 0}, {0, 0, 0}, {0, -10, 0}, {0, 0, 10}}, {{0, 1, 2}, {1, 0, 4}, {3, 1, 5}})})
	{
		weld = {};
		weld.kind = ModelEditKind::WeldVertices;
		weld.selection.vertices = {0, 3};
		const auto saved = source(invalid);
		ok &= expect(!applyModelEdit(&invalid, weld, nullptr, &error) && source(invalid) == saved,
					 "new duplicate faces, inconsistent winding and nonmanifold edges each fail atomically");
	}
	mesh = meshOf({{0, 0, 0}, {10, 0, 0}, {0, 10, 0}, {0, 1, 0}, {5, 1, 0}}, {{0, 1, 2}, {3, 4, 1}});
	mesh.surfaces[0].frames[1].positions[4].y = 0;
	updateEditableModelMetadata(&mesh);
	weld.weldDistance = 1.1;
	const auto secondPose = source(mesh);
	ok &= expect(!applyModelEdit(&mesh, weld, nullptr, &error) && source(mesh) == secondPose,
				 "geometric collapse in a later animation pose rejects the complete weld");
	for (double distance : {-1., 0.0000001, 1000001., std::numeric_limits<double>::infinity()})
	{
		mesh = seamed();
		weld.weldDistance = distance;
		ok &= expect(!applyModelEdit(&mesh, weld, nullptr, &error), "invalid weld distances are refused");
	}
	mesh = meshOf({{0, 0, 0}, {10, 0, 0}, {0, 10, 0}}, {{0, 1, 2}});
	weld = {};
	weld.kind = ModelEditKind::WeldVertices;
	weld.selection.faces = {0};
	weld.weldDistance = 20;
	ok &= expect(!applyModelEdit(&mesh, weld, nullptr, &error) && mesh.triangleCount == 1, "welding cannot erase the entire surface");
	mesh = square();
	ModelEdit uv;
	uv.kind = ModelEditKind::TransformUv;
	uv.selection.faces = {0};
	uv.selection.edges = {{0, 2}};
	uv.uvOffset = {1, 0};
	ok &= expect(applyModelEdit(&mesh, uv, &selected, &error) && mesh.vertexCount == 4 && selected.edges == uv.selection.edges &&
					 mesh.surfaces[0].texCoords[0].u == 1 && mesh.surfaces[0].texCoords[2].u == 2,
				 "explicit edge endpoints take precedence over face-local UV seam splitting");

	mesh = square();
	auto &crowded = mesh.surfaces[0];
	while (crowded.texCoords.size() < 3000)
	{
		crowded.texCoords << ModelTexCoord{float(crowded.texCoords.size()), 0};
		for (auto &frame : crowded.frames)
		{
			frame.positions << frame.positions[0];
			frame.normals << frame.normals[0];
		}
	}
	updateEditableModelMetadata(&mesh);
	QSet<int> crowdedSelection;
	for (int i = 0; i < crowded.vertexCount; ++i)
	{
		crowdedSelection.insert(i);
	}
	const auto crowdedSource = source(mesh);
	vertexMap = {99};
	faceMap = {88};
	ok &= expect(!weldModelSurface(&crowded, crowdedSelection, 0, true, &vertexMap, &faceMap, &error) &&
					 error.contains(QStringLiteral("comparison budget")) && source(mesh) == crowdedSource &&
					 vertexMap == QVector<int>{99} && faceMap == QVector<int>{88},
				 "dense incompatible weld search stops at its budget without mutating geometry or maps");
	bool cancelSearch = false;
	ModelWorkControl searchControl;
	searchControl.progress = [&](ModelWorkPhase, qint64 completed, qint64) { cancelSearch = completed >= 512; };
	searchControl.cancelled = [&] { return cancelSearch; };
	ok &= expect(!weldModelSurface(&crowded, crowdedSelection, 0, true, &vertexMap, &faceMap, &error, searchControl) && cancelSearch &&
					 source(mesh) == crowdedSource && vertexMap == QVector<int>{99},
				 "weld search polls cancellation inside dense candidate loops");

	if (app.arguments().size() > 1)
	{
		const auto path = QDir(temporary.path()).filePath(QStringLiteral("square.mesh.json"));
		const auto seamPath = QDir(temporary.path()).filePath(QStringLiteral("seamed.mesh.json"));
		const auto output = QDir(temporary.path()).filePath(QStringLiteral("edited.mesh.json"));
		ModelDocument input;
		ok &= expect(input.setMesh(original, &error) && input.save(path, false, &error) && input.setMesh(seamed(), &error) &&
						 input.save(seamPath, false, &error),
					 "write CLI fixtures");
		const auto cli = [&](QStringList args, int expected, QJsonObject *result = nullptr)
		{
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			args.prepend(QStringLiteral("--cli"));
			args << QStringLiteral("--json");
			process.start(app.arguments()[1], args);
			const bool finished = process.waitForStarted(10000) && process.waitForFinished(30000);
			const auto bytes = process.readAllStandardOutput();
			const auto json = QJsonDocument::fromJson(bytes);
			if (!finished || process.exitCode() != expected)
			{
				std::cerr << bytes.constData() << process.readAllStandardError().constData();
			}
			if (result)
			{
				*result = json.object();
			}
			return finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected && json.isObject();
		};
		QJsonObject topology;
		ok &= expect(cli({"model", "topology", path}, 0, &topology) && topology.value("edges").toArray().size() == 5 &&
						 topology.value("edges").toArray()[1].toObject().value("faces").toArray().size() == 2,
					 "CLI topology reports exact edge endpoints and incident faces");
		ok &= expect(cli({"model", "edit", path, "--operation", "split-edges", "--edges", "2:0", "--output", output, "--dry-run"}, 0) &&
						 !QFileInfo::exists(output),
					 "CLI split dry-run accepts either endpoint order without writing");
		ok &= expect(cli({"model", "edit", path, "--operation", "split-edges", "--edges", "0:2", "--output", output}, 0) &&
						 input.load(output, &error) && input.mesh().triangleCount == 4 && input.mesh().frameCount == 2,
					 "CLI split uses the same all-frame document operation");
		ok &= expect(cli({"model", "edit", path, "--operation", "split-edges", "--edges", "1:3", "--output", output, "--overwrite"}, 2),
					 "CLI invalid edge gives a usage error");
		ok &= expect(cli({"model", "edit", seamPath, "--operation", "weld", "--vertices", "all", "--weld-distance", "0", "--output", output,
						  "--overwrite"},
						 4),
					 "CLI default weld reports protected seams as validation failure");
		ok &= expect(cli({"model", "edit", seamPath, "--operation", "weld", "--vertices", "all", "--weld-distance", "0", "--merge-seams",
						  "--output", output, "--overwrite"},
						 0) &&
						 input.load(output, &error) && input.mesh().vertexCount == 4,
					 "CLI explicit seam merge produces the reviewed topology");
		ok &= expect(cli({"model", "edit", seamPath, "--operation", "weld", "--vertices", "all", "--weld-distance", "nan", "--output",
						  output, "--overwrite"},
						 2),
					 "CLI nonfinite distance is a usage error");
		ok &= expect(cli({"model", "topology", path, "--surface", "99"}, 2), "CLI topology bounds surface selection");
	}
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
