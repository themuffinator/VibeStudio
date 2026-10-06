#pragma once

#include "core/advanced_studio.h"

#include <QObject>
#include <memory>
#include <optional>

class QThread;
class QTimer;

namespace vibestudio {

// One background scanner per shell, with a coalesced latest request. Reset
// invalidates results immediately; a retired scan never publishes into a new
// project. Callbacks and snapshot access belong to the owning GUI thread.
class CodeIndexWorker final : public QObject {
public:
	explicit CodeIndexWorker(QObject* parent = nullptr);
	~CodeIndexWorker() override;
	void start(CodeWorkspaceIndexRequest request);
	void cancel();
	void reset();
	bool busy() const { return m_thread || m_pending.has_value(); }
	QString requestedRoot() const { return m_root; }
	std::function<void()> started;
	std::function<void(int files, int symbols)> progress;
	std::function<void(const CodeWorkspaceIndex&)> completed;

private:
	struct Work;
	void startPending();
	QThread* m_thread = nullptr;
	QTimer* m_progressTimer = nullptr;
	std::shared_ptr<Work> m_work;
	std::optional<CodeWorkspaceIndexRequest> m_pending;
	QString m_root;
	quint64 m_serial = 0;
};

} // namespace vibestudio
