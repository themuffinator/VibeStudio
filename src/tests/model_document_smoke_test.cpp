#include "core/model_design.h"
#include "core/model_document.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace
{
QByteArray read(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return condition;
}
QByteArray source(const ModelMesh &mesh) { return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact); }
bool write(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
ModelMesh fixture()
{
	ModelDesign design;
	ModelDesignPart part;
	part.name = QStringLiteral("panel");
	part.primitive = QStringLiteral("plane");
	design.parts << part;
	auto mesh = buildModelDesignMesh(design);
	mesh.frames[0].name = QStringLiteral("idle01");
	mesh.frames << mesh.frames[0];
	mesh.frames[1].name = QStringLiteral("idle02");
	mesh.frames[1].origin = {2, 3, 4};
	mesh.surfaces[0].frames << mesh.surfaces[0].frames[0];
	for (auto &p : mesh.surfaces[0].frames[1].positions)
	{
		p.z += 8;
	}
	mesh.animations = {{QStringLiteral("idle"), 0, 2}};
	ModelTag tag;
	tag.name = QStringLiteral("tag_mount");
	tag.origin = {1, 2, 3};
	mesh.tags << tag;
	tag.frameIndex = 1;
	tag.origin.z = 11;
	mesh.tags << tag;
	updateEditableModelMetadata(&mesh);
	return mesh;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication application(argc, argv);
	const QString tempRoot = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (tempRoot.isEmpty() || !QDir().mkpath(tempRoot))
	{
		std::cerr << "Set VIBESTUDIO_TEST_TMP_ROOT inside the project.\n";
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(tempRoot).filePath(QStringLiteral("model-document-XXXXXX")));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	bool ok = true;
	QString error;
	auto original = fixture();
	ok &= expect(validateEditableModel(original).isEmpty(), "animated source with tags validates");
	ModelMesh parsed;
	ok &= expect(parseEditableModel(source(original), &parsed, &error) && source(parsed) == source(original),
				 "editable source preserves all frames, UVs, materials, origins, "
				 "normals, animations, and tags");
	const auto initial = source(parsed);
	for (const auto *field : {"frames", "surfaces", "animations", "tags", "skins"})
	{
		auto malformed = editableModelJson(original);
		malformed.remove(QLatin1String(field));
		ok &= expect(!parseEditableModel(QJsonDocument(malformed).toJson(), &parsed, &error) && source(parsed) == initial,
					 "missing source fields fail without replacing a document");
	}
	auto invalid = original;
	invalid.surfaces[0].triangles[0].a = 999999;
	ok &= expect(!validateEditableModel(invalid).isEmpty(), "out-of-range topology is rejected");
	invalid = original;
	invalid.surfaces[0].frames[1].positions[0].x = std::numeric_limits<float>::quiet_NaN();
	ok &= expect(!validateEditableModel(invalid).isEmpty(), "non-finite animation data is rejected");
	invalid = original;
	invalid.tags.removeLast();
	ok &= expect(!validateEditableModel(invalid).isEmpty(), "missing tags in a frame are rejected");
	invalid = original;
	invalid.tags[0].axis[0] = 2;
	ok &= expect(!validateEditableModel(invalid).isEmpty(), "non-orthonormal tags are rejected");

	const auto md3 = exportEditableModel(original, QStringLiteral("md3"), 0, &error);
	ModelMesh native;
	ok &= expect(!md3.isEmpty() && importEditableModel(QStringLiteral("animated.md3"), md3, &native, &error),
				 "animated MD3 exports and imports without decoder warnings");
	if (!native.surfaces.isEmpty())
	{
		ok &= expect(native.frames.size() == 2 && native.tags.size() == 2 && native.surfaces[0].frames.size() == 2 &&
						 native.surfaces[0].skinPaths == original.surfaces[0].skinPaths && native.tags[1].origin.z == 11 &&
						 native.frames[1].origin.z == 4 && native.surfaces[0].frames[1].positions[0].z == 8,
					 "MD3 preserves frame geometry, materials, frame origins, and animated "
					 "tags");
	}
	invalid = original;
	invalid.surfaces[0].frames[1].positions[0].x = 512;
	ok &= expect(exportEditableModel(invalid, QStringLiteral("md3"), 0, &error).isEmpty() && !error.isEmpty(),
				 "export validates every frame's quantization bounds");
	invalid = original;
	invalid.frames[1].name = QStringLiteral("a_frame_name_that_would_be_truncated");
	ok &= expect(exportEditableModel(invalid, QStringLiteral("md3"), 0, &error).isEmpty(), "export refuses silent name truncation");
	invalid = original;
	invalid.surfaces << invalid.surfaces[0];
	invalid.surfaces[0].name = QStringLiteral("panel_1");
	invalid.surfaces[1].name = QStringLiteral("PANEL_2");
	ok &= expect(validateEditableModel(invalid).isEmpty() && exportEditableModel(invalid, QStringLiteral("md3"), 0, &error).isEmpty(),
				 "MD3 refuses surface names that the renderer normalizes to the same identity");
	invalid = original;
	while (invalid.surfaces[0].texCoords.size() < 1001)
	{
		invalid.surfaces[0].texCoords << ModelTexCoord{};
		for (auto &frame : invalid.surfaces[0].frames)
		{
			frame.positions << ModelVec3{};
			frame.normals << ModelVec3{0, 0, 1};
		}
	}
	invalid.surfaces[0].vertexCount = 1001;
	ok &= expect(validateEditableModel(invalid).isEmpty() && exportEditableModel(invalid, QStringLiteral("md3"), 0, &error).isEmpty(),
				 "MD3 applies renderer limits even when the document and nominal MD3 format allow more vertices");
	ok &= expect(exportEditableModel(original, QStringLiteral("obj"), 1, &error).contains("usemtl textures/common/caulk") &&
					 exportEditableModel(original, QStringLiteral("obj"), 2, &error).isEmpty(),
				 "OBJ uses chosen frame and per-surface material");

	ModelSelection all;
	all.surface = 0;
	all.faces = {0, 1};
	ModelEdit edit;
	edit.selection = all;
	edit.translation = {4, 5, 6};
	edit.frame = 1;
	parsed = original;
	ok &= expect(applyModelEdit(&parsed, edit, nullptr, &error) && parsed.surfaces[0].frames[0].positions[0].z == 0 &&
					 parsed.surfaces[0].frames[1].positions[0].z == 14,
				 "frame-local transform leaves the other pose intact");
	edit.frame = -1;
	edit.scale = {-1, 1, 1};
	edit.translation = {};
	parsed = original;
	ok &= expect(applyModelEdit(&parsed, edit, nullptr, &error) && parsed.surfaces[0].triangles[0].b == original.surfaces[0].triangles[0].c,
				 "whole-surface mirroring corrects winding across all frames");
	const auto mirrored = source(parsed);
	edit.frame = 0;
	ok &= expect(!applyModelEdit(&parsed, edit, nullptr, &error) && source(parsed) == mirrored, "frame-only mirror fails atomically");

	edit = {};
	edit.kind = ModelEditKind::Extrude;
	edit.selection = all;
	edit.translation = {0, 0, 16};
	parsed = original;
	ModelSelection result;
	ok &= expect(applyModelEdit(&parsed, edit, &result, &error) && parsed.triangleCount == 10 && parsed.vertexCount == 8 &&
					 result.faces.size() == 2,
				 "region extrusion creates a cap and only boundary walls, "
				 "without an internal diagonal wall");
	if (parsed.vertexCount == 8)
	{
		ok &= expect(parsed.surfaces[0].frames[1].positions[4].z - parsed.surfaces[0].frames[0].positions[4].z == 8,
					 "extrusion preserves animation deltas on new vertices");
	}
	const auto extruded = source(parsed);
	edit.translation = {};
	ok &= expect(!applyModelEdit(&parsed, edit, nullptr, &error) && source(parsed) == extruded,
				 "zero extrusion cannot corrupt an existing mesh");

	parsed = original;
	edit = {};
	edit.kind = ModelEditKind::Subdivide;
	edit.selection.faces = {0};
	ok &= expect(applyModelEdit(&parsed, edit, &result, &error) && parsed.triangleCount == 6 && parsed.vertexCount == 7 &&
					 result.faces.size() == 4,
				 "subdivision splits the neighbouring face along the shared edge "
				 "without a T-junction");
	parsed = original;
	edit.kind = ModelEditKind::TransformUv;
	edit.uvOffset = {0.25f, -0.5f};
	const auto oldUnselected = original.surfaces[0].triangles[1];
	ok &= expect(applyModelEdit(&parsed, edit, &result, &error) && parsed.vertexCount == 6,
				 "face UV edit creates seams at a selection boundary");
	for (int i : {oldUnselected.a, oldUnselected.b, oldUnselected.c})
	{
		ok &= expect(parsed.surfaces[0].texCoords[i].u == original.surfaces[0].texCoords[i].u &&
						 parsed.surfaces[0].texCoords[i].v == original.surfaces[0].texCoords[i].v,
					 "unselected face UVs remain unchanged");
	}
	parsed = original;
	edit = {};
	edit.kind = ModelEditKind::DuplicateFaces;
	edit.selection.faces = {0};
	edit.translation = {0, 0, 16};
	ok &= expect(applyModelEdit(&parsed, edit, &result, &error) && parsed.triangleCount == 3 && parsed.vertexCount == 7,
				 "duplicate face retains original topology and every frame");
	edit.kind = ModelEditKind::DeleteFaces;
	edit.selection = result;
	ok &= expect(applyModelEdit(&parsed, edit, &result, &error) && parsed.triangleCount == 2 && parsed.vertexCount == 4,
				 "delete face compacts its unused vertices across all frames");

	parsed = original;
	edit = {};
	edit.kind = ModelEditKind::DuplicateFrame;
	edit.frame = 0;
	edit.text = QStringLiteral("idle_copy");
	ok &= expect(applyModelEdit(&parsed, edit, nullptr, &error) && parsed.frameCount == 3 && parsed.tags.size() == 3 &&
					 parsed.animations[0].frameCount == 3,
				 "duplicate frame carries tags and updates animation ranges");
	edit.kind = ModelEditKind::DeleteFrame;
	edit.frame = 1;
	ok &= expect(applyModelEdit(&parsed, edit, nullptr, &error) && parsed.frameCount == 2 && source(parsed) == source(original),
				 "delete duplicated frame restores all animation and tag data");

	{
		auto bounded = original;
		bounded.tags.clear();
		bounded.animations.clear();
		bounded.frames.fill(original.frames[0], modelDocumentMaxFrames);
		auto &surface = bounded.surfaces[0];
		auto pose = surface.frames[0];
		pose.positions.resize(1024);
		pose.normals.fill({0, 0, 1}, 1024);
		surface.texCoords.resize(1024);
		surface.frames.fill(pose, modelDocumentMaxFrames);
		updateEditableModelMetadata(&bounded);
		ok &= expect(validateEditableModel(bounded).isEmpty(), "prepare a mesh at the animation storage limit");
		const auto *originalPositions = bounded.surfaces[0].frames[0].positions.constData();
		ModelEdit oversized;
		oversized.kind = ModelEditKind::Subdivide;
		oversized.selection.faces = {0};
		ok &= expect(!applyModelEdit(&bounded, oversized, nullptr, &error) && error.contains(QStringLiteral("storage limit")) &&
						 bounded.vertexCount == 1024 && bounded.surfaces[0].frames[0].positions.constData() == originalPositions,
					 "subdivision rejects excess frame storage before allocating expanded poses");
		bounded = original;
		bounded.surfaces[0].triangles.fill(original.surfaces[0].triangles[0], modelDocumentMaxTriangles);
		updateEditableModelMetadata(&bounded);
		oversized.kind = ModelEditKind::DuplicateFaces;
		oversized.translation = {0, 0, 8};
		ok &= expect(!applyModelEdit(&bounded, oversized, nullptr, &error) && error.contains(QStringLiteral("triangle limit")) &&
						 bounded.triangleCount == modelDocumentMaxTriangles,
					 "face duplication refuses excess topology before copying geometry");
	}

	{
		auto busyMesh = original;
		busyMesh.surfaces[0].triangles.fill(original.surfaces[0].triangles[0], 1024);
		updateEditableModelMetadata(&busyMesh);
		const auto before = source(busyMesh);
		bool cancelled = false;
		ModelWorkPhase cancelPhase = ModelWorkPhase::Validating;
		ModelWorkControl control;
		control.cancelled = [&] { return cancelled; };
		control.progress = [&](ModelWorkPhase phase, qint64 done, qint64)
		{
			if (phase == cancelPhase && done >= 256)
			{
				cancelled = true;
			}
		};
		ok &= expect(!validateEditableModel(busyMesh, control).isEmpty() && cancelled,
					 "validation polls cancellation inside long geometry loops");
		cancelled = false;
		cancelPhase = ModelWorkPhase::Editing;
		ModelEdit normals;
		normals.kind = ModelEditKind::RecalculateNormals;
		ModelSelection retained{0, {1}, {1}};
		ok &= expect(!applyModelEdit(&busyMesh, normals, &retained, &error, control) && cancelled && source(busyMesh) == before &&
						 retained.faces == QSet<int>{1},
					 "cancelling normal recalculation preserves the mesh and output selection");
		ModelDocument busyDocument;
		ok &= expect(busyDocument.setMesh(busyMesh, &error), "prepare document cancellation fixture");
		cancelled = false;
		cancelPhase = ModelWorkPhase::Serializing;
		ModelEdit rename;
		rename.kind = ModelEditKind::RenameFrame;
		rename.frame = 0;
		rename.text = QStringLiteral("changed");
		ok &= expect(!busyDocument.edit(rename, &error, control) && cancelled && !busyDocument.canUndo() && !busyDocument.isModified() &&
						 source(busyDocument.mesh()) == before,
					 "cancelling an edited document's digest leaves state and undo history intact");
		cancelled = false;
		ok &= expect(editableModelJson(busyMesh, &error, control).isEmpty() && cancelled,
					 "JSON serialization discards partial output on cancellation");
		cancelled = false;
		cancelPhase = ModelWorkPhase::Reading;
		auto parsed = original;
		ok &= expect(!parseEditableModel(before, &parsed, &error, control) && cancelled && source(parsed) == source(original),
					 "parsing cancellation retains the caller's mesh");
		for (const auto &format : {QStringLiteral("md3"), QStringLiteral("obj")})
		{
			cancelled = false;
			cancelPhase = ModelWorkPhase::Serializing;
			ok &= expect(exportEditableModel(busyMesh, format, 0, &error, control).isEmpty() && cancelled,
						 "model exporters poll cancellation while preparing output");
		}
		busyMesh.sourcePath = QStringLiteral("source\nmtllib unexpected.mtl\r\nf 1 2 3");
		const auto obj = exportEditableModel(busyMesh, QStringLiteral("obj"), 0, &error);
		ok &= expect(!obj.isEmpty() && !obj.contains("\nmtllib unexpected"),
					 "OBJ provenance comments cannot introduce additional directives");
	}

	ModelDocument document;
	ok &= expect(document.setMesh(original, &error) && !document.isModified(), "new document begins with a clean baseline");
	document.setSelection(all);
	edit = {};
	edit.selection = all;
	edit.translation = {0, 0, 4};
	ok &=
		expect(document.edit(edit, &error) && document.isModified() && document.canUndo(), "edit creates a history entry and dirty state");
	ok &= expect(document.undo() && !document.isModified() && document.selection().faces == all.faces &&
					 source(document.mesh()) == source(original),
				 "undo restores source and selection and clears the dirty state "
				 "at baseline");
	ok &= expect(document.redo() && document.isModified(), "redo restores the edit");
	const QString path = QDir(temporary.path()).filePath(QStringLiteral("saved.mesh.json"));
	ok &= expect(document.save(path, false, &error) && !document.isModified(), "source saves atomically");
	ModelDocument reopened;
	ok &= expect(reopened.load(path, &error) && source(reopened.mesh()) == source(document.mesh()),
				 "saved document reopens without data loss");
	ok &= expect(reopened.edit(edit, &error), "prepare a cancellable save");
	const auto unsaved = source(reopened.mesh());
	const auto savedBytes = read(path);
	bool cancelled = false;
	ModelWorkControl control;
	control.cancelled = [&] { return cancelled; };
	control.progress = [&](ModelWorkPhase phase, qint64, qint64)
	{
		if (phase == ModelWorkPhase::Writing)
		{
			cancelled = true;
		}
	};
	ok &= expect(!reopened.save(path, true, &error, control) && reopened.isModified() && source(reopened.mesh()) == unsaved &&
					 read(path) == savedBytes,
				 "cancelled source save keeps the prior file and unsaved document state");
	cancelled = false;
	control.progress = [&](ModelWorkPhase phase, qint64 done, qint64 total)
	{
		if (phase == ModelWorkPhase::Validating && done == total && total > 0)
		{
			cancelled = true;
		}
	};
	ok &= expect(!reopened.load(path, &error, control) && reopened.isModified() && source(reopened.mesh()) == unsaved,
				 "cancelling a prepared load leaves the current document and history intact");
	ok &= expect(write(path, "external writer\n"), "simulate external source modification");
	ok &= expect(!document.save(path, true, &error) && error.contains(QStringLiteral("changed")),
				 "even overwrite cannot silently replace an externally modified source");
	const auto beforeFailedLoad = source(document.mesh());
	ok &= expect(!document.load(path, &error) && source(document.mesh()) == beforeFailedLoad,
				 "failed reload preserves unsaved authoring state");
	for (int i = 0; i < 105; ++i)
	{
		ok &= document.edit(edit, &error);
	}
	int undos = 0;
	while (document.undo())
	{
		++undos;
	}
	ok &= expect(undos == 100, "history obeys its count limit");

	if (argc > 1)
	{
		const QString executable = QString::fromLocal8Bit(argv[1]);
		const QString designPath = QDir(temporary.path()).filePath(QStringLiteral("plane.model.json"));
		const QString imported = QDir(temporary.path()).filePath(QStringLiteral("imported.mesh.json"));
		const QString edited = QDir(temporary.path()).filePath(QStringLiteral("edited.mesh.json"));
		const QString output = QDir(temporary.path()).filePath(QStringLiteral("animated.md3"));
		ModelDesign design;
		ModelDesignPart part;
		part.primitive = QStringLiteral("plane");
		design.parts << part;
		ok &= expect(write(designPath, QJsonDocument(modelDesignJson(design)).toJson()), "write CLI source fixture");
		const auto cli = [&](QStringList arguments, int exitCode)
		{
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			arguments.prepend(QStringLiteral("--cli"));
			arguments << QStringLiteral("--json");
			process.start(executable, arguments);
			const bool finished = process.waitForStarted(10000) && process.waitForFinished(30000);
			const auto stdoutBytes = process.readAllStandardOutput();
			if (!finished || process.exitCode() != exitCode)
			{
				std::cerr << stdoutBytes.constData() << process.readAllStandardError().constData();
			}
			return finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == exitCode &&
				   QJsonDocument::fromJson(stdoutBytes).isObject();
		};
		ok &= expect(cli({QStringLiteral("model"), QStringLiteral("import"), designPath, QStringLiteral("--output"), imported,
						  QStringLiteral("--dry-run")},
						 0) &&
						 !QFileInfo::exists(imported),
					 "CLI import dry-run writes nothing");
		ok &= expect(cli({QStringLiteral("model"), QStringLiteral("import"), designPath, QStringLiteral("--output"), imported}, 0),
					 "CLI import writes an editable source");
		ok &= expect(cli({QStringLiteral("model"), QStringLiteral("edit"), QStringLiteral("--faces"), QStringLiteral("all"), imported,
						  QStringLiteral("--operation"), QStringLiteral("extrude"), QStringLiteral("--offset"), QStringLiteral("0,0,16"),
						  QStringLiteral("--output"), edited},
						 0),
					 "CLI flags before source do not corrupt positional parsing");
		ok &= expect(cli({QStringLiteral("model"), QStringLiteral("build"), edited, QStringLiteral("--output"), output}, 0),
					 "CLI builds the edited mesh through the animated writer");
		ModelDocument cliDocument;
		ok &= expect(cliDocument.load(edited, &error) && cliDocument.mesh().triangleCount == 10, "CLI output contains the extruded region");
		ok &= expect(
			cli({QStringLiteral("model"), QStringLiteral("edit"), edited, QStringLiteral("--operation"), QStringLiteral("delete-faces"),
				 QStringLiteral("--faces"), QStringLiteral("all"), QStringLiteral("--output"), imported, QStringLiteral("--overwrite")},
				4),
			"invalid CLI edit cannot overwrite an existing source");
		ok &= expect(cli({QStringLiteral("model"), QStringLiteral("build"), edited, QStringLiteral("--output"), edited,
						  QStringLiteral("--format"), QStringLiteral("md3"), QStringLiteral("--overwrite")},
						 1),
					 "CLI export protects editable source paths");
	}
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
