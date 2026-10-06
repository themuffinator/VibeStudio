#pragma once

#include "core/code_files.h"
#include <QObject>
#include <memory>
#include <optional>

class QThread;
class QTimer;

namespace vibestudio {

class CodeFilesWorker final : public QObject {
public:
	explicit CodeFilesWorker(QObject* parent = nullptr);
	~CodeFilesWorker() override;
	void start(CodeFilesRequest request);
	void cancel();
	void reset();
	bool busy() const { return m_thread || m_pending.has_value(); }
	std::function<void(int files, int entries)> progress;
	std::function<void(const CodeFilesResult&)> completed;

private:
	struct Work;
	void startPending();
	QThread* m_thread = nullptr;
	QTimer* m_timer = nullptr;
	std::shared_ptr<Work> m_work;
	std::optional<CodeFilesRequest> m_pending;
	quint64 m_serial = 0;
};

} // namespace vibestudio
