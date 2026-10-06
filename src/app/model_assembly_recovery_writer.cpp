#include "app/model_assembly_recovery_writer.h"

#include <QCoreApplication>
#include <QThread>
#include <QUuid>

namespace vibestudio
{
struct ModelAssemblyRecoveryWriter::Result
{
	QString path, error;
	QByteArray key;
	std::shared_ptr<ModelAssemblyRecoverySession> session;
	bool retirement = false;
};
ModelAssemblyRecoveryWriter::ModelAssemblyRecoveryWriter(QString directory, QObject *parent)
	: QObject(parent), m_directory(std::move(directory)), m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
}
ModelAssemblyRecoveryWriter::~ModelAssemblyRecoveryWriter()
{
	finished = {};
	retired = {};
	if (m_thread)
	{
		m_thread->wait();
		m_session = m_result->session;
		delete m_thread;
	}
	// Normal Save/Discard calls retire first. Unexpected teardown flushes the
	// last accepted pending edit; a write failure leaves the prior atomic copy.
	try
	{
		if (m_preserving)
		{
		}
		else if (m_retiring)
		{
			if (m_session)
			{
				m_session->retire();
			}
		}
		else if (m_pending)
		{
			if (!m_session)
			{
				m_session = ModelAssemblyRecoverySession::acquire(m_directory, m_id);
			}
			if (m_session)
			{
				m_session->write(*m_pending);
			}
		}
	}
	catch (const std::bad_alloc &)
	{
	}
}
bool ModelAssemblyRecoveryWriter::checkpoint(ModelAssemblyRecoverySnapshot snapshot)
{
	if (m_retiring)
	{
		return false;
	}
	const auto key = modelAssemblyRecoveryFingerprint(snapshot);
	if ((m_pending && modelAssemblyRecoveryFingerprint(*m_pending) == key) ||
		(!m_pending && m_thread && !m_thread->isInterruptionRequested() && m_result->key == key) ||
		(!m_pending && !m_thread && m_savedKey == key))
	{
		return false;
	}
	m_pending = std::move(snapshot);
	startNext();
	return true;
}
void ModelAssemblyRecoveryWriter::retire()
{
	if (m_retiring)
	{
		return;
	}
	m_retiring = true;
	m_pending.reset();
	if (m_thread)
	{
		m_thread->requestInterruption();
	}
	startNext();
}
void ModelAssemblyRecoveryWriter::preserve()
{
	if (m_retiring)
	{
		return;
	}
	m_preserving = m_retiring = true;
	m_pending.reset();
	startNext();
}
void ModelAssemblyRecoveryWriter::pause()
{
	m_pending.reset();
	if (m_thread)
	{
		m_thread->requestInterruption();
	}
}
void ModelAssemblyRecoveryWriter::startNext()
{
	if (m_thread || (!m_pending && !m_retiring))
	{
		return;
	}
	if (m_preserving)
	{
		m_session.reset();
		m_result.reset();
		if (retired)
		{
			retired();
		}
		return;
	}
	if (m_retiring && !m_session)
	{
		if (retired)
		{
			retired();
		}
		return;
	}
	auto pending = std::move(m_pending);
	m_pending.reset();
	auto result = std::make_shared<Result>();
	result->session = m_session;
	result->retirement = m_retiring;
	if (pending)
	{
		result->key = modelAssemblyRecoveryFingerprint(*pending);
	}
	m_result = result;
	const auto directory = m_directory, id = m_id;
	m_thread = QThread::create(
		[pending = std::move(pending), result, directory, id]
		{
			try
			{
				if (result->retirement)
				{
					result->session->retire(&result->error);
					result->session.reset();
					return;
				}
				if (!result->session)
				{
					result->session = ModelAssemblyRecoverySession::acquire(directory, id, &result->error);
				}
				ModelWorkControl control;
				control.cancelled = [] { return QThread::currentThread()->isInterruptionRequested(); };
				if (result->session && result->session->write(*pending, &result->error, control))
				{
					result->path = result->session->path();
				}
			}
			catch (const std::bad_alloc &)
			{
				result->error =
					QCoreApplication::translate("ModelAssemblyRecovery", "Not enough memory to write the assembly recovery copy.");
			}
		});
	m_thread->setParent(this);
	connect(m_thread, &QThread::finished, this,
			[this, result]
			{
				m_thread->deleteLater();
				m_thread = nullptr;
				m_session = result->session;
				if (result->retirement)
				{
					if (!result->error.isEmpty() && finished)
					{
						finished({}, result->error);
					}
					if (retired)
					{
						retired();
					}
					return;
				}
				if (!m_retiring)
				{
					if (!result->path.isEmpty())
					{
						m_savedKey = result->key;
					}
					if (finished)
					{
						finished(result->path, result->error);
					}
				}
				startNext();
			});
	m_thread->start();
}
} // namespace vibestudio
