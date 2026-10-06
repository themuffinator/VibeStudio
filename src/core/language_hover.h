#pragma once

#include <QByteArray>
#include <QJsonValue>
#include <QString>
#include <QVector>

namespace vibestudio {

struct LanguageHoverPart {
	QString kind; // plaintext, markdown, or code; never executable content.
	QString text;
	QString language;
};

struct LanguageHover {
	QString filePath;
	int version = 0;
	QByteArray sourceSha256;
	int requestOffset = 0;
	bool hasRange = false;
	int offset = 0, length = 0;
	bool limited = false;
	int skipped = 0;
	QString error;
	QVector<LanguageHoverPart> contents;
};

// Original LSP 3.17 hover interface implementation (Microsoft, CC-BY-4.0,
// reviewed 2026-10-04); only interface facts, no copied source or prose.
// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_hover
// Bounded to 32 parts and 65536 UTF-16 code units. Optional ranges must match
// the exact source snapshot and may not split Unicode surrogate pairs.
LanguageHover parseLanguageHover(const QJsonValue& response, const QString& source, int line, int character);

} // namespace vibestudio
