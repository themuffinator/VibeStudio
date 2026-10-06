#include "app/level_preview_worker.h"
#include "core/map_assets.h"
#include "core/doom_preview_geometry.h"
#include "core/level_model_appearance.h"
#include "core/package_staging.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QThread>
#include <QTimer>
#include <atomic>

namespace vibestudio
{
namespace
{
QString materialKey(const LevelPreviewRequest &request)
{
	QCryptographicHash hash(QCryptographicHash::Sha256);
	const auto add = [&](const QString &value) {
		hash.addData(value.toUtf8());
		hash.addData(QByteArrayView("\0", 1));
	};
	add(request.assetKey);
	add(request.options.paletteId);
	add(request.document.engineFamily);
	add(QString::number(static_cast<int>(request.document.format)));
	for (const auto &use : levelMapTextureUsage(request.document)) {
		add(use.name);
	}
	if (request.document.format == LevelMapFormat::DoomWad) {
		QStringList materialKinds;
		for (const auto& side : request.document.doomSidedefs) {
			for (const auto& name : {side.upperTexture, side.lowerTexture, side.middleTexture}) { materialKinds << doomPreviewMaterialKey(name, false); }
		}
		for (const auto& sector : request.document.doomSectors) {
			materialKinds << doomPreviewMaterialKey(sector.floorTexture, true) << doomPreviewMaterialKey(sector.ceilingTexture, true);
		}
		materialKinds.removeDuplicates(); materialKinds.sort();
		for (const auto& material : materialKinds) { add(material); }
	}
	QStringList models;
	for (const auto &entity : request.document.entities) {
		const auto appearance = levelModelAppearance(request.document, entity);
		if (!appearance.modelPath.isEmpty()) {
			models << QString::number(entity.id) + QLatin1Char(':') + appearance.cacheKey;
		}
	}
	models.removeDuplicates();
	models.sort();
	for (const auto &model : models) {
		add(model);
	}
	add(QString::number(request.options.materialLimit));
	add(QString::number(request.options.modelLimit));
	add(QString::number(request.options.entryByteLimit));
	add(QString::number(request.options.totalReadByteLimit));
	add(QString::number(request.options.imageByteLimit));
	add(QString::number(request.options.previewDimension));
	return QString::fromLatin1(hash.result().toHex());
}
} // namespace
struct LevelPreviewWorker::Work {
	std::atomic_bool cancelled{false};
	std::atomic_int done{0}, total{0};
	quint64 serial = 0;
	QString cacheKey;
	MapBrushGeometryCache geometryCache;
	LevelPreviewResult result;
};
LevelPreviewWorker::LevelPreviewWorker(QObject *parent) : QObject(parent)
{
	m_timer = new QTimer(this);
	m_timer->setInterval(100);
	connect(m_timer, &QTimer::timeout, this, [this] {
		if (m_work && m_work->serial == m_serial && progress) {
			progress(m_work->done.load(), m_work->total.load());
		}
	});
	m_debounce = new QTimer(this);
	m_debounce->setSingleShot(true);
	m_debounce->setInterval(60);
	connect(m_debounce, &QTimer::timeout, this, &LevelPreviewWorker::startPending);
}
LevelPreviewWorker::~LevelPreviewWorker()
{
	reset();
	if (m_thread) {
		m_thread->disconnect(this);
		m_thread->wait();
		delete m_thread;
	}
}
bool LevelPreviewWorker::busy() const { return m_thread || m_pending.has_value(); }
void LevelPreviewWorker::reset()
{
	++m_serial;
	m_pending.reset();
	if (m_work) {
		m_work->cancelled = true;
	}
	m_timer->stop();
	m_debounce->stop();
}
void LevelPreviewWorker::cancel()
{
	LevelPreviewResult result;
	if (m_pending) {
		result.sourceKey = m_pending->sourceKey;
		result.loadSerial = m_pending->loadSerial;
		result.revision = m_pending->document.revision;
	} else if (m_work) {
		result.sourceKey = m_work->result.sourceKey;
		result.loadSerial = m_work->result.loadSerial;
		result.revision = m_work->result.revision;
	}
	reset();
	result.assets.cancelled = true;
	result.assets.complete = false;
	if (completed) {
		completed(result);
	}
}
void LevelPreviewWorker::request(LevelPreviewRequest request)
{
	reset();
	m_pending = std::move(request);
	if (started) {
		started();
	}
	m_debounce->start();
}
void LevelPreviewWorker::startPending()
{
	if (m_thread || !m_pending) {
		return;
	}
	auto request = std::move(*m_pending);
	m_pending.reset();
	auto work = std::make_shared<Work>();
	m_work = work;
	work->serial = m_serial;
	// A value snapshot keeps mutable cache state confined to the worker. A
	// retired request can never update the next request's published cache.
	work->geometryCache = m_cachedGeometry;
	work->result.sourceKey = request.sourceKey;
	work->result.loadSerial = request.loadSerial;
	work->result.revision = request.document.revision;
	// Building the material-set key can itself walk a large map. Keep it off
	// the GUI thread along with decoding and geometry reconstruction.
	m_thread = QThread::create([work, request = std::move(request), cacheKey = m_cacheKey, assets = m_cachedAssets]() mutable {
		try {
			work->cacheKey = materialKey(request);
			const bool cached = work->cacheKey == cacheKey;
			if (cached) {
				work->result.assets = assets;
			}
			if (!cached) {
				if (request.staging) {
					PackageReadControl control; control.isCancelled = [work] { return work->cancelled.load(); };
					request.archive = std::make_shared<PackageStagingArchive>(*request.staging, PackageStagingReadMode::CompletePlan, control);
				}
				PackageArchive empty;
				work->result.assets = resolveLevelPreviewAssets(request.document, request.archive ? *request.archive : empty,
																request.options, [work](int done, int total) {
																	work->done = done;
																	work->total = total;
																	return !work->cancelled.load();
																});
			}
			if (work->cancelled || work->result.assets.cancelled) {
				return;
			}
			work->done = 0;
			work->total = 0;
			LevelMapPreviewMeshOptions options;
			options.modelMeshes = work->result.assets.models;
			options.textureSizes = levelPreviewTextureSizes(work->result.assets);
			options.isCancelled = [work] { return work->cancelled.load(); };
			options.brushGeometryCache = &work->geometryCache;
			work->result.preview = buildLevelMapPreviewMesh(request.document, options);
			work->result.geometryCache = work->geometryCache.statistics();
		} catch (const std::exception &error) {
			work->result.error = QString::fromUtf8(error.what());
		} catch (...) {
			work->result.error = QCoreApplication::translate("LevelPreviewWorker", "Unable to build the level preview.");
		}
	});
	connect(m_thread, &QThread::finished, this, [this, work] {
		auto *thread = m_thread;
		m_thread = nullptr;
		thread->wait();
		thread->deleteLater();
		m_timer->stop();
		m_work.reset();
		if (work->serial == m_serial && !work->cancelled) {
			if (work->result.error.isEmpty() && !work->result.assets.cancelled) {
				m_cacheKey = work->cacheKey;
				m_cachedAssets = work->result.assets;
				if (!work->result.preview.cancelled) { m_cachedGeometry = work->geometryCache; }
			}
			if (completed) {
				completed(work->result);
			}
		}
		if (!m_debounce->isActive()) {
			startPending();
		}
	});
	m_timer->start();
	m_thread->start();
}
} // namespace vibestudio
