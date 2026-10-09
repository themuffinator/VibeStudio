// The OpenGL backend: OpenGL 3.3 core profile, or OpenGL ES 3.0 where Qt
// runs on ES. Qt creates the context (QOpenGLContext against a
// QOffscreenSurface made on the GUI thread) and resolves the entry points;
// everything else is plain OpenGL on the device thread, rendering into
// framebuffer objects that are read back with glReadPixels.
//
// Canonical clip space (y down) maps NDC y = -1 to framebuffer row 0, which
// glReadPixels returns first, so read-back rows already start at the top and
// gl_FragCoord already counts from the top-left. Seen through OpenGL's
// y-up window convention the image is mirrored, which is why counter-
// clockwise-on-screen front faces are glFrontFace(GL_CW) here.

#include "core/render_device_p.h"
#include "core/render_shaders.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QHash>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QSurfaceFormat>

#include <algorithm>
#include <cstring>
#include <map>
#include <vector>

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_RGBA16
#define GL_RGBA16 0x805B
#endif
#ifndef GL_UNSIGNED_SHORT
#define GL_UNSIGNED_SHORT 0x1403
#endif

namespace vibestudio::render_detail {

QSurfaceFormat openGLSurfaceFormat()
{
	QSurfaceFormat format;
	if (QOpenGLContext::openGLModuleType() == QOpenGLContext::LibGLES) {
		format.setRenderableType(QSurfaceFormat::OpenGLES);
		format.setVersion(3, 0);
	} else {
		format.setRenderableType(QSurfaceFormat::OpenGL);
		format.setVersion(3, 3);
		format.setProfile(QSurfaceFormat::CoreProfile);
	}
	format.setDepthBufferSize(0);
	format.setStencilBufferSize(0);
	return format;
}

namespace {

constexpr qint64 kCacheBudgetBytes = qint64(512) * 1024 * 1024;
constexpr int kTargetPoolLimit = 32;
constexpr quint64 kTargetIdleFrames = 240;

bool softwareRenderer(const QString& renderer)
{
	const QString name = renderer.toLower();
	for (const char* marker : {"llvmpipe", "softpipe", "swrast", "swiftshader", "gdi generic", "basic render", "software renderer"}) {
		if (name.contains(QLatin1String(marker))) {
			return true;
		}
	}
	return false;
}

GLenum blendFactor(GpuBlend factor)
{
	switch (factor) {
	case GpuBlend::Zero:
		return GL_ZERO;
	case GpuBlend::One:
		return GL_ONE;
	case GpuBlend::SourceColor:
		return GL_SRC_COLOR;
	case GpuBlend::OneMinusSourceColor:
		return GL_ONE_MINUS_SRC_COLOR;
	case GpuBlend::DestinationColor:
		return GL_DST_COLOR;
	case GpuBlend::OneMinusDestinationColor:
		return GL_ONE_MINUS_DST_COLOR;
	case GpuBlend::SourceAlpha:
		return GL_SRC_ALPHA;
	case GpuBlend::OneMinusSourceAlpha:
		return GL_ONE_MINUS_SRC_ALPHA;
	case GpuBlend::DestinationAlpha:
		return GL_DST_ALPHA;
	case GpuBlend::OneMinusDestinationAlpha:
		return GL_ONE_MINUS_DST_ALPHA;
	case GpuBlend::SourceAlphaSaturate:
		return GL_SRC_ALPHA_SATURATE;
	}
	return GL_ONE;
}

GLenum compareFunction(GpuCompare compare)
{
	switch (compare) {
	case GpuCompare::Never:
		return GL_NEVER;
	case GpuCompare::Less:
		return GL_LESS;
	case GpuCompare::Equal:
		return GL_EQUAL;
	case GpuCompare::LessOrEqual:
		return GL_LEQUAL;
	case GpuCompare::Greater:
		return GL_GREATER;
	case GpuCompare::NotEqual:
		return GL_NOTEQUAL;
	case GpuCompare::GreaterOrEqual:
		return GL_GEQUAL;
	case GpuCompare::Always:
		return GL_ALWAYS;
	}
	return GL_LESS;
}

struct Program {
	GLuint id = 0;
	bool attempted = false;
	QString error;
};

struct Target {
	GLuint texture = 0;
	GpuFormat format = GpuFormat::Rgba8;
	QSize size;
	quint64 lastUsed = 0;
	bool inUse = false;
};

struct CachedTexture {
	GLuint texture = 0;
	qint64 bytes = 0;
	quint64 lastUsed = 0;
};

struct CachedBuffer {
	GLuint buffer = 0;
	qint64 bytes = 0;
	quint64 lastUsed = 0;
};

class OpenGLDevice final : public DeviceThread {
public:
	explicit OpenGLDevice(QOffscreenSurface* surface)
		: DeviceThread(RenderBackend::OpenGL)
		, m_surface(surface)
	{
	}
	~OpenGLDevice() override { stop(); }

protected:
	bool startDevice(RenderDeviceInfo* info) override;
	void stopDevice() override;
	GpuFrameResult renderFrame(const GpuFrame& frame, const std::atomic_bool* cancelled) override;
	void releaseOwnerOnDevice(quint64 owner) override;
	[[nodiscard]] bool formatSupportedOnDevice(GpuFormat format) const override;

private:
	bool makeCurrent();
	GpuFormat renderedFormat(GpuFormat format) const;
	bool ensureProgram(int index, QString* error);
	GLuint compileShader(GLenum stage, const char* body, QString* error);
	GLuint sampler(const GpuSampler& state, bool filterable);
	GLuint uploadTexture(const GpuTextureData& data, qint64* bytes);
	GLuint acquireTarget(GpuFormat format, QSize size);
	void releaseTargets();
	void evictCaches();
	bool readTarget(GLuint texture, GpuFormat format, QSize size, GpuReadback* output, QString* detail);

	QOffscreenSurface* m_surface = nullptr;
	std::unique_ptr<QOpenGLContext> m_context;
	QOpenGLExtraFunctions* m_gl = nullptr;
	bool m_es = false;
	bool m_rgba16 = false;
	GLint m_uniformAlignment = 256;
	GLuint m_vao = 0;
	GLuint m_framebuffer = 0;
	GLuint m_uniformBuffer = 0;
	GLuint m_white = 0;
	std::vector<Program> m_programs;
	QHash<quint32, GLuint> m_samplers;
	std::vector<Target> m_targets;
	std::map<std::pair<quint64, quint64>, CachedTexture> m_textures;
	std::map<std::pair<quint64, quint64>, CachedBuffer> m_buffers;
	quint64 m_frame = 0;
	int m_enabledAttributes = 0;
};

bool OpenGLDevice::makeCurrent()
{
	return m_context && m_context->makeCurrent(m_surface);
}

bool OpenGLDevice::startDevice(RenderDeviceInfo* info)
{
	if (!m_surface || !m_surface->isValid()) {
		info->error = QCoreApplication::translate("VibeStudioRendering", "OpenGL needs the studio's graphical session; it is not available to the command line or the offscreen platform.");
		info->errorDetail = QStringLiteral("no QOffscreenSurface (prepareRenderBackends() was not called on a QGuiApplication)");
		return false;
	}
	m_context = std::make_unique<QOpenGLContext>();
	m_context->setFormat(openGLSurfaceFormat());
	if (!m_context->create()) {
		const QString platform = QGuiApplication::platformName();
		info->error = platform == QLatin1String("offscreen") || platform == QLatin1String("minimal")
			? QCoreApplication::translate("VibeStudioRendering", "OpenGL is not available on Qt's %1 platform.").arg(platform)
			: QCoreApplication::translate("VibeStudioRendering", "The graphics driver could not create an OpenGL context.");
		info->errorDetail = QStringLiteral("QOpenGLContext::create() failed on the %1 platform").arg(platform);
		m_context.reset();
		return false;
	}
	if (!makeCurrent()) {
		info->error = QCoreApplication::translate("VibeStudioRendering", "The graphics driver created an OpenGL context but could not use it.");
		info->errorDetail = QStringLiteral("QOpenGLContext::makeCurrent() failed");
		m_context.reset();
		return false;
	}
	const QSurfaceFormat actual = m_context->format();
	m_es = m_context->isOpenGLES();
	m_gl = m_context->extraFunctions();
	const auto text = [this](GLenum name) {
		const auto* value = reinterpret_cast<const char*>(m_gl->glGetString(name));
		return value ? QString::fromUtf8(value) : QString();
	};
	info->deviceName = text(GL_RENDERER);
	info->vendor = text(GL_VENDOR);
	info->driverVersion = text(GL_VERSION);
	info->apiVersion = m_es ? QStringLiteral("%1.%2 ES").arg(actual.majorVersion()).arg(actual.minorVersion())
							: QStringLiteral("%1.%2 core").arg(actual.majorVersion()).arg(actual.minorVersion());
	info->deviceType = QStringLiteral("other");
	info->softwareImplementation = softwareRenderer(info->deviceName);
	const bool recent = m_es ? actual.majorVersion() >= 3 : (actual.majorVersion() > 3 || (actual.majorVersion() == 3 && actual.minorVersion() >= 3));
	if (!recent) {
		info->error = QCoreApplication::translate("VibeStudioRendering", "VibeStudio needs OpenGL 3.3 or OpenGL ES 3.0; this driver offers %1.").arg(info->apiVersion);
		info->errorDetail = info->driverVersion;
		m_context->doneCurrent();
		m_context.reset();
		return false;
	}
	if (m_es && !m_context->hasExtension("GL_EXT_color_buffer_float")) {
		info->error = QCoreApplication::translate("VibeStudioRendering", "This OpenGL ES driver cannot render the floating-point images VibeStudio's 3D views need.");
		info->errorDetail = QStringLiteral("GL_EXT_color_buffer_float is missing");
		m_context->doneCurrent();
		m_context.reset();
		return false;
	}
	m_rgba16 = !m_es || m_context->hasExtension("GL_EXT_texture_norm16");
	GLint maxTexture = 0;
	m_gl->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
	info->maxTextureSize = maxTexture;
	m_gl->glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &m_uniformAlignment);
	m_uniformAlignment = std::max<GLint>(m_uniformAlignment, 16);
	m_gl->glGenVertexArrays(1, &m_vao);
	m_gl->glGenFramebuffers(1, &m_framebuffer);
	m_gl->glGenBuffers(1, &m_uniformBuffer);
	m_gl->glGenTextures(1, &m_white);
	m_gl->glBindTexture(GL_TEXTURE_2D, m_white);
	const quint32 white = 0xffffffffu;
	m_gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
	m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	m_gl->glPixelStorei(GL_PACK_ALIGNMENT, 4);
	m_gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	m_programs.assign(size_t(gpuProgramCount()), Program {});
	if (m_gl->glGetError() != GL_NO_ERROR) {
		info->error = QCoreApplication::translate("VibeStudioRendering", "The OpenGL driver failed while VibeStudio set up its renderer.");
		info->errorDetail = QStringLiteral("glGetError after setup");
		stopDevice();
		return false;
	}
	return true;
}

void OpenGLDevice::stopDevice()
{
	if (!m_context) {
		return;
	}
	if (makeCurrent()) {
		for (Program& program : m_programs) {
			if (program.id) {
				m_gl->glDeleteProgram(program.id);
			}
		}
		for (GLuint id : std::as_const(m_samplers)) {
			m_gl->glDeleteSamplers(1, &id);
		}
		for (Target& target : m_targets) {
			m_gl->glDeleteTextures(1, &target.texture);
		}
		for (auto& [key, texture] : m_textures) {
			m_gl->glDeleteTextures(1, &texture.texture);
		}
		for (auto& [key, buffer] : m_buffers) {
			m_gl->glDeleteBuffers(1, &buffer.buffer);
		}
		m_gl->glDeleteTextures(1, &m_white);
		m_gl->glDeleteBuffers(1, &m_uniformBuffer);
		m_gl->glDeleteFramebuffers(1, &m_framebuffer);
		m_gl->glDeleteVertexArrays(1, &m_vao);
		m_context->doneCurrent();
	}
	m_programs.clear();
	m_samplers.clear();
	m_targets.clear();
	m_textures.clear();
	m_buffers.clear();
	m_context.reset();
	m_gl = nullptr;
}

bool OpenGLDevice::formatSupportedOnDevice(GpuFormat format) const
{
	return format != GpuFormat::Rgba16 || m_rgba16;
}

GpuFormat OpenGLDevice::renderedFormat(GpuFormat format) const
{
	return format == GpuFormat::Rgba16 && !m_rgba16 ? GpuFormat::Rgba8 : format;
}

GLuint OpenGLDevice::compileShader(GLenum stage, const char* body, QString* error)
{
	const QByteArray prefix = m_es
		? QByteArrayLiteral("#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n")
		: QByteArrayLiteral("#version 330 core\n");
	const QByteArray source = prefix + QByteArray(body);
	const char* text = source.constData();
	const GLuint shader = m_gl->glCreateShader(stage);
	m_gl->glShaderSource(shader, 1, &text, nullptr);
	m_gl->glCompileShader(shader);
	GLint status = GL_FALSE;
	m_gl->glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (status != GL_TRUE) {
		GLint length = 0;
		m_gl->glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
		QByteArray log(std::max(1, length), '\0');
		m_gl->glGetShaderInfoLog(shader, log.size(), nullptr, log.data());
		*error = QString::fromUtf8(log).trimmed();
		m_gl->glDeleteShader(shader);
		return 0;
	}
	return shader;
}

bool OpenGLDevice::ensureProgram(int index, QString* error)
{
	Program& program = m_programs[size_t(index)];
	if (program.attempted) {
		*error = program.error;
		return program.id != 0;
	}
	program.attempted = true;
	const GpuProgramInfo* info = gpuProgramInfo(index);
	QString detail;
	const GLuint vertex = compileShader(GL_VERTEX_SHADER, info->vertexGlsl, &detail);
	if (!vertex) {
		program.error = QStringLiteral("%1 vertex shader: %2").arg(QLatin1String(info->name), detail);
		*error = program.error;
		return false;
	}
	const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, info->fragmentGlsl, &detail);
	if (!fragment) {
		m_gl->glDeleteShader(vertex);
		program.error = QStringLiteral("%1 fragment shader: %2").arg(QLatin1String(info->name), detail);
		*error = program.error;
		return false;
	}
	const GLuint id = m_gl->glCreateProgram();
	m_gl->glAttachShader(id, vertex);
	m_gl->glAttachShader(id, fragment);
	m_gl->glLinkProgram(id);
	m_gl->glDeleteShader(vertex);
	m_gl->glDeleteShader(fragment);
	GLint status = GL_FALSE;
	m_gl->glGetProgramiv(id, GL_LINK_STATUS, &status);
	if (status != GL_TRUE) {
		GLint length = 0;
		m_gl->glGetProgramiv(id, GL_INFO_LOG_LENGTH, &length);
		QByteArray log(std::max(1, length), '\0');
		m_gl->glGetProgramInfoLog(id, log.size(), nullptr, log.data());
		m_gl->glDeleteProgram(id);
		program.error = QStringLiteral("%1 link: %2").arg(QLatin1String(info->name), QString::fromUtf8(log).trimmed());
		*error = program.error;
		return false;
	}
	const GLuint block = m_gl->glGetUniformBlockIndex(id, "Uniforms");
	if (block != GL_INVALID_INDEX) {
		m_gl->glUniformBlockBinding(id, block, 0);
	}
	m_gl->glUseProgram(id);
	for (int slot = 0; slot < info->samplerCount; ++slot) {
		const QByteArray name = "tex" + QByteArray::number(slot);
		const GLint location = m_gl->glGetUniformLocation(id, name.constData());
		if (location >= 0) {
			m_gl->glUniform1i(location, slot);
		}
	}
	program.id = id;
	return true;
}

GLuint OpenGLDevice::sampler(const GpuSampler& state, bool filterable)
{
	GpuSampler effective = state;
	if (!filterable) {
		effective.minFilter = GpuFilter::Nearest;
		effective.magFilter = GpuFilter::Nearest;
		effective.mipmap = GpuMipmap::None;
	}
	const quint32 key = quint32(effective.minFilter) | quint32(effective.magFilter) << 2 | quint32(effective.mipmap) << 4
		| quint32(effective.wrapU) << 6 | quint32(effective.wrapV) << 8;
	const auto found = m_samplers.constFind(key);
	if (found != m_samplers.cend()) {
		return found.value();
	}
	GLuint id = 0;
	m_gl->glGenSamplers(1, &id);
	GLint minFilter = GL_NEAREST;
	if (effective.minFilter == GpuFilter::Linear) {
		minFilter = effective.mipmap == GpuMipmap::None ? GL_LINEAR
			: effective.mipmap == GpuMipmap::Nearest ? GL_LINEAR_MIPMAP_NEAREST
													  : GL_LINEAR_MIPMAP_LINEAR;
	} else {
		minFilter = effective.mipmap == GpuMipmap::None ? GL_NEAREST
			: effective.mipmap == GpuMipmap::Nearest ? GL_NEAREST_MIPMAP_NEAREST
													  : GL_NEAREST_MIPMAP_LINEAR;
	}
	m_gl->glSamplerParameteri(id, GL_TEXTURE_MIN_FILTER, minFilter);
	m_gl->glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, effective.magFilter == GpuFilter::Linear ? GL_LINEAR : GL_NEAREST);
	m_gl->glSamplerParameteri(id, GL_TEXTURE_WRAP_S, effective.wrapU == GpuWrap::Repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
	m_gl->glSamplerParameteri(id, GL_TEXTURE_WRAP_T, effective.wrapV == GpuWrap::Repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
	m_samplers.insert(key, id);
	return id;
}

GLuint OpenGLDevice::uploadTexture(const GpuTextureData& data, qint64* bytes)
{
	GLuint id = 0;
	m_gl->glGenTextures(1, &id);
	m_gl->glBindTexture(GL_TEXTURE_2D, id);
	*bytes = 0;
	for (int level = 0; level < data.levels.size(); ++level) {
		const QImage image = textureLevelImage(data.levels.at(level));
		m_gl->glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, image.width(), image.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		for (int y = 0; y < image.height(); ++y) {
			m_gl->glTexSubImage2D(GL_TEXTURE_2D, level, 0, y, image.width(), 1, GL_RGBA, GL_UNSIGNED_BYTE, image.constScanLine(y));
		}
		*bytes += qint64(image.width()) * image.height() * 4;
	}
	m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, std::max(0, int(data.levels.size()) - 1));
	return id;
}

GLuint OpenGLDevice::acquireTarget(GpuFormat format, QSize size)
{
	const GpuFormat rendered = renderedFormat(format);
	for (Target& target : m_targets) {
		if (!target.inUse && target.format == rendered && target.size == size) {
			target.inUse = true;
			target.lastUsed = m_frame;
			return target.texture;
		}
	}
	Target target;
	target.format = rendered;
	target.size = size;
	target.inUse = true;
	target.lastUsed = m_frame;
	m_gl->glGenTextures(1, &target.texture);
	m_gl->glBindTexture(GL_TEXTURE_2D, target.texture);
	switch (rendered) {
	case GpuFormat::Rgba8:
		m_gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size.width(), size.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		break;
	case GpuFormat::Rgba16:
		m_gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16, size.width(), size.height(), 0, GL_RGBA, GL_UNSIGNED_SHORT, nullptr);
		break;
	case GpuFormat::R32Int:
		m_gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_R32I, size.width(), size.height(), 0, GL_RED_INTEGER, GL_INT, nullptr);
		break;
	case GpuFormat::R32Float:
		m_gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, size.width(), size.height(), 0, GL_RED, GL_FLOAT, nullptr);
		break;
	case GpuFormat::Depth32:
		m_gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, size.width(), size.height(), 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
		break;
	}
	m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	m_targets.push_back(target);
	return target.texture;
}

void OpenGLDevice::releaseTargets()
{
	for (Target& target : m_targets) {
		target.inUse = false;
	}
	// Drop targets left over from old view sizes.
	std::sort(m_targets.begin(), m_targets.end(), [](const Target& a, const Target& b) { return a.lastUsed > b.lastUsed; });
	while (!m_targets.empty() && (int(m_targets.size()) > kTargetPoolLimit || m_frame - m_targets.back().lastUsed > kTargetIdleFrames)) {
		m_gl->glDeleteTextures(1, &m_targets.back().texture);
		m_targets.pop_back();
	}
}

void OpenGLDevice::evictCaches()
{
	qint64 total = 0;
	for (const auto& [key, texture] : m_textures) {
		total += texture.bytes;
	}
	for (const auto& [key, buffer] : m_buffers) {
		total += buffer.bytes;
	}
	while (total > kCacheBudgetBytes) {
		// The least recently used upload, of either kind, not used this frame.
		auto oldestTexture = m_textures.end();
		auto oldestBuffer = m_buffers.end();
		for (auto it = m_textures.begin(); it != m_textures.end(); ++it) {
			if (it->second.lastUsed < m_frame && (oldestTexture == m_textures.end() || it->second.lastUsed < oldestTexture->second.lastUsed)) {
				oldestTexture = it;
			}
		}
		for (auto it = m_buffers.begin(); it != m_buffers.end(); ++it) {
			if (it->second.lastUsed < m_frame && (oldestBuffer == m_buffers.end() || it->second.lastUsed < oldestBuffer->second.lastUsed)) {
				oldestBuffer = it;
			}
		}
		if (oldestTexture == m_textures.end() && oldestBuffer == m_buffers.end()) {
			break;
		}
		if (oldestBuffer == m_buffers.end() || (oldestTexture != m_textures.end() && oldestTexture->second.lastUsed <= oldestBuffer->second.lastUsed)) {
			total -= oldestTexture->second.bytes;
			m_gl->glDeleteTextures(1, &oldestTexture->second.texture);
			m_textures.erase(oldestTexture);
		} else {
			total -= oldestBuffer->second.bytes;
			m_gl->glDeleteBuffers(1, &oldestBuffer->second.buffer);
			m_buffers.erase(oldestBuffer);
		}
	}
}

void OpenGLDevice::releaseOwnerOnDevice(quint64 owner)
{
	if (!makeCurrent()) {
		return;
	}
	for (auto it = m_textures.begin(); it != m_textures.end();) {
		if (it->first.first == owner) {
			m_gl->glDeleteTextures(1, &it->second.texture);
			it = m_textures.erase(it);
		} else {
			++it;
		}
	}
	for (auto it = m_buffers.begin(); it != m_buffers.end();) {
		if (it->first.first == owner) {
			m_gl->glDeleteBuffers(1, &it->second.buffer);
			it = m_buffers.erase(it);
		} else {
			++it;
		}
	}
}

bool OpenGLDevice::readTarget(GLuint texture, GpuFormat format, QSize size, GpuReadback* output, QString* detail)
{
	const int width = size.width();
	const int height = size.height();
	const qsizetype pixels = qsizetype(width) * height;
	m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
	for (int slot = 0; slot < kGpuMaxColorTargets; ++slot) {
		m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GLenum(GL_COLOR_ATTACHMENT0 + slot), GL_TEXTURE_2D, 0, 0);
	}
	m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
	if (format == GpuFormat::Depth32) {
		if (m_es) {
			*detail = QStringLiteral("OpenGL ES cannot read depth back");
			return false;
		}
		m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texture, 0);
		m_gl->glReadBuffer(GL_NONE);
	} else {
		m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
		m_gl->glReadBuffer(GL_COLOR_ATTACHMENT0);
	}
	if (m_gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		*detail = QStringLiteral("read-back framebuffer incomplete");
		return false;
	}
	output->format = format;
	output->size = size;
	switch (format) {
	case GpuFormat::Rgba8:
		output->bytes.resize(pixels * 4);
		if (!m_es) {
			m_gl->glReadPixels(0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, output->bytes.data());
		} else {
			m_gl->glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, output->bytes.data());
			auto* bytes = reinterpret_cast<uchar*>(output->bytes.data());
			for (qsizetype i = 0; i < pixels; ++i) {
				std::swap(bytes[i * 4], bytes[i * 4 + 2]);
			}
		}
		break;
	case GpuFormat::Rgba16:
		output->bytes.resize(pixels * 8);
		m_gl->glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_SHORT, output->bytes.data());
		break;
	case GpuFormat::R32Int:
	case GpuFormat::R32Float: {
		const bool integer = format == GpuFormat::R32Int;
		output->bytes.resize(pixels * 4);
		if (!m_es) {
			m_gl->glReadPixels(0, 0, width, height, integer ? GL_RED_INTEGER : GL_RED, integer ? GL_INT : GL_FLOAT, output->bytes.data());
		} else {
			// OpenGL ES guarantees only four-channel reads of these formats.
			QByteArray rgba(pixels * 16, '\0');
			m_gl->glReadPixels(0, 0, width, height, integer ? GL_RGBA_INTEGER : GL_RGBA, integer ? GL_INT : GL_FLOAT, rgba.data());
			for (qsizetype i = 0; i < pixels; ++i) {
				std::memcpy(output->bytes.data() + i * 4, rgba.constData() + i * 16, 4);
			}
		}
		break;
	}
	case GpuFormat::Depth32:
		output->bytes.resize(pixels * 4);
		m_gl->glReadPixels(0, 0, width, height, GL_DEPTH_COMPONENT, GL_FLOAT, output->bytes.data());
		break;
	}
	return true;
}

GpuFrameResult OpenGLDevice::renderFrame(const GpuFrame& frame, const std::atomic_bool* cancelled)
{
	GpuFrameResult result;
	const auto failWith = [&](const QString& message, const QString& detail) {
		result.success = false;
		result.error = message;
		result.errorDetail = detail;
		result.readbacks.clear();
		return result;
	};
	if (!makeCurrent() || !m_context->isValid()) {
		markLost(QStringLiteral("QOpenGLContext::makeCurrent() failed; the context is lost"));
		return failWith(QCoreApplication::translate("VibeStudioRendering", "The graphics device stopped responding."), QStringLiteral("OpenGL context lost"));
	}
	++m_frame;
	QOpenGLExtraFunctions& gl = *m_gl;
	gl.glGetError();

	// Client textures and buffers: cached by owner and key, or for this frame.
	std::vector<GLuint> textures(size_t(frame.textures.size()), 0);
	std::vector<GLuint> transientTextures;
	for (int index = 0; index < frame.textures.size(); ++index) {
		const GpuTextureData& data = frame.textures.at(index);
		if (data.cacheKey != 0) {
			auto found = m_textures.find({frame.owner, data.cacheKey});
			if (found != m_textures.end()) {
				found->second.lastUsed = m_frame;
				textures[size_t(index)] = found->second.texture;
				continue;
			}
		}
		qint64 bytes = 0;
		const GLuint id = uploadTexture(data, &bytes);
		textures[size_t(index)] = id;
		if (data.cacheKey != 0) {
			m_textures[{frame.owner, data.cacheKey}] = {id, bytes, m_frame};
		} else {
			transientTextures.push_back(id);
		}
	}
	std::vector<GLuint> buffers(size_t(frame.buffers.size()), 0);
	std::vector<GLuint> transientBuffers;
	for (int index = 0; index < frame.buffers.size(); ++index) {
		const GpuBufferData& data = frame.buffers.at(index);
		if (data.cacheKey != 0) {
			auto found = m_buffers.find({frame.owner, data.cacheKey});
			if (found != m_buffers.end() && found->second.bytes == data.bytes.size()) {
				found->second.lastUsed = m_frame;
				buffers[size_t(index)] = found->second.buffer;
				continue;
			}
			if (found != m_buffers.end()) {
				gl.glDeleteBuffers(1, &found->second.buffer);
				m_buffers.erase(found);
			}
		}
		GLuint id = 0;
		gl.glGenBuffers(1, &id);
		gl.glBindBuffer(GL_ARRAY_BUFFER, id);
		gl.glBufferData(GL_ARRAY_BUFFER, std::max<qsizetype>(data.bytes.size(), 4), data.bytes.isEmpty() ? nullptr : data.bytes.constData(), GL_STATIC_DRAW);
		buffers[size_t(index)] = id;
		if (data.cacheKey != 0) {
			m_buffers[{frame.owner, data.cacheKey}] = {id, data.bytes.size(), m_frame};
		} else {
			transientBuffers.push_back(id);
		}
	}
	const auto cleanUp = [&]() {
		for (GLuint id : transientTextures) {
			gl.glDeleteTextures(1, &id);
		}
		for (GLuint id : transientBuffers) {
			gl.glDeleteBuffers(1, &id);
		}
		releaseTargets();
		evictCaches();
	};

	std::vector<GLuint> targets(size_t(frame.targets.size()), 0);
	for (int index = 0; index < frame.targets.size(); ++index) {
		targets[size_t(index)] = acquireTarget(frame.targets.at(index).format, targetSize(frame, index));
	}

	// Every draw's uniforms in one buffer, bound by range.
	std::vector<std::vector<qint64>> uniformOffsets(size_t(frame.passes.size()));
	QByteArray uniforms;
	for (int passIndex = 0; passIndex < frame.passes.size(); ++passIndex) {
		const GpuPass& pass = frame.passes.at(passIndex);
		auto& offsets = uniformOffsets[size_t(passIndex)];
		offsets.assign(size_t(pass.draws.size()), -1);
		for (int drawIndex = 0; drawIndex < pass.draws.size(); ++drawIndex) {
			const GpuDraw& draw = pass.draws.at(drawIndex);
			const GpuProgramInfo* program = gpuProgramInfo(draw.program);
			if (program->uniformBytes <= 0) {
				continue;
			}
			const qint64 aligned = (uniforms.size() + m_uniformAlignment - 1) / m_uniformAlignment * m_uniformAlignment;
			uniforms.append(QByteArray(aligned - uniforms.size(), '\0'));
			offsets[size_t(drawIndex)] = uniforms.size();
			uniforms.append(paddedUniforms(draw, program->uniformBytes));
		}
	}
	gl.glBindBuffer(GL_UNIFORM_BUFFER, m_uniformBuffer);
	gl.glBufferData(GL_UNIFORM_BUFFER, std::max<qsizetype>(uniforms.size(), 16), uniforms.isEmpty() ? nullptr : uniforms.constData(), GL_STREAM_DRAW);

	gl.glBindVertexArray(m_vao);
	gl.glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
	gl.glDisable(GL_DITHER);
	for (int passIndex = 0; passIndex < frame.passes.size(); ++passIndex) {
		if (cancelled && cancelled->load()) {
			cleanUp();
			result.cancelled = true;
			return result;
		}
		const GpuPass& pass = frame.passes.at(passIndex);
		QSize passSize;
		GLenum drawBuffers[kGpuMaxColorTargets] = {GL_NONE, GL_NONE, GL_NONE, GL_NONE};
		int bufferCount = 0;
		for (int slot = 0; slot < kGpuMaxColorTargets; ++slot) {
			const int target = pass.colors[size_t(slot)];
			gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GLenum(GL_COLOR_ATTACHMENT0 + slot), GL_TEXTURE_2D, target >= 0 ? targets[size_t(target)] : 0, 0);
			if (target >= 0) {
				drawBuffers[slot] = GLenum(GL_COLOR_ATTACHMENT0 + slot);
				bufferCount = slot + 1;
				passSize = targetSize(frame, target);
			}
		}
		gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, pass.depth >= 0 ? targets[size_t(pass.depth)] : 0, 0);
		if (pass.depth >= 0) {
			passSize = targetSize(frame, pass.depth);
		}
		gl.glDrawBuffers(std::max(1, bufferCount), drawBuffers);
		const GLenum status = gl.glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE) {
			cleanUp();
			return failWith(QCoreApplication::translate("VibeStudioRendering", "The OpenGL driver cannot render this view's images."), QStringLiteral("pass %1: framebuffer status 0x%2").arg(passIndex).arg(status, 0, 16));
		}
		gl.glViewport(0, 0, passSize.width(), passSize.height());
		gl.glDisable(GL_SCISSOR_TEST);
		gl.glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		for (int slot = 0; slot < kGpuMaxColorTargets; ++slot) {
			const int target = pass.colors[size_t(slot)];
			if (target < 0 || pass.colorLoad[size_t(slot)] != GpuLoad::Clear) {
				continue;
			}
			const GpuClear& clear = pass.clear[size_t(slot)];
			if (frame.targets.at(target).format == GpuFormat::R32Int) {
				const GLint value[4] = {clear.integer, 0, 0, 0};
				gl.glClearBufferiv(GL_COLOR, slot, value);
			} else {
				gl.glClearBufferfv(GL_COLOR, slot, clear.color.data());
			}
		}
		if (pass.depth >= 0 && pass.depthLoad == GpuLoad::Clear) {
			gl.glDepthMask(GL_TRUE);
			const GLfloat depth = pass.clearDepth;
			gl.glClearBufferfv(GL_DEPTH, 0, &depth);
		}
		for (int drawIndex = 0; drawIndex < pass.draws.size(); ++drawIndex) {
			const GpuDraw& draw = pass.draws.at(drawIndex);
			if (draw.count <= 0) {
				continue;
			}
			QString error;
			if (!ensureProgram(draw.program, &error)) {
				cleanUp();
				return failWith(QCoreApplication::translate("VibeStudioRendering", "The OpenGL driver could not build one of VibeStudio's shaders."), error);
			}
			const GpuProgramInfo* program = gpuProgramInfo(draw.program);
			gl.glUseProgram(m_programs[size_t(draw.program)].id);
			const GpuState& state = draw.state;
			if (state.blend) {
				gl.glEnable(GL_BLEND);
				gl.glBlendFuncSeparate(blendFactor(state.sourceColor), blendFactor(state.destinationColor), blendFactor(state.sourceAlpha),
					blendFactor(state.destinationAlpha));
				gl.glBlendEquation(GL_FUNC_ADD);
			} else {
				gl.glDisable(GL_BLEND);
			}
			if (state.depthTest && pass.depth >= 0) {
				gl.glEnable(GL_DEPTH_TEST);
				gl.glDepthFunc(compareFunction(state.depthCompare));
			} else {
				gl.glDisable(GL_DEPTH_TEST);
			}
			gl.glDepthMask(state.depthWrite && pass.depth >= 0 ? GL_TRUE : GL_FALSE);
			if (state.cull == GpuCull::None) {
				gl.glDisable(GL_CULL_FACE);
			} else {
				gl.glEnable(GL_CULL_FACE);
				gl.glCullFace(state.cull == GpuCull::Back ? GL_BACK : GL_FRONT);
				gl.glFrontFace(GL_CW);
			}
			gl.glColorMask((state.colorMask & 1) != 0, (state.colorMask & 2) != 0, (state.colorMask & 4) != 0, (state.colorMask & 8) != 0);
			if (!draw.scissor.isEmpty()) {
				gl.glEnable(GL_SCISSOR_TEST);
				gl.glScissor(draw.scissor.x(), draw.scissor.y(), draw.scissor.width(), draw.scissor.height());
			} else {
				gl.glDisable(GL_SCISSOR_TEST);
			}
			const qint64 uniformOffset = uniformOffsets[size_t(passIndex)][size_t(drawIndex)];
			if (uniformOffset >= 0) {
				gl.glBindBufferRange(GL_UNIFORM_BUFFER, 0, m_uniformBuffer, GLintptr(uniformOffset), GLsizeiptr(program->uniformBytes));
			}
			for (int slot = 0; slot < program->samplerCount; ++slot) {
				const GpuTextureRef& ref = draw.textures[size_t(slot)];
				GLuint texture = m_white;
				bool filterable = true;
				if (ref.kind == GpuTextureRef::Kind::Texture) {
					texture = textures[size_t(ref.index)];
				} else if (ref.kind == GpuTextureRef::Kind::Target) {
					texture = targets[size_t(ref.index)];
					const GpuFormat format = frame.targets.at(ref.index).format;
					filterable = format == GpuFormat::Rgba8 || format == GpuFormat::Rgba16;
				}
				gl.glActiveTexture(GLenum(GL_TEXTURE0 + slot));
				gl.glBindTexture(GL_TEXTURE_2D, texture);
				gl.glBindSampler(GLuint(slot), sampler(ref.sampler, filterable));
			}
			// Attribute arrays for this program's bindings.
			int enabled = 0;
			for (int slot = 0; slot < 2; ++slot) {
				const GpuVertexBinding& binding = program->bindings[size_t(slot)];
				if (binding.stride <= 0) {
					continue;
				}
				const GpuBufferRef& ref = draw.vertexBuffers[size_t(slot)];
				gl.glBindBuffer(GL_ARRAY_BUFFER, buffers[size_t(ref.buffer)]);
				for (int a = 0; a < binding.attributeCount; ++a) {
					const GpuVertexAttribute& attribute = binding.attributes[size_t(a)];
					const auto* offset = reinterpret_cast<const void*>(quintptr(ref.offset + attribute.offset));
					const GLuint location = GLuint(attribute.location);
					gl.glEnableVertexAttribArray(location);
					enabled |= 1 << attribute.location;
					switch (attribute.format) {
					case GpuVertexFormat::Float1:
					case GpuVertexFormat::Float2:
					case GpuVertexFormat::Float3:
					case GpuVertexFormat::Float4:
						gl.glVertexAttribPointer(location, int(attribute.format) + 1, GL_FLOAT, GL_FALSE, binding.stride, offset);
						break;
					case GpuVertexFormat::UByte4Normalized:
						gl.glVertexAttribPointer(location, 4, GL_UNSIGNED_BYTE, GL_TRUE, binding.stride, offset);
						break;
					case GpuVertexFormat::UInt1:
						gl.glVertexAttribIPointer(location, 1, GL_UNSIGNED_INT, binding.stride, offset);
						break;
					case GpuVertexFormat::Int1:
						gl.glVertexAttribIPointer(location, 1, GL_INT, binding.stride, offset);
						break;
					}
					gl.glVertexAttribDivisor(location, binding.perInstance ? 1 : 0);
				}
			}
			for (int location = 0; location < 16; ++location) {
				if ((m_enabledAttributes & (1 << location)) != 0 && (enabled & (1 << location)) == 0) {
					gl.glDisableVertexAttribArray(GLuint(location));
				}
			}
			m_enabledAttributes = enabled;
			if (draw.indexBuffer.buffer >= 0) {
				gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers[size_t(draw.indexBuffer.buffer)]);
				gl.glDrawElementsInstanced(GL_TRIANGLES, draw.count, GL_UNSIGNED_INT, reinterpret_cast<const void*>(quintptr(draw.indexBuffer.offset)),
					draw.instances);
			} else {
				gl.glDrawArraysInstanced(GL_TRIANGLES, 0, draw.count, draw.instances);
			}
		}
	}
	gl.glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	gl.glDepthMask(GL_TRUE);
	gl.glDisable(GL_SCISSOR_TEST);
	for (int target : frame.readbacks) {
		GpuReadback readback;
		readback.target = target;
		QString detail;
		if (!readTarget(targets[size_t(target)], renderedFormat(frame.targets.at(target).format), targetSize(frame, target), &readback, &detail)) {
			cleanUp();
			return failWith(QCoreApplication::translate("VibeStudioRendering", "The OpenGL driver could not return this view's image."), detail);
		}
		result.readbacks.append(readback);
	}
	const GLenum error = gl.glGetError();
	cleanUp();
	if (error == GL_OUT_OF_MEMORY) {
		return failWith(QCoreApplication::translate("VibeStudioRendering", "The graphics device ran out of memory drawing this view."), QStringLiteral("GL_OUT_OF_MEMORY"));
	}
	if (error != GL_NO_ERROR) {
		return failWith(QCoreApplication::translate("VibeStudioRendering", "The OpenGL driver reported an error while drawing this view."), QStringLiteral("glGetError 0x%1").arg(error, 0, 16));
	}
	if (!m_context->isValid()) {
		markLost(QStringLiteral("the OpenGL context became invalid during a frame"));
		return failWith(QCoreApplication::translate("VibeStudioRendering", "The graphics device stopped responding."), QStringLiteral("OpenGL context lost"));
	}
	result.success = true;
	return result;
}

} // namespace

std::shared_ptr<DeviceThread> createOpenGLDevice(QOffscreenSurface* surface)
{
	return std::make_shared<OpenGLDevice>(surface);
}

} // namespace vibestudio::render_detail
