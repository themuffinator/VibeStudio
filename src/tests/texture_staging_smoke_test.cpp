#include "core/texture_export.h"
#include "core/package_draft.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>

#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; } return value; }
bool put(const QString& path, const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
void append32(QByteArray& bytes, quint32 value) { const auto offset = bytes.size(); bytes.resize(offset + 4); qToLittleEndian(value, bytes.data() + offset); }
using Lump = QPair<QString, QByteArray>;
QByteArray wad(const QByteArray& magic, const QVector<Lump>& lumps, const QVector<int>& types = {})
{
	const bool texture = magic.startsWith("WAD"); QByteArray bytes = magic; append32(bytes, quint32(lumps.size())); append32(bytes, 0);
	QByteArray directory;
	for (int i = 0; i < lumps.size(); ++i) {
		const auto& lump = lumps[i]; append32(directory, quint32(bytes.size())); append32(directory, quint32(lump.second.size()));
		if (texture) { append32(directory, quint32(lump.second.size())); directory.append(char(types.value(i, 0x44))); directory.append(QByteArray(3, '\0')); }
		directory.append(lump.first.toLatin1().leftJustified(texture ? 16 : 8, '\0')); bytes += lump.second;
	}
	qToLittleEndian(quint32(bytes.size()), bytes.data() + 8); bytes += directory; return bytes;
}
QStringList names(const PackageArchiveReader& archive) { QStringList result; for (const auto& entry : archive.entries()) { result << entry.virtualPath; } return result; }
QString hint(const PackageArchiveReader& archive, const QString& name) { for (const auto& entry : archive.entries()) { if (entry.virtualPath == name) { return entry.typeHint; } } return {}; }
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temporary; if (!temporary.isValid()) { return EXIT_FAILURE; }
	QDir root(temporary.path()); bool ok = true; QString error;
	QByteArray paletteBytes; IdTechPaletteResolution palette; palette.palette.id = QStringLiteral("doom");
	for (int i = 0; i < 256; ++i) { palette.palette.colors << qRgb(i, i, i); paletteBytes.append(QByteArray(3, char(i))); }
	QImage pixels(64, 64, QImage::Format_Indexed8); pixels.setColorTable(palette.palette.colors); pixels.fill(72);
	TextureExportOptions options; options.format = TextureExportFormat::DoomFlat;
	const auto flat = encodeTextureExport(pixels, options, palette);
	QVector<Lump> lumps{{"PLAYPAL", paletteBytes}, {"F_START", {}}, {"F1_START", {}}, {"OLD", QByteArray(4096, char(20))}, {"F1_END", {}}, {"F_END", {}}};
	for (const QString& marker : {QStringLiteral("MAP01"), QStringLiteral("MAP02")}) {
		lumps.append({marker, {}});
		for (const QString& name : {QStringLiteral("THINGS"), QStringLiteral("LINEDEFS"), QStringLiteral("SIDEDEFS"), QStringLiteral("VERTEXES"), QStringLiteral("SEGS"), QStringLiteral("SSECTORS"), QStringLiteral("NODES"), QStringLiteral("SECTORS"), QStringLiteral("REJECT"), QStringLiteral("BLOCKMAP")}) { lumps.append({name, marker.toLatin1() + name.toLatin1()}); }
	}
	const auto source = root.filePath("source.wad"); const auto sourceBytes = wad("PWAD", lumps); put(source, sourceBytes);
	PackageArchive archive; PackageStagingModel staging;
	ok &= expect(archive.load(source, &error) && staging.loadBaseArchive(archive, &error), "load multi-map WAD and nested flat namespaces");
	ok &= expect(stageTextureExport(flat, options, "NEWFLAT", &staging, false, &error), "new flat stages into existing outer flat namespace");
	PackageStagingArchive first(staging); auto firstNames = names(first);
	ok &= expect(firstNames.indexOf("NEWFLAT") == firstNames.indexOf("F_END") - 1 && hint(first, "NEWFLAT") == "wad-flat", "planned order and type classify new native flat before save");
	ok &= expect(decodeIdTechImageFromArchive(first, "NEWFLAT", "doom").image == pixels, "staged flat decodes with staged PLAYPAL");
	const auto count = staging.operations().size(); const auto revision = staging.revision();
	ok &= expect(!stageTextureExport(flat, options, "NEWFLAT", &staging, false, &error) && staging.operations().size() == count && staging.revision() == revision, "unapproved replacement leaves plan and undo untouched");
	ok &= expect(!stageTextureExport(flat, options, "THINGS", &staging, true, &error) && !stageTextureExport(flat, options, "PLAYPAL", &staging, true, &error)
		&& !stageTextureExport(flat, options, "../X", &staging, true, &error), "native texture cannot replace map, palette, or unsafe lump targets");
	ok &= expect(stageTextureExport(flat, options, "OLD", &staging, true, &error) && hint(PackageStagingArchive(staging), "OLD") == "wad-flat", "replacing base flat retains its nested namespace and preview classification");
	options.format = TextureExportFormat::DoomPatch; options.leftOffset = -5; options.topOffset = 12; const auto patch = encodeTextureExport(pixels, options, palette);
	ok &= expect(stageTextureExport(patch, options, "WALLA", &staging, false, &error), "new patch creates an undoable patch namespace");
	PackageStagingArchive patched(staging); const auto patchNames = names(patched);
	ok &= expect(patchNames.last() == "P_END" && patchNames.at(patchNames.size() - 2) == "WALLA" && patchNames.at(patchNames.size() - 3) == "P_START"
		&& hint(patched, "WALLA") == "wad-patch", "patch namespace is visible in the package plan");
	ok &= expect(staging.undo() && !names(PackageStagingArchive(staging)).contains("P_START") && staging.redo(), "texture and both generated markers undo and redo together");
	const auto draft = root.filePath("native.vibepackage");
	ok &= expect(PackageDraft::save(draft, &staging, false, &error), "native placements save to a portable package draft");
	PackageStagingModel reopened;
	ok &= expect(PackageDraft::load(draft, &reopened, &error) && names(PackageStagingArchive(reopened)) == patchNames
		&& reopened.undo() && !names(PackageStagingArchive(reopened)).contains("P_START") && reopened.redo(), "draft round trip retains placement and grouped undo history");
	const auto manifestPath = QDir(draft).filePath("document.json"); const auto manifestBytes = read(manifestPath);
	for (const auto& bad : QVector<QPair<QString, QJsonValue>>{{"wadType", 256}, {"wadInsertBefore", "F_START"}, {"wadNamespace", "invalid"}}) {
		auto damaged = QJsonDocument::fromJson(manifestBytes).object(); auto operations = damaged.value("operations").toArray();
		auto operation = operations.first().toObject(); operation.insert(bad.first, bad.second); operations[0] = operation; damaged.insert("operations", operations);
		put(manifestPath, QJsonDocument(damaged).toJson()); const auto originalRevision = reopened.revision();
		ok &= expect(!PackageDraft::load(draft, &reopened, &error) && reopened.revision() == originalRevision && names(PackageStagingArchive(reopened)) == patchNames, "malformed native placement metadata cannot replace the open package plan");
	}
	put(manifestPath, manifestBytes);
	PackageWriteRequest write; write.format = PackageArchiveFormat::Wad; write.destinationPath = root.filePath("output.wad"); write.verifyDeterminism = true;
	const auto report = reopened.writeArchive(write); PackageArchive saved;
	ok &= expect(report.succeeded() && report.determinismVerified && saved.load(write.destinationPath, &error), "published WAD preserves native namespaces and all map runs deterministically");
	if (!report.succeeded()) { std::cerr << report.blockedMessages.join('\n').toStdString() << error.toStdString() << '\n'; }
	PackageStagingModel writtenPlan; writtenPlan.loadBaseArchive(saved);
	ok &= expect(names(PackageStagingArchive(writtenPlan)) == patchNames, "written directory order matches the planned native namespace placement");
	QSet<QByteArray> thingPayloads;
	for (qsizetype i = 0; i < saved.entries().size(); ++i) {
		const auto entry = saved.entries()[i]; if (entry.virtualPath != "THINGS") { continue; }
		QByteArray payload; saved.readEntryAt(i, &payload, &error);
		thingPayloads.insert(payload);
	}
	ok &= expect(thingPayloads == QSet<QByteArray>{"MAP01THINGS", "MAP02THINGS"}, "duplicate map lumps retain their distinct bytes");
	ok &= expect(decodeIdTechImageFromArchive(saved, "NEWFLAT", "doom").image == pixels && decodeIdTechImageFromArchive(saved, "WALLA", "doom").leftOffset == -5 && read(source) == sourceBytes, "native published outputs decode and original source remains untouched");
	auto broken = reopened; broken.deleteEntry("F_START"); broken.deleteEntry("F1_START");
	ok &= expect(!broken.summary().canSave, "deleting namespace starts blocks anchored texture publication");
	broken = reopened; broken.renameEntry("F_END", "END"); ok &= expect(!broken.summary().canSave, "renaming an insertion anchor blocks publication");
	broken = reopened; broken.replaceFile("NEWFLAT", root.filePath("missing")); ok &= expect(!broken.summary().canSave, "missing replacement remains an actionable package conflict");
	const auto emptyPath = root.filePath("empty.wad"); put(emptyPath, wad("PWAD", {})); PackageArchive empty; empty.load(emptyPath); PackageStagingModel emptyPlan; emptyPlan.loadBaseArchive(empty);
	const auto globalPath = root.filePath("global.wad"); put(globalPath, wad("PWAD", {{"TITLEPIC", patch.bytes}, {"PLAYPAL", paletteBytes}})); PackageArchive global; global.load(globalPath); PackageStagingModel globalPlan; globalPlan.loadBaseArchive(global);
	options.format = TextureExportFormat::DoomPatch;
	ok &= expect(stageTextureExport(patch, options, "TITLEPIC", &globalPlan, true, &error) && names(PackageStagingArchive(globalPlan)) == QStringList{"TITLEPIC", "PLAYPAL"}
		&& !stageTextureExport(patch, options, "PLAYPAL", &globalPlan, true, &error), "existing global Doom graphics retain their position while palette lumps stay protected");
	options.format = TextureExportFormat::DoomFlat;
	ok &= expect(stageTextureExport(flat, options, "FLAT1", &emptyPlan, false, &error) && names(PackageStagingArchive(emptyPlan)) == QStringList{"F_START", "FLAT1", "F_END"}, "empty WAD gains a complete flat namespace");
	ok &= expect(stageTextureExport(flat, options, "FLAT2", &emptyPlan, false, &error) && stageTextureExport(flat, options, "FLAT1", &emptyPlan, true, &error)
		&& names(PackageStagingArchive(emptyPlan)) == QStringList{"F_START", "FLAT1", "FLAT2", "F_END"}, "successive additions and replacements stay inside generated markers");
	const auto malformedPath = root.filePath("malformed.wad"); put(malformedPath, wad("PWAD", {{"F_END", {}}})); PackageArchive malformed; malformed.load(malformedPath); PackageStagingModel malformedPlan; malformedPlan.loadBaseArchive(malformed);
	ok &= expect(!stageTextureExport(flat, options, "FLAT1", &malformedPlan, false, &error) && malformedPlan.operations().isEmpty(), "unbalanced namespaces refuse native staging atomically");
	const auto duplicatePath = root.filePath("duplicate.wad"); put(duplicatePath, wad("PWAD", {{"F_START", {}}, {"SAME", flat.bytes}, {"SAME", flat.bytes}, {"F_END", {}}})); PackageArchive duplicate; duplicate.load(duplicatePath); PackageStagingModel duplicatePlan; duplicatePlan.loadBaseArchive(duplicate);
	ok &= expect(!stageTextureExport(flat, options, "SAME", &duplicatePlan, true, &error), "repeated native names cannot overwrite an arbitrary lump");
	options.format = TextureExportFormat::QuakeMiptex; options.name = "stone"; const auto mip = encodeTextureExport(pixels, options, palette);
	const auto quakePath = root.filePath("quake.wad"); put(quakePath, wad("WAD2", {{"stone", mip.bytes}, {"raw", "raw bytes"}}, {0x42, 0x42})); PackageArchive quake; quake.load(quakePath); PackageStagingModel quakePlan; quakePlan.loadBaseArchive(quake);
	ok &= expect(stageTextureExport(mip, options, "stone", &quakePlan, true, &error) && quakePlan.plannedEntries()[0].wadLumpType == 0x44 && quakePlan.plannedEntries()[1].wadLumpType == 0x42, "native mip replacement sets type 0x44 without changing unrelated lump types");
	ok &= expect(!stageTextureExport(mip, options, "different", &quakePlan, false, &error), "Quake directory and embedded texture names must agree");
	const auto quakeDraft = root.filePath("quake.vibepackage"); PackageStagingModel quakeReloaded;
	ok &= expect(PackageDraft::save(quakeDraft, &quakePlan, false, &error) && PackageDraft::load(quakeDraft, &quakeReloaded, &error) && quakeReloaded.plannedEntries()[0].wadLumpType == 0x44, "explicit WAD2 type survives durable draft persistence");
	write.destinationPath = root.filePath("quake-out.wad"); const auto quakeReport = quakePlan.writeArchive(write); const auto quakeBytes = read(write.destinationPath);
	const auto directoryOffset = quakeBytes.size() >= 12 ? qFromLittleEndian<quint32>(quakeBytes.constData() + 8) : 0;
	ok &= expect(quakeReport.succeeded() && directoryOffset + 64 <= quint32(quakeBytes.size()) && quint8(quakeBytes[int(directoryOffset) + 12]) == 0x44 && quint8(quakeBytes[int(directoryOffset) + 44]) == 0x42, "independent WAD directory bytes retain explicit native lump types");
	QByteArray changedPalette = paletteBytes; changedPalette[72 * 3] = char(240); auto changed = staging; changed.addBytes(changedPalette, "PLAYPAL", &error, PackageStageConflictResolution::ReplaceExisting);
	ok &= expect(decodeIdTechImageFromArchive(PackageStagingArchive(changed), "NEWFLAT", "doom").image.pixelColor(0, 0).red() == 240, "staged palette replacement immediately changes native decoding");
	if (argc > 1) {
		const auto input = root.filePath("flat.png"); pixels.save(input); const auto cliDraft = root.filePath("cli.vibepackage");
		const auto cli = [&](QStringList args, bool success, QJsonObject* out = nullptr) {
			QProcess process; args.prepend("--cli"); args.append("--json"); process.start(QString::fromLocal8Bit(argv[1]), args);
			if (!process.waitForFinished(20000)) { process.kill(); process.waitForFinished(); return false; }
			const auto object = QJsonDocument::fromJson(process.readAllStandardOutput()).object(); if (out) { *out = object; }
			if ((process.exitCode() == 0) != success || object.isEmpty()) { std::cerr << QJsonDocument(object).toJson().constData() << process.readAllStandardError().constData(); return false; } return true;
		};
		QStringList args{"texture", "stage", input, "--profile", "doom-flat", "--target-package", source, "--target-entry", "CLIFLAT", "--output", cliDraft}; QJsonObject result;
		ok &= expect(cli(args + QStringList{"--dry-run"}, true, &result) && !QFileInfo::exists(cliDraft) && !result.value("written").toBool() && !result.value("packageWritten").toBool(), "CLI dry run validates native staging without creating a draft or archive");
		ok &= expect(cli(args, true, &result) && result.value("written").toBool() && !result.value("packageWritten").toBool(), "CLI writes only the reviewable package draft");
		PackageStagingModel cliPlan;
		ok &= expect(PackageDraft::load(cliDraft, &cliPlan, &error) && hint(PackageStagingArchive(cliPlan), "CLIFLAT") == "wad-flat", "CLI output opens through the same draft and namespace services");
		ok &= expect(cli(args, false), "CLI refuses implicit draft overwrite");
		args[args.indexOf("--target-package") + 1] = cliDraft;
		ok &= expect(cli(args + QStringList{"--overwrite"}, false) && cli(args + QStringList{"--overwrite", "--replace-texture"}, true), "entry replacement and draft overwrite have independent explicit controls");
		ok &= expect(cli({"texture", "validate", "--package", source, "--entry", "OLD", "--profile", "doom-flat"}, true), "CLI source import retains a flat namespace's format hint");
		ok &= expect(read(source) == sourceBytes, "CLI staging never overwrites the source WAD");
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
