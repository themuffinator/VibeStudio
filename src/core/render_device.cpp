#include "core/render_device.h"

#include "core/render_device_p.h"
#include "core/render_shaders.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QSet>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vibestudio {

// ---------------------------------------------------------------------------
// Identifiers
// ---------------------------------------------------------------------------

QString renderBackendId(RenderBackend backend)
{
	switch (backend) {
	case RenderBackend::OpenGL:
		return QStringLiteral("opengl");
	case RenderBackend::Vulkan:
		return QStringLiteral("vulkan");
	}
	return QStringLiteral("vulkan");
}

QString renderBackendDisplayName(RenderBackend backend)
{
	switch (backend) {
	case RenderBackend::OpenGL:
		return QStringLiteral("OpenGL");
	case RenderBackend::Vulkan:
		return QStringLiteral("Vulkan");
	}
	return QStringLiteral("Vulkan");
}

bool renderBackendFromId(const QString& id, RenderBackend* backend)
{
	const QString key = id.trimmed().toLower();
	for (RenderBackend candidate : renderBackends()) {
		if (key == renderBackendId(candidate)) {
			if (backend) {
				*backend = candidate;
			}
			return true;
		}
	}
	return false;
}

QVector<RenderBackend> renderBackends()
{
	return {RenderBackend::OpenGL, RenderBackend::Vulkan};
}

QString renderBackendChoiceId(RenderBackendChoice choice)
{
	switch (choice) {
	case RenderBackendChoice::Automatic:
		return QStringLiteral("automatic");
	case RenderBackendChoice::OpenGL:
		return QStringLiteral("opengl");
	case RenderBackendChoice::Vulkan:
		return QStringLiteral("vulkan");
	}
	return QStringLiteral("automatic");
}

QString renderBackendChoiceDisplayName(RenderBackendChoice choice)
{
	switch (choice) {
	case RenderBackendChoice::Automatic:
		return QCoreApplication::translate("VibeStudioRendering", "Automatic", "3D renderer choice: the first graphics API that works");
	case RenderBackendChoice::OpenGL:
		return renderBackendDisplayName(RenderBackend::OpenGL);
	case RenderBackendChoice::Vulkan:
		return renderBackendDisplayName(RenderBackend::Vulkan);
	}
	return {};
}

bool renderBackendChoiceFromId(const QString& id, RenderBackendChoice* choice)
{
	const QString key = id.trimmed().toLower();
	for (RenderBackendChoice candidate : renderBackendChoices()) {
		if (key == renderBackendChoiceId(candidate)) {
			if (choice) {
				*choice = candidate;
			}
			return true;
		}
	}
	return false;
}

QVector<RenderBackendChoice> renderBackendChoices()
{
	return {RenderBackendChoice::Automatic, RenderBackendChoice::OpenGL, RenderBackendChoice::Vulkan};
}

QVector<RenderBackend> automaticRenderBackendOrder()
{
#ifdef Q_OS_MACOS
	return {RenderBackend::OpenGL, RenderBackend::Vulkan};
#else
	return {RenderBackend::Vulkan, RenderBackend::OpenGL};
#endif
}

// ---------------------------------------------------------------------------
// Results
// ---------------------------------------------------------------------------

QImage GpuReadback::image(QImage::Format imageFormat) const
{
	if (size.isEmpty()) {
		return {};
	}
	const int width = size.width();
	const int height = size.height();
	const qsizetype pixels = qsizetype(width) * height;
	const QImage::Format output = imageFormat == QImage::Format_ARGB32 ? QImage::Format_ARGB32 : QImage::Format_ARGB32_Premultiplied;
	if (format == GpuFormat::Rgba8) {
		if (bytes.size() < pixels * 4) {
			return {};
		}
		QImage result(size, output);
		if (result.isNull()) {
			return {};
		}
		for (int y = 0; y < height; ++y) {
			const uchar* row = reinterpret_cast<const uchar*>(bytes.constData()) + qsizetype(y) * width * 4;
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
			std::memcpy(result.scanLine(y), row, size_t(width) * 4);
#else
			auto* line = reinterpret_cast<QRgb*>(result.scanLine(y));
			for (int x = 0; x < width; ++x) {
				const uchar* p = row + x * 4;
				line[x] = qRgba(p[2], p[1], p[0], p[3]);
			}
#endif
		}
		return result;
	}
	if (format == GpuFormat::Rgba16) {
		if (bytes.size() < pixels * 8) {
			return {};
		}
		QImage result(size, output);
		if (result.isNull()) {
			return {};
		}
		const auto* source = reinterpret_cast<const quint16*>(bytes.constData());
		const auto eight = [](quint16 value) { return int((quint32(value) * 255 + 32767) / 65535); };
		for (int y = 0; y < height; ++y) {
			auto* line = reinterpret_cast<QRgb*>(result.scanLine(y));
			const quint16* row = source + qsizetype(y) * width * 4;
			for (int x = 0; x < width; ++x) {
				const quint16* p = row + x * 4;
				line[x] = qRgba(eight(p[0]), eight(p[1]), eight(p[2]), eight(p[3]));
			}
		}
		return result;
	}
	return {};
}

QVector<int> GpuReadback::integers() const
{
	const qsizetype pixels = qsizetype(size.width()) * size.height();
	if (format != GpuFormat::R32Int || pixels <= 0 || bytes.size() < pixels * 4) {
		return {};
	}
	QVector<int> result(pixels);
	std::memcpy(result.data(), bytes.constData(), size_t(pixels) * 4);
	return result;
}

QVector<float> GpuReadback::floats() const
{
	const qsizetype pixels = qsizetype(size.width()) * size.height();
	if ((format != GpuFormat::R32Float && format != GpuFormat::Depth32) || pixels <= 0 || bytes.size() < pixels * 4) {
		return {};
	}
	QVector<float> result(pixels);
	std::memcpy(result.data(), bytes.constData(), size_t(pixels) * 4);
	return result;
}

const GpuReadback* GpuFrameResult::readback(int target) const
{
	for (const GpuReadback& item : readbacks) {
		if (item.target == target) {
			return &item;
		}
	}
	return nullptr;
}

RenderDevice::~RenderDevice() = default;

// ---------------------------------------------------------------------------
// Device thread
// ---------------------------------------------------------------------------

namespace render_detail {

DeviceThread::DeviceThread(RenderBackend backend)
	: m_backend(backend)
{
	m_info.backend = backend;
}

DeviceThread::~DeviceThread()
{
	// Backends stop in their own destructors, while their state still exists.
	Q_ASSERT(!m_thread.joinable());
}

void DeviceThread::ensureThread()
{
	if (m_threadStarted) {
		return;
	}
	m_threadStarted = true;
	m_thread = std::thread([this]() { run(); });
}

void DeviceThread::startAsync()
{
	std::lock_guard lock(m_mutex);
	if (!m_stopping) {
		ensureThread();
	}
}

void DeviceThread::waitUntilStarted()
{
	std::unique_lock lock(m_mutex);
	if (m_stopping && !m_threadStarted) {
		return;
	}
	ensureThread();
	m_settled.wait(lock, [this]() { return m_started; });
}

void DeviceThread::run()
{
	RenderDeviceInfo info;
	info.backend = m_backend;
	QElapsedTimer timer;
	timer.start();
	bool available = false;
	try {
		available = startDevice(&info);
	} catch (const std::exception& error) {
		available = false;
		info.error = QCoreApplication::translate("VibeStudioRendering", "The %1 renderer failed while starting.").arg(renderBackendDisplayName(m_backend));
		info.errorDetail = QString::fromLocal8Bit(error.what());
	}
	info.backend = m_backend;
	info.started = true;
	info.available = available;
	info.startupMilliseconds = timer.elapsed();
	if (!available && info.error.isEmpty()) {
		info.error = QCoreApplication::translate("VibeStudioRendering", "%1 could not start on this computer.").arg(renderBackendDisplayName(m_backend));
	}
	{
		std::lock_guard lock(m_mutex);
		m_info = info;
		m_started = true;
	}
	m_settled.notify_all();
	for (;;) {
		std::function<void()> job;
		{
			std::unique_lock lock(m_mutex);
			m_wake.wait(lock, [this]() { return m_stopping || !m_jobs.empty(); });
			if (m_jobs.empty()) {
				break;
			}
			job = std::move(m_jobs.front());
			m_jobs.pop_front();
		}
		job();
	}
	if (available) {
		stopDevice();
	}
}

RenderDeviceInfo DeviceThread::info()
{
	waitUntilStarted();
	std::lock_guard lock(m_mutex);
	return m_info;
}

bool DeviceThread::failed()
{
	std::lock_guard lock(m_mutex);
	return m_lost;
}

RenderDeviceInfo DeviceThread::snapshot()
{
	std::lock_guard lock(m_mutex);
	return m_info;
}

void DeviceThread::markLost(const QString& detail)
{
	std::lock_guard lock(m_mutex);
	m_lost = true;
	m_info.available = false;
	m_info.error = QCoreApplication::translate("VibeStudioRendering",
		"The graphics device stopped responding. The %1 renderer restarts with the next frame.").arg(renderBackendDisplayName(m_backend));
	m_info.errorDetail = detail;
}

GpuFrameResult DeviceThread::render(const GpuFrame& frame, const std::atomic_bool* cancelled)
{
	GpuFrameResult result;
	if (cancelled && cancelled->load()) {
		result.cancelled = true;
		return result;
	}
	waitUntilStarted();
	{
		std::lock_guard lock(m_mutex);
		if (!m_info.available || m_lost || m_stopping) {
			result.error = m_info.error;
			result.errorDetail = m_info.errorDetail;
			if (result.error.isEmpty()) {
				result.error = QCoreApplication::translate("VibeStudioRendering", "The %1 renderer is not running.").arg(renderBackendDisplayName(m_backend));
			}
			return result;
		}
	}
	QString detail;
	const QString invalid = validateFrame(frame, &detail);
	if (!invalid.isEmpty()) {
		result.error = invalid;
		result.errorDetail = detail;
		return result;
	}
	std::mutex doneMutex;
	std::condition_variable doneCondition;
	bool done = false;
	{
		std::lock_guard lock(m_mutex);
		m_jobs.push_back([&]() {
			QElapsedTimer timer;
			timer.start();
			GpuFrameResult rendered;
			if (cancelled && cancelled->load()) {
				rendered.cancelled = true;
			} else {
				try {
					rendered = renderFrame(frame, cancelled);
				} catch (const std::bad_alloc&) {
					rendered = {};
					rendered.error = QCoreApplication::translate("VibeStudioRendering", "There was not enough memory to render this view.");
					rendered.errorDetail = QStringLiteral("std::bad_alloc");
				}
			}
			rendered.milliseconds = double(timer.nsecsElapsed()) / 1.0e6;
			{
				std::lock_guard guard(doneMutex);
				result = std::move(rendered);
				done = true;
			}
			doneCondition.notify_all();
		});
	}
	m_wake.notify_one();
	std::unique_lock wait(doneMutex);
	doneCondition.wait(wait, [&]() { return done; });
	return result;
}

void DeviceThread::releaseOwner(quint64 owner)
{
	{
		std::lock_guard lock(m_mutex);
		if (!m_threadStarted || m_stopping) {
			return;
		}
		m_jobs.push_back([this, owner]() {
			if (m_info.available) {
				releaseOwnerOnDevice(owner);
			}
		});
	}
	m_wake.notify_one();
}

bool DeviceThread::supportsFormat(GpuFormat format)
{
	waitUntilStarted();
	{
		std::lock_guard lock(m_mutex);
		if (!m_info.available) {
			return false;
		}
	}
	return formatSupportedOnDevice(format);
}

void DeviceThread::stop()
{
	{
		std::lock_guard lock(m_mutex);
		m_stopping = true;
	}
	m_wake.notify_all();
	if (m_thread.joinable()) {
		m_thread.join();
	}
}

// ---------------------------------------------------------------------------
// Frame checks
// ---------------------------------------------------------------------------

namespace {

constexpr int kMaximumTargetSide = 16384;

bool isColorFormat(GpuFormat format)
{
	return format != GpuFormat::Depth32;
}

} // namespace

QSize targetSize(const GpuFrame& frame, int target)
{
	if (target < 0 || target >= frame.targets.size()) {
		return {};
	}
	const QSize size = frame.targets.at(target).size;
	return size.isEmpty() ? frame.size : size;
}

QString validateFrame(const GpuFrame& frame, QString* detail)
{
	const auto fail = [&](const QString& why) {
		if (detail) {
			*detail = why;
		}
		return QCoreApplication::translate("VibeStudioRendering", "The view could not be drawn because its frame was malformed.");
	};
	if (frame.size.isEmpty() || frame.size.width() > kMaximumTargetSide || frame.size.height() > kMaximumTargetSide) {
		return fail(QStringLiteral("frame size %1x%2 outside 1..%3").arg(frame.size.width()).arg(frame.size.height()).arg(kMaximumTargetSide));
	}
	for (int index = 0; index < frame.targets.size(); ++index) {
		const QSize size = targetSize(frame, index);
		if (size.isEmpty() || size.width() > kMaximumTargetSide || size.height() > kMaximumTargetSide) {
			return fail(QStringLiteral("target %1 has size %2x%3").arg(index).arg(size.width()).arg(size.height()));
		}
	}
	for (int index = 0; index < frame.textures.size(); ++index) {
		const GpuTextureData& texture = frame.textures.at(index);
		if (texture.levels.isEmpty() || texture.levels.first().isNull()) {
			return fail(QStringLiteral("texture %1 has no image").arg(index));
		}
		QSize expected = texture.levels.first().size();
		if (expected.width() > kMaximumTargetSide || expected.height() > kMaximumTargetSide) {
			return fail(QStringLiteral("texture %1 is larger than %2 pixels").arg(index).arg(kMaximumTargetSide));
		}
		for (int level = 1; level < texture.levels.size(); ++level) {
			expected = QSize(std::max(1, expected.width() / 2), std::max(1, expected.height() / 2));
			if (texture.levels.at(level).size() != expected) {
				return fail(QStringLiteral("texture %1 level %2 is not half the size of the level above").arg(index).arg(level));
			}
		}
	}
	for (int passIndex = 0; passIndex < frame.passes.size(); ++passIndex) {
		const GpuPass& pass = frame.passes.at(passIndex);
		QSize size;
		QSet<int> attached;
		bool any = false;
		for (int slot = 0; slot < kGpuMaxColorTargets; ++slot) {
			const int target = pass.colors[size_t(slot)];
			if (target < 0) {
				continue;
			}
			if (target >= frame.targets.size() || !isColorFormat(frame.targets.at(target).format) || attached.contains(target)) {
				return fail(QStringLiteral("pass %1 colour slot %2 names target %3, which is missing, a depth target, or attached twice").arg(passIndex).arg(slot).arg(target));
			}
			attached.insert(target);
			const QSize targetSz = targetSize(frame, target);
			if (any && targetSz != size) {
				return fail(QStringLiteral("pass %1 attaches targets of different sizes").arg(passIndex));
			}
			size = targetSz;
			any = true;
		}
		if (pass.depth >= 0) {
			if (pass.depth >= frame.targets.size() || frame.targets.at(pass.depth).format != GpuFormat::Depth32) {
				return fail(QStringLiteral("pass %1 depth names target %2, which is missing or not a depth target").arg(passIndex).arg(pass.depth));
			}
			attached.insert(pass.depth);
			const QSize depthSize = targetSize(frame, pass.depth);
			if (any && depthSize != size) {
				return fail(QStringLiteral("pass %1 depth size differs from its colour targets").arg(passIndex));
			}
			size = depthSize;
			any = true;
		}
		if (!any) {
			return fail(QStringLiteral("pass %1 has no attachments").arg(passIndex));
		}
		for (int drawIndex = 0; drawIndex < pass.draws.size(); ++drawIndex) {
			const GpuDraw& draw = pass.draws.at(drawIndex);
			const GpuProgramInfo* program = gpuProgramInfo(draw.program);
			const QString where = QStringLiteral("pass %1 draw %2").arg(passIndex).arg(drawIndex);
			if (!program) {
				return fail(QStringLiteral("%1 names unknown program %2").arg(where).arg(draw.program));
			}
			if (draw.count < 0 || draw.instances < 1 || draw.uniforms.size() > kGpuMaxUniformBytes) {
				return fail(QStringLiteral("%1 has count %2, instances %3, %4 uniform bytes").arg(where).arg(draw.count).arg(draw.instances).arg(draw.uniforms.size()));
			}
			qint64 vertices = -1;
			if (draw.indexBuffer.buffer >= 0) {
				if (draw.indexBuffer.buffer >= frame.buffers.size() || draw.indexBuffer.offset < 0 || draw.indexBuffer.offset % 4 != 0
					|| draw.indexBuffer.offset + qint64(draw.count) * 4 > frame.buffers.at(draw.indexBuffer.buffer).bytes.size()) {
					return fail(QStringLiteral("%1 reads indices outside its buffer").arg(where));
				}
				const auto* indices = reinterpret_cast<const quint32*>(frame.buffers.at(draw.indexBuffer.buffer).bytes.constData() + draw.indexBuffer.offset);
				for (int i = 0; i < draw.count; ++i) {
					vertices = std::max<qint64>(vertices, qint64(indices[i]) + 1);
				}
			} else {
				vertices = draw.count;
			}
			for (int slot = 0; slot < 2; ++slot) {
				const GpuVertexBinding& binding = program->bindings[size_t(slot)];
				const GpuBufferRef& ref = draw.vertexBuffers[size_t(slot)];
				if (binding.stride <= 0) {
					continue;
				}
				const qint64 needed = binding.perInstance ? draw.instances : vertices;
				if (ref.buffer < 0 || ref.buffer >= frame.buffers.size() || ref.offset < 0
					|| ref.offset + std::max<qint64>(needed, 0) * binding.stride > frame.buffers.at(ref.buffer).bytes.size()) {
					return fail(QStringLiteral("%1 reads vertex buffer %2 outside its data").arg(where).arg(slot));
				}
			}
			for (int slot = 0; slot < kGpuMaxTextures; ++slot) {
				const GpuTextureRef& texture = draw.textures[size_t(slot)];
				if (slot >= program->samplerCount) {
					if (texture.kind != GpuTextureRef::Kind::None) {
						return fail(QStringLiteral("%1 binds texture slot %2, which its program does not read").arg(where).arg(slot));
					}
					continue;
				}
				switch (texture.kind) {
				case GpuTextureRef::Kind::None:
					break;
				case GpuTextureRef::Kind::Texture:
					if (texture.index < 0 || texture.index >= frame.textures.size()) {
						return fail(QStringLiteral("%1 slot %2 names missing texture %3").arg(where).arg(slot).arg(texture.index));
					}
					break;
				case GpuTextureRef::Kind::Target:
					if (texture.index < 0 || texture.index >= frame.targets.size() || !isColorFormat(frame.targets.at(texture.index).format)
						|| attached.contains(texture.index)) {
						return fail(QStringLiteral("%1 slot %2 samples target %3, which is missing, a depth target, or drawn by this pass").arg(where).arg(slot).arg(texture.index));
					}
					break;
				}
			}
			if (!draw.scissor.isEmpty() && !QRect(QPoint(), size).contains(draw.scissor)) {
				return fail(QStringLiteral("%1 scissor lies outside its targets").arg(where));
			}
		}
	}
	for (int target : frame.readbacks) {
		if (target < 0 || target >= frame.targets.size()) {
			return fail(QStringLiteral("readback of missing target %1").arg(target));
		}
	}
	return {};
}

QImage textureLevelImage(const QImage& image)
{
	const bool premultiplied = image.pixelFormat().premultiplied() == QPixelFormat::Premultiplied;
	return image.convertToFormat(premultiplied ? QImage::Format_RGBA8888_Premultiplied : QImage::Format_RGBA8888);
}

QByteArray paddedUniforms(const GpuDraw& draw, int programBytes)
{
	QByteArray bytes = draw.uniforms;
	if (bytes.size() < programBytes) {
		bytes.append(QByteArray(programBytes - bytes.size(), '\0'));
	}
	return bytes;
}

} // namespace render_detail

// ---------------------------------------------------------------------------
// Device manager
// ---------------------------------------------------------------------------

namespace {

using render_detail::DeviceThread;

struct Manager {
	std::mutex mutex;
	std::array<std::shared_ptr<DeviceThread>, 2> devices;
	RenderBackendChoice choice = RenderBackendChoice::Automatic;
	// setRenderBackendChoiceOverride(): ahead of the environment variable.
	bool overridden = false;
	RenderBackendChoice overrideChoice = RenderBackendChoice::Automatic;
	QOffscreenSurface* surface = nullptr;
	bool postRoutine = false;
	std::atomic<quint64> generation {1};
	std::atomic<quint64> nextOwner {1};
};

// Deliberately never destroyed: device threads stop in the post routine,
// while the application object (and OpenGL's platform) still exists.
Manager& manager()
{
	static Manager* instance = new Manager;
	return *instance;
}

std::size_t slot(RenderBackend backend)
{
	return backend == RenderBackend::OpenGL ? 0 : 1;
}

struct EnvironmentChoice {
	bool set = false;
	RenderBackendChoice choice = RenderBackendChoice::Automatic;
};

const EnvironmentChoice& environmentChoice()
{
	static const EnvironmentChoice value = [] {
		EnvironmentChoice result;
		const QString text = qEnvironmentVariable("VIBESTUDIO_RENDER_BACKEND");
		result.set = !text.trimmed().isEmpty() && renderBackendChoiceFromId(text, &result.choice);
		return result;
	}();
	return value;
}

void registerPostRoutine(Manager& state)
{
	if (!state.postRoutine && QCoreApplication::instance()) {
		state.postRoutine = true;
		qAddPostRoutine(shutdownRenderBackends);
	}
}

} // namespace

quint64 newRenderCacheOwner()
{
	return manager().nextOwner.fetch_add(1);
}

void releaseRenderCacheOwner(quint64 owner)
{
	Manager& state = manager();
	std::array<std::shared_ptr<DeviceThread>, 2> devices;
	{
		std::lock_guard lock(state.mutex);
		devices = state.devices;
	}
	for (const auto& device : devices) {
		if (device) {
			device->releaseOwner(owner);
		}
	}
}

std::shared_ptr<RenderDevice> renderDevice(RenderBackend backend)
{
	Manager& state = manager();
	std::shared_ptr<DeviceThread> retired;
	std::shared_ptr<DeviceThread> device;
	{
		std::lock_guard lock(state.mutex);
		auto& current = state.devices[slot(backend)];
		if (current && current->failed()) {
			retired = std::move(current);
			++state.generation;
		}
		if (!current) {
			current = backend == RenderBackend::OpenGL ? render_detail::createOpenGLDevice(state.surface) : render_detail::createVulkanDevice();
			registerPostRoutine(state);
		}
		device = current;
	}
	if (retired) {
		retired->stop();
	}
	return device;
}

std::shared_ptr<RenderDevice> activeRenderDevice(RenderDeviceInfo* failure)
{
	const RenderBackendChoice choice = renderBackendChoice();
	QVector<RenderBackend> order;
	if (choice == RenderBackendChoice::OpenGL) {
		order = {RenderBackend::OpenGL};
	} else if (choice == RenderBackendChoice::Vulkan) {
		order = {RenderBackend::Vulkan};
	} else {
		order = automaticRenderBackendOrder();
	}
	QVector<RenderDeviceInfo> failures;
	for (RenderBackend backend : std::as_const(order)) {
		const std::shared_ptr<RenderDevice> device = renderDevice(backend);
		const RenderDeviceInfo info = device->info();
		if (info.available) {
			return device;
		}
		failures.append(info);
	}
	if (failure) {
		*failure = failures.isEmpty() ? RenderDeviceInfo {} : failures.first();
		if (failures.size() > 1) {
			QStringList reasons;
			for (const RenderDeviceInfo& info : std::as_const(failures)) {
				reasons << QCoreApplication::translate("VibeStudioRendering", "%1: %2", "renderer name, then why it is unavailable")
							   .arg(renderBackendDisplayName(info.backend), info.error);
			}
			failure->error = QCoreApplication::translate("VibeStudioRendering", "No 3D renderer is available. %1").arg(reasons.join(QLatin1Char(' ')));
		}
	}
	return nullptr;
}

void setRenderBackendChoice(RenderBackendChoice choice)
{
	Manager& state = manager();
	std::lock_guard lock(state.mutex);
	if (state.choice != choice) {
		state.choice = choice;
		++state.generation;
	}
}

RenderBackendChoice renderBackendChoice()
{
	Manager& state = manager();
	std::lock_guard lock(state.mutex);
	if (state.overridden) {
		return state.overrideChoice;
	}
	if (environmentChoice().set) {
		return environmentChoice().choice;
	}
	return state.choice;
}

bool renderBackendChoiceOverridden()
{
	return renderBackendChoiceSource() != QStringLiteral("preference");
}

QString renderBackendChoiceSource()
{
	Manager& state = manager();
	std::lock_guard lock(state.mutex);
	if (state.overridden) {
		return QStringLiteral("command-line");
	}
	return environmentChoice().set ? QStringLiteral("environment") : QStringLiteral("preference");
}

void setRenderBackendChoiceOverride(RenderBackendChoice choice)
{
	Manager& state = manager();
	std::lock_guard lock(state.mutex);
	if (!state.overridden || state.overrideChoice != choice) {
		state.overridden = true;
		state.overrideChoice = choice;
		++state.generation;
	}
}

quint64 renderBackendGeneration()
{
	return manager().generation.load();
}

void prepareRenderBackends(bool warmUp)
{
	auto* application = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
	if (!application || QThread::currentThread() != application->thread()) {
		return;
	}
	Manager& state = manager();
	std::shared_ptr<DeviceThread> retired;
	{
		std::lock_guard lock(state.mutex);
		registerPostRoutine(state);
		if (!state.surface) {
			auto* surface = new QOffscreenSurface;
			surface->setFormat(render_detail::openGLSurfaceFormat());
			surface->create();
			state.surface = surface;
			// An OpenGL device started before the surface existed reported
			// itself unavailable; let the next frame start it properly.
			auto& current = state.devices[slot(RenderBackend::OpenGL)];
			if (current) {
				retired = std::move(current);
				++state.generation;
			}
		}
	}
	if (retired) {
		retired->stop();
	}
	if (warmUp) {
		const RenderBackendChoice choice = renderBackendChoice();
		const RenderBackend first = choice == RenderBackendChoice::OpenGL ? RenderBackend::OpenGL
			: choice == RenderBackendChoice::Vulkan				  ? RenderBackend::Vulkan
																  : automaticRenderBackendOrder().first();
		std::shared_ptr<DeviceThread> device;
		{
			std::lock_guard lock(state.mutex);
			auto& current = state.devices[slot(first)];
			if (!current) {
				current = first == RenderBackend::OpenGL ? render_detail::createOpenGLDevice(state.surface) : render_detail::createVulkanDevice();
			}
			device = current;
		}
		device->startAsync();
	}
}

QVector<RenderDeviceInfo> probeRenderBackends()
{
	QVector<RenderDeviceInfo> result;
	for (RenderBackend backend : renderBackends()) {
		result.append(renderDevice(backend)->info());
	}
	return result;
}

RenderDeviceInfo renderBackendStatus(RenderBackend backend)
{
	Manager& state = manager();
	std::shared_ptr<DeviceThread> device;
	{
		std::lock_guard lock(state.mutex);
		device = state.devices[slot(backend)];
	}
	if (device) {
		return device->snapshot();
	}
	RenderDeviceInfo info;
	info.backend = backend;
	return info;
}

void resetRenderBackend(RenderBackend backend)
{
	Manager& state = manager();
	std::shared_ptr<DeviceThread> retired;
	{
		std::lock_guard lock(state.mutex);
		retired = std::move(state.devices[slot(backend)]);
		++state.generation;
	}
	if (retired) {
		retired->stop();
	}
}

void shutdownRenderBackends()
{
	Manager& state = manager();
	std::array<std::shared_ptr<DeviceThread>, 2> devices;
	QOffscreenSurface* surface = nullptr;
	{
		std::lock_guard lock(state.mutex);
		devices = std::move(state.devices);
		state.devices = {};
		surface = state.surface;
		state.surface = nullptr;
		++state.generation;
	}
	for (const auto& device : devices) {
		if (device) {
			device->stop();
		}
	}
	delete surface;
}

QString renderDeviceSummary(const RenderDeviceInfo& info)
{
	const QString name = renderBackendDisplayName(info.backend);
	if (!info.started) {
		return QCoreApplication::translate("VibeStudioRendering", "%1 (not started)").arg(name);
	}
	if (!info.available) {
		return QCoreApplication::translate("VibeStudioRendering", "%1 unavailable").arg(name);
	}
	QString summary = QCoreApplication::translate("VibeStudioRendering", "%1 %2 · %3", "renderer, its version, the graphics device")
						  .arg(name, info.apiVersion, info.deviceName);
	if (info.softwareImplementation) {
		summary = QCoreApplication::translate("VibeStudioRendering", "%1 (runs on the processor)", "a graphics driver that emulates a GPU in software").arg(summary);
	}
	return summary;
}

RenderSelfTestResult runRenderSelfTest(RenderDevice& device)
{
	RenderSelfTestResult result;
	const QString name = renderBackendDisplayName(device.backend());
	const RenderDeviceInfo info = device.info();
	if (!info.available) {
		result.error = info.error;
		result.errorDetail = info.errorDetail;
		return result;
	}
	// A texture with a different colour in every texel, so a flipped,
	// shifted or misread upload shows.
	const QSize size(8, 4);
	QImage texture(size, QImage::Format_ARGB32);
	for (int y = 0; y < size.height(); ++y) {
		for (int x = 0; x < size.width(); ++x) {
			texture.setPixel(x, y, qRgba(16 + x * 32, 32 + y * 64, 255 - x * 30, 255));
		}
	}
	const std::array<float, 4> clear {0.2f, 0.4f, 0.6f, 1.0f};
	const std::array<float, 4> multiply {0.5f, 0.25f, 0.75f, 1.0f};
	GpuFrame frame;
	frame.size = size;
	frame.targets = {{GpuFormat::Rgba8, {}}};
	GpuTextureData upload;
	upload.levels = {texture};
	frame.textures = {upload};
	GpuDraw draw;
	draw.program = int(GpuProgram::Composite);
	draw.count = 3;
	// Added over the clear colour; alpha keeps the target's.
	draw.state.blend = true;
	draw.state.sourceColor = GpuBlend::One;
	draw.state.destinationColor = GpuBlend::One;
	draw.state.sourceAlpha = GpuBlend::Zero;
	draw.state.destinationAlpha = GpuBlend::One;
	const float uniforms[8] = {multiply[0], multiply[1], multiply[2], multiply[3], 0.0f, 0.0f, 0.0f, 0.0f};
	draw.uniforms = QByteArray(reinterpret_cast<const char*>(uniforms), sizeof(uniforms));
	GpuSampler fetch;
	fetch.minFilter = GpuFilter::Nearest;
	fetch.magFilter = GpuFilter::Nearest;
	fetch.wrapU = GpuWrap::Clamp;
	fetch.wrapV = GpuWrap::Clamp;
	draw.textures[0] = GpuTextureRef::texture(0, fetch);
	GpuPass pass;
	pass.colors = {0, -1, -1, -1};
	pass.clear[0].color = clear;
	pass.draws = {draw};
	frame.passes = {pass};
	frame.readbacks = {0};
	const GpuFrameResult rendered = device.render(frame);
	result.milliseconds = rendered.milliseconds;
	if (!rendered.success) {
		result.error = rendered.error;
		result.errorDetail = rendered.errorDetail;
		return result;
	}
	const GpuReadback* readback = rendered.readback(0);
	const QImage image = readback ? readback->image(QImage::Format_ARGB32) : QImage();
	if (image.size() != size) {
		result.error = QCoreApplication::translate("VibeStudioRendering", "The %1 renderer did not return its test image.").arg(name);
		result.errorDetail = QStringLiteral("read back %1x%2, expected %3x%4").arg(image.width()).arg(image.height()).arg(size.width()).arg(size.height());
		return result;
	}
	// The clear colour as the target stores it, plus the scaled texel, then
	// stored again; either rounding may differ by one.
	const auto expected = [&](int stored, int texel, float scale) {
		return std::clamp(int(std::lround(stored + texel * scale)), 0, 255);
	};
	for (int y = 0; y < size.height(); ++y) {
		for (int x = 0; x < size.width(); ++x) {
			const QRgb source = texture.pixel(x, y);
			const QRgb want = qRgba(expected(int(std::lround(clear[0] * 255.0f)), qRed(source), multiply[0]),
				expected(int(std::lround(clear[1] * 255.0f)), qGreen(source), multiply[1]),
				expected(int(std::lround(clear[2] * 255.0f)), qBlue(source), multiply[2]), 255);
			const QRgb got = image.pixel(x, y);
			if (std::abs(qRed(got) - qRed(want)) > 1 || std::abs(qGreen(got) - qGreen(want)) > 1 || std::abs(qBlue(got) - qBlue(want)) > 1
				|| std::abs(qAlpha(got) - qAlpha(want)) > 1) {
				result.error = QCoreApplication::translate("VibeStudioRendering", "The %1 renderer drew its test image wrongly.").arg(name);
				result.errorDetail = QStringLiteral("pixel (%1, %2) is #%3, expected #%4")
										 .arg(x)
										 .arg(y)
										 .arg(got, 8, 16, QLatin1Char('0'))
										 .arg(want, 8, 16, QLatin1Char('0'));
				return result;
			}
		}
	}
	result.passed = true;
	return result;
}

} // namespace vibestudio
