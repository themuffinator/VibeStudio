#include "app/model_material_worker.h"
#include "core/model_material_slots.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <new>

namespace vibestudio
{
namespace
{
QByteArray requestKey(const ModelMesh &mesh, const ModelMaterialSource &source)
{
	QCryptographicHash hash(QCryptographicHash::Sha256);
	const auto add = [&](const QString &value)
	{
		hash.addData(value.toUtf8());
		hash.addData(QByteArrayView("\0", 1));
	};
	add(source.revision);
	add(source.archive ? source.archive->sourcePath() : QString());
	add(source.paletteId);
	add(mesh.sourcePath);
	add(QString::number(static_cast<int>(mesh.format)));
	for (const auto &surface : mesh.surfaces)
	{
		add(surface.skinPaths.value(0));
	}
	add(QString::number(mesh.surfaces.size()));
	if (!mesh.embeddedSkins.isEmpty())
	{
		add(QString::number(mesh.embeddedSkins.first().image.cacheKey()));
	}
	return hash.result();
}
} // namespace

struct ModelMaterialWorker::Work
{
	quint64 serial = 0;
	std::atomic_int done = 0, total = 0;
	ModelMaterialResult result;
};

ModelMaterialWorker::ModelMaterialWorker(QObject *parent) : QObject(parent)
{
	m_progressTimer = new QTimer(this);
	m_progressTimer->setInterval(75);
	connect(m_progressTimer, &QTimer::timeout, this,
			[this]
			{
				if (m_work && m_work->serial == m_serial && progress)
				{
					progress(m_work->done.load(), m_work->total.load());
				}
			});
	m_startTimer = new QTimer(this);
	m_startTimer->setSingleShot(true);
	m_startTimer->setInterval(35);
	connect(m_startTimer, &QTimer::timeout, this, [this] { startNext(); });
}

ModelMaterialWorker::~ModelMaterialWorker()
{
	reset();
	if (m_thread)
	{
		m_thread->disconnect(this);
		m_thread->wait();
		delete m_thread;
	}
}

bool ModelMaterialWorker::busy() const { return m_thread || m_pending.has_value(); }

void ModelMaterialWorker::stop(bool forgetKey)
{
	++m_serial;
	m_pending.reset();
	m_work.reset();
	m_startTimer->stop();
	m_progressTimer->stop();
	if (forgetKey)
	{
		m_key.clear();
	}
	if (m_thread)
	{
		m_thread->requestInterruption();
	}
}

void ModelMaterialWorker::reset() { stop(true); }

void ModelMaterialWorker::cancel()
{
	stop(false);
	ModelMaterialResult result;
	result.assets.cancelled = true;
	result.assets.complete = false;
	if (completed)
	{
		completed(result);
	}
}

void ModelMaterialWorker::request(const ModelMesh &mesh, ModelMaterialSource source, const QHash<int, int> &selectedSlots)
{
	ModelMesh snapshot;
	QString error;
	if (!modelMaterialPreviewSnapshot(mesh, selectedSlots, &snapshot, &error))
	{
		stop(true);
		if (started) started();
		ModelMaterialResult result;
		result.assets.complete = false;
		result.error = error;
		if (completed) completed(result);
		return;
	}
	const auto key = requestKey(snapshot, source);
	if (key == m_key)
	{
		return;
	}
	stop(false);
	m_key = key;
	m_pending = Request{std::move(snapshot), std::move(source)};
	if (started)
	{
		started();
	}
	m_startTimer->start();
}

void ModelMaterialWorker::startNext()
{
	if (m_thread || !m_pending)
	{
		return;
	}
	auto request = std::move(*m_pending);
	m_pending.reset();
	auto work = std::make_shared<Work>();
	work->serial = m_serial;
	m_work = work;
	m_thread = QThread::create(
		[work, request = std::move(request)]
		{
			try
			{
				PackageArchive empty;
				LevelPreviewAssetOptions options;
				options.paletteId = request.source.paletteId;
				options.materialLimit = 32;
				options.modelLimit = 0;
				options.previewDimension = 1024;
				work->result.assets =
					resolveModelPreviewAssets(request.mesh, request.source.archive ? *request.source.archive : empty, options,
											  [work](int done, int total)
											  {
												  work->done = done;
												  work->total = total;
												  return !QThread::currentThread()->isInterruptionRequested();
											  });
			}
			catch (const std::bad_alloc &)
			{
				work->result.error =
					QCoreApplication::translate("VibeStudioModelEditor", "Not enough memory to load the model's material images.");
			}
		});
	m_thread->setParent(this);
	connect(m_thread, &QThread::finished, this,
			[this, work]
			{
				m_thread->deleteLater();
				m_thread = nullptr;
				m_progressTimer->stop();
				if (work->serial == m_serial && completed)
				{
					completed(work->result);
				}
				startNext();
			});
	m_progressTimer->start();
	m_thread->start();
}
} // namespace vibestudio
