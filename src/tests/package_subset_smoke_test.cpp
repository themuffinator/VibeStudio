#include "core/package_selection.h"
#include "core/package_staging.h"
#include "core/package_draft.h"
#include "core/studio_settings.h"
#include <algorithm>
#include "tests/package_subset_test_helpers.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QTemporaryDir>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::subset_test;
namespace {
bool expect(bool value, const char* message, const QString& error = {})
{
	if (!value) { std::cerr << message << ": " << error.toStdString() << '\n'; } return value;
}
QStringList paths(const PackageStagingModel& plan)
{
	QStringList result; for (const auto& entry : plan.plannedEntries()) { result << entry.virtualPath; } return result;
}
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temporary; if (!temporary.isValid()) { return 1; }
	const QDir root(temporary.path()); bool ok = true; QString error;
	StudioSettings::setOverrideFilePath(root.filePath("settings.ini"));
	const QString source = root.filePath("maps.wad"); const auto lumps = fixture(); PackageArchive archive;
	ok &= wad(source, lumps) && archive.load(source, &error); if (!expect(ok, "load WAD subset fixture", error)) { return 1; }
	const auto secondThings = index(archive, "THINGS", 1);
	PackageSelectionRequest request; request.entryIndexes = {secondThings};
	const auto selection = selectPackageFiles(archive, request);
	ok &= expect(selection.succeeded() && selection.entryIndexes == QVector<qsizetype>{secondThings}, "exact selection preserves second duplicate");
	ok &= expect(!selectPackageFiles(archive, {{QStringLiteral("THINGS")}, {}, {}, {}}).succeeded(), "path-only duplicate selection is refused");
	request.entryIndexes = {-1}; ok &= expect(!selectPackageFiles(archive, request).succeeded(), "invalid index is refused");
	request.entryIndexes = {archive.entries().size()}; ok &= expect(!selectPackageFiles(archive, request).succeeded(), "out-of-range index is refused");
	request.entryIndexes.clear(); request.query = "name=THINGS";
	ok &= expect(selectPackageFiles(archive, request).entryIndexes.size() == 2, "query returns both occurrences without collapsing names");
	PackageStagingModel subset; PackageSubsetReview review;
	ok &= expect(subset.loadBaseArchiveSubsetAt(archive, {secondThings}, &error, &review), "map member expands into its own map group", error);
	ok &= expect(paths(subset).size() == 13 && paths(subset).first() == "MAP02" && paths(subset).last() == "SCRIPTS", "second map includes optional Hexen sidecars in order");
	int selected = 0; for (const auto& member : review.members) { selected += member.selected; }
	ok &= expect(selected == 1 && review.members.size() == 13 && packageSubsetReviewJson(review).value("entries").toArray().size() == 13, "review distinguishes explicit and required members");
	PackageWriteRequest write; write.destinationPath = root.filePath("subset.wad");
	auto result = subset.writeArchive(write); PackageArchive reopened;
	ok &= expect(result.succeeded() && reopened.load(write.destinationPath, &error), "subset writes and reopens", result.blockedMessages.join(';'));
	QByteArray payload; ok &= expect(reopened.readEntryBytes("THINGS", &payload, &error) && payload == "THINGS-second", "subset reads the selected map occurrence");
	ok &= expect(get(write.destinationPath).first(4) == "PWAD", "Doom subset retains its container family");
	PackageStagingModel staged; ok &= staged.loadBaseArchive(archive, &error);
	const auto replacement = root.filePath("replacement.bin"); ok &= put(replacement, "changed-things");
	ok &= expect(staged.replaceOccurrence(12, replacement, &error), "stage exact source occurrence", error);
	const QString draft = root.filePath("work.vibepackage"); ok &= PackageDraft::save(draft, &staged, false, &error);
	PackageStagingModel loaded; ok &= PackageDraft::load(draft, &loaded, &error); const auto planned = packagePlannedArchive(loaded);
	ok &= expect(subset.loadBaseArchiveSubsetAt(planned, {index(planned, "THINGS", 1)}, &error, &review), "portable planned snapshot supports exact subset", error);
	write.destinationPath = root.filePath("edited.wad"); result = subset.writeArchive(write);
	ok &= expect(result.succeeded() && reopened.load(write.destinationPath, &error) && reopened.readEntryBytes("THINGS", &payload, &error)
		&& payload == "changed-things" && loaded.canUndo(), "subset uses staged bytes without consuming editing history");
	ok &= loaded.addBytes("newly generated", "FRESH", &error); const auto generatedDraftView = packagePlannedArchive(loaded);
	ok &= subset.loadBaseArchiveSubsetAt(generatedDraftView, {index(generatedDraftView, "FRESH")}, &error);
	write.destinationPath = QDir(draft).filePath("new-export.wad"); write.dryRun = true;
	ok &= expect(!subset.writeArchive(write).succeeded(), "a generated-only subset retains protection for its entire source draft directory");
	write.destinationPath = QDir(draft).filePath("document.json"); write.format = PackageArchiveFormat::Wad; write.allowOverwrite = true;
	ok &= expect(!subset.writeArchive(write).succeeded(), "a generated-only subset cannot overwrite source draft metadata");
	write = {};
	ok &= expect(subset.loadBaseArchiveSubsetAt(archive, {index(archive, "CUSTOM")}, &error, &review), "UDMF sidecar selects its entire delimited map", error);
	ok &= expect(paths(subset) == QStringList{"TITLEMAP", "TEXTMAP", "CUSTOM", "ZNODES", "ENDMAP"}, "UDMF retains arbitrary sidecars and end marker");
	write.destinationPath = root.filePath("udmf.wad"); result = subset.writeArchive(write);
	ok &= expect(result.succeeded() && reopened.load(write.destinationPath, &error), "named UDMF map exports", result.blockedMessages.join(';'));
	QVector<QPair<qint64, QString>> order; for (const auto& entry : reopened.entries()) { order.append({entry.sourceOrdinal, entry.virtualPath}); }
	std::sort(order.begin(), order.end()); QStringList writtenOrder; for (const auto& item : order) { writtenOrder << item.second; }
	ok &= expect(writtenOrder == paths(subset), "WAD writer never path-sorts named-map records");
	ok &= expect(subset.loadBaseArchiveSubsetAt(archive, {index(archive, "FLOORB")}, &error, &review)
		&& paths(subset) == QStringList{"F_START", "FLOORB", "F_END"}, "single namespace member keeps only its required boundaries", error);
	ok &= expect(subset.loadBaseArchiveSubsetAt(archive, {index(archive, "F_START")}, &error)
		&& paths(subset) == QStringList{"F_START", "FLOORA", "FLOORB", "F_END"}, "selecting a namespace marker includes its region", error);
	ok &= expect(subset.loadBaseArchiveSubsetAt(archive, {index(archive, "PATCHA")}, &error)
		&& paths(subset) == QStringList{"P_START", "P1_START", "PATCHA", "P1_END", "P_END"}, "nested namespace boundaries stay balanced", error);
	ok &= expect(subset.loadBaseArchiveSubsetAt(archive, {index(archive, "TEXTURE1")}, &error)
		&& paths(subset) == QStringList{"PNAMES", "TEXTURE1", "TEXTURE2"}, "local texture name tables stay together", error);
	const auto sidecarPath = root.filePath("sidecar.wad"); PackageArchive sidecar;
	ok &= wad(sidecarPath, {{"TITLEMAP", {}}, {"TEXTMAP", "map"}, {"PNAMES", "sidecar"}, {"ENDMAP", {}}, {"PNAMES", "global"}, {"TEXTURE1", "table"}})
		&& sidecar.load(sidecarPath, &error);
	ok &= expect(subset.loadBaseArchiveSubsetAt(sidecar, {index(sidecar, "TEXTMAP")}, &error, &review) && review.members.size() == 4,
		"UDMF sidecars do not accidentally claim unrelated global texture tables", error);
	const auto namedPath = root.filePath("named-resources.wad"); PackageArchive namedResources;
	ok &= wad(namedPath, {{"F_START", {}}, {"FLOOR", "floor"}, {"THINGS", "flat"}, {"MAP01", "another flat"}, {"F_END", {}}})
		&& namedResources.load(namedPath, &error);
	for (const QString& name : {QString("THINGS"), QString("MAP01")}) {
		ok &= expect(subset.loadBaseArchiveSubsetAt(namedResources, {index(namedResources, name)}, &error)
			&& paths(subset) == QStringList{"F_START", name, "F_END"}, "namespace context disambiguates textures named like map records", error);
	}
	const auto before = subset.manifestJson();
	PackageReadControl control; control.isCancelled = []() { return true; };
	ok &= expect(!subset.loadBaseArchiveSubsetAt(archive, {secondThings}, &error, nullptr, control) && subset.manifestJson() == before, "cancelled preparation preserves the prior destination model");
	for (const auto& [name, broken] : QVector<QPair<QString, QVector<Lump>>>{
		{"missing-sector", {{"MAP01", {}}, {"THINGS", "one"}, {"LINEDEFS", "two"}}},
		{"missing-endmap", {{"TITLEMAP", {}}, {"TEXTMAP", "one"}, {"CUSTOM", "two"}}},
		{"open-namespace", {{"F_START", {}}, {"FLOOR", "one"}}},
		{"orphan-map-lump", {{"THINGS", "one"}}}}) {
		const auto path = root.filePath(name + ".wad"); PackageArchive invalid;
		ok &= wad(path, broken) && invalid.load(path, &error);
		ok &= expect(!subset.loadBaseArchiveSubsetAt(invalid, {index(invalid, QString::fromLatin1(broken.last().first))}, &error)
			&& subset.manifestJson() == before, "incomplete semantic group is refused atomically", name);
	}
	for (const auto& magic : {QByteArray("WAD2"), QByteArray("WAD3")}) {
		const auto path = root.filePath(QString::fromLatin1(magic) + ".wad"); PackageArchive textures;
		ok &= wad(path, {{"DUP", "first"}, {"DUP", "second"}, {"OTHER", "third"}}, magic) && textures.load(path, &error);
		ok &= expect(subset.loadBaseArchiveSubsetAt(textures, {index(textures, "DUP", 1)}, &error), "texture WAD exact subset does not need Doom map groups", error);
		write.destinationPath = root.filePath(QString::fromLatin1(magic) + "-out.wad"); result = subset.writeArchive(write);
		ok &= expect(result.succeeded() && get(write.destinationPath).first(4) == magic && reopened.load(write.destinationPath, &error)
			&& reopened.readEntryBytes("DUP", &payload, &error) && payload == "second", "texture WAD type and exact payload survive subset export");
	}
	const QVector<Lump> repeated{{"docs/repeated.txt", "first"}, {"docs/repeated.txt", "second"}, {"readme.txt", "third"}};
	for (const QString& extension : {QString("pak"), QString("zip")}) {
		const auto path = root.filePath("duplicates." + extension); PackageArchive duplicates;
		ok &= (extension == "pak" ? pak(path, repeated) : zip(path, repeated)) && duplicates.load(path, &error);
		ok &= expect(subset.loadBaseArchiveSubsetAt(duplicates, {index(duplicates, "docs/repeated.txt", 1)}, &error, &review), "PAK and ZIP preserve exact repeated occurrence", error);
		write.destinationPath = root.filePath("exact." + extension); result = subset.writeArchive(write);
		ok &= expect(result.succeeded() && reopened.load(write.destinationPath, &error) && reopened.readEntryBytes("docs/repeated.txt", &payload, &error)
			&& payload == "second", "exact repeated file writes its own bytes", result.blockedMessages.join(';'));
		PackageSelectionRequest folder; folder.prefixes = {"docs"}; const auto selectedFiles = selectPackageFiles(duplicates, folder);
		ok &= expect(selectedFiles.entryIndexes.size() == 2 && subset.loadBaseArchiveSubsetAt(duplicates, selectedFiles.entryIndexes, &error), "prefix preserves both repeated records", error);
		write.destinationPath = root.filePath("both." + extension); result = subset.writeArchive(write);
		ok &= expect(result.succeeded() && reopened.load(write.destinationPath, &error) && reopened.readEntryAt(index(reopened, "docs/repeated.txt", 1), &payload, &error)
			&& payload == "second", "subset writer never collapses repeated names", result.blockedMessages.join(';'));
	}
	const QVector<Lump> gl{{"GL_MAP01", {}}, {"GL_VERT", "v"}, {"GL_SEGS", "s"}, {"GL_SSECT", "ss"}, {"GL_NODES", "n"}};
	const auto glPath = root.filePath("gl.wad"); PackageArchive glArchive;
	ok &= wad(glPath, map("MAP01", "-one") + gl) && glArchive.load(glPath, &error);
	ok &= expect(subset.loadBaseArchiveSubsetAt(glArchive, {index(glArchive, "GL_SEGS")}, &error, &review) && review.members.size() == 16,
		"GL node selection includes the matching map and entire node group", error);
	const auto ambiguousPath = root.filePath("ambiguous.wad"); PackageArchive ambiguous;
	ok &= wad(ambiguousPath, map("MAP01", "-one") + map("MAP01", "-two") + gl) && ambiguous.load(ambiguousPath, &error);
	ok &= expect(!subset.loadBaseArchiveSubsetAt(ambiguous, {index(ambiguous, "THINGS")}, &error) && error.contains("ambiguous"), "ambiguous main labels cannot claim a shared GL group");
	const auto invalidPath = root.filePath("skipped.wad"); PackageArchive invalid;
	auto malformed = map("MAP01", "-one"); ok &= wad(invalidPath, malformed);
	auto bytes = get(invalidPath); QByteArray offset; u32(offset, 0x7fffffff);
	bytes.replace(bytes.size() - malformed.size() * 16 + 5 * 16, 4, offset); ok &= put(invalidPath, bytes) && invalid.load(invalidPath, &error);
	ok &= expect(!subset.loadBaseArchiveSubsetAt(invalid, {index(invalid, "THINGS")}, &error), "a skipped optional map record cannot silently disappear from a subset");
	PackageStagingModel generated; ok &= generated.createEmpty(PackageArchiveFormat::Pk3, {}, &error);
	ok &= generated.addBytes("generated", "docs/new.txt", &error) && generated.addBytes("excluded", "else.txt", &error);
	const auto generatedView = packagePlannedArchive(generated);
	ok &= expect(subset.loadBaseArchiveSubsetAt(generatedView, {index(generatedView, "docs/new.txt")}, &error, &review)
		&& review.members.size() == 1 && review.members.first().sourceOrdinal == -1 && generated.canUndo(), "source-free entries export without consuming the document", error);
	write.destinationPath = root.filePath("new.pk3"); result = subset.writeArchive(write);
	ok &= expect(result.succeeded() && reopened.load(write.destinationPath, &error) && reopened.readEntryBytes("docs/new.txt", &payload, &error) && payload == "generated", "generated subset payload survives");
	const auto prior = subset.manifestJson(); int checks = 0; control.isCancelled = [&]() { return ++checks > 160; };
	ok &= expect(!subset.loadBaseArchiveSubsetAt(archive, {secondThings}, &error, nullptr, control) && checks > 160 && subset.manifestJson() == prior, "mid-scan cancellation preserves existing subset state");
	PackageStagingModel newWad; ok &= newWad.createEmpty(PackageArchiveFormat::Wad, "PWAD", &error);
	const auto newMap = map("MAP01", "-generated");
	for (auto at = newMap.crbegin(); at != newMap.crend(); ++at) { ok &= newWad.addBytes(at->second, QString::fromLatin1(at->first), &error); }
	write.destinationPath = root.filePath("assembled.wad"); result = newWad.writeArchive(write);
	ok &= expect(result.succeeded() && reopened.load(write.destinationPath, &error) && reopened.entries().at(index(reopened, "MAP01")).sourceOrdinal == 0
		&& reopened.entries().at(index(reopened, "THINGS")).sourceOrdinal == 1, "new source-free WADs retain canonical binary-map assembly", result.blockedMessages.join(';'));
	PackageArchive changed; const auto changedPath = root.filePath("changed.wad");
	ok &= wad(changedPath, fixture()) && changed.load(changedPath, &error) && subset.loadBaseArchiveSubsetAt(changed, {index(changed, "CUSTOM")}, &error);
	bytes = get(changedPath); bytes[12] = static_cast<char>(bytes.at(12) ^ 1); ok &= put(changedPath, bytes);
	write.destinationPath = root.filePath("stale.wad"); result = subset.writeArchive(write);
	ok &= expect(!result.succeeded() && !QFileInfo::exists(write.destinationPath), "changed source is refused at export without publishing a subset");
	return ok ? 0 : 1;
}
