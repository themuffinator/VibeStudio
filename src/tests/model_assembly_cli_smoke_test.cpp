#include "core/package_draft.h"
#include "tests/model_assembly_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
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
	if (argc < 2 || root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath("assembly-cli-XXXXXX"));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QString::fromLatin1(name)); };
	QJsonObject output;
	const auto run = [&](QStringList args, int code = 0)
	{
		QProcess process;
		process.setWorkingDirectory(temporary.path());
		process.start(QString::fromLocal8Bit(argv[1]),
					  QStringList{"--cli", "--settings-file", path("settings.ini")} + args + QStringList{"--json"});
		if (!process.waitForFinished(30000))
		{
			process.kill();
			process.waitForFinished();
			return false;
		}
		const auto bytes = process.readAllStandardOutput();
		output = QJsonDocument::fromJson(bytes).object();
		if (process.exitCode() != code || output.isEmpty())
		{
			std::cerr << args.join(' ').toStdString() << " exit=" << process.exitCode() << '\n'
					  << bytes.toStdString() << process.readAllStandardError().toStdString() << '\n';
			return false;
		}
		return process.exitStatus() == QProcess::NormalExit;
	};
	const auto fixture = QJsonDocument(editableModelJson(tests::assemblyModel())).toJson();
	bool ok = expect(write(path("part.mesh.json"), fixture), "write original synthetic model");
	const QStringList create{
		"model", "assembly", "--new", "--part", "root", "--model", "part.mesh.json", "--fps", "1", "--output", "source.assembly.json"};
	ok &= expect(run(create + QStringList{"--dry-run"}) && !output.value("written").toBool() &&
					 !QFileInfo::exists(path("source.assembly.json")),
				 "new assembly dry run validates without a file");
	ok &= expect(run(create) && output.value("written").toBool(), "new root recipe writes through shared document");
	ok &= expect(run({"model", "assembly", "source.assembly.json", "--operation", "add", "--part", "child", "--model", "part.mesh.json",
					  "--parent", "root", "--tag", "tag_link", "--fps", "2", "--output", "source.assembly.json"}),
				 "child attaches and saves guarded current source");
	ok &= expect(run({"model", "assembly", "source.assembly.json", "--time", "0.5"}) && output.value("vertices").toInt() == 6 &&
					 output.value("inputs").toArray()[0].toObject().value("fraction").toDouble() == .5 &&
					 output.value("inputs").toArray()[1].toObject().value("frame").toInt() == 1,
				 "independent animation samples and dependency fingerprints");
	const QStringList bake{"model", "assembly", "source.assembly.json", "--operation", "bake", "--time", "0.5", "--output", "pose.obj"};
	ok &= expect(run(bake + QStringList{"--dry-run"}) && !QFileInfo::exists(path("pose.obj")), "bake dry run does not publish");
	ok &= expect(run(bake) && !output.value("notes").toArray().isEmpty(), "explicit static bake reports data omissions");
	QByteArray bytes;
	QString error;
	ModelMesh baked;
	ok &= expect(readModelFile(path("pose.obj"), &bytes, &error) && importEditableModel("pose.obj", bytes, &baked, &error) &&
					 baked.vertexCount == 6 && baked.frames.size() == 1 && baked.surfaces[1].frames[0].positions[0].x == 15,
				 "CLI bake geometry follows the moving parent tag");
	ok &= expect(run(bake, 4) && run(bake + QStringList{"--overwrite"}), "existing derivatives require explicit overwrite");
	ok &= expect(run({"model", "assembly", "source.assembly.json", "--operation", "update", "--part", "root", "--rename-to", "body",
					  "--output", "renamed.assembly.json"}) &&
					 output.value("assembly").toObject().value("parts").toArray()[1].toObject().value("parent") == "body",
				 "CLI renaming updates dependent links");
	ok &= expect(
		run({"model", "assembly", "renamed.assembly.json", "--operation", "remove", "--part", "body", "--output", "empty.assembly.json"}) &&
			output.value("assembly").toObject().value("parts").toArray().isEmpty(),
		"branch removal produces a valid empty source");
	for (const auto &options : QVector<QStringList>{{"--part", "unexpected"},
													{"--time", "nan"},
													{"--time", "0", "--time", "1"},
													{"--palette", "quake"},
													{"--output", "unwanted.obj"},
													{"--unknown"}})
	{
		ok &= expect(run(QStringList{"model", "assembly", "source.assembly.json"} + options, 2),
					 "reject irrelevant, duplicate and invalid options");
	}
	ok &= expect(run({"model", "assembly", "source.assembly.json", "--operation", "add", "--part", "broken", "--model", "part.mesh.json",
					  "--parent", "root", "--tag", "missing", "--output", "bad.assembly.json"},
					 4) &&
					 !QFileInfo::exists(path("bad.assembly.json")),
				 "unresolved attachment cannot publish through CLI");
	ok &= expect(
		run({"model", "assembly", "source.assembly.json", "--operation", "bake", "--output", "part.mesh.json", "--overwrite", "--dry-run"},
			4),
		"dry run protects model input just like export");
	ok &= expect(write(path("input.assembly.json"), fixture), "model content can have an unusual file extension");
	ModelAssemblyDocument guarded;
	auto recipe = tests::assemblyRecipe(path("input.assembly.json"));
	ok &=
		expect(guarded.setAssembly(recipe, temporary.path(), &error) && !guarded.save(path("input.assembly.json"), true, &error, {}, true),
			   "source dry run rejects replacing its input before writing");
	ok &= expect(guarded.save(path("guarded.assembly.json"), false, &error) && write(path("guarded.assembly.json"), "externally changed") &&
					 !guarded.save(path("guarded.assembly.json"), true, &error, {}, true),
				 "dry run detects stale loaded source");
	ok &= expect(readModelFile(path("part.mesh.json"), &bytes, &error) && bytes == fixture,
				 "all CLI operations preserve original model bytes");
	PackageStagingModel staging;
	ok &= expect(staging.createEmpty(PackageArchiveFormat::Pak, {}, &error) && staging.addBytes(fixture, "models/part.mesh.json", &error) &&
					 PackageDraft::save(path("models.vibepackage"), &staging, false, &error),
				 "portable draft with a generated model");
	ok &= expect(run({"model", "assembly", "--new", "--part", "root", "--kind", "package", "--model", "models/part.mesh.json", "--package",
					  path("models.vibepackage"), "--output", "package.assembly.json"}),
				 "CLI source resolves portable staged model bytes");
	ok &= expect(run({"model", "assembly", "package.assembly.json"}, 4), "package references require an explicit package context");
	ok &= expect(run({"model", "assembly", "package.assembly.json", "--package", path("models.vibepackage"), "--operation", "bake",
					  "--output", "package.md3"}) &&
					 output.value("vertices").toInt() == 3,
				 "CLI bakes a portable package snapshot through native exporter");
	ok &= expect(run({"model", "assembly", "package.assembly.json", "--package", path("models.vibepackage"), "--operation", "update",
					  "--part", "root", "--output", path("models.vibepackage/blocked.assembly.json"), "--dry-run"},
					 2) &&
					 !QFileInfo::exists(path("models.vibepackage/blocked.assembly.json")),
				 "source dry run protects the portable draft container");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
