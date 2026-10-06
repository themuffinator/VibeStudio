#include "app/audio_recovery.h"
#include "core/audio_recovery_store.h"

#include <QCoreApplication>
#include <QLockFile>
#include <QThread>

namespace vibestudio
{

AudioRecoveryDiscovery::AudioRecoveryDiscovery(QObject *parent)
    : QObject(parent), m_cancel(std::make_shared<std::atomic_bool>(false))
{
}

AudioRecoveryDiscovery::~AudioRecoveryDiscovery()
{
	finished = {};
	cancel();
	if (m_thread) {
		m_thread->wait();
		delete m_thread;
	}
}

void AudioRecoveryDiscovery::cancel() { m_cancel->store(true); }

void AudioRecoveryDiscovery::start(const QString &directory)
{
	if (busy()) {
		return;
	}
	m_cancel = std::make_shared<std::atomic_bool>(false);
	auto result = std::make_shared<AudioRecoveryInventory>();
	m_thread = QThread::create([directory, result, cancel = m_cancel]() {
		*result = discoverAudioRecoveries(directory, {[cancel] { return cancel->load(); }});
	});
	connect(m_thread, &QThread::finished, this, [this, result]() {
		m_thread->deleteLater();
		m_thread = nullptr;
		if (!m_cancel->load() && finished) {
			finished(*result);
		}
	});
	m_thread->start();
}

AudioRecoveryWriter::AudioRecoveryWriter(QString directory, QObject *parent, AudioRecoveryKind kind)
    : QObject(parent), m_kind(kind), m_directory(std::move(directory))
{
}

AudioRecoveryWriter::~AudioRecoveryWriter()
{
	finished = {};
	if (m_thread) {
		m_thread->wait();
		delete m_thread;
		m_thread = nullptr;
	}
	// Destruction without an approved close keeps the most recent accepted edit.
	if (m_pending && !m_retired.contains(m_pending->id)) {
		writeSnapshot(*m_pending, m_directory);
	}
	removeRetired();
}

bool AudioRecoveryWriter::busy() const { return m_thread || m_pending.has_value(); }

void AudioRecoveryWriter::checkpoint(const QString &id, quint64 revision, AudioProject project)
{
	if (m_kind == AudioRecoveryKind::Waveform) {
		queue({id, revision, std::move(project)});
	}
}
void AudioRecoveryWriter::checkpoint(const QString &id, quint64 revision, AudioSessionRecovery session)
{
	if (m_kind == AudioRecoveryKind::Session) {
		queue({id, revision, std::move(session)});
	}
}

QString AudioRecoveryWriter::writeSnapshot(const Snapshot &snapshot, const QString &directory, QString *error)
{
	try {
		if (const auto *project = std::get_if<AudioProject>(&snapshot.document)) {
			return writeAudioRecovery(*project, directory, snapshot.id, error);
		}
		const auto &session = std::get<AudioSessionRecovery>(snapshot.document);
		return writeAudioSessionRecovery(session.session, session.sourcePath, directory, snapshot.id, error);
	} catch (const std::bad_alloc &) {
		if (error) {
			*error = QCoreApplication::translate("AudioRecovery",
			                                     "Insufficient memory for recovery. Save the document and try again.");
		}
		return {};
	}
}

void AudioRecoveryWriter::queue(Snapshot snapshot)
{
	const auto &id = snapshot.id;
	const auto revision = snapshot.revision;
	if (m_closedIds.contains(id)) {
		return;
	}
	if (!m_sessions.contains(id)) {
		QString error;
		auto session = acquireAudioRecoverySession(m_directory, id, &error, m_kind);
		if (!session) {
			if (finished) {
				finished({}, error);
			}
			return;
		}
		m_sessions.insert(id, std::shared_ptr<QLockFile>(std::move(session)));
	}
	if ((m_pending && m_pending->id == id && m_pending->revision == revision) ||
	    (!m_pending && m_thread && m_result->id == id && m_result->revision == revision) ||
	    (!m_pending && !m_thread && m_savedId == id && m_savedRevision == revision)) {
		return;
	}
	m_pending = std::move(snapshot);
	startNext();
}

void AudioRecoveryWriter::retire(const QString &id)
{
	if (id.isEmpty()) {
		return;
	}
	m_closedIds.insert(id);
	if (m_pending && m_pending->id == id) {
		m_pending.reset();
	}
	if (m_savedId == id) {
		m_savedId.clear();
	}
	m_retired.insert(id);
	removeRetired();
}

bool AudioRecoveryWriter::retain(const QString &id)
{
	if ((m_thread && m_result->id == id) || (m_pending && m_pending->id == id) || m_retired.contains(id)) {
		return false;
	}
	m_closedIds.insert(id);
	m_sessions.remove(id);
	if (m_savedId == id) {
		m_savedId.clear();
	}
	return true;
}

void AudioRecoveryWriter::removeRetired()
{
	for (auto it = m_retired.begin(); it != m_retired.end();) {
		if (m_thread && m_result->id == *it) {
			++it;
			continue;
		}
		QString error;
		const bool removed = m_kind == AudioRecoveryKind::Session ? removeAudioSessionRecovery(m_directory, *it, &error)
		                                                          : removeAudioRecovery(m_directory, *it, &error);
		if (removed) {
			m_sessions.remove(*it);
			it = m_retired.erase(it);
		} else {
			if (finished) {
				finished({}, error);
			}
			++it;
		}
	}
}

void AudioRecoveryWriter::startNext()
{
	if (m_thread || !m_pending) {
		return;
	}
	Snapshot snapshot = std::move(*m_pending);
	m_pending.reset();
	const QString directory = m_directory;
	auto result = std::make_shared<Result>();
	result->id = snapshot.id;
	result->revision = snapshot.revision;
	m_result = result;
	m_thread = QThread::create([snapshot = std::move(snapshot), directory, result]() {
		result->path = writeSnapshot(snapshot, directory, &result->error);
	});
	connect(m_thread, &QThread::finished, this, [this, result]() {
		m_thread->deleteLater();
		m_thread = nullptr;
		const bool retired = m_retired.contains(result->id);
		if (!retired && !result->path.isEmpty()) {
			m_savedId = result->id;
			m_savedRevision = result->revision;
		}
		removeRetired();
		if (!retired && finished) {
			finished(result->path, result->error);
		}
		startNext();
	});
	m_thread->start();
}

} // namespace vibestudio
