#include "package_summary_test_fixture.h"
#include "core/package_draft.h"
#include "core/package_draft_storage.h"
#include "core/package_preview.h"
#include "core/package_recovery.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message, const QString& error = {})
{
	if (!value) { std::cerr << message << ": " << error.toStdString() << '\n'; } return value;
}
QByteArray bytes(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
bool write(const QString& path, const QByteArray& data)
{
	QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir root; if (!root.isValid()) { return 1; }
	QString error; bool ok = true;
	const auto source = root.filePath(QStringLiteral("damaged.zip"));
	// Independent tiny ZIP64 metadata declares a longer payload than stored.
	// The other member is one valid byte. No game content or enormous allocation.
	if (!tests::summaryZip(source, {{"bad.bin", 2}, {"keep.bin", 1}})) { return 1; }
	PackageArchive archive; PackageStagingModel plan;
	if (!expect(archive.load(source, &error) && plan.loadBaseArchive(archive, &error), "load damaged source metadata", error)) { return 1; }
	const auto refused = root.filePath(QStringLiteral("active.vibepackage"));
	ok &= expect(!PackageDraft::save(refused, &plan, false, &error) && !QFileInfo::exists(refused),
		"unreadable current content still refuses draft preflight before creating a destination", error);
	ok &= expect(plan.deleteEntry(QStringLiteral("bad.bin"), &error)
		&& plan.addBytes("retained redo", QStringLiteral("redo.txt"), &error) && plan.undo(), "repair the current plan and retain redo bytes", error);
	const auto revision = plan.revision();
	PackageReadControl cancelled; cancelled.isCancelled = [] { return true; };
	const auto cancelledPath = root.filePath(QStringLiteral("cancelled.vibepackage"));
	ok &= expect(!PackageDraft::save(cancelledPath, &plan, false, &error, cancelled) && !QFileInfo::exists(cancelledPath)
		&& plan.revision() == revision && plan.canRedo(), "cancellation is not converted into missing history", error);
	PackageDraftSaveLimits small; small.maximumBytes = 1;
	PackageReadControl limited; limited.draftLimits = std::make_shared<PackageDraftSaveLimits>(small);
	const auto limitedPath = root.filePath(QStringLiteral("limited.vibepackage"));
	ok &= expect(!PackageDraft::save(limitedPath, &plan, false, &error, limited) && !QFileInfo::exists(limitedPath)
		&& plan.revision() == revision && plan.canRedo(), "storage refusal preserves repair/history and creates no draft", error);
	const auto draft = root.filePath(QStringLiteral("repaired.vibepackage"));
	if (!expect(PackageDraft::save(draft, &plan, false, &error, {}, true) && !QFileInfo::exists(draft),
		"repaired draft preflight tolerates unavailable original history without writing", error)) { return 1; }
	if (!expect(PackageDraft::save(draft, &plan, false, &error), "save repaired current content and explicitly unavailable original history", error)) { return 1; }
	ok &= expect(plan.revision() == revision && plan.canUndo() && plan.canRedo() && !plan.isModified(), "draft publication preserves revision and both history directions");
	const auto manifestPath = QDir(draft).filePath(QStringLiteral("document.json"));
	const auto manifestBytes = bytes(manifestPath);
	const auto manifest = QJsonDocument::fromJson(manifestBytes).object();
	QJsonObject missing;
	for (const auto value : manifest.value("base").toArray()) {
		if (value.toObject().value("path") == QStringLiteral("bad.bin")) { missing = value.toObject(); }
	}
	ok &= expect(manifest.value("version").toInt() == 4 && !missing.value("unavailable").toString().isEmpty()
		&& !missing.contains("object"), "versioned draft records missing historical bytes without a fabricated object");
	ok &= expect(QFile::remove(source), "remove only the owned original fixture");
	PackageStagingModel restored;
	if (!expect(PackageDraft::load(draft, &restored, &error), "reopen the independent repaired draft without its original", error)) { return 1; }
	ok &= expect(restored.summary().canSave && restored.summary().conflictCount > 0 && restored.canRedo(), "historical data loss is a nonblocking visible warning for the repaired plan");
	ok &= expect(restored.undo() && !restored.summary().canSave, "Undo restores unavailable metadata and blocks archive publication");
	const auto inspect = packagePlannedArchive(restored, &error);
	qsizetype bad = -1;
	for (qsizetype i = 0; i < inspect.entries().size(); ++i) { if (inspect.entries().at(i).virtualPath == QStringLiteral("bad.bin")) { bad = i; } }
	ok &= expect(inspect.isOpen() && bad >= 0 && !inspect.entries().at(bad).readable,
		"an unavailable historical occurrence remains browsable and is marked unreadable", error);
	if (bad >= 0) {
		const auto preview = buildPackageEntryPreviewAt(inspect, bad);
		ok &= expect(!preview.error.isEmpty() && preview.bytesRead == 0, "preview cannot invent restored historical bytes");
	}
	PackageWriteRequest request; request.format = PackageArchiveFormat::Zip; request.destinationPath = root.filePath(QStringLiteral("blocked.zip"));
	ok &= expect(!restored.writeArchive(request).succeeded() && !QFileInfo::exists(request.destinationPath), "unavailable current content cannot publish an archive");
	const auto undoneDraft = root.filePath(QStringLiteral("undone.vibepackage"));
	ok &= expect(PackageDraft::save(undoneDraft, &restored, false, &error), "already explicit missing content remains checkpointable during Undo", error);
	ok &= expect(restored.redo() && restored.redo() && restored.summary().canSave, "Redo restores repaired content and separately retained generated bytes");
	request.destinationPath = root.filePath(QStringLiteral("repaired.zip"));
	ok &= expect(restored.writeArchive(request).succeeded(), "the repaired plan writes without the damaged original");
	PackageArchive exported; QByteArray payload;
	ok &= expect(exported.load(request.destinationPath, &error) && exported.readEntryBytes(QStringLiteral("redo.txt"), &payload, &error)
		&& payload == "retained redo", "export preserves recovered generated payloads", error);
	const auto store = root.filePath(QStringLiteral("recovery"));
	const QString id = QStringLiteral("11111111-2222-4333-8444-555555555555");
	auto session = PackageRecoverySession::acquire(store, id, &error);
	if (!expect(bool(session), "acquire owned recovery store", error)) { return 1; }
	const auto checkpoint = session->checkpoint(restored, QStringLiteral("Repaired damaged package"));
	ok &= expect(checkpoint.succeeded(), "automatic recovery preserves the same explicit historical limitation", checkpoint.error);
	if (checkpoint.succeeded()) {
		const auto inventory = listPackageRecoveries(store);
		const auto records = packageRecoveryInventoryJson(inventory).value("records").toArray();
		ok &= expect(!records.isEmpty() && records.first().toObject().value("unavailableBaseCount").toInt() == 1,
			"recovery inventory exposes unavailable historical content before restoration");
		PackageStagingModel recovered;
		ok &= expect(restorePackageRecovery(store, id, checkpoint.manifestSha256, root.filePath(QStringLiteral("recovered.vibepackage")), &recovered, &error)
			&& recovered.canUndo() && recovered.summary().canSave, "recovery restore independently preserves the repair and history", error);
	}
	// A malformed declaration must not replace an already open repaired plan.
	for (const int mutation : {0, 1, 2, 3}) {
		auto changed = manifest; auto entries = changed.value("base").toArray();
		for (qsizetype i = 0; i < entries.size(); ++i) {
			auto entry = entries.at(i).toObject(); if (entry.value("path") != QStringLiteral("bad.bin")) { continue; }
			if (mutation == 0) { entry.insert("object", QString(64, '0')); }
			if (mutation == 1) { changed.insert("version", 2); }
			if (mutation == 2) { entry.insert("unavailable", QString()); }
			if (mutation == 3) { entry.insert("unavailable", QString(4097, 'x')); }
			entries[i] = entry;
		}
		changed.insert("base", entries); const auto before = restored.revision();
		ok &= write(manifestPath, QJsonDocument(changed).toJson());
		ok &= expect(!PackageDraft::load(draft, &restored, &error) && restored.revision() == before && restored.summary().canSave,
			"invalid unavailable-content records cannot mutate an open document", error);
	}
	ok &= write(manifestPath, manifestBytes);
	// Existing verified draft objects remain strict even when no longer current.
	const auto files = QDir(QDir(draft).filePath(QStringLiteral("objects"))).entryList(QDir::Files);
	if (!files.isEmpty()) {
		ok &= write(QDir(draft).filePath(QStringLiteral("objects/") + files.first()), "corrupt");
		ok &= expect(!PackageDraft::load(draft, &restored, &error), "corrupt referenced objects are never downgraded to missing-history markers", error);
	}
	for (const quint64 size : {quint64(2), std::numeric_limits<quint64>::max()}) {
		const auto name = QString::number(size);
		const auto input = root.filePath(name + QStringLiteral(".zip"));
		ok &= tests::summaryZip(input, {{"same.bin", size}, {"same.bin", 1}});
		PackageArchive repeated; PackageStagingModel repair;
		ok &= expect(repeated.load(input, &error) && repair.loadBaseArchive(repeated, &error)
			&& repair.deleteOccurrence(0, &error), "repair one exact duplicate occurrence", error);
		const auto output = root.filePath(name + QStringLiteral(".vibepackage"));
		ok &= expect(PackageDraft::save(output, &repair, false, &error), "duplicate identity and unsigned declared sizes survive repaired drafts", error);
		PackageStagingModel reopened;
		ok &= expect(PackageDraft::load(output, &reopened, &error) && reopened.summary().canSave
			&& reopened.plannedEntries().size() == 1 && reopened.entryBytes(reopened.plannedEntries().first(), &payload, &error)
			&& payload == "x", "the readable duplicate remains distinct from unavailable historical metadata", error);
		ok &= expect(reopened.undo() && !reopened.summary().canSave && reopened.redo() && reopened.summary().canSave,
			"duplicate Undo/Redo preserves the correct unavailable occurrence");
	}
	// Replacing an unreadable original also preserves its identity in history.
	const auto replaceSource = root.filePath(QStringLiteral("replace.zip"));
	const auto replacement = root.filePath(QStringLiteral("replacement.bin"));
	ok &= tests::summaryZip(replaceSource, {{"bad.bin", 2}}) && write(replacement, "replacement bytes");
	PackageArchive replacementArchive; PackageStagingModel replaced;
	ok &= expect(replacementArchive.load(replaceSource, &error) && replaced.loadBaseArchive(replacementArchive, &error)
		&& replaced.replaceOccurrence(0, replacement, &error), "replace the exact unreadable original", error);
	const auto replacementDraft = root.filePath(QStringLiteral("replacement.vibepackage"));
	ok &= expect(PackageDraft::save(replacementDraft, &replaced, false, &error), "save replacement with unavailable original history", error);
	PackageStagingModel reopenedReplacement;
	ok &= expect(PackageDraft::load(replacementDraft, &reopenedReplacement, &error)
		&& reopenedReplacement.entryBytes(reopenedReplacement.plannedEntries().first(), &payload, &error)
		&& payload == "replacement bytes" && reopenedReplacement.undo() && !reopenedReplacement.summary().canSave
		&& reopenedReplacement.redo() && reopenedReplacement.summary().canSave, "replacement bytes and missing original Undo remain distinct", error);
	// Source mutation and cancellation during the failing original read must not
	// become an accepted missing-content declaration, even with no current files.
	for (const bool mutateSource : {false, true}) {
		const auto label = mutateSource ? QStringLiteral("source-race") : QStringLiteral("read-cancel");
		const auto input = root.filePath(label + QStringLiteral(".zip"));
		ok &= tests::summaryZip(input, {{"bad.bin", 2}});
		PackageArchive original; PackageStagingModel emptyRepair;
		ok &= expect(original.load(input, &error) && emptyRepair.loadBaseArchive(original, &error)
			&& emptyRepair.deleteEntry(QStringLiteral("bad.bin"), &error), "prepare a deleted damaged original for strict read failure", error);
		const auto oldRevision = emptyRepair.revision(); bool reachedRead = false, stop = false, sourceChanged = false;
		const auto originalBytes = bytes(input); PackageReadControl failure;
		failure.isCancelled = [&] { return stop; };
		failure.progress = [&](const QString& name, qint64, qint64) {
			if (name != QStringLiteral("bad.bin") || reachedRead) { return; }
			reachedRead = true;
			if (mutateSource) { sourceChanged = write(input, originalBytes + "changed"); } else { stop = true; }
		};
		const auto output = root.filePath(label + QStringLiteral(".vibepackage"));
		ok &= expect(!PackageDraft::save(output, &emptyRepair, false, &error, failure) && reachedRead
			&& (!mutateSource || sourceChanged) && !QFileInfo::exists(output) && emptyRepair.revision() == oldRevision,
			"a failed original read cannot hide source changes or mid-read cancellation", error);
	}
	return ok ? 0 : 1;
}
