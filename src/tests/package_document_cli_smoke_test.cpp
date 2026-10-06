#include "core/package_draft.h"
#include "package_summary_test_fixture.h"
#include "core/package_validation.h"
#include "package_legacy_test_helpers.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

#include <iostream>
#include <limits>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; }
	return value;
}
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temporary;
	if (!temporary.isValid() || argc < 2) { return 1; }
	QDir root(temporary.path()); bool ok = true; QString error;
	const QString input = root.filePath(QStringLiteral("input.txt"));
	QFile file(input); if (!file.open(QIODevice::WriteOnly) || file.write("original") != 8) { return 1; } file.close();
	const QString blockedTemporary = root.filePath(QStringLiteral("blocked-temporary"));
	{ QFile blocked(blockedTemporary); if (!blocked.open(QIODevice::WriteOnly) || blocked.write("file") != 4) { return 1; } }
	const auto cli = [&](const QStringList& arguments, int expectedExit = 0, bool noTemporary = false) {
		QProcess process; process.setWorkingDirectory(root.path());
		if (noTemporary) {
			auto environment = QProcessEnvironment::systemEnvironment();
			for (const QString& name : {QStringLiteral("TEMP"), QStringLiteral("TMP"), QStringLiteral("TMPDIR")}) { environment.insert(name, blockedTemporary); }
			process.setProcessEnvironment(environment);
		}
		process.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "--settings-file", root.filePath("settings.ini"), "package"} + arguments + QStringList{"--json"});
		const bool ended = process.waitForFinished(15000);
		const auto bytes = process.readAllStandardOutput();
		if (!ended || process.exitCode() != expectedExit) { std::cerr << arguments.join(' ').toStdString() << '\n' << bytes.toStdString() << process.readAllStandardError().toStdString(); }
		ok &= expect(ended && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expectedExit, "document CLI exit status");
		const auto document = QJsonDocument::fromJson(bytes);
		ok &= expect(document.isObject(), "document CLI always returns JSON");
		return document.object();
	};
	for (const QString& extension : {QStringLiteral("pak"), QStringLiteral("zip"), QStringLiteral("pk3"), QStringLiteral("wad")}) {
		const QString output = root.filePath(QStringLiteral("empty.") + extension);
		cli({"create", output, "--dry-run"});
		ok &= expect(!QFileInfo::exists(output), "empty archive dry run writes nothing");
		cli({"create", output});
		PackageArchive archive;
		ok &= expect(archive.load(output, &error) && archive.entries().isEmpty() && validatePackage(archive).valid(), "CLI creates valid empty archives");
		const auto original = read(output);
		cli({"create", output}, 4);
		ok &= expect(read(output) == original, "existing archive requires explicit overwrite");
	}
	// Edit/view admission must precede publication in both execution modes. The
	// CLI dry run also works with a deliberately unusable temporary directory.
	QString deepPath; for (int index = 0; index < 128; ++index) { deepPath += "a/"; } deepPath += "file.txt";
	const QString tooDeep = root.filePath("depth-refused.zip");
	const auto depthDry = cli({"create", tooDeep, "--add-file", input, "--as", deepPath, "--dry-run"}, 2, true);
	ok &= expect(!QFileInfo::exists(tooDeep) && QJsonDocument(depthDry).toJson().contains("depth"),
		"Over-depth dry run refuses its edit before output without temporary payload storage.");
	cli({"create", tooDeep}); const auto priorOutput = read(tooDeep);
	cli({"create", tooDeep, "--add-file", input, "--as", deepPath, "--overwrite"}, 2);
	ok &= expect(read(tooDeep) == priorOutput && !QFileInfo::exists(tooDeep + ".bak") && read(input) == "original",
		"Index refusal preserves a reviewed output and its source before backup publication.");
	const QString depthBoundary = root.filePath("depth-boundary.zip");
	cli({"create", depthBoundary, "--add-file", input, "--as", deepPath.mid(2)});
	cli({"info", depthBoundary});
	const QString depthDraft = root.filePath("depth.vibepackage");
	cli({"create", depthDraft, "--add-file", input, "--as", deepPath.mid(2)});
	const auto depthManifest = QDir(depthDraft).filePath("document.json");
	const auto admittedDraft = read(depthManifest);
	cli({"draft-stage", depthDraft, "--add-file", input, "--as", deepPath}, 2);
	ok &= expect(read(depthManifest) == admittedDraft, "A refused staged batch preserves the saved draft bytes and history.");
	// Model a legacy draft predating edit-time depth admission. Content objects
	// remain unchanged; both active/history paths use the older deep spelling.
	QByteArray legacyMetadata = admittedDraft; legacyMetadata.replace(deepPath.mid(2).toUtf8(), deepPath.toUtf8());
	{ QFile legacy(depthManifest); ok &= expect(legacy.open(QIODevice::WriteOnly) && legacy.write(legacyMetadata) == legacyMetadata.size(), "Prepare legacy over-depth history."); }
	const auto draftBefore = read(depthManifest);
	cli({"save-as", depthDraft, root.filePath("draft-depth.zip")}, 4);
	ok &= expect(read(QDir(depthDraft).filePath("document.json")) == draftBefore, "Failed export retains the draft and its undo history.");
	cli({"draft-undo", depthDraft});
	cli({"save-as", depthDraft, root.filePath("draft-depth.zip")});
	const QString draft = root.filePath(QStringLiteral("new.vibepackage"));
	const QStringList create{"create", draft, "--format", "pk3", "--mkdir", "source/empty", "--add-file", input, "--as", "source/deep/input.txt", "--rename-folder", "source", "--folder-to", "moved"};
	const auto checked = cli(create + QStringList{"--dry-run"});
	ok &= expect(checked.value("dryRun").toBool() && !checked.value("written").toBool() && !QFileInfo::exists(draft), "draft dry run verifies without creating objects or locks");
	cli(create);
	PackageStagingModel plan;
	ok &= expect(PackageDraft::load(draft, &plan, &error) && plan.sourcePath().isEmpty() && plan.operations().size() == 3
		&& plan.canUndo() && plan.summary().canSave, "CLI creates a source-free draft with grouped folder history");
	QByteArray payload; PackageStagingArchive(plan).readEntryBytes("moved/deep/input.txt", &payload, &error);
	ok &= expect(payload == "original", "folder rename preserves imported bytes");
	const auto manifest = read(QDir(draft).filePath("document.json"));
	cli(create, 1);
	ok &= expect(read(QDir(draft).filePath("document.json")) == manifest, "existing draft is preserved");
	cli({"draft-undo", draft});
	ok &= expect(PackageDraft::load(draft, &plan, &error) && plan.operations().isEmpty() && plan.canRedo() && plan.sourcePath().isEmpty(), "one CLI undo returns to the empty document");
	cli({"draft-redo", draft});
	const QString next = root.filePath(QStringLiteral("next.vibepackage"));
	const auto drySaved = cli({"draft-save", draft, next, "--delete-folder", "moved", "--dry-run"});
	ok &= expect(drySaved.value("dryRun").toBool() && !drySaved.value("saved").toBool() && !QFileInfo::exists(next), "draft-save shares no-write preflight");
	cli({"draft-save", draft, next, "--delete-folder", "moved"});
	ok &= expect(PackageDraft::load(next, &plan, &error) && plan.summary().stagedFileCount == 0
		&& plan.undo() && plan.summary().stagedFileCount == 1, "folder delete remains reversible after reopening");
	const QString zip = root.filePath(QStringLiteral("result.pk3"));
	cli({"save-as", draft, zip});
	PackageArchive archive; bool emptyFolder = false;
	if (archive.load(zip, &error)) { for (const auto& entry : archive.entries()) { emptyFolder |= normalizePackageVirtualPath(entry.virtualPath, false).normalizedPath == QStringLiteral("moved/empty") && entry.kind == PackageEntryKind::Directory; } }
	ok &= expect(emptyFolder, "archive export retains empty folders");
	const QString pak = root.filePath(QStringLiteral("result.pak"));
	cli({"save-as", draft, pak}, 4);
	ok &= expect(!QFileInfo::exists(pak), "PAK export refuses to discard empty folders");
	cli({"save-as", draft, pak, "--delete-folder", "moved/empty"});
	ok &= expect(read(input) == "original", "document operations never modify input files");
	for (const QString& magic : {QStringLiteral("PWAD"), QStringLiteral("IWAD"), QStringLiteral("WAD2"), QStringLiteral("WAD3")}) {
		const QString output = root.filePath(magic + QStringLiteral(".wad"));
		cli({"create", output, "--wad-magic", magic});
		ok &= expect(read(output).left(4) == magic.toLatin1(), "CLI preserves selected WAD header");
	}
	const QString rejected = root.filePath(QStringLiteral("rejected.pk3"));
	for (const QStringList& invalid : QVector<QStringList>{
		{"--unknown"}, {"--format="}, {"--format", "folder"}, {"--format", "pak", "--format", "zip"}, {"--wad-magic", "WAD3"},
		{"--mkdir="}, {"--mkdir", "--dry-run"}, {"--mkdir", "../escape"}, {"--mkdir", "a", "--mkdir", "a"},
		{"--rename-folder", "a"}, {"--folder-to", "b"}, {"--delete-folder", "missing"}, {"--entry", "unused"},
		{"--add-file", input, "--as", "a/input.txt", "--rename-folder", "a", "--folder-to", "a/deep"},
		{"--mkdir", "a", "--mkdir", "b", "--rename-folder", "a", "--folder-to", "b"}}) {
		cli(QStringList{"create", rejected} + invalid, 2);
		ok &= expect(!QFileInfo::exists(rejected), "invalid document commands produce no archive");
	}
	cli({"create", root.filePath("bad.wad"), "--mkdir", "flat"}, 2);
	cli({"create", root.filePath("bad.wad"), "--wad-magic", "INVALID"}, 2);
	cli({"compare", zip, zip, "--mkdir", "ignored"}, 2);
	cli({"draft-save", draft, next, "--overwrite", "--delete-folder="}, 2);
	const QString unchanged = root.filePath(QStringLiteral("unchanged.pk3"));
	cli({"save-as", zip, unchanged, "--rename-folder="}, 2);
	ok &= expect(!QFileInfo::exists(unchanged), "missing folder selector never becomes an unchanged copy");
	// Diagnostic and dry-run staging must not need writable temporary storage.
	// The corresponding real import proves the invalid temp path is effective.
	const QString verifiedDraft = root.filePath(QStringLiteral("verified-only.vibepackage"));
	cli({"create", verifiedDraft, "--add-file", input, "--as", "input.txt", "--dry-run"}, 0, true);
	ok &= expect(!QFileInfo::exists(verifiedDraft), "dry-run import creates no working or output copy");
	cli({"create", verifiedDraft, "--add-file", input, "--as", "input.txt"}, 2, true);
	ok &= expect(!QFileInfo::exists(verifiedDraft), "failed retention cannot create a draft");
	const QStringList stage{"stage", root.filePath(QStringLiteral("empty.pak")), "--add-file", input, "--as", "input.txt"};
	const auto checkedStage = cli(stage, 0, true).value("staging").toObject();
	ok &= expect(checkedStage.value("operations").toArray().first().toObject().value("contentStorage") == QStringLiteral("verified-file"),
		"read-only CLI stages verified references without temporary copies");
	cli({"manifest", root.filePath(QStringLiteral("empty.pak")), "--add-file", input, "--as", "input.txt", "--output", root.filePath(QStringLiteral("verified-manifest.json"))}, 0, true);
	const auto retained = QJsonDocument::fromJson(read(root.filePath(QStringLiteral("verified-manifest.json")))).object()
		.value("summary").toObject().value("retainedContent").toObject();
	ok &= expect(retained.value("generatedBytes").toString() == "0" && retained.value("fingerprintBytes").toString() == "64"
		&& retained.value("maximumGeneratedBytes").toString() == QString::number(PackageStagingContentLimits::generatedCeiling)
		&& retained.value("maximumFingerprintBytes").toString() == QString::number(PackageStagingContentLimits::fingerprintCeiling),
		"CLI manifest reports retained base and import hashes with the shared document policy.");
	const auto metadata = QJsonDocument::fromJson(read(root.filePath(QStringLiteral("verified-manifest.json")))).object()
		.value("summary").toObject().value("retainedMetadata").toObject();
	ok &= expect(metadata.value("records").toString().toLongLong() > 0
		&& metadata.value("metadataBytes").toString().toLongLong() > 0
		&& metadata.value("maximumRecords").toString() == QString::number(PackageStagingMetadataLimits::recordCeiling)
		&& metadata.value("maximumMetadataBytes").toString() == QString::number(PackageStagingMetadataLimits::metadataCeiling),
		"CLI manifest reports logical retained records and text bytes with the shared document policy.");
	cli({"save-as", root.filePath(QStringLiteral("empty.pak")), root.filePath(QStringLiteral("verified.pak")), "--add-file", input, "--as", "input.txt", "--dry-run"}, 0, true);
	cli({"compare", root.filePath(QStringLiteral("empty.pak")), "--staged", "--add-file", input, "--as", "input.txt"}, 4, true);
	const QString inPlace = root.filePath(QStringLiteral("report-inplace.pak"));
	cli({"create", inPlace, "--add-file", input, "--as", "input.txt"});
	const auto written = cli({"save-as", inPlace, inPlace, "--in-place", "--rename", "input.txt", "--to", "renamed.txt", "--write-manifest"});
	const auto beforeEntries = written.value("staging").toObject().value("beforeEntries").toArray();
	const auto afterEntries = written.value("staging").toObject().value("afterEntries").toArray();
	const auto hash = QString::fromLatin1(QCryptographicHash::hash(read(input), QCryptographicHash::Sha256).toHex());
	ok &= expect(beforeEntries.size() == 1 && afterEntries.size() == 1
		&& beforeEntries.first().toObject().value("virtualPath") == QStringLiteral("input.txt")
		&& afterEntries.first().toObject().value("virtualPath") == QStringLiteral("renamed.txt")
		&& beforeEntries.first().toObject().value("sha256") == hash && afterEntries.first().toObject().value("sha256") == hash,
		"in-place CLI reports must retain before/after hashes from the original snapshot");
	ok &= expect(QJsonDocument::fromJson(read(inPlace + QStringLiteral(".manifest.json"))).object() == written.value("staging").toObject(),
		"published manifest and CLI report must describe the same pre-publication plan");
	const QString deepDraft = root.filePath(QStringLiteral("snapshot-depth.vibepackage"));
	ok &= expect(tests::saveLegacyDeepPackageDraft(deepDraft, &error), "prepare legacy history whose inspection view exceeds the depth cap");
	const auto deepManifest = read(QDir(deepDraft).filePath(QStringLiteral("document.json")));
	for (const QString& command : {QStringLiteral("info"), QStringLiteral("list"), QStringLiteral("validate")}) {
		const auto refused = cli({command, deepDraft}, 1);
		ok &= expect(refused.value("message").toString().contains(QStringLiteral("depth"))
			&& !refused.contains("entries") && read(QDir(deepDraft).filePath(QStringLiteral("document.json"))) == deepManifest,
			"CLI snapshot refusal explains the limit and preserves the draft without a partial listing");
	}
	cli({"draft-undo", deepDraft});
	cli({"list", deepDraft});
	PackageStagingModel recovered;
	ok &= expect(PackageDraft::load(deepDraft, &recovered, &error) && recovered.operations().isEmpty() && recovered.canRedo(),
		"CLI undo can recover a view-limited document without dropping its redo history");
	QByteArray oversized("PWAD");
	const auto append32 = [&](quint32 value) { for (int shift = 0; shift < 32; shift += 8) { oversized.append(static_cast<char>(value >> shift)); } };
	append32(PackageIndexLimits::entryCeiling + 1); append32(12);
	oversized += QByteArray((PackageIndexLimits::entryCeiling + 1) * 16, '\0');
	const QString tooMany = root.filePath(QStringLiteral("index-limit.wad"));
	{ QFile file(tooMany); ok &= expect(file.open(QIODevice::WriteOnly) && file.write(oversized) == oversized.size(), "create CLI index admission fixture"); }
	for (const QString& command : {QStringLiteral("info"), QStringLiteral("list"), QStringLiteral("validate")}) {
		const auto rejectedIndex = cli({command, tooMany}, 1);
		ok &= expect(rejectedIndex.value("message").toString().contains(QStringLiteral("250000"))
			&& !rejectedIndex.contains("package") && !rejectedIndex.contains("entries") && read(tooMany) == oversized,
			"CLI index-limit errors identify the ceiling without a partial listing or input modification");
	}
	const QString mapPath = root.filePath(QStringLiteral("empty-map.map"));
	{ QFile file(mapPath); const QByteArray bytes("{\n\"classname\" \"worldspawn\"\n}\n");
		ok &= expect(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "create empty map for source-admission CLI checks"); }
	QStringList assetRoots;
	for (int index = 0; index <= PackageArchiveSession::layerCeiling; ++index) {
		const QString folder = root.filePath(QStringLiteral("asset-root-%1").arg(index));
		ok &= expect(QDir().mkpath(folder), "create independent empty asset root");
		assetRoots.append(folder);
	}
	for (int count : {64, 65}) {
		QStringList arguments {"--cli", "--settings-file", root.filePath("settings.ini"), "map", "textures", mapPath, "--no-decode", "--json"};
		for (int index = 0; index < count; ++index) { arguments << "--root" << assetRoots.at(index); }
		QProcess process; process.setWorkingDirectory(root.path()); process.start(QString::fromLocal8Bit(argv[1]), arguments);
		const bool ended = process.waitForFinished(15000);
		const QByteArray output = process.readAllStandardOutput();
		const auto report = QJsonDocument::fromJson(output).object().value("textures").toObject();
		const bool complete = count == 64;
		if (!ended || process.exitCode() != (complete ? 0 : 4)) { std::cerr << output.toStdString() << process.readAllStandardError().toStdString(); }
		ok &= expect(ended && process.exitStatus() == QProcess::NormalExit && process.exitCode() == (complete ? 0 : 4)
			&& !report.isEmpty() && report.value("sourceIndexComplete").toBool() == complete,
			"map texture CLI reports aggregate source admission even when no textures are referenced");
	}

	for (const QString& counter : {QStringLiteral("revisionSerial"), QStringLiteral("operationSerial")}) {
		PackageStagingModel serialPlan;
		const QString source = root.filePath(counter + QStringLiteral(".vibepackage"));
		const QString target = root.filePath(counter + QStringLiteral("-copy.vibepackage"));
		ok &= serialPlan.createEmpty(PackageArchiveFormat::Zip) && serialPlan.addBytes("retained", QStringLiteral("one.txt"))
			&& PackageDraft::save(source, &serialPlan, false, &error);
		const QString manifestPath = QDir(source).filePath(QStringLiteral("document.json"));
		auto document = QJsonDocument::fromJson(read(manifestPath)).object();
		document.insert(counter, QString::number(std::numeric_limits<quint64>::max() - 1));
		const QByteArray before = QJsonDocument(document).toJson();
		{ QFile manifestFile(manifestPath); ok &= manifestFile.open(QIODevice::WriteOnly) && manifestFile.write(before) == before.size(); }
		const auto refused = cli({"draft-save", source, target, "--add-file", input, "--as", "new.txt"}, 2, true);
		ok &= expect(QJsonDocument(refused).toJson().contains("counter limit") && read(manifestPath) == before
			&& !QFileInfo::exists(target), "counter refusal has a clear CLI error and preserves the source without output or temporary imports");
		cli({"draft-save", source, target}, 0, true);
		PackageStagingModel saved;
		ok &= expect(PackageDraft::load(target, &saved, &error) && saved.canUndo(), "draft-save without edits remains available at the serial ceiling");
		cli({"draft-undo", target}); cli({"draft-redo", target});
		const QString output = root.filePath(counter + QStringLiteral(".zip"));
		cli({"save-as", target, output}, 0, true);
		PackageArchive written;
		ok &= expect(written.load(output, &error) && written.entries().size() == 1, "CLI export remains available at exhausted history counters");
	}
	for (const bool localHeaderDamage : {false, true}) {
		const QString prefix = localHeaderDamage ? QStringLiteral("bad-local-header-") : QStringLiteral("bad-payload-");
		const auto damaged = root.filePath(prefix + QStringLiteral("damaged.zip"));
		const auto repairedDraft = root.filePath(prefix + QStringLiteral("repaired.vibepackage"));
		ok &= tests::summaryZip(damaged, {{"bad.bin", localHeaderDamage ? 1u : 2u}, {"keep.bin", 1}});
		if (localHeaderDamage) {
			QFile file(damaged);
			ok &= file.open(QIODevice::ReadWrite) && file.seek(14) && file.write(QByteArray(4, '\0')) == 4;
			file.close();
			PackageArchive source;
			ok &= expect(source.load(damaged, &error) && !source.entries().first().readable,
				"CLI fixture must enter repair with unavailable local-header metadata");
		}
		cli({"draft-save", damaged, repairedDraft, "--delete", "bad.bin", "--dry-run"});
		ok &= expect(!QFileInfo::exists(repairedDraft), "repaired draft dry run writes no directory");
		cli({"draft-save", damaged, repairedDraft, "--delete", "bad.bin"});
		cli({"draft-info", repairedDraft});
		cli({"draft-undo", repairedDraft});
		const auto blockedArchive = root.filePath(prefix + QStringLiteral("unavailable.zip"));
		cli({"save-as", repairedDraft, blockedArchive}, 4);
		ok &= expect(!QFileInfo::exists(blockedArchive), "CLI refuses publication when Undo exposes unavailable content");
		cli({"draft-redo", repairedDraft});
		const auto repairedArchive = root.filePath(prefix + QStringLiteral("repaired.zip"));
		cli({"save-as", repairedDraft, repairedArchive});
		PackageArchive repaired; QByteArray repairedBytes;
		ok &= expect(repaired.load(repairedArchive, &error) && repaired.readEntryBytes(QStringLiteral("keep.bin"), &repairedBytes, &error)
			&& repairedBytes == "x", "CLI repaired draft lifecycle retains real payloads");
	}
	return ok ? 0 : 1;
}
