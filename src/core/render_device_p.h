#pragma once

// Internal pieces shared by the render backends: the device thread that
// runs frames one at a time, frame validation, and the backend factories.

#include "core/render_device.h"

#include <QSurfaceFormat>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

class QOffscreenSurface;

namespace vibestudio::render_detail {

// Owns the backend's thread. Every graphics call happens on it; start(),
// render() and releaseOwner() hand work across and (except release) wait.
class DeviceThread : public RenderDevice {
public:
	explicit DeviceThread(RenderBackend backend);
	~DeviceThread() override;

	[[nodiscard]] RenderBackend backend() const final { return m_backend; }
	[[nodiscard]] RenderDeviceInfo info() final;
	GpuFrameResult render(const GpuFrame& frame, const std::atomic_bool* cancelled) final;
	void releaseOwner(quint64 owner) final;
	[[nodiscard]] bool supportsFormat(GpuFormat format) final;
	// Starts the thread and the device without waiting.
	void startAsync();
	// Finishes queued work and stops the thread; the device is destroyed on it.
	void stop();
	// A started device that has since failed (lost) reports unavailable.
	[[nodiscard]] bool failed();
	// The current info without starting or waiting; `started` is false until
	// the device has started.
	[[nodiscard]] RenderDeviceInfo snapshot();

protected:
	// Run on the device thread.
	virtual bool startDevice(RenderDeviceInfo* info) = 0;
	virtual void stopDevice() = 0;
	virtual GpuFrameResult renderFrame(const GpuFrame& frame, const std::atomic_bool* cancelled) = 0;
	virtual void releaseOwnerOnDevice(quint64 owner) = 0;
	[[nodiscard]] virtual bool formatSupportedOnDevice(GpuFormat format) const = 0;
	// Called on the device thread when a frame shows the device is gone.
	void markLost(const QString& detail);

private:
	void ensureThread();
	void waitUntilStarted();
	void run();

	RenderBackend m_backend;
	std::mutex m_mutex;
	std::condition_variable m_wake;
	std::condition_variable m_settled;
	std::deque<std::function<void()>> m_jobs;
	std::thread m_thread;
	bool m_threadStarted = false;
	bool m_started = false;
	bool m_stopping = false;
	bool m_lost = false;
	RenderDeviceInfo m_info;
};

// Checks indices, sizes and limits before a backend sees a frame. Returns a
// translated error, with the untranslated detail in `detail`.
QString validateFrame(const GpuFrame& frame, QString* detail);
// The size a target renders at.
QSize targetSize(const GpuFrame& frame, int target);
// RGBA8 rows for an uploaded texture level, in QImage RGBA8888 byte order,
// premultiplied or not as the source image was.
QImage textureLevelImage(const QImage& image);
// The uniform bytes for a draw, padded with zeros to the program's size.
QByteArray paddedUniforms(const GpuDraw& draw, int programBytes);

// The context format the OpenGL backend asks for: 3.3 core, or 3.0 ES where
// Qt uses OpenGL ES. The offscreen surface is created with it too.
QSurfaceFormat openGLSurfaceFormat();

// Factories; the OpenGL device renders against `surface`, created on the
// GUI thread, or reports itself unavailable when it is null.
std::shared_ptr<DeviceThread> createOpenGLDevice(QOffscreenSurface* surface);
std::shared_ptr<DeviceThread> createVulkanDevice();

} // namespace vibestudio::render_detail
