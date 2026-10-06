#include "tests/model_assembly_test_helpers.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <cmath>
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
QByteArray read(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
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
	QTemporaryDir temporary(QDir(root).filePath("collision-cli-XXXXXX"));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QString::fromLatin1(name)); };
	QJsonObject output;
	const auto run = [&](QStringList args, int expected = 0)
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
		if (process.exitCode() != expected || output.isEmpty())
		{
			std::cerr << args.join(' ').toStdString() << " exit=" << process.exitCode() << " " << bytes.toStdString()
					  << process.readAllStandardError().toStdString() << '\n';
			return false;
		}
		return process.exitStatus() == QProcess::NormalExit;
	};
	const auto original = QJsonDocument(editableModelJson(tests::assemblyModel())).toJson();
	bool ok = expect(write(path("original.mesh.json"), original), "source fixture");
	const QStringList prefix{"model", "collision", "original.mesh.json"};
	const QStringList add = prefix + QStringList{"--operation", "add",		  "--name", "body",		"--size",
												 "16,24,32",	"--rotation", "0,0,45", "--output", "edited.mesh.json"};
	ok &= expect(run(prefix) && output["collisionBoxes"].toArray().isEmpty(), "inspect is read-only");
	ok &= expect(run(prefix + QStringList{"--locale", "en", "--catalog-root", temporary.path()}),
				 "normal CLI locale options remain accepted");
	ok &= expect(run(prefix + QStringList{"--language", "en"}, 2), "unrelated language-server option is refused");
	ok &= expect(run(add + QStringList{"--dry-run"}) && !QFileInfo::exists(path("edited.mesh.json")) &&
					 output["collisionBoxes"].toArray().size() == 1 && !output["written"].toBool(),
				 "authoring dry run validates full result");
	ok &= expect(run(add) && output["written"].toBool(), "add collision");
	ok &= expect(run({"model", "collision", "edited.mesh.json", "--operation", "update", "--box", "body", "--centre", "10,20,30", "--name",
					  "renamed", "--output", "edited.mesh.json"}) &&
					 output["collisionBoxes"].toArray()[0].toObject()["name"] == "renamed",
				 "guarded same-source edit and rename");
	QProcess inspection;
	inspection.setWorkingDirectory(temporary.path());
	inspection.start(QString::fromLocal8Bit(argv[1]),
					 {"--cli", "--settings-file", path("settings.ini"), "model", "collision", "edited.mesh.json"});
	if (!inspection.waitForFinished(30000))
	{
		inspection.kill();
		inspection.waitForFinished();
		return EXIT_FAILURE;
	}
	const auto summary = inspection.readAllStandardOutput();
	ok &= expect(inspection.exitCode() == 0 && summary.contains("renamed:") && summary.contains("10,20,30") && summary.contains("16,24,32"),
				 "text inspection identifies boxes and editable values");
	const QStringList transform{"model",		"collision",   "edited.mesh.json",
								"--operation",	"transform",   "--box",
								"renamed",		"--offset",	   "1.4,0,0",
								"--rotate",		"0,0,43",	   "--scale",
								"1.37,1,1",		"--snap-grid", "1",
								"--snap-angle", "15",		   "--snap-scale",
								"0.25",			"--output",	   "transformed.mesh.json"};
	const auto beforeTransform = read(path("edited.mesh.json"));
	ok &= expect(run(transform + QStringList{"--dry-run"}) && !QFileInfo::exists(path("transformed.mesh.json")),
				 "transform dry run does not write");
	ok &= expect(run(transform), "CLI transform uses shared snapped collision operation");
	const auto transformed = output["collisionBoxes"].toArray()[0].toObject();
	ok &= expect(transformed["centre"].toArray() == QJsonArray{11, 20, 30} && transformed["size"].toArray() == QJsonArray{20, 24, 32} &&
					 std::abs(transformed["rotation"].toArray()[2].toDouble() - 90) < .001 &&
					 read(path("edited.mesh.json")) == beforeTransform,
				 "CLI selection pivot, local scale and composed world rotation preserve source");
	for (const auto &invalid : QList<QStringList>{{"--frame", "0"},
												  {"--rotation", "0,0,45"},
												  {"--name", "ignored"},
												  {"--pivot-mode", "origin", "--pivot", "1,2,3"},
												  {"--pivot-mode", "custom"},
												  {"--pivot-mode", "unsupported"}})
	{
		ok &= expect(run(transform + invalid + QStringList{"--dry-run"}, 2), "transform refuses ignored or contradictory options");
	}
	ok &= expect(run({"model", "collision", "edited.mesh.json", "--operation", "transform", "--box", "renamed", "--scale", "0,1,1",
					  "--output", "invalid.mesh.json"},
					 4) &&
					 !QFileInfo::exists(path("invalid.mesh.json")),
				 "invalid transform never publishes a file");
	ok &= expect(run({"model", "collision", "edited.mesh.json", "--operation", "transform", "--box", "renamed", "--rotate", "0,0,90",
					  "--pivot-mode", "origin", "--output", "origin.mesh.json"}),
				 "explicit origin pivot accepted");
	const auto centre = output["collisionBoxes"].toArray()[0].toObject()["centre"].toArray();
	ok &= expect(centre.size() == 3 && std::abs(centre[0].toDouble() + 20) < .001 && std::abs(centre[1].toDouble() - 10) < .001,
				 "origin pivot rotates box centre independently of local dimensions");
	ok &= expect(run({"model", "collision", "edited.mesh.json", "--operation", "duplicate", "--box", "renamed", "--name", "copy",
					  "--output", "duplicate.mesh.json"}) &&
					 output["collisionBoxes"].toArray().size() == 2,
				 "duplicate collision");
	ok &= expect(
		run({"model", "collision", "duplicate.mesh.json", "--operation", "delete", "--box", "copy", "--output", "deleted.mesh.json"}) &&
			output["collisionBoxes"].toArray().size() == 1,
		"delete collision");
	ok &= expect(run(prefix + QStringList{"--operation", "fit", "--name", "single", "--vertices", "2", "--surface", "0", "--frame", "1",
										  "--output", "fit.mesh.json"}) &&
					 output["collisionBoxes"].toArray()[0].toObject()["centre"].toArray() == QJsonArray{0, 4, 1},
				 "fit component frame scope matches GUI service");
	for (const auto &target : {QStringLiteral("quake"), QStringLiteral("quake2"), QStringLiteral("quake3")})
	{
		const QString outputPath = target + ".map";
		QStringList request{"model", "collision", "edited.mesh.json", "--operation", "export-map", "--target",
							target,	 "--origin",  "40,50,60",		  "--output",	 outputPath};
		if (target == "quake3")
		{
			request += {"--material", "common/playerclip"};
		}
		ok &= expect(run(request + QStringList{"--dry-run"}) && !QFileInfo::exists(QDir(temporary.path()).filePath(outputPath)),
					 "map dry run writes no file");
		ok &= expect(run(request) && output["brushes"].toInt() == 1 && !output["notes"].toArray().isEmpty(),
					 "target-specific clip map and caveats");
		ok &= expect(run(request, 4) && run(request + QStringList{"--overwrite"}), "existing derivative overwrite explicit");
		QStringList placement{"model",	  "collision", "edited.mesh.json", "--operation",		  "place", "--map", outputPath,
							  "--target", target,	   "--output",		   "placed-" + outputPath};
		if (target == "quake3")
		{
			placement += {"--material", "common/playerclip"};
		}
		ok &= expect(run(placement), "collision placement through shared map service");
		placement[placement.indexOf("--output") + 1] = outputPath;
		const auto before = read(QDir(temporary.path()).filePath(outputPath));
		ok &= expect(run(placement + QStringList{"--overwrite"}, 4) && read(QDir(temporary.path()).filePath(outputPath)) == before,
					 "CLI placement protects original map even with overwrite");
	}
	for (const auto &bad : QVector<QStringList>{
			 {"--output", "unwanted.map"}, {"--name", "ignored"}, {"--operation", "inspect", "--operation", "fit"}, {"--unknown"}})
	{
		ok &= expect(run(prefix + bad, 2), "strict unrelated or repeated options");
	}
	ok &= expect(run(prefix + QStringList{"--operation", "add", "--name", "bad", "--size", "nan,2,3", "--output", "bad.mesh.json"}, 2) &&
					 !QFileInfo::exists(path("bad.mesh.json")),
				 "nonfinite CLI values refused");
	ok &= expect(run(prefix + QStringList{"--operation", "fit", "--name", "bad", "--surface", "0", "--output", "bad.mesh.json"}, 2),
				 "surface without components is not silently ignored");
	ok &= expect(
		run({"model", "collision", "edited.mesh.json", "--operation", "export-map", "--target", "quake3", "--output", "bad.map"}, 4) &&
			!QFileInfo::exists(path("bad.map")),
		"Quake III requires explicit shader dependency");
	ok &= expect(read(path("original.mesh.json")) == original, "original source unchanged");
	std::cout << (ok ? "Collision CLI checks passed.\n" : "Collision CLI checks failed.\n");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
