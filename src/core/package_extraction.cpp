#include "core/package_archive.h"
#include "core/package_staging.h"
#include "core/package_protection_p.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryFile>

#include <algorithm>
#include <limits>
#include <memory>

namespace vibestudio {
namespace {

QString pathKey(const QString& path)
{
	// Portable output names: collisions must behave the same on a case-sensitive
	// development machine and the case-insensitive machines receiving its files.
	return path.normalized(QString::NormalizationForm_C).toCaseFolded();
}

void addResult(PackageExtractionReport& report, const PackageExtractionEntryResult& result)
{
	report.entries << result;
	if (result.error.isEmpty()) { ++report.processedCount; }
	else {
		++report.errorCount;
		report.warnings << (result.virtualPath.isEmpty() ? result.error : QStringLiteral("%1: %2").arg(result.virtualPath, result.error));
	}
	if (result.kind == PackageEntryKind::Directory && result.error.isEmpty()) { ++report.directoryCount; }
	if (result.written) { ++report.writtenCount; report.totalBytes += result.bytes; }
	if (result.skipped) {
		++report.skippedCount;
		if (!result.message.isEmpty()) { report.warnings << QStringLiteral("%1: %2").arg(result.virtualPath, result.message); }
	}
}

bool safeFilesystemPath(const QString& path, QString* error)
{
	QString probe = QFileInfo(path).absoluteFilePath();
	bool leaf = true;
	while (!probe.isEmpty()) {
		const QFileInfo info(probe);
		if (info.isSymLink() || info.isJunction()) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Extraction refuses symbolic links and junctions in the output path: %1").arg(probe);
			return false;
		}
		if (!leaf && info.exists() && !info.isDir()) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "An output parent is not a directory: %1").arg(probe);
			return false;
		}
		const QString parent = info.absolutePath();
		if (parent == probe) { break; }
		probe = parent;
		leaf = false;
	}
	return true;
}

bool checkOutput(PackageInputProtectionSet& inputs, const QString& root, const QString& path, QString* error)
{
	bool protectedPath = false;
	if (!inputs.check(path, &protectedPath)) { *error = inputs.errorString(); return false; }
	if (!safeFilesystemPath(root, error) || !safeFilesystemPath(path, error)) { return false; }
	if (!packagePathIsInsideDirectory(root, path)) { *error = QCoreApplication::translate("VibeStudioPackageArchive", "Output path escapes the selected root."); return false; }
	if (protectedPath) { *error = QCoreApplication::translate("VibeStudioPackageArchive", "Extraction would replace or modify package source content."); return false; }
	return true;
}

} // namespace

PackageExtractionReport extractPackageEntries(const PackageArchiveReader& archive, const PackageExtractionRequest& request,
	PackageExtractionProgressCallback progress)
{
	PackageExtractionReport report;
	report.sourcePath = archive.sourcePath();
	report.targetDirectory = QDir::cleanPath(QFileInfo(request.targetDirectory).absoluteFilePath());
	report.extractAll = request.extractAll;
	report.dryRun = request.dryRun;
	report.overwriteExisting = request.overwriteExisting;
	const auto cancelled = [&]() {
		if (!report.cancelled && request.control.isCancelled && request.control.isCancelled()) {
			report.cancelled = true;
			report.warnings << QCoreApplication::translate("VibeStudioPackageArchive", "Package extraction cancelled. Completed files are retained; the current file was not committed.");
		}
		return report.cancelled;
	};
	const auto fail = [&](const QString& message) {
		PackageExtractionEntryResult result; result.outputPath = report.targetDirectory; result.error = message;
		addResult(report, result);
	};
	if (cancelled()) { return report; }
	if (!archive.isOpen()) { fail(archive.errorString().isEmpty() ? QCoreApplication::translate("VibeStudioPackageArchive", "No package is open.") : archive.errorString()); return report; }
	const auto* document = dynamic_cast<const PackageArchive*>(&archive);
	const auto* plannedReader = dynamic_cast<const PackageStagingArchive*>(document && document->snapshotReader() ? document->snapshotReader().get() : &archive);
	if (plannedReader) {
		for (const auto& warning : plannedReader->warnings()) {
			if (warning.blocksSaving) {
				fail(QCoreApplication::translate("VibeStudioPackageArchive", "Resolve blocked package edits before extracting the planned package: %1").arg(warning.message));
				return report;
			}
		}
	}
	if (request.extractAll && (!request.virtualPaths.isEmpty() || !request.entrySelections.isEmpty())) {
		fail(QCoreApplication::translate("VibeStudioPackageArchive", "Choose explicit entries or extract-all, not both.")); return report;
	}
	if (!request.extractAll && request.virtualPaths.isEmpty() && request.entrySelections.isEmpty()) {
		report.warnings << QCoreApplication::translate("VibeStudioPackageArchive", "No package entries were selected; nothing was extracted. Request extract-all to write the whole archive.");
		return report;
	}
	if (request.targetDirectory.trimmed().isEmpty()) { fail(QCoreApplication::translate("VibeStudioPackageArchive", "Output directory is required.")); return report; }
	const QFileInfo rootInfo(report.targetDirectory);
	QString rootError;
	if (!safeFilesystemPath(report.targetDirectory, &rootError)) { fail(rootError); return report; }
	if (rootInfo.exists() && !rootInfo.isDir()) { fail(QCoreApplication::translate("VibeStudioPackageArchive", "Output path exists but is not a directory.")); return report; }

	// Capture one immutable protection view for the entire operation. Every
	// output still receives fresh path/link checks before creation and commit.
	auto protectionControl = request.control; protectionControl.isCancelled = cancelled;
	PackageInputProtectionSet inputs(PackageInputProtectionSet::pathCeiling, PackageInputProtectionSet::byteCeiling, &rootError, protectionControl);
	if (!inputs.finish(archive.visitProtectedInputPaths([&](const QString& path) { return inputs.add(path); }, &rootError, protectionControl))) {
		if (!cancelled()) { fail(inputs.errorString()); } return report;
	}
	const auto entries = archive.entries();
	QSet<qsizetype> selected;
	QHash<qsizetype, QString> outputNames;
	if (request.extractAll) {
		for (qsizetype index = 0; index < entries.size(); ++index) { selected.insert(index); }
	} else {
		for (const auto& selection : request.entrySelections) {
			if (cancelled()) { return report; }
			if (selection.entryIndex < 0 || selection.entryIndex >= entries.size()
				|| entries.at(selection.entryIndex).kind != PackageEntryKind::File || outputNames.contains(selection.entryIndex)) {
				PackageExtractionEntryResult result; result.entryIndex = selection.entryIndex;
				result.error = QCoreApplication::translate("VibeStudioPackageArchive", "An explicit extraction index must select one existing file and cannot be repeated.");
				addResult(report, result); continue;
			}
			selected.insert(selection.entryIndex);
			outputNames.insert(selection.entryIndex, selection.outputVirtualPath.isEmpty()
				? entries.at(selection.entryIndex).virtualPath : selection.outputVirtualPath);
		}
		for (const auto& path : request.virtualPaths) {
			if (cancelled()) { return report; }
			const auto normalized = normalizePackageVirtualPath(path, false);
			QString error;
			if (!normalized.isSafe()) { error = QCoreApplication::translate("VibeStudioPackageArchive", "Unsafe package path: %1").arg(packagePathIssueDisplayName(normalized.issue)); }
			else {
				bool found = false;
				const QString key = pathKey(normalized.normalizedPath);
				for (qsizetype index = 0; index < entries.size(); ++index) {
					if ((index & 255) == 0 && cancelled()) { return report; }
					const QString candidate = pathKey(entries.at(index).virtualPath);
					if (candidate == key || candidate.startsWith(key + QLatin1Char('/'))) { selected.insert(index); found = true; }
				}
				if (!found) { error = QCoreApplication::translate("VibeStudioPackageArchive", "Package entry not found."); }
			}
			if (!error.isEmpty()) { PackageExtractionEntryResult result; result.virtualPath = path; result.error = error; addResult(report, result); }
		}
	}
	QVector<qsizetype> order(selected.begin(), selected.end());
	std::sort(order.begin(), order.end(), [&](qsizetype left, qsizetype right) {
		const auto& a = entries.at(left); const auto& b = entries.at(right);
		if (a.kind != b.kind) { return a.kind == PackageEntryKind::Directory; }
		const int compared = a.virtualPath.compare(b.virtualPath, Qt::CaseInsensitive);
		return compared == 0 ? left < right : compared < 0;
	});
	report.requestedCount = static_cast<int>(order.size()) + report.errorCount;

	// Validate the entire namespace before making directories or writing files.
	// Never collapse duplicate source records to a name lookup or let overwrite
	// choose an arbitrary WAD lump, ZIP member or case-folded filename.
	QHash<QString, int> counts;
	QSet<QString> fileNames;
	for (const qsizetype index : order) {
		const auto& entry = entries.at(index);
		const QString key = pathKey(normalizePackageVirtualPath(outputNames.value(index, entry.virtualPath), false).normalizedPath);
		++counts[key];
		if (entry.kind == PackageEntryKind::File) { fileNames.insert(key); }
	}
	QVector<PackageExtractionEntryResult> planned;
	for (const qsizetype index : order) {
		if (cancelled()) { return report; }
		const auto& entry = entries.at(index);
		PackageExtractionEntryResult result;
		result.virtualPath = entry.virtualPath; result.kind = entry.kind; result.bytes = entry.sizeBytes;
		result.entryIndex = index; result.sourceOrdinal = entry.sourceOrdinal; result.dryRun = request.dryRun;
		// Check the lexical path before canonicalization can hide a link.
		const QString outputName = outputNames.value(index, entry.virtualPath);
		result.outputPath = safePackageOutputPath(report.targetDirectory, outputName, &result.error);
		if (result.error.isEmpty() && !isSafePackageVirtualPath(entry.virtualPath)) {
			result.error = QCoreApplication::translate("VibeStudioPackageArchive", "The selected source entry has an unsafe package path.");
		}
		const QString normalized = normalizePackageVirtualPath(outputName, false).normalizedPath;
		if (result.error.isEmpty()) {
			const QString lexical = QDir(report.targetDirectory).filePath(normalized);
			if (checkOutput(inputs, report.targetDirectory, lexical, &result.error)) { result.outputPath = lexical; }
			if (cancelled()) { return report; }
		}
		const QString key = pathKey(normalized);
		if (result.error.isEmpty() && counts.value(key) > 1) { result.error = QCoreApplication::translate("VibeStudioPackageArchive", "Multiple package entries share this output path. Extraction requires unique paths."); }
		for (QString parent = packageVirtualPathParent(key); result.error.isEmpty() && !parent.isEmpty(); parent = packageVirtualPathParent(parent)) {
			if (fileNames.contains(parent)) { result.error = QCoreApplication::translate("VibeStudioPackageArchive", "A package file also occupies a required output directory."); }
		}
		const QFileInfo output(result.outputPath);
		if (result.error.isEmpty() && output.exists() && output.isDir() != (entry.kind == PackageEntryKind::Directory)) {
			result.error = QCoreApplication::translate("VibeStudioPackageArchive", "The output path has a conflicting file or directory type.");
		}
		if (result.error.isEmpty() && entry.kind == PackageEntryKind::File && !entry.readable) {
			result.error = entry.note.isEmpty() ? QCoreApplication::translate("VibeStudioPackageArchive", "Package entry is not readable by the current reader.") : entry.note;
		}
		if (entry.sizeBytes > static_cast<quint64>(std::numeric_limits<qint64>::max())) { result.error = QCoreApplication::translate("VibeStudioPackageArchive", "Package entry size exceeds the supported range."); }
		if (!result.error.isEmpty()) { addResult(report, result); }
		planned << result;
	}
	if (report.errorCount) {
		report.warnings << QCoreApplication::translate("VibeStudioPackageArchive", "Extraction preflight failed. No output files or directories were created.");
		return report;
	}

	for (auto result : planned) {
		if (cancelled()) { break; }
		const QFileInfo before(result.outputPath);
		if (!checkOutput(inputs, report.targetDirectory, result.outputPath, &result.error)) {
			// Recheck after previous entries and any progress callback.
			if (cancelled()) { break; }
		} else if (result.kind == PackageEntryKind::Directory) {
			if (before.exists() && !before.isDir()) { result.error = QCoreApplication::translate("VibeStudioPackageArchive", "The output path has a conflicting file or directory type."); }
			else if (request.dryRun) { result.message = QCoreApplication::translate("VibeStudioPackageArchive", "Would create directory."); }
			else if (!QDir().mkpath(result.outputPath)) { result.error = QCoreApplication::translate("VibeStudioPackageArchive", "Unable to create output directory."); }
			else { result.written = true; result.bytes = 0; result.message = QCoreApplication::translate("VibeStudioPackageArchive", "Created directory."); }
		} else if (before.exists() && !before.isFile()) {
			result.error = QCoreApplication::translate("VibeStudioPackageArchive", "The output path is not a regular file.");
		} else if (before.exists() && !request.overwriteExisting) {
			result.skipped = true; result.message = QCoreApplication::translate("VibeStudioPackageArchive", "Output exists; pass overwrite to replace it.");
		} else if (request.dryRun) {
			result.message = before.exists() ? QCoreApplication::translate("VibeStudioPackageArchive", "Would overwrite file.") : QCoreApplication::translate("VibeStudioPackageArchive", "Would write file.");
		} else {
			PackageFileIdentityPtr existing;
			if (before.exists()) { existing = capturePackageFileIdentity(result.outputPath, &result.error, request.control); }
			const QString parent = before.absolutePath();
			if (result.error.isEmpty() && !cancelled() && !QDir().mkpath(parent)) { result.error = QCoreApplication::translate("VibeStudioPackageArchive", "Unable to create output parent directory."); }
			std::unique_ptr<QSaveFile> replacement;
			std::unique_ptr<QTemporaryFile> fresh;
			QIODevice* output = nullptr;
			if (result.error.isEmpty() && !cancelled() && checkOutput(inputs, report.targetDirectory, result.outputPath, &result.error)) {
				if (before.exists()) {
					replacement = std::make_unique<QSaveFile>(result.outputPath);
					replacement->setDirectWriteFallback(false);
					if (!replacement->open(QIODevice::WriteOnly)) { result.error = replacement->errorString(); }
					else { output = replacement.get(); }
				} else {
					fresh = std::make_unique<QTemporaryFile>(QDir(parent).filePath(QStringLiteral(".vibestudio-extract-XXXXXX")));
					if (!fresh->open()) { result.error = fresh->errorString(); }
					else { output = fresh.get(); }
				}
			}
			quint64 received = 0;
			if (output) {
				if (request.control.progress) { request.control.progress(result.virtualPath, 0, static_cast<qint64>(result.bytes)); }
				QString readError;
				const bool read = archive.streamEntryAt(result.entryIndex, [&](QByteArrayView chunk) {
					if (cancelled()) { return false; }
					if (static_cast<quint64>(chunk.size()) > result.bytes - received) { result.error = QCoreApplication::translate("VibeStudioPackageArchive", "Package entry exceeded its declared size."); return false; }
					report.bytesRead += static_cast<quint64>(chunk.size());
					if (output->write(chunk.data(), chunk.size()) != chunk.size()) { result.error = QCoreApplication::translate("VibeStudioPackageArchive", "Unable to write all output bytes: %1").arg(output->errorString()); return false; }
					received += static_cast<quint64>(chunk.size());
					if (request.control.progress) { request.control.progress(result.virtualPath, static_cast<qint64>(received), static_cast<qint64>(result.bytes)); }
					return !cancelled();
				}, &readError, cancelled);
				if (!read && result.error.isEmpty() && !cancelled()) { result.error = readError.isEmpty() ? QCoreApplication::translate("VibeStudioPackageArchive", "Unable to read the complete package entry.") : readError; }
				if (read && received != result.bytes) { result.error = QCoreApplication::translate("VibeStudioPackageArchive", "Package entry size does not match its directory record."); }
				if (read && result.error.isEmpty() && !cancelled() && checkOutput(inputs, report.targetDirectory, result.outputPath, &result.error)) {
					bool committed = false;
					if (replacement) {
						if (existing && verifyPackageFileIdentity(existing, &result.error, request.control) && !cancelled()
							&& checkOutput(inputs, report.targetDirectory, result.outputPath, &result.error)) {
							committed = replacement->commit();
							if (!committed) { result.error = replacement->errorString(); }
						}
					} else {
						// QTemporaryFile::rename is atomic-only and refuses an existing
						// target; QFile::rename's copy fallback must not be used here.
						committed = fresh->flush() && !cancelled() && fresh->rename(result.outputPath);
						if (committed) { fresh->setAutoRemove(false); }
						else if (!cancelled()) { result.error = QCoreApplication::translate("VibeStudioPackageArchive", "Unable to publish without overwriting another file: %1").arg(fresh->errorString()); }
					}
					if (committed) { result.written = true; result.message = before.exists() ? QCoreApplication::translate("VibeStudioPackageArchive", "Overwrote file.") : QCoreApplication::translate("VibeStudioPackageArchive", "Wrote file."); }
				}
			}
			if (cancelled() && !result.written) { result.error.clear(); result.message = QCoreApplication::translate("VibeStudioPackageArchive", "Cancelled before this file was committed."); }
		}
		addResult(report, result);
		if (progress && !progress(report.entries.back(), report)) {
			report.cancelled = true;
			report.warnings << QCoreApplication::translate("VibeStudioPackageArchive", "Package extraction cancelled. Completed files are retained.");
		}
		if (report.cancelled || !result.error.isEmpty()) { break; }
	}
	return report;
}

} // namespace vibestudio
