// The `release` and `install register` CLI families end to end, run as a
// separate process against generated installations and projects (no game
// data): indexing, checking, exporting, planning, notes, the changelog,
// publishing, history, the catalog, and their usage errors and exit codes.

#include "tests/release_test_fixtures.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <iostream>

namespace fixtures = vibestudio::tests::release;

namespace {

int failures = 0;

bool expect(bool condition, const std::string& message, const QString& detail = QString())
{
	if (!condition) {
		std::cerr << "FAIL: " << message;
		if (!detail.isEmpty()) {
			std::cerr << "\n  " << detail.toStdString();
		}
		std::cerr << '\n';
		++failures;
	}
	return condition;
}

struct Run {
	int exitCode = -1;
	QString out;
	QString err;
	QJsonObject json;
};

QString binary;
QString settingsFile;
QString workingDirectory;

Run run(QStringList arguments, const QString& settings = QString())
{
	QProcess process;
	process.setWorkingDirectory(workingDirectory);
	arguments.prepend(settings.isEmpty() ? settingsFile : settings);
	arguments.prepend(QStringLiteral("--settings-file"));
	arguments.prepend(QStringLiteral("--cli"));
	process.start(binary, arguments);
	Run result;
	if (!process.waitForFinished(120000)) {
		process.kill();
		result.err = QStringLiteral("timed out");
		return result;
	}
	result.exitCode = process.exitCode();
	result.out = QString::fromUtf8(process.readAllStandardOutput());
	result.err = QString::fromUtf8(process.readAllStandardError());
	if (arguments.contains(QStringLiteral("--json"))) {
		result.json = QJsonDocument::fromJson(result.out.toUtf8()).object();
	}
	return result;
}

QString describe(const Run& run)
{
	return QStringLiteral("exit %1\n%2\n%3").arg(run.exitCode).arg(run.out.left(1500), run.err.left(800));
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	if (argc < 2) {
		std::cerr << "usage: release_cli_smoke_test <vibestudio>\n";
		return 2;
	}
	binary = QString::fromLocal8Bit(argv[1]);
	QTemporaryDir temp;
	workingDirectory = temp.path();
	settingsFile = QDir(temp.path()).filePath(QStringLiteral("settings.ini"));
	const QString game = QDir(temp.path()).filePath(QStringLiteral("q3"));
	const QString project = QDir(temp.path()).filePath(QStringLiteral("mymod"));
	expect(fixtures::makeQuake3Installation(game) && fixtures::makeQuake3Project(project), "the fixtures are written");

	Run added = run({QStringLiteral("install"), QStringLiteral("add"), game, QStringLiteral("--install-game"), QStringLiteral("quake3"),
		QStringLiteral("--install-name"), QStringLiteral("Fake Q3"), QStringLiteral("--json")});
	const QString id = added.json.value(QStringLiteral("installation")).toObject().value(QStringLiteral("id")).toString();
	expect(added.exitCode == 0 && !id.isEmpty(), "the installation is added", describe(added));

	// The register.
	Run info = run({QStringLiteral("install"), QStringLiteral("register"), QStringLiteral("info"), id, QStringLiteral("--json")});
	expect(info.exitCode == 4 && !info.json.value(QStringLiteral("status")).toObject().value(QStringLiteral("exists")).toBool(),
		"an unindexed installation reports exit 4", describe(info));
	Run dry = run({QStringLiteral("install"), QStringLiteral("register"), QStringLiteral("build"), id, QStringLiteral("--dry-run"), QStringLiteral("--json")});
	const QString registerPath = dry.json.value(QStringLiteral("path")).toString();
	expect(dry.exitCode == 0 && !dry.json.value(QStringLiteral("written")).toBool() && !QFileInfo::exists(registerPath),
		"a dry-run index writes nothing", describe(dry));
	Run built = run({QStringLiteral("install"), QStringLiteral("register"), QStringLiteral("build"), id, QStringLiteral("--json")});
	expect(built.exitCode == 0 && QFileInfo::exists(built.json.value(QStringLiteral("path")).toString())
		&& built.json.value(QStringLiteral("register")).toObject().value(QStringLiteral("files")).toInt() == 9, "indexing writes the register", describe(built));
	info = run({QStringLiteral("install"), QStringLiteral("register"), QStringLiteral("info"), id, QStringLiteral("--json")});
	expect(info.exitCode == 0 && info.json.value(QStringLiteral("status")).toObject().value(QStringLiteral("usable")).toBool(), "the index is usable", describe(info));
	Run check = run({QStringLiteral("install"), QStringLiteral("register"), QStringLiteral("check"), id, QStringLiteral("textures/base_wall/wall1.tga"),
		QStringLiteral("textures/base_wall/glass"), QStringLiteral("textures/mymod/new.tga"), QStringLiteral("--json")});
	const QJsonArray checks = check.json.value(QStringLiteral("checks")).toArray();
	expect(check.exitCode == 0 && checks.size() == 3 && checks[0].toObject().value(QStringLiteral("stock")).toBool()
		&& checks[1].toObject().value(QStringLiteral("shaderScript")).toString() == QStringLiteral("scripts/base_wall.shader")
		&& !checks[2].toObject().value(QStringLiteral("stock")).toBool(), "check names stock files, stock shaders and new files", describe(check));
	Run compare = run({QStringLiteral("install"), QStringLiteral("register"), QStringLiteral("check"), id, QStringLiteral("--file"),
		QDir(project).filePath(QStringLiteral("textures/base_wall/crate.tga")), QStringLiteral("--as"), QStringLiteral("textures/base_wall/crate.tga"), QStringLiteral("--json")});
	expect(compare.json.value(QStringLiteral("checks")).toArray().first().toObject().value(QStringLiteral("match")).toString() == QStringLiteral("different"),
		"comparing a project file reports a replacement", describe(compare));
	const QString exported = QDir(temp.path()).filePath(QStringLiteral("quake3.register.json"));
	Run exportRun = run({QStringLiteral("install"), QStringLiteral("register"), QStringLiteral("export"), id, QStringLiteral("--output"), exported});
	expect(exportRun.exitCode == 0 && QFileInfo::exists(exported), "the register exports", describe(exportRun));

	// Planning.
	const QStringList map {QStringLiteral("--map"), QStringLiteral("maps/arena1.map")};
	Run plan = run(QStringList {QStringLiteral("release"), QStringLiteral("plan"), project} + map + QStringList {QStringLiteral("--installation"), id, QStringLiteral("--json")});
	const QJsonObject planJson = plan.json.value(QStringLiteral("plan")).toObject();
	expect(plan.exitCode == 0 && planJson.value(QStringLiteral("canPublish")).toBool() && planJson.value(QStringLiteral("files")).toInt() == 10
		&& planJson.value(QStringLiteral("package")).toString() == QStringLiteral("thepit.pk3") && plan.json.value(QStringLiteral("stock")).toObject().value(QStringLiteral("available")).toBool(),
		"the map plan is ready, against the installation's index", describe(plan));
	const QString emptySettings = QDir(temp.path()).filePath(QStringLiteral("empty.ini"));
	Run offline = run(QStringList {QStringLiteral("release"), QStringLiteral("plan"), project} + map + QStringList {QStringLiteral("--register"), exported, QStringLiteral("--json")}, emptySettings);
	expect(offline.exitCode == 0 && offline.json.value(QStringLiteral("plan")).toObject().value(QStringLiteral("stockChecked")).toBool()
		&& offline.json.value(QStringLiteral("plan")).toObject().value(QStringLiteral("files")).toInt() == 10,
		"an exported register checks a release on a machine without the game", describe(offline));
	Run unchecked = run(QStringList {QStringLiteral("release"), QStringLiteral("plan"), project} + map + QStringList {QStringLiteral("--json")}, emptySettings);
	expect(unchecked.exitCode == 0 && !unchecked.json.value(QStringLiteral("plan")).toObject().value(QStringLiteral("stockChecked")).toBool()
		&& !unchecked.json.value(QStringLiteral("stock")).toObject().value(QStringLiteral("warnings")).toArray().isEmpty(),
		"without an installation the plan says the game was not checked", describe(unchecked));
	Run textPlan = run(QStringList {QStringLiteral("release"), QStringLiteral("plan"), project} + map + QStringList {QStringLiteral("--installation"), id});
	expect(textPlan.exitCode == 0 && textPlan.out.contains(QStringLiteral("Ready to publish")) && textPlan.out.contains(QStringLiteral("thepit.pk3")),
		"the plan reads as text", describe(textPlan));

	// The map's dependency check knows the game's files too.
	Run dependencies = run({QStringLiteral("map"), QStringLiteral("dependencies"), QDir(project).filePath(QStringLiteral("maps/arena1.map")), QStringLiteral("--package"), project,
		QStringLiteral("--engine"), QStringLiteral("idTech3"), QStringLiteral("--installation"), id, QStringLiteral("--json")});
	const QJsonObject dependencyJson = dependencies.json.value(QStringLiteral("dependencies")).toObject();
	expect(dependencies.exitCode == 0 && dependencyJson.value(QStringLiteral("stock")).toInt() >= 4, "map dependencies lists what the game provides", describe(dependencies));
	Run unknownInstall = run({QStringLiteral("map"), QStringLiteral("dependencies"), QDir(project).filePath(QStringLiteral("maps/arena1.map")), QStringLiteral("--package"), project,
		QStringLiteral("--installation"), QStringLiteral("no-such-install")});
	expect(unknownInstall.exitCode == 3, "map dependencies refuses an unknown installation", describe(unknownInstall));

	// Notes and the changelog.
	Run notes = run(QStringList {QStringLiteral("release"), QStringLiteral("notes"), project} + map + QStringList {QStringLiteral("--installation"), id, QStringLiteral("--json")});
	expect(notes.exitCode == 0 && notes.json.value(QStringLiteral("notes")).toString().contains(QStringLiteral("# The Pit 1.0.0"))
		&& notes.json.value(QStringLiteral("changes")).toArray().first().toObject().value(QStringLiteral("text")).toString() == QStringLiteral("First release."),
		"notes suggest a first release", describe(notes));
	Run readme = run(QStringList {QStringLiteral("release"), QStringLiteral("notes"), project} + map + QStringList {QStringLiteral("--installation"), id, QStringLiteral("--readme")});
	expect(readme.exitCode == 0 && readme.out.contains(QStringLiteral("Title                   : The Pit")), "--readme prints the player readme", describe(readme));
	Run change = run({QStringLiteral("release"), QStringLiteral("changelog"), project, QStringLiteral("--add"), QStringLiteral("New arena: The Pit"),
		QStringLiteral("--category"), QStringLiteral("added"), QStringLiteral("--json")});
	expect(change.exitCode == 0 && change.json.value(QStringLiteral("added")).toBool() && change.json.value(QStringLiteral("unreleased")).toArray().size() == 1
		&& QFileInfo::exists(QDir(project).filePath(QStringLiteral("CHANGELOG.md"))), "a change is recorded in CHANGELOG.md", describe(change));

	// Publishing.
	const QStringList publishArgs = QStringList {QStringLiteral("release"), QStringLiteral("publish"), project} + map + QStringList {QStringLiteral("--installation"), id};
	Run dryPublish = run(publishArgs + QStringList {QStringLiteral("--dry-run"), QStringLiteral("--json")});
	const QJsonObject dryPublication = dryPublish.json.value(QStringLiteral("publication")).toObject();
	expect(dryPublish.exitCode == 0 && dryPublication.value(QStringLiteral("dryRun")).toBool() && !QFileInfo::exists(dryPublication.value(QStringLiteral("outputDirectory")).toString()),
		"a dry-run publish writes nothing", describe(dryPublish));
	Run published = run(publishArgs + QStringList {QStringLiteral("--json")});
	const QJsonObject publication = published.json.value(QStringLiteral("publication")).toObject();
	expect(published.exitCode == 0 && QFileInfo::exists(publication.value(QStringLiteral("package")).toString())
		&& QFileInfo::exists(publication.value(QStringLiteral("archive")).toString()) && QFileInfo::exists(publication.value(QStringLiteral("readme")).toString())
		&& QFileInfo::exists(publication.value(QStringLiteral("notes")).toString()) && QFileInfo::exists(publication.value(QStringLiteral("record")).toString()),
		"publishing writes the package, archive, readme, notes and record", describe(published));
	QFile changelog(QDir(project).filePath(QStringLiteral("CHANGELOG.md")));
	expect(changelog.open(QIODevice::ReadOnly) && QString::fromUtf8(changelog.readAll()).contains(QStringLiteral("## [1.0.0]")), "the changelog gains the released version");
	Run again = run(publishArgs + QStringList {QStringLiteral("--json")});
	expect(again.exitCode == 2 && again.json.value(QStringLiteral("message")).toString().contains(QStringLiteral("--release-version 1.0.1")),
		"a released version is refused with the next version suggested", describe(again));
	Run next = run(publishArgs + QStringList {QStringLiteral("--release-version"), QStringLiteral("1.0.1"), QStringLiteral("--no-archive"), QStringLiteral("--json")});
	expect(next.exitCode == 0 && next.json.value(QStringLiteral("publication")).toObject().value(QStringLiteral("archive")).toString().isEmpty(),
		"the next version publishes, without an archive when asked", describe(next));
	Run history = run({QStringLiteral("release"), QStringLiteral("history"), project, QStringLiteral("--json")});
	const QJsonArray releases = history.json.value(QStringLiteral("releases")).toArray();
	expect(history.exitCode == 0 && releases.size() == 2 && releases.last().toObject().value(QStringLiteral("version")).toString() == QStringLiteral("1.0.1"),
		"history lists both releases", describe(history));
	Run catalog = run({QStringLiteral("release"), QStringLiteral("catalog"), project, QStringLiteral("--json")});
	const QJsonArray maps = catalog.json.value(QStringLiteral("maps")).toArray();
	expect(catalog.exitCode == 0 && maps.size() == 1 && maps.first().toObject().value(QStringLiteral("built")).toBool(), "the catalog lists the built map", describe(catalog));

	// Usage errors and exit codes.
	Run mixed = run({QStringLiteral("release"), QStringLiteral("plan"), project, QStringLiteral("--map"), QStringLiteral("a.map"), QStringLiteral("--model"), QStringLiteral("b.md3")});
	expect(mixed.exitCode == 2, "two kinds of items are refused", describe(mixed));
	Run unknown = run({QStringLiteral("release"), QStringLiteral("frob"), project});
	expect(unknown.exitCode == 2 && !unknown.err.contains(QStringLiteral("Unknown VibeStudio CLI subcommand")), "an unknown release action is a usage error", describe(unknown));
	Run noProject = run({QStringLiteral("release"), QStringLiteral("plan")});
	expect(noProject.exitCode == 3, "a working folder without a manifest is not scanned", describe(noProject));
	Run noItems = run({QStringLiteral("release"), QStringLiteral("plan"), project, QStringLiteral("--scope"), QStringLiteral("maps")});
	expect(noItems.exitCode == 2, "a map release needs maps", describe(noItems));
	Run publishOnly = run({QStringLiteral("release"), QStringLiteral("plan"), project, QStringLiteral("--output"), temp.path()});
	expect(publishOnly.exitCode == 2, "publish options are refused elsewhere", describe(publishOnly));
	Run registerUsage = run({QStringLiteral("install"), QStringLiteral("register")});
	expect(registerUsage.exitCode == 2, "install register needs an action and id", describe(registerUsage));
	Run missingInstall = run({QStringLiteral("install"), QStringLiteral("register"), QStringLiteral("build"), QStringLiteral("no-such-install")});
	expect(missingInstall.exitCode == 3, "an unknown installation is not found", describe(missingInstall));
	Run bad = run({QStringLiteral("release"), QStringLiteral("plan"), project, QStringLiteral("--release-version"), QStringLiteral("../x")});
	expect(bad.exitCode == 2, "an unsafe version is refused", describe(bad));

	if (failures > 0) {
		std::cerr << failures << " failure(s)\n";
		return 1;
	}
	std::cout << "release CLI smoke passed\n";
	return 0;
}
