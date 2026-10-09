#pragma once

// GPU rendering for the studio's 3D views, with an OpenGL and a Vulkan
// backend behind one frame description.
//
// A client describes a frame as plain data on any thread: render targets,
// passes of draws, the textures and vertex data they read, and the targets
// to read back. The device executes the frame on its own thread, against
// its own context, and returns the read-back pixels. Clients never touch a
// graphics API, so the model and level viewports, material previews, the CLI
// and tests share one path on either backend.
//
// Frames render offscreen and are read back; the widgets present the result
// with QPainter beneath their own overlays and accessible readouts. There is
// no CPU rendering path: when neither backend works, 3D views say why and
// draw nothing.
//
// Conventions shared by both backends, so the same frame gives the same
// pixels on either:
// - Clip space is x right, y down (the image's top row is y = -1) and depth
//   0..1. Shaders write it that way; the OpenGL path converts depth itself.
// - Read-back rows start at the top of the image, and gl_FragCoord is
//   measured from the top-left corner in both APIs.
// - Texture coordinate (0, 0) is the first pixel of the image's first row.
// - Front faces wind counter-clockwise as seen in the final image.
//
// Vulkan is loaded at run time (vulkan-1, libvulkan.so.1, or the macOS
// loader or MoltenVK), so machines without it still run and report the
// backend unavailable. OpenGL needs QGuiApplication: call
// prepareRenderBackends() on the GUI thread once it exists. Under a
// QCoreApplication (the CLI) or a platform without OpenGL (the offscreen
// test platform) only Vulkan can render.

#include <QByteArray>
#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>

#include <array>
#include <atomic>
#include <functional>
#include <memory>

namespace vibestudio {

enum class RenderBackend {
	OpenGL,
	Vulkan,
};

// What the user asked for. Automatic takes the first backend that starts,
// in automaticRenderBackendOrder().
enum class RenderBackendChoice {
	Automatic,
	OpenGL,
	Vulkan,
};

QString renderBackendId(RenderBackend backend);
// Product names; never translated.
QString renderBackendDisplayName(RenderBackend backend);
bool renderBackendFromId(const QString& id, RenderBackend* backend);
QVector<RenderBackend> renderBackends();
QString renderBackendChoiceId(RenderBackendChoice choice);
QString renderBackendChoiceDisplayName(RenderBackendChoice choice);
bool renderBackendChoiceFromId(const QString& id, RenderBackendChoice* choice);
QVector<RenderBackendChoice> renderBackendChoices();
// Vulkan then OpenGL, except on macOS, where OpenGL is native and Vulkan
// needs MoltenVK.
QVector<RenderBackend> automaticRenderBackendOrder();

struct RenderDeviceInfo {
	RenderBackend backend = RenderBackend::Vulkan;
	// False until the device has started, and when it could not.
	bool started = false;
	bool available = false;
	// "4.6 core profile", "3.0 ES", "1.3.290".
	QString apiVersion;
	QString deviceName;
	QString vendor;
	QString driverVersion;
	// "discrete-gpu", "integrated-gpu", "virtual-gpu", "cpu" or "other".
	QString deviceType;
	// The API is implemented on the CPU (llvmpipe, lavapipe, SwiftShader):
	// still this backend, reported so slow frames have an explanation.
	bool softwareImplementation = false;
	int maxTextureSize = 0;
	// Translated reason the backend is unavailable, and the untranslated API
	// detail behind it, for logs and the CLI.
	QString error;
	QString errorDetail;
	qint64 startupMilliseconds = 0;
};

// ---------------------------------------------------------------------------
// Frame description
// ---------------------------------------------------------------------------

enum class GpuFormat : quint8 {
	// 8-bit colour. Reads back as QImage::Format_ARGB32 memory order (BGRA
	// bytes on little-endian machines); premultiplication is the client's.
	Rgba8,
	// 16-bit normalized colour for many blended passes; falls back to Rgba8
	// where the device cannot render or blend it (see GpuFrameResult).
	Rgba16,
	R32Int,
	R32Float,
	Depth32,
};

struct GpuTarget {
	GpuFormat format = GpuFormat::Rgba8;
	// Empty: the frame size.
	QSize size;
};

enum class GpuLoad : quint8 {
	Clear,
	Load,
	DontCare,
};

struct GpuClear {
	std::array<float, 4> color {0.0f, 0.0f, 0.0f, 0.0f};
	int integer = 0;
	float depth = 0.0f;
};

enum class GpuBlend : quint8 {
	Zero,
	One,
	SourceColor,
	OneMinusSourceColor,
	DestinationColor,
	OneMinusDestinationColor,
	SourceAlpha,
	OneMinusSourceAlpha,
	DestinationAlpha,
	OneMinusDestinationAlpha,
	SourceAlphaSaturate,
};

enum class GpuCompare : quint8 {
	Never,
	Less,
	Equal,
	LessOrEqual,
	Greater,
	NotEqual,
	GreaterOrEqual,
	Always,
};

// Which faces are dropped; front faces wind counter-clockwise on screen.
enum class GpuCull : quint8 {
	None,
	Back,
	Front,
};

enum class GpuFilter : quint8 {
	Nearest,
	Linear,
};

enum class GpuMipmap : quint8 {
	None,
	Nearest,
	Linear,
};

enum class GpuWrap : quint8 {
	Repeat,
	Clamp,
};

struct GpuSampler {
	GpuFilter minFilter = GpuFilter::Linear;
	GpuFilter magFilter = GpuFilter::Linear;
	GpuMipmap mipmap = GpuMipmap::None;
	GpuWrap wrapU = GpuWrap::Repeat;
	GpuWrap wrapV = GpuWrap::Repeat;

	friend bool operator==(const GpuSampler&, const GpuSampler&) = default;
};

// Fixed-function state for one draw. The vertex layout and bindings belong
// to the program (core/render_shaders.h).
struct GpuState {
	bool blend = false;
	GpuBlend sourceColor = GpuBlend::One;
	GpuBlend destinationColor = GpuBlend::Zero;
	GpuBlend sourceAlpha = GpuBlend::One;
	GpuBlend destinationAlpha = GpuBlend::Zero;
	bool depthTest = false;
	bool depthWrite = false;
	GpuCompare depthCompare = GpuCompare::Less;
	GpuCull cull = GpuCull::None;
	// Bit 0 red, 1 green, 2 blue, 3 alpha; applies to every colour output.
	quint8 colorMask = 0xF;

	friend bool operator==(const GpuState&, const GpuState&) = default;
};

// A sampled image: a client texture uploaded for the frame, or a target
// rendered by an earlier pass of the same frame.
struct GpuTextureRef {
	enum class Kind : quint8 { None, Texture, Target };
	Kind kind = Kind::None;
	int index = -1;
	GpuSampler sampler;

	static GpuTextureRef texture(int index, const GpuSampler& sampler = {})
	{
		return {Kind::Texture, index, sampler};
	}
	static GpuTextureRef target(int index, const GpuSampler& sampler = {})
	{
		return {Kind::Target, index, sampler};
	}
};

struct GpuBufferRef {
	int buffer = -1;
	qint64 offset = 0;
};

inline constexpr int kGpuMaxTextures = 8;
inline constexpr int kGpuMaxColorTargets = 4;
inline constexpr int kGpuMaxUniformBytes = 1024;

struct GpuDraw {
	// A GpuProgram value (core/render_shaders.h).
	int program = 0;
	GpuState state;
	// Per-vertex data, then per-vertex or per-instance data, as the program
	// declares. Programs without vertex input leave both unset.
	std::array<GpuBufferRef, 2> vertexBuffers;
	// 32-bit indices; unset draws vertices in order.
	GpuBufferRef indexBuffer;
	int count = 0;
	int instances = 1;
	// The std140 block the program declares, at most kGpuMaxUniformBytes.
	QByteArray uniforms;
	std::array<GpuTextureRef, kGpuMaxTextures> textures {};
	// Pixels from the top-left of the targets; empty means the whole frame.
	QRect scissor;
};

struct GpuPass {
	// Target indices by fragment output location; -1 leaves that output
	// unbound. Every bound target must have the same size.
	std::array<int, kGpuMaxColorTargets> colors {-1, -1, -1, -1};
	std::array<GpuLoad, kGpuMaxColorTargets> colorLoad {GpuLoad::Clear, GpuLoad::Clear, GpuLoad::Clear, GpuLoad::Clear};
	std::array<GpuClear, kGpuMaxColorTargets> clear {};
	int depth = -1;
	GpuLoad depthLoad = GpuLoad::Clear;
	float clearDepth = 0.0f;
	QVector<GpuDraw> draws;
};

struct GpuTextureData {
	// Mip levels, largest first; level n is half of level n - 1 rounded down,
	// never smaller than 1. Any QImage format; uploaded as RGBA8 with the
	// image's own premultiplication.
	QVector<QImage> levels;
	// Nonzero: the device keeps the upload for later frames of the same
	// owner with the same key (QImage::cacheKey() is a good one).
	quint64 cacheKey = 0;
};

struct GpuBufferData {
	QByteArray bytes;
	// Nonzero: kept for later frames of the same owner, as for textures.
	quint64 cacheKey = 0;
};

struct GpuFrame {
	QSize size;
	// Groups cached uploads; see newRenderCacheOwner().
	quint64 owner = 0;
	QVector<GpuTarget> targets;
	QVector<GpuTextureData> textures;
	QVector<GpuBufferData> buffers;
	QVector<GpuPass> passes;
	// Targets to read back after the last pass.
	QVector<int> readbacks;
};

struct GpuReadback {
	int target = -1;
	// What was actually rendered; Rgba16 may have become Rgba8.
	GpuFormat format = GpuFormat::Rgba8;
	QSize size;
	// Tightly packed rows, top row first: four bytes per pixel in QImage
	// ARGB32 order for Rgba8, four 16-bit channels in RGBA order for Rgba16,
	// one 32-bit value for R32Int, R32Float and Depth32.
	QByteArray bytes;

	// Rgba8 and Rgba16 as an 8-bit image, in `format` (ARGB32 or
	// ARGB32_Premultiplied, as the client rendered).
	[[nodiscard]] QImage image(QImage::Format format = QImage::Format_ARGB32_Premultiplied) const;
	[[nodiscard]] QVector<int> integers() const;
	[[nodiscard]] QVector<float> floats() const;
};

struct GpuFrameResult {
	bool success = false;
	bool cancelled = false;
	// Translated summary and untranslated detail when the frame failed.
	QString error;
	QString errorDetail;
	QVector<GpuReadback> readbacks;
	double milliseconds = 0.0;

	[[nodiscard]] const GpuReadback* readback(int target) const;
};

// ---------------------------------------------------------------------------
// Devices
// ---------------------------------------------------------------------------

class RenderDevice {
public:
	virtual ~RenderDevice();

	[[nodiscard]] virtual RenderBackend backend() const = 0;
	// Starts the device if it has not started. Blocks until it has.
	[[nodiscard]] virtual RenderDeviceInfo info() = 0;
	[[nodiscard]] bool isAvailable() { return info().available; }
	// Renders on the device's thread and blocks until it finishes. Safe from
	// any thread; frames run one at a time in submission order. Setting
	// `cancelled` before the frame starts skips it; once it starts it is
	// checked between passes.
	virtual GpuFrameResult render(const GpuFrame& frame, const std::atomic_bool* cancelled = nullptr) = 0;
	// Drops the uploads cached for an owner.
	virtual void releaseOwner(quint64 owner) = 0;
	// Whether a target format renders natively (Rgba16 may fall back).
	[[nodiscard]] virtual bool supportsFormat(GpuFormat format) = 0;
};

// A process-unique owner id for cached uploads. Release it on every device
// through releaseRenderCacheOwner() when its client goes away.
quint64 newRenderCacheOwner();
void releaseRenderCacheOwner(quint64 owner);

// The device for one backend, created on first use and kept for the
// process. Never null; check isAvailable().
std::shared_ptr<RenderDevice> renderDevice(RenderBackend backend);

// The device for the current choice: the chosen backend, or the first that
// starts for Automatic. Null when none is available, with `failure` saying
// why. Blocks while a device starts, so call it from worker threads or
// accept the wait.
std::shared_ptr<RenderDevice> activeRenderDevice(RenderDeviceInfo* failure = nullptr);

// The choice applies to frames started afterwards. The environment variable
// VIBESTUDIO_RENDER_BACKEND (automatic, opengl or vulkan) overrides it, for
// tests and diagnosis, and setRenderBackendChoiceOverride() overrides both.
void setRenderBackendChoice(RenderBackendChoice choice);
RenderBackendChoice renderBackendChoice();
// Whether the environment variable or a command-line override sets the
// choice rather than the preference (see renderBackendChoiceSource()).
bool renderBackendChoiceOverridden();
// Increases whenever the effective device may have changed, so views know to
// render again.
quint64 renderBackendGeneration();

// Call on the GUI thread once QGuiApplication exists, before any frame asks
// for OpenGL. Idempotent and cheap. With `warmUp` the chosen backend starts
// on its own thread straight away, so the first frame does not wait for it.
void prepareRenderBackends(bool warmUp = false);
// Starts each backend (blocking) and reports what it found.
QVector<RenderDeviceInfo> probeRenderBackends();
// The started backend's info, without starting anything: `started` is false
// until a frame or a probe has started it.
RenderDeviceInfo renderBackendStatus(RenderBackend backend);
// Forgets a failed or lost device so the next use starts it again.
void resetRenderBackend(RenderBackend backend);
// Stops the device threads. Runs automatically while the application object
// is destroyed; calling it earlier is allowed.
void shutdownRenderBackends();

// One readable line for status bars and logs, such as
// "Vulkan 1.3.290 · Intel(R) Iris(R) Xe Graphics".
QString renderDeviceSummary(const RenderDeviceInfo& info);

// Where the effective choice comes from: "preference" (the saved setting),
// "environment" (VIBESTUDIO_RENDER_BACKEND) or "command-line" (an override
// for this run, such as the CLI's --renderer).
QString renderBackendChoiceSource();
// Overrides the choice for this run ahead of the environment variable,
// without changing the saved preference.
void setRenderBackendChoiceOverride(RenderBackendChoice choice);

struct RenderSelfTestResult {
	bool passed = false;
	// Translated reason it failed, and the untranslated detail.
	QString error;
	QString errorDetail;
	double milliseconds = 0.0;
};

// Draws a small known frame and checks every pixel: a texture uploaded,
// fetched texel for texel, scaled and added over a cleared 8-bit target,
// then read back. A driver that starts but draws wrongly fails here rather
// than in a view.
RenderSelfTestResult runRenderSelfTest(RenderDevice& device);

} // namespace vibestudio
