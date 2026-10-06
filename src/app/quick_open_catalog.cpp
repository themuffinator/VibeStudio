#include "app/quick_open_catalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <atomic>

namespace vibestudio {
namespace {
QString fileKey(const QString& path)
{
	const QString key = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
	return key.toCaseFolded();
#else
	return key;
#endif
}
} // namespace

QVector<QuickOpenEntry> quickOpenRecentEntries(const QStringList& paths)
{
	QVector<QuickOpenEntry> result;
	QSet<QString> seen;
	for (const QString& path : paths) {
		if (path.isEmpty() || !QDir::isAbsolutePath(path) || seen.contains(fileKey(path))) { continue; }
		seen.insert(fileKey(path));
		const QFileInfo info(path);
		result.push_back({QDir::cleanPath(path), info.fileName(), QDir::toNativeSeparators(info.path()), QCoreApplication::translate("VibeStudioQuickOpen", "Recent")});
	}
	return result;
}

struct QuickOpenCatalog::Work {
	std::atomic_bool cancel {false};
	std::atomic_int files {0};
	std::atomic_int entries {0};
	QuickOpenResult result;
	quint64 serial = 0;
};

QuickOpenCatalog::QuickOpenCatalog(QObject* parent) : QObject(parent)
{
	setObjectName(QStringLiteral("quickOpenCatalog"));
	m_timer = new QTimer(this);
	m_timer->setInterval(100);
	connect(m_timer, &QTimer::timeout, this, [this]() {
		if (m_work && m_work->serial == m_serial && progress) { progress(m_work->files, m_work->entries); }
	});
}

QuickOpenCatalog::~QuickOpenCatalog()
{
	reset();
	if (m_thread) { m_thread->disconnect(this); m_thread->wait(); delete m_thread; }
}

void QuickOpenCatalog::start(QuickOpenRequest request)
{
	reset();
	m_pending = std::move(request);
	startPending();
}

void QuickOpenCatalog::reset()
{
	++m_serial;
	m_pending.reset();
	if (m_work) { m_work->cancel = true; }
	m_timer->stop();
}

void QuickOpenCatalog::cancel()
{
	if (m_pending) {
		reset();
		QuickOpenResult result;
		result.state = OperationState::Cancelled;
		if (completed) { completed(result); }
	} else if (m_work) { m_work->cancel = true; }
}

void QuickOpenCatalog::startPending()
{
	if (m_thread || !m_pending) { return; }
	auto work = std::make_shared<Work>();
	work->serial = m_serial;
	m_work = work;
	QuickOpenRequest request = std::move(*m_pending);
	m_pending.reset();
	m_thread = QThread::create([work, request = std::move(request)]() {
		auto& result = work->result;
		QSet<QString> seen;
		for (const auto& entry : quickOpenRecentEntries(request.recentPaths)) {
			if (work->cancel) { break; }
			if (QFileInfo(entry.key).isFile()) { result.entries << entry; seen.insert(fileKey(entry.key)); }
		}
		// Archive metadata is an immutable, implicitly shared snapshot. No payload
		// reads, extraction, or access to the live shell/archive occur here.
		QVector<QuickOpenEntry> packageRows;
		const int packageLimit = std::clamp(request.maxPackageEntries, 1, 100000);
		const QString packageName = QFileInfo(request.packageSource).fileName();
		int visited = 0;
		for (const auto& entry : request.packageEntries) {
			if (work->cancel) { break; }
			if (visited++ >= packageLimit) {
				result.warnings << QCoreApplication::translate("VibeStudioQuickOpen", "The package list reached its %1 entry limit. Filtering searches only the gathered entries.").arg(packageLimit);
				break;
			}
			if (entry.kind != PackageEntryKind::File) { continue; }
			const QFileInfo info(entry.virtualPath);
			packageRows.push_back({QStringLiteral("package:") + entry.virtualPath, info.fileName(), info.path() == QStringLiteral(".") ? QString() : info.path(), packageName});
		}
		if (!request.rootPath.isEmpty() && !work->cancel) {
			CodeFilesRequest scan;
			scan.rootPath = request.rootPath;
			scan.maxFiles = request.maxProjectFiles;
			scan.includeAssets = true;
			scan.isCancelled = [work]() { return work->cancel.load(); };
			scan.progress = [work](int files, int entries) { work->files = files; work->entries = entries; };
			const auto catalog = listCodeFiles(scan);
			result.error = catalog.error;
			result.warnings += catalog.warnings;
			for (const auto& file : catalog.files) {
				if (seen.contains(fileKey(file.filePath))) { continue; }
				seen.insert(fileKey(file.filePath));
				const QFileInfo info(file.relativePath);
				result.entries.push_back({file.filePath, info.fileName(), info.path() == QStringLiteral(".") ? QString() : info.path(), QCoreApplication::translate("VibeStudioQuickOpen", "Project")});
			}
		}
		result.entries += packageRows;
		result.state = work->cancel ? OperationState::Cancelled : !result.error.isEmpty() ? OperationState::Failed
			: result.warnings.isEmpty() ? OperationState::Completed : OperationState::Warning;
	});
	connect(m_thread, &QThread::finished, this, [this, work]() {
		QThread* finished = m_thread;
		m_thread = nullptr;
		finished->wait();
		finished->deleteLater();
		m_timer->stop();
		m_work.reset();
		if (work->serial == m_serial) {
			if (work->cancel) { work->result.state = OperationState::Cancelled; }
			if (completed) { completed(work->result); }
		}
		startPending();
	});
	m_timer->start();
	m_thread->start();
}

} // namespace vibestudio
