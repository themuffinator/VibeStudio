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
	case LevelPlacementOperation::SmartTag:
		dialog.setWindowTitle(request.smartTag.enabled ? tr("Make %1").arg(request.smartTag.tag.name) : tr("Make non-%1").arg(request.smartTag.tag.name));
		break;
	case LevelPlacementOperation::DeleteObjects:
		dialog.setWindowTitle(tr("Delete Map Objects"));
		break;
	case LevelPlacementOperation::RemoveComponents:
		dialog.setWindowTitle(tr("Remove Brush Components"));
		break;
	case LevelPlacementOperation::SelectTextured:
		dialog.setWindowTitle(tr("Select Textured"));
		break;
	case LevelPlacementOperation::ConnectEntities:
		dialog.setWindowTitle(request.connection.mode == LevelEntityConnectionMode::RadiantKillTarget ? tr("KillConnect Entities") : tr("Connect Entities"));
		break;
	case LevelPlacementOperation::Visibility:
		dialog.setWindowTitle(request.visibility.operation == LevelVisibilityOperation::Isolate ? tr("Isolate Selection")
			: request.visibility.operation == LevelVisibilityOperation::ShowAll ? tr("Show All Hidden") : tr("Hide Selection"));
		break;
	case LevelPlacementOperation::SelectObjects:
		dialog.setWindowTitle(request.objectSelection.mode == LevelObjectSelectionMode::RadiantType ? tr("Select All of Type")
			: request.objectSelection.mode == LevelObjectSelectionMode::ConnectedEntities ? tr("Select Connected Entities")
			: request.objectSelection.mode == LevelObjectSelectionMode::Layers ? tr("Expand Selection to Layers")
			: request.objectSelection.mode == LevelObjectSelectionMode::Siblings ? tr("Select Siblings") : tr("Select by Source Line Number"));
		break;
	case LevelPlacementOperation::SelectRegion:
		dialog.setWindowTitle(tr("Select by Region"));
		break;
	case LevelPlacementOperation::Copy:
		dialog.setWindowTitle(tr("Copy Map Selection"));
		break;
	case LevelPlacementOperation::Cut:
		dialog.setWindowTitle(tr("Cut Map Objects"));
		break;
	case LevelPlacementOperation::Repeat:
		dialog.setWindowTitle(tr("Repeat Commands"));
		break;
	case LevelPlacementOperation::RepeatTransforms:
		dialog.setWindowTitle(tr("Repeat Transforms"));
		break;
	case LevelPlacementOperation::MoveToCamera:
		dialog.setWindowTitle(tr("Move to Camera"));
		break;
	case LevelPlacementOperation::ExtractLinkedGroups:
		dialog.setWindowTitle(tr("Extract Linked Groups"));
		break;
	case LevelPlacementOperation::WrapMerge:
		dialog.setWindowTitle(tr("Wrap/Merge"));
		break;
	case LevelPlacementOperation::Sweep:
		dialog.setWindowTitle(tr("Sweep Faces"));
		break;
	case LevelPlacementOperation::TransformComponents:
		dialog.setWindowTitle(tr("Transform Components"));
		break;
	case LevelPlacementOperation::AddBrush:
		dialog.setWindowTitle(tr("Add Brush"));
		break;
	case LevelPlacementOperation::AddEntity:
		dialog.setWindowTitle(tr("Add Entity"));
		break;
	case LevelPlacementOperation::CreateEntity:
		dialog.setWindowTitle(tr("Create %1").arg(request.entityCreate.className));
		break;
	case LevelPlacementOperation::AddThing:
		dialog.setWindowTitle(tr("Add Thing"));
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
	case LevelPlacementOperation::Scale:
		dialog.setWindowTitle(tr("Scale Selection"));
		break;
	case LevelPlacementOperation::Snap:
		dialog.setWindowTitle(tr("Snap Selection to Grid"));
		break;
	case LevelPlacementOperation::RoundToGrid:
		dialog.setWindowTitle(tr("Round Selection to Grid"));
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
	label->setAccessibleName(tr("Operation phase"));
	layout->addWidget(label);
	auto* bar = new QProgressBar;
	bar->setObjectName(QStringLiteral("levelPlacementTaskProgress"));
	bar->setAccessibleName(tr("Operation progress"));
	bar->setTextVisible(false);
	const bool reducedMotion = StudioSettings().accessibilityPreferences().reducedMotion;
	bar->setRange(0, reducedMotion ? 1 : 0);
	layout->addWidget(bar);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("levelPlacementTaskCancel"));
	buttons->button(QDialogButtonBox::Cancel)->setAccessibleName(tr("Cancel operation"));
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
