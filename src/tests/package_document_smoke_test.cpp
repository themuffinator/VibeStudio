#include "core/package_draft.h"
#include "core/package_validation.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <iostream>

using namespace vibestudio;

namespace {
bool expect(bool result, const char* message, const QString& error = {})
{
	if (!result) { std::cerr << message << ": " << error.toStdString() << '\n'; }
	return result;
}
QByteArray bytes(const PackageStagingModel& plan, const QString& path)
{
	QByteArray result; QString error; PackageStagingArchive(plan, PackageStagingReadMode::InspectPlan).readEntryBytes(path, &result, &error); return result;
}
bool hasDirectory(const PackageStagingModel& plan, const QString& path)
{
	for (const auto& entry : plan.plannedEntries()) { if (entry.virtualPath == path && entry.kind == PackageEntryKind::Directory) { return true; } }
	return false;
}
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temporary;
	if (!temporary.isValid()) { return 1; }
	QDir root(temporary.path()); QString error; bool ok = true;
	for (const auto format : {PackageArchiveFormat::Pak, PackageArchiveFormat::Zip, PackageArchiveFormat::Pk3, PackageArchiveFormat::Wad}) {
		PackageStagingModel plan;
		ok &= expect(plan.createEmpty(format, {}, &error) && plan.isLoaded() && plan.isModified() && !plan.canUndo()
			&& plan.sourcePath().isEmpty() && plan.summary().canSave && packagePlannedArchive(plan).isOpen(), "new empty document needs no filesystem source", error);
		PackageWriteRequest request; request.destinationPath = root.filePath(QStringLiteral("empty.%1").arg(packageArchiveFormatId(format)));
		ok &= expect(plan.writeArchive(request).succeeded(), "write a valid empty archive", error);
		PackageArchive output;
		ok &= expect(output.load(request.destinationPath, &error) && output.entries().isEmpty() && validatePackage(output).valid(), "reopen and validate empty archive", error);
		const QString draft = request.destinationPath + QStringLiteral(".vibepackage");
		ok &= expect(PackageDraft::save(draft, &plan, false, &error) && !plan.isModified(), "save an empty new document as a draft", error);
		PackageStagingModel reopened;
		ok &= expect(PackageDraft::load(draft, &reopened, &error) && reopened.sourceFormat() == format
			&& reopened.sourcePath().isEmpty() && reopened.summary().canSave && !reopened.isModified(), "empty draft has no invented source path", error);
	}
	for (const auto& magic : {QStringLiteral("PWAD"), QStringLiteral("IWAD"), QStringLiteral("WAD2"), QStringLiteral("WAD3")}) {
		PackageStagingModel plan; ok &= plan.createEmpty(PackageArchiveFormat::Wad, magic, &error);
		ok &= expect(!plan.createDirectory(QStringLiteral("folder"), &error) && plan.operations().isEmpty(), "WAD folders are rejected without changing history");
		PackageWriteRequest request; request.destinationPath = root.filePath(magic + QStringLiteral(".wad"));
		ok &= expect(plan.writeArchive(request).succeeded(), "write selected WAD variant");
		QFile file(request.destinationPath); ok &= file.open(QIODevice::ReadOnly);
		ok &= expect(file.read(4) == magic.toLatin1(), "WAD variant survives an empty document write");
	}
	PackageStagingModel plan;
	ok &= plan.createEmpty(PackageArchiveFormat::Zip, {}, &error);
	ok &= expect(!plan.createEmpty(PackageArchiveFormat::Unknown, {}, &error)
		&& !plan.createEmpty(PackageArchiveFormat::Zip, QStringLiteral("PWAD"), &error)
		&& plan.sourceFormat() == PackageArchiveFormat::Zip, "invalid creation leaves the document unchanged");
	ok &= plan.addBytes("one", QStringLiteral("a/one.txt"));
	const QString firstId = plan.operations().last().id;
	ok &= plan.addBytes("two", QStringLiteral("a/deep/two.txt"));
	ok &= plan.addBytes("other", QStringLiteral("ab/other.txt"));
	ok &= expect(plan.createDirectory(QStringLiteral("a/empty/"), &error), "create a real empty directory with a trailing slash", error);
	const auto initialCount = plan.operations().size();
	ok &= expect(!plan.createDirectory(QStringLiteral("a"), &error) && !plan.createDirectory(QStringLiteral("../outside"), &error)
		&& !plan.createDirectory(QStringLiteral("a/one.txt/child"), &error) && plan.operations().size() == initialCount,
		"folder creation rejects existing, unsafe and file-parent paths without history changes");
	ok &= expect(plan.renameDirectory(QStringLiteral("a"), QStringLiteral("moved"), &error) && plan.summary().canSave
		&& bytes(plan, QStringLiteral("moved/one.txt")) == "one" && bytes(plan, QStringLiteral("moved/deep/two.txt")) == "two"
		&& bytes(plan, QStringLiteral("ab/other.txt")) == "other" && hasDirectory(plan, QStringLiteral("moved/empty")), "rename moves the entire exact subtree and empty directories", error);
	ok &= expect(plan.undo() && bytes(plan, QStringLiteral("a/one.txt")) == "one" && plan.redo()
		&& bytes(plan, QStringLiteral("moved/one.txt")) == "one", "folder rename is one undo step");
	const auto beforeFailedRename = plan.operations().size();
	ok &= expect(!plan.renameDirectory(QStringLiteral("moved"), QStringLiteral("ab"), &error)
		&& !plan.renameDirectory(QStringLiteral("moved"), QStringLiteral("moved/child"), &error)
		&& !plan.renameDirectory(QStringLiteral("moved/deep"), QStringLiteral("moved"), &error)
		&& !plan.renameDirectory(QStringLiteral("moved"), QStringLiteral("../unsafe"), &error)
		&& plan.operations().size() == beforeFailedRename && bytes(plan, QStringLiteral("moved/deep/two.txt")) == "two", "failed tree renames never move a subset");
	ok &= expect(plan.renameDirectory(QStringLiteral("moved"), QStringLiteral("moved"), &error)
		&& plan.operations().size() == beforeFailedRename, "no-op folder rename creates no undo step");
	ok &= expect(plan.deleteDirectory(QStringLiteral("moved"), &error) && plan.summary().stagedFileCount == 1
		&& plan.summary().stagedDirectoryCount == 0 && bytes(plan, QStringLiteral("ab/other.txt")) == "other"
		&& plan.undo() && hasDirectory(plan, QStringLiteral("moved/empty")), "folder deletion and undo retain sibling prefixes and restore empty directories", error);
	const QString draft = root.filePath(QStringLiteral("folders.vibepackage"));
	ok &= expect(PackageDraft::save(draft, &plan, false, &error), "save folder history and redo content", error);
	PackageStagingModel restored;
	ok &= expect(PackageDraft::load(draft, &restored, &error) && restored.summary().canSave && restored.canRedo()
		&& restored.redo() && restored.summary().stagedFileCount == 1 && restored.undo()
		&& bytes(restored, QStringLiteral("moved/deep/two.txt")) == "two", "draft replay preserves whole-tree identity", error);
	PackageWriteRequest zip; zip.destinationPath = root.filePath(QStringLiteral("folders.pk3"));
	ok &= expect(restored.writeArchive(zip).succeeded(), "ZIP family preserves explicit empty folders");
	PackageArchive written; PackageStagingModel reopened;
	ok &= expect(written.load(zip.destinationPath, &error) && reopened.loadBaseArchive(written, &error)
		&& hasDirectory(reopened, QStringLiteral("moved/empty")), "empty folder round trip", error);
	PackageWriteRequest pak; pak.destinationPath = root.filePath(QStringLiteral("folders.pak"));
	ok &= expect(!restored.writeArchive(pak).succeeded() && !QFileInfo::exists(pak.destinationPath), "PAK cannot silently discard an empty folder");
	ok &= restored.deleteDirectory(QStringLiteral("moved/empty"), &error);
	ok &= expect(restored.writeArchive(pak).succeeded(), "PAK preserves populated paths after removing unsupported empty folders");
	auto stale = plan;
	ok &= expect(stale.clearOperation(firstId) && !stale.summary().canSave && bytes(stale, QStringLiteral("a/deep/two.txt")) == "two"
		&& bytes(stale, QStringLiteral("moved/deep/two.txt")).isEmpty(), "changed subtree blocks the entire captured rename on replay");
	PackageStagingModel derived;
	ok &= derived.loadBaseArchive(PackageStagingArchive(plan), &error);
	ok &= derived.renameDirectory(QStringLiteral("moved"), QStringLiteral("derived"), &error);
	const QString derivedDraft = root.filePath(QStringLiteral("derived.vibepackage"));
	ok &= expect(PackageDraft::save(derivedDraft, &derived, false, &error) && PackageDraft::load(derivedDraft, &derived, &error)
		&& derived.summary().canSave && bytes(derived, QStringLiteral("derived/one.txt")) == "one", "rebased generated origins survive directory identity persistence", error);
	{
		PackageStagingModel unicode; unicode.createEmpty(PackageArchiveFormat::Zip);
		unicode.addBytes("sharp-s", QStringLiteral("A\u030a/file.txt"));
		ok &= expect(unicode.renameDirectory(QStringLiteral("å"), QStringLiteral("wide"), &error)
			&& bytes(unicode, QStringLiteral("wide/file.txt")) == "sharp-s", "Unicode normalization cannot truncate a renamed child path", error);
		ok &= expect(unicode.renameDirectory(QStringLiteral("wide"), QStringLiteral("WIDE"), &error)
			&& bytes(unicode, QStringLiteral("WIDE/file.txt")) == "sharp-s", "case-only directory rename preserves bytes", error);
		ok &= expect(unicode.renameDirectory(QStringLiteral("wide"), QStringLiteral("wide"), &error)
			&& unicode.plannedEntries().first().virtualPath == QStringLiteral("wide/file.txt"), "case-insensitive source aliases retain the requested target spelling", error);
		const auto beforeLongRename = unicode.operations().size();
		ok &= expect(!unicode.renameDirectory(QStringLiteral("wide"), QString(4090, QLatin1Char('x')), &error)
			&& unicode.operations().size() == beforeLongRename && bytes(unicode, QStringLiteral("wide/file.txt")) == "sharp-s", "oversized child paths reject the whole folder rename", error);
	}
	{
		PackageStagingModel conflict; conflict.createEmpty(PackageArchiveFormat::Zip);
		conflict.createDirectory(QStringLiteral("folder"));
		conflict.addBytes("file", QStringLiteral("folder"));
		ok &= expect(!conflict.summary().canSave && hasDirectory(conflict, QStringLiteral("folder")), "file import cannot replace a folder");
		conflict.undo(); conflict.addBytes("child", QStringLiteral("implicit/child.txt"));
		conflict.addBytes("parent-file", QStringLiteral("implicit"));
		ok &= expect(!conflict.summary().canSave && bytes(conflict, QStringLiteral("implicit/child.txt")) == "child", "file import cannot occupy an implicit directory");
	}
	{
		PackageStagingModel checked; checked.createEmpty(PackageArchiveFormat::Zip);
		checked.addBytes("inline", QStringLiteral("content.txt"));
		const QString input = root.filePath(QStringLiteral("dry-run-input.txt"));
		QFile file(input); ok &= file.open(QIODevice::WriteOnly); ok &= file.write("redo-content") == 12; file.close();
		ok &= checked.addFile(input, QStringLiteral("redo.txt"), &error);
		const QString retained = checked.operations().last().sourceIdentity->path;
		ok &= checked.undo();
		const QString output = root.filePath(QStringLiteral("dry-run.vibepackage"));
		const auto revision = checked.revision();
		ok &= expect(PackageDraft::save(output, &checked, false, &error, {}, true)
			&& !QFileInfo::exists(output) && checked.draftPath().isEmpty() && checked.revision() == revision
			&& checked.isModified() && checked.canRedo() && checked.operations().first().hasInlineBytes,
			"draft dry run verifies history without writing, freezing or marking the plan saved", error);
		PackageReadControl cancelled; cancelled.isCancelled = []() { return true; };
		ok &= expect(!PackageDraft::save(output, &checked, false, &error, cancelled, true) && !QFileInfo::exists(output), "cancelled draft dry run writes nothing");
		ok &= file.open(QIODevice::WriteOnly | QIODevice::Truncate); ok &= file.write("changed") == 7; file.close();
		ok &= expect(PackageDraft::save(output, &checked, false, &error, {}, true) && !QFileInfo::exists(output)
			&& checked.canRedo(), "dry run retains redo-only content after its original import changes", error);
		QFile damaged(retained); ok &= damaged.open(QIODevice::WriteOnly | QIODevice::Truncate);
		ok &= damaged.write("damaged") == 7; damaged.close();
		ok &= expect(!PackageDraft::save(output, &checked, false, &error, {}, true) && !QFileInfo::exists(output)
			&& checked.canRedo(), "dry run detects damaged retained redo-only content without changing history");
		ok &= expect(QFile::remove(retained), "remove the deliberately damaged temporary test import");
		PackageStagingModel clean; clean.createEmpty(PackageArchiveFormat::Zip); clean.addBytes("good", QStringLiteral("file.txt"));
		ok &= expect(PackageDraft::save(output, &clean, false, &error), "prepare an existing draft", error);
		QFile manifest(QDir(output).filePath(QStringLiteral("document.json"))); ok &= manifest.open(QIODevice::ReadOnly);
		const auto original = manifest.readAll(); manifest.close();
		ok &= expect(!PackageDraft::save(output, &clean, false, &error, {}, true)
			&& PackageDraft::save(output, &clean, true, &error, {}, true), "dry run honors draft overwrite intent", error);
		ok &= manifest.open(QIODevice::ReadOnly);
		ok &= expect(manifest.readAll() == original && !QFileInfo::exists(QDir(output).filePath(QStringLiteral(".write.lock"))), "dry run preserves the saved manifest and creates no lock");
		const QString impossible = input + QStringLiteral("/child.vibepackage");
		ok &= expect(!PackageDraft::save(impossible, &clean, false, &error, {}, true), "dry run rejects a file occupying a destination parent");
	}
	{
		const QString source = root.filePath(QStringLiteral("disk-source"));
		ok &= QDir().mkpath(QDir(source).filePath(QStringLiteral("empty/deep")));
		PackageArchive folder; PackageStagingModel fromFolder;
		ok &= expect(folder.load(source, &error) && fromFolder.loadBaseArchive(folder, &error)
			&& hasDirectory(fromFolder, QStringLiteral("empty/deep")) && folder.verifySourceIdentity(&error), "opening a disk folder retains empty subfolders", error);
		PackageWriteRequest output; output.destinationPath = root.filePath(QStringLiteral("disk-folders.zip"));
		ok &= expect(fromFolder.writeArchive(output).succeeded(), "empty disk folders export to ZIP");
		const auto identity = folder.contentId();
		ok &= QDir().mkpath(QDir(source).filePath(QStringLiteral("added")));
		ok &= expect(!folder.verifySourceIdentity(&error), "new empty disk folder invalidates source membership");
		PackageArchive changed; ok &= changed.load(source, &error);
		ok &= expect(changed.contentId() != identity, "empty folders contribute to folder content identity");
		ok &= QDir(source).rename(QStringLiteral("empty"), QStringLiteral("renamed"));
		ok &= expect(!changed.verifySourceIdentity(&error), "renamed empty disk subtree invalidates the snapshot");
		PackageStagingModel savedFolder;
		ok &= folder.load(source, &error) && savedFolder.loadBaseArchive(folder, &error);
		ok &= expect(savedFolder.renameDirectory(QStringLiteral("renamed"), QStringLiteral("planned"), &error)
			&& QDir(QDir(source).filePath(QStringLiteral("renamed/deep"))).exists(), "staging a folder rename leaves the disk source untouched", error);
		const QString folderDraft = root.filePath(QStringLiteral("disk-folders.vibepackage"));
		ok &= expect(PackageDraft::save(folderDraft, &savedFolder, false, &error)
			&& PackageDraft::load(folderDraft, &savedFolder, &error) && savedFolder.summary().canSave
			&& hasDirectory(savedFolder, QStringLiteral("planned/deep")), "base-directory identities survive draft folder replay", error);
	}
	return ok ? 0 : 1;
}
