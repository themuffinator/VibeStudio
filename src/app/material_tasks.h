#pragma once

// Background work for the Materials page: library scans, image decoding and
// preview frames run off the GUI thread, newest request first.

#include <QObject>

#include <atomic>
#include <functional>
#include <memory>

class QThread;

namespace vibestudio {

// Runs one task at a time off the GUI thread and keeps one queued task. A
// task returns the function that publishes its result; that function runs on
// the GUI thread unless the task was superseded or cancelled meanwhile, so a
// stale result never shows.
class MaterialTaskLane final : public QObject {
public:
	using Cancelled = std::function<bool()>;
	using Task = std::function<std::function<void()>(const Cancelled& cancelled)>;

	explicit MaterialTaskLane(QObject* parent = nullptr);
	~MaterialTaskLane() override;

	// `supersede` stops the running task and drops its result: right when the
	// inputs changed. Without it the running task finishes and publishes and
	// only the queued task is replaced: right for animation frames, which
	// should keep arriving while newer ones are asked for.
	void submit(Task task, bool supersede = true);
	void cancel();
	[[nodiscard]] bool busy() const;
	// True while a task runs or waits.
	[[nodiscard]] bool pending() const;

private:
	void startNext();

	Task m_queued;
	bool m_hasQueued = false;
	QThread* m_thread = nullptr;
	std::shared_ptr<std::atomic_bool> m_stop;
};

} // namespace vibestudio
