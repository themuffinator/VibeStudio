#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace vibestudio {

enum class TextEncoding { Utf8, Utf16LittleEndian, Utf16BigEndian };
enum class TextLineEnding { Lf, CrLf, Cr, ParagraphSeparator };

inline constexpr qint64 textDocumentByteLimit = 4ll * 1024 * 1024;

// The source snapshot is immutable while editing. Text uses LF for paragraph
// boundaries; soft Unicode line separators and non-breaking spaces stay intact.
struct TextFileDocument {
	QString path;
	QString resolvedPath;
	QString text;
	QByteArray originalBytes;
	QByteArray sha256;
	TextEncoding encoding = TextEncoding::Utf8;
	bool byteOrderMark = false;
	QVector<TextLineEnding> lineEndings;
	TextLineEnding preferredLineEnding = TextLineEnding::Lf;
	bool readable = false;
	bool truncated = false;
	QString error;
	[[nodiscard]] bool editable() const { return readable && error.isEmpty() && !truncated; }
};

struct TextFileSaveResult {
	bool succeeded = false;
	bool dryRun = true;
	bool changed = false;
	bool conflict = false;
	bool canRecreate = false;
	qint64 bytesWritten = 0;
	QByteArray currentSha256;
	QByteArray outputSha256;
	QByteArray outputBytes;
	QString error;
};

// Inspect before asking to overwrite. The destination snapshot is independent
// of the source: Save As must still work if the original file disappeared.
struct TextFileWriteTarget {
	QString path;
	QString resolvedPath;
	bool existed = false;
	QByteArray sha256;
	QString error;
	[[nodiscard]] bool isValid() const { return !path.isEmpty() && !resolvedPath.isEmpty() && error.isEmpty(); }
};

// Non-overlapping edits in ascending, normalized UTF-16 document offsets.
struct TextFileEdit {
	qsizetype offset = 0;
	qsizetype length = 0;
	QString replacement;
};

TextFileDocument decodeTextFile(const QByteArray& bytes);
TextFileDocument readTextFile(const QString& path);
// Matched lines keep their separators. Changed runs reuse their corresponding
// separators; additional lines use the most common source ending (first on ties).
// Normalized text controls whether there is a final newline.
bool encodeTextFile(const TextFileDocument& source, const QString& text, QByteArray* bytes, QString* error = nullptr);
// Exact ranges preserve all untouched separators, including repeated lines.
// New paragraph boundaries use the source's preferred separator.
bool encodeTextFileEdits(const TextFileDocument& source, const QVector<TextFileEdit>& edits,
	QByteArray* bytes, QString* text = nullptr, QString* error = nullptr);
// Expected hash defaults to the source snapshot. An explicit hash permits a
// separately reviewed overwrite, still checked before the atomic commit.
// Recreating a missing source needs a snapshot and explicit approval. It never
// overwrites a file that reappears or follows a changed parent/link resolution.
TextFileSaveResult saveTextFile(const TextFileDocument& source, const QString& text, bool dryRun = true,
	const QByteArray& expectedSha256 = {}, bool recreateMissing = false);
// Same atomic/hash-guarded save boundary, with exact unchanged line separators.
TextFileSaveResult saveTextFileEdits(const TextFileDocument& source, const QVector<TextFileEdit>& edits, bool dryRun = true,
	const QByteArray& expectedSha256 = {});
TextFileWriteTarget inspectTextWriteTarget(const QString& path);
// Existing destinations require caller-reviewed approval of this snapshot.
// A missing destination must remain missing; a changed target is never adopted.
TextFileSaveResult saveTextFileAs(const TextFileDocument& source, const QString& text,
	const TextFileWriteTarget& target, bool dryRun = true);
QString textEncodingName(TextEncoding encoding);
QString textLineEndingName(TextLineEnding ending);
QString textFileFormatLabel(const TextFileDocument& document);

} // namespace vibestudio
