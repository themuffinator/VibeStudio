#include "app/audio_browser_worker.h"

#include "core/audio_clip.h"

#include <QCoreApplication>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <new>
#include <utility>

namespace vibestudio
{
namespace
{
constexpr qint64 PreviewByteLimit = 64ll * 1024 * 1024;
constexpr qint64 HeaderByteLimit = 65536;

// Analyze a single already-verified occurrence without reading it a second
// time or resolving a duplicate WAD name through a path lookup.
class AudioPreviewBytes final : public PackageArchiveReader {
public:
	AudioPreviewBytes(PackageEntry entry, QByteArray bytes) : m_entry(std::move(entry)), m_bytes(std::move(bytes)) {}
	PackageArchiveFormat format() const override { return PackageArchiveFormat::Unknown; }
	QString sourcePath() const override { return {}; }
	bool isOpen() const override { return true; }
	QVector<PackageEntry> entries() const override { return {m_entry}; }
	bool readEntryBytes(const QString& path, QByteArray* out, QString*, qint64 maxBytes) const override
	{
		if (path != m_entry.virtualPath || !out) { return false; }
		*out = maxBytes < 0 ? m_bytes : m_bytes.first(std::min<qint64>(maxBytes, m_bytes.size()));
		return true;
	}
	bool streamEntryAt(qsizetype index, const std::function<bool(QByteArrayView)>& sink, QString*,
		const std::function<bool()>& isCancelled) const override
	{
		if (index != 0 || !sink) { return false; }
		for (qsizetype offset = 0; offset < m_bytes.size(); offset += 65536) {
			if (isCancelled && isCancelled()) { return false; }
			if (!sink(QByteArrayView(m_bytes).sliced(offset, std::min<qsizetype>(65536, m_bytes.size() - offset)))) { return false; }
		}
		return !(isCancelled && isCancelled());
	}
private:
	PackageEntry m_entry;
	QByteArray m_bytes;
};

QString text(const char* message) { return QCoreApplication::translate("VibeStudioAudioBrowser", message); }
} // namespace

struct AudioBrowserWorker::Work {
	quint64 serial = 0;
	std::atomic_bool cancelled = false;
	std::atomic_int progress = 0;
	AudioBrowserResult result;
};

AudioBrowserWorker::AudioBrowserWorker(QObject* parent) : QObject(parent)
{
	m_timer = new QTimer(this);
	m_timer->setInterval(75);
	connect(m_timer, &QTimer::timeout, this, [this] {
		if (m_work && m_work->serial == m_serial && progress) { progress(m_work->progress.load()); }
	});
}

AudioBrowserWorker::~AudioBrowserWorker()
{
	cancel();
	if (m_thread) { m_thread->disconnect(this); m_thread->wait(); delete m_thread; }
}
bool AudioBrowserWorker::busy() const { return m_pending.has_value() || (m_work && m_work->serial == m_serial && !m_work->cancelled); }
void AudioBrowserWorker::cancel()
{
	++m_serial;
	m_pending.reset();
	if (m_work) { m_work->cancelled = true; }
	m_timer->stop();
}
void AudioBrowserWorker::request(AudioBrowserRequest request)
{
	cancel();
	m_pending = std::move(request);
	QTimer::singleShot(0, this, [this] { startNext(); });
}
void AudioBrowserWorker::startNext()
{
	if (m_thread || !m_pending) { return; }
	auto request = std::move(*m_pending); m_pending.reset();
	auto work = std::make_shared<Work>(); m_work = work; work->serial = m_serial;
	work->result.kind = request.kind; work->result.revision = request.revision;
	work->result.entryIndex = request.entryIndex; work->result.virtualPath = request.virtualPath;
	m_thread = QThread::create([work, request = std::move(request)] {
		try {
			const auto cancelled = [work] { return work->cancelled.load(); };
			auto& result = work->result;
			if (cancelled()) { return; }
			const auto entries = request.archive ? request.archive->entries() : QVector<PackageEntry>{};
			if (!request.archive || !request.archive->isOpen() || request.entryIndex < 0 || request.entryIndex >= entries.size() ||
			    entries.at(request.entryIndex).virtualPath != request.virtualPath) {
				result.error = text(QT_TRANSLATE_NOOP("VibeStudioAudioBrowser", "The selected audio entry is no longer available.")); return;
			}
			const auto& entry = entries.at(request.entryIndex);
			if (!entry.readable || entry.kind != PackageEntryKind::File) {
				result.error = entry.note.isEmpty() ? text(QT_TRANSLATE_NOOP("VibeStudioAudioBrowser", "This audio entry is not readable.")) : entry.note; return;
			}
			const bool audition = request.kind == AudioBrowserRequest::Kind::Audition;
			if (audition && entry.sizeBytes > AudioInputByteLimit) {
				result.error = text(QT_TRANSLATE_NOOP("VibeStudioAudioBrowser", "Audio audition is limited to 128 MiB per sound.")); return;
			}
			const bool headerOnly = !audition && entry.sizeBytes > PreviewByteLimit;
			QByteArray bytes;
			if (headerOnly) {
				if (!request.archive->readEntryAt(request.entryIndex, &bytes, &result.error, HeaderByteLimit)) {
					if (result.error.isEmpty()) { result.error = text(QT_TRANSLATE_NOOP("VibeStudioAudioBrowser", "Unable to read the audio header.")); }
					return;
				}
				if (bytes.size() > HeaderByteLimit) {
					result.error = text(QT_TRANSLATE_NOOP("VibeStudioAudioBrowser", "The audio reader exceeded the requested byte limit.")); return;
				}
			} else {
				const qint64 limit = audition ? AudioInputByteLimit : PreviewByteLimit;
				bytes.reserve(static_cast<qsizetype>(entry.sizeBytes));
				bool exceeded = false;
				const bool read = request.archive->streamEntryAt(request.entryIndex, [&](QByteArrayView chunk) {
					if (cancelled()) { return false; }
					if (chunk.size() > limit - bytes.size() || static_cast<quint64>(bytes.size() + chunk.size()) > entry.sizeBytes) {
						exceeded = true; return false;
					}
					bytes.append(chunk.data(), chunk.size());
					work->progress = static_cast<int>(bytes.size() * 90 / std::max<quint64>(1, entry.sizeBytes));
					return true;
				}, &result.error, cancelled);
				if (cancelled()) { return; }
				if (!read || exceeded || static_cast<quint64>(bytes.size()) != entry.sizeBytes) {
					// Even a checksum failure after the last chunk invalidates all bytes.
					if (exceeded) { result.error = text(QT_TRANSLATE_NOOP("VibeStudioAudioBrowser", "The audio reader exceeded the requested byte limit.")); }
					else if (result.error.isEmpty()) { result.error = text(QT_TRANSLATE_NOOP("VibeStudioAudioBrowser", "Unable to read and verify the complete audio entry.")); }
					return;
				}
			}
			if (cancelled()) { return; }
			work->progress = 90;
			if (audition) {
				result.source = assetAudioPlaybackSource(request.virtualPath, bytes);
				if (result.source.bytes.size() > AudioInputByteLimit) {
					result.source = {};
					result.error = text(QT_TRANSLATE_NOOP("VibeStudioAudioBrowser", "The converted sound exceeds the 128 MiB audition limit."));
				} else { result.error = result.source.error; }
			} else {
				const qint64 cap = headerOnly ? HeaderByteLimit : PreviewByteLimit;
				const AudioPreviewBytes reader(entry, std::move(bytes));
				result.preview = buildPackageEntryPreviewAt(reader, 0, cap, 0);
				result.error = result.preview.error;
				if (headerOnly) {
					result.preview.audioPeaks = {};
					result.preview.audioWaveformLines = {text(QT_TRANSLATE_NOOP("VibeStudioAudioBrowser",
						"Waveform unavailable: this sound exceeds the 64 MiB preview limit. Only header metadata was sampled."))};
				}
			}
			work->progress = 100;
		} catch (const std::bad_alloc&) {
			work->result.preview = {}; work->result.source = {};
			work->result.error = text(QT_TRANSLATE_NOOP("VibeStudioAudioBrowser", "Not enough memory to prepare this sound."));
		}
	});
	m_thread->setParent(this);
	connect(m_thread, &QThread::finished, this, [this, work] {
		m_thread->deleteLater(); m_thread = nullptr; m_timer->stop();
		if (m_work == work) { m_work.reset(); }
		if (work->serial == m_serial && !work->cancelled && completed) { completed(work->result); }
		startNext();
	});
	m_timer->start(); m_thread->start();
}

} // namespace vibestudio
