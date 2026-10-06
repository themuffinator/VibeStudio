#include "app/package_recovery_writer.h"

#include <QHash>
#include <QThread>

#include <algorithm>

namespace vibestudio {

struct PackageRecoveryWriter::Storage {
	QString directory;
	QHash<QString, std::shared_ptr<PackageRecoverySession>> sessions;
};

PackageRecoveryWriter::PackageRecoveryWriter(QString directory, QObject* parent)
	: QObject(parent), m_directory(std::move(directory)), m_storage(std::make_shared<Storage>())
{
	m_storage->directory = m_directory;
}

PackageRecoveryWriter::~PackageRecoveryWriter()
{
	finished = {};
	if (m_thread) { m_thread->disconnect(this); m_thread->wait(); delete m_thread; m_thread = nullptr; }
	// Unexpected destruction preserves the newest accepted snapshot. Approved
	// closes retire the ID first, so an in-flight completion cannot resurrect it.
	if (m_pending && !m_retired.contains(m_pending->id)) {
		auto result = std::make_shared<Result>(); result->id = m_pending->id;
		run(m_storage, result, m_pending);
	}
	for (const QString& id : std::as_const(m_cleanup)) {
		auto result = std::make_shared<Result>(); result->id = id; result->retiring = true;
		run(m_storage, result, {});
	}
}

bool PackageRecoveryWriter::busy() const { return m_thread || m_pending.has_value() || !m_cleanup.isEmpty(); }
int PackageRecoveryWriter::progress() const { return m_thread && m_result ? m_result->progress.load() : 1000; }

bool PackageRecoveryWriter::checkpoint(const QString& id, const PackageStagingModel& staging, const QString& title, const PackageRecoveryLimits& limits)
{
	if (m_retired.contains(id) || !staging.isLoaded() || packageRecoveryPath(m_directory, id).isEmpty()) { return false; }
	const auto revision = staging.revision();
	if ((m_pending && m_pending->id == id && m_pending->staging.revision() == revision)
		|| (!m_pending && m_thread && !m_result->retiring && m_result->id == id && m_result->revision == revision)
		|| (!m_pending && !m_thread && m_savedId == id && m_savedRevision == revision)) { return false; }
	m_pending = Pending{id, title, staging, limits}; startNext(); return true;
}

void PackageRecoveryWriter::retire(const QString& id)
{
	if (id.isEmpty() || m_retired.contains(id)) { return; }
	if (m_pending && m_pending->id == id) { m_pending.reset(); }
	if (m_savedId == id) { m_savedId.clear(); }
	if (m_thread && m_result->id == id) { m_result->cancelled.store(true); }
	m_retired.insert(id); m_cleanup.insert(id); startNext();
}

void PackageRecoveryWriter::run(const std::shared_ptr<Storage>& storage, const std::shared_ptr<Result>& result,
	const std::optional<Pending>& pending)
{
	if (result->retiring) {
		if (const auto session = storage->sessions.value(result->id)) { session->retire(&result->output.error); }
		storage->sessions.remove(result->id); return;
	}
	if (!pending || result->cancelled.load()) { return; }
	auto session = storage->sessions.value(result->id);
	if (!session) {
		session = PackageRecoverySession::acquire(storage->directory, result->id, &result->output.error);
		if (!session) { return; }
		storage->sessions.insert(result->id, session);
	}
	PackageReadControl control;
	control.isCancelled = [result]() { return result->cancelled.load(); };
	control.progress = [result](const QString&, qint64 done, qint64 total) {
		if (total > 0) { result->progress.store(static_cast<int>(std::clamp(1000.0 * done / total, 0.0, 1000.0))); }
	};
	result->output = session->checkpoint(pending->staging, pending->title, control, pending->limits);
}

void PackageRecoveryWriter::startNext()
{
	if (m_thread || (!m_pending && m_cleanup.isEmpty())) { return; }
	auto result = std::make_shared<Result>(); std::optional<Pending> pending;
	if (!m_cleanup.isEmpty()) {
		result->id = *m_cleanup.begin(); result->retiring = true; m_cleanup.remove(result->id);
	} else {
		pending = std::move(m_pending); m_pending.reset(); result->id = pending->id; result->revision = pending->staging.revision();
	}
	m_result = result;
	m_thread = QThread::create([storage = m_storage, result, pending = std::move(pending)]() { run(storage, result, pending); });
	connect(m_thread, &QThread::finished, this, [this, result]() {
		m_thread->wait(); m_thread->deleteLater(); m_thread = nullptr;
		const bool retired = m_retired.contains(result->id);
		if (!retired && result->output.succeeded()) { m_savedId = result->id; m_savedRevision = result->revision; }
		if (finished && (result->retiring ? !result->output.error.isEmpty() : !retired)) { finished(result->id, result->revision, result->output); }
		startNext();
	});
	m_thread->start();
}

} // namespace vibestudio
