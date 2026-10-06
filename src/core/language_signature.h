#pragma once

#include "core/language_hover.h"
#include <QJsonObject>

namespace vibestudio {

struct LanguageSignatureParameter {
	QString label;
	int offset = 0, length = 0; // Validated UTF-16 range within the signature label.
	LanguageHoverPart documentation;
};

struct LanguageSignatureInformation {
	QString label;
	LanguageHoverPart documentation;
	QVector<LanguageSignatureParameter> parameters;
	int activeParameter = -1;
};

struct LanguageSignatureHelp {
	QString filePath;
	int version = 0;
	QByteArray sourceSha256;
	int requestOffset = 0;
	QVector<LanguageSignatureInformation> signatures;
	int activeSignature = -1;
	bool limited = false;
	int skipped = 0;
	QString error;
};

// Original implementation of Microsoft's LSP 3.17 signature-help interface
// facts (CC-BY-4.0, reviewed 2026-10-04); no copied source or prose.
// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_signatureHelp
// At most 32 signatures, 128 parameters each and 65536 retained UTF-16 units.
LanguageSignatureHelp parseLanguageSignatureHelp(const QJsonValue& response, const QString& source, int line, int character);
// Rebuild the bounded, validated active help for a retrigger request. Indices
// refer to the retained arrays, including the user's selected overload.
QJsonObject languageSignatureContext(const LanguageSignatureHelp& report);

} // namespace vibestudio
