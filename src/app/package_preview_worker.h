#pragma once

#include "core/package_preview.h"

#include <QObject>
#include <functional>
#include <memory>
#include <optional>

class QThread;
class QTimer;

namespace vibestudio {

struct PackagePreviewRequest {
	std::shared_ptr<const PackageArchiveReader> archive;
	QString revision;
	QString virtualPath;
	qsizetype entryIndex = -1;
	qint64 byteLimit = 65536;
	qint64 mediaByteLimit = 64ll * 1024 * 1024;
};

struct PackagePreviewResult {
	QString revision;
	qsizetype entryIndex = -1;
	PackagePreview preview;
};

// One immutable active snapshot and one replaceable pending selection. The UI
// receives only the current generation; cancellation also stops streamed reads.
// Destruction joins the worker before releasing document-owned content.
class PackagePreviewWorker final : public QObject {
public:
	explicit PackagePreviewWorker(QObject* parent = nullptr);
	~PackagePreviewWorker() override;
	void request(PackagePreviewRequest request);
	void cancel();
	[[nodiscard]] bool busy() const;
	std::function<void(qint64 bytes, qint64 total)> progress;
	std::function<void(const PackagePreviewResult&)> completed;

private:
	struct Work;
	quint64 m_serial = 0;
	std::optional<PackagePreviewRequest> m_pending;
	std::shared_ptr<Work> m_work;
	QThread* m_thread = nullptr;
	QTimer* m_timer = nullptr;
	void startNext();
};

} // namespace vibestudio
