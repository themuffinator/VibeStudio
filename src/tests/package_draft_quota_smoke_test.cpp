#include "core/package_draft.h"
#include "core/package_draft_storage.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* label, const QString& error = {})
{
	if (!value) { std::cerr << label << ": " << error.toStdString() << '\n'; } return value;
}
PackageReadControl limits(qint64 bytes, int files)
{
	PackageReadControl control; auto value = std::make_shared<PackageDraftSaveLimits>();
	value->maximumBytes = bytes; value->maximumFiles = files; control.draftLimits = value; return control;
}
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temporary; if (!temporary.isValid()) { return 1; }
	const QDir root(temporary.path()); const QString destination = root.filePath(QStringLiteral("bounded.vibepackage"));
	PackageStagingModel plan; plan.createEmpty(PackageArchiveFormat::Pk3); QString error; bool ok = true;
	ok &= plan.addBytes("identical", QStringLiteral("one.bin"), &error);
	ok &= plan.addBytes("identical", QStringLiteral("two.bin"), &error);
	const auto revision = plan.revision();
	ok &= expect(!PackageDraft::save(destination, &plan, false, &error, limits(100000, 1))
		&& !QFileInfo::exists(destination) && plan.revision() == revision && plan.isModified(), "file quota preflight preserves destination and live edits", error);
	ok &= expect(!PackageDraft::save(destination, &plan, false, &error, limits(9, 2)) && !QFileInfo::exists(destination),
		"byte quota includes the manifest before creating payloads", error);
	ok &= expect(PackageDraft::save(destination, &plan, false, &error, limits(100000, 2), true)
		&& !QFileInfo::exists(destination), "dry-run quotas deduplicate identical projected objects", error);
	ok &= expect(PackageDraft::save(destination, &plan, false, &error, limits(100000, 2)), "one stored object plus metadata fits two file slots", error);
	const auto storage = inspectPackageStorage(destination); const auto manifest = QDir(destination).filePath(QStringLiteral("document.json"));
	const auto original = read(manifest);
	ok &= expect(storage.safe() && storage.files.size() == 2, "duplicate payloads use one immutable object");
	ok &= expect(PackageDraft::save(destination, &plan, true, &error, limits(0, 1))
		&& inspectPackageStorage(destination).fingerprint == storage.fingerprint, "unchanged save verifies but writes nothing after limits are lowered", error);
	ok &= plan.renameEntry(QStringLiteral("one.bin"), QStringLiteral("renamed.bin"), &error);
	const auto changedRevision = plan.revision();
	ok &= expect(!PackageDraft::save(destination, &plan, true, &error, limits(storage.bytes, 3))
		&& read(manifest) == original && plan.revision() == changedRevision && plan.isModified(), "peak manifest bytes preserve the prior committed document", error);
	ok &= expect(!PackageDraft::save(destination, &plan, true, &error, limits(100000, 2))
		&& inspectPackageStorage(destination).fingerprint == storage.fingerprint, "replacement metadata needs a transient file slot", error);
	ok &= expect(PackageDraft::save(destination, &plan, true, &error, limits(100000, 3)), "increased quota permits verified commit", error);
	const QString interrupted = QDir(destination).filePath(QStringLiteral(".document-Zz9999"));
	{
		QFile partial(interrupted); ok &= partial.open(QIODevice::WriteOnly) && partial.write("unfinished") == 10;
	}
	ok &= plan.renameEntry(QStringLiteral("two.bin"), QStringLiteral("second.bin"), &error);
	const auto withUnused = inspectPackageStorage(destination);
	ok &= expect(!PackageDraft::save(destination, &plan, true, &error, limits(100000, 3))
		&& inspectPackageStorage(destination).fingerprint == withUnused.fingerprint,
		"recognized interrupted writes consume file quota without being silently evicted", error);
	ok &= expect(QFile::remove(interrupted) && PackageDraft::save(destination, &plan, true, &error, limits(100000, 3)),
		"removing a synthetic interrupted write restores manifest headroom", error);
	const QString empty = root.filePath(QStringLiteral("empty.vibepackage")); PackageStagingModel blank; blank.createEmpty(PackageArchiveFormat::Pk3);
	ok &= expect(PackageDraft::save(empty, &blank, false, &error, limits(100000, 1)), "empty document fits one manifest slot", error);
	const QString zero = root.filePath(QStringLiteral("zero.vibepackage")); PackageStagingModel zeroBytes; zeroBytes.createEmpty(PackageArchiveFormat::Pk3);
	ok &= zeroBytes.addBytes({}, QStringLiteral("empty.bin"), &error);
	ok &= expect(!PackageDraft::save(zero, &zeroBytes, false, &error, limits(100000, 1)) && !QFileInfo::exists(zero), "zero-byte payloads still need a file slot", error);
	ok &= expect(PackageDraft::save(zero, &zeroBytes, false, &error, limits(100000, 2)), "zero-byte payload and metadata fit two slots", error);
	std::cout << (ok ? "Package draft quota smoke passed\n" : "Package draft quota smoke failed\n"); return ok ? 0 : 1;
}
