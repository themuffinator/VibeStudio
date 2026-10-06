#include "app/game_launch_task_dialog.h"
#include "core/studio_settings.h"
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QVBoxLayout>
#include <atomic>
#include <exception>

namespace vibestudio {
GameLaunchTaskDialog::GameLaunchTaskDialog(QWidget* parent) : QDialog(parent) {
	setObjectName(QStringLiteral("gameLaunchTaskDialog"));
	setWindowTitle(tr("Prepare Game Launch"));
	setAccessibleName(windowTitle());
	setWindowModality(Qt::WindowModal);
	setMinimumWidth(420);
}
void GameLaunchTaskDialog::reject() {
	if (m_cancel) {
		m_cancel();
	}
}
void GameLaunchTaskDialog::closeEvent(QCloseEvent* event) {
	reject();
	event->ignore();
}
GameLaunchTaskResult GameLaunchTaskDialog::run(QWidget* parent, bool starting,
											   const std::function<void(GameLaunchTaskResult&, const std::function<bool()>&)>& action) {
	GameLaunchTaskDialog dialog(parent);
	auto* layout = new QVBoxLayout(&dialog);
	auto* phase = new QLabel(starting ? tr("Checking the reviewed map before launch…") : tr("Checking map nodes and launch settings…"));
	phase->setObjectName(QStringLiteral("gameLaunchTaskPhase"));
	phase->setWordWrap(true);
	phase->setAccessibleName(tr("Launch preparation status"));
	layout->addWidget(phase);
	auto* bar = new QProgressBar;
	bar->setObjectName(QStringLiteral("gameLaunchTaskProgress"));
	bar->setAccessibleName(tr("Launch preparation progress"));
	bar->setRange(0, StudioSettings().accessibilityPreferences().reducedMotion ? 1 : 0);
	bar->setTextVisible(false);
	layout->addWidget(bar);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("gameLaunchTaskCancel"));
	buttons->button(QDialogButtonBox::Cancel)->setAccessibleName(tr("Cancel launch preparation"));
	buttons->button(QDialogButtonBox::Cancel)->setAccessibleDescription(tr("Stop validation before starting the game."));
	layout->addWidget(buttons);
	GameLaunchTaskResult result;
	std::atomic_bool cancelled{false};
	auto* worker = QThread::create([&] {
		try {
			action(result, [&] { return cancelled.load(); });
		} catch (const std::exception& error) {
			result.error = tr("Launch preparation failed: %1").arg(QString::fromUtf8(error.what()));
		} catch (...) {
			result.error = tr("Launch preparation failed.");
		}
	});
	dialog.m_cancel = [&] {
		cancelled = true;
		buttons->setEnabled(false);
		phase->setText(tr("Cancelling launch preparation…"));
	};
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &GameLaunchTaskDialog::reject);
	bool completed = false;
	QObject::connect(worker, &QThread::finished, &dialog, [&] {
		completed = true;
		dialog.accept();
	});
	worker->start();
	dialog.exec();
	if (!completed) {
		cancelled = true;
	}
	worker->wait();
	delete worker;
	// Never claim a process that already started was cancelled. Cancellation
	// is acknowledged before return; no background worker can launch it later.
	result.cancelled = cancelled && !result.started;
	if (result.cancelled) {
		result.plan.cancelled = true;
		result.plan.runnable = false;
	}
	if (!result.error.isEmpty()) {
		result.plan.errors << result.error;
		result.plan.runnable = false;
	}
	return result;
}
GameLaunchPlan GameLaunchTaskDialog::prepare(QWidget* parent, const GameLaunchRequest& request,
											 const GameInstallationProfile& installation) {
	return run(parent, false, [&](auto& result, const auto& cancel) { result.plan = buildGameLaunchPlan(request, installation, cancel); })
		.plan;
}
GameLaunchTaskResult GameLaunchTaskDialog::start(QWidget* parent, const GameLaunchPlan& plan) {
	return run(parent, true,
			   [&](auto& result, const auto& cancel) { result.started = startGameLaunch(plan, &result.pid, &result.error, cancel); });
}
} // namespace vibestudio
