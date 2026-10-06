#include "core/model_recovery.h"
#include "tests/model_uv_islands_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <iostream>
#include <limits>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0;
bool expect(bool value, const char *message)
{
	++checks;
	if (!value)
		std::cerr << "FAIL: " << message << '\n';
	return value;
}
QByteArray read(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("uv-islands-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	bool ok = true;
	QString error;
	const auto original = uvIslandsFixture();
	const auto &source = original.surfaces[0];
	ModelUvTopology topology;
	ok &= expect(validateEditableModel(original).isEmpty() && buildModelUvTopology(source, &topology, &error) &&
					 topology.islands.size() == 3 && topology.islands[0].faces == QVector<int>{0, 1} &&
					 topology.islands[1].faces == QVector<int>{2, 3},
				 "fixture has two marked shared boundaries and an unused vertex");
	ModelDocument document;
	ok &= expect(document.setMesh(original, &error), "open animated fixture");
	ModelEdit edit;
	edit.kind = ModelEditKind::TransformUv;
	edit.selection.faces = {0, 1, 2, 3};
	edit.uvPivotMode = ModelUvPivot::IndividualIslands;
	edit.uvScale = {2, .5f};
	edit.uvRotation = 90;
	edit.uvOffset = {.0625f, -.0625f};
	edit.uvTranslationGrid = .125;
	document.setSelection(edit.selection);
	ok &= expect(document.edit(edit, &error), "rotate and scale each island with shared snapped offset");
	const auto transformed = document.mesh();
	const auto &surface = transformed.surfaces[0];
	ok &= expect(surface.vertexCount == 12 && uvSamePoses(source, surface),
				 "four shared corners split with exact geometry and normals in every pose");
	for (int face = 0; face < 4; ++face)
	{
		const auto before = source.triangles[face], after = surface.triangles[face];
		const int old[]{before.a, before.b, before.c}, now[]{after.a, after.b, after.c};
		const float centre = face < 2 ? 1.f : 3.f;
		for (int corner = 0; corner < 3; ++corner)
		{
			const auto uv = source.texCoords[old[corner]];
			// Independent 90-degree oracle; centres remain at 1,1 and 3,1.
			ok &= expect(uvNear(surface.texCoords[now[corner]], {centre - (uv.v - 1) * .5f + .125f, 1 + (uv.u - centre) * 2 - .125f}),
						 "selected corners use their island's centre, scale, quarter turn and snapped offset");
		}
	}
	for (int vertex : {2, 3, 6, 7})
		ok &= expect(uvNear(surface.texCoords[vertex], source.texCoords[vertex]), "unselected face and unused UVs stay fixed");
	ok &= expect(surface.uvSeams.size() == 5 && buildModelUvTopology(surface, &topology, &error) && topology.islands.size() == 3,
				 "both sides of split seams retain their marks and island identities");
	auto metadata = transformed;
	metadata.surfaces[0] = source;
	updateEditableModelMetadata(&metadata);
	ok &= expect(uvIslandBytes(metadata) == uvIslandBytes(original),
				 "other surfaces, materials, tags and animation metadata survive exactly");
	ok &= expect(document.selection() == edit.selection && document.undo() && !document.canUndo() &&
					 uvIslandBytes(document.mesh()) == uvIslandBytes(original) && document.selection() == edit.selection,
				 "one undo restores source and complete island selection");
	ok &= expect(document.redo() && uvIslandBytes(document.mesh()) == uvIslandBytes(transformed), "redo restores all isolated UV corners");
	ok &= expect(document.edit(edit, &error) && document.mesh().surfaces[0].vertexCount == 12,
				 "subsequent island transforms do not accumulate redundant split vertices");
	auto reordered = original;
	auto reorderedEdit = edit;
	reorderedEdit.selection.faces = {3, 2, 1, 0};
	ok &= expect(applyModelEdit(&reordered, reorderedEdit, nullptr, &error) && uvIslandBytes(reordered) == uvIslandBytes(transformed),
				 "selection insertion order cannot change deterministic output");
	const auto input = QDir(temporary.path()).filePath("source.mesh.json");
	const auto saved = QDir(temporary.path()).filePath("islands.mesh.json");
	ModelDocument loaded;
	ok &= expect(document.setMesh(transformed, &error) && document.save(saved, false, &error) && loaded.load(saved, &error) &&
					 uvIslandBytes(loaded.mesh()) == uvIslandBytes(transformed),
				 "save and reopen retains splits, UVs and every pose");
	ModelRecoverySnapshot snapshot;
	snapshot.mesh = transformed;
	snapshot.selection = edit.selection;
	const auto recovery = writeModelRecovery(snapshot, temporary.path(), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	ModelRecoverySnapshot recovered;
	ok &= expect(!recovery.isEmpty() && inspectModelRecovery(recovery, &recovered).isValid() && recovered.selection == edit.selection &&
					 uvIslandBytes(recovered.mesh) == uvIslandBytes(transformed),
				 "recovery retains independent UV mappings and selection");
	const auto native = exportEditableModel(transformed, QStringLiteral("md3"), 0, &error);
	const auto decoded = decodeModelMesh(QStringLiteral("islands.md3"), native);
	ok &= expect(!native.isEmpty() && decoded.surfaces.size() == 2 && decoded.frameCount == 2 && decoded.surfaces[0].vertexCount == 12,
				 "native MD3 keeps seam splits across animation frames");
	for (int vertex = 0; vertex < surface.vertexCount && !decoded.surfaces.isEmpty(); ++vertex)
		ok &= expect(uvNear(surface.texCoords[vertex], decoded.surfaces[0].texCoords[vertex]), "native export retains each transformed UV");
	ModelEdit simple;
	simple.kind = ModelEditKind::TransformUv;
	simple.selection = edit.selection;
	simple.uvPivotMode = ModelUvPivot::IndividualIslands;
	simple.uvScale = {-1, 2};
	auto mirrored = original;
	ok &= expect(applyModelEdit(&mirrored, simple, nullptr, &error) && uvSamePoses(source, mirrored.surfaces[0]) &&
					 uvNear(mirrored.surfaces[0].texCoords[0], {2, -1}),
				 "negative UV scale mirrors the chart without changing mesh winding");
	for (int plane = 0; plane < 3; ++plane)
	{
		auto projected = original;
		auto project = simple;
		project.kind = ModelEditKind::ProjectUv;
		project.frame = 1;
		project.projection = plane;
		project.uvScale = {2, 2};
		const ModelTexCoord expected[]{{-8, 13}, {-8, 1.5f}, {-13, 1.5f}};
		ok &= expect(applyModelEdit(&projected, project, nullptr, &error) && uvNear(projected.surfaces[0].texCoords[0], expected[plane]) &&
						 uvSamePoses(source, projected.surfaces[0]),
					 "projection derives each pivot from chosen plane and reference pose");
	}
	for (int failure = 0; failure < 7; ++failure)
	{
		auto invalid = simple;
		if (failure == 0)
			invalid.selection.faces = {0};
		if (failure == 1)
			invalid.selection.vertices = {0};
		if (failure == 2)
			invalid.selection.edges = {{0, 1}};
		if (failure == 3)
			invalid.selection.faces.clear();
		if (failure == 4)
			invalid.uvScale.u = 0;
		if (failure == 5)
			invalid.uvScale.u = 1000000;
		if (failure == 6)
			invalid.kind = ModelEditKind::DetachUv;
		auto rejected = original;
		ModelSelection returned;
		returned.faces = {99};
		ok &= expect(!applyModelEdit(&rejected, invalid, &returned, &error) && !error.isEmpty() &&
						 uvIslandBytes(rejected) == uvIslandBytes(original) && returned.faces == QSet<int>{99},
					 "invalid selection, mode or values reject without partial geometry or selection publication");
	}
	auto expanded = original;
	auto expansion = simple;
	expansion.selection.faces.clear();
	expansion.selection.vertices = {1};
	expansion.uvIslands = true;
	ModelSelection selection;
	ok &= expect(applyModelEdit(&expanded, expansion, &selection, &error) && selection.faces == simple.selection.faces &&
					 selection.vertices.isEmpty() && uvIslandBytes(expanded) == uvIslandBytes(mirrored),
				 "explicit expansion converts touched components to complete islands");
	ModelUvIslandTransform options;
	options.vertexLimit = 11;
	ModelSurface result = original.surfaces[1];
	ok &= expect(!transformModelUvIslands(source, simple.selection.faces, options, &result, &error) &&
					 result.name == QStringLiteral("untouched"),
				 "preflight refuses split capacity before publishing output");
	options.vertexLimit = modelDocumentMaxVertices;
	options.scale.u = std::numeric_limits<float>::quiet_NaN();
	ok &= expect(!transformModelUvIslands(source, simple.selection.faces, options, &result, &error),
				 "service rejects nonfinite transform values");
	options.scale.u = 1;
	bool cancelled = false;
	ModelWorkControl cancellation{[&] { return cancelled; },
								  [&](ModelWorkPhase phase, qint64 completed, qint64) {
									  if (phase == ModelWorkPhase::Editing && completed > 0)
										  cancelled = true;
								  }};
	ok &= expect(!transformModelUvIslands(source, simple.selection.faces, options, &result, &error, cancellation) && cancelled &&
					 result.name == QStringLiteral("untouched"),
				 "cancellation after candidate work keeps output unchanged");
	bool edited = false;
	cancelled = false;
	cancellation.progress = [&](ModelWorkPhase phase, qint64 completed, qint64) {
		if (phase == ModelWorkPhase::Editing && completed > 0)
			edited = true;
		if (phase == ModelWorkPhase::Validating && edited)
			cancelled = true;
	};
	auto interrupted = original;
	selection.faces = {99};
	ok &= expect(!applyModelEdit(&interrupted, simple, &selection, &error, cancellation) && cancelled && edited &&
					 uvIslandBytes(interrupted) == uvIslandBytes(original) && selection.faces == QSet<int>{99},
				 "cancellation during final validation cannot publish transformed candidate or selection");
	{
		auto full = original;
		auto &other = full.surfaces[1];
		other.vertexCount = modelDocumentMaxVertices - source.vertexCount;
		other.texCoords.resize(other.vertexCount);
		for (auto &frame : other.frames)
		{
			frame.positions.resize(other.vertexCount);
			frame.normals.resize(other.vertexCount, ModelVec3{0, 0, 1});
		}
		updateEditableModelMetadata(&full);
		const auto before = uvIslandBytes(full);
		ok &= expect(validateEditableModel(full).isEmpty() && !applyModelEdit(&full, simple, nullptr, &error) &&
						 uvIslandBytes(full) == before,
					 "capacity includes unused vertices on other surfaces and refuses the whole edit");
	}
	// Three incident faces do not join charts; marked and point-only boundaries use
	// the same deterministic per-island splitting path.
	auto nonmanifold = original;
	nonmanifold.surfaces[0].triangles = {{0, 1, 2}, {1, 0, 3}, {0, 1, 5}};
	nonmanifold.surfaces[0].uvSeams = {{0, 1}};
	updateEditableModelMetadata(&nonmanifold);
	ok &= expect(validateEditableModel(nonmanifold).isEmpty(), "nonmanifold fixture has valid noncollapsed poses");
	auto nm = simple;
	nm.selection.faces = {0, 1, 2};
	const auto nmBefore = nonmanifold;
	ok &= expect(applyModelEdit(&nonmanifold, nm, nullptr, &error) && nonmanifold.surfaces[0].vertexCount == 12 &&
					 nonmanifold.surfaces[0].uvSeams.size() == 3 && uvSamePoses(nmBefore.surfaces[0], nonmanifold.surfaces[0]),
				 "nonmanifold boundary copies each shared corner independently");
	auto point = original;
	point.surfaces[0].triangles = {{0, 1, 2}, {2, 4, 5}};
	point.surfaces[0].uvSeams.clear();
	updateEditableModelMetadata(&point);
	nm.selection.faces = {0, 1};
	ok &= expect(applyModelEdit(&point, nm, nullptr, &error) && point.surfaces[0].vertexCount == 9,
				 "point-only island contact receives a separate UV index");
	if (argc == 2)
	{
		ok &= expect(document.setMesh(original, &error) && document.save(input, false, &error), "save CLI source fixture");
		const auto originalBytes = read(input);
		const auto output = QDir(temporary.path()).filePath("cli.mesh.json");
		QJsonObject json;
		const auto cli = [&](QStringList arguments, int expected) {
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			arguments.prepend("--cli");
			arguments << "--json" << "--settings-file" << QDir(temporary.path()).filePath("settings.ini");
			process.start(app.arguments()[1], arguments);
			const bool done = process.waitForStarted(10000) && process.waitForFinished(30000);
			const auto bytes = process.readAllStandardOutput();
			json = QJsonDocument::fromJson(bytes).object();
			if (!done || process.exitCode() != expected)
				std::cerr << bytes.constData() << process.readAllStandardError().constData();
			return done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected && !json.isEmpty();
		};
		const QStringList base{"model", "edit", input, "--operation", "uv-transform", "--output", output};
		const QStringList values{"--uv-pivot-mode", "islands",		  "--uv-scale", "2,0.5", "--uv-rotation", "90",
								 "--uv-offset",		"0.0625,-0.0625", "--uv-grid",	"0.125"};
		ok &= expect(cli(base + values + QStringList{"--faces", "0,1,2,3", "--dry-run"}, 0) && !QFileInfo::exists(output),
					 "CLI dry run checks complete islands without writing");
		ok &= expect(cli(base + values + QStringList{"--vertices", "1", "--uv-islands"}, 0) && loaded.load(output, &error) &&
						 uvIslandBytes(loaded.mesh()) == uvIslandBytes(transformed),
					 "CLI expands components and writes same deterministic island transform");
		const auto outputBytes = read(output);
		ok &= expect(cli(base + values + QStringList{"--faces", "0,1,2,3"}, 1) && read(output) == outputBytes,
					 "CLI protects existing output");
		ok &= expect(cli(base + values + QStringList{"--faces", "0", "--overwrite"}, 4) && read(output) == outputBytes,
					 "CLI partial island is an atomic validation failure");
		for (const auto &extra : QVector<QStringList>{{"--uv-pivot", "0,0"}, {"--uv-pivot-mode", "selection"}})
			ok &= expect(cli(base + values + extra + QStringList{"--faces", "0,1,2,3", "--overwrite"}, 2),
						 "CLI rejects conflicting or repeated pivot options");
		ok &= expect(cli({"model", "edit", input, "--operation", "normals", "--uv-pivot-mode", "islands", "--output", output}, 2),
					 "CLI rejects island pivot on unrelated operations");
		ok &= expect(cli({"model", "edit", input, "--operation", "uv-project", "--faces", "0,1,2,3", "--uv-pivot-mode", "islands",
						  "--projection", "xz", "--frame", "1", "--output", output, "--overwrite"},
						 0),
					 "CLI supports island-centred projection in a reference pose");
		ok &= expect(read(input) == originalBytes, "CLI leaves source bytes unchanged");
	}
	if (!ok)
		std::cerr << error.toStdString() << '\n';
	std::cout << checks << " UV island transform checks\n";
	return ok ? 0 : 1;
}
