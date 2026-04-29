#include "core/operation_state.h"

#include <QCoreApplication>

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message)
{
	std::cerr << message << "\n";
	return EXIT_FAILURE;
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);

	vibestudio::OperationStateModel model;
	if (model.summaryText() != QStringLiteral("No activity yet.")) {
		return fail("Expected empty activity summary.");
	}
	if (vibestudio::operationStateFromId(QStringLiteral("canceled")) != vibestudio::OperationState::Cancelled) {
		return fail("Expected operation state id normalization.");
	}
	if (!vibestudio::operationStateAllowsCancellation(vibestudio::OperationState::Queued) || !vibestudio::operationStateAllowsCancellation(vibestudio::OperationState::Loading) || vibestudio::operationStateAllowsCancellation(vibestudio::OperationState::Completed)) {
		return fail("Expected cancellation policy.");
	}

	const QString scanId = model.createTask(QStringLiteral("Package Scan"), QStringLiteral("Read package entries"), QStringLiteral("package"), vibestudio::OperationState::Queued, true);
	if (!model.contains(scanId) || model.task(scanId).state != vibestudio::OperationState::Queued) {
		return fail("Expected created queued task.");
	}
	model.transitionTask(scanId, vibestudio::OperationState::Running, QStringLiteral("Scan started."));
	model.transitionTask(scanId, vibestudio::OperationState::Loading, QStringLiteral("Entries loading."));
	model.setProgress(scanId, 5, 10, QStringLiteral("Halfway."));
	if (model.task(scanId).state != vibestudio::OperationState::Loading) {
		return fail("Expected loading state.");
	}
	if (model.task(scanId).progress.current != 5 || vibestudio::operationProgressPercent(model.task(scanId).progress) != 50) {
		return fail("Expected progress to be stored and normalized.");
	}
	model.appendWarning(scanId, QStringLiteral("Unknown lump type."));
	if (model.task(scanId).state != vibestudio::OperationState::Warning || model.task(scanId).warnings.size() != 1) {
		return fail("Expected warning state and warning list.");
	}
	model.completeTask(scanId, QStringLiteral("Scan completed with warnings."));
	if (model.task(scanId).state != vibestudio::OperationState::Warning || model.task(scanId).progress.current != model.task(scanId).progress.total) {
		return fail("Expected warning task to remain warning after completion.");
	}

	const QString compilerId = model.createTask(QStringLiteral("Compiler Probe"), QStringLiteral("Run q3map2 help"), QStringLiteral("compiler"), vibestudio::OperationState::Running, true);
	if (!model.cancelTask(compilerId, QStringLiteral("Probe cancelled by user."))) {
		return fail("Expected cancellable task to cancel.");
	}
	if (model.task(compilerId).state != vibestudio::OperationState::Cancelled || model.cancelTask(compilerId)) {
		return fail("Expected cancelled task to be terminal.");
	}

	const QString failedId = model.createTask(QStringLiteral("Validation"), QStringLiteral("Validate package"), QStringLiteral("qa"));
	model.failTask(failedId, QStringLiteral("Validation failed."));
	if (model.task(failedId).state != vibestudio::OperationState::Failed || model.task(failedId).finishedUtc.isNull()) {
		return fail("Expected failed terminal task.");
	}

	const QMap<vibestudio::OperationState, int> counts = model.stateCounts();
	if (counts.value(vibestudio::OperationState::Warning) != 1 || counts.value(vibestudio::OperationState::Cancelled) != 1 || counts.value(vibestudio::OperationState::Failed) != 1) {
		return fail("Expected state counts.");
	}
	if (!model.summaryText().contains(QStringLiteral("1 warning")) || !model.summaryText().contains(QStringLiteral("1 failed"))) {
		return fail("Expected summary text to include terminal states.");
	}
	if (!model.clearTerminalTasks() || !model.tasks().isEmpty()) {
		return fail("Expected terminal tasks to clear.");
	}

	return EXIT_SUCCESS;
}
