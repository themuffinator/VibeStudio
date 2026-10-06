#pragma once

#include "core/text_document.h"
#include <QJsonValue>

namespace vibestudio {

struct LanguageFormattingOptions {
	int tabSize = 4;
	bool insertSpaces = false;
	int rangeOffset = -1; // -1 means the whole document; otherwise a nonempty range.
	int rangeLength = 0;
};

struct LanguageFormatting {
	QString filePath;
	int version = 0;
	QByteArray sourceSha256;
	QString error;
	// Ascending, disjoint normalized UTF-16 edits. Same-position insertions are
	// coalesced in protocol order. Invalid replies never expose partial edits.
	QVector<TextFileEdit> edits;
};

// Original implementation of LSP 3.17 formatting/TextEdit interface facts
// (Microsoft, CC-BY-4.0; reviewed 2026-10-04). No source or prose copied.
// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_formatting
// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_rangeFormatting
bool validLanguageFormattingOptions(const QString& source, const LanguageFormattingOptions& options);
LanguageFormatting parseLanguageFormatting(const QJsonValue& response, const QString& source);
bool previewLanguageFormatting(const QString& source, const LanguageFormatting& report, QString* text, QString* error = nullptr);
// Preserve the position relative to a changed span, clamping to its replacement.
int languageFormattedOffset(const LanguageFormatting& report, const QString& formatted, int offset);

} // namespace vibestudio
