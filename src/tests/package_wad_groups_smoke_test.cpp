#include "core/package_wad_groups.h"
#include "core/package_staging.h"
#include "core/package_draft.h"
#include "core/package_selection.h"
#include "core/studio_settings.h"
#include "tests/package_subset_test_helpers.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QTemporaryDir>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::subset_test;
namespace {
bool expect(bool value, const char* label, const QString& error = {}) { if (!value) { std::cerr << label << ": " << error.toStdString() << '\n'; } return value; }
QStringList names(const PackageStagingModel& model) { QStringList result; for (const auto& entry : model.plannedEntries()) { result << entry.virtualPath; } return result; }
PackageWadGroup named(const PackageWadGroupInventory& inventory, const QString& name)
{
	for (const auto& group : inventory.groups) { if (group.name == name) { return group; } } return {};
}
bool newWadOrderWorkflow(const QDir& root, const QString& magic)
{
	bool ok = true; QString error; PackageStagingModel model;
	ok &= model.createEmpty(PackageArchiveFormat::Wad, magic, &error); auto expected = newWadFixture();
	for (const auto& [name, bytes] : expected) { ok &= model.addBytes(bytes, QString::fromLatin1(name), &error); }
	if (!expect(ok && model.summary().canSave, "create source-free WAD groups", error)) { return false; }
	const auto inventory = inspectPackageWadGroups(model); const auto intro = named(inventory, "INTRO");
	ok &= expect(inventory.succeeded() && inventory.groups.size() == 7 && intro.memberCount == 16 && intro.id.startsWith("map:new:")
		&& model.sourcePath().isEmpty() && model.sourceWadLumps().isEmpty(), "new WAD inspection uses the construction order before any save", inventory.error);
	PackageWadGroupEditRequest edit{intro.id, inventory.fingerprint, "BEGIN", false}; PackageWadGroupEditReview review;
	ok &= expect(stagePackageWadGroupEdit(&model, edit, &review, &error) && review.changes.size() == 2 && model.undo()
		&& names(model).contains("INTRO") && model.redo(), "new named map and GL rename is one undo step", error);
	for (auto& [name, bytes] : expected) { Q_UNUSED(bytes); if (name == "INTRO") { name = "BEGIN"; } else if (name == "GL_INTRO") { name = "GL_BEGIN"; } }
	ok &= model.addWadBytes(QByteArray(4096, 'n'), "FLOORNEW", "flat", 0, false, &error);
	for (qsizetype at = 0; at < expected.size(); ++at) { if (expected.at(at).first == "F_END") { expected.insert(at, Lump{"FLOORNEW", QByteArray(4096, 'n')}); break; } }
	const auto plannedNames = names(model); const auto plannedSnapshot = packagePlannedArchive(model);
	QByteArray payload; ok &= expect(plannedSnapshot.readEntryAt(index(plannedSnapshot, "CUSTOM"), &payload, &error) && payload == "sidecar",
		"planned preview reads source-free UDMF sidecar bytes", error);
	PackageStagingModel subset; PackageSubsetReview selected;
	ok &= expect(subset.loadBaseArchiveSubsetAt(plannedSnapshot, {index(plannedSnapshot, "CUSTOM")}, &error, &selected)
		&& selected.members.size() == 5 && names(subset) == QStringList{"TITLEMAP", "TEXTMAP", "CUSTOM", "ZNODES", "ENDMAP"},
		"new WAD subset expands the same complete UDMF run", error);
	PackageWriteRequest write; write.destinationPath = root.filePath(magic + "-subset.wad");
	const auto expectedSubset = root.filePath(magic + "-expected-subset.wad");
	ok &= wad(expectedSubset, {{"TITLEMAP", {}}, {"TEXTMAP", "namespace=\"zdoom\";"}, {"CUSTOM", "sidecar"}, {"ZNODES", "nodes"}, {"ENDMAP", {}}}, magic.toLatin1());
	ok &= expect(subset.writeArchive(write).succeeded() && get(write.destinationPath) == get(expectedSubset), "subset writer retains source-free named map order");
	const auto draft = root.filePath(magic + "-new.vibepackage"); PackageStagingModel restored;
	ok &= expect(PackageDraft::save(draft, &model, false, &error) && PackageDraft::load(draft, &restored, &error)
		&& names(restored) == plannedNames && restored.sourcePath().isEmpty() && restored.sourceWadLumps().isEmpty(), "portable draft retains construction order and source-free identity", error);
	const auto draftInventory = inspectPackageWadGroups(restored); edit = {named(draftInventory, "F_START").id, draftInventory.fingerprint, {}, true};
	ok &= expect(stagePackageWadGroupEdit(&restored, edit, &review, &error) && review.changes.size() == 4 && restored.undo()
		&& names(restored) == plannedNames && restored.redo() && !names(restored).contains("FLOORNEW") && restored.undo(),
		"source-free draft group deletion retains anchored texture history", error);
	write.destinationPath = root.filePath(magic + "-new.wad"); write.dryRun = true;
	ok &= expect(restored.writeArchive(write).succeeded() && !QFileInfo::exists(write.destinationPath) && names(restored) == plannedNames,
		"new WAD dry run leaves output and reviewed order unchanged"); write.dryRun = false;
	const auto expectedPath = root.filePath(magic + "-expected.wad"); ok &= wad(expectedPath, expected, magic.toLatin1());
	ok &= expect(restored.writeArchive(write).succeeded() && get(write.destinationPath) == get(expectedPath),
		"new WAD save matches independent ordered directory and payload fixture byte for byte");
	PackageArchive reopened; PackageStagingModel roundtrip;
	ok &= expect(reopened.load(write.destinationPath, &error) && roundtrip.loadBaseArchive(reopened, &error) && names(roundtrip) == plannedNames
		&& inspectPackageWadGroups(roundtrip).groups.size() == 7, "save/reopen preserves every reviewed semantic group", error);
	// Rebase all generated entries without an on-disk WAD directory. The writer
	// must still consume this positional plan, not infer a new order from names.
	PackageStagingModel rebased; ok &= rebased.loadBaseArchive(plannedSnapshot, &error); write.destinationPath = root.filePath(magic + "-rebased.wad");
	ok &= expect(rebased.operations().isEmpty() && rebased.sourceWadLumps().isEmpty() && rebased.writeArchive(write).succeeded()
		&& get(write.destinationPath) == get(expectedPath), "rebased generated WAD keeps plan order without inventing source ordinals");
	PackageStagingModel namespaceOnly; namespaceOnly.createEmpty(PackageArchiveFormat::Wad, magic);
	const QVector<Lump> resources{{"F_START", {}}, {"THINGS", "flat-a"}, {"MAP77", "flat-b"}, {"F_END", {}}};
	for (const auto& [name, bytes] : resources) { ok &= namespaceOnly.addBytes(bytes, QString::fromLatin1(name)); }
	const auto resourceInventory = inspectPackageWadGroups(namespaceOnly);
	ok &= expect(resourceInventory.succeeded() && resourceInventory.groups.size() == 1, "map-like texture names in a new namespace are not map groups", resourceInventory.error);
	write.destinationPath = root.filePath(magic + "-namespace.wad"); ok &= wad(expectedPath, resources, magic.toLatin1());
	ok &= expect(namespaceOnly.writeArchive(write).succeeded() && get(write.destinationPath) == get(expectedPath), "map-like texture names remain inside their namespace at save");
	PackageStagingModel empty; empty.createEmpty(PackageArchiveFormat::Wad, magic); write.destinationPath = root.filePath(magic + "-empty.wad");
	ok &= empty.writeArchive(write).succeeded() && reopened.load(write.destinationPath, &error) && empty.loadBaseArchive(reopened, &error);
	ok &= empty.addWadBytes(QByteArray(4096, 'f'), "FLAT", "flat", 0, false, &error); const auto emptyInventory = inspectPackageWadGroups(empty);
	ok &= expect(emptyInventory.succeeded() && named(emptyInventory, "F_START").memberCount == 3, "opened empty WAD supports immediate semantic editing", error);
	write.destinationPath = root.filePath(magic + "-formerly-empty.wad"); ok &= wad(expectedPath, {{"F_START", {}}, {"FLAT", QByteArray(4096, 'f')}, {"F_END", {}}}, magic.toLatin1());
	ok &= expect(empty.writeArchive(write).succeeded() && get(write.destinationPath) == get(expectedPath), "native texture added to an opened empty WAD retains its anchor");
	return ok;
}
bool unorderedWadWorkflow(const QDir& root)
{
	bool ok = true; QString error; PackageStagingModel model; model.createEmpty(PackageArchiveFormat::Wad);
	const QVector<Lump> prefix{{"F_START", {}}, {"MAP77", "flat"}, {"F_END", {}}};
	const auto binary = map("MAP01", "-loose");
	const QVector<Lump> gl{{"GL_MAP01", {}}, {"GL_VERT", "v"}, {"GL_SEGS", "s"}, {"GL_SSECT", "ss"}, {"GL_NODES", "n"}, {"GL_PVS", "pvs"}};
	for (const auto& [name, bytes] : prefix) { ok &= model.addBytes(bytes, QString::fromLatin1(name)); }
	for (auto at = binary.crbegin(); at != binary.crend(); ++at) { ok &= model.addBytes(at->second, QString::fromLatin1(at->first)); }
	for (auto at = gl.crbegin(); at != gl.crend(); ++at) { ok &= model.addBytes(at->second, QString::fromLatin1(at->first)); }
	QStringList expectedNames; auto expected = prefix + binary + gl;
	for (const auto& [name, bytes] : expected) { Q_UNUSED(bytes); expectedNames << QString::fromLatin1(name); }
	ok &= expect(model.summary().canSave && names(model) == expectedNames, "loose binary and GL assembly is visible in the plan and preserves namespace resources", model.summary().blockedMessages.join(';'));
	const auto inventory = inspectPackageWadGroups(model); const auto group = named(inventory, "MAP01"); PackageWadGroupEditReview review;
	ok &= expect(inventory.succeeded() && group.memberCount == 17 && group.error.isEmpty(), "assembled new map and GL groups are immediately reviewable", inventory.error);
	ok &= expect(stagePackageWadGroupEdit(&model, {group.id, inventory.fingerprint, "INTRO", false}, &review, &error)
		&& names(model).value(3) == "INTRO" && names(model).value(4) == "THINGS", "renaming a loosely assembled marker retains its bound run", error);
	const auto renamedNames = names(model); ok &= expect(model.undo() && names(model) == expectedNames && model.redo() && names(model) == renamedNames,
		"assembly and rename replay with the same membership through undo and redo");
	for (auto& [name, bytes] : expected) { Q_UNUSED(bytes); if (name == "MAP01") { name = "INTRO"; } else if (name == "GL_MAP01") { name = "GL_INTRO"; } }
	const auto draft = root.filePath("assembled.vibepackage"); PackageStagingModel restored;
	ok &= expect(PackageDraft::save(draft, &model, false, &error) && PackageDraft::load(draft, &restored, &error) && names(restored) == renamedNames,
		"draft history replays assembly before the marker loses its conventional name", error);
	PackageWriteRequest write; write.destinationPath = root.filePath("assembled-renamed.wad"); const auto expectedPath = root.filePath("expected-assembled.wad"); ok &= wad(expectedPath, expected);
	ok &= expect(restored.writeArchive(write).succeeded() && get(write.destinationPath) == get(expectedPath), "assembled reviewed plan and independently constructed archive match exactly");
	const auto current = inspectPackageWadGroups(restored); const auto beforeDelete = names(restored);
	ok &= expect(stagePackageWadGroupEdit(&restored, {named(current, "INTRO").id, current.fingerprint, {}, true}, &review, &error)
		&& review.changes.size() == 17 && names(restored) == QStringList{"F_START", "MAP77", "F_END"}
		&& restored.undo() && names(restored) == beforeDelete, "group deletion removes an assembled map and GL companion without moving namespace resources", error);
	PackageStagingModel ambiguous; ambiguous.createEmpty(PackageArchiveFormat::Wad);
	ambiguous.addBytes("things", "THINGS"); ambiguous.addBytes({}, "MAP01"); ambiguous.addBytes({}, "MAP02");
	write.destinationPath = root.filePath("ambiguous-new.wad");
	ok &= expect(!ambiguous.summary().canSave && !inspectPackageWadGroups(ambiguous).succeeded() && !ambiguous.writeArchive(write).succeeded()
		&& !QFileInfo::exists(write.destinationPath), "ambiguous new map membership blocks review and publication");
	ok &= expect(ambiguous.deleteEntry("MAP02", &error) && ambiguous.summary().canSave && names(ambiguous) == QStringList{"MAP01", "THINGS"},
		"raw editing can repair ambiguous assembly without clearing the document", error);
	QVector<qsizetype> order; PackageReadControl control; control.isCancelled = []() { return true; };
	ok &= expect(!orderNewPackageDoomLumps(expectedNames, &order, &error, control) && order.isEmpty(), "new WAD ordering honors cancellation without exposing a partial permutation");
	return ok;
}
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temporary; if (!temporary.isValid()) { return 1; }
	const QDir root(temporary.path()); StudioSettings::setOverrideFilePath(root.filePath("settings.ini")); bool ok = true; QString error;
	auto lumps = fixture(); lumps += QVector<Lump>{{"GL_MAP02", {}}, {"GL_VERT", "vertices"}, {"GL_SEGS", "segs"}, {"GL_SSECT", "subsectors"}, {"GL_NODES", "nodes"}};
	const auto source = root.filePath("source.wad"); PackageArchive archive; PackageStagingModel original;
	ok &= wad(source, lumps) && archive.load(source, &error) && original.loadBaseArchive(archive, &error);
	if (!expect(ok, "prepare group fixture", error)) { return 1; } const auto sourceBytes = get(source);
	const auto inventory = inspectPackageWadGroups(original); const auto second = named(inventory, "MAP02"); const auto gl = named(inventory, "GL_MAP02");
	ok &= expect(inventory.succeeded() && second.id == "map:11" && second.memberCount == 18 && gl.memberCount == 18 && second.canRename,
		"inventory keeps occurrence IDs and GL companions", inventory.error);
	ok &= expect(packageWadGroupsJson(inventory).value("groups").toArray().size() == inventory.groups.size(), "inventory JSON includes all semantic groups");
	auto model = original; PackageWadGroupEditReview review;
	PackageWadGroupEditRequest request{second.id, inventory.fingerprint, "MAP03", false};
	ok &= expect(stagePackageWadGroupEdit(&model, request, &review, &error), "map and GL labels rename together", error);
	ok &= expect(review.changes.size() == 2 && names(model).at(11) == "MAP03" && names(model).contains("GL_MAP03")
		&& model.revision() == 1 && model.operations().size() == 2 && original.operations().isEmpty(), "rename is one immutable-snapshot undo step");
	const auto renamed = names(model); ok &= expect(model.undo() && names(model) == names(original) && model.redo() && names(model) == renamed, "whole map rename undo and redo");
	const auto before = model.manifestJson(); ok &= expect(!stagePackageWadGroupEdit(&model, request, &review, &error) && model.manifestJson() == before && review.changes.isEmpty(), "stale review fails atomically");
	PackageWriteRequest write; write.destinationPath = root.filePath("renamed.wad"); auto written = model.writeArchive(write); PackageArchive reopened; PackageStagingModel roundtrip;
	ok &= expect(written.succeeded() && reopened.load(write.destinationPath, &error) && roundtrip.loadBaseArchive(reopened, &error)
		&& names(roundtrip) == renamed, "save preserves group order and companion labels", error);
	QByteArray payload; const auto planned = packagePlannedArchive(roundtrip);
	ok &= expect(planned.readEntryAt(index(planned, "THINGS", 1), &payload, &error) && payload == "THINGS-second", "map rename preserves exact second-map bytes");
	const auto draft = root.filePath("work.vibepackage"); ok &= PackageDraft::save(draft, &model, false, &error);
	PackageStagingModel restored; ok &= expect(PackageDraft::load(draft, &restored, &error) && names(restored) == renamed && restored.undo()
		&& names(restored) == names(original) && restored.redo(), "portable draft replays grouped rename and history", error);
	for (const QString& target : {QString("MAP01"), QString("THINGS"), QString("F_START"), QString("TEXTMAP"), QString("PNAMES"), QString("GL_FOO"), QString("LONGNAME"), QString("../BAD"), QString("bad label"), QString("é")}) {
		model = original; request = {second.id, inventory.fingerprint, target, false};
		ok &= expect(!stagePackageWadGroupEdit(&model, request, nullptr, &error) && model.operations().isEmpty() && !model.canUndo(), "invalid or colliding rename consumes no history");
	}
	model = original; request = {gl.id, inventory.fingerprint, "e2m3", false};
	ok &= expect(stagePackageWadGroupEdit(&model, request, &review, &error) && names(model).contains("E2M3") && names(model).contains("GL_E2M3"), "selecting GL renames the main companion too", error);
	model = original; request = {second.id, inventory.fingerprint, "MAP02", false};
	ok &= expect(stagePackageWadGroupEdit(&model, request, &review, &error) && !model.canUndo() && review.changes.isEmpty(), "identical label makes no history");
	model = original; request = {second.id, inventory.fingerprint, {}, true};
	ok &= expect(stagePackageWadGroupEdit(&model, request, &review, &error) && review.changes.size() == 18 && !names(model).contains("MAP02")
		&& !names(model).contains("GL_MAP02") && names(model).count("THINGS") == 1, "delete removes only the chosen complete map and GL group", error);
	ok &= expect(model.undo() && names(model) == names(original) && model.redo(), "group deletion is one undo step");
	model = original; const auto nested = named(inventory, "P1_START"); request = {nested.id, inventory.fingerprint, {}, true};
	ok &= expect(stagePackageWadGroupEdit(&model, request, &review, &error) && review.changes.size() == 3 && names(model).contains("P_START")
		&& names(model).contains("P_END") && !names(model).contains("PATCHA"), "nested namespace deletion retains the enclosing pair", error);
	model = original; request = {named(inventory, "F_START").id, inventory.fingerprint, {}, true};
	ok &= model.addWadBytes(QByteArray(4096, 'a'), "FLOORNEW", "flat", 0, false, &error);
	request.expectedFingerprint = inspectPackageWadGroups(model).fingerprint;
	ok &= expect(stagePackageWadGroupEdit(&model, request, &review, &error) && review.changes.size() == 5 && !names(model).contains("FLOORNEW")
		&& model.summary().canSave && model.undo() && names(model).contains("FLOORNEW"), "group includes anchored native texture additions and undo retains their placement", error);
	model = original; request = {"texture-tables", inventory.fingerprint, {}, true};
	ok &= expect(stagePackageWadGroupEdit(&model, request, &review, &error) && review.changes.size() == 3 && !names(model).contains("PNAMES"), "delete local texture-name tables as a unit", error);
	model = original; request = {named(inventory, "TITLEMAP").id, inventory.fingerprint, "INTRO", false};
	ok &= expect(stagePackageWadGroupEdit(&model, request, &review, &error) && names(model).contains("INTRO") && names(model).contains("CUSTOM"), "UDMF named map rename retains arbitrary sidecars", error);
	request = {named(inspectPackageWadGroups(model), "INTRO").id, inspectPackageWadGroups(model).fingerprint, {}, true};
	ok &= expect(stagePackageWadGroupEdit(&model, request, &review, &error) && review.changes.size() == 5 && !names(model).contains("CUSTOM"), "UDMF delete retains other groups", error);
	model = original; request = {second.id, inventory.fingerprint, {}, true}; int calls = 0; PackageReadControl control; control.isCancelled = [&]() { return ++calls > 150; };
	ok &= expect(!stagePackageWadGroupEdit(&model, request, &review, &error, control) && model.operations().isEmpty() && !model.canUndo(), "mid-analysis cancellation preserves original history");
	bool verifying = false; control.isCancelled = [&]() { return verifying; };
	control.progress = [&](const QString&, qint64, qint64) { verifying = true; };
	ok &= expect(!stagePackageWadGroupEdit(&model, request, &review, &error, control) && verifying && model.operations().isEmpty()
		&& !model.canUndo() && review.changes.isEmpty(), "cancelling source verification discards the already prepared private edit");
	const auto malformed = root.filePath("malformed.wad"); PackageArchive broken;
	ok &= wad(malformed, {{"MAP01", {}}, {"THINGS", "x"}}) && broken.load(malformed, &error) && model.loadBaseArchive(broken, &error);
	auto blocked = inspectPackageWadGroups(model); request = {named(blocked, "MAP01").id, blocked.fingerprint, {}, true};
	ok &= expect(!stagePackageWadGroupEdit(&model, request, nullptr, &error) && model.operations().isEmpty(), "incomplete map deletion is refused");
	ok &= wad(malformed, {{"F_START", {}}, {"FLAT", "x"}}) && broken.load(malformed, &error) && model.loadBaseArchive(broken, &error);
	ok &= expect(!inspectPackageWadGroups(model).succeeded(), "unclosed namespace blocks group interpretation");
	ok &= wad(malformed, map("MAP01", "a") + map("MAP01", "b") + QVector<Lump>{{"GL_MAP01", {}}, {"GL_VERT", "a"}, {"GL_SEGS", "b"}, {"GL_SSECT", "c"}, {"GL_NODES", "d"}})
		&& broken.load(malformed, &error) && model.loadBaseArchive(broken, &error);
	blocked = inspectPackageWadGroups(model); request = {named(blocked, "GL_MAP01").id, blocked.fingerprint, {}, true};
	ok &= expect(!stagePackageWadGroupEdit(&model, request, nullptr, &error), "ambiguous companion ownership blocks deletion");
	ok &= wad(malformed, {{"F_START", {}}, {"PNAMES", "flat"}, {"THINGS", "flat"}, {"MAP01", "flat"}, {"F_END", {}}, {"PNAMES", "global"}, {"TEXTURE1", "global"}})
		&& broken.load(malformed, &error) && model.loadBaseArchive(broken, &error);
	blocked = inspectPackageWadGroups(model); request = {"texture-tables", blocked.fingerprint, {}, true};
	ok &= expect(blocked.groups.size() == 2 && stagePackageWadGroupEdit(&model, request, &review, &error) && review.changes.size() == 2
		&& names(model).contains("PNAMES") && names(model).contains("THINGS"), "namespace resources are not mistaken for global semantic names", error);
	ok &= wad(malformed, map("MAP01", "main") + map("GL_MAP01", "separate")) && broken.load(malformed, &error) && model.loadBaseArchive(broken, &error);
	blocked = inspectPackageWadGroups(model); request = {named(blocked, "GL_MAP01").id, blocked.fingerprint, "INTRO", false};
	ok &= expect(named(blocked, "MAP01").memberCount == 11 && stagePackageWadGroupEdit(&model, request, &review, &error)
		&& review.changes.size() == 1 && names(model).contains("INTRO") && names(model).contains("MAP01"), "a binary map with GL prefix is not a GL node run", error);
	model = original; request = {second.id, inventory.fingerprint, "MAP03", false}; ok &= put(source, sourceBytes + "changed");
	ok &= expect(!stagePackageWadGroupEdit(&model, request, nullptr, &error) && model.operations().isEmpty(), "source replacement during review fails atomically");
	auto changed = sourceBytes; changed[12] = static_cast<char>(changed.at(12) ^ 1); ok &= put(source, changed) && archive.load(source, &error) && model.loadBaseArchive(archive, &error);
	ok &= expect(inspectPackageWadGroups(model).fingerprint != inventory.fingerprint && !stagePackageWadGroupEdit(&model, request, nullptr, &error), "same-sized source content changes invalidate an earlier CLI review");
	for (const auto& magic : {QStringLiteral("PWAD"), QStringLiteral("IWAD")}) { ok &= newWadOrderWorkflow(root, magic); }
	ok &= unorderedWadWorkflow(root);
	return ok ? 0 : 1;
}
