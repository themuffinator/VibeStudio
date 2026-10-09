#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

namespace vibestudio {

struct QuakeMapPreflightIssue {
	QString issueId;
	QString clusterId;
	QString severity;
	QString message;
	QString entityClassName;
	QString key;
	int line = 0;
};

struct QuakeMapPreflightReport {
	QVector<QuakeMapPreflightIssue> issues;

	[[nodiscard]] bool hasIssues() const;
	[[nodiscard]] QStringList warningMessages() const;
};

enum class QuakeMapPreflightSeverity {
	Info,
	Warning,
	Error,
};

struct QuakeMapPreflightOptions {
	QString mapPath;
	bool regionCompile = false;
	int longValueWarningThreshold = 1024;
	// Callbacks run on the caller's thread. Cancelled reports contain no
	// partial diagnostics; inspect cancelled/parseComplete before using them.
	std::function<bool()> isCancelled {};
	std::function<void(qint64 completed, qint64 total)> progress {};
};

struct QuakeMapPreflightWarning {
	QuakeMapPreflightSeverity severity = QuakeMapPreflightSeverity::Warning;
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

struct QuakeMapPreflightResult {
	QVector<QuakeMapPreflightWarning> warnings;
	int entityCount = 0;
	int brushEntityCount = 0;
	bool parseComplete = true;
	bool cancelled = false;
};

QString quakeMapPreflightSeverityId(QuakeMapPreflightSeverity severity);
QuakeMapPreflightResult validateQuakeMapPreflightText(const QString& mapText, const QuakeMapPreflightOptions& options = {});
QuakeMapPreflightResult validateQuakeMapPreflightFile(const QString& mapPath, const QuakeMapPreflightOptions& options = {});
QuakeMapPreflightReport inspectQuakeMapPreflightText(const QString& mapText, const QString& mapPath = QString());
QuakeMapPreflightReport inspectQuakeMapPreflightFile(const QString& mapPath, QString* error = nullptr);

} // namespace vibestudio
