#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

struct EricwMapPreflightIssue {
	QString issueId;
	QString clusterId;
	QString severity;
	QString message;
	QString entityClassName;
	QString key;
	int line = 0;
};

struct EricwMapPreflightReport {
	QVector<EricwMapPreflightIssue> issues;

	[[nodiscard]] bool hasIssues() const;
	[[nodiscard]] QStringList warningMessages() const;
};

enum class EricwMapPreflightSeverity {
	Info,
	Warning,
	Error,
};

struct EricwMapPreflightOptions {
	QString mapPath;
	bool regionCompile = false;
	int longValueWarningThreshold = 1024;
};

struct EricwMapPreflightWarning {
	EricwMapPreflightSeverity severity = EricwMapPreflightSeverity::Warning;
	QString code;
	QString message;
	QString upstreamIssue;
	QString filePath;
	int line = 0;
	int column = 0;
	int entityIndex = -1;
	QString classname;
	QString key;
};

struct EricwMapPreflightResult {
	QVector<EricwMapPreflightWarning> warnings;
	int entityCount = 0;
	int brushEntityCount = 0;
	bool parseComplete = true;
};

QString ericwMapPreflightSeverityId(EricwMapPreflightSeverity severity);
EricwMapPreflightResult validateEricwMapPreflightText(const QString& mapText, const EricwMapPreflightOptions& options = {});
EricwMapPreflightResult validateEricwMapPreflightFile(const QString& mapPath, const EricwMapPreflightOptions& options = {});
EricwMapPreflightReport inspectEricwMapPreflightText(const QString& mapText, const QString& mapPath = QString());
EricwMapPreflightReport inspectEricwMapPreflightFile(const QString& mapPath, QString* error = nullptr);

} // namespace vibestudio
