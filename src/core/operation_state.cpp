#include "core/operation_state.h"

#include <algorithm>

namespace vibestudio {

namespace {

QDateTime nowUtc()
{
	return QDateTime::currentDateTimeUtc();
}

QString normalizedId(const QString& id)
{
	return id.trimmed().toLower().replace('_', '-');
}

} // namespace

QString OperationStateModel::createTask(const QString& title, const QString& detail, const QString& source, OperationState initialState, bool cancellable)
{
	OperationTask task;
	task.id = QStringLiteral("task-%1").arg(m_nextId++);
	task.title = title.trimmed().isEmpty() ? QStringLiteral("Untitled Operation") : title.trimmed();
	task.detail = detail.trimmed();
	task.source = source.trimmed();
	task.state = initialState;
	task.cancellable = cancellable && operationStateAllowsCancellation(initialState);
	task.createdUtc = nowUtc();
	task.updatedUtc = task.createdUtc;
	task.log.push_back({task.createdUtc, initialState, QStringLiteral("Operation created: %1").arg(task.title)});
	task.transitions.push_back({task.createdUtc, initialState, 0, QStringLiteral("Operation created: %1").arg(task.title)});
	m_tasks.prepend(task);
	return task.id;
}

bool OperationStateModel::contains(const QString& taskId) const
{
	return std::any_of(m_tasks.cbegin(), m_tasks.cend(), [&taskId](const OperationTask& task) {
		return task.id == taskId;
	});
}

OperationTask OperationStateModel::task(const QString& taskId) const
{
	for (const OperationTask& task : m_tasks) {
		if (task.id == taskId) {
			return task;
		}
	}
	return {};
}

QVector<OperationTask> OperationStateModel::tasks() const
{
	return m_tasks;
}

bool OperationStateModel::transitionTask(const QString& taskId, OperationState state, const QString& message)
{
	OperationTask* task = findTask(taskId);
	if (!task) {
		return false;
	}

	touch(*task, state);
	task->cancellable = task->cancellable && operationStateAllowsCancellation(state);
	if (!message.trimmed().isEmpty()) {
		task->log.push_back({task->updatedUtc, state, message.trimmed()});
	}
	recordTransition(*task, state, message);
	return true;
}

bool OperationStateModel::setProgress(const QString& taskId, int current, int total, const QString& message)
{
	OperationTask* task = findTask(taskId);
	if (!task) {
		return false;
	}

	task->progress.total = std::max(0, total);
	task->progress.current = std::clamp(current, 0, task->progress.total);
	touch(*task, task->state);
	if (!message.trimmed().isEmpty()) {
		task->log.push_back({task->updatedUtc, task->state, message.trimmed()});
	}
	return true;
}

bool OperationStateModel::appendLog(const QString& taskId, OperationState state, const QString& message)
{
	OperationTask* task = findTask(taskId);
	if (!task || message.trimmed().isEmpty()) {
		return false;
	}

	touch(*task, state);
	task->log.push_back({task->updatedUtc, state, message.trimmed()});
	recordTransition(*task, state, message);
	return true;
}

bool OperationStateModel::appendWarning(const QString& taskId, const QString& warning)
{
	OperationTask* task = findTask(taskId);
	if (!task || warning.trimmed().isEmpty()) {
		return false;
	}

	task->warnings.push_back(warning.trimmed());
	touch(*task, OperationState::Warning);
	task->log.push_back({task->updatedUtc, OperationState::Warning, warning.trimmed()});
	recordTransition(*task, OperationState::Warning, warning);
	return true;
}

bool OperationStateModel::completeTask(const QString& taskId, const QString& resultSummary)
{
	OperationTask* task = findTask(taskId);
	if (!task) {
		return false;
	}

	task->resultSummary = resultSummary.trimmed();
	task->progress.total = task->progress.total > 0 ? task->progress.total : 1;
	task->progress.current = task->progress.total;
	touch(*task, task->warnings.isEmpty() ? OperationState::Completed : OperationState::Warning);
	task->finishedUtc = task->updatedUtc;
	task->cancellable = false;
	if (!task->resultSummary.isEmpty()) {
		task->log.push_back({task->updatedUtc, task->state, task->resultSummary});
	}
	recordTransition(*task, task->state, task->resultSummary);
	return true;
}

bool OperationStateModel::failTask(const QString& taskId, const QString& resultSummary)
{
	OperationTask* task = findTask(taskId);
	if (!task) {
		return false;
	}

	task->resultSummary = resultSummary.trimmed();
	touch(*task, OperationState::Failed);
	task->finishedUtc = task->updatedUtc;
	task->cancellable = false;
	if (!task->resultSummary.isEmpty()) {
		task->log.push_back({task->updatedUtc, OperationState::Failed, task->resultSummary});
	}
	recordTransition(*task, OperationState::Failed, task->resultSummary);
	return true;
}

bool OperationStateModel::cancelTask(const QString& taskId, const QString& resultSummary)
{
	OperationTask* task = findTask(taskId);
	if (!task || !operationStateAllowsCancellation(task->state)) {
		return false;
	}

	task->resultSummary = resultSummary.trimmed().isEmpty() ? QStringLiteral("Operation cancelled.") : resultSummary.trimmed();
	touch(*task, OperationState::Cancelled);
	task->finishedUtc = task->updatedUtc;
	task->cancellable = false;
	task->log.push_back({task->updatedUtc, OperationState::Cancelled, task->resultSummary});
	recordTransition(*task, OperationState::Cancelled, task->resultSummary);
	return true;
}

bool OperationStateModel::clearTerminalTasks()
{
	const qsizetype oldSize = m_tasks.size();
	m_tasks.erase(
		std::remove_if(m_tasks.begin(), m_tasks.end(), [](const OperationTask& task) {
			return operationStateIsTerminal(task.state);
		}),
		m_tasks.end());
	return m_tasks.size() != oldSize;
}

QMap<OperationState, int> OperationStateModel::stateCounts() const
{
	QMap<OperationState, int> counts;
	for (const OperationTask& task : m_tasks) {
		counts[task.state] += 1;
	}
	return counts;
}

QString OperationStateModel::summaryText() const
{
	if (m_tasks.isEmpty()) {
		return QStringLiteral("No activity yet.");
	}

	const QMap<OperationState, int> counts = stateCounts();
	QStringList parts;
	for (OperationState state : {OperationState::Queued, OperationState::Loading, OperationState::Running, OperationState::Warning, OperationState::Failed, OperationState::Cancelled, OperationState::Completed, OperationState::Idle}) {
		const int count = counts.value(state, 0);
		if (count > 0) {
			parts.push_back(QStringLiteral("%1 %2").arg(count).arg(operationStateDisplayName(state).toLower()));
		}
	}
	return parts.join(QStringLiteral(", "));
}

OperationTask* OperationStateModel::findTask(const QString& taskId)
{
	for (OperationTask& task : m_tasks) {
		if (task.id == taskId) {
			return &task;
		}
	}
	return nullptr;
}

void OperationStateModel::touch(OperationTask& task, OperationState state)
{
	task.state = state;
	task.updatedUtc = nowUtc();
	task.durationMs = operationTaskElapsedMs(task, task.updatedUtc);
	if (operationStateIsTerminal(state)) {
		task.finishedUtc = task.updatedUtc;
	}
}

void OperationStateModel::recordTransition(OperationTask& task, OperationState state, const QString& message)
{
	const QString trimmed = message.trimmed();
	if (!task.transitions.isEmpty()) {
		const OperationStateTransition& previous = task.transitions.constLast();
		if (previous.state == state && previous.message == trimmed) {
			return;
		}
	}
	task.transitions.push_back({task.updatedUtc, state, task.durationMs, trimmed});
}

QString operationStateId(OperationState state)
{
	switch (state) {
	case OperationState::Idle:
		return QStringLiteral("idle");
	case OperationState::Queued:
		return QStringLiteral("queued");
	case OperationState::Loading:
		return QStringLiteral("loading");
	case OperationState::Running:
		return QStringLiteral("running");
	case OperationState::Warning:
		return QStringLiteral("warning");
	case OperationState::Failed:
		return QStringLiteral("failed");
	case OperationState::Cancelled:
		return QStringLiteral("cancelled");
	case OperationState::Completed:
		return QStringLiteral("completed");
	}
	return QStringLiteral("idle");
}

QString operationStateDisplayName(OperationState state)
{
	switch (state) {
	case OperationState::Idle:
		return QStringLiteral("Idle");
	case OperationState::Queued:
		return QStringLiteral("Queued");
	case OperationState::Loading:
		return QStringLiteral("Loading");
	case OperationState::Running:
		return QStringLiteral("Running");
	case OperationState::Warning:
		return QStringLiteral("Warning");
	case OperationState::Failed:
		return QStringLiteral("Failed");
	case OperationState::Cancelled:
		return QStringLiteral("Cancelled");
	case OperationState::Completed:
		return QStringLiteral("Completed");
	}
	return QStringLiteral("Idle");
}

OperationState operationStateFromId(const QString& id)
{
	const QString normalized = normalizedId(id);
	if (normalized == QStringLiteral("queued")) {
		return OperationState::Queued;
	}
	if (normalized == QStringLiteral("loading")) {
		return OperationState::Loading;
	}
	if (normalized == QStringLiteral("running")) {
		return OperationState::Running;
	}
	if (normalized == QStringLiteral("warning")) {
		return OperationState::Warning;
	}
	if (normalized == QStringLiteral("failed")) {
		return OperationState::Failed;
	}
	if (normalized == QStringLiteral("cancelled") || normalized == QStringLiteral("canceled")) {
		return OperationState::Cancelled;
	}
	if (normalized == QStringLiteral("completed") || normalized == QStringLiteral("complete")) {
		return OperationState::Completed;
	}
	return OperationState::Idle;
}

QStringList operationStateIds()
{
	return {
		operationStateId(OperationState::Idle),
		operationStateId(OperationState::Queued),
		operationStateId(OperationState::Loading),
		operationStateId(OperationState::Running),
		operationStateId(OperationState::Warning),
		operationStateId(OperationState::Failed),
		operationStateId(OperationState::Cancelled),
		operationStateId(OperationState::Completed),
	};
}

bool operationStateIsTerminal(OperationState state)
{
	return state == OperationState::Warning
		|| state == OperationState::Failed
		|| state == OperationState::Cancelled
		|| state == OperationState::Completed;
}

bool operationStateAllowsCancellation(OperationState state)
{
	return state == OperationState::Queued || state == OperationState::Loading || state == OperationState::Running;
}

int operationProgressPercent(const OperationProgress& progress)
{
	if (progress.total <= 0) {
		return 0;
	}
	return std::clamp((progress.current * 100) / progress.total, 0, 100);
}

qint64 operationTaskElapsedMs(const OperationTask& task, const QDateTime& atUtc)
{
	if (!task.createdUtc.isValid()) {
		return 0;
	}
	const QDateTime endUtc = atUtc.isValid()
		? atUtc.toUTC()
		: (task.finishedUtc.isValid() ? task.finishedUtc.toUTC() : task.updatedUtc.toUTC());
	return std::max<qint64>(0, task.createdUtc.toUTC().msecsTo(endUtc));
}

QString operationTaskTimelineText(const OperationTask& task)
{
	QStringList lines;
	for (const OperationStateTransition& transition : task.transitions) {
		const QString timestamp = transition.timestampUtc.isValid()
			? transition.timestampUtc.toUTC().toString(Qt::ISODate)
			: QStringLiteral("unknown-time");
		const QString message = transition.message.trimmed().isEmpty()
			? operationStateDisplayName(transition.state)
			: transition.message.trimmed();
		lines << QStringLiteral("%1 +%2 ms [%3] %4").arg(timestamp).arg(transition.elapsedMs).arg(operationStateId(transition.state), message);
	}
	return lines.join('\n');
}

} // namespace vibestudio
