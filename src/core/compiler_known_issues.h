#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

enum class CompilerKnownIssueSeverity {
	Info,
	Warning,
	Error,
	Critical,
};

struct CompilerKnownIssueDescriptor {
	QString issueId;
	QString clusterId;
	CompilerKnownIssueSeverity severity = CompilerKnownIssueSeverity::Warning;
	bool highValue = false;
	QStringList affectedToolIds;
	QStringList affectedProfileIds;
	QStringList matchKeywords;
	QString upstreamUrl;
	QString summaryText;
	QString warningText;
	QString actionText;
};

struct CompilerKnownIssueMatch {
	CompilerKnownIssueDescriptor issue;
	QString matchedKeyword;
};

QString compilerKnownIssueSeverityId(CompilerKnownIssueSeverity severity);
QString compilerKnownIssueSeverityText(CompilerKnownIssueSeverity severity);

QVector<CompilerKnownIssueDescriptor> compilerKnownIssueDescriptors();
QStringList compilerKnownIssueIds();
bool compilerKnownIssueForId(const QString& issueId, CompilerKnownIssueDescriptor* out = nullptr);
QVector<CompilerKnownIssueDescriptor> compilerKnownIssuesForCluster(const QString& clusterId);
QVector<CompilerKnownIssueDescriptor> compilerKnownIssuesForTool(const QString& toolId);
QVector<CompilerKnownIssueDescriptor> compilerKnownIssuesForProfile(const QString& profileId);
QVector<CompilerKnownIssueMatch> matchCompilerKnownIssues(const QString& text, const QString& toolId = QString(), const QString& profileId = QString());
QString compilerKnownIssueText(const CompilerKnownIssueDescriptor& issue);
QStringList ericwKnownIssuePlanWarnings(const QString& profileId, const QString& inputPath, const QStringList& arguments);

} // namespace vibestudio
