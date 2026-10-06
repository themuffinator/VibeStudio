#include "app/model_preview_worker.h"

#include "core/model_archive.h"

#include <QCoreApplication>
#include <QThread>
#include <QTimer>
#include <new>

namespace vibestudio
{
struct ModelPreviewWorker::Work
{
	quint64 serial = 0;
	ModelPreviewResult result;
};

ModelPreviewWorker::ModelPreviewWorker(QObject *parent) : QObject(parent)
{
	m_timer = new QTimer(this);
	m_timer->setSingleShot(true);
	m_timer->setInterval(35);
	connect(m_timer, &QTimer::timeout, this, [this] { startNext(); });
}
ModelPreviewWorker::~ModelPreviewWorker()
{
	reset();
	if (m_thread)
	{
		m_thread->disconnect(this);
		m_thread->wait();
		delete m_thread;
	}
}
bool ModelPreviewWorker::busy() const { return m_thread || m_pending.has_value(); }
void ModelPreviewWorker::reset()
{
	++m_serial;
	m_timer->stop();
	m_pending.reset();
	if (m_thread)
	{
		m_thread->requestInterruption();
	}
}
void ModelPreviewWorker::cancel()
{
	reset();
	ModelPreviewResult result;
	result.key = m_key;
	result.path = m_path;
	result.cancelled = true;
	if (completed)
	{
		completed(result);
	}
}
void ModelPreviewWorker::request(ModelMaterialSource source, QString path, QString key, ModelAppearance appearance)
{
	reset();
	m_key = key;
	m_path = path;
	m_pending = Request{std::move(source), std::move(path), std::move(key), std::move(appearance)};
	m_timer->start();
}
void ModelPreviewWorker::startNext()
{
	if (m_thread || !m_pending)
	{
		return;
	}
	auto request = std::move(*m_pending);
	m_pending.reset();
	auto work = std::make_shared<Work>();
	work->serial = m_serial;
	work->result.key = request.key;
	work->result.path = request.path;
	m_thread = QThread::create(
		[work, request = std::move(request)]
		{
			const auto cancelled = [] { return QThread::currentThread()->isInterruptionRequested(); };
			try
			{
				if (!request.source.archive)
				{
					work->result.error = QCoreApplication::translate("VibeStudioModelMesh", "Open a package before previewing a model.");
					return;
				}
				ModelWorkControl control;
				control.cancelled = cancelled;
				work->result.mesh = decodeModelMeshFromArchive(*request.source.archive, request.path, request.source.paletteId, control);
				work->result.error = work->result.mesh.error;
				if (work->result.error.isEmpty() && !cancelled())
				{
					if (!prepareModelAppearance(work->result.mesh, request.appearance, request.source.archive.get(),
						&work->result.appearance, &work->result.error, control)) {
						work->result.cancelled = cancelled();
						return;
					}
					LevelPreviewAssetOptions options;
					options.paletteId = request.source.paletteId;
					options.materialLimit = 32;
					options.modelLimit = 0;
					options.previewDimension = 1024;
					const ModelArchiveReader reader(*request.source.archive, control);
					work->result.assets =
						resolveModelPreviewAssets(work->result.appearance.materials, reader, options, [&](int, int) { return !cancelled(); });
					if (!cancelled() && work->result.mesh.format != ModelMeshFormat::WavefrontObj)
					{
						PackageReadControl readControl;
						readControl.isCancelled = cancelled;
						work->result.metadata = buildPackageEntryPreview(*request.source.archive, request.path, 65536, 0, readControl);
					}
				}
			}
			catch (const std::bad_alloc &)
			{
				work->result.mesh = {};
				work->result.appearance = {};
				work->result.assets = {};
				work->result.metadata = {};
				work->result.error = QCoreApplication::translate("VibeStudioModelMesh", "Not enough memory to preview this model.");
			}
			work->result.cancelled = cancelled();
		});
	m_thread->setParent(this);
	connect(m_thread, &QThread::finished, this,
			[this, work]
			{
				m_thread->deleteLater();
				m_thread = nullptr;
				if (work->serial == m_serial && completed)
				{
					completed(work->result);
				}
				startNext();
				if (!busy() && settled) { settled(); }
			});
	m_thread->start();
}
} // namespace vibestudio
