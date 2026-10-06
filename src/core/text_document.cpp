#include "core/text_document.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>
#include <QStringConverter>

#include <algorithm>
#include <array>

namespace vibestudio {
namespace {

QByteArray hash(const QByteArray& bytes)
{
	return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

QString endingText(TextLineEnding ending)
{
	switch (ending) {
	case TextLineEnding::CrLf: return QStringLiteral("\r\n");
	case TextLineEnding::Cr: return QStringLiteral("\r");
	case TextLineEnding::ParagraphSeparator: return QString(QChar(0x2029));
	case TextLineEnding::Lf: return QStringLiteral("\n");
	}
	return QStringLiteral("\n");
}

QStringConverter::Encoding converterEncoding(TextEncoding encoding)
{
	switch (encoding) {
	case TextEncoding::Utf16LittleEndian: return QStringConverter::Utf16LE;
	case TextEncoding::Utf16BigEndian: return QStringConverter::Utf16BE;
	case TextEncoding::Utf8: return QStringConverter::Utf8;
	}
	return QStringConverter::Utf8;
}

// A bounded, monotonic line correspondence: keep common edges first, then
// locate matching middle lines in source order. There is no quadratic diff
// table even for a file containing hundreds of thousands of short lines.
QVector<int> sourceLineMapping(const QString& before, const QString& after)
{
	const auto oldLines = QStringView(before).split(QLatin1Char('\n'));
	const auto newLines = QStringView(after).split(QLatin1Char('\n'));
	const int oldCount = static_cast<int>(oldLines.size());
	const int newCount = static_cast<int>(newLines.size());
	QVector<int> mapping(newLines.size(), -1);
	int prefix = 0;
	while (prefix < oldCount && prefix < newCount && oldLines[prefix] == newLines[prefix]) {
		mapping[prefix] = prefix;
		++prefix;
	}
	int oldEnd = oldCount, newEnd = newCount;
	while (oldEnd > prefix && newEnd > prefix && oldLines[oldEnd - 1] == newLines[newEnd - 1]) {
		mapping[--newEnd] = --oldEnd;
	}
	QHash<QStringView, QVector<int>> occurrences;
	for (int line = prefix; line < oldEnd; ++line) { occurrences[oldLines[line]].push_back(line); }
	int oldStart = prefix, newStart = prefix;
	const auto pairChanged = [&](int oldStop, int newStop) {
		const int count = std::min(oldStop - oldStart, newStop - newStart);
		for (int index = 0; index < count; ++index) { mapping[newStart + index] = oldStart + index; }
	};
	for (int line = prefix; line < newEnd; ++line) {
		const auto it = occurrences.constFind(newLines[line]);
		if (it == occurrences.cend()) { continue; }
		const auto match = std::lower_bound(it->cbegin(), it->cend(), oldStart);
		if (match == it->cend()) { continue; }
		pairChanged(*match, line);
		mapping[line] = *match;
		oldStart = *match + 1;
		newStart = line + 1;
	}
	pairChanged(oldEnd, newEnd);
	return mapping;
}

bool sameResolvedFile(const QString& first, const QString& second)
{
#ifdef Q_OS_WIN
	return first.compare(second, Qt::CaseInsensitive) == 0;
#else
	return first == second;
#endif
}

} // namespace

TextFileDocument decodeTextFile(const QByteArray& bytes)
{
	TextFileDocument document;
	document.readable = true;
	if (bytes.size() > textDocumentByteLimit) {
		document.originalBytes = bytes.first(textDocumentByteLimit + 1);
		document.text = QString::fromUtf8(QByteArrayView(bytes).first(textDocumentByteLimit));
		document.truncated = true;
		document.error = QCoreApplication::translate("TextDocument", "Text files larger than 4 MiB are read-only.");
		return document;
	}
	document.originalBytes = bytes;
	document.sha256 = hash(bytes);
	// A lossy preview is read-only. Its error must be displayed by the caller.
	document.text = QString::fromUtf8(bytes);
	if (bytes.startsWith(QByteArray::fromHex("fffe0000")) || bytes.startsWith(QByteArray::fromHex("0000feff"))) {
		document.error = QCoreApplication::translate("TextDocument", "UTF-32 is not supported. This preview is read-only.");
		return document;
	}
	qsizetype bomBytes = 0;
	if (bytes.startsWith(QByteArray::fromHex("efbbbf"))) { bomBytes = 3; }
	else if (bytes.startsWith(QByteArray::fromHex("fffe"))) { document.encoding = TextEncoding::Utf16LittleEndian; bomBytes = 2; }
	else if (bytes.startsWith(QByteArray::fromHex("feff"))) { document.encoding = TextEncoding::Utf16BigEndian; bomBytes = 2; }
	document.byteOrderMark = bomBytes > 0;
	QStringDecoder decoder(converterEncoding(document.encoding), QStringConverter::Flag::Stateless | QStringConverter::Flag::ConvertInitialBom);
	const QByteArrayView payload = QByteArrayView(bytes).sliced(bomBytes);
	const QString decoded = decoder(payload);
	QStringEncoder verifier(converterEncoding(document.encoding), QStringConverter::Flag::Stateless);
	const QByteArray verifiedBytes = verifier(decoded);
	// Some UTF-16 decoder paths retain an unpaired surrogate or omit a final
	// odd byte without setting hasError(). Require an exact, valid round trip.
	if (decoder.hasError() || !decoded.isValidUtf16() || verifier.hasError() || QByteArrayView(verifiedBytes) != payload) {
		document.error = QCoreApplication::translate("TextDocument", "The file is not valid UTF-8 or BOM-marked UTF-16. This preview is read-only.");
		return document;
	}
	document.text.clear();
	document.text.reserve(decoded.size());
	std::array<int, 4> counts {};
	for (qsizetype index = 0; index < decoded.size(); ++index) {
		const QChar character = decoded[index];
		if (character == QLatin1Char('\r') || character == QLatin1Char('\n') || character == QChar(0x2029)) {
			TextLineEnding ending = TextLineEnding::Lf;
			if (character == QLatin1Char('\r')) {
				ending = TextLineEnding::Cr;
				if (index + 1 < decoded.size() && decoded[index + 1] == QLatin1Char('\n')) { ending = TextLineEnding::CrLf; ++index; }
			} else if (character == QChar(0x2029)) { ending = TextLineEnding::ParagraphSeparator; }
			document.lineEndings.push_back(ending);
			++counts[static_cast<int>(ending)];
			document.text += QLatin1Char('\n');
		} else {
			document.text += character;
			if (character.unicode() < 0x20 && character != QLatin1Char('\t') && character != QLatin1Char('\f')) {
				document.error = QCoreApplication::translate("TextDocument", "Binary control characters were found. This preview is read-only.");
			}
		}
	}
	int highest = 0;
	for (const auto ending : document.lineEndings) {
		if (counts[static_cast<int>(ending)] > highest) {
			highest = counts[static_cast<int>(ending)];
			document.preferredLineEnding = ending;
		}
	}
	return document;
}

TextFileDocument readTextFile(const QString& path)
{
	TextFileDocument document;
	document.path = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
	document.resolvedPath = QFileInfo(document.path).canonicalFilePath();
	QFile file(document.path);
	if (document.resolvedPath.isEmpty() || !QFileInfo(document.path).isFile() || !file.open(QIODevice::ReadOnly)) {
		document.error = QCoreApplication::translate("TextDocument", "The text file could not be opened: %1").arg(file.errorString());
		return document;
	}
	const QByteArray bytes = file.read(textDocumentByteLimit + 1);
	if (file.error() != QFileDevice::NoError) {
		document.error = QCoreApplication::translate("TextDocument", "The text file could not be read: %1").arg(file.errorString());
		return document;
	}
	const QString resolved = document.resolvedPath;
	const QString absolute = document.path;
	document = decodeTextFile(bytes);
	document.path = absolute;
	document.resolvedPath = resolved;
	return document;
}

bool encodeTextFile(const TextFileDocument& source, const QString& text, QByteArray* bytes, QString* error)
{
	const auto fail = [&](const QString& message) { if (error) { *error = message; } return false; };
	if (error) { error->clear(); }
	if (!bytes) { return fail(QCoreApplication::translate("TextDocument", "No output buffer was supplied.")); }
	bytes->clear();
	if (!source.editable()) { return fail(source.error.isEmpty() ? QCoreApplication::translate("TextDocument", "This document cannot be saved.") : source.error); }
	if (text.size() > textDocumentByteLimit) { return fail(QCoreApplication::translate("TextDocument", "The edited text exceeds the 4 MiB limit.")); }
	if (text == source.text) { *bytes = source.originalBytes; return true; }
	// Input is the normalized editor text, never a silent encoding conversion.
	if (text.contains(QLatin1Char('\r')) || text.contains(QChar(0x2029))) { return fail(QCoreApplication::translate("TextDocument", "Editor text must use LF paragraph boundaries.")); }
	const bool mixed = std::any_of(source.lineEndings.cbegin(), source.lineEndings.cend(),
		[&](TextLineEnding ending) { return ending != source.preferredLineEnding; });
	// Uniform files need no line correspondence table, even after large edits.
	const QVector<int> mapping = mixed ? sourceLineMapping(source.text, text) : QVector<int>();
	const auto lines = QStringView(text).split(QLatin1Char('\n'));
	QString restored;
	restored.reserve(text.size() + lines.size());
	for (qsizetype line = 0; line < lines.size(); ++line) {
		restored += lines[line];
		if (line + 1 < lines.size()) {
			const int original = mixed ? mapping[line] : -1;
			restored += endingText(original >= 0 && original < source.lineEndings.size()
				? source.lineEndings[original] : source.preferredLineEnding);
		}
	}
	QStringEncoder encoder(converterEncoding(source.encoding), QStringConverter::Flag::Stateless);
	QByteArray encoded = encoder(restored);
	if (encoder.hasError() || !restored.isValidUtf16()) { return fail(QCoreApplication::translate("TextDocument", "The edited text contains invalid Unicode and cannot be saved.")); }
	// This also rejects newly inserted binary controls and verifies the exact
	// decoder/encoder pair. No replacement characters can enter a saved file.
	if (source.byteOrderMark) {
		encoded.prepend(QByteArray::fromHex(source.encoding == TextEncoding::Utf8 ? "efbbbf"
			: source.encoding == TextEncoding::Utf16LittleEndian ? "fffe" : "feff"));
	}
	if (encoded.size() > textDocumentByteLimit) { return fail(QCoreApplication::translate("TextDocument", "The encoded file exceeds the 4 MiB limit.")); }
	const TextFileDocument verified = decodeTextFile(encoded);
	if (!verified.editable() || verified.text != text) { return fail(QCoreApplication::translate("TextDocument", "The edited text cannot be saved without changing its characters.")); }
	*bytes = encoded;
	return true;
}

bool encodeTextFileEdits(const TextFileDocument& source, const QVector<TextFileEdit>& edits,
	QByteArray* bytes, QString* text, QString* error)
{
	const auto fail = [&](const QString& message) { if (error) { *error = message; } return false; };
	if (error) { error->clear(); }
	if (!bytes) { return fail(QCoreApplication::translate("TextDocument", "No output buffer was supplied.")); }
	bytes->clear();
	if (!source.editable()) { return fail(source.error.isEmpty() ? QCoreApplication::translate("TextDocument", "This document cannot be saved.") : source.error); }
	QString normalized, restored;
	qsizetype cursor = 0;
	int endingIndex = 0;
	const auto sourceRange = [&](qsizetype end, bool keep) {
		while (cursor < end) {
			const QChar ch = source.text[cursor++];
			if (keep) { normalized += ch; }
			if (ch == QLatin1Char('\n')) {
				if (keep) { restored += endingText(source.lineEndings.value(endingIndex, source.preferredLineEnding)); }
				++endingIndex;
			} else if (keep) { restored += ch; }
		}
	};
	for (const auto& edit : edits) {
		if (edit.offset < cursor || edit.length < 0 || edit.offset > source.text.size() || edit.length > source.text.size() - edit.offset) {
			return fail(QCoreApplication::translate("TextDocument", "Text edits must stay inside the reviewed document."));
		}
		const qsizetype end = edit.offset + edit.length;
		if ((edit.offset < source.text.size() && source.text[edit.offset].isLowSurrogate())
			|| (end < source.text.size() && source.text[end].isLowSurrogate())
			|| !edit.replacement.isValidUtf16() || edit.replacement.contains(QLatin1Char('\r')) || edit.replacement.contains(QChar(0x2029))) {
			return fail(QCoreApplication::translate("TextDocument", "Text edits must use ordered, non-overlapping Unicode ranges and LF paragraph boundaries."));
		}
		sourceRange(edit.offset, true);
		sourceRange(end, false);
		normalized += edit.replacement;
		QString inserted = edit.replacement;
		inserted.replace(QStringLiteral("\n"), endingText(source.preferredLineEnding));
		restored += inserted;
		if (normalized.size() > textDocumentByteLimit || restored.size() > textDocumentByteLimit * 2) {
			return fail(QCoreApplication::translate("TextDocument", "The edited text exceeds the 4 MiB limit."));
		}
	}
	sourceRange(source.text.size(), true);
	QStringEncoder encoder(converterEncoding(source.encoding), QStringConverter::Flag::Stateless);
	QByteArray encoded = encoder(restored);
	if (source.byteOrderMark) { encoded.prepend(QByteArray::fromHex(source.encoding == TextEncoding::Utf8 ? "efbbbf" : source.encoding == TextEncoding::Utf16LittleEndian ? "fffe" : "feff")); }
	if (encoder.hasError() || encoded.size() > textDocumentByteLimit || normalized.size() > textDocumentByteLimit) {
		return fail(QCoreApplication::translate("TextDocument", "The edited file exceeds its size limit or contains invalid Unicode."));
	}
	const auto verified = decodeTextFile(encoded);
	if (!verified.editable() || verified.text != normalized) { return fail(QCoreApplication::translate("TextDocument", "The edited text cannot be saved without changing its characters.")); }
	*bytes = std::move(encoded);
	if (text) { *text = std::move(normalized); }
	return true;
}

static TextFileSaveResult commitTextFileBytes(const TextFileDocument& source, const QByteArray& bytes, bool dryRun, const QByteArray& expectedSha256, bool recreateMissing)
{
	TextFileSaveResult result;
	result.dryRun = dryRun;
	const QByteArray expected = expectedSha256.isEmpty() ? source.sha256 : expectedSha256;
	const auto checkSource = [&]() {
		const auto current = readTextFile(source.path);
		const QFileInfo pathInfo(source.path);
		const QString parent = pathInfo.dir().canonicalPath();
		result.canRecreate = !source.path.isEmpty() && !source.resolvedPath.isEmpty() && !parent.isEmpty()
			&& !pathInfo.exists() && !pathInfo.isSymLink() && !pathInfo.isJunction()
			&& sameResolvedFile(QDir(parent).filePath(pathInfo.fileName()), source.resolvedPath);
		result.currentSha256 = current.readable && !current.truncated ? current.sha256 : QByteArray();
		if (recreateMissing) {
			if (result.canRecreate) { return true; }
			result.conflict = true;
			result.error = QCoreApplication::translate("TextDocument", "The file reappeared or its parent path changed. It has not been recreated.");
			return false;
		}
		if (source.path.isEmpty() || source.resolvedPath.isEmpty() || !current.readable || current.truncated
			|| !sameResolvedFile(source.resolvedPath, current.resolvedPath) || current.sha256 != expected) {
			result.conflict = true;
			result.error = QCoreApplication::translate("TextDocument", "The file changed, moved, or became unreadable after it was opened. Your edits have not been written.");
			return false;
		}
		return true;
	};
	if (!checkSource()) { return result; }
	result.outputSha256 = hash(bytes);
	result.changed = result.outputSha256 != result.currentSha256;
	if (!dryRun && result.changed) {
		// Use the original resolved path so an existing symbolic link itself is
		// preserved. A changed resolution is a conflict, even if bytes match.
		QSaveFile output(source.resolvedPath);
		if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size()) {
			result.error = QCoreApplication::translate("TextDocument", "The file could not be written: %1").arg(output.errorString());
			return result;
		}
		if (!checkSource()) { output.cancelWriting(); return result; }
		if (!output.commit()) { result.error = QCoreApplication::translate("TextDocument", "The file could not be committed: %1").arg(output.errorString()); return result; }
		result.bytesWritten = bytes.size();
	}
	result.succeeded = true;
	result.outputBytes = bytes;
	return result;
}

TextFileSaveResult saveTextFile(const TextFileDocument& source, const QString& text, bool dryRun, const QByteArray& expectedSha256, bool recreateMissing)
{
	TextFileSaveResult result; result.dryRun = dryRun; QByteArray bytes;
	if (!encodeTextFile(source, text, &bytes, &result.error)) { return result; }
	return commitTextFileBytes(source, bytes, dryRun, expectedSha256, recreateMissing);
}

TextFileSaveResult saveTextFileEdits(const TextFileDocument& source, const QVector<TextFileEdit>& edits, bool dryRun, const QByteArray& expectedSha256)
{
	TextFileSaveResult result; result.dryRun = dryRun; QByteArray bytes;
	if (!encodeTextFileEdits(source, edits, &bytes, nullptr, &result.error)) { return result; }
	return commitTextFileBytes(source, bytes, dryRun, expectedSha256, false);
}

TextFileWriteTarget inspectTextWriteTarget(const QString& path)
{
	TextFileWriteTarget target;
	if (path.trimmed().isEmpty()) {
		target.error = QCoreApplication::translate("TextDocument", "Choose a destination file.");
		return target;
	}
	const QFileInfo info(path);
	target.path = QDir::cleanPath(info.absoluteFilePath());
	target.existed = info.exists();
	if (target.existed) {
		const TextFileDocument current = readTextFile(target.path);
		if (!current.readable || current.truncated) {
			target.error = QCoreApplication::translate("TextDocument", "The destination cannot be inspected safely: %1").arg(current.error);
			return target;
		}
		target.resolvedPath = current.resolvedPath;
		target.sha256 = current.sha256;
	} else {
		const QString parent = info.dir().canonicalPath();
		if (parent.isEmpty() || info.isSymLink() || info.isJunction()) {
			target.error = QCoreApplication::translate("TextDocument", "The destination folder must exist and the file must not be a broken link.");
			return target;
		}
		target.resolvedPath = QDir(parent).filePath(info.fileName());
	}
	return target;
}

TextFileSaveResult saveTextFileAs(const TextFileDocument& source, const QString& text,
	const TextFileWriteTarget& target, bool dryRun)
{
	TextFileSaveResult result;
	result.dryRun = dryRun;
	if (!target.isValid()) {
		result.error = target.error.isEmpty() ? QCoreApplication::translate("TextDocument", "The destination has not been inspected.") : target.error;
		return result;
	}
	QByteArray bytes;
	if (!encodeTextFile(source, text, &bytes, &result.error)) { return result; }
	const auto checkTarget = [&]() {
		const auto current = inspectTextWriteTarget(target.path);
		result.currentSha256 = current.sha256;
		if (!current.isValid() || current.existed != target.existed || current.sha256 != target.sha256
			|| !sameResolvedFile(current.resolvedPath, target.resolvedPath)) {
			result.conflict = true;
			result.error = QCoreApplication::translate("TextDocument", "The destination changed after it was reviewed. Your document has not been written.");
			return false;
		}
		return true;
	};
	if (!checkTarget()) { return result; }
	result.outputSha256 = hash(bytes);
	result.changed = !target.existed || result.outputSha256 != target.sha256;
	if (!dryRun && result.changed) {
		QSaveFile output(target.resolvedPath);
		if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size()) {
			result.error = QCoreApplication::translate("TextDocument", "The file could not be written: %1").arg(output.errorString());
			return result;
		}
		if (!checkTarget()) { output.cancelWriting(); return result; }
		if (!output.commit()) {
			result.error = QCoreApplication::translate("TextDocument", "The file could not be committed: %1").arg(output.errorString());
			return result;
		}
		result.bytesWritten = bytes.size();
	}
	result.succeeded = true;
	result.outputBytes = bytes;
	return result;
}

QString textEncodingName(TextEncoding encoding)
{
	switch (encoding) {
	case TextEncoding::Utf16LittleEndian: return QStringLiteral("UTF-16 LE");
	case TextEncoding::Utf16BigEndian: return QStringLiteral("UTF-16 BE");
	case TextEncoding::Utf8: return QStringLiteral("UTF-8");
	}
	return QStringLiteral("UTF-8");
}

QString textLineEndingName(TextLineEnding ending)
{
	switch (ending) {
	case TextLineEnding::CrLf: return QStringLiteral("CRLF");
	case TextLineEnding::Cr: return QStringLiteral("CR");
	case TextLineEnding::ParagraphSeparator: return QStringLiteral("U+2029");
	case TextLineEnding::Lf: return QStringLiteral("LF");
	}
	return QStringLiteral("LF");
}

QString textFileFormatLabel(const TextFileDocument& document)
{
	QStringList endings;
	for (const auto ending : document.lineEndings) {
		const QString label = textLineEndingName(ending);
		if (!endings.contains(label)) { endings << label; }
	}
	const QString encoding = document.byteOrderMark ? QCoreApplication::translate("TextDocument", "%1 with BOM").arg(textEncodingName(document.encoding)) : textEncodingName(document.encoding);
	const QString newline = endings.isEmpty() ? QCoreApplication::translate("TextDocument", "No line endings")
		: endings.size() == 1 ? endings.first() : QCoreApplication::translate("TextDocument", "Mixed: %1").arg(endings.join(QStringLiteral("/")));
	return QCoreApplication::translate("TextDocument", "%1 · %2").arg(encoding, newline);
}

} // namespace vibestudio
