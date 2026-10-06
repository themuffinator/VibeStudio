#include "app/package_preview_worker.h"

#include <QCoreApplication>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <new>
#include <utility>

namespace vibestudio {

struct PackagePreviewWorker::Work {
	quint64 serial = 0;
	std::atomic_bool cancelled = false;
	std::atomic<qint64> bytes = 0, total = 0;
	PackagePreviewResult result;
};

PackagePreviewWorker::PackagePreviewWorker(QObject* parent) : QObject(parent)
{
	m_timer = new QTimer(this);
	m_timer->setInterval(75);
	connect(m_timer, &QTimer::timeout, this, [this] {
		if (m_work && m_work->serial == m_serial && progress) { progress(m_work->bytes.load(), m_work->total.load()); }
	});
}

PackagePreviewWorker::~PackagePreviewWorker()
{
	cancel();
	if (m_thread) { m_thread->disconnect(this); m_thread->wait(); delete m_thread; }
}

bool PackagePreviewWorker::busy() const { return m_thread || m_pending.has_value(); }

void PackagePreviewWorker::cancel()
{
	++m_serial;
	m_pending.reset();
	if (m_work) { m_work->cancelled = true; }
	m_timer->stop();
}

void PackagePreviewWorker::request(PackagePreviewRequest request)
{
	cancel();
	m_pending = std::move(request);
	QTimer::singleShot(0, this, [this] { startNext(); });
}

void PackagePreviewWorker::startNext()
{
	if (m_thread || !m_pending) { return; }
	auto request = std::move(*m_pending); m_pending.reset();
	auto work = std::make_shared<Work>(); m_work = work; work->serial = m_serial;
	work->result.revision = request.revision; work->result.entryIndex = request.entryIndex;
	work->result.preview.virtualPath = request.virtualPath;
	m_thread = QThread::create([work, request = std::move(request)] {
		try {
			if (work->cancelled) { return; }
			const auto entries = request.archive ? request.archive->entries() : QVector<PackageEntry>();
			if (!request.archive || !request.archive->isOpen() || request.entryIndex < 0 || request.entryIndex >= entries.size()
				|| entries.at(request.entryIndex).virtualPath != request.virtualPath || request.byteLimit < 0 || request.mediaByteLimit < 0
				|| request.byteLimit > 64ll * 1024 * 1024 || request.mediaByteLimit > 64ll * 1024 * 1024) {
				work->result.preview.error = QCoreApplication::translate("VibeStudioPackagePreview", "The selected preview request is no longer valid.");
				return;
			}
			PackageReadControl control;
			control.isCancelled = [work] { return work->cancelled.load(); };
			control.progress = [work](const QString&, qint64 bytes, qint64 total) { work->total = total; work->bytes = bytes; };
			work->result.preview = buildPackageEntryPreviewAt(*request.archive, request.entryIndex,
				request.byteLimit, request.mediaByteLimit, control);
		} catch (const std::bad_alloc&) {
			work->result.preview = {};
			work->result.preview.virtualPath = request.virtualPath;
			work->result.preview.error = QCoreApplication::translate("VibeStudioPackagePreview", "Not enough memory to prepare this preview.");
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
