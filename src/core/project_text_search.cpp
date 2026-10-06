#include "core/project_text_search.h"
#include "core/code_files.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

namespace vibestudio {
namespace {

bool linked(const QFileInfo& file)
{
	return file.isSymLink() || file.isJunction();
}

QStringList sourceExtensions()
{
	return QStringLiteral("cfg shader qc qh txt map def ent arena skin c h cc hh cpp hpp cxx hxx inl glsl vert frag geom json yaml yml toml ini lua py js ts md mtr script acs").split(QLatin1Char(' '));
}

struct PathGlob {
	QRegularExpression expression;
	bool basename = false;
};

QString nonPathWildcard(const QString& pattern)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
	return QRegularExpression::wildcardToRegularExpression(pattern, QRegularExpression::NonPathWildcardConversion);
#else
	// Preserve Qt's documented non-path glob semantics on Qt 6.0–6.5.
	// This independent adapter uses Qt's escaping and anchoring APIs; it does
	// not copy Qt implementation code. See the Qt entry in docs/CREDITS.md.
	QString expression;
	for (qsizetype index = 0; index < pattern.size(); ++index) {
		const QChar ch = pattern[index];
		if (ch == QLatin1Char('*')) { expression += QStringLiteral(".*"); }
		else if (ch == QLatin1Char('?')) { expression += QLatin1Char('.'); }
		else if (ch == QLatin1Char('[')) {
			qsizetype start = index + 1;
			const bool negated = start < pattern.size() && pattern[start] == QLatin1Char('!');
			if (negated) { ++start; }
			const qsizetype search = start < pattern.size() && pattern[start] == QLatin1Char(']') ? start + 1 : start;
			const qsizetype end = pattern.indexOf(QLatin1Char(']'), search);
			if (end < 0) { return QStringLiteral("(?!)"); }
			QString members = pattern.mid(start, end - start);
			members.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
			members.replace(QStringLiteral("["), QStringLiteral("\\["));
			members.replace(QStringLiteral("]"), QStringLiteral("\\]"));
			members.replace(QStringLiteral("^"), QStringLiteral("\\^"));
			expression += (negated ? QStringLiteral("[^") : QStringLiteral("[")) + members + QLatin1Char(']');
			index = end;
		} else { expression += QRegularExpression::escape(QString(ch)); }
	}
	return QRegularExpression::anchoredPattern(expression);
#endif
}

QVector<PathGlob> compileGlobs(const QStringList& patterns)
{
	QVector<PathGlob> globs;
	for (QString pattern : patterns) {
		pattern = QDir::fromNativeSeparators(pattern.trimmed());
		if (pattern.isEmpty()) { continue; }
		if (pattern.startsWith(QStringLiteral("./"))) { pattern.remove(0, 2); }
		if (pattern.endsWith(QLatin1Char('/'))) { pattern += QLatin1Char('*'); }
		globs.push_back({QRegularExpression(nonPathWildcard(pattern), QRegularExpression::CaseInsensitiveOption), !pattern.contains(QLatin1Char('/'))});
	}
	return globs;
}

bool matchesGlobs(const QVector<PathGlob>& globs, const QString& relativePath, const QString& name)
{
	return std::any_of(globs.cbegin(), globs.cend(), [&](const PathGlob& glob) {
		return glob.expression.match(glob.basename ? name : relativePath).hasMatch();
	});
}

bool identifierCharacter(const QString& text, qsizetype offset)
{
	const QChar ch = text[offset];
	char32_t codepoint = ch.unicode();
	if (ch.isLowSurrogate() && offset > 0 && text[offset - 1].isHighSurrogate()) { codepoint = QChar::surrogateToUcs4(text[offset - 1], ch); }
	else if (ch.isHighSurrogate() && offset + 1 < text.size() && text[offset + 1].isLowSurrogate()) { codepoint = QChar::surrogateToUcs4(ch, text[offset + 1]); }
	return QChar::isLetterOrNumber(codepoint) || QChar::isMark(codepoint) || ch == QLatin1Char('_');
}

bool safeTarget(const QString& root, const QString& path)
{
	const QString relative = QDir(root).relativeFilePath(path);
	if (QDir::isAbsolutePath(relative) || relative == QStringLiteral("..") || relative.startsWith(QStringLiteral("../"))) {
		return false;
	}
	QString current = path;
	for (;;) {
		const QFileInfo info(current);
		if (!info.exists() || linked(info)) { return false; }
		if (current == root) { break; }
		const QString parent = info.absolutePath();
		if (parent == current) { return false; }
		current = parent;
	}
	return QFileInfo(path).isFile();
}

bool unchanged(const QString& root, const AssetTextFileChange& change)
{
	if (!safeTarget(root, change.filePath) || QFileInfo(change.filePath).size() != change.originalSize) { return false; }
	QFile file(change.filePath);
	if (!file.open(QIODevice::ReadOnly)) { return false; }
	const QByteArray bytes = file.read(change.originalSize + 1);
	return file.error() == QFileDevice::NoError && bytes.size() == change.originalSize
		&& QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) == change.originalSha256;
}

bool cancelRequested(AssetTextSearchReport& report, const std::function<bool()>& isCancelled)
{
	if (!isCancelled || !isCancelled()) { return false; }
	report.cancelled = true;
	report.complete = false;
	report.saveState = QStringLiteral("cancelled");
	return true;
}

} // namespace

bool AssetTextSearchReport::succeeded() const
{
	return complete && !cancelled && !bufferEditsPending && saveState != QStringLiteral("failed");
}

bool AssetTextSearchReport::canApply() const
{
	return succeeded() && replace && dryRun && !changes.isEmpty();
}

QByteArray assetTextSnapshotHash(const QString& text)
{
	return QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256);
}

AssetTextSearchReport findReplaceProjectText(const AssetTextSearchRequest& request)
{
	AssetTextSearchReport report;
	const QFileInfo rootInfo(request.rootPath);
	report.rootPath = rootInfo.canonicalFilePath();
	report.findText = request.findText;
	report.replaceText = request.replaceText;
	report.replace = request.replace;
	report.dryRun = true;
	const auto warn = [&](const QString& message, bool incomplete = true) {
		if (incomplete) { report.complete = false; }
		if (report.warnings.size() < 128) { report.warnings << message; }
	};
	if (request.rootPath.trimmed().isEmpty() || !rootInfo.isDir() || linked(rootInfo)) {
		warn(QCoreApplication::translate("VibeStudioProjectText", "Project root path is required and must be a directory, not a link."));
		report.saveState = QStringLiteral("failed");
		return report;
	}
	if (request.findText.isEmpty() || !request.findText.isValidUtf16() || request.findText.contains(QLatin1Char('\n'))
		|| request.findText.contains(QLatin1Char('\r')) || request.findText.contains(QChar(0x2029))) {
		warn(QCoreApplication::translate("VibeStudioProjectText", "Find text must be a nonempty Unicode line."));
		report.saveState = QStringLiteral("failed");
		return report;
	}
	if (request.maxMatches <= 0 || request.maxFiles <= 0 || request.maxFileBytes <= 0 || request.maxTotalBytes <= 0) {
		warn(QCoreApplication::translate("VibeStudioProjectText", "Search limits must be positive."));
		report.saveState = QStringLiteral("failed");
		return report;
	}
	QString replacementText = request.replaceText;
	replacementText.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
	replacementText.replace(QLatin1Char('\r'), QLatin1Char('\n'));
	replacementText.replace(QChar(0x2029), QLatin1Char('\n'));
	const auto validText = [](const QString& text) {
		return text.isValidUtf16() && !text.contains(QLatin1Char('\r')) && !text.contains(QChar(0x2029))
			&& std::none_of(text.cbegin(), text.cend(), [](QChar ch) { return ch.unicode() < 0x20 && ch != QLatin1Char('\n') && ch != QLatin1Char('\t') && ch != QLatin1Char('\f'); });
	};
	if (request.replace && !validText(replacementText)) {
		warn(QCoreApplication::translate("VibeStudioProjectText", "Replacement text contains invalid Unicode or binary controls."));
		report.saveState = QStringLiteral("failed");
		return report;
	}
	QSet<QString> extensions;
	for (QString extension : request.extensions.isEmpty() ? sourceExtensions() : request.extensions) {
		extension = extension.trimmed().toLower();
		while (extension.startsWith(QLatin1Char('.'))) { extension.remove(0, 1); }
		if (!extension.isEmpty()) { extensions.insert(extension); }
	}
	const auto includes = compileGlobs(request.includeGlobs);
	const auto excludes = compileGlobs(request.excludeGlobs);
	const Qt::CaseSensitivity sensitivity = request.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
	const qint64 fileLimit = std::min(request.maxFileBytes, textDocumentByteLimit);
	const qint64 totalLimit = std::min(request.maxTotalBytes, 64ll * 1024 * 1024);
	const int matchLimit = std::min(request.maxMatches, 10000);
	const int filesLimit = std::min(request.maxFiles, 20000);
	qint64 bytesScanned = 0, bytesPrepared = 0;
	int entriesVisited = 0;
	bool limitReached = false;
	QSet<QString> seen;
	const auto key = [](const QString& path) {
		const QString normalized = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
		return normalized.toCaseFolded();
#else
		return normalized;
#endif
	};
	const auto eligible = [&](const QString& path) {
		const QFileInfo info(path);
		const QString relative = QDir(report.rootPath).relativeFilePath(info.absoluteFilePath());
		if (QDir::isAbsolutePath(relative) || relative == QStringLiteral("..") || relative.startsWith(QStringLiteral("../"))) { return false; }
		if (!extensions.contains(info.suffix().toLower()) && !(request.extensions.isEmpty() && isCodeFileCandidate(path))) { return false; }
		if ((!includes.isEmpty() && !matchesGlobs(includes, relative, info.fileName())) || matchesGlobs(excludes, relative, info.fileName())) { return false; }
		QString parent = QFileInfo(relative).path();
		while (parent != QStringLiteral(".") && !parent.isEmpty()) {
			if (isExcludedCodeDirectory(parent) || matchesGlobs(excludes, parent + QLatin1Char('/'), QFileInfo(parent).fileName())) { return false; }
			parent = QFileInfo(parent).path();
		}
		return true;
	};
	const auto safeBuffer = [&](const QString& path) {
		QString current = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
		if (QFileInfo(current).isDir()) { return false; }
		while (key(current) != key(report.rootPath)) {
			if (linked(QFileInfo(current))) { return false; }
			const QString parent = QFileInfo(current).absolutePath();
			if (parent == current) { return false; }
			current = parent;
		}
		return true;
	};
	const auto processFile = [&](const QString& path, const AssetTextBuffer* buffer) {
		if (limitReached || cancelRequested(report, request.isCancelled) || !eligible(path) || seen.contains(key(path))) { return; }
		seen.insert(key(path));
		if (report.filesScanned >= filesLimit) { limitReached = true; return; }
		++report.filesScanned;
		if (buffer) { ++report.buffersScanned; }
		if (request.progress) { request.progress(report.filesScanned, report.matchCount); }
		QString text, encoding;
		TextFileDocument source;
		if (buffer) {
			const qint64 size = qint64(buffer->text.size()) * sizeof(QChar);
			if (buffer->id.isEmpty() || !buffer->error.isEmpty() || !safeBuffer(path) || !validText(buffer->text)
				|| size > fileLimit * 2 || size > totalLimit - bytesScanned) {
				++report.filesSkipped;
				warn(QCoreApplication::translate("VibeStudioProjectText", "Open document unavailable or outside search limits: %1. %2").arg(path, buffer->error));
				return;
			}
			bytesScanned += size;
			text = buffer->text;
			encoding = buffer->encoding;
		} else {
			const QFileInfo info(path);
			if (info.size() > fileLimit) {
				++report.filesSkipped;
				warn(QCoreApplication::translate("VibeStudioProjectText", "File exceeds the search size limit: %1").arg(path));
				return;
			}
			if (info.size() > totalLimit - bytesScanned) { limitReached = true; return; }
			QFile file(path);
			if (!safeTarget(report.rootPath, path) || !file.open(QIODevice::ReadOnly)) {
				++report.filesSkipped;
				warn(QCoreApplication::translate("VibeStudioProjectText", "Unable to read %1.").arg(path));
				return;
			}
			const QByteArray bytes = file.read(fileLimit + 1);
			if (file.error() != QFileDevice::NoError || bytes.size() > fileLimit || bytes.size() > totalLimit - bytesScanned) {
				++report.filesSkipped;
				warn(QCoreApplication::translate("VibeStudioProjectText", "File could not be read within the search limits: %1").arg(path));
				return;
			}
			bytesScanned += bytes.size();
			source = decodeTextFile(bytes);
			if (!source.editable()) { ++report.filesSkipped; return; }
			text = source.text;
			encoding = textEncodingName(source.encoding);
		}
		const auto textHash = assetTextSnapshotHash(text);
		const int firstMatch = report.matchCount;
		QVector<TextFileEdit> edits;
		int lineNumber = 1;
		qsizetype start = 0;
		while (start < text.size() && !limitReached && !cancelRequested(report, request.isCancelled)) {
			qsizetype end = text.indexOf(QLatin1Char('\n'), start);
			if (end < 0) { end = text.size(); }
			const QString line = text.mid(start, end - start);
			const QString linePreview = line.trimmed();
			const qsizetype firstLineMatch = report.matches.size();
			QString replacedLine;
			qsizetype copied = 0;
			qsizetype column = line.indexOf(request.findText, 0, sensitivity);
			while (column >= 0) {
				if (cancelRequested(report, request.isCancelled)) { break; }
				const qsizetype after = column + request.findText.size();
				if (!request.wholeWords || ((column == 0 || !identifierCharacter(line, column - 1))
					&& (after == line.size() || !identifierCharacter(line, after)))) {
					if (report.matchCount >= matchLimit) { limitReached = true; break; }
					AssetTextMatch match {path, lineNumber, static_cast<int>(column + 1), linePreview, line, {}};
					match.textSha256 = textHash;
					match.encoding = encoding;
					if (buffer) { match.bufferId = buffer->id; match.bufferRevision = buffer->revision; }
					report.matches << match;
					++report.matchCount;
					if (request.replace) {
						if (replacedLine.size() + column - copied + replacementText.size() > totalLimit / 4) { limitReached = true; break; }
						replacedLine += QStringView(line).mid(copied, column - copied);
						replacedLine += replacementText;
						copied = after;
						if (QStringView(line).mid(column, request.findText.size()) != replacementText) { edits.push_back({start + column, request.findText.size(), replacementText}); }
					}
				}
				column = line.indexOf(request.findText, after, sensitivity);
			}
			if (request.replace) {
				replacedLine += QStringView(line).mid(copied);
				for (qsizetype index = firstLineMatch; index < report.matches.size(); ++index) { report.matches[index].replacementLine = replacedLine; }
			}
			start = end + 1;
			++lineNumber;
		}
		const int count = report.matchCount - firstMatch;
		if (count > 0) {
			++report.filesWithMatches;
			if (request.replace) {
				report.replacementCount += count;
				if (!edits.isEmpty() && !limitReached && !report.cancelled) {
					AssetTextFileChange change;
					change.filePath = path;
					change.replacementCount = int(edits.size());
					change.textSha256 = textHash;
					if (buffer) {
						change.bufferId = buffer->id;
						change.bufferRevision = buffer->revision;
						change.edits = edits;
						qint64 outputChars = text.size();
						for (const auto& edit : edits) { outputChars += edit.replacement.size() - edit.length; }
						if (outputChars > textDocumentByteLimit || outputChars * 2 > totalLimit - bytesPrepared) { limitReached = true; return; }
						bytesPrepared += outputChars * 2;
					} else {
						QString error;
						if (!encodeTextFileEdits(source, edits, &change.replacementBytes, nullptr, &error)) {
							warn(QCoreApplication::translate("VibeStudioProjectText", "Unable to prepare replacements for %1: %2").arg(path, error));
							return;
						}
						if (change.replacementBytes.size() > totalLimit - bytesPrepared) { limitReached = true; return; }
						bytesPrepared += change.replacementBytes.size();
						change.originalSha256 = source.sha256;
						change.originalSize = source.originalBytes.size();
					}
					report.changes << std::move(change);
				}
			}
		}
	};
	if (request.buffers.size() > filesLimit) {
		limitReached = true;
	} else {
		for (const auto& buffer : request.buffers) {
			if (!buffer.filePath.isEmpty()) { processFile(QDir::cleanPath(QFileInfo(buffer.filePath).absoluteFilePath()), &buffer); }
		}
	}
	QStringList directories {report.rootPath};
	while (!directories.isEmpty() && !limitReached && !cancelRequested(report, request.isCancelled)) {
		const QString directory = directories.takeLast();
		if (linked(QFileInfo(directory)) || !QFileInfo(directory).isReadable()) {
			warn(QCoreApplication::translate("VibeStudioProjectText", "Unable to read directory %1.").arg(directory));
			continue;
		}
		QDirIterator iterator(directory, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
		while (iterator.hasNext() && !limitReached && !cancelRequested(report, request.isCancelled)) {
			if (entriesVisited++ >= 100000) { limitReached = true; break; }
			iterator.next();
			const QFileInfo info = iterator.fileInfo();
			if (linked(info)) { ++report.filesSkipped; continue; }
			const QString relative = QDir(report.rootPath).relativeFilePath(info.absoluteFilePath());
			if (info.isDir()) {
				if (!isExcludedCodeDirectory(info.fileName()) && !matchesGlobs(excludes, relative + QLatin1Char('/'), info.fileName())) { directories << info.absoluteFilePath(); }
			} else if (info.isFile()) { processFile(info.absoluteFilePath(), nullptr); }
		}
	}
	if (limitReached) { warn(QCoreApplication::translate("VibeStudioProjectText", "Search limit reached. Narrow the file filters and search again before replacing.")); }
	std::sort(report.matches.begin(), report.matches.end(), [](const auto& left, const auto& right) {
		if (left.filePath != right.filePath) { return left.filePath < right.filePath; }
		return left.line < right.line || (left.line == right.line && left.column < right.column);
	});
	std::sort(report.changes.begin(), report.changes.end(), [](const auto& left, const auto& right) { return left.filePath < right.filePath; });
	if (!report.cancelled) { report.saveState = !report.complete ? QStringLiteral("failed") : report.changes.isEmpty() ? QStringLiteral("clean") : QStringLiteral("modified"); }
	if (request.progress) { request.progress(report.filesScanned, report.matchCount); }
	if (request.replace && !request.dryRun) {
		if (report.canApply()) { return applyProjectTextReplacements(report, request.isCancelled); }
		report.dryRun = false;
	}
	return report;
}

AssetTextSearchReport applyProjectTextReplacements(const AssetTextSearchReport& preview,
	const std::function<bool()>& isCancelled, const std::function<void(int, int)>& progress, bool deferBufferEdits)
{
	AssetTextSearchReport report = preview;
	report.dryRun = false;
	report.writtenFiles.clear();
	report.editedBuffers.clear();
	report.bufferEditsPending = false;
	report.replacementsApplied = 0;
	if (!preview.canApply()) {
		report.saveState = QStringLiteral("failed");
		report.warnings << QCoreApplication::translate("VibeStudioProjectText", "A complete replacement preview is required before applying changes.");
		return report;
	}
	const bool hasBuffers = std::any_of(preview.changes.cbegin(), preview.changes.cend(), [](const auto& change) { return !change.bufferId.isEmpty(); });
	if (hasBuffers && !deferBufferEdits) {
		report.saveState = QStringLiteral("failed");
		report.warnings << QCoreApplication::translate("VibeStudioProjectText", "This preview includes open documents and must be applied by its editor host.");
		return report;
	}
	// Do not start a batch if even one reviewed file already changed.
	for (const auto& change : preview.changes) {
		if (!change.bufferId.isEmpty()) { continue; }
		if (cancelRequested(report, isCancelled)) { return report; }
		if (!unchanged(report.rootPath, change)) {
			report.saveState = QStringLiteral("failed");
			report.warnings << QCoreApplication::translate("VibeStudioProjectText", "File changed, disappeared, or became a link after preview: %1. Search again.").arg(change.filePath);
			return report;
		}
	}
	for (const auto& change : preview.changes) {
		if (!change.bufferId.isEmpty()) { continue; }
		if (cancelRequested(report, isCancelled)) { return report; }
		if (progress) { progress(static_cast<int>(report.writtenFiles.size()), static_cast<int>(preview.changes.size())); }
		QSaveFile output(change.filePath);
		if (!unchanged(report.rootPath, change)) {
			report.saveState = QStringLiteral("failed");
			report.warnings << QCoreApplication::translate("VibeStudioProjectText", "File changed during replacement: %1. Search again.").arg(change.filePath);
			return report;
		}
		if (!output.open(QIODevice::WriteOnly) || output.write(change.replacementBytes) != change.replacementBytes.size()) {
			report.saveState = QStringLiteral("failed");
			report.warnings << QCoreApplication::translate("VibeStudioProjectText", "Unable to write %1: %2").arg(change.filePath, output.errorString());
			return report;
		}
		if (cancelRequested(report, isCancelled)) { return report; }
		if (!unchanged(report.rootPath, change)) {
			report.saveState = QStringLiteral("failed");
			report.warnings << QCoreApplication::translate("VibeStudioProjectText", "File changed during replacement: %1. Search again.").arg(change.filePath);
			return report;
		}
		if (!output.commit()) {
			report.saveState = QStringLiteral("failed");
			report.warnings << QCoreApplication::translate("VibeStudioProjectText", "Unable to save %1: %2").arg(change.filePath, output.errorString());
			return report;
		}
		report.writtenFiles << change.filePath;
		report.replacementsApplied += change.replacementCount;
	}
	if (cancelRequested(report, isCancelled)) { return report; }
	report.bufferEditsPending = hasBuffers;
	report.saveState = hasBuffers ? QStringLiteral("pending-buffer-edits") : QStringLiteral("saved");
	if (progress) { progress(static_cast<int>(report.writtenFiles.size()), static_cast<int>(preview.changes.size())); }
	return report;
}

QString assetTextSearchReportText(const AssetTextSearchReport& report)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioProjectText", "Project text search");
	lines << QCoreApplication::translate("VibeStudioProjectText", "Root: %1").arg(report.rootPath);
	lines << QCoreApplication::translate("VibeStudioProjectText", "Find: %1").arg(report.findText);
	lines << QCoreApplication::translate("VibeStudioProjectText", "Replace: %1").arg(report.replace ? report.replaceText : QCoreApplication::translate("VibeStudioProjectText", "disabled"));
	// Preserve the original CLI labels for scripts consuming text output.
	lines << QCoreApplication::translate("VibeStudioProjectText", "Mode: %1").arg(report.dryRun
		? QCoreApplication::translate("VibeStudioProjectText", "dry run") : QCoreApplication::translate("VibeStudioProjectText", "write"));
	lines << QCoreApplication::translate("VibeStudioProjectText", "Files scanned: %1").arg(report.filesScanned);
	lines << QCoreApplication::translate("VibeStudioProjectText", "Open documents scanned: %1").arg(report.buffersScanned);
	lines << QCoreApplication::translate("VibeStudioProjectText", "Files with matches: %1").arg(report.filesWithMatches);
	lines << QCoreApplication::translate("VibeStudioProjectText", "Matches: %1").arg(report.matchCount);
	lines << QCoreApplication::translate("VibeStudioProjectText", "Replacements: %1").arg(report.replacementCount);
	lines << QCoreApplication::translate("VibeStudioProjectText", "Save state: %1").arg(report.saveState);
	lines << QCoreApplication::translate("VibeStudioProjectText", "Files skipped: %1").arg(report.filesSkipped);
	lines << QCoreApplication::translate("VibeStudioProjectText", "Replacements applied: %1").arg(report.replacementsApplied);
	lines << QCoreApplication::translate("VibeStudioProjectText", "Complete: %1; cancelled: %2").arg(report.complete).arg(report.cancelled);
	for (const auto& match : report.matches) {
		lines << QStringLiteral("%1:%2:%3  %4").arg(QDir::toNativeSeparators(match.filePath)).arg(match.line).arg(match.column).arg(match.lineText.left(2000));
		if (report.replace) { lines << QStringLiteral("  → %1").arg(match.replacementLine.left(2000)); }
	}
	for (const auto& path : report.editedBuffers) { lines << QCoreApplication::translate("VibeStudioProjectText", "Edited, not saved: %1").arg(path); }
	for (const auto& path : report.writtenFiles) { lines << QCoreApplication::translate("VibeStudioProjectText", "Saved: %1").arg(path); }
	for (const auto& warning : report.warnings) { lines << QCoreApplication::translate("VibeStudioProjectText", "Warning: %1").arg(warning); }
	return lines.join(QLatin1Char('\n'));
}

} // namespace vibestudio
