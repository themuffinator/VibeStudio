#include "core/package_draft.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <iostream>

using namespace vibestudio;

namespace {
bool expect(bool condition, const char* message)
{
	if (!condition) { std::cerr << message << '\n'; }
	return condition;
}
bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString& path)
{
	QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
QByteArray entry(const PackageStagingModel& model, const QString& path)
{
	QByteArray bytes; QString error;
	if (!PackageStagingArchive(model).readEntryBytes(path, &bytes, &error)) { std::cerr << error.toStdString() << '\n'; }
	return bytes;
}
PackageStagingModel bounded(qsizetype records, qint64 bytes = PackageStagingMetadataLimits::metadataCeiling)
{
	return PackageStagingModel({}, PackageStagingMetadataLimits{records, bytes});
}
bool history()
{
	bool ok = true; QString error; PackageStagingMetadataUsage usage;
	auto model = bounded(3);
	ok &= expect(model.createEmpty(PackageArchiveFormat::Zip) && model.addBytes("a", "a", &error)
		&& model.metadataUsage(&usage, &error) && usage.records == 3, "One edit reserves its active slot, delta and history step.");
	const auto revision = model.revision();
	ok &= expect(!model.addBytes("b", "b", &error) && !error.isEmpty() && model.revision() == revision
		&& !model.clearOperation(model.operations().first().id, &error) && model.revision() == revision && entry(model, "a") == "a",
		"Admission failure preserves edits, revision and unstage history.");
	ok &= expect(model.undo() && model.metadataUsage(&usage, &error) && usage.records == 3
		&& model.redo() && model.metadataUsage(&usage, &error) && usage.records == 3, "Undo and redo use reserved active slots at the record boundary.");
	const auto report = QJsonDocument::fromJson(model.manifestJson(&error)).object().value("summary").toObject().value("retainedMetadata").toObject();
	ok &= expect(report.value("records").toString() == "3" && report.value("maximumRecords").toString() == "3", "Manifest metadata diagnostics expose the document policy.");

	auto unstaged = bounded(5);
	ok &= expect(unstaged.createEmpty(PackageArchiveFormat::Zip) && unstaged.addBytes("x", "x")
		&& unstaged.clearOperation(unstaged.operations().first().id, &error) && unstaged.metadataUsage(&usage, &error) && usage.records == 5,
		"Unstage reserves the hidden operation and both history deltas.");
	ok &= expect(unstaged.undo() && entry(unstaged, "x") == "x" && unstaged.metadataUsage(&usage, &error) && usage.records == 5
		&& unstaged.redo() && unstaged.metadataUsage(&usage, &error) && usage.records == 5, "Unstage undo/redo never needs additional record allowance.");

	auto group = bounded(6);
	ok &= expect(group.createEmpty(PackageArchiveFormat::Zip) && group.addBytes("a", "before") && group.undo()
		&& group.beginOperationGroup("Group", &error) && group.addBytes("b", "inside") && !group.addBytes("c", "refused", &error)
		&& group.metadataUsage(&usage, &error) && usage.records == 6, "Open groups retain prior redo and reserve their own edits.");
	group.endOperationGroup(false);
	ok &= expect(group.canRedo() && group.metadataUsage(&usage, &error) && usage.records == 3 && group.redo(), "Group cancellation restores redo and releases new metadata.");
	ok &= expect(group.undo() && group.beginOperationGroup("Committed", &error) && group.addBytes("b", "inside"), "Prepare metadata group commit.");
	group.endOperationGroup();
	ok &= expect(!group.canRedo() && group.metadataUsage(&usage, &error) && usage.records == 3 && group.addBytes("c", "after", &error), "Group commit releases old redo before later admission.");

	auto evicted = bounded(768);
	ok &= expect(evicted.createEmpty(PackageArchiveFormat::Zip) && evicted.addBytes({}, "gone") && evicted.clearOperation(evicted.operations().first().id), "Prepare history-only metadata.");
	for (int index = 0; index < 256; ++index) {
		if (!evicted.addBytes({}, QStringLiteral("empty%1").arg(index), &error)) { std::cerr << error.toStdString() << '\n'; return false; }
	}
	ok &= expect(evicted.metadataUsage(&usage, &error) && usage.records == 768 && evicted.operations().size() == 256
		&& !evicted.addBytes({}, "extra", &error), "Evicted history releases unreachable operations but keeps every active slot charged.");
	return ok;
}
bool textAndOwnership()
{
	bool ok = true; QString error; PackageStagingMetadataUsage usage;
	PackageStagingModel reference;
	ok &= expect(reference.createEmpty(PackageArchiveFormat::Zip) && reference.addBytes("a", "a") && reference.metadataUsage(&usage, &error), "Measure the supported edit's logical text boundary.");
	const auto exactBytes = usage.metadataBytes;
	auto model = bounded(PackageStagingMetadataLimits::recordCeiling, exactBytes);
	ok &= expect(model.createEmpty(PackageArchiveFormat::Zip) && model.addBytes("a", "a") && model.undo(), "Prepare an exact text budget and redo.");
	const auto revision = model.revision();
	ok &= expect(!model.addBytes("b", "longer", &error) && model.canRedo() && model.revision() == revision,
		"A rejected larger branch keeps the original redo and identifiers.");
	ok &= expect(model.addBytes("b", "b", &error) && model.operations().first().id == "stage-2"
		&& model.metadataUsage(&usage, &error) && usage.metadataBytes == exactBytes, "A fitting branch reuses discarded redo without consuming an id on failure.");
	ok &= expect(!model.beginOperationGroup("too much label", &error) && model.canUndo(), "A refused group does not leave history locked in an open group.");
	PackageReadControl cancelled; cancelled.isCancelled = [] { return true; };
	ok &= expect(!model.metadataUsage(&usage, &error, cancelled) && !error.isEmpty(), "Metadata cancellation remains effective with a warm cache.");

	QChar path[] = {QChar('r'), QChar('a'), QChar('w')};
	QChar label[] = {QChar('G'), QChar('r'), QChar('o'), QChar('u'), QChar('p')};
	PackageStagingModel owned;
	ok &= expect(owned.createEmpty(PackageArchiveFormat::Zip) && owned.beginOperationGroup(QString::fromRawData(label, 5), &error)
		&& owned.addBytes("x", QString::fromRawData(path, 3), &error), "Stage caller-owned path and group-label views.");
	path[0] = QChar('z'); label[0] = QChar('X'); owned.endOperationGroup();
	ok &= expect(owned.undoLabel() == "Group" && entry(owned, "raw") == "x", "Accepted paths and group labels own their text independently of caller buffers.");

	auto empty = bounded(PackageStagingMetadataLimits::recordCeiling, 0);
	ok &= expect(empty.createEmpty(PackageArchiveFormat::Zip) && !empty.createEmpty(PackageArchiveFormat::Wad, {}, &error)
		&& empty.sourceFormat() == PackageArchiveFormat::Zip, "A failed new-document metadata transition preserves the existing format.");
	PackageStagingModel nested;
	ok &= expect(nested.createEmpty(PackageArchiveFormat::Zip), "Prepare nested groups.");
	for (int depth = 0; depth < 256; ++depth) { ok &= nested.beginOperationGroup("Nested", &error); }
	ok &= expect(!nested.beginOperationGroup("Overflow", &error), "Group nesting is bounded before its counter can overflow.");
	nested.endOperationGroup(false);
	ok &= expect(nested.metadataUsage(&usage, &error) && usage.records == 0 && nested.beginOperationGroup("Again", &error), "Cancelling nested groups leaves the model reusable.");
	nested.endOperationGroup(false);
	return ok;
}
bool sourcesAndDrafts(const QDir& root)
{
	bool ok = true; QString error; PackageStagingMetadataUsage usage;
	const auto input = root.filePath("source.bin");
	ok &= expect(write(input, "payload"), "Create a metadata admission source.");
	auto rejected = bounded(3); int reads = 0;
	PackageReadControl control; control.progress = [&](const QString&, qint64, qint64) { ++reads; };
	ok &= expect(rejected.createEmpty(PackageArchiveFormat::Zip) && !rejected.addFile(input, "source", &error, PackageStageConflictResolution::Block, control, PackageFileImportMode::VerifyOnly)
		&& reads == 0 && rejected.operations().isEmpty(), "Known source identity record limits are checked before hashing or copying.");
	auto tooSmall = bounded(1);
	PackageStagingModel source;
	ok &= expect(source.createEmpty(PackageArchiveFormat::Zip) && source.addBytes("a", "a") && source.addBytes("b", "b")
		&& tooSmall.createEmpty(PackageArchiveFormat::Pak) && !tooSmall.loadBaseArchive(PackageStagingArchive(source), &error)
		&& tooSmall.sourceFormat() == PackageArchiveFormat::Pak && tooSmall.operations().isEmpty(), "Base snapshot adoption obeys the destination metadata policy atomically.");

	const auto refusedPath = root.filePath("refused.vibepackage");
	auto insufficient = bounded(4);
	ok &= expect(insufficient.createEmpty(PackageArchiveFormat::Zip) && insufficient.addBytes("x", "x"), "Prepare a draft whose manifest identity cannot fit.");
	const auto revision = insufficient.revision();
	ok &= expect(!PackageDraft::save(refusedPath, &insufficient, false, &error, {}, true)
		&& !PackageDraft::save(refusedPath, &insufficient, false, &error) && !QFileInfo::exists(refusedPath)
		&& insufficient.revision() == revision && entry(insufficient, "x") == "x", "Dry and real draft saves include the destination identity before any output is created.");

	const auto draft = root.filePath("metadata.vibepackage");
	auto saved = bounded(5);
	ok &= expect(saved.createEmpty(PackageArchiveFormat::Zip) && saved.addBytes("x", "x") && PackageDraft::save(draft, &saved, false, &error)
		&& saved.metadataUsage(&usage, &error) && usage.records == 5, "Draft ownership transition fits exactly with its payload and manifest identities.");
	auto loaded = bounded(5);
	ok &= expect(PackageDraft::load(draft, &loaded, &error) && loaded.metadataUsage(&usage, &error) && usage.records == 5 && entry(loaded, "x") == "x", "A boundary draft reloads under the same record policy.");
	auto preserved = bounded(4);
	ok &= expect(preserved.createEmpty(PackageArchiveFormat::Zip) && preserved.addBytes("k", "keep"), "Prepare a lower-limit draft destination.");
	int objectReads = 0;
	control.progress = [&](const QString& path, qint64, qint64) { if (path.contains("/objects/")) { ++objectReads; } };
	const auto beforeLoad = preserved.revision();
	const auto manifestPath = QDir(draft).filePath("document.json"), manifest = QString::fromUtf8(read(manifestPath));
	ok &= expect(saved.metadataUsage(&usage, &error), "Measure a committed draft's text metadata.");
	const auto exactBytes = usage.metadataBytes;
	auto exact = bounded(PackageStagingMetadataLimits::recordCeiling, exactBytes);
	ok &= expect(PackageDraft::load(draft, &exact, &error) && exact.metadataUsage(&usage, &error) && usage.metadataBytes == exactBytes,
		"A saved draft reloads at its exact logical text boundary.");
	auto shortText = bounded(PackageStagingMetadataLimits::recordCeiling, exactBytes - 1);
	ok &= expect(shortText.createEmpty(PackageArchiveFormat::Zip) && shortText.addBytes("x", "x"), "Prepare a text-limited draft conversion.");
	const auto shortPath = root.filePath("metadatb.vibepackage"); // Same path length as the measured draft.
	int preflightReads = 0, protectionSteps = 0; PackageReadControl preflightControl;
	preflightControl.progress = [&](const QString& phase, qint64, qint64) {
		if (phase == "Retaining package source protections") { ++protectionSteps; } else { ++preflightReads; }
	};
	ok &= expect(!PackageDraft::save(shortPath, &shortText, false, &error, preflightControl, true)
		&& !PackageDraft::save(shortPath, &shortText, false, &error, preflightControl) && preflightReads == 0 && protectionSteps > 0 && !QFileInfo::exists(shortPath),
		"Known destination metadata refusal precedes all payload reads, hashing and directory creation.");
	ok &= expect(!PackageDraft::load(draft, &shortText, &error, control) && objectReads == 0 && entry(shortText, "x") == "x",
		"A one-byte metadata shortfall refuses draft hydration without adopting a partial document.");
	ok &= expect(!PackageDraft::load(draft, &preserved, &error, control) && objectReads == 0 && preserved.revision() == beforeLoad
		&& entry(preserved, "keep") == "k" && QString::fromUtf8(read(manifestPath)) == manifest, "Draft metadata refusal precedes payload reads and preserves both documents.");

	// The writer never emits unreachable operations; refuse crafted registries
	// before an otherwise unused object's content can be read.
	auto document = QJsonDocument::fromJson(manifest.toUtf8()).object(); auto operations = document.value("operations").toArray();
	auto extra = operations.first().toObject(); extra.insert("id", "stage-2"); operations.append(extra);
	document.insert("operations", operations); document.insert("operationSerial", "2");
	ok &= expect(write(manifestPath, QJsonDocument(document).toJson()) && !PackageDraft::load(draft, &preserved, &error, control)
		&& objectReads == 0 && preserved.revision() == beforeLoad, "Unreachable operation metadata cannot trigger payload reads or partial adoption.");
	return ok;
}
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temporary;
	if (!temporary.isValid()) { return 1; }
	bool ok = history(); ok &= textAndOwnership(); ok &= sourcesAndDrafts(QDir(temporary.path()));
	for (const auto limits : {PackageStagingMetadataLimits{-1, 0}, PackageStagingMetadataLimits{0, -1},
		PackageStagingMetadataLimits{PackageStagingMetadataLimits::recordCeiling + 1, 0},
		PackageStagingMetadataLimits{0, PackageStagingMetadataLimits::metadataCeiling + 1}}) {
		PackageStagingModel model({}, limits); QString error;
		ok &= expect(!model.createEmpty(PackageArchiveFormat::Zip, {}, &error) && !error.isEmpty() && !model.isLoaded(), "Invalid policies cannot raise metadata ceilings or create a partial document.");
	}
	return ok ? 0 : 1;
}
