#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include "core/completion_snippet.h"

namespace vibestudio {

struct LanguageCompletionEdit {
	int offset = 0;
	int length = 0;
	QString text;
};

struct LanguageCompletionItem {
	QString label, detail, documentation, filterText, sortText;
	int kind = 1;
	bool deprecated = false;
	bool filterable = true;
	bool needsResolve = false;
	bool snippet = false;
	QString sourceFilePath;
	// Absolute ranges in the fully edited preview, including related imports.
	QVector<CompletionSnippetStop> tabStops;
	// Effective protocol item, including inherited list defaults and opaque data.
	QJsonObject wire;
	QByteArray sourceSha256;
	int requestOffset = 0;
	int caret = 0;
	// Validated, non-overlapping UTF-16 edits, descending by source offset.
	QVector<LanguageCompletionEdit> edits;
};

struct LanguageCompletions {
	QString filePath;
	int version = 0;
	bool incomplete = false;
	bool limited = false;
	int skipped = 0;
	QString error;
	QVector<LanguageCompletionItem> items;
};

// Original implementation of LSP 3.17 completion interface facts (Microsoft,
// CC-BY-4.0, reviewed 2026-10-04). No upstream code or prose copied.
// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_completion
LanguageCompletions parseLanguageCompletions(const QJsonValue& response, const QString& source, int line, int character, bool resolveSupported = false, const QString& filePath = {});
LanguageCompletionItem resolveLanguageCompletion(const LanguageCompletionItem& original, const QJsonValue& response, const QString& source, QString* error);
// Pure preview/validation used before the GUI's one-block undoable edit.
// Never writes a source file. A changed source hash rejects the whole item.
bool previewLanguageCompletion(const QString& source, const LanguageCompletionItem& item, QString* text, int* caret, QString* error);
int languageSourceOffset(const QString& source, int line, int character);
// Start of the identifier prefix ending at a UTF-16 caret boundary.
int languageWordStart(const QString& source, int caret);

} // namespace vibestudio
