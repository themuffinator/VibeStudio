#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

struct CompletionSnippetStop {
	int number = 0;
	int offset = 0;
	int length = 0;
	int parent = -1;
	QStringList choices;
};
struct CompletionSnippet {
	QString text, error;
	QVector<CompletionSnippetStop> stops;
	int finalOffset = 0;
};

// Original implementation of LSP 3.17 snippet syntax facts, Microsoft CC-BY-4.0,
// reviewed 2026-10-04. No upstream implementation or prose copied.
// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#snippet_syntax
// No filesystem, environment, clipboard or executable access. Regex transforms
// are explicitly unsupported. All ranges count UTF-16 units in expanded text.
CompletionSnippet expandCompletionSnippet(const QString& body, const QHash<QString, QString>& variables = {});
QHash<QString, QString> completionSnippetVariables(const QString& source, int caret, const QString& filePath);
bool validCompletionSnippetStops(const QString& text, const QVector<CompletionSnippetStop>& stops);

} // namespace vibestudio
