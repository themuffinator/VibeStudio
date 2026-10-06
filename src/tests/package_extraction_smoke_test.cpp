#include "core/package_archive.h"
#include "core/package_staging.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include <filesystem>
#include <iostream>

using namespace vibestudio;

namespace {
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; }
	return value;
}
bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// A streaming-only reader exposes physical positions, bounded chunks and a
// failure after the last chunk without building payload-sized test buffers.
class StreamFixture final : public PackageArchiveReader {
public:
	QVector<PackageEntry> members;
	QString source;
	bool failAfterPayload = false;
	mutable int streams = 0;
	PackageArchiveFormat format() const override { return PackageArchiveFormat::Pak; }
	QString sourcePath() const override { return source; }
	bool isOpen() const override { return true; }
	QVector<PackageEntry> entries() const override { return members; }
	bool readEntryBytes(const QString&, QByteArray*, QString*, qint64) const override { return false; }
	bool streamEntryAt(qsizetype index, const std::function<bool(QByteArrayView)>& sink, QString* error,
		const std::function<bool()>& cancelled) const override
	{
		++streams;
		quint64 remaining = members.at(index).sizeBytes;
		const QByteArray block(65536, static_cast<char>('a' + index));
		while (remaining) {
			if (cancelled && cancelled()) { return false; }
			const auto count = static_cast<qsizetype>(qMin<quint64>(remaining, block.size()));
			if (!sink(QByteArrayView(block).first(count))) { return false; }
			remaining -= static_cast<quint64>(count);
		}
		if (failAfterPayload) { if (error) { *error = QStringLiteral("fixture checksum failure"); } return false; }
		return true;
	}
	void setNames(const QStringList& names, quint64 size = 200003)
	{
		members.clear(); streams = 0;
		for (const auto& name : names) { PackageEntry entry; entry.virtualPath = name; entry.sizeBytes = size; entry.sourceOrdinal = members.size(); members << entry; }
	}
};
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) { return 1; }
	QDir root(temp.path());
	bool ok = true;
	StreamFixture source;
	source.source = root.filePath(QStringLiteral("source.pak"));
	source.setNames({QStringLiteral("z.txt"), QStringLiteral("a.txt")});
	PackageExtractionRequest request; request.extractAll = true; request.targetDirectory = root.filePath(QStringLiteral("streamed"));
	const auto streamed = extractPackageEntries(source, request);
	ok &= expect(streamed.succeeded() && streamed.writtenCount == 2 && streamed.bytesRead == 400006 && source.streams == 2,
		"extraction must use bounded positional streaming without buffered reads");
	ok &= expect(read(QDir(request.targetDirectory).filePath(QStringLiteral("a.txt"))) == QByteArray(200003, 'b')
		&& read(QDir(request.targetDirectory).filePath(QStringLiteral("z.txt"))) == QByteArray(200003, 'a'),
		"path sorting must preserve the physical entry indexes used for reads");
	ok &= expect(streamed.entries.first().entryIndex == 1 && streamed.entries.first().sourceOrdinal == 1, "reports expose the source occurrence");
	const auto skipped = extractPackageEntries(source, request);
	ok &= expect(skipped.succeeded() && skipped.skippedCount == 2 && skipped.bytesRead == 0, "existing files skip without reading payloads");

	int collisionCase = 0;
	for (const QStringList& names : {
		QStringList {QStringLiteral("THINGS"), QStringLiteral("THINGS")},
		QStringList {QStringLiteral("Upper.txt"), QStringLiteral("upper.txt")},
		QStringList {QString::fromUtf8("caf\xc3\xa9.txt"), QString::fromUtf8("cafe\xcc\x81.txt")},
		QStringList {QStringLiteral("same"), QStringLiteral("same/child.txt")},
		QStringList {QStringLiteral("okay.txt"), QStringLiteral("../escaped.txt")}}) {
		source.setNames(names);
		request.targetDirectory = root.filePath(QStringLiteral("collision-%1").arg(++collisionCase));
		request.overwriteExisting = true;
		const auto blocked = extractPackageEntries(source, request);
		ok &= expect(!blocked.succeeded() && blocked.errorCount > 0 && source.streams == 0 && !QFileInfo::exists(request.targetDirectory),
			"namespace conflicts and unsafe paths must fail before creating any output, even with overwrite");
		request.extractAll = false; request.virtualPaths = {names.front()};
		if (collisionCase <= 3) {
			const auto selected = extractPackageEntries(source, request);
			ok &= expect(!selected.succeeded() && selected.errorCount == 2 && selected.entries[0].entryIndex != selected.entries[1].entryIndex,
				"name selection must preserve both duplicate records and report their collision");
		}
		request.extractAll = true; request.virtualPaths.clear();
	}
	{
		source.setNames({QStringLiteral("THINGS"), QStringLiteral("THINGS")}, 4);
		PackageExtractionRequest exact; exact.targetDirectory = root.filePath(QStringLiteral("mapped"));
		exact.entrySelections = {{0, QStringLiteral("map-one/THINGS")}, {1, QStringLiteral("map-two/THINGS")}};
		exact.dryRun = true;
		const auto dry = extractPackageEntries(source, exact);
		ok &= expect(dry.succeeded() && dry.entries.size() == 2 && source.streams == 0 && !QFileInfo::exists(exact.targetDirectory),
			"mapped occurrence dry run validates without reading or writing payloads");
		exact.dryRun = false;
		const auto mapped = extractPackageEntries(source, exact);
		ok &= expect(mapped.succeeded() && mapped.writtenCount == 2
			&& read(QDir(exact.targetDirectory).filePath(QStringLiteral("map-one/THINGS"))) == "aaaa"
			&& read(QDir(exact.targetDirectory).filePath(QStringLiteral("map-two/THINGS"))) == "bbbb",
			"mapped duplicate entries stream their own occurrence bytes to separate destinations");
		for (const QString& unsafe : {QStringLiteral("../escape"), QStringLiteral("map-two/THINGS"), QStringLiteral("map-two")}) {
			exact.targetDirectory = root.filePath(QStringLiteral("mapped-refused"));
			exact.entrySelections[0].outputVirtualPath = unsafe;
			source.streams = 0;
			const auto refused = extractPackageEntries(source, exact);
			ok &= expect(!refused.succeeded() && source.streams == 0 && !QFileInfo::exists(exact.targetDirectory),
				"mapped traversal, collisions and file/directory conflicts fail before any output");
		}
		exact.entrySelections = {{1, {}}};
		exact.targetDirectory = root.filePath(QStringLiteral("one-occurrence"));
		ok &= expect(extractPackageEntries(source, exact).succeeded()
			&& read(QDir(exact.targetDirectory).filePath(QStringLiteral("THINGS"))) == "bbbb", "one exact occurrence keeps its original output name");
		exact.targetDirectory = root.filePath(QStringLiteral("invalid-occurrence"));
		for (const QVector<PackageExtractionSelection>& selection : {
			QVector<PackageExtractionSelection>{{-1, {}}}, QVector<PackageExtractionSelection>{{2, {}}},
			QVector<PackageExtractionSelection>{{1, QStringLiteral("one")}, {1, QStringLiteral("two")}}}) {
			exact.entrySelections = selection;
			ok &= expect(!extractPackageEntries(source, exact).succeeded() && !QFileInfo::exists(exact.targetDirectory),
				"invalid and repeated extraction indexes fail closed");
		}
	}
	source.setNames({QStringLiteral("source.pak")}); request.targetDirectory = root.path();
	ok &= expect(write(source.source, "source bytes"), "create source overwrite fixture");
	const auto protectedSource = extractPackageEntries(source, request);
	ok &= expect(!protectedSource.succeeded() && read(source.source) == "source bytes", "overwrite must never replace an input archive");

	source.setNames({QStringLiteral("data.bin")}); request.targetDirectory = root.filePath(QStringLiteral("cancel"));
	root.mkpath(QStringLiteral("cancel"));
	const QString output = QDir(request.targetDirectory).filePath(QStringLiteral("data.bin"));
	ok &= expect(write(output, "original"), "create existing output");
	bool cancel = false;
	request.control.isCancelled = [&]() { return cancel; };
	request.control.progress = [&](const QString& path, qint64 bytes, qint64) { if (path == QStringLiteral("data.bin") && bytes >= 65536) { cancel = true; } };
	const auto stopped = extractPackageEntries(source, request);
	ok &= expect(stopped.cancelled && stopped.writtenCount == 0 && stopped.bytesRead == 65536 && read(output) == "original",
		"within-file cancellation must discard partial output and retain the original");
	request.control = {};
	source.failAfterPayload = true;
	const auto corrupt = extractPackageEntries(source, request);
	ok &= expect(!corrupt.succeeded() && corrupt.bytesRead == 200003 && corrupt.writtenCount == 0 && read(output) == "original",
		"checksum failure after the last payload chunk must not commit a replacement");
	source.failAfterPayload = false;
	request.control.progress = [&](const QString& path, qint64 bytes, qint64) { if (path == QStringLiteral("data.bin") && bytes == 200003) { ok &= write(output, "user edit"); } };
	const auto changed = extractPackageEntries(source, request);
	ok &= expect(!changed.succeeded() && changed.writtenCount == 0 && read(output) == "user edit", "a late edit to existing output must be preserved");
	request.control = {};
	const auto replaced = extractPackageEntries(source, request);
	ok &= expect(replaced.succeeded() && read(output) == QByteArray(200003, 'a'), "explicit overwrite must commit a verified complete file");
#ifdef Q_OS_WIN
	// Force the real atomic replacement to fail after payload verification.
	// The held handle permits reads/writes but denies renaming this destination.
	ok &= expect(write(output, "locked original"), "prepare locked output");
	const HANDLE held = CreateFileW(reinterpret_cast<const wchar_t*>(output.utf16()), GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	ok &= expect(held != INVALID_HANDLE_VALUE, "hold extraction destination without delete sharing");
	if (held != INVALID_HANDLE_VALUE) {
		const auto refused = extractPackageEntries(source, request);
		CloseHandle(held);
		ok &= expect(!refused.succeeded() && refused.writtenCount == 0 && refused.bytesRead == 200003
			&& read(output) == "locked original", "native commit failure must preserve the existing extraction destination");
	}
#endif

	request.targetDirectory = root.filePath(QStringLiteral("late-collision")); request.overwriteExisting = false;
	const QString raced = QDir(request.targetDirectory).filePath(QStringLiteral("data.bin"));
	request.control.progress = [&](const QString&, qint64 bytes, qint64) { if (bytes == 200003) { ok &= write(raced, "arrived later"); } };
	const auto late = extractPackageEntries(source, request);
	ok &= expect(!late.succeeded() && late.writtenCount == 0 && read(raced) == "arrived later", "new output publication must never clobber a late file");
	request.control = {};
	ok &= expect(QDir(request.targetDirectory).entryList({QStringLiteral(".vibestudio-extract-*")}, QDir::Files | QDir::Hidden).isEmpty(),
		"failed new-file publication removes its own disposable temporary output");

	source.setNames({QStringLiteral("a.txt"), QStringLiteral("z.txt")});
	request.targetDirectory = root.filePath(QStringLiteral("partial")); cancel = false;
	request.control.isCancelled = [&]() { return cancel; };
	request.control.progress = [&](const QString& path, qint64 bytes, qint64) { if (path == QStringLiteral("z.txt") && bytes > 0) { cancel = true; } };
	const auto partial = extractPackageEntries(source, request);
	ok &= expect(partial.cancelled && partial.writtenCount == 1 && read(QDir(request.targetDirectory).filePath(QStringLiteral("a.txt"))) == QByteArray(200003, 'a')
		&& !QFileInfo::exists(QDir(request.targetDirectory).filePath(QStringLiteral("z.txt"))), "cancellation retains completed files and discards the current file");
	request.control = {};

	root.mkpath(QStringLiteral("base"));
	PackageArchive folder; QString error;
	ok &= expect(folder.load(root.filePath(QStringLiteral("base")), &error), "open staging base");
	PackageStagingModel plan; ok &= expect(plan.loadBaseArchive(folder, &error), "load staging base");
	const QString input = root.filePath(QStringLiteral("import.bin"));
	ok &= expect(write(input, QByteArray(200003, 'i')) && plan.addFile(input, QStringLiteral("import.bin"), &error)
		&& plan.addBytes(QByteArray(200003, 'g'), QStringLiteral("generated.bin"), &error), "prepare disk and generated staged inputs");
	PackageStagingArchive staged(plan);
	request.targetDirectory = root.filePath(QStringLiteral("staged"));
	const auto stagedResult = extractPackageEntries(staged, request);
	ok &= expect(stagedResult.succeeded() && stagedResult.bytesRead == 400006
		&& read(QDir(request.targetDirectory).filePath(QStringLiteral("import.bin"))) == QByteArray(200003, 'i')
		&& read(QDir(request.targetDirectory).filePath(QStringLiteral("generated.bin"))) == QByteArray(200003, 'g'), "planned extraction streams disk and owned generated content");
	request.targetDirectory = root.path(); request.overwriteExisting = true;
	ok &= expect(!extractPackageEntries(staged, request).succeeded() && read(input) == QByteArray(200003, 'i'), "staged import files are protected extraction inputs");
	request.targetDirectory = root.filePath(QStringLiteral("base/inside"));
	ok &= expect(!extractPackageEntries(staged, request).succeeded() && !QFileInfo::exists(request.targetDirectory), "extraction cannot mutate the source folder membership");
	ok &= expect(write(input, QByteArray(200003, 'x')), "change original staged input");
	request.targetDirectory = root.filePath(QStringLiteral("changed-original")); request.extractAll = false; request.virtualPaths = {QStringLiteral("import.bin")};
	const auto retained = extractPackageEntries(staged, request);
	ok &= expect(retained.succeeded() && retained.writtenCount == 1
		&& read(QDir(request.targetDirectory).filePath(QStringLiteral("import.bin"))) == QByteArray(200003, 'i'), "extraction retains the accepted import after its original changes");
	ok &= expect(write(plan.operations().first().sourceIdentity->path, QByteArray(200003, 'x')), "damage retained staged input");
	request.targetDirectory = root.filePath(QStringLiteral("changed-retained-input"));
	const auto stale = extractPackageEntries(staged, request);
	ok &= expect(!stale.succeeded() && stale.writtenCount == 0 && !QFileInfo::exists(QDir(request.targetDirectory).filePath(QStringLiteral("import.bin"))), "damaged retained input never becomes extraction output");
	ok &= expect(QFile::remove(plan.operations().first().sourceIdentity->path), "remove the deliberately damaged temporary test import");

	// Creating a real link may require developer mode on Windows. When supported,
	// exercise both an output-root link and a linked child, including links whose
	// target remains inside the requested tree.
	root.mkpath(QStringLiteral("links/real"));
	std::error_code linkError;
	std::filesystem::create_directory_symlink(std::filesystem::path(root.filePath(QStringLiteral("links/real")).toStdU16String()),
		std::filesystem::path(root.filePath(QStringLiteral("links/link")).toStdU16String()), linkError);
	const QString linkPath = root.filePath(QStringLiteral("links/link"));
	bool linkAvailable = !linkError;
#ifdef Q_OS_WIN
	if (!linkAvailable) {
		// Junction creation does not need the symlink privilege on Windows.
		// Both endpoints are synthetic directories inside this test's temp root.
		const auto quoted = [](QString path) { return path.replace(QLatin1Char('\''), QStringLiteral("''")); };
		QProcess junction;
		junction.start(QStringLiteral("powershell.exe"), {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-Command"),
			QStringLiteral("New-Item -ItemType Junction -Path '%1' -Target '%2' -ErrorAction Stop | Out-Null")
				.arg(quoted(linkPath), quoted(root.filePath(QStringLiteral("links/real"))))});
		linkAvailable = junction.waitForFinished(10000) && junction.exitCode() == 0 && QFileInfo(linkPath).isJunction();
		ok &= expect(linkAvailable, "Windows extraction must exercise a real junction fixture");
	}
#endif
	if (linkAvailable) {
		source.setNames({QStringLiteral("data.bin")}); request.extractAll = true; request.virtualPaths.clear();
		request.targetDirectory = root.filePath(QStringLiteral("links/link"));
		ok &= expect(!extractPackageEntries(source, request).succeeded(), "linked output root must be rejected");
		source.setNames({QStringLiteral("link/data.bin")}); request.targetDirectory = root.filePath(QStringLiteral("links"));
		ok &= expect(!extractPackageEntries(source, request).succeeded() && !QFileInfo::exists(root.filePath(QStringLiteral("links/real/data.bin"))), "linked output child must be rejected even within the root");
		// Remove the verified link itself before the temp-directory destructor;
		// never recursively clean a path containing an unexamined redirect.
		const QFileInfo linkInfo(linkPath);
		ok &= expect(linkInfo.isJunction() ? QDir().rmdir(linkPath) : linkInfo.isSymLink() && QFile::remove(linkPath), "remove only the fixture link");
		ok &= expect(QFileInfo(root.filePath(QStringLiteral("links/real"))).isDir(), "link cleanup preserves its target");
	} else { std::cout << "Directory symlink fixture unavailable: " << linkError.message() << '\n'; }
	return ok ? 0 : 1;
}
