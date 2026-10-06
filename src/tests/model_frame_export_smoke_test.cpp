#include "core/model_design.h"
#include "core/model_document.h"
#include "core/model_frame_export.h"
#include "core/model_obj.h"
#include "core/package_draft.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QProcess>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return value;
}
QByteArray read(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
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
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath("mesh-frame-export-XXXXXX"));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QString::fromLatin1(name)); };
	ModelDesign design;
	design.parts << ModelDesignPart{};
	auto mesh = buildModelDesignMesh(design);
	mesh.frames << mesh.frames.first();
	mesh.frames[1].name = "raised";
	for (auto &surface : mesh.surfaces)
	{
		auto frame = surface.frames.first();
		for (auto &position : frame.positions)
		{
			position.z += 16;
		}
		surface.frames << frame;
	}
	updateEditableModelMetadata(&mesh);
	QString error;
	bool ok = expect(validateEditableModel(mesh).isEmpty(), "valid synthetic two-pose source");
	ModelFrameExportRequest request;
	request.frame = 1;
	request.materialName = "textures/test/frame";
	const auto stdoutResult = exportModelFrame(mesh, request);
	const auto decoded = decodeModelObj("frame.obj", stdoutResult.bytes);
	ok &= expect(stdoutResult.succeeded() && !stdoutResult.written && decoded.geometryAvailable &&
					 decoded.triangleCount == mesh.triangleCount &&
					 decoded.surfaces.first().frames.first().positions.first().z == mesh.surfaces.first().frames[1].positions.first().z &&
					 !stdoutResult.notes.isEmpty(),
				 "in-memory result preserves requested frame and reports omitted native data");
	ok &= expect(stdoutResult.bytes == exportModelFrameObj(mesh, 1, request.materialName).toUtf8(),
				 "legacy geometry encoding remains deterministic");
	request.outputPath = path("new/nested/frame.obj");
	request.dryRun = true;
	auto result = exportModelFrame(mesh, request);
	ok &= expect(result.succeeded() && !result.written && !QFileInfo::exists(path("new")), "dry run creates no directories or output");
	request.dryRun = false;
	result = exportModelFrame(mesh, request);
	ok &= expect(result.succeeded() && result.written && read(request.outputPath) == stdoutResult.bytes,
				 "guarded publication writes complete OBJ bytes");
	ok &= expect(!exportModelFrame(mesh, request).succeeded() && read(request.outputPath) == stdoutResult.bytes,
				 "existing output requires overwrite");
	request.overwrite = true;
	ok &= expect(exportModelFrame(mesh, request).written, "explicit overwrite accepts an unchanged destination");
	const QByteArray external("external writer");
	bool changed = false;
	ModelWorkControl race;
	race.progress = [&](ModelWorkPhase phase, qint64, qint64)
	{
		if (!changed && phase == ModelWorkPhase::Serializing)
		{
			changed = write(request.outputPath, external);
		}
	};
	result = exportModelFrame(mesh, request, race);
	ok &= expect(changed && !result.succeeded() && !result.written && read(request.outputPath) == external,
				 "destination review precedes serialization");
	bool cancelled = false;
	ModelWorkControl cancel;
	cancel.cancelled = [&] { return cancelled; };
	cancel.progress = [&](ModelWorkPhase phase, qint64, qint64)
	{
		if (phase == ModelWorkPhase::Writing)
		{
			cancelled = true;
		}
	};
	result = exportModelFrame(mesh, request, cancel);
	ok &= expect(result.cancelled && !result.written && !result.succeeded() && read(request.outputPath) == external,
				 "cancelled replacement preserves external bytes");
	request.outputPath = path("cancelled.obj");
	cancelled = false;
	result = exportModelFrame(mesh, request, cancel);
	ok &= expect(result.cancelled && !QFileInfo::exists(request.outputPath), "cancelled new export publishes no partial file");
	cancelled = false;
	cancel.progress = [&](ModelWorkPhase phase, qint64 done, qint64 total)
	{
		if (phase == ModelWorkPhase::Committing && done == total)
		{
			cancelled = true;
		}
	};
	result = exportModelFrame(mesh, request, cancel);
	ok &= expect(cancelled && result.succeeded() && result.written && !result.cancelled,
				 "late cancellation preserves the durable success result");
	{
		QLockFile lock(request.outputPath + ".vibestudio-model.lock");
		ok &= expect(lock.tryLock(), "reserve competing output writer");
		ok &= expect(!exportModelFrame(mesh, request).written && read(request.outputPath) == stdoutResult.bytes,
					 "competing writer cannot truncate output");
	}
	const auto native = exportEditableModel(mesh, "md3", 0, &error);
	ok &= expect(!native.isEmpty() && write(path("source.md3"), native), "independent native fixture");
	request.outputPath = path("./source.md3");
	request.protectedFiles = {path("source.md3")};
	ok &= expect(!exportModelFrame(mesh, request).succeeded() && read(path("source.md3")) == native,
				 "alternate spelling cannot replace a protected source");
	request.dryRun = true;
	ok &= expect(!exportModelFrame(mesh, request).succeeded(), "dry run enforces the same source protection");
	request.dryRun = false;
	request.outputPath = temporary.path();
	ok &= expect(!exportModelFrame(mesh, request).succeeded(), "directory destination is rejected");
	request.outputPath.clear();
	request.frame = -1;
	ok &= expect(!exportModelFrame(mesh, request).succeeded(), "invalid frame cannot produce output");
	request.frame = 1;
	QDir().mkpath(path("package"));
	ok &= expect(write(path("package/model.md3"), native), "folder package fixture");
	PackageArchive folder;
	PackageStagingModel plan;
	ok &= expect(folder.load(path("package"), &error) && plan.loadBaseArchive(folder, &error) &&
					 plan.renameEntry("model.md3", "renamed.md3", &error),
				 "renamed staged source");
	request.sourceArchive = std::make_shared<PackageArchive>(packagePlannedArchive(plan));
	for (const auto &target : {path("package/model.md3"), path("package/renamed.md3"), path("package/new/frame.obj")})
	{
		request.outputPath = target;
		ok &= expect(!exportModelFrame(mesh, request).succeeded(),
					 "package source folder is protected, including renamed and new descendants");
	}
	ok &= expect(PackageDraft::save(path("models.vibepackage"), &plan, false, &error), "portable draft fixture");
	request.sourceArchive = std::make_shared<PackageArchive>(packagePlannedArchive(plan));
	const auto manifest = read(path("models.vibepackage/document.json"));
	for (const auto &target : {path("models.vibepackage/document.json"), path("models.vibepackage/objects/new.obj")})
	{
		request.outputPath = target;
		ok &= expect(!exportModelFrame(mesh, request).succeeded(), "portable manifest and payload store are protected");
	}
	ok &= expect(read(path("models.vibepackage/document.json")) == manifest, "draft manifest remains byte-identical");
	request.outputPath = path("outside.obj");
	ok &= expect(exportModelFrame(mesh, request).written, "derivative outside source package is allowed");
	PackageWriteRequest archiveWrite;
	archiveWrite.format = PackageArchiveFormat::Pak;
	archiveWrite.destinationPath = path("models.pak");
	ok &= expect(plan.writeArchive(archiveWrite).succeeded(), "binary archive fixture");
	const auto originalArchive = read(path("models.pak"));
	PackageArchive pak;
	ok &= expect(pak.load(path("models.pak"), &error), "open binary package protection");
	request.sourceArchive = std::make_shared<PackageArchive>(pak);
	request.outputPath = path("models.pak");
	ok &= expect(!exportModelFrame(mesh, request).succeeded() && read(path("models.pak")) == originalArchive,
				 "binary package input cannot become an OBJ output");
	if (argc > 1)
	{
		QJsonObject output;
		QByteArray raw;
		const auto run = [&](const QStringList &args, int expected, bool json = true)
		{
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			process.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "--settings-file", path("settings.ini")} + args +
															   (json ? QStringList{"--json"} : QStringList{}));
			if (!process.waitForFinished(30000))
			{
				process.kill();
				process.waitForFinished();
				return false;
			}
			raw = process.readAllStandardOutput();
			output = QJsonDocument::fromJson(raw).object();
			const bool passed =
				process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected && (!json || !output.isEmpty());
			if (!passed)
			{
				std::cerr << raw.toStdString() << process.readAllStandardError().toStdString() << '\n';
			}
			return passed;
		};
		const QStringList loose{"model", "export", "--file", path("source.md3"), "--frame", "1", "--material", "textures/test/frame"};
		ok &= expect(run(loose, 0) && output.value("frame").toInt() == 1 && !output.value("obj").toString().isEmpty() &&
						 !output.value("notes").toArray().isEmpty(),
					 "CLI JSON stdout retains OBJ and frame, adds omission notes");
		const auto jsonObj = output.value("obj").toString().toUtf8();
		ok &= expect(run(loose, 0, false) && QByteArray(raw).replace("\r\n", "\n") == jsonObj,
					 "text stdout contains only OBJ records with native platform line endings");
		ok &= expect(run(loose + QStringList{"--output", path("cli/nested/result.obj"), "--dry-run"}, 0) &&
						 !output.value("written").toBool() && !QFileInfo::exists(path("cli")),
					 "CLI dry-run creates nothing");
		ok &= expect(run(loose + QStringList{"--output", path("cli/result.obj")}, 0) && output.value("written").toBool() &&
						 read(path("cli/result.obj")) == jsonObj,
					 "CLI publishes the requested pose");
		ok &= expect(run(loose + QStringList{"--output", path("cli/result.obj")}, 1), "CLI overwrite is explicit");
		ok &= expect(!output.value("written").toBool() && output.value("outputPath").toString() == path("cli/result.obj"),
					 "write failure retains explicit output and written=false in JSON");
		ok &= expect(run(loose + QStringList{"--output", path("cli/result.obj"), "--overwrite"}, 0), "CLI explicit replacement succeeds");
		ok &= expect(run(loose + QStringList{"--output", path("source.md3"), "--overwrite"}, 1) && read(path("source.md3")) == native,
					 "CLI cannot overwrite loose source");
		ok &= expect(run({"model", "export", "--file", path("source.md3"), "--frame", "wrong"}, 2),
					 "malformed --frame is not silently frame zero");
		ok &= expect(run(loose + QStringList{"--output", " "}, 2), "empty explicit output is not silently stdout");
		ok &= expect(run({"model", "export", path("package"), "model.md3", "--output", path("package/inside.obj")}, 1),
					 "CLI protects the input folder");
		ok &= expect(run({"model", "export", path("models.vibepackage"), "renamed.md3", "--output", path("draft-result.obj")}, 0) &&
						 QFileInfo::exists(path("draft-result.obj")),
					 "CLI exports the portable staged snapshot");
		ok &= expect(run({"model", "export", path("models.pak"), "renamed.md3", "--output", path("pak-result.obj")}, 0),
					 "CLI exports verified native geometry from a binary package");
		ok &= expect(run({"model", "export", path("models.pak"), "renamed.md3", "--output", path("models.pak"), "--overwrite"}, 1) &&
						 read(path("models.pak")) == originalArchive,
					 "CLI cannot overwrite its binary package");
		ok &= expect(run({"model", "export", path("models.vibepackage"), "renamed.md3", "--output",
						  path("models.vibepackage/document.json"), "--overwrite"},
						 1) &&
						 read(path("models.vibepackage/document.json")) == manifest,
					 "CLI protects the draft manifest");
		QFile oversized(path("oversized.md3"));
		ok &= expect(oversized.open(QIODevice::WriteOnly) && oversized.resize(modelFileByteLimit + 1), "oversized loose source fixture");
		oversized.close();
		ok &= expect(run({"model", "inspect", "--file", path("oversized.md3")}, 1) && output.value("message").toString().contains("64 MiB"),
					 "native CLI inspection checks the loose-file bound before decoding");
	}
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
