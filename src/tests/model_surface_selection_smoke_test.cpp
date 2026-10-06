#include "core/model_surface_selection.h"
#include "core/model_recovery.h"
#include "tests/model_surfaces_test_helpers.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>
#include <cmath>
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
bool close(ModelVec3 a, ModelVec3 b)
{
	return std::hypot(double(a.x) - b.x, double(a.y) - b.y, double(a.z) - b.z) < .0001;
}
QByteArray read(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool write(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("surface-selection-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	bool ok = true;
	QString error;
	const auto original = surfaceFixture();
	ModelDocument document;
	ok &= expect(document.setMesh(original, &error), "open original multi-pose fixture");
	const auto source = QDir(temporary.path()).filePath("source.mesh.json");
	ok &= expect(document.save(source, false, &error), "save clean fixture before selecting");
	const auto digest = document.revisionFingerprint();
	ModelSelection selected;
	selected.surface = 2;
	selected.surfaces = {0, 2};
	document.setSelection(selected);
	ok &= expect(document.selection() == selected && !document.isModified() && !document.canUndo() &&
					 document.revisionFingerprint() == digest,
				 "surface selection does not dirty the document or create history");
	ModelVec3 pivot;
	ok &= expect(modelSurfaceSelectionPivot(original, {0, 2}, 0, ModelTransformPivot::SelectionCentre, {}, &pivot) &&
					 close(pivot, {14, 3.5f, 3}),
				 "one combined bounding box includes unused vertices and gaps between surfaces");
	ok &= expect(modelSurfaceSelectionPivot(original, {2, 0}, 1, ModelTransformPivot::SelectionCentre, {}, &pivot) &&
					 close(pivot, {14, 3.5f, 3.8125f}),
				 "reference pose and set order resolve a deterministic common pivot");
	ModelEdit edit;
	edit.selection = selected;
	edit.translation = {.49f, .51f, -.5f};
	edit.translationGrid = 1;
	ok &= expect(document.edit(edit, &error) && document.selection() == selected, "snapped transform preserves persistent selection");
	for (int s = 0; s < 3; ++s)
		for (int frame = 0; frame < 2; ++frame)
			for (int vertex = 0; vertex < 6; ++vertex)
			{
				auto expected = original.surfaces[s].frames[frame].positions[vertex];
				if (s != 1)
				{
					expected.y += 1;
					expected.z -= 1;
				}
				ok &= expect(close(document.mesh().surfaces[s].frames[frame].positions[vertex], expected),
							 "only selected surfaces move, including unused vertices in every pose");
			}
	const auto moved = surfaceBytes(document.mesh());
	ok &= expect(document.undo() && document.selection() == selected && surfaceBytes(document.mesh()) == surfaceBytes(original) &&
					 !document.canUndo(),
				 "one undo restores geometry and whole-surface selection");
	ok &= expect(document.redo() && surfaceBytes(document.mesh()) == moved && document.selection() == selected,
				 "redo restores geometry and selection");
	ok &= expect(document.undo(), "reset transform history");
	edit = {};
	edit.selection = selected;
	edit.rotation = {0, 0, 89};
	edit.rotationGrid = 15;
	edit.scale = {1.26f, 2.04f, 1};
	edit.scaleGrid = .1;
	edit.pivotMode = ModelTransformPivot::SelectionCentre;
	edit.pivotFrame = 1;
	ok &= expect(document.edit(edit, &error), "rotate and scale use shared reference bounds");
	for (int s : {0, 2})
		for (int f = 0; f < 2; ++f)
		{
			const auto before = original.surfaces[s].frames[f].positions[0];
			const ModelVec3 expected{21, 3.5f + (before.x - 14) * 1.3f, before.z};
			ok &= expect(close(document.mesh().surfaces[s].frames[f].positions[0], expected),
						 "rotation acts around common world centre, not separate surface centres");
			const auto n = document.mesh().surfaces[s].frames[f].normals[5];
			ok &= expect(std::abs(std::hypot(n.x, n.y, n.z) - 1) < .0001, "nonuniform scale preserves normalized transformed normals");
		}
	ok &= expect(exactVertex(original.surfaces[1], 5, document.mesh().surfaces[1], 5) &&
					 document.mesh().surfaces[0].uvSeams == original.surfaces[0].uvSeams &&
					 document.mesh().surfaces[2].skinPaths == original.surfaces[2].skinPaths,
				 "unselected attributes, seams and material slots remain exact");
	ok &= expect(document.undo(), "restore before current-frame operation");
	edit.rotation = {};
	edit.scale = {1, 1, 2};
	edit.frame = 1;
	ok &= expect(document.edit(edit, &error) && close(document.mesh().surfaces[0].frames[0].positions[0], {0, 0, 0}) &&
					 close(document.mesh().surfaces[2].frames[1].positions[0], {20, 0, -2.8125f}),
				 "current-frame pivot uses its own displayed pose");
	ok &= expect(document.undo(), "restore before mirror");
	edit.frame = -1;
	edit.pivotMode = ModelTransformPivot::Custom;
	edit.pivot = {3, 4, 5};
	edit.scale = {-1, 1, 1};
	ok &= expect(document.edit(edit, &error) && close(document.mesh().surfaces[2].frames[0].positions[0], {-14, 0, 0}) &&
					 document.mesh().surfaces[0].triangles[0].b == 2 && document.mesh().surfaces[1].triangles[0].b == 1,
				 "all-frame mirror uses custom pivot and reverses winding only on selected surfaces");
	const auto mirrored = document.mesh();
	ModelMesh native;
	const auto md3 = exportEditableModel(mirrored, "md3", 0, &error);
	ok &= expect(!md3.isEmpty() && importEditableModel("selection.md3", md3, &native, &error) && native.surfaces.size() == 3 &&
					 close(native.surfaces[2].frames[1].positions[0], {-14, 0, .5f}),
				 "multi-surface edits hand off to animated MD3 export");
	ModelRecoverySnapshot snapshot{mirrored, selected, 1, "surface selection", source, {}};
	const auto recovery = writeModelRecovery(snapshot, temporary.path(), "66ee1e58-d5ac-4469-9520-4a7c900159c7", &error);
	ModelRecoverySnapshot restored;
	ok &= expect(!recovery.isEmpty() && inspectModelRecovery(recovery, &restored).isValid() && restored.selection == selected &&
					 restored.frame == 1,
				 "recovery preserves active surface, complete set and pose");
	ModelDocument draft;
	ok &= expect(draft.restoreDraft(restored.mesh, restored.selection, &error) && draft.selection() == selected && draft.path().isEmpty(),
				 "recovery becomes an unsaved selected draft");
	const auto saved = read(recovery);
	const auto rewrite = [&](QJsonValue surfaces, bool mixed = false) {
		const quint32 size = qFromLittleEndian<quint32>(saved.constData() + 8);
		auto header = QJsonDocument::fromJson(saved.mid(12, size)).object();
		auto payload = QJsonDocument::fromJson(saved.mid(12 + size)).object();
		auto selection = payload.value("recoverySelection").toObject();
		selection.insert("surfaces", surfaces);
		if (mixed)
			selection.insert("vertices", QJsonArray{0});
		payload.insert("recoverySelection", selection);
		const auto body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
		header.insert("payloadBytes", body.size());
		header.insert("payloadSha256", QString::fromLatin1(QCryptographicHash::hash(body, QCryptographicHash::Sha256).toHex()));
		const auto meta = QJsonDocument(header).toJson(QJsonDocument::Compact);
		const quint32 length = qToLittleEndian(quint32(meta.size()));
		return write(recovery, saved.left(8) + QByteArray(reinterpret_cast<const char *>(&length), sizeof(length)) + meta + body);
	};
	ok &= expect(rewrite(QJsonValue::Undefined) && inspectModelRecovery(recovery, &restored).isValid() &&
					 restored.selection.surfaces.isEmpty(),
				 "older recovery payloads without surface sets remain readable");
	for (const auto &value : {QJsonValue(QJsonArray{0, 0, 2}), QJsonValue(QJsonArray{0, 99}), QJsonValue(QJsonArray{0}),
							 QJsonValue(QJsonArray{0, 2.5}), QJsonValue("0,2")})
		ok &= expect(rewrite(value) && !inspectModelRecovery(recovery, &restored).isValid(),
					 "corrupt recovery sets are rejected after checking their hash");
	ok &= expect(rewrite(QJsonArray{0, 2}, true) && !inspectModelRecovery(recovery, &restored).isValid(),
				 "recovery rejects mixed whole and component selection");
	for (int failure = 0; failure < 9; ++failure)
	{
		auto invalid = edit;
		if (failure == 0)
			invalid.frame = 0;
		if (failure == 1)
			invalid.selection.surfaces.insert(99);
		if (failure == 2)
			invalid.selection.surface = 1;
		if (failure == 3)
			invalid.selection.vertices.insert(0);
		if (failure == 4)
			invalid.selection.faces.insert(0);
		if (failure == 5)
			invalid.selection.edges.insert({0, 1});
		if (failure == 6)
			invalid.selection.tag = "tag_mount";
		if (failure == 7)
			invalid.selection.collision = "body";
		if (failure == 8)
			invalid.pivotFrame = 99;
		const auto before = document.revisionFingerprint();
		ok &= expect(!document.edit(invalid, &error) && !error.isEmpty() && document.revisionFingerprint() == before &&
						 document.selection() == selected,
					 "invalid transform is atomic across geometry and selection");
		if (failure > 0 && failure < 8)
		{
			document.setSelection(invalid.selection);
			ok &= expect(document.selection() == selected, "invalid selection cannot replace document state");
		}
	}
	for (auto kind : {ModelEditKind::RecalculateNormals, ModelEditKind::ProjectUv, ModelEditKind::DeleteFaces,
					  ModelEditKind::FitCollisionBox, ModelEditKind::DeleteSurface})
	{
		auto invalid = edit;
		invalid.kind = kind;
		invalid.scaleGrid = 0;
		invalid.rotationGrid = 0;
		ok &= expect(!document.edit(invalid, &error) && surfaceBytes(document.mesh()) == surfaceBytes(mirrored),
					 "component-only operations cannot silently edit active surface");
	}
	bool cancelled = false;
	int completedPasses = 0;
	ModelWorkControl control{[&] { return cancelled; },
							 [&](ModelWorkPhase phase, qint64 completed, qint64) {
								 if (phase == ModelWorkPhase::Editing && completed > 0 && ++completedPasses == 2)
									 cancelled = true;
							 }};
	ok &= expect(!document.edit(edit, &error, control) && cancelled && surfaceBytes(document.mesh()) == surfaceBytes(mirrored),
				 "cancellation leaves every selected surface untouched");
	ModelEdit join;
	join.kind = ModelEditKind::JoinSurfaces;
	join.selection = selected;
	join.surfaces = selected.surfaces;
	join.targetSurface = 2;
	ok &= expect(document.edit(join, &error) && document.selection().surface == 1 && document.selection().surfaces == QSet<int>{1} &&
					 document.selection().faces.isEmpty() && document.undo() && document.selection() == selected,
				 "join remaps persistent selection and undo restores original identities");
	if (argc == 2)
	{
		const auto output = QDir(temporary.path()).filePath("output.mesh.json");
		QJsonObject result;
		const auto cli = [&](QStringList args, int expected) {
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			args.prepend("--cli");
			args << "--json" << "--settings-file" << QDir(temporary.path()).filePath("settings.ini");
			process.start(app.arguments()[1], args);
			const bool done = process.waitForStarted(10000) && process.waitForFinished(30000);
			const auto bytes = process.readAllStandardOutput();
			result = QJsonDocument::fromJson(bytes).object();
			if (!done || process.exitCode() != expected)
				std::cerr << bytes.constData() << process.readAllStandardError().constData();
			return done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected && !result.isEmpty();
		};
		const QStringList base{"model", "edit", source, "--operation", "transform", "--output", output};
		const QStringList options{"--surfaces", "2,0", "--offset", "0,0,3"};
		ok &= expect(cli(base + options + QStringList{"--dry-run"}, 0) && !QFileInfo::exists(output) &&
						 result.value("selectedSurfaces").toArray() == QJsonArray{0, 2},
					 "CLI dry run reports exact sorted surface set without writing");
		ModelDocument loaded;
		ok &= expect(cli(base + options, 0) && loaded.load(output, &error) &&
						 close(loaded.mesh().surfaces[2].frames[1].positions[5], {28, 7, 10.125f}) &&
						 exactVertex(loaded.mesh().surfaces[1], 5, original.surfaces[1], 5),
					 "CLI writes same all-pose whole-surface edit");
		ok &= expect(cli(base + options, 1), "CLI protects existing output");
		ok &= expect(cli(base + QStringList{"--surfaces", "all", "--dry-run", "--overwrite"}, 0) &&
						 result.value("selectedSurfaces").toArray() == QJsonArray{0, 1, 2},
					 "CLI supports explicit all-surfaces selection");
		for (const auto &value : QStringList{"", "0,0", "0,02", "-1", "3", "0,", " 0", "+0", "0.5"})
			ok &= expect(cli(base + QStringList{"--surfaces", value, "--dry-run", "--overwrite"}, 2),
						 "CLI rejects malformed, duplicate and missing surface indices");
		for (const auto &extra :
			 QVector<QStringList>{{"--surface", "0"}, {"--faces", "all"}, {"--vertices", "all"}, {"--edges", "all"}, {"--surfaces", "1"}})
			ok &= expect(cli(base + options + extra + QStringList{"--dry-run", "--overwrite"}, 2),
						 "CLI rejects mixed or repeated selection options");
		ok &= expect(
			cli({"model", "edit", source, "--operation", "normals", "--surfaces", "all", "--output", output, "--dry-run", "--overwrite"},
				2),
			"CLI rejects whole-surface selector on unsupported operation");
		ok &= expect(read(source) == QJsonDocument(editableModelJson(original)).toJson(QJsonDocument::Compact),
					 "CLI keeps source bytes intact");
	}
	if (!ok)
		std::cerr << error.toStdString() << '\n';
	std::cout << checks << " surface selection checks\n";
	return ok ? 0 : 1;
}
