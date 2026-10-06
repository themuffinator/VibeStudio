#include "core/package_draft.h"
#include "core/package_preview.h"
#include "core/package_validation.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QTemporaryDir>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include <iostream>
#include <limits>

using namespace vibestudio;

namespace {
bool expect(bool value, const char* message, const QString& error = {})
{
	if (!value) { std::cerr << message << ": " << error.toStdString() << '\n'; }
	return value;
}
bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
QByteArray payload(const PackageStagingModel& model, const QString& path)
{
	QByteArray bytes; QString error; PackageStagingArchive(model).readEntryBytes(path, &bytes, &error); return bytes;
}
bool historyCounterBoundaries(const QDir& root)
{
	constexpr quint64 ceiling = std::numeric_limits<quint64>::max() - 1;
	QString error; bool ok = true;
	const auto fixture = [&](const QString& name, quint64 operationSerial, quint64 revisionSerial, PackageStagingModel* model) {
		PackageStagingModel seed;
		const QString path = root.filePath(name + QStringLiteral(".vibepackage"));
		if (!seed.createEmpty(PackageArchiveFormat::Zip) || !seed.addBytes("one", QStringLiteral("one.txt"))
			|| !seed.addBytes("two", QStringLiteral("two.txt")) || !seed.undo()
			|| !PackageDraft::save(path, &seed, false, &error)) { return false; }
		const QString manifest = QDir(path).filePath(QStringLiteral("document.json"));
		auto json = QJsonDocument::fromJson(read(manifest)).object();
		json.insert(QStringLiteral("operationSerial"), QString::number(operationSerial));
		json.insert(QStringLiteral("revisionSerial"), QString::number(revisionSerial));
		return write(manifest, QJsonDocument(json).toJson()) && PackageDraft::load(path, model, &error);
	};
	const QString input = root.filePath(QStringLiteral("counter-input.bin"));
	if (!write(input, "read only after admission")) { return false; }
	PackageStagingModel revision;
	if (!expect(fixture(QStringLiteral("revision-boundary"), 2, ceiling - 1, &revision), "load revision boundary fixture", error)) { return false; }
	ok &= expect(revision.addBytes("last revision", QStringLiteral("last.txt"), &error) && revision.revision() == ceiling,
		"last persistable revision is accepted exactly", error);
	ok &= expect(revision.undo() && revision.canRedo(), "boundary undo keeps the last revision available for redo");
	const auto oldRevision = revision.revision(); const auto oldId = revision.operations().first().id;
	int reads = 0; PackageReadControl control;
	control.progress = [&](const QString&, qint64, qint64) { ++reads; };
	ok &= expect(!revision.addFile(input, QStringLiteral("rejected.bin"), &error, PackageStageConflictResolution::Block, control)
		&& error.contains(QStringLiteral("counter limit")) && reads == 0, "revision refusal precedes source reads", error);
	ok &= expect(!revision.clearOperation(oldId, &error) && error.contains(QStringLiteral("counter limit"))
		&& revision.revision() == oldRevision && revision.operations().size() == 1 && revision.operations().first().id == oldId
		&& revision.canRedo(), "unstage cannot consume an unpersistable revision or discard redo", error);
	ok &= expect(revision.beginOperationGroup(QStringLiteral("No edits"), &error), "empty groups do not require a new revision", error);
	ok &= expect(!revision.addBytes("no", QStringLiteral("no.txt"), &error), "groups cannot bypass revision admission");
	revision.endOperationGroup();
	ok &= expect(revision.revision() == oldRevision && revision.canRedo(), "refused empty group preserves revision and redo");
	const QString savedRevision = root.filePath(QStringLiteral("revision-saved.vibepackage"));
	PackageStagingModel revisionReloaded;
	ok &= expect(PackageDraft::save(savedRevision, &revision, false, &error)
		&& PackageDraft::load(savedRevision, &revisionReloaded, &error) && revisionReloaded.redo()
		&& revisionReloaded.revision() == ceiling && payload(revisionReloaded, QStringLiteral("last.txt")) == "last revision",
		"saturated revision saves and reloads with working redo", error);
	const QString savedManifest = QDir(savedRevision).filePath(QStringLiteral("document.json"));
	ok &= expect(QJsonDocument::fromJson(read(savedManifest)).object().value(QStringLiteral("revisionSerial")).toString() == QString::number(ceiling),
		"draft serial remains the exact decimal boundary value");
	PackageWriteRequest exported; exported.format = PackageArchiveFormat::Zip;
	exported.destinationPath = root.filePath(QStringLiteral("counter-reset.zip"));
	PackageArchive archive; PackageStagingModel reset;
	ok &= expect(revisionReloaded.writeArchive(exported).succeeded() && archive.load(exported.destinationPath, &error)
		&& reset.loadBaseArchive(archive, &error) && reset.addBytes("new", QStringLiteral("new.txt"), &error)
		&& reset.operations().first().id == QStringLiteral("stage-1"), "export and reopen starts fresh history without changing package content", error);

	PackageStagingModel grouped;
	if (!expect(fixture(QStringLiteral("group-boundary"), 2, ceiling - 1, &grouped), "load final group fixture", error)) { return false; }
	const auto groupStart = grouped.revision();
	ok &= grouped.beginOperationGroup(QStringLiteral("Last group"), &error);
	ok &= grouped.beginOperationGroup(QStringLiteral("Nested"), &error);
	ok &= grouped.clearOperation(grouped.operations().first().id, &error);
	ok &= grouped.addBytes("A", QStringLiteral("a.txt"), &error) && grouped.addBytes("B", QStringLiteral("b.txt"), &error);
	grouped.endOperationGroup();
	ok &= expect(grouped.revision() == groupStart && !grouped.canUndo(), "nested edits reserve one outer revision");
	grouped.endOperationGroup();
	ok &= expect(grouped.revision() == ceiling && grouped.undo() && grouped.operations().size() == 1 && grouped.redo()
		&& payload(grouped, QStringLiteral("a.txt")) == "A" && payload(grouped, QStringLiteral("b.txt")) == "B",
		"the final grouped revision remains atomic and reversible");
	const QString groupedPath = root.filePath(QStringLiteral("group-saved.vibepackage"));
	PackageStagingModel groupedReloaded;
	ok &= expect(PackageDraft::save(groupedPath, &grouped, false, &error) && PackageDraft::load(groupedPath, &groupedReloaded, &error)
		&& groupedReloaded.undo() && groupedReloaded.redo() && groupedReloaded.revision() == ceiling,
		"the final nested group survives draft replay", error);

	PackageStagingModel operation;
	if (!expect(fixture(QStringLiteral("operation-boundary"), ceiling - 1, 3, &operation), "load operation boundary fixture", error)) { return false; }
	auto cancelled = operation;
	ok &= expect(operation.addBytes("last operation", QStringLiteral("last.txt"), &error)
		&& operation.operations().last().id == QStringLiteral("stage-%1").arg(ceiling) && operation.undo() && operation.canRedo(),
		"last operation id is exact and its undo remains available", error);
	const auto operationRevision = operation.revision(); reads = 0;
	ok &= expect(!operation.addFile(input, QStringLiteral("rejected.bin"), &error, PackageStageConflictResolution::Block, control) && reads == 0
		&& error.contains(QStringLiteral("counter limit")) && operation.revision() == operationRevision && operation.canRedo(),
		"operation counter refusal precedes reads and preserves the redo branch", error);
	ok &= expect(operation.clearOperation(operation.operations().first().id, &error) && operation.operations().isEmpty()
		&& operation.undo() && payload(operation, QStringLiteral("one.txt")) == "one",
		"unstage and its undo still work when only operation ids are exhausted", error);
	const QString operationPath = root.filePath(QStringLiteral("operation-saved.vibepackage"));
	PackageStagingModel operationReloaded;
	ok &= expect(PackageDraft::save(operationPath, &operation, false, &error) && PackageDraft::load(operationPath, &operationReloaded, &error)
		&& operationReloaded.canRedo() && !operationReloaded.addBytes("no", QStringLiteral("no.txt"), &error)
		&& operationReloaded.canRedo(), "operation saturation remains diagnosed after saving without losing redo", error);
	ok &= cancelled.beginOperationGroup(QStringLiteral("Cancelled final id"), &error);
	ok &= expect(cancelled.addBytes("discarded", QStringLiteral("discarded.txt"), &error)
		&& !cancelled.addBytes("overflow", QStringLiteral("overflow.txt"), &error), "a group cannot allocate a sentinel or wrapped operation id", error);
	cancelled.endOperationGroup(false);
	ok &= expect(cancelled.operations().size() == 1 && cancelled.canRedo() && cancelled.revision() == operationRevision
		&& !cancelled.addBytes("reused", QStringLiteral("reused.txt"), &error) && cancelled.redo()
		&& payload(cancelled, QStringLiteral("two.txt")) == "two", "group cancellation preserves redo without reusing its consumed final id", error);

	const QByteArray validManifest = read(savedManifest);
	for (const QString& field : {QStringLiteral("operationSerial"), QStringLiteral("revisionSerial")}) {
		for (const QString& value : {QString::number(std::numeric_limits<quint64>::max()), QStringLiteral("18446744073709551616")}) {
			auto malformed = QJsonDocument::fromJson(validManifest).object(); malformed.insert(field, value);
			ok &= write(savedManifest, QJsonDocument(malformed).toJson());
			const auto preservedRevision = groupedReloaded.revision();
			ok &= expect(!PackageDraft::load(savedRevision, &groupedReloaded, &error)
				&& groupedReloaded.revision() == preservedRevision && payload(groupedReloaded, QStringLiteral("b.txt")) == "B",
				"sentinel and overflow serials cannot replace an open document", error);
		}
	}
	ok &= write(savedManifest, validManifest);
	return ok;
}

void u32(QByteArray& bytes, quint32 value) { for (int shift = 0; shift < 32; shift += 8) { bytes.append(static_cast<char>(value >> shift)); } }
QByteArray wad()
{
	QByteArray bytes("PWAD"); u32(bytes, 4); u32(bytes, 20); bytes.append("AAAABBBB");
	for (int index = 0; index < 4; ++index) {
		u32(bytes, index < 2 ? 12 : 16); u32(bytes, index % 2 ? 4 : 0);
		QByteArray name = index % 2 ? QByteArray("THINGS") : index ? QByteArray("MAP02") : QByteArray("MAP01");
		name.resize(8, '\0'); bytes.append(name);
	}
	return bytes;
}
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temporary;
	if (!temporary.isValid()) { return 1; }
	QDir root(temporary.path()); root.mkdir(QStringLiteral("source"));
	const QString source = root.filePath(QStringLiteral("source"));
	const QString sourceFile = QDir(source).filePath(QStringLiteral("original.txt"));
	const QString import = root.filePath(QStringLiteral("import.bin"));
	const QString redoFile = root.filePath(QStringLiteral("redo.bin"));
	const QByteArray large(200003, 'L');
	bool ok = write(sourceFile, "original") && write(import, large) && write(redoFile, "redo-only");
	QString error;
	PackageArchive archive;
	PackageStagingModel plan;
	ok &= expect(archive.load(source, &error) && plan.loadBaseArchive(archive, &error), "load base", error);
	if (!ok) { return 1; }
	ok &= expect(!plan.isModified() && !plan.canUndo() && !plan.canRedo(), "newly loaded document is clean");
	plan.beginOperationGroup(QStringLiteral("Import and rename"));
	ok &= plan.addFile(import, QStringLiteral("data.bin"), &error);
	plan.beginOperationGroup(QStringLiteral("nested"));
	ok &= plan.renameEntry(QStringLiteral("original.txt"), QStringLiteral("renamed.txt"), &error);
	plan.endOperationGroup();
	ok &= expect(!plan.canUndo() && plan.isModified(), "in-progress group has no partial undo");
	plan.endOperationGroup();
	ok &= expect(plan.undoLabel() == QStringLiteral("Import and rename") && plan.operations().size() == 2 && plan.undo(), "grouped undo available");
	ok &= expect(plan.operations().isEmpty() && !plan.isModified() && plan.canRedo() && plan.redo(), "one undo reverses whole group");
	const auto firstIds = plan.operations();
	plan.markSaved();
	ok &= expect(!plan.isModified() && plan.undo() && plan.isModified() && plan.redo() && !plan.isModified(), "saved checkpoint follows undo/redo");
	plan.beginOperationGroup(QStringLiteral("Unstage both"));
	ok &= plan.clearOperation(firstIds.at(0).id); ok &= plan.clearOperation(firstIds.at(1).id);
	plan.endOperationGroup();
	ok &= expect(plan.operations().isEmpty() && plan.undo() && plan.operations().size() == 2, "unstage group is undoable and restores order");
	plan.beginOperationGroup(QStringLiteral("Cancelled")); plan.addBytes("cancel", QStringLiteral("cancel.txt")); plan.endOperationGroup(false);
	ok &= expect(plan.operations().size() == 2 && !plan.isModified() && plan.canRedo(), "cancelled group preserves redo and checkpoint");
	ok &= plan.addBytes("generated", QStringLiteral("generated.txt"));
	ok &= expect(!plan.canRedo(), "new edit after undo truncates redo branch");
	ok &= plan.addFile(redoFile, QStringLiteral("redo.txt"));
	const QString redoId = plan.operations().last().id;
	ok &= plan.undo();
	const QString draft = root.filePath(QStringLiteral("work.vibepackage"));
	ok &= expect(PackageDraft::save(draft, &plan, false, &error), "save draft with undone disk input", error);
	if (!ok) { return 1; }
	ok &= expect(!plan.isModified() && plan.canRedo() && plan.draftPath() == draft, "saving marks current revision and retains redo");
	const QByteArray manifest = read(QDir(draft).filePath(QStringLiteral("document.json")));
	ok &= expect(!manifest.isEmpty() && !manifest.contains(large.left(500)), "payloads are stored outside JSON");
	ok &= write(sourceFile, "changed original"); ok &= write(import, "changed import"); ok &= write(redoFile, "changed redo");
	ok &= expect(payload(plan, QStringLiteral("renamed.txt")) == "original" && payload(plan, QStringLiteral("data.bin")) == large, "saved live plan owns immutable payloads");
	PackageStagingModel reopened;
	ok &= expect(PackageDraft::load(draft, &reopened, &error), "reopen without original sources", error);
	ok &= expect(payload(reopened, QStringLiteral("generated.txt")) == "generated" && reopened.redo()
		&& payload(reopened, QStringLiteral("redo.txt")) == "redo-only", "generated and redo-only payloads survive reopening");
	const QString copiedDraft = root.filePath(QStringLiteral("copy.vibepackage"));
	auto independent = reopened;
	ok &= expect(PackageDraft::save(copiedDraft, &independent, false, &error) && independent.canUndo(), "Save Draft As carries independent history", error);
	ok &= expect(reopened.operations().last().id == redoId && reopened.isModified(), "restored history keeps stable operation identities");
	ok &= expect(reopened.undo() && !reopened.isModified(), "undo returns to persisted checkpoint");
	PackageArchive base = PackageDraft::baseArchive(reopened);
	QByteArray baseBytes;
	ok &= expect(base.readEntryBytes(QStringLiteral("original.txt"), &baseBytes, &error) && baseBytes == "original"
		&& base.verifySourceIdentity(&error), "base reader adapts immutable draft content", error);
	PackageStagingModel derived;
	ok &= expect(derived.loadBaseArchive(base, &error) && payload(derived, QStringLiteral("original.txt")) == "original", "shared archive consumers retain draft backing", error);
	PackageWriteRequest request; request.destinationPath = root.filePath(QStringLiteral("export.pk3")); request.format = PackageArchiveFormat::Pk3;
	const auto report = reopened.writeArchive(request);
	ok &= expect(report.succeeded(), "export resumed draft", report.blockedMessages.join('\n'));
	PackageArchive exported;
	ok &= expect(exported.load(request.destinationPath, &error) && exported.readEntryBytes(QStringLiteral("data.bin"), &baseBytes, &error)
		&& baseBytes == large, "round-trip draft output", error);
	request.destinationPath = QDir(draft).filePath(QStringLiteral("objects/output.pk3"));
	ok &= expect(!reopened.writeArchive(request).succeeded() && !QFileInfo::exists(request.destinationPath), "archive output cannot modify draft backing");
	PackageStagingModel concurrent;
	ok &= PackageDraft::load(draft, &concurrent, &error);
	reopened.addBytes("new", QStringLiteral("new.txt"));
	ok &= expect(!PackageDraft::save(draft, &reopened, false, &error), "draft overwrite requires explicit intent");
	ok &= expect(PackageDraft::save(draft, &reopened, true, &error), "save next draft checkpoint", error);
	concurrent.addBytes("stale", QStringLiteral("stale.txt"));
	ok &= expect(!PackageDraft::save(draft, &concurrent, true, &error) && error.contains(QStringLiteral("changed")), "stale draft refuses overwriting newer edits", error);
	const QByteArray goodManifest = read(QDir(draft).filePath(QStringLiteral("document.json")));
	{
		QLockFile held(QDir(draft).filePath(QStringLiteral(".write.lock")));
		ok &= expect(held.tryLock(), "reserve draft writer lock");
		ok &= expect(!PackageDraft::save(draft, &reopened, true, &error) && read(QDir(draft).filePath(QStringLiteral("document.json"))) == goodManifest,
			"a second draft writer cannot change the committed document");
	}
#ifdef Q_OS_WIN
	{
		const QString path = QDir(draft).filePath(QStringLiteral("document.json"));
		const HANDLE held = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		ok &= expect(held != INVALID_HANDLE_VALUE, "hold real manifest handle without delete sharing");
		if (held != INVALID_HANDLE_VALUE) {
			auto failed = reopened; failed.addBytes("uncommitted", QStringLiteral("failure.txt"));
			ok &= expect(!PackageDraft::save(draft, &failed, true, &error) && failed.isModified()
				&& read(path) == goodManifest, "failed atomic draft replacement preserves original metadata and dirty history", error);
			CloseHandle(held);
		}
	}
#endif
	bool cancel = false;
	PackageReadControl control;
	control.isCancelled = [&]() { return cancel; };
	control.progress = [&](const QString&, qint64 completed, qint64) { if (completed >= 65536) { cancel = true; } };
	reopened.addBytes(large, QStringLiteral("cancel-copy.bin"));
	ok &= expect(!PackageDraft::save(draft, &reopened, true, &error, control) && reopened.isModified()
		&& read(QDir(draft).filePath(QStringLiteral("document.json"))) == goodManifest, "within-file cancellation preserves committed draft", error);
	const auto modifyManifest = [&](const std::function<void(QJsonObject&)>& mutate) {
		auto document = QJsonDocument::fromJson(goodManifest).object(); mutate(document);
		return write(QDir(draft).filePath(QStringLiteral("document.json")), QJsonDocument(document).toJson());
	};
	PackageStagingModel preserved = concurrent;
	const auto preservedRevision = preserved.revision();
	modifyManifest([](QJsonObject& document) {
		auto history = document.value(QStringLiteral("history")).toArray(); auto step = history.last().toObject();
		auto changes = step.value(QStringLiteral("changes")).toArray(); auto change = changes.first().toObject();
		change.insert(QStringLiteral("index"), QStringLiteral("99999")); changes[0] = change; step.insert(QStringLiteral("changes"), changes);
		history[history.size() - 1] = step; document.insert(QStringLiteral("history"), history);
	});
	ok &= expect(!PackageDraft::load(draft, &preserved, &error) && preserved.revision() == preservedRevision, "invalid undo index rejected without replacing live document", error);
	modifyManifest([](QJsonObject& document) {
		auto entries = document.value(QStringLiteral("base")).toArray(); auto entry = entries.first().toObject();
		entry.insert(QStringLiteral("object"), QStringLiteral("../../import.bin")); entries[0] = entry; document.insert(QStringLiteral("base"), entries);
	});
	ok &= expect(!PackageDraft::load(draft, &preserved, &error), "manifest payload traversal rejected");
	write(QDir(draft).filePath(QStringLiteral("document.json")), goodManifest);
	const QString hash = QString::fromLatin1(QCryptographicHash::hash(large, QCryptographicHash::Sha256).toHex());
	const QString blob = QDir(draft).filePath(QStringLiteral("objects/") + hash);
	ok &= write(blob, QByteArray(large.size(), 'X'));
	ok &= expect(!PackageDraft::load(draft, &preserved, &error) && error.contains(QStringLiteral("content check")), "changed payload blocks draft loading", error);
	write(blob, large);
	// Persisted data is independent across Save As, not hard linked or reused
	// through paths back into the earlier draft's object directory.
	write(blob, QByteArray(large.size(), 'Z'));
	PackageStagingModel copied;
	ok &= expect(PackageDraft::load(copiedDraft, &copied, &error) && payload(copied, QStringLiteral("data.bin")) == large, "independent draft copy survives corruption of the earlier draft", error);
	write(blob, large);
	const QString wadPath = root.filePath(QStringLiteral("maps.wad")); write(wadPath, wad());
	PackageArchive wadArchive; PackageStagingModel wadPlan;
	ok &= wadArchive.load(wadPath, &error) && wadPlan.loadBaseArchive(wadArchive, &error);
	const QString wadDraft = root.filePath(QStringLiteral("maps.vibepackage"));
	ok &= expect(PackageDraft::save(wadDraft, &wadPlan, false, &error), "snapshot repeated WAD lumps", error);
	write(wadPath, "no original WAD remains");
	ok &= expect(PackageDraft::load(wadDraft, &wadPlan, &error), "reopen repeated WAD draft", error);
	PackageStagingArchive wadReader(wadPlan);
	QVector<QByteArray> things;
	for (qsizetype i = 0; i < wadReader.entries().size(); ++i) {
		if (wadReader.entries().at(i).virtualPath == QStringLiteral("THINGS")) { QByteArray bytes; wadReader.readEntryAt(i, &bytes, &error); things << bytes; }
	}
	ok &= expect(things == QVector<QByteArray>{"AAAA", "BBBB"} && wadPlan.sourceWadMagic() == QStringLiteral("PWAD"), "physical WAD occurrence identities preserved");
	const auto wadView = packagePlannedArchive(wadPlan);
	QVector<QString> occurrencePreviews;
	for (qsizetype index = 0; index < wadView.entries().size(); ++index) {
		if (wadView.entries().at(index).virtualPath == QStringLiteral("THINGS")) { occurrencePreviews << buildPackageEntryPreviewAt(wadView, index).body; }
	}
	ok &= expect(occurrencePreviews.size() == 2 && occurrencePreviews.at(0) != occurrencePreviews.at(1)
		&& occurrencePreviews.at(0).contains(QStringLiteral("AAAA")) && occurrencePreviews.at(1).contains(QStringLiteral("BBBB"))
		&& buildPackageEntryPreview(wadView, QStringLiteral("THINGS")).kind == PackagePreviewKind::Unavailable,
		"row previews identify repeated WAD payloads and path previews refuse ambiguity");
	{
		auto edited = wadPlan;
		const QString replacement = root.filePath(QStringLiteral("occurrence.bin"));
		ok &= write(replacement, "CCCC");
		const auto occurrenceBytes = [&](const PackageStagingModel& model, int ordinal) {
			for (const auto& entry : model.plannedEntries()) {
				if (entry.sourceOrdinal == ordinal) { QByteArray bytes; model.entryBytes(entry, &bytes); return bytes; }
			}
			return QByteArray();
		};
		ok &= expect(edited.replaceOccurrence(3, replacement, &error) && edited.summary().canSave
			&& occurrenceBytes(edited, 1) == "AAAA" && occurrenceBytes(edited, 3) == "CCCC", "replace exactly the selected repeated WAD occurrence", error);
		ok &= expect(edited.undoLabel().contains(QStringLiteral("source entry 4")), "undo label identifies the edited source occurrence");
		ok &= expect(edited.renameOccurrence(1, QStringLiteral("ACTORS"), &error) && edited.summary().canSave,
			"rename one repeated occurrence without moving it", error);
		const QString renameId = edited.operations().last().id;
		ok &= expect(edited.deleteEntry(QStringLiteral("THINGS"), &error) && edited.summary().canSave
			&& occurrenceBytes(edited, 1) == "AAAA" && occurrenceBytes(edited, 3).isEmpty(),
			"path lookup finds the surviving occurrence after another was renamed", error);
		ok &= expect(edited.undo() && occurrenceBytes(edited, 3) == "CCCC", "undo restores the removed occurrence");
		const QString selectedDraft = root.filePath(QStringLiteral("occurrences.vibepackage"));
		ok &= expect(PackageDraft::save(selectedDraft, &edited, false, &error), "save occurrence edits and their redo branch", error);
		PackageStagingModel restored;
		ok &= expect(PackageDraft::load(selectedDraft, &restored, &error) && restored.canRedo() && restored.redo()
			&& occurrenceBytes(restored, 3).isEmpty() && restored.undo(), "restore occurrence history and redo identity", error);
		ok &= expect(restored.replaceOccurrence(1, replacement, &error) && restored.summary().canSave
			&& restored.clearOperation(renameId) && !restored.summary().canSave
			&& occurrenceBytes(restored, 1) == "AAAA", "removing an earlier rename blocks a stale occurrence path instead of retargeting it", error);
		const auto operationsBefore = restored.operations().size();
		ok &= expect(!restored.deleteOccurrence(-1, &error) && !restored.renameOccurrence(1000, QStringLiteral("MISSING"), &error)
			&& restored.operations().size() == operationsBefore, "invalid occurrence selectors do not alter history");
		auto removed = wadPlan;
		removed.beginOperationGroup(QStringLiteral("Delete occurrences"));
		ok &= removed.deleteOccurrence(1, &error) && removed.deleteOccurrence(3, &error);
		removed.endOperationGroup();
		ok &= expect(removed.summary().canSave && removed.plannedEntries().size() == 2 && removed.undo()
			&& occurrenceBytes(removed, 1) == "AAAA" && occurrenceBytes(removed, 3) == "BBBB",
			"grouped exact deletes preserve neighboring map markers and undo as one edit");
		PackageWriteRequest output; output.format = PackageArchiveFormat::Wad;
		output.destinationPath = root.filePath(QStringLiteral("occurrence-output.wad"));
		ok &= expect(edited.writeArchive(output).succeeded(), "write occurrence edits in source WAD order");
		PackageArchive written; PackageStagingModel writtenPlan;
		ok &= expect(written.load(output.destinationPath, &error) && writtenPlan.loadBaseArchive(written, &error)
			&& occurrenceBytes(writtenPlan, 1) == "AAAA" && occurrenceBytes(writtenPlan, 3) == "CCCC",
			"reopened WAD preserves selected payloads and source positions", error);
		// Version 1 remains readable, while selectors require version 2 so old
		// builds cannot silently discard identity fields and replay by name.
		const QString legacy = root.filePath(QStringLiteral("legacy.vibepackage"));
		auto oldPlan = wadPlan;
		oldPlan.renameEntry(QStringLiteral("MAP01"), QStringLiteral("E1M1"));
		ok &= PackageDraft::save(legacy, &oldPlan, false, &error);
		const QString metadata = QDir(legacy).filePath(QStringLiteral("document.json"));
		auto document = QJsonDocument::fromJson(read(metadata)).object();
		document.insert(QStringLiteral("version"), 1);
		auto legacyOperations = document.value(QStringLiteral("operations")).toArray();
		for (qsizetype index = 0; index < legacyOperations.size(); ++index) {
			auto operation = legacyOperations.at(index).toObject(); operation.remove(QStringLiteral("sourceOrdinal")); legacyOperations[index] = operation;
		}
		document.insert(QStringLiteral("operations"), legacyOperations);
		ok &= write(metadata, QJsonDocument(document).toJson());
		ok &= expect(PackageDraft::load(legacy, &oldPlan, &error) && oldPlan.summary().canSave && oldPlan.canUndo(), "version 1 drafts remain readable", error);
		const QString selectedMetadata = QDir(selectedDraft).filePath(QStringLiteral("document.json"));
		auto invalidDocument = QJsonDocument::fromJson(read(selectedMetadata)).object();
		auto invalidOperations = invalidDocument.value(QStringLiteral("operations")).toArray();
		auto invalidOperation = invalidOperations.at(0).toObject(); invalidOperation.insert(QStringLiteral("sourceOrdinal"), 1.5);
		invalidOperations[0] = invalidOperation; invalidDocument.insert(QStringLiteral("operations"), invalidOperations);
		ok &= write(selectedMetadata, QJsonDocument(invalidDocument).toJson());
		ok &= expect(!PackageDraft::load(selectedDraft, &restored, &error), "non-integral occurrence selectors are rejected before history adoption");
	}
	{
		// One deliberately unsupported compressed WAD2 lump. The browser must
		// keep its row and diagnostic even though staging excludes its payload.
		QByteArray compressed("WAD2"); u32(compressed, 1); u32(compressed, 16); compressed.append("data");
		u32(compressed, 12); u32(compressed, 4); u32(compressed, 4);
		compressed.append(char(67)); compressed.append(char(1)); compressed.append(QByteArray(2, '\0'));
		QByteArray name("COMPRESSED"); name.resize(16, '\0'); compressed.append(name);
		const QString unreadablePath = root.filePath(QStringLiteral("unsupported.wad"));
		ok &= write(unreadablePath, compressed);
		PackageArchive unreadable; PackageStagingModel blocked;
		ok &= expect(unreadable.load(unreadablePath, &error) && blocked.loadBaseArchive(unreadable, &error), "load unsupported source member", error);
		const auto inspection = packagePlannedArchive(blocked);
		const auto rows = inspection.entries();
		QByteArray discarded;
		ok &= expect(inspection.isOpen() && rows.size() == 1 && rows.at(0).virtualPath == QStringLiteral("COMPRESSED")
			&& !rows.at(0).readable && !rows.at(0).note.isEmpty() && !inspection.readEntryAt(0, &discarded, &error)
			&& !inspection.warnings().isEmpty(), "unsupported source rows remain visible and unreadable in a planned browser");
	}
	{
		root.mkdir(QStringLiteral("metadata-source"));
		const QString path = root.filePath(QStringLiteral("metadata-source"));
		ok &= write(QDir(path).filePath(QStringLiteral("entry.txt")), "original checksum");
		PackageArchive original; PackageStagingModel originalPlan;
		ok &= original.load(path, &error) && originalPlan.loadBaseArchive(original, &error);
		PackageWriteRequest output; output.destinationPath = root.filePath(QStringLiteral("metadata.zip"));
		ok &= originalPlan.writeArchive(output).succeeded() && original.load(output.destinationPath, &error)
			&& originalPlan.loadBaseArchive(original, &error);
		if (expect(original.entries().size() == 1 && original.entries().at(0).hasCrc32, "source archive carries checksum metadata")) {
			const int ordinal = static_cast<int>(original.entries().at(0).sourceOrdinal);
			for (const bool generated : {false, true}) {
				auto changed = originalPlan;
				ok &= generated ? changed.addBytes("replacement", QStringLiteral("entry.txt"), &error, PackageStageConflictResolution::ReplaceExisting)
					: changed.replaceOccurrence(ordinal, import, &error);
				const auto view = packagePlannedArchive(changed);
				const auto entry = view.entries().first();
				ok &= expect(entry.sourceOrdinal == ordinal && !entry.hasCrc32 && entry.dataOffset == -1 && entry.compressedSizeBytes == 0
					&& entry.storageMethod == (generated ? QStringLiteral("memory") : QStringLiteral("staged"))
					&& validatePackage(view).valid(), "replacement retains identity but drops original compression, offset and checksum metadata");
			}
		} else { ok = false; }
	}
	PackageStagingModel bounded = derived;
	for (int i = 0; i < 300; ++i) { bounded.addBytes("x", QStringLiteral("entry-%1").arg(i)); }
	int undos = 0; while (bounded.undo()) { ++undos; }
	ok &= expect(undos == 256 && bounded.operations().size() == 44, "bounded delta history retains old plan content");
	{
		auto blocked = derived;
		blocked.renameEntry(QStringLiteral("original.txt"), QStringLiteral("../unsafe.txt"));
		blocked.addFile(root.filePath(QStringLiteral("missing-input")), QStringLiteral("missing.txt"));
		const QString target = root.filePath(QStringLiteral("blocked.vibepackage"));
		ok &= expect(!blocked.summary().canSave && PackageDraft::save(target, &blocked, false, &error), "drafts preserve unresolved operations for later repair", error);
		PackageStagingModel restored;
		ok &= expect(PackageDraft::load(target, &restored, &error) && !restored.summary().canSave && restored.canUndo()
			&& restored.undo() && restored.undo() && restored.summary().canSave, "blocked draft can be repaired through restored undo history", error);
	}
	{
		auto candidate = derived;
		candidate.addBytes("first object", QStringLiteral("first.bin"));
		candidate.addBytes("second object", QStringLiteral("second.bin"));
		const QString target = root.filePath(QStringLiteral("changed-object.vibepackage"));
		const QString firstHash = QString::fromLatin1(QCryptographicHash::hash("first object", QCryptographicHash::Sha256).toHex());
		bool changed = false;
		PackageReadControl mutate;
		mutate.progress = [&](const QString& name, qint64 completed, qint64) {
			if (!changed && name == QStringLiteral("second.bin") && completed == 0) {
				changed = write(QDir(target).filePath(QStringLiteral("objects/") + firstHash), "wrong object");
			}
		};
		ok &= expect(!PackageDraft::save(target, &candidate, false, &error, mutate) && changed
			&& !QFileInfo::exists(QDir(target).filePath(QStringLiteral("document.json"))), "reverify all immutable objects before committing metadata", error);
	}
	{
		root.mkdir(QStringLiteral("inline-source"));
		PackageArchive empty; PackageStagingModel generated;
		ok &= empty.load(root.filePath(QStringLiteral("inline-source")), &error) && generated.loadBaseArchive(empty, &error);
		generated.addBytes(large, QStringLiteral("generated-base.bin"));
		PackageStagingModel generatedBase;
		ok &= generatedBase.loadBaseArchive(PackageStagingArchive(generated), &error);
		qint64 readBytes = 0;
		PackageReadControl stop;
		stop.isCancelled = [&]() { return readBytes >= 65536; };
		stop.progress = [&](const QString& name, qint64 count, qint64) { if (name == QStringLiteral("generated-base.bin")) { readBytes = count; } };
		const QString target = root.filePath(QStringLiteral("inline-cancel.vibepackage"));
		ok &= expect(!PackageDraft::save(target, &generatedBase, false, &error, stop) && readBytes == 65536
			&& !QFileInfo::exists(QDir(target).filePath(QStringLiteral("document.json"))), "generated base snapshots also cancel at chunk boundaries", error);
	}
	ok &= historyCounterBoundaries(root);
	std::cout << (ok ? "Package draft and history checks passed\n" : "Package draft and history checks failed\n");
	return ok ? 0 : 1;
}
