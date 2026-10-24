#include "cli/model_tools.h"
#include "tests/model_output_test_helpers.h"
#include "core/model_recovery.h"
#include "tests/model_weight_transfer_test_helpers.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0, failures = 0;
bool expect(bool ok, const char *name)
{
	++checks;
	if (!ok)
	{
		++failures;
		std::cerr << "FAIL: " << name << '\n';
	}
	return ok;
}
bool close(double a, double b)
{
	return std::abs(a - b) < .00001;
}
double weight(const ModelMesh &mesh, int v, int joint, int surface = 0)
{
	return modelVertexJointWeight(mesh.surfaces[surface], v, joint);
}
bool write(const QString &path, const QByteArray &bytes)
{
	QFile f(path);
	return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir tmp(QDir(root).filePath("weight-transfer-XXXXXX"));
	if (!tmp.isValid())
		return 1;
	const auto mesh = transferFixture();
	const auto bytes = rigBytes(mesh);
	QString error;
	ModelDocument document;
	auto edit = mirrorWeights();
	if (!expect(document.setMesh(mesh, &error) && document.edit(edit, &error), "mirror validates and commits"))
	{
		std::cerr << error.toStdString() << '\n';
		return 1;
	}
	const auto mirrored = document.mesh();
	expect(close(weight(mirrored, 3, 2), .75) && close(weight(mirrored, 4, 2), 0) && close(weight(mirrored, 5, 2), 1),
		   "one-way triangle mirror maps the opposite arm");
	expect(close(mirrored.surfaces[0].frames[2].positions[3].y, -2.25), "mirrored weight drives analytic destination pose");
	expect(close(weight(mirrored, 0, 1), .75) && close(weight(mirrored, 6, 0), 1), "source and centre weights remain intact");
	expect(document.undo() && rigBytes(document.mesh()) == bytes, "undo restores source weights and poses");
	expect(document.redo() && rigBytes(document.mesh()) == rigBytes(mirrored), "redo restores mirrored result");
	const auto fingerprint = document.revisionFingerprint();
	expect(document.edit(edit, &error) && document.revisionFingerprint() == fingerprint, "repeated mirror is a true no-op");
	expect(document.undo() && rigBytes(document.mesh()) == bytes, "no-op does not consume undo history");
	for (auto method : {ModelWeightTransferMethod::Vertex, ModelWeightTransferMethod::Triangle})
	{
		auto candidate = mesh;
		auto request = edit;
		request.weightTransfer.method = method;
		expect(applyModelEdit(&candidate, request, nullptr, &error) && rigBytes(candidate) == rigBytes(mirrored),
			   "vertex and triangle sampling agree at corresponding corners");
	}
	auto both = edit;
	both.selection.vertices = {0, 1, 2, 3, 4, 5};
	both.weightTransfer.side = ModelWeightTransferSide::All;
	auto candidate = mesh;
	expect(applyModelEdit(&candidate, both, nullptr, &error) && close(weight(candidate, 0, 1), 0) && close(weight(candidate, 3, 2), .75),
		   "bidirectional mirror reads a frozen original source");
	auto reverse = edit;
	reverse.weightTransfer.side = ModelWeightTransferSide::NegativeToPositive;
	candidate = mirrored;
	expect(applyModelEdit(&candidate, reverse, nullptr, &error) && close(weight(candidate, 0, 1), .75),
		   "reverse direction maps right back to left");
	for (int axis = 1; axis < 3; ++axis)
	{
		auto rotated = mesh;
		for (auto &s : rotated.surfaces)
			for (auto &frame : s.frames)
			{
				for (auto &p : frame.positions)
					std::swap(p.x, axis == 1 ? p.y : p.z);
				for (auto &n : frame.normals)
					std::swap(n.x, axis == 1 ? n.y : n.z);
			}
		// Isolate bind-space axis classification without a live pose.
		auto request = edit;
		request.weightTransfer.mirrorAxis = axis;
		// A static root-only analytic fixture isolates plane classification on each axis.
		rotated.skeleton.clips.clear();
		rotated.skeleton.bakedClipForFrame = {-1};
		rotated.frames.resize(1);
		rotated.animations.clear();
		for (auto &s : rotated.surfaces)
		{
			s.frames.resize(1);
			expect(modelSkinningFromBindPose(rotated.skeleton, s.frames[0].positions, s.frames[0].normals,
											 {0, 1, 0, 1, 0, 1, 0, 2, 0, 2, 0, 2, 0, 1}, {.25, .75, 1, 0, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0}, 2,
											 &s.skinning),
				   "rebind axis fixture");
		}
		updateEditableModelMetadata(&rotated);
		expect(applyModelEdit(&rotated, request, nullptr, &error) && close(weight(rotated, 3, 2), .75),
			   "Y and Z mirror planes use bind coordinates");
	}
	auto subset = edit;
	subset.selection.vertices = {3};
	candidate = mesh;
	expect(applyModelEdit(&candidate, subset, nullptr, &error) && close(weight(candidate, 3, 2), .75) && close(weight(candidate, 5, 2), 0),
		   "component mask restricts destinations");
	auto locked = edit;
	locked.weightTransfer.lockedJoints = {"root"};
	candidate = mesh;
	expect(applyModelEdit(&candidate, locked, nullptr, &error) && rigBytes(candidate) == bytes,
		   "fully locked destination stays byte exact");
	auto blend = edit;
	blend.weightTransfer.strength = .5;
	candidate = mesh;
	expect(applyModelEdit(&candidate, blend, nullptr, &error) && close(weight(candidate, 3, 2), .375),
		   "partial strength blends distributions");
	// A second surface inside the source triangle tests true barycentric transfer.
	auto layered = mesh;
	auto target = mesh.surfaces[0];
	target.name = "armour";
	target.index = 1;
	target.vertexCount = 3;
	target.triangles = {{0, 1, 2}};
	target.texCoords = {{0, 0}, {1, 0}, {0, 1}};
	target.frames = {{{{2, 1, 1}, {2, 1.5, 1}, {2, 1, 1.5}}, {{1, 0, 0}, {1, 0, 0}, {1, 0, 0}}}};
	target.skinning = {};
	layered.surfaces.append(target);
	for (int f = 1; f < layered.frames.size(); ++f)
		layered.surfaces[1].frames.append(target.frames[0]);
	updateEditableModelMetadata(&layered);
	ModelEdit copy;
	copy.kind = ModelEditKind::TransferSkinWeights;
	copy.selection.surface = 1;
	copy.weightTransfer.maximumDistance = .001;
	candidate = layered;
	expect(applyModelEdit(&candidate, copy, nullptr, &error), "transfer binds a separate static surface");
	if (!error.isEmpty())
		std::cerr << error.toStdString() << '\n';
	expect(close(weight(candidate, 0, 1, 1), .625) && close(candidate.surfaces[1].frames[2].positions[0].y, 2.25),
		   "triangle interpolation yields analytic weights and posed motion");
	expect(rigBytes(candidate).contains("armour") && candidate.surfaces[1].texCoords[1].u == 1,
		   "destination surface metadata and UVs survive");
	auto copyVertex = copy;
	copyVertex.weightTransfer.method = ModelWeightTransferMethod::Vertex;
	candidate = layered;
	expect(!applyModelEdit(&candidate, copyVertex, nullptr, &error) && rigBytes(candidate) == rigBytes(layered),
		   "vertex sampling respects strict distance independently of triangles");
	copyVertex.weightTransfer.maximumDistance = 10;
	candidate = layered;
	expect(applyModelEdit(&candidate, copyVertex, nullptr, &error) && close(weight(candidate, 0, 1, 1), .75),
		   "nearest vertex supports nonidentical topology");
	// Coincident vertices with conflicting weights must never depend on source order.
	auto seam = mesh;
	auto &s = seam.surfaces[0];
	s.vertexCount = 10;
	s.triangles.append({7, 8, 9});
	s.texCoords.append({0, 0});
	s.texCoords.append({1, 0});
	s.texCoords.append({0, 1});
	s.frames.resize(1);
	s.frames[0].positions.append({2, 0, 0});
	s.frames[0].positions.append({2, 4, 0});
	s.frames[0].positions.append({2, 0, 4});
	for (int i = 0; i < 3; ++i)
		s.frames[0].normals.append({0, 1, 0});
	expect(modelSkinningFromBindPose(seam.skeleton, s.frames[0].positions, s.frames[0].normals,
									 {0, 1, 0, 1, 0, 1, 0, 2, 0, 2, 0, 2, 0, 1, 0, 1, 0, 1, 0, 1},
									 {.25, .75, 1, 0, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0}, 2, &s.skinning) &&
			   bakeModelSkeleton(&seam),
		   "build conflicting seam fixture");
	updateEditableModelMetadata(&seam);
	auto seamEdit = subset;
	seamEdit.weightTransfer.method = ModelWeightTransferMethod::Vertex;
	candidate = seam;
	const auto seamBytes = rigBytes(seam);
	expect(!applyModelEdit(&candidate, seamEdit, nullptr, &error) && error.contains("equally close") && rigBytes(candidate) == seamBytes,
		   "ambiguous seam transfer is atomic");
	seamEdit.weightTransfer.normalAngle = 5;
	expect(applyModelEdit(&candidate, seamEdit, nullptr, &error) && close(weight(candidate, 3, 2), .75),
		   "normal filter selects the correct reflected seam");
	auto triangleSeam = seamEdit;
	triangleSeam.weightTransfer.method = ModelWeightTransferMethod::Triangle;
	candidate = seam;
	expect(applyModelEdit(&candidate, triangleSeam, nullptr, &error) && close(weight(candidate, 3, 2), .75),
		   "triangle correspondence respects the reflected normal filter");
	triangleSeam.weightTransfer.normalAngle = 180;
	candidate = seam;
	expect(!applyModelEdit(&candidate, triangleSeam, nullptr, &error) && rigBytes(candidate) == seamBytes,
		   "overlapping triangles with conflicting weights refuse atomically");
	triangleSeam.weightTransfer.joints = {{"arm.L", "root"}, {"arm.R", "root"}};
	expect(applyModelEdit(&candidate, triangleSeam, nullptr, &error) && rigBytes(candidate) == seamBytes,
		   "equivalent mapped seam samples produce a deterministic no-op");
	// Refusal after an earlier match must roll back the entire candidate.
	auto unmatched = mesh;
	unmatched.surfaces[0].skinning.influences[unmatched.surfaces[0].skinning.first[5]].offset.x -= 1;
	expect(bakeModelSkeleton(&unmatched), "prepare unmatched destination fixture");
	updateEditableModelMetadata(&unmatched);
	candidate = unmatched;
	const auto unmatchedBytes = rigBytes(candidate);
	expect(!applyModelEdit(&candidate, edit, nullptr, &error) && rigBytes(candidate) == unmatchedBytes,
		   "late unmatched vertex refuses the whole edit");
	auto skip = edit;
	skip.weightTransfer.skipUnmatched = true;
	expect(applyModelEdit(&candidate, skip, nullptr, &error) && close(weight(candidate, 3, 2), .75) && close(weight(candidate, 5, 0), 1),
		   "explicit skip keeps unmatched bound vertices");
	auto partial = copy;
	partial.selection.vertices = {0};
	candidate = layered;
	expect(!applyModelEdit(&candidate, partial, nullptr, &error) && rigBytes(candidate) == rigBytes(layered),
		   "partial transfer cannot create incomplete skinning");
	partial = copy;
	partial.weightTransfer.strength = .5;
	candidate = layered;
	expect(!applyModelEdit(&candidate, partial, nullptr, &error), "unbound destination requires full strength");
	auto mapped = edit;
	mapped.weightTransfer.joints = {{"arm.L", "root"}, {"arm.R", "root"}};
	candidate = mesh;
	expect(applyModelEdit(&candidate, mapped, nullptr, &error) && close(weight(candidate, 3, 0), 1),
		   "explicit many-to-one joint mapping merges weights");
	auto lockedMesh = mesh;
	ModelEdit prepare;
	prepare.kind = ModelEditKind::PaintSkinWeights;
	ModelWeightStroke stroke;
	stroke.joint = "root";
	stroke.value = .2;
	stroke.strength = 1;
	stroke.remainderJoint = "arm.R";
	stroke.samples = {{3, 1}};
	prepare.weightStrokes = {stroke};
	expect(applyModelEdit(&lockedMesh, prepare, nullptr, &error), "prepare fractional lock budget");
	stroke.joint = "arm.L";
	stroke.value = .5;
	stroke.lockedJoints = {"root"};
	prepare.weightStrokes = {stroke};
	expect(applyModelEdit(&lockedMesh, prepare, nullptr, &error), "prepare three-way destination binding");
	const auto lockedOffset = lockedMesh.surfaces[0].skinning.influences[lockedMesh.surfaces[0].skinning.first[3]];
	auto lockedRequest = edit;
	lockedRequest.selection.vertices = {3};
	lockedRequest.weightTransfer.lockedJoints = {"root"};
	candidate = lockedMesh;
	expect(applyModelEdit(&candidate, lockedRequest, nullptr, &error) && close(weight(candidate, 3, 0), .2) &&
			   close(weight(candidate, 3, 2), .8) && close(weight(candidate, 3, 1), 0),
		   "transfer fills only the unlocked budget");
	const auto retainedLock = candidate.surfaces[0].skinning.influences[candidate.surfaces[0].skinning.first[3]];
	expect(retainedLock.weight == lockedOffset.weight && retainedLock.offset.x == lockedOffset.offset.x &&
			   retainedLock.normalOffset.x == lockedOffset.normalOffset.x,
		   "locked raw influence stays exact");
	auto allLocks = lockedRequest;
	allLocks.weightTransfer.lockedJoints = {"root", "arm.L", "arm.R"};
	candidate = lockedMesh;
	expect(applyModelEdit(&candidate, allLocks, nullptr, &error) && rigBytes(candidate) == rigBytes(lockedMesh),
		   "all locked float influences preserve normalisation round-off exactly");
	auto invalidEdge = edit;
	invalidEdge.selection.edges = {modelEdge(0, 6)};
	candidate = mesh;
	expect(!applyModelEdit(&candidate, invalidEdge, nullptr, &error) && rigBytes(candidate) == bytes,
		   "nonexistent component edges cannot define transfer scope");
	auto edgeScope = edit;
	edgeScope.selection.edges = {modelEdge(3, 5)};
	candidate = mesh;
	expect(applyModelEdit(&candidate, edgeScope, nullptr, &error) && close(weight(candidate, 3, 2), .75) &&
			   close(weight(candidate, 5, 2), 1),
		   "valid edge scope expands to endpoint vertices");
	auto faceScope = edit;
	faceScope.selection.faces = {1};
	candidate = mesh;
	expect(applyModelEdit(&candidate, faceScope, nullptr, &error) && close(weight(candidate, 3, 2), .75),
		   "face scope expands to destination corners");
	auto shifted = mesh;
	for (auto &w : shifted.surfaces[0].skinning.influences)
		w.offset.x += 1;
	expect(bakeModelSkeleton(&shifted), "prepare translated mirror plane");
	updateEditableModelMetadata(&shifted);
	auto shiftedEdit = edit;
	shiftedEdit.weightTransfer.plane = 1;
	expect(applyModelEdit(&shifted, shiftedEdit, nullptr, &error) && close(weight(shifted, 3, 2), .75) &&
			   close(shifted.surfaces[0].frames[0].positions[3].x, -1),
		   "offset mirror plane preserves translated bind coordinates");
	auto imported = lockedMesh;
	auto &importedSkin = imported.surfaces[0].skinning;
	const int importedFirst = importedSkin.first[3];
	importedSkin.influences[importedFirst].offset.x += 1;
	importedSkin.influences[importedFirst + 1].offset.x -= float(.2 / .3);
	expect(bakeModelSkeleton(&imported), "prepare distinct imported offsets");
	updateEditableModelMetadata(&imported);
	const auto importedBytes = rigBytes(imported);
	expect(!applyModelEdit(&imported, edit, nullptr, &error) && rigBytes(imported) == importedBytes,
		   "transfer refuses to erase distinct imported offset behaviour");
	auto dense = mesh;
	auto &denseSurface = dense.surfaces[0];
	denseSurface.frames.resize(1);
	for (int i = 0; i < 8192; ++i)
		for (int side : {1, -1})
		{
			denseSurface.frames[0].positions.append({float(side * 2), 10 + float(i) * .01f, 0});
			denseSurface.frames[0].normals.append({float(side), 0, 0});
			denseSurface.texCoords.append({0, 0});
		}
	denseSurface.vertexCount = denseSurface.frames[0].positions.size();
	QVector<int> indices;
	QVector<float> values;
	for (const auto &p : denseSurface.frames[0].positions)
	{
		indices << 0 << (p.x > 0 ? 1 : 2);
		values << (p.x > 0 ? .25f : 1.f) << (p.x > 0 ? .75f : 0.f);
	}
	expect(modelSkinningFromBindPose(dense.skeleton, denseSurface.frames[0].positions, denseSurface.frames[0].normals, indices, values, 2,
									 &denseSurface.skinning) &&
			   bakeModelSkeleton(&dense),
		   "prepare dense correspondence fixture");
	updateEditableModelMetadata(&dense);
	auto denseEdit = edit;
	denseEdit.weightTransfer.method = ModelWeightTransferMethod::Vertex;
	candidate = dense;
	QElapsedTimer timer;
	timer.start();
	expect(applyModelEdit(&candidate, denseEdit, nullptr, &error) &&
			   close(weight(candidate, candidate.surfaces[0].vertexCount - 1, 2), .75),
		   "BVH handles dense mirrored correspondence");
	expect(dense.surfaces[0].vertexCount == 16391, "dense fixture retains its complete vertex population");
	std::cout << "Dense transfer: " << dense.surfaces[0].vertexCount << " vertices in " << timer.elapsed() << " ms\n";
	bool cancelDense = false;
	ModelWorkControl denseControl;
	denseControl.progress = [&](ModelWorkPhase phase, qint64 complete, qint64)
	{
		if (phase == ModelWorkPhase::Editing && complete > 1024)
			cancelDense = true;
	};
	denseControl.cancelled = [&] { return cancelDense; };
	candidate = dense;
	expect(!applyModelEdit(&candidate, denseEdit, nullptr, &error, denseControl) && rigBytes(candidate) == rigBytes(dense),
		   "dense index cancellation is atomic");
	for (int variant = 0; variant < 16; ++variant)
	{
		auto bad = edit;
		auto &o = bad.weightTransfer;
		switch (variant)
		{
		case 0:
			o.sourceSurface = -1;
			break;
		case 1:
			o.mirrorAxis = 3;
			break;
		case 2:
			o.side = ModelWeightTransferSide(3);
			break;
		case 3:
			o.method = ModelWeightTransferMethod(2);
			break;
		case 4:
			o.maximumDistance = -1;
			break;
		case 5:
			o.normalAngle = 181;
			break;
		case 6:
			o.strength = 0;
			break;
		case 7:
			o.strength = std::numeric_limits<double>::quiet_NaN();
			break;
		case 8:
			o.plane = std::numeric_limits<double>::infinity();
			break;
		case 9:
			o.centreTolerance = -1;
			break;
		case 10:
			o.joints["missing"] = "root";
			break;
		case 11:
			o.joints["root"] = "missing";
			break;
		case 12:
			o.lockedJoints = {"root", "root"};
			break;
		case 13:
			bad.selection.vertices = {-1};
			break;
		case 14:
			bad.frame = 0;
			break;
		case 15:
			o.mirrorAxis = -1;
			break;
		}
		candidate = mesh;
		expect(!applyModelEdit(&candidate, bad, nullptr, &error) && rigBytes(candidate) == bytes,
			   "invalid transfer parameters refuse atomically");
	}
	auto unrelated = edit;
	unrelated.kind = ModelEditKind::RecalculateNormals;
	candidate = mesh;
	expect(!applyModelEdit(&candidate, unrelated, nullptr, &error), "other edits refuse transfer options");
	for (int variant = 0; variant < 3; ++variant)
	{
		candidate = mesh;
		if (variant == 0)
			candidate.surfaces[0].frames[2].positions[3].z += 1;
		if (variant == 1)
			candidate.surfaces[0].frames[2].normals[3] = {0, 0, 1};
		if (variant == 2)
			candidate.skeleton.bakedClipForFrame.clear();
		updateEditableModelMetadata(&candidate);
		const auto before = rigBytes(candidate);
		expect(!applyModelEdit(&candidate, edit, nullptr, &error) && rigBytes(candidate) == before,
			   "independent geometry, normals and missing pose mapping are protected");
	}
	for (int threshold : {0, 10, 20})
	{
		candidate = mesh;
		int polls = 0;
		ModelWorkControl control;
		control.cancelled = [&] { return ++polls > threshold; };
		expect(!applyModelEdit(&candidate, edit, nullptr, &error, control) && rigBytes(candidate) == bytes,
			   "early and late cancellation preserve the source");
	}
	const auto input = QDir(tmp.path()).filePath("source.mesh.json"), output = QDir(tmp.path()).filePath("result.mesh.json"),
			   map = QDir(tmp.path()).filePath("joints.json");
	expect(document.setMesh(mesh, &error) && document.save(input, false, &error), "save source fixture");
	expect(document.setMesh(mirrored, &error) && document.save(output, false, &error), "save mirrored source");
	ModelDocument reopened;
	expect(reopened.load(output, &error) && rigBytes(reopened.mesh()) == rigBytes(mirrored), "save and reopen retain transferred skinning");
	const auto recovery = writeModelRecovery({mirrored, {}}, tmp.path(), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	ModelRecoverySnapshot restored;
	expect(!recovery.isEmpty() && inspectModelRecovery(recovery, &restored).isValid() && rigBytes(restored.mesh) == rigBytes(mirrored),
		   "recovery retains weights and animation");
	for (const auto &format : {QStringLiteral("md5mesh"), QStringLiteral("iqm"), QStringLiteral("md3")})
	{
		const auto encoded = exportEditableModel(mirrored, format, 0, &error);
		const auto decoded = decodeModelMesh("mirror." + format, encoded);
		expect(!encoded.isEmpty() && decoded.isValid(), "native export handoff accepts transferred weights");
		if (format == "iqm")
		{
			ModelFrameGeometry pose;
			expect(skinModelSurface(decoded.surfaces[0], decoded.skeleton.clips[0].frames[1], &pose, &error) &&
					   std::abs(pose.positions[3].y + 2.25) < .02,
				   "IQM pose retains mirrored deformation within quantisation tolerance");
		}
	}
	expect(write(map, R"({"arm.L":"arm.R","arm.R":"arm.L"})"), "write exact joint map");
	const QStringList command{"vibestudio",
							  "--cli",
							  "model",
							  "weight-transfer",
							  input,
							  "--source-surface",
							  "0",
							  "--surface",
							  "0",
							  "--vertices",
							  "all",
							  "--mirror",
							  "x",
							  "--side",
							  "positive-to-negative",
							  "--joint-map",
							  map,
							  "--output",
							  output,
							  "--overwrite"};
	checkModelAuxiliaryOutput(command, "--joint-map", cli::runModelWeightTransfer, expect);
	expect(cli::runModelWeightTransfer(command + QStringList{"--dry-run"}).exitCode == 0, "CLI dry run accepts shared transfer");
	expect(cli::runModelWeightTransfer(command).exitCode == 0 && reopened.load(output, &error) &&
			   rigBytes(reopened.mesh()) == rigBytes(mirrored),
		   "CLI mapping output exactly matches document service");
	for (const QStringList &options : {QStringList{"--surface", "0"},
									   {"--unknown"},
									   {"--strength", "nan"},
									   {"--method", "invalid"},
									   {"--vertices", "0,0"},
									   {"--normal-angle", "-1"}})
		expect(cli::runModelWeightTransfer(command + options).exitCode != 0, "CLI rejects repeated, unknown or invalid values");
	expect(write(map, R"({"root":4})") && cli::runModelWeightTransfer(command).exitCode == 2, "joint map refuses non-string destinations");
	write(map, R"({"arm.L":"arm.R","arm.R":"arm.L"})");
	if (argc > 1)
	{
		QProcess process;
		process.start(QString::fromLocal8Bit(argv[1]), (command + QStringList{"--dry-run", "--json"}).mid(1));
		expect(process.waitForFinished(30000) && process.exitCode() == 0 && process.readAllStandardOutput().contains("sourceSurface"),
			   "actual executable exposes weight-transfer CLI");
	}
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
