#pragma once

#include <QDateTime>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

enum class OperationState {
	Idle,
	Queued,
	Loading,
	Running,
	Warning,
	Failed,
	Cancelled,
	Completed,
};

struct OperationProgress {
	int current = 0;
	int total = 0;
};

struct OperationLogEntry {
	QDateTime timestampUtc;
	OperationState state = OperationState::Idle;
	QString message;
};

struct OperationStateTransition {
	QDateTime timestampUtc;
	OperationState state = OperationState::Idle;
	qint64 elapsedMs = 0;
	QString message;
};

struct OperationTask {
	QString id;
	QString title;
	QString detail;
	QString source;
	OperationState state = OperationState::Idle;
	OperationProgress progress;
	QString resultSummary;
	QStringList warnings;
	QVector<OperationLogEntry> log;
	QVector<OperationStateTransition> transitions;
	bool cancellable = false;
	QDateTime createdUtc;
	QDateTime updatedUtc;
	QDateTime finishedUtc;
	qint64 durationMs = 0;
};

class OperationStateModel final {
public:
	QString createTask(const QString& title, const QString& detail, const QString& source, OperationState initialState = OperationState::Queued, bool cancellable = false);
	bool contains(const QString& taskId) const;
	OperationTask task(const QString& taskId) const;
	QVector<OperationTask> tasks() const;

	bool transitionTask(const QString& taskId, OperationState state, const QString& message = QString());
	bool setProgress(const QString& taskId, int current, int total, const QString& message = QString());
	bool appendLog(const QString& taskId, OperationState state, const QString& message);
	bool appendWarning(const QString& taskId, const QString& warning);
	bool completeTask(const QString& taskId, const QString& resultSummary);
	bool failTask(const QString& taskId, const QString& resultSummary);
	bool cancelTask(const QString& taskId, const QString& resultSummary = QString());
	bool clearTerminalTasks();

	QMap<OperationState, int> stateCounts() const;
	QString summaryText() const;

private:
	OperationTask* findTask(const QString& taskId);
	void touch(OperationTask& task, OperationState state);
	void recordTransition(OperationTask& task, OperationState state, const QString& message);

	QVector<OperationTask> m_tasks;
	int m_nextId = 1;
};

QString operationStateId(OperationState state);
QString operationStateDisplayName(OperationState state);
OperationState operationStateFromId(const QString& id);
QStringList operationStateIds();
bool operationStateIsTerminal(OperationState state);
bool operationStateAllowsCancellation(OperationState state);
int operationProgressPercent(const OperationProgress& progress);
qint64 operationTaskElapsedMs(const OperationTask& task, const QDateTime& atUtc = QDateTime());
QString operationTaskTimelineText(const OperationTask& task);

} // namespace vibestudio
