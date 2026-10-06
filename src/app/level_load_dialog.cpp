#include "app/level_load_dialog.h"
#include "app/studio_layout.h"
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QDir>
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
#include <exception>

namespace vibestudio {
namespace {
struct LoadProgress {
	QMutex mutex;
	LevelMapLoadPhase phase = LevelMapLoadPhase::Reading;
	qint64 completed = 0, total = 0;
};
} // namespace

LevelMapLoadDialog::LevelMapLoadDialog(QWidget* parent) : QDialog(parent)
{
	setObjectName(QStringLiteral("levelMapLoadDialog"));
	setWindowTitle(tr("Open Map"));
	setWindowModality(Qt::WindowModal);
	setAccessibleName(windowTitle());
	setMinimumWidth(420);
}
void LevelMapLoadDialog::reject() { if (m_cancel) { m_cancel(); } }
void LevelMapLoadDialog::closeEvent(QCloseEvent* event) { reject(); event->ignore(); }

LevelMapOpenResult LevelMapLoadDialog::openMap(QWidget* parent, const LevelMapLoadRequest& input)
{
	LevelMapLoadDialog dialog(parent);
	auto* layout = new QVBoxLayout(&dialog);
	auto* path = new ElidedLabel(QDir::toNativeSeparators(input.path));
	path->setObjectName(QStringLiteral("levelMapLoadSource"));
	path->setTextFormat(Qt::PlainText); path->setElideMode(Qt::ElideMiddle);
	path->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	path->setLayoutDirection(Qt::LeftToRight);
	path->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	path->setAccessibleName(tr("Map source"));
	layout->addWidget(path);
	auto* phase = new QLabel(tr("Reading map…"));
	phase->setObjectName(QStringLiteral("levelMapLoadPhase")); phase->setWordWrap(true);
	phase->setAccessibleName(tr("Map loading phase"));
	layout->addWidget(phase);
	auto* bar = new QProgressBar;
	bar->setObjectName(QStringLiteral("levelMapLoadProgress"));
	bar->setRange(0, 0); bar->setAccessibleName(tr("Map loading progress"));
	// Native sizing follows the shared theme's font without a fixed height cap.
	layout->addWidget(bar);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("levelMapLoadCancel"));
	buttons->button(QDialogButtonBox::Cancel)->setAccessibleName(tr("Cancel opening map"));
	layout->addWidget(buttons);

	LevelMapOpenResult result;
	result.geometry = std::make_shared<MapBrushGeometryCache>();
	auto state = std::make_shared<LoadProgress>();
	std::atomic_bool cancellationObserved{false};
	auto request = input;
	request.brushGeometryCache = result.geometry.get();
	request.isCancelled = [original = input.isCancelled, &cancellationObserved] {
		const bool stopped = QThread::currentThread()->isInterruptionRequested() || (original && original());
		if (stopped) { cancellationObserved = true; }
		return stopped;
	};
	request.progress = [original = input.progress, state](LevelMapLoadPhase current, qint64 completed, qint64 total) {
		{ QMutexLocker lock(&state->mutex); state->phase = current; state->completed = completed; state->total = total; }
		if (original) { original(current, completed, total); }
	};
	auto* worker = QThread::create([request, &result, &cancellationObserved] {
		try {
			result.succeeded = loadLevelMap(request, &result.document, &result.error);
		} catch (const std::exception& error) {
			result.error = tr("Unable to open the map: %1").arg(QString::fromUtf8(error.what()));
		} catch (...) {
			result.error = tr("Unable to open the map.");
		}
		result.cancelled = cancellationObserved || QThread::currentThread()->isInterruptionRequested();
		if (result.cancelled) { result.succeeded = false; }
	});
	bool cancelRequested = false;
	dialog.m_cancel = [&] {
		cancelRequested = true; worker->requestInterruption();
		buttons->setEnabled(false); phase->setText(tr("Cancelling map open…"));
	};
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &LevelMapLoadDialog::reject);
	QTimer timer;
	timer.setInterval(50);
	QObject::connect(&timer, &QTimer::timeout, &dialog, [&] {
		if (cancelRequested) { return; }
		QMutexLocker lock(&state->mutex);
		switch (state->phase) {
		case LevelMapLoadPhase::Reading: phase->setText(tr("Reading map…")); break;
		case LevelMapLoadPhase::Indexing: phase->setText(tr("Indexing source…")); break;
		case LevelMapLoadPhase::Tokenizing: phase->setText(tr("Reading map syntax…")); break;
		case LevelMapLoadPhase::Parsing: phase->setText(tr("Building map objects…")); break;
		case LevelMapLoadPhase::Solving: phase->setText(tr("Solving brush geometry…")); break;
		case LevelMapLoadPhase::Validating: phase->setText(tr("Checking map health…")); break;
		case LevelMapLoadPhase::Hashing: phase->setText(tr("Recording source fingerprint…")); break;
		case LevelMapLoadPhase::Complete: phase->setText(tr("Map ready.")); break;
		}
		if (state->total > 0) {
			bar->setRange(0, 1000);
			bar->setValue(int(std::clamp(state->completed, qint64(0), state->total) * 1000 / state->total));
		} else { bar->setRange(0, 0); }
	});
	bool workerCompleted = false;
	QObject::connect(worker, &QThread::finished, &dialog, [&] { workerCompleted = true; dialog.accept(); });
	worker->start(); timer.start(); dialog.exec();
	// Covers application shutdown or programmatic dismissal as well as normal
	// completion. No worker can retain references to this stack after return.
	if (!workerCompleted) { worker->requestInterruption(); cancelRequested = true; }
	worker->wait(); timer.stop(); delete worker;
	if (cancelRequested) { result.cancelled = true; result.succeeded = false; }
	if (!result.succeeded) { result.document = {}; result.geometry.reset(); }
	return result;
}
} // namespace vibestudio
