#include "core/model_file_io.h"
#include "tests/model_output_test_helpers.h"
#include "tests/model_rig_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QJsonDocument>
#include <QLockFile>
#include <QProcess>
#include <QTemporaryDir>

#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0, failures = 0;
bool expect(bool value, const char *message, const QString &error = {})
{
	++checks;
	if (!value)
	{
		++failures;
		std::cerr << "FAIL: " << message << ": " << error.toStdString() << '\n';
	}
	return value;
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
	QTemporaryDir temp(QDir(root).filePath("model-save-XXXXXX"));
	if (!temp.isValid())
		return 1;
	const auto source = temp.filePath("source.mesh.json"), alias = temp.filePath("alias.mesh.json");
	const auto nested = temp.filePath("absent/nested/output.mesh.json");
	QString error;
	const auto mesh = rigFixture();
	ModelDocument document;
	expect(document.setMesh(mesh, &error), "prepare animated source", error);
	expect(document.save(nested, false, &error, {}, true) && document.path().isEmpty() && document.sourceFingerprint().isEmpty() &&
			   !document.isModified() && !QFileInfo::exists(temp.filePath("absent")),
		   "fresh-document dry run creates no folders and does not adopt a saved identity", error);
	expect(document.save(source, false, &error), "save animated source", error);
	ModelEdit rename;
	rename.kind = ModelEditKind::RenameSurface;
	rename.selection.surface = 0;
	rename.text = "edited";
	expect(document.edit(rename, &error), "prepare unsaved edit", error);
	const auto savedBytes = modelTestReadFile(source), editedBytes = rigBytes(document.mesh()), fingerprint = document.sourceFingerprint();
	const auto selection = document.selection();
	const auto intact = [&]
	{
		return document.path() == source && document.sourceFingerprint() == fingerprint && document.isModified() && document.canUndo() &&
			   !document.canRedo() && document.selection() == selection && rigBytes(document.mesh()) == editedBytes;
	};
	QSet<ModelWorkPhase> phases;
	ModelWorkControl progress;
	progress.progress = [&](ModelWorkPhase phase, qint64, qint64) { phases.insert(phase); };
	expect(document.save(nested, false, &error, progress, true) && intact() && modelTestReadFile(source) == savedBytes &&
			   !QFileInfo::exists(temp.filePath("absent")) && phases.contains(ModelWorkPhase::Serializing) &&
			   phases.contains(ModelWorkPhase::Validating) && !phases.contains(ModelWorkPhase::Writing) &&
			   !phases.contains(ModelWorkPhase::Committing),
		   "dry run validates and serialises without publishing, adopting identity or changing history", error);
	expect(document.save(source, false, &error, {}, true) && intact() && modelTestReadFile(source) == savedBytes,
		   "dry run permits guarded ordinary in-place source saves", error);
	expect(document.undo() && !document.isModified() && document.redo() && intact(), "dry run keeps undo/redo and saved baseline");
	expect(modelTestHardLink(source, alias), "prepare hard-linked current-source alias");
	for (bool dry : {true, false})
	{
		expect(!document.save(alias, true, &error, {}, dry) && intact() && modelTestReadFile(source) == savedBytes &&
				   modelTestReadFile(alias) == savedBytes,
			   "save refuses a hard-linked source alias with a different resolved identity", error);
		for (const auto &bad : {temp.path(), temp.filePath("wrong.obj"), temp.filePath("source.mesh.json/child.mesh.json")})
			expect(!document.save(bad, true, &error, {}, dry) && !error.isEmpty() && intact(),
				   "dry and real saves reject invalid output types and unresolvable parent paths");
	}
	const auto existing = temp.filePath("existing.mesh.json");
	expect(write(existing, "retained destination"), "prepare existing destination");
	for (bool dry : {true, false})
		expect(!document.save(existing, false, &error, {}, dry) && modelTestReadFile(existing) == "retained destination" && intact(),
			   "dry and real saves require overwrite for an unrelated existing file");
	expect(document.save(existing, true, &error, {}, true) && modelTestReadFile(existing) == "retained destination" && intact(),
		   "explicit overwrite in a dry run leaves existing output untouched", error);
	for (auto phase : {ModelWorkPhase::Reading, ModelWorkPhase::Validating, ModelWorkPhase::Serializing})
	{
		bool cancelled = false;
		ModelWorkControl control;
		control.cancelled = [&] { return cancelled; };
		control.progress = [&](ModelWorkPhase current, qint64, qint64) { cancelled |= current == phase; };
		expect(!document.save(source, true, &error, control, true) && cancelled && intact() && modelTestReadFile(source) == savedBytes,
			   "dry-run cancellation preserves document and source at every preparation phase", error);
	}
	expect(write(source, "external edit"), "simulate source replacement");
	for (bool dry : {true, false})
		expect(!document.save(source, true, &error, {}, dry) && intact() && modelTestReadFile(source) == "external edit",
			   "dry and real saves reject a source changed since load even with overwrite");
	expect(write(source, savedBytes), "restore owned source fixture");
	const auto moved = temp.filePath("moved.mesh.json");
	expect(QFile::rename(source, moved), "simulate source disappearing");
	for (bool dry : {true, false})
		expect(!document.save(source, true, &error, {}, dry) && intact() && !QFileInfo::exists(source),
			   "dry and real saves refuse a deleted source rather than recreating it");
	expect(QFile::rename(moved, source), "restore owned source name");
	bool changed = false;
	ModelWorkControl during;
	during.progress = [&](ModelWorkPhase phase, qint64 done, qint64 total)
	{
		if (!changed && phase == ModelWorkPhase::Serializing && total > 0 && done == total)
			changed = write(source, "changed during preparation");
	};
	expect(!document.save(source, true, &error, during, true) && changed && intact() &&
			   modelTestReadFile(source) == "changed during preparation",
		   "dry run rechecks its destination after serialisation", error);
	expect(write(source, savedBytes), "restore source after injected concurrent edit");
	{
		QLockFile lock(source + ".vibestudio-model.lock");
		expect(lock.tryLock(0), "reserve destination in competing writer fixture");
		expect(document.save(source, true, &error, {}, true) && intact() && modelTestReadFile(source) == savedBytes,
			   "dry run performs no lock acquisition and does not promise future write availability", error);
		expect(!document.save(source, true, &error) && intact() && modelTestReadFile(source) == savedBytes,
			   "actual save still honours competing writers");
	}
	expect(document.save(source, false, &error) && !document.isModified() && modelTestReadFile(source) == editedBytes &&
			   document.sourceFingerprint() != fingerprint,
		   "ordinary in-place save publishes and adopts the new baseline", error);
	expect(document.undo() && document.isModified() && document.redo() && !document.isModified(),
		   "actual save keeps history relative to its new saved baseline");
	const auto exported = exportEditableModel(mesh, "md3", 0, &error);
	const auto exportPath = temp.filePath("export/nested/model.md3");
	expect(!exported.isEmpty() && saveModelDesignBytes(exportPath, exported, false, &error, source, {}, true) &&
			   !QFileInfo::exists(temp.filePath("export")),
		   "native export dry run validates without creating output folders", error);
	const auto exportAlias = temp.filePath("export-alias.md3");
	expect(modelTestHardLink(source, exportAlias), "prepare native-export source alias");
	for (bool dry : {true, false})
		expect(!saveModelDesignBytes(exportAlias, exported, true, &error, source, {}, dry) &&
				   modelTestReadFile(exportAlias) == editedBytes && modelTestReadFile(source) == editedBytes,
			   "native exports reject hard-link source aliases in dry and real writes", error);
	{
		const QByteArray oversized(modelFileByteLimit + 1, 'x');
		for (bool dry : {true, false})
			expect(!saveModelDesignBytes(exportPath, oversized, true, &error, source, {}, dry) && error.contains("64 MiB") &&
					   !QFileInfo::exists(temp.filePath("export")),
				   "native output budget is enforced before directory creation in dry and real writes", error);
	}
	{
		ModelDesign design;
		design.parts.append(ModelDesignPart{});
		auto large = buildModelDesignMesh(design);
		auto &surface = large.surfaces[0];
		surface.texCoords.resize(modelDocumentMaxVertices);
		auto frame = surface.frames[0];
		frame.positions.resize(modelDocumentMaxVertices);
		frame.normals.fill({.577350269f, .577350269f, .577350269f}, modelDocumentMaxVertices);
		for (int v = surface.vertexCount; v < modelDocumentMaxVertices; ++v)
			frame.positions[v] = {1.1234567f, 2.2345678f, 3.3456789f};
		surface.frames.fill(frame, 16);
		large.frames.fill(large.frames[0], 16);
		updateEditableModelMetadata(&large);
		// Readable JSON of this geometry passes 64 MiB; the source is written
		// with packed arrays instead (core/model_source_encoding.h).
		ModelDocument packed;
		const auto packedPath = temp.filePath("packed.mesh.json");
		ModelDocument reopened;
		expect(packed.setMesh(large, &error) && packed.save(packedPath, false, &error) &&
				   packed.savedSourceEncoding() == ModelSourceEncoding::Packed && QFileInfo(packedPath).size() <= modelDocumentMaxSourceBytes &&
				   reopened.load(packedPath, &error) && reopened.revisionFingerprint() == packed.revisionFingerprint(),
			   "geometry whose readable JSON exceeds the source byte budget saves packed within it and reopens", error);
		// An incompressible embedded skin takes even the packed source over.
		ModelEmbeddedSkin skin;
		skin.index = 0;
		skin.name = QStringLiteral("noise");
		skin.groupFrameCount = 1;
		skin.image = QImage(2900, 2900, QImage::Format_RGB32);
		quint32 seed = 12345;
		for (int y = 0; y < skin.image.height(); ++y)
		{
			auto *line = reinterpret_cast<QRgb *>(skin.image.scanLine(y));
			for (int x = 0; x < skin.image.width(); ++x)
			{
				seed = seed * 1664525u + 1013904223u;
				line[x] = 0xff000000u | (seed >> 8);
			}
		}
		large.embeddedSkins = {skin};
		ModelDocument oversized;
		expect(oversized.setMesh(large, &error) && oversized.sourceBytes() > modelDocumentMaxSourceBytes,
			   "prepare a valid model whose packed source exceeds the source byte budget", error);
		for (bool dry : {true, false})
			expect(!oversized.save(nested, false, &error, {}, dry) && error.contains("64 MiB") && oversized.path().isEmpty() &&
					   !oversized.isModified() && !QFileInfo::exists(temp.filePath("absent")),
				   "complete JSON byte budget is enforced in dry and real saves", error);
	}
	if (argc > 1)
	{
		const auto executable = QString::fromLocal8Bit(argv[1]);
		const auto cli = [&](QStringList arguments, int expected)
		{
			QProcess process;
			process.setWorkingDirectory(temp.path());
			process.start(executable, QStringList{"--cli", "model"} + arguments + QStringList{"--json"});
			const bool finished = process.waitForStarted(10000) && process.waitForFinished(30000);
			const auto output = process.readAllStandardOutput();
			const bool correct = finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected &&
								 QJsonDocument::fromJson(output).isObject();
			if (!correct)
				std::cerr << output.constData() << process.readAllStandardError().constData();
			return correct;
		};
		const QList<QStringList> operations{
			{"edit", "--operation", "material", "--surface", "0", "--material", "models/new.tga"},
			{"rig", "--operation", "rename", "--joint", "root", "--name", "new_root"},
			{"pose", "--operation", "rename", "--clip", "0", "--name", "new_clip"},
			{"slots", "--operation", "set", "--surface", "0", "--slot", "0", "--material", "models/new.tga"},
			{"surfaces", "--operation", "rename", "--surface", "0", "--name", "new_surface"},
			{"weight-paint", "--surface", "0", "--joint", "root", "--vertices", "0", "--value", "0.5", "--remainder-joint", "tip"},
			{"curves", "--operation", "capture", "--clip", "0", "--joint", "root"}};
		for (const auto &operation : operations)
		{
			const auto input = temp.filePath(operation[0] + "-cli.mesh.json"),
					   output = temp.filePath(operation[0] + "-cli-alias.mesh.json");
			expect(write(input, savedBytes) && modelTestHardLink(input, output), "prepare actual CLI source and hard-link alias");
			auto arguments = operation;
			arguments.insert(1, input);
			for (bool dry : {true, false})
				expect(
					cli(arguments + QStringList{"--output", output, "--overwrite"} + (dry ? QStringList{"--dry-run"} : QStringList{}), 1) &&
						modelTestReadFile(input) == savedBytes && modelTestReadFile(output) == savedBytes,
					"actual authoring CLI rejects source aliases in dry and real saves", operation[0]);
			expect(cli(arguments + QStringList{"--output", input, "--overwrite", "--dry-run"}, 0) && modelTestReadFile(input) == savedBytes,
				   "actual authoring CLI retains in-place dry-run support", operation[0]);
			expect(cli(arguments + QStringList{"--output", input, "--overwrite"}, 0) && modelTestReadFile(input) != savedBytes,
				   "actual authoring CLI retains explicit in-place writing", operation[0]);
		}
		for (bool dry : {true, false})
			expect(cli(QStringList{"build", source, "--output", exportAlias, "--format", "md3", "--overwrite"} +
						   (dry ? QStringList{"--dry-run"} : QStringList{}),
					   1) &&
					   modelTestReadFile(source) == editedBytes,
				   "actual native-export CLI protects source aliases in dry and real runs");
		const auto nativeInput = temp.filePath("native.md3"), importedAlias = temp.filePath("native-alias.mesh.json");
		expect(write(nativeInput, exported) && modelTestHardLink(nativeInput, importedAlias), "prepare native import hard-link alias");
		for (bool dry : {true, false})
			expect(cli(QStringList{"import", nativeInput, "--output", importedAlias, "--overwrite"} +
						   (dry ? QStringList{"--dry-run"} : QStringList{}),
					   1) &&
					   modelTestReadFile(nativeInput) == exported && modelTestReadFile(importedAlias) == exported,
				   "actual import CLI protects native source aliases in dry and real runs");
	}
	expect(!QFileInfo::exists(source + ".vibestudio-model.lock") && !QFileInfo::exists(temp.filePath("absent")),
		   "dry runs leave no save locks or output directories");
	std::cout << checks << " save checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
