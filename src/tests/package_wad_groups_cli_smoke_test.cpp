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
namespace { bool expect(bool value, const char* label) { if (!value) { std::cerr << label << '\n'; } return value; } }
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temporary; if (!temporary.isValid() || argc < 2) { return 1; }
	const QDir root(temporary.path()); bool ok = true; QString error;
	const auto settings = root.filePath("settings.ini"); StudioSettings::setOverrideFilePath(settings); StudioSettings().sync(); const auto initialSettings = get(settings);
	const auto source = root.filePath("maps.wad"); ok &= wad(source, fixture()); const auto original = get(source); const auto output = root.filePath("edited.wad");
	const auto cli = [&](const QStringList& args, int exit = 0) {
		QProcess process; process.setWorkingDirectory(root.path()); process.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "--settings-file", settings, "package"} + args + QStringList{"--json"});
		const bool ended = process.waitForFinished(15000); const auto bytes = process.readAllStandardOutput();
		if (!ended || process.exitCode() != exit) { std::cerr << args.join(' ').toStdString() << '\n' << bytes.toStdString() << process.readAllStandardError().toStdString(); }
		ok &= expect(ended && process.exitStatus() == QProcess::NormalExit && process.exitCode() == exit, "group CLI exit matches");
		QJsonParseError parse; const auto result = QJsonDocument::fromJson(bytes, &parse); ok &= expect(parse.error == QJsonParseError::NoError && result.isObject(), "group CLI emits JSON"); return result.object();
	};
	const auto inventory = cli({"groups", source}).value("inventory").toObject(); const auto fingerprint = inventory.value("fingerprint").toString();
	ok &= expect(fingerprint.size() == 64 && inventory.value("schemaVersion") == 1 && inventory.value("groups").toArray().size() == 7, "CLI inventory is complete and versioned");
	const QStringList rename{"--rename-group", "map:11", "--group-to", "MAP03", "--groups-fingerprint", fingerprint};
	auto result = cli(QStringList{"stage", source} + rename);
	ok &= expect(result.value("staging").toObject().value("operations").toArray().size() == 1, "CLI stages exact map label");
	cli(QStringList{"save-as", source, output, "--dry-run"} + rename);
	const auto draft = root.filePath("work.vibepackage"); cli(QStringList{"draft-save", source, draft, "--dry-run"} + rename);
	ok &= expect(!QFileInfo::exists(output) && !QFileInfo::exists(draft) && get(settings) == initialSettings && get(source) == original, "group dry runs leave sources, outputs and settings unchanged");
	for (const QStringList& invalid : {QStringList{"--rename-group", "map:11"}, {"--group-to", "MAP03"}, {"--delete-group", "map:11"},
		{"--rename-group=", "--group-to=MAP03", "--groups-fingerprint=" + fingerprint}, {"--delete-group=map:999", "--groups-fingerprint=" + fingerprint},
		{"--delete-group=map:11", "--groups-fingerprint=bad"}, {"--delete-group=map:11", "--groups-fingerprint=" + fingerprint, "--groups-fingerprint=" + fingerprint},
		{"--delete-group=map:11", "--groups-fingerprint=" + fingerprint, "--delete=MAP01"},
		{"--delete-group=map:11", "--delete-group=map:11", "--groups-fingerprint=" + fingerprint},
		{"--rename-group=map:11", "--group-to=MAP01", "--groups-fingerprint=" + fingerprint}}) { cli(QStringList{"draft-save", source, draft} + invalid, 2); }
	for (const QStringList& invalid : {QStringList{"--unknown"}, {"--dry-run"}, {"--json=false"}, {"extra"}, {"--locale="}, {"--locale=en", "--locale=en"}}) { cli(QStringList{"groups", source} + invalid, 2); }
	for (const QStringList& invalid : {QStringList{"--unknown"}, {"extra"}, {"--dry-run=false"}, {"--dry-run", "--dry-run"}, {"--format="}, {"--format=wad", "--format=wad"}}) {
		cli(QStringList{"save-as", source, output} + rename + invalid, 2);
	}
	cli({"stage", source, "--delete-group=namespace:33", "--delete-group=namespace:34", "--groups-fingerprint", fingerprint}, 2);
	cli({"stage", source, "--rename-group=texture-tables", "--group-to=OTHER", "--groups-fingerprint", fingerprint}, 2);
	ok &= expect(!QFileInfo::exists(draft), "rejected edits create no draft");
	cli(QStringList{"draft-save", source, draft} + rename); PackageStagingModel model;
	ok &= expect(PackageDraft::load(draft, &model, &error) && model.plannedEntries().at(11).virtualPath == "MAP03" && model.canUndo(), "group rename persists in portable draft");
	model.clear(); cli({"draft-undo", draft}); ok &= expect(PackageDraft::load(draft, &model, &error) && model.plannedEntries().at(11).virtualPath == "MAP02", "CLI undo restores whole group");
	model.clear(); cli({"draft-redo", draft});
	const auto next = cli({"groups", draft}).value("inventory").toObject().value("fingerprint").toString();
	const auto unchangedDraft = get(QDir(draft).filePath("document.json"));
	const auto compared = cli({"compare", draft, "--staged", "--delete-group=map:11", "--groups-fingerprint", next}, 4);
	ok &= expect(compared.contains("comparison") && compared.value("staging").toObject().value("operations").toArray().size() == 14
		&& get(QDir(draft).filePath("document.json")) == unchangedDraft, "staged comparison uses the same draft revision and preserves persisted history");
	cli({"compare", draft, draft});
	cli({"save-as", draft, output, "--delete-group=map:11", "--groups-fingerprint", next}); PackageArchive archive;
	ok &= expect(archive.load(output, &error) && index(archive, "MAP03") < 0 && index(archive, "MAP01") >= 0, "CLI draft group deletion writes only intended groups");
	ok &= expect(get(source) == original, "group edits preserve source archive bytes");
	const auto freshDraft = root.filePath("new.vibepackage"); auto expected = newWadFixture();
	QStringList create{"create", freshDraft, "--format", "wad", "--wad-magic", "PWAD"};
	for (const auto& [name, bytes] : expected) {
		const auto path = root.filePath("input-" + QString::fromLatin1(name)); ok &= put(path, bytes);
		create << "--add-file" << path << "--as" << QString::fromLatin1(name);
	}
	const auto settingsBeforeCreate = get(settings); cli(create + QStringList{"--dry-run"});
	ok &= expect(!QFileInfo::exists(freshDraft) && get(settings) == settingsBeforeCreate, "new WAD creation dry run leaves outputs and settings unchanged");
	cli(create); const auto freshInventory = cli({"groups", freshDraft}).value("inventory").toObject(); QString freshId;
	for (const auto& value : freshInventory.value("groups").toArray()) { if (value.toObject().value("name") == "INTRO") { freshId = value.toObject().value("id").toString(); } }
	ok &= expect(freshId.startsWith("map:new:") && freshInventory.value("groups").toArray().size() == 7, "CLI groups inspect source-free draft before any archive save");
	const QStringList freshRename{"--rename-group", freshId, "--group-to", "BEGIN", "--groups-fingerprint", freshInventory.value("fingerprint").toString()};
	const auto freshOutput = root.filePath("new.wad"); const auto originalDraft = get(QDir(freshDraft).filePath("document.json"));
	cli(QStringList{"save-as", freshDraft, freshOutput, "--dry-run"} + freshRename);
	ok &= expect(!QFileInfo::exists(freshOutput) && get(QDir(freshDraft).filePath("document.json")) == originalDraft,
		"new WAD group dry run keeps persisted order and history unchanged");
	cli(QStringList{"save-as", freshDraft, freshOutput} + freshRename);
	for (auto& [name, bytes] : expected) { Q_UNUSED(bytes); if (name == "INTRO") { name = "BEGIN"; } else if (name == "GL_INTRO") { name = "GL_BEGIN"; } }
	const auto expectedOutput = root.filePath("expected-new.wad"); ok &= wad(expectedOutput, expected);
	ok &= expect(get(freshOutput) == get(expectedOutput), "CLI new WAD rename writes the independently specified reviewed order");
	const auto freshSubset = root.filePath("new-subset.wad"); cli({"subset", freshDraft, freshSubset, "--entry", "CUSTOM"});
	ok &= wad(expectedOutput, {{"TITLEMAP", {}}, {"TEXTMAP", "namespace=\"zdoom\";"}, {"CUSTOM", "sidecar"}, {"ZNODES", "nodes"}, {"ENDMAP", {}}});
	ok &= expect(get(freshSubset) == get(expectedOutput) && get(QDir(freshDraft).filePath("document.json")) == originalDraft,
		"CLI new WAD subset keeps the full named UDMF run without changing its draft");
	const auto looseDraft = root.filePath("loose.vibepackage"); const auto looseMap = map("MAP01", "-loose");
	QStringList assemble{"create", looseDraft, "--format", "wad"};
	for (auto at = looseMap.crbegin(); at != looseMap.crend(); ++at) {
		const auto path = root.filePath("loose-" + QString::fromLatin1(at->first)); ok &= put(path, at->second);
		assemble << "--add-file" << path << "--as" << QString::fromLatin1(at->first);
	}
	cli(assemble); const auto looseInventory = cli({"groups", looseDraft}).value("inventory").toObject(); const auto looseGroups = looseInventory.value("groups").toArray();
	ok &= expect(looseGroups.size() == 1 && looseGroups.first().toObject().value("canRename").toBool(), "CLI new WAD inventory sees canonical assembly before saving an archive");
	const auto looseId = looseGroups.isEmpty() ? QString() : looseGroups.first().toObject().value("id").toString(); const auto looseOutput = root.filePath("loose.wad");
	cli({"save-as", looseDraft, looseOutput, "--rename-group", looseId, "--group-to", "INTRO", "--groups-fingerprint", looseInventory.value("fingerprint").toString()});
	ok &= wad(expectedOutput, map("INTRO", "-loose"));
	ok &= expect(get(looseOutput) == get(expectedOutput), "CLI marker rename preserves a map assembled from reversed input order");
	return ok ? 0 : 1;
}
