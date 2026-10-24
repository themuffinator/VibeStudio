#include "cli/model_tools.h"
#include "core/model_edge_slide.h"
#include "core/model_recovery.h"
#include "tests/model_edge_slide_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <algorithm>
#include <iostream>
#include <limits>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0, failures = 0;
bool expect(bool value, const char *message)
{
	++checks;
	if (!value)
	{
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
	return value;
}
bool near(double a, double b)
{
	return std::abs(a - b) < 1e-5;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temp(QDir(root).filePath("edge-slide-XXXXXX"));
	if (!temp.isValid())
		return 1;
	QString error;
	const auto source = edgeSlideGrid();
	const auto original = slideBytes(source);
	const auto edit = edgeSlideEdit();
	ModelEdgeSlidePlan plan;
	expect(planModelEdgeSlide(source.surfaces[0], edit.selection.edges, 0, 40, &plan, &error), "plan a full open chain");
	expect(plan.chains == 1 && plan.vertices.size() == 5, "plan includes both endpoints once");
	bool rails = true;
	for (const auto &v : plan.vertices)
		rails &= v.rails[0] == v.vertex - 1 && v.rails[1] == v.vertex + 1;
	expect(rails, "positive direction follows winding side along whole chain");
	ModelDocument document;
	expect(document.setMesh(source, &error) && document.edit(edit, &error), "document applies edge slide");
	auto changed = document.mesh();
	for (int f = 0; f < 2; ++f)
	{
		bool poses = true;
		for (int v = 0; v < source.surfaces[0].vertexCount; ++v)
		{
			const auto before = source.surfaces[0].frames[f].positions[v], after = changed.surfaces[0].frames[f].positions[v];
			poses &= near(after.x, before.x - (v % 5 == 2 ? 2 * (f + 1) : 0)) && after.y == before.y && after.z == before.z;
		}
		expect(poses, "selected geometric vertices follow original rails in every pose");
	}
	expect(std::equal(changed.surfaces[0].triangles.cbegin(), changed.surfaces[0].triangles.cend(), source.surfaces[0].triangles.cbegin(),
					  source.surfaces[0].triangles.cend(),
					  [](const auto &a, const auto &b) { return a.a == b.a && a.b == b.b && a.c == b.c; }) &&
			   sameSlideUvs(changed.surfaces[0].texCoords, source.surfaces[0].texCoords),
		   "topology and attached UV coordinates stay exact");
	expect(document.selection().edges == edit.selection.edges && document.selection().vertices.isEmpty(), "slide keeps edge selection");
	const auto resultBytes = slideBytes(changed);
	expect(document.undo() && slideBytes(document.mesh()) == original && document.redo() && slideBytes(document.mesh()) == resultBytes,
		   "undo and redo preserve complete source");
	for (int scope : {-1, 0, 1})
	{
		auto candidate = source;
		auto op = edit;
		op.frame = scope;
		op.tool.slide = -.5;
		expect(applyModelEdit(&candidate, op, nullptr, &error), "negative slide honours pose scope");
		for (int f = 0; f < 2; ++f)
			expect(near(candidate.surfaces[0].frames[f].positions[2].x,
						source.surfaces[0].frames[f].positions[2].x + (scope < 0 || scope == f ? 4 * (f + 1) : 0)),
				   "negative side is consistent, other pose unchanged");
	}
	for (int reference : {0, 1})
	{
		auto candidate = source;
		auto op = edit;
		op.tool.slideByDistance = true;
		op.tool.offset = 2;
		op.tool.referenceFrame = reference;
		expect(applyModelEdit(&candidate, op, nullptr, &error), "fixed distance uses reference pose");
		expect(near(candidate.surfaces[0].frames[0].positions[2].x, 16 - 2.0 / (reference + 1)) &&
				   near(candidate.surfaces[0].frames[1].positions[2].x, 32 - 4.0 / (reference + 1)),
			   "other poses use same reference-derived fraction");
	}
	// Closed loop with a split UV seam at the cylinder wrap.
	auto tube = edgeSlideTube();
	tube.surfaces[0].uvSeams = {modelEdge(9, 10), modelEdge(9, 18)};
	for (auto &frame : tube.surfaces[0].frames)
	{
		frame.normals[9] = {float(std::cos(std::numbers::pi / 8)), float(std::sin(std::numbers::pi / 8)), 0};
		frame.normals[17] = {frame.normals[9].x, -frame.normals[9].y, 0};
	}
	const auto seamMarks = tube.surfaces[0].uvSeams;
	auto loop = edit;
	loop.selection.edges.clear();
	for (int i = 9; i < 17; ++i)
		loop.selection.edges.insert(modelEdge(i, i + 1));
	ModelEdgeSlidePlan loopPlan;
	expect(planModelEdgeSlide(tube.surfaces[0], loop.selection.edges, 0, 40, &loopPlan, &error) && loopPlan.vertices.size() == 8 &&
			   loopPlan.chains == 1,
		   "closed seam-split loop has eight geometric vertices");
	const auto tubeUvs = tube.surfaces[0].texCoords;
	expect(applyModelEdit(&tube, loop, nullptr, &error), "slide a closed seam-split loop");
	bool closed = true;
	for (int f = 0; f < 2; ++f)
	{
		const auto &p = tube.surfaces[0].frames[f].positions;
		for (int i = 9; i <= 17; ++i)
			closed &= near(p[i].z, p[9].z) && std::abs(p[i].z - (8 + f * 2)) > 1;
		closed &= p[9].x == p[17].x && p[9].y == p[17].y && p[9].z == p[17].z;
	}
	expect(closed && sameSlideUvs(tube.surfaces[0].texCoords, tubeUvs) && tubeUvs[9].u != tubeUvs[17].u,
		   "seam copies move together while independent UVs survive");
	expect(tube.surfaces[0].uvSeams == seamMarks && tube.surfaces[0].frames[0].normals[9].y > .1 &&
			   tube.surfaces[0].frames[0].normals[17].y < -.1,
		   "explicit seam marks and distinct hard normals survive the closed-loop edit");
	// Rails are planned in one pose, but every edited pose must pass the fold guard.
	auto folded = source;
	for (int y = 0; y <= 4; ++y)
		folded.surfaces[0].frames[1].positions[y * 5 + 3].x = 20;
	updateEditableModelMetadata(&folded);
	const auto foldedBytes = slideBytes(folded);
	auto foldEdit = edit;
	foldEdit.tool.slide = .9;
	expect(!applyModelEdit(&folded, foldEdit, nullptr, &error) && error.contains("sample 1") && slideBytes(folded) == foldedBytes,
		   "a face reversed only in a later pose refuses the entire edit");
	foldEdit.frame = 0;
	expect(applyModelEdit(&folded, foldEdit, nullptr, &error), "a safe local edit does not reject an untouched pose");
	auto reversed = source;
	for (auto &triangle : reversed.surfaces[0].triangles)
		std::swap(triangle.b, triangle.c);
	expect(applyModelEdit(&reversed, edit, nullptr, &error) && near(reversed.surfaces[0].frames[0].positions[2].x, 18),
		   "reversing every face reverses the winding side consistently");
	auto nonmanifold = source;
	nonmanifold.surfaces[0].triangles.append(nonmanifold.surfaces[0].triangles[2]);
	updateEditableModelMetadata(&nonmanifold);
	const auto nonmanifoldBytes = slideBytes(nonmanifold);
	expect(!applyModelEdit(&nonmanifold, edit, nullptr, &error) && slideBytes(nonmanifold) == nonmanifoldBytes,
		   "nonmanifold incident geometry refuses without publication");
	auto authored = source;
	authored.surfaces.append(authored.surfaces[0]);
	authored.surfaces[1].name = "untouched";
	authored.surfaces[0].skinPaths = {"models/slide/skin.pcx"};
	authored.tags = {{"tag_test", 0, {3, 4, 5}}, {"tag_test", 1, {6, 7, 8}}};
	authored.animations = {{"motion", 0, 2, 12}};
	authored.collisionBoxes = {{"body", {16, 16, 4}, {32, 32, 8}}};
	updateEditableModelMetadata(&authored);
	const auto authoredBefore = editableModelJson(authored);
	expect(applyModelEdit(&authored, edit, nullptr, &error), "slide a model with other surfaces and authored metadata");
	const auto authoredAfter = editableModelJson(authored);
	bool metadata = authoredBefore["surfaces"].toArray()[1] == authoredAfter["surfaces"].toArray()[1];
	for (const auto &key : {"tags", "animations", "collisionBoxes", "mdl"})
		metadata &= authoredBefore[key] == authoredAfter[key];
	expect(metadata && authored.surfaces[0].skinPaths == QStringList{"models/slide/skin.pcx"} && authored.tags.size() == 2 &&
			   authored.collisionBoxes.size() == 1,
		   "unselected surface, materials, tags, collision and animation metadata survive exactly");
	// A one-sided border permits its inward direction, and never fabricates a second rail.
	auto border = edgeSlideEdit(4, 4, 0);
	auto bordered = source;
	expect(applyModelEdit(&bordered, border, nullptr, &error) && near(bordered.surfaces[0].frames[0].positions[0].x, 2),
		   "border slides into its available face strip");
	border.tool.slide = -.25;
	bordered = source;
	expect(!applyModelEdit(&bordered, border, nullptr, &error) && slideBytes(bordered) == original,
		   "missing border side refuses atomically");
	// Multiple separate chains are deterministic under selector insertion order.
	auto multiple = edgeSlideEdit(4, 4, 1);
	multiple.selection.edges.unite(edgeSlideEdit(4, 4, 3).selection.edges);
	auto a = source, b = source;
	expect(applyModelEdit(&a, multiple, nullptr, &error), "separate chains slide together");
	QList<ModelEdge> reverse(multiple.selection.edges.cbegin(), multiple.selection.edges.cend());
	std::sort(reverse.begin(), reverse.end(), std::greater<ModelEdge>());
	multiple.selection.edges.clear();
	for (auto edge : reverse)
		multiple.selection.edges.insert(edge);
	expect(applyModelEdit(&b, multiple, nullptr, &error) && slideBytes(a) == slideBytes(b), "selector order does not alter geometry");
	// Invalid operations preserve both mesh and caller-provided result selection.
	QList<ModelEdit> invalid;
	auto bad = edit;
	bad.tool.slide = 1;
	invalid << bad;
	bad = edit;
	bad.tool.slide = -1;
	invalid << bad;
	bad = edit;
	bad.tool.slide = std::numeric_limits<double>::quiet_NaN();
	invalid << bad;
	bad = edit;
	bad.tool.slideByDistance = true;
	bad.tool.offset = 8;
	invalid << bad;
	bad = edit;
	bad.tool.referenceFrame = 2;
	invalid << bad;
	bad = edit;
	bad.frame = 2;
	invalid << bad;
	bad = edit;
	bad.selection.edges.clear();
	invalid << bad;
	bad = edit;
	bad.selection.edges = {modelEdge(0, 6)};
	invalid << bad;
	bad = edit;
	bad.selection.edges = {modelEdge(0, 24)};
	invalid << bad;
	bad = edit;
	bad.selection.edges.insert(modelEdge(12, 13));
	invalid << bad;
	bad = edit;
	bad.selection.edges = {modelEdge(6, 7), modelEdge(7, 12)};
	invalid << bad;
	bad = edit;
	bad.kind = ModelEditKind::SmoothVertices;
	bad.tool.slideByDistance = true;
	invalid << bad;
	for (const auto &op : invalid)
	{
		auto candidate = source;
		ModelSelection selection;
		selection.vertices = {24};
		const auto untouched = selection;
		expect(!applyModelEdit(&candidate, op, &selection, &error) && !error.isEmpty() && slideBytes(candidate) == original &&
				   selection == untouched,
			   "invalid geometry/options refuse without partial publication");
	}
	auto zero = edit;
	zero.tool.slide = 0;
	ModelDocument unchanged;
	expect(unchanged.setMesh(source, &error) && unchanged.edit(zero, &error) && !unchanged.isModified() && !unchanged.canUndo(),
		   "zero slide creates no undo entry");
	auto cancelled = source;
	ModelWorkControl cancel;
	cancel.cancelled = [] { return true; };
	expect(!applyModelEdit(&cancelled, edit, nullptr, &error, cancel) && slideBytes(cancelled) == original,
		   "cancellation leaves source intact");
	auto dense = edgeSlideGrid(128, 128);
	auto denseEdit = edgeSlideEdit(128, 128, 64);
	const auto denseBytes = slideBytes(dense);
	int callbacks = 0;
	cancel.cancelled = [&] { return callbacks > 5; };
	cancel.progress = [&](ModelWorkPhase phase, qint64, qint64)
	{
		if (phase == ModelWorkPhase::Editing)
			++callbacks;
	};
	expect(!applyModelEdit(&dense, denseEdit, nullptr, &error, cancel) && callbacks > 5 && slideBytes(dense) == denseBytes,
		   "mid-plan cancellation is atomic on dense geometry");
	QElapsedTimer timer;
	timer.start();
	expect(applyModelEdit(&dense, denseEdit, nullptr, &error), "dense two-pose grid slides successfully");
	std::cout << "16,641-vertex two-pose slide: " << timer.elapsed() << " ms\n";
	ModelRecoverySnapshot restored;
	const auto recovery =
		writeModelRecovery({changed, edit.selection}, temp.path(), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	expect(!recovery.isEmpty() && inspectModelRecovery(recovery, &restored).isValid() && slideBytes(restored.mesh) == resultBytes &&
			   restored.selection == edit.selection,
		   "recovery preserves slid poses and edge selection");
	for (const auto &format : QStringList{"md2", "md3"})
	{
		auto native = changed;
		native.surfaces[0].skinPaths = {"models/slide/skin.pcx"};
		const auto data = exportEditableModel(native, format, 0, &error);
		const auto decoded = decodeModelMesh("slid." + format, data);
		bool vertices = decoded.isValid() && decoded.frames.size() == 2;
		if (vertices)
			for (int f = 0; f < 2; ++f)
			{
				bool found = false;
				for (const auto &p : decoded.surfaces[0].frames[f].positions)
					found |= std::abs(p.x - 14 * (f + 1)) < .15 && std::abs(p.y) < .15;
				vertices &= found;
			}
		if (!expect(!data.isEmpty() && vertices, "native export preserves slid sampled geometry within format precision"))
			std::cerr << format.toStdString() << ": " << error.toStdString() << "; " << data.size() << " bytes, " << decoded.frames.size()
					  << " frames\n";
	}
	auto skinned = source;
	skinned.frames.resize(1);
	skinned.surfaces[0].frames.resize(1);
	updateEditableModelMetadata(&skinned);
	ModelEdit joint;
	joint.kind = ModelEditKind::AddJoint;
	joint.rig.name = "root";
	// The first authored joint binds all existing vertices rigidly to itself.
	const bool bound = applyModelEdit(&skinned, joint, nullptr, &error) && !skinned.surfaces[0].skinning.isEmpty();
	expect(bound, "prepare a rigidly skinned grid");
	if (bound)
	{
		const auto skinBefore = editableModelJson(skinned);
		expect(applyModelEdit(&skinned, edit, nullptr, &error), "edge slide accepts skinned source");
		const auto skinAfter = editableModelJson(skinned);
		expect(!skinBefore["skeleton"].toObject()["skinning"].toArray().isEmpty() && skinBefore["skeleton"] == skinAfter["skeleton"],
			   "skin influence indices, weights and skeleton are unchanged");
	}
	const auto path = temp.filePath("grid.mesh.json"), output = temp.filePath("slide.mesh.json");
	ModelDocument saved;
	expect(saved.setMesh(source, &error) && saved.save(path, false, &error), "save CLI fixture");
	const QStringList base{"vibestudio",		   "--cli",	  "model", "tool",	   path,  "--tool", "edge-slide", "--edges",
						   "2:7,7:12,12:17,17:22", "--slide", ".25",   "--output", output};
	const auto dry = cli::runModelTool(base + QStringList{"--dry-run"});
	expect(dry.exitCode == 0 && !QFile::exists(output) && !dry.payload["written"].toBool(), "CLI dry-run creates no output");
	expect(cli::runModelTool(base).exitCode == 0 && saved.load(output, &error) && slideBytes(saved.mesh()) == resultBytes,
		   "CLI and document produce identical source");
	expect(cli::runModelTool(base).exitCode != 0 && cli::runModelTool(base + QStringList{"--overwrite"}).exitCode == 0,
		   "CLI output overwrite is explicit");
	for (const auto &extra :
		 QList<QStringList>{{"--distance", "2"}, {"--slide", ".5"}, {"--vertices", "2"}, {"--snap"}, {"--frame", "bad"}})
		expect(cli::runModelTool(base + extra).exitCode != 0, "CLI rejects repeated and inapplicable values");
	auto distanceArgs = base;
	distanceArgs[distanceArgs.indexOf("--slide")] = "--distance";
	distanceArgs[distanceArgs.indexOf(".25")] = "2";
	expect(cli::runModelTool(distanceArgs + QStringList{"--overwrite"}).exitCode == 0 && saved.load(output, &error) &&
			   slideBytes(saved.mesh()) == resultBytes,
		   "CLI distance uses same plan and source writer");
	if (argc > 1)
	{
		QProcess process;
		process.start(QString::fromLocal8Bit(argv[1]), base.mid(1) + QStringList{"--overwrite", "--json"});
		expect(process.waitForFinished(30000) && process.exitCode() == 0 &&
				   QJsonDocument::fromJson(process.readAllStandardOutput()).object()["tool"] == "edge-slide",
			   "application dispatches edge-slide CLI");
	}
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
