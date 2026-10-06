#include "core/language_workspace_edit.h"
#include "core/language_server.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio {
namespace {
QString key(const QString& path)
{
	QString result = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
	result = result.toCaseFolded();
#endif
	return result;
}
bool integer(const QJsonValue& value, int* result)
{
	const double n = value.toDouble(-1);
	if (!value.isDouble() || !std::isfinite(n) || n < 0 || n >= std::numeric_limits<int>::max() || std::floor(n) != n) { return false; }
	*result = int(n); return true;
}
}

// Original LSP 3.17 WorkspaceEdit implementation using Microsoft protocol facts
// (CC-BY-4.0), reviewed 2026-10-04. No upstream code copied.
// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#workspaceEdit
AssetTextSearchReport prepareLanguageWorkspaceEdit(const LanguageWorkspaceEditRequest& request)
{
	AssetTextSearchReport report; report.rootPath = QFileInfo(request.rootPath).canonicalFilePath();
	report.findText = request.findText; report.replaceText = request.replaceText; report.replace = true;
	report.referenceProvider = request.provider; report.semanticRename = request.rename; report.codeActionTitle = request.rename ? QString() : request.title;
	const auto fail = [&](const QString& why) {
		report.complete = false; report.saveState = report.cancelled ? QStringLiteral("cancelled") : QStringLiteral("failed");
		report.matches.clear(); report.changes.clear(); report.matchCount = report.replacementCount = report.filesWithMatches = 0;
		report.warnings << why; return report;
	};
	if (!request.error.isEmpty()) { return fail(request.error); }
	const QFileInfo root(request.rootPath);
	if (request.rootPath.isEmpty() || report.rootPath.isEmpty() || !root.isDir() || root.isSymLink() || root.isJunction() || (!request.rename && (request.title.isEmpty() || request.title.size() > 512 || !request.title.isValidUtf16() || request.title.contains(QChar(0))))) {
		return fail(QCoreApplication::translate("LanguageWorkspaceEdit", "Language edits require a project directory and a valid action title."));
	}
	QHash<QString, AssetTextBuffer> buffers; QHash<QString, int> versions;
	for (const auto& buffer : request.buffers) {
		if (buffers.contains(key(buffer.filePath))) { return fail(QCoreApplication::translate("LanguageWorkspaceEdit", "Ambiguous open documents prevent this edit.")); }
		buffers.insert(key(buffer.filePath), buffer);
	}
	for (auto it = request.versions.cbegin(); it != request.versions.cend(); ++it) { versions.insert(key(it.key()), it.value()); }
	if (request.workspaceEdit.isNull()) { return report; }
	if (!request.workspaceEdit.isObject()) { return fail(QCoreApplication::translate("LanguageWorkspaceEdit", "The language server returned an invalid workspace edit.")); }
	const auto workspace = request.workspaceEdit.toObject();
	if (workspace.contains(QStringLiteral("changeAnnotations")) && (!workspace.value(QStringLiteral("changeAnnotations")).isObject() || !workspace.value(QStringLiteral("changeAnnotations")).toObject().isEmpty())) {
		return fail(QCoreApplication::translate("LanguageWorkspaceEdit", "Annotated workspace edits are not supported. No changes were prepared."));
	}
	struct Target { QString path; QJsonArray edits; int version = -1; };
	QMap<QString, Target> targets;
	const auto add = [&](const QString& uri, const QJsonValue& edits, int version) {
		const QString path = languageServerPath(uri), identity = key(path);
		if (path.isEmpty() || !languageProjectSourcePath(report.rootPath, path) || !edits.isArray() || targets.contains(identity) || targets.size() >= 256) { return false; }
		targets.insert(identity, {path, edits.toArray(), version}); return true;
	};
	// documentChanges takes precedence when supplied; file operations are never
	// advertised or accepted. Duplicate document entries are ambiguous here.
	if (workspace.contains(QStringLiteral("documentChanges"))) {
		if (!workspace.value(QStringLiteral("documentChanges")).isArray()) { return fail(QCoreApplication::translate("LanguageWorkspaceEdit", "Invalid versioned workspace edits.")); }
		for (const auto& value : workspace.value(QStringLiteral("documentChanges")).toArray()) {
			const auto object = value.toObject(), doc = object.value(QStringLiteral("textDocument")).toObject(); int version = -1;
			const auto wireVersion = doc.value(QStringLiteral("version"));
			if (object.contains(QStringLiteral("kind")) || (!wireVersion.isNull() && !integer(wireVersion, &version))
				|| !add(doc.value(QStringLiteral("uri")).toString(), object.value(QStringLiteral("edits")), version)) {
				return fail(QCoreApplication::translate("LanguageWorkspaceEdit", "Language edit contains unsupported file operations, duplicate targets, invalid paths or versions, or too many files."));
			}
		}
	} else if (workspace.contains(QStringLiteral("changes"))) {
		if (!workspace.value(QStringLiteral("changes")).isObject()) { return fail(QCoreApplication::translate("LanguageWorkspaceEdit", "Invalid workspace edit map.")); }
		const auto changes = workspace.value(QStringLiteral("changes")).toObject();
		for (auto it = changes.begin(); it != changes.end(); ++it) {
			if (!add(it.key(), it.value(), -1)) { return fail(QCoreApplication::translate("LanguageWorkspaceEdit", "Language edit contains duplicate targets, invalid paths, or too many files.")); }
		}
	}
	qint64 totalSource = 0, totalOutput = 0, totalPreview = 0; int totalEdits = 0;
	for (const auto& target : targets) {
		if (request.isCancelled && request.isCancelled()) { report.cancelled = true; return fail(QCoreApplication::translate("LanguageWorkspaceEdit", "Language edit preview cancelled.")); }
		const QString identity = key(target.path); const bool live = buffers.contains(identity);
		if (live && versions.value(identity) <= 0) { return fail(QCoreApplication::translate("LanguageWorkspaceEdit", "An open language edit target was not synchronized with this language server: %1").arg(target.path)); }
		if (target.version >= 0 && (!versions.contains(identity) || versions.value(identity) != target.version)) { return fail(QCoreApplication::translate("LanguageWorkspaceEdit", "A language edit target has a stale or unknown document version: %1").arg(target.path)); }
		const auto buffer = buffers.value(identity);
		TextFileDocument disk;
		if (!live) { disk = readTextFile(target.path); }
		const QString source = live ? buffer.text : disk.text;
		if ((live && (buffer.id.isEmpty() || buffer.revision < 0 || !buffer.error.isEmpty())) || (!live && !disk.editable())
			|| source.size() > textDocumentByteLimit || !source.isValidUtf16() || source.contains(QChar(0)) || source.contains(QLatin1Char('\r')) || source.contains(QChar(0x2029))) {
			return fail(QCoreApplication::translate("LanguageWorkspaceEdit", "A language edit target cannot be edited: %1").arg(target.path));
		}
		totalSource += source.toUtf8().size(); totalEdits += target.edits.size();
		if (totalSource > 64ll * 1024 * 1024 || totalEdits > 10000) { return fail(QCoreApplication::translate("LanguageWorkspaceEdit", "Language edit exceeds the source or edit limit.")); }
		const auto edits = parseLanguageFormatting(target.edits, source); QString after;
		if (!edits.error.isEmpty() || !previewLanguageFormatting(source, edits, &after)) { return fail(QCoreApplication::translate("LanguageWorkspaceEdit", "Language edit contains invalid, overlapping or excessive text edits: %1").arg(target.path)); }
		++report.filesScanned; if (live) { ++report.buffersScanned; }
		if (edits.edits.isEmpty()) { continue; }
		AssetTextFileChange change; change.filePath = target.path; change.edits = edits.edits; change.textSha256 = assetTextSnapshotHash(source);
		change.replacementCount = edits.edits.size();
		if (live) { change.bufferId = buffer.id; change.bufferRevision = buffer.revision; totalOutput += after.toUtf8().size(); }
		else {
			change.originalSha256 = disk.sha256; change.originalSize = disk.originalBytes.size(); QString error;
			if (!encodeTextFileEdits(disk, edits.edits, &change.replacementBytes, nullptr, &error)) { return fail(QCoreApplication::translate("LanguageWorkspaceEdit", "Unable to encode language edit target %1: %2").arg(target.path, error)); }
			totalOutput += change.replacementBytes.size();
		}
		if (totalOutput > 64ll * 1024 * 1024) { return fail(QCoreApplication::translate("LanguageWorkspaceEdit", "Language edit exceeds the output limit.")); }
		QVector<int> starts {0};
		for (int at = source.indexOf(QLatin1Char('\n')); at >= 0; at = source.indexOf(QLatin1Char('\n'), at + 1)) { starts << at + 1; }
		for (const auto& edit : edits.edits) {
			const int line = int(std::upper_bound(starts.cbegin(), starts.cend(), edit.offset) - starts.cbegin()) - 1;
			const int end = int(std::upper_bound(starts.cbegin(), starts.cend(), edit.offset + edit.length) - starts.cbegin()) - 1;
			AssetTextMatch match; match.filePath = target.path; match.line = line + 1; match.column = edit.offset - starts[line] + 1;
			match.endLine = end + 1; match.endColumn = edit.offset + edit.length - starts[end] + 1;
			match.bufferId = change.bufferId; match.bufferRevision = change.bufferRevision; match.textSha256 = change.textSha256;
			match.encoding = live ? buffer.encoding : textEncodingName(disk.encoding);
			// Exact replaced range, bounded for display; the complete edit remains in
			// the plan. Context in the list comes from the original source line.
			match.rawLine = source.mid(edit.offset, std::min<qsizetype>(edit.length, 8193)); match.replacementLine = edit.replacement.left(8193);
			const int lineEnd = line + 1 < starts.size() ? starts[line + 1] - 1 : source.size();
			match.lineText = source.mid(starts[line], std::min(lineEnd - starts[line], 8193));
			totalPreview += match.rawLine.size() + match.replacementLine.size() + match.lineText.size();
			if (totalPreview > 4ll * 1024 * 1024) { return fail(QCoreApplication::translate("LanguageWorkspaceEdit", "Language edit exceeds the preview limit.")); }
			report.matches << match;
		}
		report.changes << change; ++report.filesWithMatches; report.matchCount += edits.edits.size(); report.replacementCount += edits.edits.size();
		if (request.progress) { request.progress(report.filesScanned, report.matchCount); }
	}
	return report;
}

QByteArray languageWorkspaceEditPlanHash(const AssetTextSearchReport& report)
{
	if (!report.hasLanguageEdits() || !report.succeeded()) { return {}; }
	QJsonArray files;
	for (const auto& change : report.changes) {
		QJsonArray edits;
		for (const auto& edit : change.edits) { edits << QJsonObject {{QStringLiteral("offset"), qint64(edit.offset)}, {QStringLiteral("length"), qint64(edit.length)}, {QStringLiteral("text"), edit.replacement}}; }
		files << QJsonObject {{QStringLiteral("path"), change.filePath}, {QStringLiteral("original"), QString::fromLatin1(change.originalSha256.toHex())},
			{QStringLiteral("text"), QString::fromLatin1(change.textSha256.toHex())}, {QStringLiteral("output"), QString::fromLatin1(QCryptographicHash::hash(change.replacementBytes, QCryptographicHash::Sha256).toHex())},
			{QStringLiteral("buffer"), change.bufferId}, {QStringLiteral("revision"), change.bufferRevision}, {QStringLiteral("edits"), edits}};
	}
	QJsonObject plan {{QStringLiteral("schema"), 1}, {QStringLiteral("root"), report.rootPath}, {QStringLiteral("symbol"), report.findText},
		{QStringLiteral("newName"), report.replaceText}, {QStringLiteral("provider"), report.referenceProvider}, {QStringLiteral("files"), files}};
	if (!report.semanticRename) { plan.insert(QStringLiteral("codeAction"), report.codeActionTitle); }
	return QCryptographicHash::hash(QJsonDocument(plan).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256);
}

} // namespace vibestudio
