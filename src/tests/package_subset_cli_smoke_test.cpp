#include "tests/package_subset_test_helpers.h"
#include "core/package_draft.h"
#include "core/package_staging.h"
#include "core/studio_settings.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::subset_test;
namespace {
bool expect(bool value, const char* label) { if (!value) { std::cerr << label << '\n'; } return value; }
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temporary; if (!temporary.isValid() || argc < 2) { return 1; }
	const QDir root(temporary.path()); bool ok = true; QString error;
	const auto settings = root.filePath("settings.ini"); StudioSettings::setOverrideFilePath(settings); StudioSettings().sync();
	const auto settingsBefore = get(settings); const auto source = root.filePath("maps.wad");
	PackageArchive archive; ok &= wad(source, fixture()) && archive.load(source, &error);
	if (!expect(ok, "prepare CLI subset source")) { return 1; }
	const auto sourceBefore = get(source); const auto second = QString::number(index(archive, "THINGS", 1));
	const auto output = root.filePath("subset.wad");
	const auto cli = [&](const QStringList& args, int exit = 0) {
		QProcess process; process.setWorkingDirectory(root.path());
		process.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "--settings-file", settings, "package"} + args + QStringList{"--json"});
		const bool ended = process.waitForFinished(15000); const auto out = process.readAllStandardOutput();
		if (!ended || process.exitCode() != exit) { std::cerr << args.join(' ').toStdString() << '\n' << out.toStdString() << process.readAllStandardError().toStdString(); }
		ok &= expect(ended && process.exitStatus() == QProcess::NormalExit && process.exitCode() == exit, "CLI exit matches");
		QJsonParseError parse; const auto json = QJsonDocument::fromJson(out, &parse);
		ok &= expect(parse.error == QJsonParseError::NoError && json.isObject(), "subset CLI emits valid JSON"); return json.object();
	};
	const auto result = cli({"subset", source, output, "--entry-index", second, "--dry-run"});
	const auto reviewed = result.value("subset").toObject().value("entries").toArray(); int selected = 0;
	for (const auto& member : reviewed) { selected += member.toObject().value("selected").toBool(); }
	ok &= expect(reviewed.size() == 13 && selected == 1 && reviewed.first().toObject().value("virtualPath") == "MAP02", "dry run identifies exact selection and complete map group");
	ok &= expect(!QFileInfo::exists(output) && get(settings) == settingsBefore && get(source) == sourceBefore, "subset dry run preserves files and preferences");
	for (const QStringList& options : {QStringList{}, {"--entry-index=-1"}, {"--entry-index=+1"}, {"--entry-index=9999"}, {"--entry-index=4294967296"}, {"--entry-index=9223372036854775808"},
		{"--entry-index="}, {"--entry-index"}, {"--entry", "THINGS"}, {"--prefix="}, {"--where="}, {"--entry=PLAYPAL", "--where=name=PLAYPAL", "--where=name=PLAYPAL"},
		{"--entry=PLAYPAL", "--dry-run", "--dry-run"}, {"--entry=PLAYPAL", "--json=false"}, {"--entry=PLAYPAL", "--unknown"},
		{"--entry=PLAYPAL", "--in-place"}, {"--entry=PLAYPAL", "--delete=PLAYPAL"}, {"--entry=PLAYPAL", "--map-input=map.map"},
		{"--entry=PLAYPAL", "--format=wrong"}, {"--entry=PLAYPAL", "--compression=wrong"}, {"--entry=PLAYPAL", "extra"}}) {
		cli(QStringList{"subset", source, output} + options, 2);
	}
	ok &= expect(!QFileInfo::exists(output) && get(settings) == settingsBefore, "invalid options never publish output or mutate settings");
	cli({"subset", source, "--output", output, "--entry-index=" + second});
	PackageArchive exported; QByteArray bytes;
	ok &= expect(exported.load(output, &error) && exported.readEntryBytes("THINGS", &bytes, &error) && bytes == "THINGS-second", "CLI exports second occurrence bytes");
	cli({"subset", source, output, "--entry-index=" + second}, 4);
	cli({"subset", source, output, "--entry-index=" + second, "--overwrite"});
	cli({"subset", source, source, "--entry-index=" + second, "--overwrite"}, 4);
	ok &= expect(get(source) == sourceBefore, "source overwrite is refused");
	const auto both = root.filePath("both.wad");
	ok &= expect(cli({"subset", source, both, "--where=name=THINGS"}).value("subset").toObject().value("entries").toArray().size() == 24,
		"query includes both map occurrences and their groups");
	PackageStagingModel plan; ok &= plan.loadBaseArchive(archive, &error); const auto replacement = root.filePath("replacement.bin");
	ok &= put(replacement, "staged") && plan.replaceOccurrence(12, replacement, &error);
	const auto draft = root.filePath("work.vibepackage"); ok &= PackageDraft::save(draft, &plan, false, &error);
	const auto draftView = packagePlannedArchive(plan); const auto draftIndex = QString::number(index(draftView, "THINGS", 1));
	cli({"subset", draft, root.filePath("draft.wad"), "--entry-index", draftIndex});
	ok &= expect(exported.load(root.filePath("draft.wad"), &error) && exported.readEntryBytes("THINGS", &bytes, &error) && bytes == "staged", "CLI subsets read staged draft bytes");
	PackageStagingModel restored; ok &= expect(PackageDraft::load(draft, &restored, &error) && restored.canUndo(), "CLI export preserves draft history");
	const auto malformed = root.filePath("broken.wad"); ok &= wad(malformed, {{"MAP01", {}}, {"THINGS", "x"}});
	cli({"subset", malformed, root.filePath("broken-out.wad"), "--entry=THINGS"}, 4);
	ok &= expect(!QFileInfo::exists(root.filePath("broken-out.wad")), "malformed group blocks CLI output");
	return ok ? 0 : 1;
}
