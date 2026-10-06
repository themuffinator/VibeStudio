#include "app/level_placement_task_dialog.h"
#include "core/studio_settings.h"
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QLabel>
#include <QMutex>
#include <QMutexLocker>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <atomic>
#include <memory>

namespace vibestudio {
namespace {
struct PlacementTask {
	std::atomic_bool cancel{false};
	QMutex mutex;
	LevelPlacementProgress progress;
	LevelPlacementResult result;
};
} // namespace
LevelPlacementTaskDialog::LevelPlacementTaskDialog(QWidget* parent) : QDialog(parent) {
	setObjectName(QStringLiteral("levelPlacementTaskDialog"));
	setWindowModality(Qt::WindowModal);
	setMinimumWidth(420);
}
void LevelPlacementTaskDialog::reject() {
	if (m_cancel) {
		m_cancel();
	}
	QDialog::reject();
}
LevelPlacementResult LevelPlacementTaskDialog::prepare(QWidget* parent, const LevelMapDocument& source,
													   const LevelPlacementRequest& request) {
	LevelPlacementTaskDialog dialog(parent);
	switch (request.operation) {
	case LevelPlacementOperation::AddBrush:
		dialog.setWindowTitle(tr("Add Brush"));
		break;
	case LevelPlacementOperation::Move:
		dialog.setWindowTitle(tr("Move Selection"));
		break;
	case LevelPlacementOperation::Rotate:
		dialog.setWindowTitle(tr("Rotate Selection"));
		break;
	case LevelPlacementOperation::Resize:
		dialog.setWindowTitle(tr("Resize Selection"));
		break;
	case LevelPlacementOperation::Snap:
		dialog.setWindowTitle(tr("Snap Selection to Grid"));
		break;
	case LevelPlacementOperation::Duplicate:
		dialog.setWindowTitle(tr("Duplicate Selection"));
		break;
	case LevelPlacementOperation::Paste:
		dialog.setWindowTitle(tr("Paste Objects"));
		break;
	case LevelPlacementOperation::Mirror:
		dialog.setWindowTitle(tr("Mirror Selection"));
		break;
	case LevelPlacementOperation::SelectConnectedDoom:
		dialog.setWindowTitle(tr("Select Connected Geometry"));
		break;
	}
	dialog.setAccessibleName(dialog.windowTitle());
	auto* layout = new QVBoxLayout(&dialog);
	auto* label = new QLabel(levelPlacementPhaseName(LevelPlacementPhase::Preparing));
	label->setWordWrap(true);
	label->setObjectName(QStringLiteral("levelPlacementTaskPhase"));
	label->setAccessibleName(tr("Placement phase"));
	layout->addWidget(label);
	auto* bar = new QProgressBar;
	bar->setObjectName(QStringLiteral("levelPlacementTaskProgress"));
	bar->setAccessibleName(tr("Placement progress"));
	bar->setTextVisible(false);
	const bool reducedMotion = StudioSettings().accessibilityPreferences().reducedMotion;
	bar->setRange(0, reducedMotion ? 1 : 0);
	layout->addWidget(bar);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("levelPlacementTaskCancel"));
	buttons->button(QDialogButtonBox::Cancel)->setAccessibleName(tr("Cancel placement"));
	buttons->button(QDialogButtonBox::Cancel)->setAccessibleDescription(tr("Discard this preparation and keep the map unchanged."));
	layout->addWidget(buttons);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	auto state = std::make_shared<PlacementTask>();
	dialog.m_cancel = [state] { state->cancel = true; };
	auto* worker = QThread::create([state, source, request] {
		LevelPlacementControl control;
		control.isCancelled = [state] { return state->cancel.load(); };
		control.progress = [state](const auto& progress) {
			QMutexLocker lock(&state->mutex);
			state->progress = progress;
		};
		state->result = prepareLevelPlacement(source, request, control);
	});
	QEventLoop events;
	QTimer poll;
	poll.setInterval(40);
	QObject::connect(&poll, &QTimer::timeout, &dialog, [&] {
		LevelPlacementProgress progress;
		{
			QMutexLocker lock(&state->mutex);
			progress = state->progress;
		}
		QString text = levelPlacementPhaseName(progress.phase);
		if (progress.total > 0) {
			bar->setRange(0, 1000);
			bar->setValue(static_cast<int>(1000.0 * std::clamp(progress.completed, qint64(0), progress.total) / progress.total));
			text += QStringLiteral("\n") + QChar(0x2066) + dialog.locale().toString(progress.completed) + QStringLiteral(" / ") +
					dialog.locale().toString(progress.total) + QChar(0x2069);
		} else {
			bar->setRange(0, reducedMotion ? 1 : 0);
		}
		label->setText(text);
	});
	bool completed = false;
	QObject::connect(worker, &QThread::finished, &dialog, [&] {
		completed = true;
		dialog.accept();
		events.quit();
	});
	QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
	QObject::connect(&dialog, &QDialog::finished, &events, &QEventLoop::quit);
	// Fast edits avoid a flashing progress window. Preparation still runs on
	// the worker; publication guards cover edits made before the modal opens.
	QTimer::singleShot(150, &dialog, [&] {
		if (!completed && !state->cancel) {
			dialog.open();
		}
	});
	worker->start();
	poll.start();
	events.exec();
	poll.stop();
	if (!completed || state->cancel) {
		state->cancel = true;
		LevelPlacementResult cancelled;
		cancelled.cancelled = true;
		cancelled.error = tr("Placement cancelled. The map was not changed.");
		return cancelled;
	}
	return std::move(state->result);
}
} // namespace vibestudio
