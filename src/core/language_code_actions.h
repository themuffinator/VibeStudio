#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace vibestudio {

struct LanguageCodeAction {
	QString title, kind, disabledReason;
	bool preferred = false, needsResolve = false;
	QJsonObject wire;
	bool available() const { return disabledReason.isEmpty() && !title.isEmpty(); }
};

struct LanguageCodeActions {
	QString filePath, error;
	int version = 0, offset = 0, length = 0, skipped = 0;
	bool limited = false;
	QByteArray sourceSha256;
	QVector<LanguageCodeAction> items;
};

bool validLanguageCodeActionRange(const QString& source, int offset, int length);
LanguageCodeActions parseLanguageCodeActions(const QJsonValue& reply, bool resolveSupported);
LanguageCodeAction resolveLanguageCodeAction(const LanguageCodeAction& original, const QJsonValue& reply);

} // namespace vibestudio
