#include "app/model_recovery_writer.h"

#include <QCoreApplication>
#include <QThread>

namespace vibestudio
{

ModelRecoveryWriter::ModelRecoveryWriter(QString directory, QObject *parent) : QObject(parent), m_directory(std::move(directory)) {}

ModelRecoveryWriter::~ModelRecoveryWriter()
{
	finished = {};
	if (m_thread)
	{
		m_thread->wait();
		delete m_thread;
		m_thread = nullptr;
	}
	// Approved Save/Discard retires the ID before destruction. Unexpected
	// destruction preserves the last accepted pending edit instead of dropping it.
	if (m_pending && !m_retired.contains(m_pending->id))
	{
		try
		{
			writeModelRecovery(m_pending->snapshot, m_directory, m_pending->id);
		}
		catch (const std::bad_alloc &)
		{
			// Destruction cannot report an error. Retain the preceding atomic copy
			// instead of terminating the application while flushing a newer one.
		}
	}
	removeRetired();
}

bool ModelRecoveryWriter::busy() const { return m_thread || m_pending.has_value(); }

void ModelRecoveryWriter::checkpoint(const QString &id, quint64 revision, ModelRecoverySnapshot snapshot)
{
	if (m_retired.contains(id) || (m_pending && m_pending->id == id && m_pending->revision == revision) ||
		(!m_pending && m_thread && m_result->id == id && m_result->revision == revision) ||
		(!m_pending && !m_thread && m_savedId == id && m_savedRevision == revision))
	{
		return;
	}
	m_pending = Pending{id, revision, std::move(snapshot)};
	startNext();
}

void ModelRecoveryWriter::retire(const QString &id)
{
	if (id.isEmpty())
	{
		return;
	}
	m_retired.insert(id);
	m_removals.insert(id);
	if (m_pending && m_pending->id == id)
	{
		m_pending.reset();
	}
	if (m_savedId == id)
	{
		m_savedId.clear();
	}
	if (m_thread && m_result->id == id)
	{
		m_thread->requestInterruption();
	}
	removeRetired();
}

void ModelRecoveryWriter::cancelPending()
{
	m_pending.reset();
	if (m_thread)
	{
		m_thread->requestInterruption();
	}
}

void ModelRecoveryWriter::removeRetired()
{
	for (auto it = m_removals.begin(); it != m_removals.end();)
	{
		if (m_thread && m_result->id == *it)
		{
			++it;
			continue;
		}
		QString error;
		if (removeModelRecovery(m_directory, *it, &error))
		{
			it = m_removals.erase(it);
		}
		else
		{
			if (finished)
			{
				finished({}, error);
			}
			++it;
		}
	}
}

void ModelRecoveryWriter::startNext()
{
	if (m_thread || !m_pending)
	{
		return;
	}
	auto pending = std::move(*m_pending);
	m_pending.reset();
	auto result = std::make_shared<Result>();
	result->id = pending.id;
	result->revision = pending.revision;
	m_result = result;
	const auto directory = m_directory;
	m_thread = QThread::create(
		[pending = std::move(pending), result, directory]()
		{
			try
			{
				result->path = writeModelRecovery(pending.snapshot, directory, result->id, &result->error,
												  [] { return QThread::currentThread()->isInterruptionRequested(); });
			}
			catch (const std::bad_alloc &)
			{
				result->error = QCoreApplication::translate("VibeStudioModelRecovery", "Not enough memory to write the recovery copy.");
			}
		});
	m_thread->setParent(this);
	connect(m_thread, &QThread::finished, this,
			[this, result]()
			{
				const bool cancelled = m_thread->isInterruptionRequested();
				m_thread->deleteLater();
				m_thread = nullptr;
				const bool retired = m_retired.contains(result->id);
				if (!retired && !cancelled && !result->path.isEmpty())
				{
					m_savedId = result->id;
					m_savedRevision = result->revision;
				}
				removeRetired();
				if (!retired && !cancelled && finished)
				{
					finished(result->path, result->error);
				}
				startNext();
			});
	m_thread->start();
}

} // namespace vibestudio
