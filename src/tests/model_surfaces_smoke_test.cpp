#include "cli/model_surfaces.h"
#include "core/model_recovery.h"
#include "tests/model_surfaces_test_helpers.h"
#include "tests/model_mdl_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
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
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("surface-authoring-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	bool ok = true;
	QString error;
	const auto original = surfaceFixture();
	const auto originalBytes = surfaceBytes(original);
	ModelMesh mdl;
	ok &= expect(importEditableModel("grouped.mdl", groupedMdlFixture().bytes, &mdl, &error), "import independent grouped MDL fixture");
	const auto mdlBefore = editableModelJson(mdl);
	ModelEdit mdlCopy;
	mdlCopy.kind = ModelEditKind::DuplicateSurface;
	mdlCopy.text = "mdl_copy";
	ok &= expect(applyModelEdit(&mdl, mdlCopy, nullptr, &error), "surface duplication supports native MDL authoring metadata");
	const auto mdlAfter = editableModelJson(mdl);
	for (const QString key : {"mdl", "skins", "frames", "animations"})
		ok &= expect(mdlBefore.contains(key) && mdlBefore.value(key) == mdlAfter.value(key),
					 "surface duplication preserves native skin indices, groups and header metadata");
	ok &= expect(validateEditableModel(original).isEmpty(), "original animated surface fixture is valid");
	ModelDocument document;
	ModelEdit edit;
	edit.kind = ModelEditKind::SeparateFaces;
	edit.selection = {1, {}, {1}};
	edit.text = "panel";
	ok &= expect(document.setMesh(original, &error), "open animated source");
	document.setSelection(edit.selection);
	ok &= expect(document.edit(edit, &error), "separate a central face");
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
		return 1;
	}
	const auto separated = document.mesh();
	const auto selected = document.selection();
	ok &= expect(separated.surfaces.size() == 4 && separated.vertexCount == 21 && separated.triangleCount == 9 &&
					 selected == ModelSelection{3, {}, {0}},
				 "separation duplicates only the three shared vertices and selects the new face");
	ok &= expect(
		exactFace(original.surfaces[1], 1, separated.surfaces[3], 0) && exactFace(original.surfaces[1], 0, separated.surfaces[1], 0) &&
			exactFace(original.surfaces[1], 2, separated.surfaces[1], 1) && exactVertex(original.surfaces[1], 5, separated.surfaces[1], 5),
		"every pose, normal, UV, face winding and original unused vertex survives separation");
	ok &= expect(separated.surfaces[3].skinPaths == original.surfaces[1].skinPaths &&
					 separated.surfaces[3].uvSeams == QSet<ModelEdge>{{0, 1}, {1, 2}} &&
					 separated.surfaces[1].uvSeams == original.surfaces[1].uvSeams,
				 "ordered material alternates and only surviving seam edges are retained");
	const auto originalJson = editableModelJson(original), separatedJson = editableModelJson(separated);
	for (const QString key : {"tags", "animations", "collisionBoxes", "frames", "md2SkinSize"})
		ok &= expect(originalJson.contains(key) && originalJson.value(key) == separatedJson.value(key),
					 "surface edits retain model-global authoring metadata");
	ok &= expect(document.undo() && surfaceBytes(document.mesh()) == originalBytes && document.selection() == edit.selection &&
					 document.redo() && surfaceBytes(document.mesh()) == surfaceBytes(separated) && document.selection() == selected,
				 "undo and redo restore exact source and component selection");
	ModelRecoverySnapshot snapshot;
	snapshot.mesh = separated;
	snapshot.selection = selected;
	snapshot.title = "surfaces";
	const auto recovery = writeModelRecovery(snapshot, temporary.path(), "64d3ce78-05e4-4075-a606-f9398f25474b", &error);
	ModelRecoverySnapshot restored;
	ok &= expect(!recovery.isEmpty() && inspectModelRecovery(recovery, &restored).isValid() &&
					 surfaceBytes(restored.mesh) == surfaceBytes(separated) && restored.selection == selected,
				 "recovery retains partitioned all-pose geometry and selection");
	const auto source = QDir(temporary.path()).filePath("source.mesh.json"), output = QDir(temporary.path()).filePath("out.mesh.json");
	ok &= expect(document.save(output, false, &error), "save separated editable source");
	ModelDocument loaded;
	ok &= expect(loaded.load(output, &error) && surfaceBytes(loaded.mesh()) == surfaceBytes(separated),
				 "source reopen retains exact surface partition");
	const auto md3 = exportEditableModel(separated, "md3", 0, &error);
	ModelMesh native;
	ok &= expect(!md3.isEmpty() && importEditableModel("model.md3", md3, &native, &error) && native.surfaces.size() == 4 &&
					 native.frames.size() == 2 && native.surfaces[3].name == "panel" &&
					 native.surfaces[3].skinPaths == separated.surfaces[3].skinPaths,
				 "MD3 exports all renamed material surfaces and poses");
	// An edge can disappear while both endpoints remain used by other faces.
	auto seamCase = original;
	seamCase.surfaces[1].triangles = {{0, 1, 2}, {0, 3, 4}, {1, 4, 3}};
	seamCase.surfaces[1].uvSeams = {{0, 1}};
	// Move one vertex so the two new triangles are noncollapsed in both poses.
	for (auto &pose : seamCase.surfaces[1].frames)
		pose.positions[4].x += 1;
	edit.selection.faces = {0};
	ok &= expect(document.setMesh(seamCase, &error) && document.edit(edit, &error) && document.mesh().surfaces[1].uvSeams.isEmpty() &&
					 document.mesh().surfaces[3].uvSeams == QSet<ModelEdge>{{0, 1}},
				 "partition removes seam marks whose edge no longer exists, even when endpoints survive");
	for (int from = 0; from < 3; ++from)
		for (int to = 0; to < 3; ++to)
		{
			if (from == to)
				continue;
			for (bool all : {false, true})
			{
				edit = {};
				edit.kind = ModelEditKind::MoveFacesToSurface;
				edit.selection = {from, {}, all ? QSet<int>{0, 1, 2} : QSet<int>{1}};
				edit.targetSurface = to;
				ok &=
					expect(document.setMesh(original, &error) && document.edit(edit, &error), "move supports each source/target ordering");
				const int destination = to - int(all && from < to);
				const auto &mesh = document.mesh();
				ok &= expect(mesh.surfaces.size() == (all ? 2 : 3) && document.selection().surface == destination &&
								 document.selection().faces == (all ? QSet<int>{3, 4, 5} : QSet<int>{3}) &&
								 exactFace(original.surfaces[to], 0, mesh.surfaces[destination], 0) &&
								 exactFace(original.surfaces[from], all ? 0 : 1, mesh.surfaces[destination], 3),
							 "move preserves target indices and remaps resulting selection after source removal");
				ok &= expect(all ? (mesh.vertexCount == 18 && exactVertex(original.surfaces[from], 5, mesh.surfaces[destination], 11))
								 : (mesh.vertexCount == 21 && exactVertex(original.surfaces[from], 5, mesh.surfaces[from], 5)),
							 "move preserves unused vertices and duplicates only shared partition boundaries");
			}
		}
	edit = {};
	edit.kind = ModelEditKind::SeparateFaces;
	edit.selection = {1, {}, {0, 1, 2}};
	edit.text = "whole";
	ok &= expect(document.setMesh(original, &error) && document.edit(edit, &error) && document.mesh().surfaces.size() == 3 &&
					 document.mesh().vertexCount == 18 && document.mesh().surfaces[1].name == "whole",
				 "separating every face renames in place without an empty surface or dropped unused vertex");
	edit = {};
	edit.kind = ModelEditKind::DuplicateSurface;
	edit.selection.surface = 1;
	edit.text = "copy";
	ok &= expect(document.setMesh(original, &error) && document.edit(edit, &error) && document.mesh().surfaces.size() == 4 &&
					 document.mesh().vertexCount == 24 && exactVertex(original.surfaces[1], 5, document.mesh().surfaces[3], 5) &&
					 document.mesh().surfaces[3].uvSeams == original.surfaces[1].uvSeams &&
					 document.selection() == ModelSelection{3, {}, {0, 1, 2}},
				 "duplication preserves full animated surface, unused vertices and seams");
	// Editing the duplicate must never alias the source data through Qt COW.
	ModelEdit transform;
	transform.selection = document.selection();
	transform.translation = {1, 0, 0};
	ok &= expect(document.edit(transform, &error) && exactVertex(original.surfaces[1], 0, document.mesh().surfaces[1], 0) &&
					 !exactVertex(original.surfaces[1], 0, document.mesh().surfaces[3], 0),
				 "duplicated geometry is independently editable");
	for (int target = 0; target < 3; ++target)
	{
		edit = {};
		edit.kind = ModelEditKind::JoinSurfaces;
		edit.targetSurface = target;
		edit.surfaces = {0, 1, 2};
		ok &= expect(document.setMesh(original, &error) && document.edit(edit, &error) && document.mesh().surfaces.size() == 1 &&
						 document.mesh().vertexCount == 18 && document.mesh().surfaces[0].name == original.surfaces[target].name &&
						 document.selection().surface == 0 && document.selection().faces.size() == 9 &&
						 exactVertex(original.surfaces[target], 5, document.mesh().surfaces[0], 5),
					 "join retains target geometry first and exact unused vertices");
		int face = 3;
		for (int i = 0; i < 3; ++i)
			if (i != target)
			{
				ok &= expect(exactFace(original.surfaces[i], 1, document.mesh().surfaces[0], face + 1),
							 "joined sources follow deterministic source-index order");
				face += 3;
			}
	}
	auto single = document.mesh();
	single.tags.clear();
	single.surfaces[0].skinPaths = {"models/body.pcx", "models/body_alt.pcx"};
	const auto md2 = exportEditableModel(single, "md2", 0, &error);
	ok &= expect(!md2.isEmpty() && importEditableModel("model.md2", md2, &native, &error) && native.triangleCount == 9 &&
					 native.frames.size() == 2,
				 "joining hands directly to single-surface MD2 export with all poses");
	edit = {};
	edit.kind = ModelEditKind::DeleteSurface;
	edit.selection.surface = 1;
	ok &= expect(document.setMesh(original, &error) && document.edit(edit, &error) && document.mesh().surfaces.size() == 2 &&
					 document.selection().surface == 1 && document.selection().faces.isEmpty() &&
					 exactVertex(original.surfaces[2], 5, document.mesh().surfaces[1], 5),
				 "deletion keeps unselected order and selects a surviving surface");
	const auto refuse = [&](const ModelMesh &mesh, const ModelEdit &request, const char *message) {
		ModelDocument candidate;
		if (!candidate.setMesh(mesh, &error))
		{
			std::cerr << "Invalid refusal fixture: " << error.toStdString() << '\n';
			return false;
		}
		const auto before = candidate.revisionFingerprint();
		const auto selection = candidate.selection();
		return expect(!candidate.edit(request, &error) && !error.isEmpty() && candidate.revisionFingerprint() == before &&
						  candidate.selection() == selection && !candidate.canUndo(),
					  message);
	};
	edit.selection.surface = 0;
	ok &= refuse(single, edit, "cannot delete last surface");
	edit.kind = ModelEditKind::RenameSurface;
	for (const auto &name : QStringList{"", "part1", QString(129, 'a'), QString("bad\nname")})
	{
		edit.text = name;
		ok &= refuse(original, edit, "invalid or duplicate surface names fail atomically");
	}
	edit.text = "part0";
	ok &= expect(document.setMesh(original, &error) && document.edit(edit, &error) && !document.canUndo(),
				 "no-op rename does not create history");
	edit.text = QString::fromUtf8("panneau_表面");
	ok &= expect(document.edit(edit, &error) && document.mesh().surfaces[0].name == edit.text,
				 "editable names support localization independently of native limits");
	auto collision = original;
	collision.surfaces[1].name = "part0_1";
	ok &= expect(exportEditableModel(collision, "md3", 0, &error).isEmpty(), "fixture triggers native normalized-name collision");
	edit.selection.surface = 1;
	edit.text = "panel";
	ok &= expect(document.setMesh(collision, &error) && document.edit(edit, &error) &&
					 !exportEditableModel(document.mesh(), "md3", 0, &error).isEmpty(),
				 "surface rename resolves the actual MD3 export collision");
	auto mismatch = original;
	mismatch.surfaces[1].skinPaths[1] = "models/different_alt.tga";
	for (const auto kind : {ModelEditKind::MoveFacesToSurface, ModelEditKind::JoinSurfaces})
	{
		edit = {};
		edit.kind = kind;
		edit.selection = {0, {}, {1}};
		edit.targetSurface = 1;
		if (kind == ModelEditKind::JoinSurfaces)
			edit.surfaces = {0, 1};
		ok &= refuse(mismatch, edit, "different alternate material binding requires explicit replacement");
		edit.adoptTargetMaterials = true;
		ok &= expect(document.setMesh(mismatch, &error) && document.edit(edit, &error) &&
						 document.mesh().surfaces[document.selection().surface].skinPaths == mismatch.surfaces[1].skinPaths,
					 "explicit material adoption retains exact target slot order");
	}
	edit = {};
	edit.kind = ModelEditKind::SeparateFaces;
	edit.selection.faces = {0};
	edit.text = "split";
	for (int mode = 0; mode < 7; ++mode)
	{
		auto invalid = edit;
		if (mode == 0)
			invalid.frame = 0;
		if (mode == 1)
			invalid.selection.faces = {99};
		if (mode == 2)
			invalid.selection.vertices = {0};
		if (mode == 3)
			invalid.selection.edges = {{0, 1}};
		if (mode == 4)
			invalid.selection.faces.clear();
		if (mode == 5)
			invalid.adoptTargetMaterials = true;
		if (mode == 6)
			invalid.surfaces = {0, 1};
		ok &= refuse(original, invalid, "ambiguous face, scope and unrelated options are rejected atomically");
	}
	auto crowded = original;
	while (crowded.surfaces.size() < modelDocumentMaxSurfaces)
	{
		auto copy = original.surfaces[0];
		copy.name = QStringLiteral("extra%1").arg(crowded.surfaces.size());
		crowded.surfaces.append(copy);
	}
	updateEditableModelMetadata(&crowded);
	ok &= refuse(crowded, edit, "separation enforces surface capacity");
	edit.kind = ModelEditKind::DuplicateSurface;
	ok &= refuse(crowded, edit, "duplication enforces surface capacity");
	auto full = original;
	auto &padded = full.surfaces[0];
	while (padded.vertexCount < modelDocumentMaxVertices - 12)
	{
		padded.texCoords.append(padded.texCoords[5]);
		for (auto &pose : padded.frames)
		{
			pose.positions.append(pose.positions[5]);
			pose.normals.append(pose.normals[5]);
		}
		++padded.vertexCount;
	}
	updateEditableModelMetadata(&full);
	edit.kind = ModelEditKind::SeparateFaces;
	ok &= refuse(full, edit, "shared boundary duplication checks global vertex capacity before allocating");
	edit.kind = ModelEditKind::DuplicateSurface;
	edit.selection.surface = 1;
	ok &= refuse(full, edit, "whole-surface duplication checks global vertex capacity");
	auto faceLimit = original;
	while (faceLimit.surfaces[0].triangles.size() < modelDocumentMaxTriangles - 6)
		faceLimit.surfaces[0].triangles.append(faceLimit.surfaces[0].triangles[0]);
	updateEditableModelMetadata(&faceLimit);
	ok &= refuse(faceLimit, edit, "duplication independently checks the global triangle capacity");
	auto storage = original;
	storage.frames.resize(1024);
	for (int f = 0; f < 1024; ++f)
		storage.frames[f].name = QStringLiteral("pose%1").arg(f);
	storage.tags.clear();
	for (auto &surface : storage.surfaces)
		surface.frames.fill(surface.frames[0], 1024);
	while (storage.surfaces[0].vertexCount < 1009)
	{
		auto &surface = storage.surfaces[0];
		surface.texCoords.append(ModelTexCoord{});
		for (auto &pose : surface.frames)
		{
			pose.positions.append(ModelVec3{});
			pose.normals.append({0, 0, 1});
		}
		++surface.vertexCount;
	}
	updateEditableModelMetadata(&storage);
	ok &= refuse(storage, edit, "duplication independently checks all-pose storage capacity");
	// Cancellation while copying many frame vertices leaves document/history untouched.
	edit = {};
	edit.kind = ModelEditKind::JoinSurfaces;
	edit.targetSurface = 1;
	edit.surfaces = {0, 1, 2};
	bool cancelled = false;
	ModelWorkControl control{[&] { return cancelled; },
							 [&](ModelWorkPhase phase, qint64 done, qint64) {
								 if (phase == ModelWorkPhase::Editing && done >= 1024)
									 cancelled = true;
							 }};
	ok &= expect(document.setMesh(storage, &error), "prepare bounded animation-storage fixture");
	const auto beforeCancel = document.revisionFingerprint();
	ok &=
		expect(!document.edit(edit, &error, control) && cancelled && document.revisionFingerprint() == beforeCancel && !document.canUndo(),
			   "cancellation during surface assembly is atomic and responsive");
	// Shared CLI parser/service plus a real executable dispatch check.
	ok &= expect(document.setMesh(original, &error) && document.save(source, false, &error), "save CLI input");
	const QStringList base{"vibestudio", "--cli", "model", "surfaces", source};
	const auto list = cli::runModelSurfaces(base);
	ok &= expect(list.exitCode == 0 && list.payload.value("surfaces").toArray().size() == 3 && !list.payload.value("written").toBool(),
				 "CLI lists indexed surfaces and ordered materials without mutation");
	const QStringList command =
		base + QStringList{"--operation", "separate", "--surface", "1", "--faces", "1", "--name", "panel", "--output", output};
	ok &= expect(cli::runModelSurfaces(command).exitCode == 1, "CLI refuses an existing output without overwrite");
	QFile protectedOutput(output);
	ok &= expect(protectedOutput.open(QIODevice::WriteOnly | QIODevice::Truncate) && protectedOutput.write("protected destination") == 21,
				 "prepare a distinct existing destination");
	protectedOutput.close();
	const auto dryResult = cli::runModelSurfaces(command + QStringList{"--dry-run", "--overwrite"});
	ok &=
		expect(protectedOutput.open(QIODevice::ReadOnly) && dryResult.exitCode == 0 && protectedOutput.readAll() == "protected destination",
			   "CLI dry-run validates and preserves existing bytes");
	protectedOutput.close();
	auto fresh = command;
	const auto freshPath = QDir(temporary.path()).filePath("dry.mesh.json");
	fresh[fresh.indexOf("--output") + 1] = freshPath;
	ok &= expect(cli::runModelSurfaces(fresh + QStringList{"--dry-run"}).exitCode == 0 && !QFileInfo::exists(freshPath),
				 "CLI dry-run never creates a new destination");
	ok &= expect(cli::runModelSurfaces(command + QStringList{"--overwrite"}).exitCode == 0 && loaded.load(output, &error) &&
					 surfaceBytes(loaded.mesh()) == surfaceBytes(separated),
				 "CLI and GUI document operations produce the same animated source");
	for (const auto &extra : QVector<QStringList>{
			 {"--frame", "0"}, {"--surface", "0"}, {"--adopt-target-materials"}, {"--surfaces", "0,1"}, {"--target-surface", "2"}})
		ok &= expect(cli::runModelSurfaces(command + extra).exitCode == 2, "CLI rejects repeated, unrelated and unknown options");
	for (const auto &faces : QStringList{"-1", "0,0", "0,", "0,999999999999999999"})
	{
		auto args = command;
		args[args.indexOf("--faces") + 1] = faces;
		ok &= expect(cli::runModelSurfaces(args).exitCode == 2, "CLI rejects malformed face indices before editing");
	}
	if (argc == 2)
	{
		QProcess process;
		process.setWorkingDirectory(temporary.path());
		process.start(app.arguments()[1], {"--cli", "model", "surfaces", source, "--operation", "join", "--surfaces", "0,1,2",
										   "--target-surface", "2", "--output", output, "--dry-run", "--overwrite", "--json",
										   "--settings-file", QDir(temporary.path()).filePath("settings.ini")});
		const bool finished = process.waitForStarted(10000) && process.waitForFinished(30000);
		const auto json = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
		ok &= expect(finished && process.exitCode() == 0 && json.value("surfaces").toArray().size() == 1 && !json.value("written").toBool(),
					 "real CLI dispatch recognizes multi-surface options and emits structured dry-run result");
		if (!finished || process.exitCode() != 0)
			std::cerr << process.readAllStandardError().constData();
	}
	if (!ok)
		std::cerr << error.toStdString() << '\n';
	std::cout << checks << " surface authoring assertions\n";
	return ok ? 0 : 1;
}
