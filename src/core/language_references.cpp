#include "core/language_references.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QSet>

#include <algorithm>
#include <tuple>

namespace vibestudio {
namespace {
QString key(const QString& path)
{
	const QString clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
	return clean.toCaseFolded();
#else
	return clean;
#endif
}
}

AssetTextSearchReport prepareLanguageReferences(const LanguageReferenceRequest& request)
{
	AssetTextSearchReport report;
	const QFileInfo root(request.rootPath);
	report.rootPath = root.canonicalFilePath(); report.findText = request.symbol; report.referenceProvider = request.provider;
	if (report.referenceProvider.isEmpty()) { report.referenceProvider = QCoreApplication::translate("LanguageReferences", "Language server"); }
	report.referenceLocationsSkipped = request.references.skipped;
	const auto warn = [&](const QString& message) { report.complete = false; if (report.warnings.size() < 128) { report.warnings << message; } };
	const auto cancel = [&]() {
		if (!request.isCancelled || !request.isCancelled()) { return false; }
		report.cancelled = true; report.complete = false; report.saveState = QStringLiteral("cancelled"); return true;
	};
	if (cancel()) { return report; }
	if (request.rootPath.isEmpty() || !root.isDir() || root.isSymLink() || root.isJunction()) {
		warn(QCoreApplication::translate("LanguageReferences", "Reference lookup requires an existing project directory without links.")); report.saveState = QStringLiteral("failed"); return report;
	}
	if (!request.references.error.isEmpty()) { warn(request.references.error); report.saveState = QStringLiteral("failed"); return report; }
	if (request.references.limited || request.references.items.size() > languageReferenceLimit) { warn(QCoreApplication::translate("LanguageReferences", "The reference list was limited to 10000 locations.")); }
	if (request.references.skipped > 0) { warn(QCoreApplication::translate("LanguageReferences", "Invalid server reference locations omitted: %1.").arg(request.references.skipped)); }
	QHash<QString, const AssetTextBuffer*> buffers;
	QSet<QString> duplicateBuffers;
	for (const auto& buffer : request.buffers) {
		const auto identity = key(buffer.filePath);
		if (buffers.contains(identity)) { duplicateBuffers.insert(identity); }
		buffers.insert(identity, &buffer);
	}
	QMap<QString, QVector<LanguageLocation>> files;
	for (qsizetype i = 0; i < std::min<qsizetype>(request.references.items.size(), languageReferenceLimit); ++i) { const auto& at = request.references.items[i]; files[key(at.filePath)] << at; }
	qint64 bytes = 0, previews = 0;
	for (auto file = files.cbegin(); file != files.cend(); ++file) {
		if (cancel()) { break; }
		const QString path = file.value().first().filePath;
		const auto skip = [&](const QString& reason) {
			++report.filesSkipped; report.referenceLocationsSkipped += int(file.value().size());
			warn(QCoreApplication::translate("LanguageReferences", "%1: %2").arg(QDir(report.rootPath).relativeFilePath(path), reason));
		};
		if (!languageProjectSourcePath(report.rootPath, path)) { skip(QCoreApplication::translate("LanguageReferences", "Reference source is outside the project or uses a linked path.")); continue; }
		if (duplicateBuffers.contains(file.key())) { skip(QCoreApplication::translate("LanguageReferences", "More than one open snapshot names this source.")); continue; }
		const auto* buffer = buffers.value(file.key(), nullptr);
		QString text, encoding;
		qint64 sourceBytes = 0;
		if (buffer) {
			if (buffer->id.isEmpty() || !buffer->error.isEmpty() || !buffer->text.isValidUtf16() || buffer->text.contains(QChar(0))
				|| buffer->text.contains(QLatin1Char('\r')) || buffer->text.contains(QChar(0x2029)) || buffer->text.size() > textDocumentByteLimit) {
				skip(buffer->error.isEmpty() ? QCoreApplication::translate("LanguageReferences", "The open source snapshot is invalid or too large.") : buffer->error); continue;
			}
			text = buffer->text; encoding = buffer->encoding; sourceBytes = text.toUtf8().size();
		} else {
			const auto info = QFileInfo(path);
			if (!info.isFile() || info.size() > textDocumentByteLimit || info.size() > 64ll * 1024 * 1024 - bytes) {
				skip(QCoreApplication::translate("LanguageReferences", "The saved source is unavailable or exceeds the reference read limit.")); continue;
			}
			const auto document = readTextFile(path);
			if (!document.editable()) { skip(document.error); continue; }
			text = document.text; encoding = textEncodingName(document.encoding); sourceBytes = std::max<qint64>(document.originalBytes.size(), text.toUtf8().size());
		}
		if (sourceBytes > textDocumentByteLimit || sourceBytes > 64ll * 1024 * 1024 - bytes) { skip(QCoreApplication::translate("LanguageReferences", "Reference sources exceed the 4 MiB file or 64 MiB total text limit.")); continue; }
		bytes += sourceBytes; ++report.filesScanned; if (buffer) { ++report.buffersScanned; }
		const auto hash = assetTextSnapshotHash(text);
		QVector<int> starts {0};
		for (qsizetype at = text.indexOf(QLatin1Char('\n')); at >= 0; at = text.indexOf(QLatin1Char('\n'), at + 1)) { starts << int(at) + 1; }
		const auto offset = [&](int line, int character) {
			if (line < 0 || line >= starts.size() || character < 0) { return -1; }
			const int start = starts[line], end = line + 1 < starts.size() ? starts[line + 1] - 1 : int(text.size());
			if (character > end - start) { return -1; }
			const int at = start + character;
			return at > 0 && at < text.size() && text[at - 1].isHighSurrogate() && text[at].isLowSurrogate() ? -1 : at;
		};
		auto locations = file.value();
		std::sort(locations.begin(), locations.end(), [](const auto& a, const auto& b) { return std::tie(a.line, a.character, a.endLine, a.endCharacter) < std::tie(b.line, b.character, b.endLine, b.endCharacter); });
		QSet<QString> seen; const auto before = report.matches.size();
		for (const auto& at : locations) {
			if (cancel()) { break; }
			const int start = offset(at.line, at.character), end = offset(at.endLine, at.endCharacter);
			if (start < 0 || end <= start) { ++report.referenceLocationsSkipped; warn(QCoreApplication::translate("LanguageReferences", "%1: A reference range does not match the source snapshot.").arg(QDir(report.rootPath).relativeFilePath(path))); continue; }
			const QString identity = QStringLiteral("%1:%2").arg(start).arg(end);
			if (seen.contains(identity)) { continue; } seen.insert(identity);
			const int lineEnd = at.line + 1 < starts.size() ? starts[at.line + 1] - 1 : int(text.size());
			const QString line = text.mid(starts[at.line], std::min(8193, lineEnd - starts[at.line]));
			const qint64 size = (line.size() + path.size() + 256) * sizeof(QChar);
			if (size > 4ll * 1024 * 1024 - previews) {
				++report.referenceLocationsSkipped; warn(QCoreApplication::translate("LanguageReferences", "Reference previews exceed the 4 MiB display limit.")); continue;
			}
			previews += size;
			AssetTextMatch match; match.filePath = path; match.line = at.line + 1; match.column = at.character + 1; match.endLine = at.endLine + 1; match.endColumn = at.endCharacter + 1;
			match.lineText = line.simplified().left(400); match.rawLine = line; match.textSha256 = hash; match.encoding = encoding;
			if (buffer) { match.bufferId = buffer->id; match.bufferRevision = buffer->revision; }
			report.matches << match;
		}
		if (report.matches.size() > before) { ++report.filesWithMatches; }
		report.matchCount = int(report.matches.size());
		if (request.progress) { request.progress(report.filesScanned, report.matchCount); }
	}
	return report;
}

} // namespace vibestudio
