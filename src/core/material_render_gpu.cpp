// The GPU frame behind every material preview: the engines' framebuffer as a
// 16-bit colour target clamped after each blend, a depth buffer, textures
// uploaded from the decoded images, and the passes each engine path adds.
// finish() renders on the active backend (core/render_device.h) and reads
// back the display-scaled image.

#include "core/material_render_p.h"

#include <QCoreApplication>

#include <algorithm>
#include <cstring>
#include <limits>

namespace vibestudio::material_render {

namespace {

enum Target {
	ColorTarget,
	DepthTarget,
	FinalTarget,
};

void put(float* values, double x, double y, double z, double w)
{
	values[0] = static_cast<float>(x);
	values[1] = static_cast<float>(y);
	values[2] = static_cast<float>(z);
	values[3] = static_cast<float>(w);
}

QImage textureImage(int width, int height, const QVector<QRgb>& pixels)
{
	QImage image(width, height, QImage::Format_ARGB32);
	if (image.isNull() || pixels.size() < qsizetype(width) * height) {
		return {};
	}
	for (int y = 0; y < height; ++y) {
		std::memcpy(image.scanLine(y), pixels.constData() + qsizetype(y) * width, size_t(width) * sizeof(QRgb));
	}
	return image;
}

int wrapCode(Wrap wrap)
{
	switch (wrap) {
	case Wrap::Repeat:
		return 0;
	case Wrap::Clamp:
		return 1;
	case Wrap::ZeroClamp:
		return 2;
	case Wrap::AlphaZeroClamp:
		return 3;
	}
	return 0;
}

// Texels are fetched exactly in the shaders; the sampler only has to be
// valid for every level.
GpuSampler fetchSampler()
{
	GpuSampler sampler;
	sampler.minFilter = GpuFilter::Nearest;
	sampler.magFilter = GpuFilter::Nearest;
	sampler.mipmap = GpuMipmap::Nearest;
	sampler.wrapU = GpuWrap::Clamp;
	sampler.wrapV = GpuWrap::Clamp;
	return sampler;
}

} // namespace

void MaterialUniforms::setColor(const Color& value)
{
	put(color, value.r, value.g, value.b, value.a);
}

void MaterialUniforms::setParam(int index, double x, double y, double z, double w)
{
	if (index >= 0 && index < 8) {
		put(params[index], x, y, z, w);
	}
}

void MaterialUniforms::setMatrix(int slot, const double rows[2][3])
{
	if (slot < 0 || slot >= 8) {
		return;
	}
	put(matrices[slot * 2], rows[0][0], rows[0][1], rows[0][2], 0.0);
	put(matrices[slot * 2 + 1], rows[1][0], rows[1][1], rows[1][2], 0.0);
}

void MaterialUniforms::setIdentityMatrix(int slot)
{
	const double identity[2][3] = {{1, 0, 0}, {0, 1, 0}};
	setMatrix(slot, identity);
}

GpuCull gpuCull(CullMode cull)
{
	switch (cull) {
	case CullMode::Front:
		return GpuCull::Back;
	case CullMode::Back:
		return GpuCull::Front;
	case CullMode::None:
		return GpuCull::None;
	}
	return GpuCull::Back;
}

GpuBlend gpuBlendFactor(MaterialBlendFactor factor)
{
	switch (factor) {
	case MaterialBlendFactor::Zero:
		return GpuBlend::Zero;
	case MaterialBlendFactor::One:
		return GpuBlend::One;
	case MaterialBlendFactor::SourceColor:
		return GpuBlend::SourceColor;
	case MaterialBlendFactor::OneMinusSourceColor:
		return GpuBlend::OneMinusSourceColor;
	case MaterialBlendFactor::DestinationColor:
		return GpuBlend::DestinationColor;
	case MaterialBlendFactor::OneMinusDestinationColor:
		return GpuBlend::OneMinusDestinationColor;
	case MaterialBlendFactor::SourceAlpha:
		return GpuBlend::SourceAlpha;
	case MaterialBlendFactor::OneMinusSourceAlpha:
		return GpuBlend::OneMinusSourceAlpha;
	case MaterialBlendFactor::DestinationAlpha:
		return GpuBlend::DestinationAlpha;
	case MaterialBlendFactor::OneMinusDestinationAlpha:
		return GpuBlend::OneMinusDestinationAlpha;
	case MaterialBlendFactor::SourceAlphaSaturate:
		return GpuBlend::SourceAlphaSaturate;
	}
	return GpuBlend::One;
}

GpuState stageState(const MaterialBlend& blend, GpuCompare depth, bool depthWrite, CullMode cull)
{
	GpuState state;
	// The engines blend in the framebuffer's precision and clamp; a replace
	// is the same blend with a zero destination.
	state.blend = !blend.isOpaqueReplace();
	state.sourceColor = gpuBlendFactor(blend.source);
	state.destinationColor = gpuBlendFactor(blend.destination);
	state.sourceAlpha = state.sourceColor;
	state.destinationAlpha = state.destinationColor;
	if (state.sourceAlpha == GpuBlend::SourceAlphaSaturate) {
		// GL's saturate factor for alpha is one.
		state.sourceAlpha = GpuBlend::One;
	}
	state.depthTest = true;
	state.depthCompare = depth;
	state.depthWrite = depthWrite;
	state.cull = gpuCull(cull);
	return state;
}

MaterialGpuFrame::MaterialGpuFrame(const Camera& camera, const Mesh& mesh, const MaterialRenderOptions& options)
	: m_camera(camera)
	, m_mesh(&mesh)
	, m_options(&options)
{
	m_frame.size = QSize(std::max(1, camera.width), std::max(1, camera.height));
	m_frame.targets = {{GpuFormat::Rgba16, {}}, {GpuFormat::Depth32, {}}, {GpuFormat::Rgba8, {}}};
	QByteArray indices;
	indices.reserve(mesh.triangles.size() * 12);
	for (const Triangle& triangle : mesh.triangles) {
		const quint32 corners[3] = {quint32(triangle.a), quint32(triangle.b), quint32(triangle.c)};
		indices.append(reinterpret_cast<const char*>(corners), sizeof(corners));
	}
	m_meshIndices = static_cast<int>(m_frame.buffers.size());
	m_meshIndexCount = static_cast<int>(mesh.triangles.size()) * 3;
	m_frame.buffers.append({indices, 0});
	// Orthographic depth spans the camera's near plane to the farthest
	// vertex; perspective depth needs no far plane.
	double far = camera.nearPlane + 1.0;
	for (const Vertex& vertex : mesh.vertices) {
		far = std::max(far, camera.depthOf(vertex.position));
	}
	m_depthLow = camera.nearPlane;
	m_depthHigh = far * 1.01 + 1.0;
}

MaterialUniforms MaterialGpuFrame::uniforms() const
{
	MaterialUniforms u;
	const Camera& c = m_camera;
	const double width = std::max(1, c.width);
	const double height = std::max(1, c.height);
	double rows[4][4] {};
	const double rightRow[4] = {c.right.x, c.right.y, c.right.z, -c.right.dot(c.eye)};
	const double upRow[4] = {c.up.x, c.up.y, c.up.z, -c.up.dot(c.eye)};
	const double forwardRow[4] = {c.forward.x, c.forward.y, c.forward.z, -c.forward.dot(c.eye)};
	if (c.orthographic) {
		// x = centre + right * scale, y = centre - up * scale, in pixels;
		// depth nearness 1 at the near plane, 0 at the far end.
		const double span = std::max(1.0e-6, m_depthHigh - m_depthLow);
		for (int column = 0; column < 4; ++column) {
			rows[0][column] = 2.0 * c.orthoScale / width * rightRow[column];
			rows[1][column] = -2.0 * c.orthoScale / height * upRow[column];
			rows[2][column] = -forwardRow[column] / span;
			rows[3][column] = 0.0;
		}
		rows[0][3] += 2.0 * c.centerX / width - 1.0;
		rows[1][3] += 2.0 * c.centerY / height - 1.0;
		rows[2][3] += m_depthHigh / span;
		rows[3][3] = 1.0;
	} else {
		// Divide by distance; the near plane sits at clip w = nearPlane and
		// there is no far plane.
		const double sx = 2.0 * c.focal / width;
		const double cx = 2.0 * c.centerX / width - 1.0;
		const double sy = 2.0 * c.focal / height;
		const double cy = 2.0 * c.centerY / height - 1.0;
		for (int column = 0; column < 4; ++column) {
			rows[0][column] = sx * rightRow[column] + cx * forwardRow[column];
			rows[1][column] = -sy * upRow[column] + cy * forwardRow[column];
			rows[2][column] = 0.0;
			rows[3][column] = forwardRow[column];
		}
		rows[2][3] = c.nearPlane;
	}
	for (int column = 0; column < 4; ++column) {
		for (int row = 0; row < 4; ++row) {
			u.viewProjection[column * 4 + row] = static_cast<float>(rows[row][column]);
		}
	}
	put(u.eye, c.eye.x, c.eye.y, c.eye.z, c.orthographic ? 1.0 : 0.0);
	put(u.forward, c.forward.x, c.forward.y, c.forward.z, c.focal);
	put(u.right, c.right.x, c.right.y, c.right.z, c.orthoScale);
	put(u.up, c.up.x, c.up.y, c.up.z, 0.0);
	put(u.viewport, width, height, 1.0, 1.0);
	const MaterialPreviewLighting& lighting = m_options->lighting;
	const Vec3 lightPosition = previewLightPosition(*m_mesh, lighting, m_options->time);
	put(u.light, lightPosition.x, lightPosition.y, lightPosition.z, lighting.lightmap);
	put(u.mesh, m_mesh->center.x, m_mesh->center.y, m_mesh->center.z, m_mesh->radius);
	put(u.lighting, lighting.lightmapSpot ? 1.0 : 0.0, 0.0, m_options->time, 1.0);
	put(u.lightColor, lighting.lightColor.redF(), lighting.lightColor.greenF(), lighting.lightColor.blueF(), 1.0);
	for (int slot = 0; slot < 8; ++slot) {
		u.setIdentityMatrix(slot);
	}
	return u;
}

int MaterialGpuFrame::texture(const MaterialTexture* texture)
{
	if (!texture || !texture->usable()) {
		return -1;
	}
	const auto found = m_textures.constFind(texture);
	if (found != m_textures.cend()) {
		return found.value();
	}
	GpuTextureData data;
	const QImage base = textureImage(texture->width, texture->height, texture->pixels);
	if (base.isNull()) {
		return -1;
	}
	data.levels.append(base);
	QSize expected = base.size();
	for (int level = 0; level < texture->mipPixels.size() && level < texture->mipSizes.size(); ++level) {
		expected = QSize(std::max(1, expected.width() / 2), std::max(1, expected.height() / 2));
		const QSize size = texture->mipSizes.at(level);
		if (size != expected) {
			break;
		}
		const QImage image = textureImage(size.width(), size.height(), texture->mipPixels.at(level));
		if (image.isNull()) {
			break;
		}
		data.levels.append(image);
	}
	const int index = static_cast<int>(m_frame.textures.size());
	m_levels.insert(index, static_cast<int>(data.levels.size()));
	m_frame.textures.append(data);
	m_textures.insert(texture, index);
	return index;
}

int MaterialGpuFrame::indices(const MaterialTexture* texture)
{
	if (!texture || !texture->hasIndices()) {
		return -1;
	}
	const auto found = m_indexTextures.constFind(texture);
	if (found != m_indexTextures.cend()) {
		return found.value();
	}
	QImage image(texture->width, texture->height, QImage::Format_RGBA8888);
	for (int y = 0; y < texture->height; ++y) {
		uchar* row = image.scanLine(y);
		for (int x = 0; x < texture->width; ++x) {
			const qsizetype at = qsizetype(y) * texture->width + x;
			row[x * 4] = texture->indices.at(at);
			row[x * 4 + 1] = 0;
			row[x * 4 + 2] = 0;
			row[x * 4 + 3] = static_cast<uchar>(qAlpha(texture->pixels.at(at)));
		}
	}
	const int index = static_cast<int>(m_frame.textures.size());
	m_levels.insert(index, 1);
	m_frame.textures.append({{image}, 0});
	m_indexTextures.insert(texture, index);
	return index;
}

int MaterialGpuFrame::palette(const QVector<QRgb>& palette)
{
	QImage image(256, 1, QImage::Format_ARGB32);
	for (int index = 0; index < 256; ++index) {
		image.setPixel(index, 0, (index < palette.size() ? palette.at(index) : 0u) | 0xff000000u);
	}
	const int texture = static_cast<int>(m_frame.textures.size());
	m_levels.insert(texture, 1);
	m_frame.textures.append({{image}, 0});
	return texture;
}

int MaterialGpuFrame::rows(const QByteArray& table, int rowCount)
{
	rowCount = std::max(1, rowCount);
	QImage image(256, rowCount, QImage::Format_RGBA8888);
	for (int row = 0; row < rowCount; ++row) {
		uchar* line = image.scanLine(row);
		for (int index = 0; index < 256; ++index) {
			const qsizetype at = qsizetype(row) * 256 + index;
			line[index * 4] = at < table.size() ? static_cast<uchar>(table.at(at)) : 0;
			line[index * 4 + 1] = 0;
			line[index * 4 + 2] = 0;
			line[index * 4 + 3] = 255;
		}
	}
	const int texture = static_cast<int>(m_frame.textures.size());
	m_levels.insert(texture, 1);
	m_frame.textures.append({{image}, 0});
	return texture;
}

void MaterialGpuFrame::bind(GpuDraw* draw, MaterialUniforms* uniforms, int slot, int texture, Wrap wrap) const
{
	if (slot < 0 || slot >= kGpuMaxTextures) {
		return;
	}
	uniforms->samplerInfo[slot][0] = wrapCode(wrap);
	if (texture < 0) {
		uniforms->samplerInfo[slot][1] = 0;
		draw->textures[size_t(slot)] = {};
		return;
	}
	uniforms->samplerInfo[slot][1] = m_levels.value(texture, 1);
	draw->textures[size_t(slot)] = GpuTextureRef::texture(texture, fetchSampler());
}

void MaterialGpuFrame::bindSnapshot(GpuDraw* draw, MaterialUniforms* uniforms, int slot)
{
	closePass();
	if (m_snapshotTarget < 0) {
		m_snapshotTarget = static_cast<int>(m_frame.targets.size());
		m_frame.targets.append({GpuFormat::Rgba16, {}});
	}
	GpuDraw copy;
	copy.program = int(GpuProgram::Composite);
	copy.count = 3;
	QByteArray identity;
	const float values[8] = {1, 1, 1, 1, 0, 0, 0, 0};
	identity.append(reinterpret_cast<const char*>(values), sizeof(values));
	copy.uniforms = identity;
	copy.textures[0] = GpuTextureRef::target(ColorTarget, fetchSampler());
	GpuPass pass;
	pass.colors = {m_snapshotTarget, -1, -1, -1};
	pass.colorLoad[0] = GpuLoad::DontCare;
	pass.draws = {copy};
	if (!m_colorWritten) {
		// Nothing drawn yet: the snapshot is the cleared colour.
		mainPass();
		closePass();
	}
	m_frame.passes.append(pass);
	uniforms->samplerInfo[slot][0] = 1;
	uniforms->samplerInfo[slot][1] = 1;
	draw->textures[size_t(slot)] = GpuTextureRef::target(m_snapshotTarget, fetchSampler());
}

int MaterialGpuFrame::vertices(const QVector<StageVertex>* stage)
{
	return vertices(*m_mesh, stage);
}

int MaterialGpuFrame::vertices(const Mesh& mesh, const QVector<StageVertex>* stage)
{
	QByteArray bytes(mesh.vertices.size() * 80, Qt::Uninitialized);
	auto* out = reinterpret_cast<float*>(bytes.data());
	for (int index = 0; index < mesh.vertices.size(); ++index) {
		const Vertex& vertex = mesh.vertices.at(index);
		const bool own = !stage || index >= stage->size();
		const StageVertex values = own ? StageVertex {vertex.s, vertex.t, vertex.color} : stage->at(index);
		float* v = out + qsizetype(index) * 20;
		const double fields[20] = {vertex.position.x, vertex.position.y, vertex.position.z, vertex.normal.x, vertex.normal.y, vertex.normal.z,
			vertex.tangent.x, vertex.tangent.y, vertex.tangent.z, vertex.bitangent.x, vertex.bitangent.y, vertex.bitangent.z, values.s, values.t,
			vertex.ls, vertex.lt, values.color.r, values.color.g, values.color.b, values.color.a};
		for (int field = 0; field < 20; ++field) {
			v[field] = static_cast<float>(fields[field]);
		}
	}
	const int buffer = static_cast<int>(m_frame.buffers.size());
	m_frame.buffers.append({bytes, 0});
	return buffer;
}

GpuPass& MaterialGpuFrame::mainPass()
{
	if (!m_passOpen) {
		GpuPass pass;
		pass.colors = {ColorTarget, -1, -1, -1};
		pass.colorLoad[0] = m_colorWritten ? GpuLoad::Load : GpuLoad::Clear;
		pass.depth = DepthTarget;
		pass.depthLoad = m_depthWritten ? GpuLoad::Load : GpuLoad::Clear;
		pass.clearDepth = 0.0f;
		m_frame.passes.append(pass);
		m_passOpen = true;
		m_colorWritten = true;
		m_depthWritten = true;
	}
	return m_frame.passes.last();
}

void MaterialGpuFrame::closePass()
{
	m_passOpen = false;
}

void MaterialGpuFrame::draw(GpuProgram program, const MaterialUniforms& uniforms, const GpuState& state, int vertexBuffer, const GpuDraw& textures,
	int indexBuffer, int indexCount)
{
	GpuDraw draw;
	draw.program = int(program);
	draw.state = state;
	draw.vertexBuffers[0] = {vertexBuffer, 0};
	draw.indexBuffer = {indexBuffer < 0 ? m_meshIndices : indexBuffer, 0};
	draw.count = indexCount < 0 ? m_meshIndexCount : indexCount;
	draw.uniforms = QByteArray(reinterpret_cast<const char*>(&uniforms), sizeof(MaterialUniforms));
	draw.textures = textures.textures;
	mainPass().draws.append(draw);
}

void MaterialGpuFrame::background(const QColor& colour, bool checker, double scale)
{
	MaterialUniforms u = uniforms();
	u.mode[0] = 0;
	u.setParam(0, colour.redF() * scale, colour.greenF() * scale, colour.blueF() * scale, checker ? 1.0 : 0.0);
	GpuDraw draw;
	draw.program = int(GpuProgram::MaterialScreen);
	draw.count = 3;
	draw.uniforms = QByteArray(reinterpret_cast<const char*>(&u), sizeof(MaterialUniforms));
	mainPass().draws.append(draw);
}

void MaterialGpuFrame::fogScene(int vertexBuffer, const Color& fog, double distanceToOpaque, double identityLight)
{
	closePass();
	const int distances = static_cast<int>(m_frame.targets.size());
	m_frame.targets.append({GpuFormat::R32Float, {}});
	MaterialUniforms walls = uniforms();
	walls.mode[0] = 3;
	put(walls.viewport, m_frame.size.width(), m_frame.size.height(), 1.0, identityLight);
	GpuDraw wallDraw;
	wallDraw.program = int(GpuProgram::MaterialQuake3);
	wallDraw.state.depthTest = true;
	wallDraw.state.depthWrite = true;
	wallDraw.state.depthCompare = GpuCompare::Greater;
	wallDraw.state.cull = gpuCull(CullMode::Front);
	wallDraw.vertexBuffers[0] = {vertexBuffer, 0};
	wallDraw.indexBuffer = {m_meshIndices, 0};
	wallDraw.count = m_meshIndexCount;
	wallDraw.uniforms = QByteArray(reinterpret_cast<const char*>(&walls), sizeof(MaterialUniforms));
	GpuPass wallPass;
	wallPass.colors = {ColorTarget, distances, -1, -1};
	wallPass.colorLoad = {m_colorWritten ? GpuLoad::Load : GpuLoad::Clear, GpuLoad::Clear, GpuLoad::Clear, GpuLoad::Clear};
	wallPass.clear[1].color = {1.0e30f, 0.0f, 0.0f, 0.0f};
	wallPass.depth = DepthTarget;
	wallPass.depthLoad = m_depthWritten ? GpuLoad::Load : GpuLoad::Clear;
	wallPass.draws = {wallDraw};
	m_frame.passes.append(wallPass);
	m_colorWritten = true;
	m_depthWritten = true;

	MaterialUniforms curve = uniforms();
	curve.mode[0] = 1;
	curve.setParam(0, distanceToOpaque);
	curve.setColor(fog);
	GpuDraw curveDraw;
	curveDraw.program = int(GpuProgram::MaterialScreen);
	curveDraw.count = 3;
	curveDraw.state.blend = true;
	curveDraw.state.sourceColor = GpuBlend::SourceAlpha;
	curveDraw.state.destinationColor = GpuBlend::OneMinusSourceAlpha;
	curveDraw.state.sourceAlpha = GpuBlend::Zero;
	curveDraw.state.destinationAlpha = GpuBlend::One;
	curveDraw.uniforms = QByteArray(reinterpret_cast<const char*>(&curve), sizeof(MaterialUniforms));
	curveDraw.textures[0] = GpuTextureRef::target(distances, fetchSampler());
	GpuPass curvePass;
	curvePass.colors = {ColorTarget, -1, -1, -1};
	curvePass.colorLoad[0] = GpuLoad::Load;
	curvePass.draws = {curveDraw};
	m_frame.passes.append(curvePass);
}

void MaterialGpuFrame::finish(double displayScale, MaterialRenderResult* result, const std::function<bool()>& cancelled)
{
	if (cancelled && cancelled()) {
		result->cancelled = true;
		return;
	}
	closePass();
	if (!m_colorWritten) {
		mainPass();
		closePass();
	}
	// What the engines show: the framebuffer times their display scale,
	// clamped, opaque.
	GpuDraw present;
	present.program = int(GpuProgram::Composite);
	present.count = 3;
	const float scale = static_cast<float>(displayScale);
	const float values[8] = {scale, scale, scale, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
	present.uniforms = QByteArray(reinterpret_cast<const char*>(values), sizeof(values));
	present.textures[0] = GpuTextureRef::target(ColorTarget, fetchSampler());
	GpuPass presentPass;
	presentPass.colors = {FinalTarget, -1, -1, -1};
	presentPass.colorLoad[0] = GpuLoad::DontCare;
	presentPass.draws = {present};
	m_frame.passes.append(presentPass);
	m_frame.readbacks = {FinalTarget};

	RenderDeviceInfo failure;
	const std::shared_ptr<RenderDevice> device = activeRenderDevice(&failure);
	if (!device) {
		result->error = failure.error;
		result->errorDetail = failure.errorDetail;
		return;
	}
	result->renderer = renderDeviceSummary(device->info());
	const GpuFrameResult rendered = device->render(m_frame);
	if (rendered.cancelled || (cancelled && cancelled())) {
		result->cancelled = true;
		return;
	}
	if (!rendered.success) {
		result->error = rendered.error;
		result->errorDetail = rendered.errorDetail;
		return;
	}
	const GpuReadback* image = rendered.readback(FinalTarget);
	if (!image) {
		result->error = QCoreApplication::translate("VibeStudioRendering", "The graphics device did not return this view's image.");
		return;
	}
	result->image = image->image(QImage::Format_ARGB32);
}

} // namespace vibestudio::material_render
