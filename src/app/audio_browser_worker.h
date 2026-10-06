#pragma once

#include "core/package_preview.h"

#include <QObject>
#include <functional>
#include <memory>
#include <optional>

class QThread;
class QTimer;

namespace vibestudio
{

struct AudioBrowserRequest {
	enum class Kind { Preview, Audition };
	Kind kind = Kind::Preview;
	std::shared_ptr<const PackageArchiveReader> archive;
	quint64 revision = 0;
	qsizetype entryIndex = -1;
	QString virtualPath;
};

struct AudioBrowserResult {
	AudioBrowserRequest::Kind kind = AudioBrowserRequest::Kind::Preview;
	quint64 revision = 0;
	qsizetype entryIndex = -1;
	QString virtualPath;
	PackagePreview preview;
	AssetAudioPlaybackSource source;
	QString error;
};

// One active value-only job and one replaceable pending request per worker.
// Readers are immutable snapshots. Full reads stream with cancellation and
// integrity checks; bounded header/codec phases check between phases.
class AudioBrowserWorker final : public QObject {
public:
	explicit AudioBrowserWorker(QObject* parent = nullptr);
	~AudioBrowserWorker() override;
	void request(AudioBrowserRequest request);
	void cancel();
	[[nodiscard]] bool busy() const;
	std::function<void(int)> progress;
	std::function<void(const AudioBrowserResult&)> completed;

private:
	struct Work;
	void startNext();
	quint64 m_serial = 0;
	std::optional<AudioBrowserRequest> m_pending;
	std::shared_ptr<Work> m_work;
	QThread* m_thread = nullptr;
	QTimer* m_timer = nullptr;
};

} // namespace vibestudio
