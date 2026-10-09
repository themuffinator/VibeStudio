// GPU render backends: every available backend renders the same frames,
// the results are checked against known pixels, and OpenGL and Vulkan are
// compared with each other when both run. Covers clears, depth testing,
// triangle ids, culling and winding, flat shading, translucency peeling,
// wireframe coverage, textures with mip levels and cached uploads, and
// read-back of every target format.
//
// The offscreen test platform has no OpenGL, so there only Vulkan renders;
// a backend that cannot start is reported, not failed. With no backend at
// all the test exits 77 (skipped). VIBESTUDIO_RENDER_REQUIRE=opengl,vulkan
// turns a missing backend into a failure, for machines that must have it.

#include "core/render_device.h"
#include "core/render_shaders.h"

#include <QGuiApplication>
#include <QImage>
#include <QMatrix4x4>
#include <QVector3D>

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace vibestudio;

namespace {

int failures = 0;

bool expect(bool condition, const QString& message)
{
	if (!condition) {
		std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
		std::fflush(stderr);
		++failures;
	}
	return condition;
}

template <typename T>
void append(QByteArray* bytes, const T& value)
{
	bytes->append(reinterpret_cast<const char*>(&value), sizeof(T));
}

void appendMatrix(QByteArray* bytes, const QMatrix4x4& matrix)
{
	// std140 mat4: four column vectors.
	bytes->append(reinterpret_cast<const char*>(matrix.constData()), 16 * sizeof(float));
}

void appendVec4(QByteArray* bytes, float x, float y, float z, float w)
{
	const float values[4] = {x, y, z, w};
	bytes->append(reinterpret_cast<const char*>(values), sizeof(values));
}

void appendIvec4(QByteArray* bytes, int x, int y, int z, int w)
{
	const qint32 values[4] = {x, y, z, w};
	bytes->append(reinterpret_cast<const char*>(values), sizeof(values));
}

struct Corner {
	QVector3D position;
	QPointF uv;
};

// One model-surface vertex: position, uv, plane, colour, info.
void appendSurfaceVertex(QByteArray* bytes, const QVector3D& position, QPointF uv, const QVector3D& normal, float planeD, QRgb color, quint32 info)
{
	append(bytes, position.x());
	append(bytes, position.y());
	append(bytes, position.z());
	append(bytes, float(uv.x()));
	append(bytes, float(uv.y()));
	append(bytes, normal.x());
	append(bytes, normal.y());
	append(bytes, normal.z());
	append(bytes, planeD);
	const quint8 rgba[4] = {quint8(qRed(color)), quint8(qGreen(color)), quint8(qBlue(color)), 255};
	bytes->append(reinterpret_cast<const char*>(rgba), 4);
	append(bytes, info);
}

void appendTriangle(QByteArray* bytes, QByteArray* flags, const std::array<QVector3D, 3>& corners, QRgb color, quint32 flag = 0,
	const std::array<QPointF, 3>& uvs = {QPointF(0, 0), QPointF(1, 0), QPointF(0, 1)}, quint32 info = 1)
{
	QVector3D normal = QVector3D::crossProduct(corners[1] - corners[0], corners[2] - corners[0]).normalized();
	const float d = QVector3D::dotProduct(normal, corners[0]);
	for (int i = 0; i < 3; ++i) {
		appendSurfaceVertex(bytes, corners[size_t(i)], uvs[size_t(i)], normal, d, color, info);
		append(flags, flag);
	}
}

// Orthographic: world x and y map straight to the target's pixels (y down)
// and z runs away from the viewer, so the frame is right-handed like a real
// camera: a triangle wound counter-clockwise on screen faces the viewer.
// Depth nearness is 1 - z / 100: smaller z is nearer.
QMatrix4x4 pixelProjection(QSize size)
{
	QMatrix4x4 matrix;
	matrix.setToIdentity();
	matrix(0, 0) = 2.0f / size.width();
	matrix(0, 3) = -1.0f;
	matrix(1, 1) = 2.0f / size.height();
	matrix(1, 3) = -1.0f;
	matrix(2, 2) = -1.0f / 100.0f;
	matrix(2, 3) = 1.0f;
	return matrix;
}

QByteArray surfaceUniforms(const QMatrix4x4& projection, bool textured, int pass = 0, int hovered = -1, bool showEdges = false, bool cull = false,
	int firstTriangle = 0)
{
	QByteArray bytes;
	appendMatrix(&bytes, projection);
	appendVec4(&bytes, 0.0f, 0.0f, -1.0f, 0.0f); // orthographic: toward the viewer is -z
	appendVec4(&bytes, 0.0f, 0.0f, -1.0f, 0.0f); // light along the eye: full shade
	appendVec4(&bytes, 0.0f, 0.0f, 0.0f, 110.0f / 255.0f);
	appendVec4(&bytes, 24.0f / 255.0f, 26.0f / 255.0f, 32.0f / 255.0f, 150.0f / 255.0f);
	appendVec4(&bytes, 1.0f, 210.0f / 255.0f, 90.0f / 255.0f, 1.0f);
	appendVec4(&bytes, 1.0f, 170.0f / 255.0f, 60.0f / 255.0f, 1.0f);
	appendVec4(&bytes, 1.0f, 170.0f / 255.0f, 60.0f / 255.0f, 1.0f);
	appendVec4(&bytes, 1.0f, showEdges ? 1.0f : 0.0f, textured ? 1.0f : 0.0f, 0.0f);
	appendIvec4(&bytes, hovered, firstTriangle, cull ? 1 : 0, pass);
	return bytes;
}

struct FrameOutput {
	bool ok = false;
	QImage color;
	QVector<int> ids;
	QString error;
};

FrameOutput run(RenderDevice& device, const GpuFrame& frame, int colorTarget, int idTarget = -1)
{
	FrameOutput output;
	const GpuFrameResult result = device.render(frame);
	if (!result.success) {
		output.error = result.error + QStringLiteral(" / ") + result.errorDetail;
		return output;
	}
	if (const GpuReadback* color = result.readback(colorTarget)) {
		output.color = color->image();
	}
	if (idTarget >= 0) {
		if (const GpuReadback* ids = result.readback(idTarget)) {
			output.ids = ids->integers();
		}
	}
	output.ok = !output.color.isNull();
	return output;
}

bool near(QRgb a, QRgb b, int tolerance = 1)
{
	return std::abs(qRed(a) - qRed(b)) <= tolerance && std::abs(qGreen(a) - qGreen(b)) <= tolerance && std::abs(qBlue(a) - qBlue(b)) <= tolerance
		&& std::abs(qAlpha(a) - qAlpha(b)) <= tolerance;
}

QString hex(QRgb value)
{
	return QStringLiteral("#%1").arg(value, 8, 16, QLatin1Char('0'));
}

// Clears and the opaque surface pass: colours, depth order, ids, culling.
FrameOutput surfaceScene(RenderDevice& device, bool gpuCullBack)
{
	const QSize size(64, 48);
	GpuFrame frame;
	frame.size = size;
	frame.owner = 1;
	frame.targets = {{GpuFormat::Rgba8, {}}, {GpuFormat::R32Int, {}}, {GpuFormat::R32Float, {}}, {GpuFormat::Depth32, {}}};
	QByteArray vertices;
	QByteArray flags;
	// Triangle 0: far, red, covering the left half.
	appendTriangle(&vertices, &flags, {QVector3D(2, 2, 90), QVector3D(2, 46, 90), QVector3D(40, 2, 90)}, qRgb(255, 0, 0));
	// Triangle 1: nearer, highlighted, overlapping triangle 0 on the right.
	appendTriangle(&vertices, &flags, {QVector3D(20, 2, 50), QVector3D(20, 46, 50), QVector3D(62, 24, 50)}, qRgb(0, 255, 0), 1);
	// Triangle 2: nearest, but wound clockwise on screen (seen from behind):
	// culled by back-face culling in either the shader or the fixed function.
	appendTriangle(&vertices, &flags, {QVector3D(44, 30, 40), QVector3D(60, 30, 40), QVector3D(44, 46, 40)}, qRgb(0, 0, 255));
	frame.buffers = {{vertices, 0}, {flags, 0}};
	GpuPass pass;
	pass.colors = {0, 1, 2, -1};
	pass.clear[0].color = {0.0f, 0.0f, 0.0f, 0.0f};
	pass.clear[1].integer = -1;
	pass.depth = 3;
	pass.clearDepth = 0.0f;
	GpuDraw draw;
	draw.program = int(GpuProgram::ModelSurface);
	draw.state.depthTest = true;
	draw.state.depthWrite = true;
	draw.state.depthCompare = GpuCompare::Greater;
	draw.state.cull = gpuCullBack ? GpuCull::Back : GpuCull::None;
	draw.vertexBuffers[0] = {0, 0};
	draw.vertexBuffers[1] = {1, 0};
	draw.count = 9;
	draw.uniforms = surfaceUniforms(pixelProjection(size), false, 0, -1, false, !gpuCullBack);
	pass.draws = {draw};
	frame.passes = {pass};
	frame.readbacks = {0, 1};
	return run(device, frame, 0, 1);
}

void checkSurfaceScene(const QString& name, const FrameOutput& output)
{
	if (!expect(output.ok, name + QStringLiteral(": surface frame failed: ") + output.error)) {
		return;
	}
	const QImage& image = output.color;
	expect(image.size() == QSize(64, 48), name + QStringLiteral(": surface image size"));
	// Flat shading at full light: 0.22 + 0.78 = 1, so the surface colour.
	expect(near(image.pixel(8, 24), qRgb(255, 0, 0)), name + QStringLiteral(": far red triangle at (8,24) is ") + hex(image.pixel(8, 24)));
	// Highlighted green triangle: the highlight colour at full light, with
	// hatching where (x + y) mod 8 < 1.2 (pixel centres).
	const QRgb highlight = qRgb(255, 170, 60);
	expect(near(image.pixel(30, 24), highlight), name + QStringLiteral(": near highlighted triangle at (30,24) is ") + hex(image.pixel(30, 24)));
	const QRgb hatched = image.pixel(30, 25); // 30.5 + 25.5 = 56: 56 mod 8 = 0 < 1.2
	expect(qRed(hatched) < 255 && qAlpha(hatched) == 255, name + QStringLiteral(": hatch at (30,25) is ") + hex(hatched));
	expect(qAlpha(image.pixel(52, 40)) == 0, name + QStringLiteral(": culled back face drew ") + hex(image.pixel(52, 40)));
	expect(qAlpha(image.pixel(63, 0)) == 0, name + QStringLiteral(": background is transparent"));
	if (expect(output.ids.size() == 64 * 48, name + QStringLiteral(": id buffer size"))) {
		expect(output.ids.at(24 * 64 + 8) == 0, name + QStringLiteral(": id under the red triangle is %1").arg(output.ids.at(24 * 64 + 8)));
		expect(output.ids.at(24 * 64 + 30) == 1, name + QStringLiteral(": id under the green triangle is %1").arg(output.ids.at(24 * 64 + 30)));
		expect(output.ids.at(40 * 64 + 52) == -1, name + QStringLiteral(": id under the culled triangle is %1").arg(output.ids.at(40 * 64 + 52)));
		expect(output.ids.at(0 * 64 + 63) == -1, name + QStringLiteral(": background id is %1").arg(output.ids.at(63)));
	}
}

// A skin with alpha: opaque, translucent and transparent texels, sampled
// with nearest filtering, peeled into layers and composited.
FrameOutput translucentScene(RenderDevice& device)
{
	const QSize size(32, 32);
	GpuFrame frame;
	frame.size = size;
	frame.owner = 2;
	// 0 colour, 1 ids, 2 opaque depth, 3 depth, 4 layer colour, 5/6 layer
	// depth, 7 accumulation.
	frame.targets = {{GpuFormat::Rgba8, {}}, {GpuFormat::R32Int, {}}, {GpuFormat::R32Float, {}}, {GpuFormat::Depth32, {}}, {GpuFormat::Rgba8, {}},
		{GpuFormat::R32Float, {}}, {GpuFormat::R32Float, {}}, {GpuFormat::Rgba8, {}}};
	QImage skin(2, 1, QImage::Format_ARGB32_Premultiplied);
	skin.setPixel(0, 0, qPremultiply(qRgba(0, 0, 255, 128)));
	skin.setPixel(1, 0, qPremultiply(qRgba(255, 255, 0, 0)));
	QImage opaqueSkin(1, 1, QImage::Format_ARGB32_Premultiplied);
	opaqueSkin.fill(qRgb(200, 200, 200));
	frame.textures = {{{skin}, 0}, {{opaqueSkin}, 0}};
	QByteArray vertices;
	QByteArray flags;
	// Triangles 0, 1: an opaque grey quad, far.
	appendTriangle(&vertices, &flags, {QVector3D(0, 0, 90), QVector3D(0, 32, 90), QVector3D(32, 0, 90)}, qRgb(0, 0, 0), 0,
		{QPointF(0.5, 0.5), QPointF(0.5, 0.5), QPointF(0.5, 0.5)});
	appendTriangle(&vertices, &flags, {QVector3D(32, 0, 90), QVector3D(0, 32, 90), QVector3D(32, 32, 90)}, qRgb(0, 0, 0), 0,
		{QPointF(0.5, 0.5), QPointF(0.5, 0.5), QPointF(0.5, 0.5)});
	// Triangles 2, 3: nearer, the half-transparent blue texel on the left
	// and the fully transparent texel on the right.
	appendTriangle(&vertices, &flags, {QVector3D(0, 0, 50), QVector3D(0, 32, 50), QVector3D(16, 0, 50)}, qRgb(0, 0, 0), 0,
		{QPointF(0.25, 0.5), QPointF(0.25, 0.5), QPointF(0.25, 0.5)});
	appendTriangle(&vertices, &flags, {QVector3D(16, 0, 50), QVector3D(16, 32, 50), QVector3D(32, 0, 50)}, qRgb(0, 0, 0), 0,
		{QPointF(0.75, 0.5), QPointF(0.75, 0.5), QPointF(0.75, 0.5)});
	frame.buffers = {{vertices, 0}, {flags, 0}};
	const QMatrix4x4 projection = pixelProjection(size);
	GpuSampler nearest;
	nearest.minFilter = GpuFilter::Nearest;
	nearest.magFilter = GpuFilter::Nearest;
	const auto surfaceDraw = [&](int first, int count, int texture, int passKind) {
		GpuDraw draw;
		draw.program = int(GpuProgram::ModelSurface);
		draw.state.depthTest = true;
		draw.state.depthWrite = true;
		draw.state.depthCompare = GpuCompare::Greater;
		draw.vertexBuffers[0] = {0, qint64(first) * 3 * 44};
		draw.vertexBuffers[1] = {1, qint64(first) * 3 * 4};
		draw.count = count * 3;
		draw.uniforms = surfaceUniforms(projection, true, passKind, -1, false, false, first);
		draw.textures[0] = GpuTextureRef::texture(texture, nearest);
		if (passKind > 0) {
			draw.textures[1] = GpuTextureRef::target(2);
		}
		if (passKind == 2) {
			draw.textures[2] = GpuTextureRef::target(5);
		}
		return draw;
	};
	GpuPass opaque;
	opaque.colors = {0, 1, 2, -1};
	opaque.clear[1].integer = -1;
	opaque.depth = 3;
	opaque.draws = {surfaceDraw(0, 2, 1, 0), surfaceDraw(2, 2, 0, 0)};
	GpuPass layer;
	layer.colors = {4, 1, 5, -1};
	layer.colorLoad = {GpuLoad::Clear, GpuLoad::Load, GpuLoad::Clear, GpuLoad::Clear};
	layer.depth = 3;
	layer.depthLoad = GpuLoad::Clear;
	layer.draws = {surfaceDraw(2, 2, 0, 1)};
	GpuDraw under;
	under.program = int(GpuProgram::Composite);
	under.count = 3;
	under.state.blend = true;
	under.state.sourceColor = GpuBlend::OneMinusDestinationAlpha;
	under.state.destinationColor = GpuBlend::One;
	under.state.sourceAlpha = GpuBlend::OneMinusDestinationAlpha;
	under.state.destinationAlpha = GpuBlend::One;
	QByteArray identity;
	appendVec4(&identity, 1, 1, 1, 1);
	appendVec4(&identity, 0, 0, 0, 0);
	under.uniforms = identity;
	under.textures[0] = GpuTextureRef::target(4);
	GpuPass accumulateLayer;
	accumulateLayer.colors = {7, -1, -1, -1};
	accumulateLayer.draws = {under};
	GpuDraw opaqueUnder = under;
	opaqueUnder.textures[0] = GpuTextureRef::target(0);
	GpuPass accumulateOpaque;
	accumulateOpaque.colors = {7, -1, -1, -1};
	accumulateOpaque.colorLoad[0] = GpuLoad::Load;
	accumulateOpaque.draws = {opaqueUnder};
	frame.passes = {opaque, layer, accumulateLayer, accumulateOpaque};
	frame.readbacks = {7, 1};
	return run(device, frame, 7, 1);
}

void checkTranslucentScene(const QString& name, const FrameOutput& output)
{
	if (!expect(output.ok, name + QStringLiteral(": translucent frame failed: ") + output.error)) {
		return;
	}
	const QRgb left = output.color.pixel(3, 16);
	const QRgb right = output.color.pixel(28, 16);
	// Light factor at full shade is 1: half blue over opaque grey.
	const QRgb expected = qRgba(100, 100, 228, 255);
	expect(near(left, expected, 2), name + QStringLiteral(": translucent blue over grey is ") + hex(left) + QStringLiteral(", expected ") + hex(expected));
	expect(near(right, qRgb(200, 200, 200), 1), name + QStringLiteral(": transparent texel left grey ") + hex(right));
	if (expect(output.ids.size() == 32 * 32, name + QStringLiteral(": translucent id buffer size"))) {
		const int leftId = output.ids.at(16 * 32 + 3);
		const int rightId = output.ids.at(16 * 32 + 28);
		expect(leftId == 2, name + QStringLiteral(": nearest translucent triangle picks first (%1)").arg(leftId));
		expect(rightId == 1, name + QStringLiteral(": a transparent texel never picks (%1)").arg(rightId));
	}
}

// One horizontal and one selected diagonal segment, in pixels.
FrameOutput wireScene(RenderDevice& device)
{
	const QSize size(48, 32);
	GpuFrame frame;
	frame.size = size;
	frame.owner = 3;
	frame.targets = {{GpuFormat::Rgba8, {}}};
	QByteArray instances;
	const auto segment = [&](QVector3D a, QVector3D b, quint32 selected) {
		append(&instances, a.x());
		append(&instances, a.y());
		append(&instances, a.z());
		append(&instances, b.x());
		append(&instances, b.y());
		append(&instances, b.z());
		append(&instances, selected);
	};
	segment(QVector3D(4, 8.5f, 0), QVector3D(44, 8.5f, 0), 0);
	segment(QVector3D(4, 28, 0), QVector3D(44, 14, 0), 1);
	frame.buffers = {{instances, 0}};
	QByteArray uniforms;
	QMatrix4x4 projection = pixelProjection(size);
	appendMatrix(&uniforms, projection);
	appendVec4(&uniforms, size.width(), size.height(), -1.0e30f, 0.0f);
	appendVec4(&uniforms, 214.0f / 255.0f, 222.0f / 255.0f, 234.0f / 255.0f, 1.0f);
	appendVec4(&uniforms, 1.0f, 170.0f / 255.0f, 60.0f / 255.0f, 1.0f);
	appendVec4(&uniforms, 1.0f, 2.4f, 4.0f, 2.0f);
	GpuDraw draw;
	draw.program = int(GpuProgram::ModelWire);
	draw.count = 6;
	draw.instances = 2;
	draw.vertexBuffers[1] = {0, 0};
	draw.uniforms = uniforms;
	draw.state.blend = true;
	draw.state.sourceColor = GpuBlend::One;
	draw.state.destinationColor = GpuBlend::OneMinusSourceAlpha;
	draw.state.sourceAlpha = GpuBlend::One;
	draw.state.destinationAlpha = GpuBlend::OneMinusSourceAlpha;
	GpuPass pass;
	pass.colors = {0, -1, -1, -1};
	pass.draws = {draw};
	frame.passes = {pass};
	frame.readbacks = {0};
	return run(device, frame, 0);
}

void checkWireScene(const QString& name, const FrameOutput& output)
{
	if (!expect(output.ok, name + QStringLiteral(": wire frame failed: ") + output.error)) {
		return;
	}
	// A one-pixel line centred on row 8's centre covers that row fully.
	expect(near(output.color.pixel(20, 8), qRgb(214, 222, 234)), name + QStringLiteral(": wire row is ") + hex(output.color.pixel(20, 8)));
	expect(qAlpha(output.color.pixel(20, 6)) == 0, name + QStringLiteral(": wire stays thin"));
	int selected = 0;
	for (int y = 12; y < 30; ++y) {
		for (int x = 0; x < 48; ++x) {
			const QRgb pixel = output.color.pixel(x, y);
			selected += qAlpha(pixel) > 0 && qRed(pixel) > qBlue(pixel) + 40;
		}
	}
	expect(selected > 30, name + QStringLiteral(": selected dashes drew %1 pixels").arg(selected));
}

// Mip levels chosen by explicit sizes, a cached texture reused across frames.
FrameOutput mipScene(RenderDevice& device, quint64 cacheKey)
{
	const QSize size(16, 16);
	GpuFrame frame;
	frame.size = size;
	frame.owner = 4;
	frame.targets = {{GpuFormat::Rgba8, {}}, {GpuFormat::R32Int, {}}, {GpuFormat::R32Float, {}}, {GpuFormat::Depth32, {}}};
	QImage level0(64, 64, QImage::Format_ARGB32_Premultiplied);
	level0.fill(qRgb(255, 0, 0));
	QVector<QImage> levels {level0};
	const QRgb colors[] = {qRgb(0, 255, 0), qRgb(0, 0, 255), qRgb(255, 255, 0), qRgb(0, 255, 255), qRgb(255, 0, 255), qRgb(255, 255, 255)};
	int side = 32;
	for (const QRgb color : colors) {
		QImage level(side, side, QImage::Format_ARGB32_Premultiplied);
		level.fill(color);
		levels.append(level);
		side = std::max(1, side / 2);
	}
	frame.textures = {{levels, cacheKey}};
	QByteArray vertices;
	QByteArray flags;
	// 16 pixels across for 4 texture repeats of a 64 texel image: 16 texels
	// a pixel, mip level 4 (cyan).
	appendTriangle(&vertices, &flags, {QVector3D(0, 0, 90), QVector3D(0, 16, 90), QVector3D(16, 0, 90)}, qRgb(0, 0, 0), 0,
		{QPointF(0, 0), QPointF(0, 4), QPointF(4, 0)});
	appendTriangle(&vertices, &flags, {QVector3D(16, 0, 90), QVector3D(0, 16, 90), QVector3D(16, 16, 90)}, qRgb(0, 0, 0), 0,
		{QPointF(4, 0), QPointF(0, 4), QPointF(4, 4)});
	frame.buffers = {{vertices, cacheKey}, {flags, cacheKey + 1}};
	GpuSampler mip;
	mip.minFilter = GpuFilter::Nearest;
	mip.magFilter = GpuFilter::Nearest;
	mip.mipmap = GpuMipmap::Nearest;
	GpuDraw draw;
	draw.program = int(GpuProgram::ModelSurface);
	draw.state.depthTest = true;
	draw.state.depthWrite = true;
	draw.state.depthCompare = GpuCompare::Greater;
	draw.vertexBuffers[0] = {0, 0};
	draw.vertexBuffers[1] = {1, 0};
	draw.count = 6;
	draw.uniforms = surfaceUniforms(pixelProjection(size), true);
	draw.textures[0] = GpuTextureRef::texture(0, mip);
	GpuPass pass;
	pass.colors = {0, 1, 2, -1};
	pass.clear[1].integer = -1;
	pass.depth = 3;
	pass.draws = {draw};
	frame.passes = {pass};
	frame.readbacks = {0};
	return run(device, frame, 0);
}

void checkMipScene(const QString& name, const FrameOutput& first, const FrameOutput& second)
{
	if (!expect(first.ok && second.ok, name + QStringLiteral(": mip frames failed: ") + first.error + second.error)) {
		return;
	}
	expect(near(first.color.pixel(8, 8), qRgb(0, 255, 255)), name + QStringLiteral(": minified texture uses level 4: ") + hex(first.color.pixel(8, 8)));
	expect(first.color == second.color, name + QStringLiteral(": a cached upload renders the same frame again"));
}

// Fixed-function culling: counter-clockwise on screen is the front.
FrameOutput windingScene(RenderDevice& device, GpuCull cull)
{
	const QSize size(16, 8);
	GpuFrame frame;
	frame.size = size;
	frame.owner = 5;
	frame.targets = {{GpuFormat::Rgba8, {}}, {GpuFormat::R32Int, {}}, {GpuFormat::R32Float, {}}};
	QByteArray vertices;
	QByteArray flags;
	// Left: counter-clockwise as seen on screen (y down): top-left,
	// bottom-left, top-right. Right: the same corners clockwise.
	appendTriangle(&vertices, &flags, {QVector3D(0, 0, 90), QVector3D(0, 8, 90), QVector3D(8, 0, 90)}, qRgb(255, 255, 255));
	appendTriangle(&vertices, &flags, {QVector3D(8, 0, 90), QVector3D(16, 0, 90), QVector3D(8, 8, 90)}, qRgb(255, 255, 255));
	frame.buffers = {{vertices, 0}, {flags, 0}};
	GpuDraw draw;
	draw.program = int(GpuProgram::ModelSurface);
	draw.state.cull = cull;
	draw.vertexBuffers[0] = {0, 0};
	draw.vertexBuffers[1] = {1, 0};
	draw.count = 6;
	// The shader's own culling is off, so only the fixed function culls.
	draw.uniforms = surfaceUniforms(pixelProjection(size), false);
	GpuPass pass;
	pass.colors = {0, 1, 2, -1};
	pass.clear[1].integer = -1;
	pass.draws = {draw};
	frame.passes = {pass};
	frame.readbacks = {0};
	return run(device, frame, 0);
}

void checkWinding(const QString& name, RenderDevice& device)
{
	const FrameOutput back = windingScene(device, GpuCull::Back);
	const FrameOutput front = windingScene(device, GpuCull::Front);
	if (!expect(back.ok && front.ok, name + QStringLiteral(": winding frames failed: ") + back.error + front.error)) {
		return;
	}
	// Pixel centres (2.5, 2.5) and (10.5, 2.5) lie inside the left and right
	// triangles.
	expect(qAlpha(back.color.pixel(2, 2)) == 255 && qAlpha(back.color.pixel(10, 2)) == 0,
		name + QStringLiteral(": culling back faces keeps the counter-clockwise triangle (%1, %2)").arg(hex(back.color.pixel(2, 2)), hex(back.color.pixel(10, 2))));
	expect(qAlpha(front.color.pixel(2, 2)) == 0 && qAlpha(front.color.pixel(10, 2)) == 255,
		name + QStringLiteral(": culling front faces keeps the clockwise triangle (%1, %2)").arg(hex(front.color.pixel(2, 2)), hex(front.color.pixel(10, 2))));
}

void checkFormats(const QString& name, RenderDevice& device)
{
	GpuFrame frame;
	frame.size = QSize(8, 4);
	frame.targets = {{GpuFormat::Rgba8, {}}, {GpuFormat::Rgba16, {}}, {GpuFormat::R32Int, {}}, {GpuFormat::R32Float, {}}};
	GpuPass pass;
	pass.colors = {0, 1, 2, 3};
	pass.clear[0].color = {1.0f, 0.5f, 0.25f, 1.0f};
	pass.clear[1].color = {0.0f, 1.0f, 0.0f, 0.5f};
	pass.clear[2].integer = 12345;
	pass.clear[3].color = {0.75f, 0.0f, 0.0f, 0.0f};
	frame.passes = {pass};
	frame.readbacks = {0, 1, 2, 3};
	const GpuFrameResult result = device.render(frame);
	if (!expect(result.success, name + QStringLiteral(": clear frame failed: ") + result.error + QStringLiteral(" / ") + result.errorDetail)) {
		return;
	}
	const QImage rgba8 = result.readback(0)->image(QImage::Format_ARGB32);
	expect(near(rgba8.pixel(3, 2), qRgba(255, 128, 64, 255)), name + QStringLiteral(": Rgba8 clear reads ") + hex(rgba8.pixel(3, 2)));
	const QImage rgba16 = result.readback(1)->image(QImage::Format_ARGB32);
	expect(near(rgba16.pixel(0, 0), qRgba(0, 255, 0, 128)), name + QStringLiteral(": Rgba16 clear reads ") + hex(rgba16.pixel(0, 0)));
	const QVector<int> integers = result.readback(2)->integers();
	expect(integers.size() == 32 && integers.at(31) == 12345, name + QStringLiteral(": R32Int clear"));
	const QVector<float> floats = result.readback(3)->floats();
	expect(floats.size() == 32 && std::abs(floats.at(0) - 0.75f) < 1e-6f, name + QStringLiteral(": R32Float clear"));
}

void checkMalformed(const QString& name, RenderDevice& device)
{
	GpuFrame frame;
	frame.size = QSize(4, 4);
	frame.targets = {{GpuFormat::Rgba8, {}}};
	GpuPass pass;
	pass.colors = {0, -1, -1, -1};
	GpuDraw draw;
	draw.program = int(GpuProgram::ModelSurface);
	draw.count = 3;
	draw.vertexBuffers[0] = {7, 0};
	pass.draws = {draw};
	frame.passes = {pass};
	const GpuFrameResult result = device.render(frame);
	expect(!result.success && !result.error.isEmpty() && result.errorDetail.contains(QStringLiteral("vertex buffer")),
		name + QStringLiteral(": a frame reading a missing buffer is refused: ") + result.errorDetail);
	std::atomic_bool cancelled {true};
	const GpuFrameResult skipped = device.render(frame, &cancelled);
	expect(skipped.cancelled && !skipped.success, name + QStringLiteral(": a cancelled frame is skipped"));
}

} // namespace

int main(int argc, char** argv)
{
	QGuiApplication application(argc, argv);
	prepareRenderBackends();
	const QStringList required = qEnvironmentVariable("VIBESTUDIO_RENDER_REQUIRE").split(QLatin1Char(','), Qt::SkipEmptyParts);
	expect(gpuProgramCount() == int(GpuProgram::Count), QStringLiteral("program table size"));
	for (int program = 0; program < gpuProgramCount(); ++program) {
		const GpuProgramInfo* info = gpuProgramInfo(program);
		expect(info && info->vertexGlsl && info->fragmentGlsl && info->vertexSpirvWords > 5 && info->fragmentSpirvWords > 5
				&& info->vertexSpirv[0] == 0x07230203u && info->fragmentSpirv[0] == 0x07230203u,
			QStringLiteral("program %1 has OpenGL text and SPIR-V").arg(program));
	}
	RenderBackendChoice choice = RenderBackendChoice::Automatic;
	expect(renderBackendChoiceFromId(QStringLiteral(" Vulkan "), &choice) && choice == RenderBackendChoice::Vulkan, QStringLiteral("choice ids parse"));
	expect(!renderBackendChoiceFromId(QStringLiteral("software"), &choice), QStringLiteral("there is no software choice"));

	struct Run {
		RenderBackend backend;
		FrameOutput surface;
		FrameOutput surfaceCulled;
		FrameOutput translucent;
		FrameOutput wire;
	};
	QVector<Run> runs;
	for (RenderBackend backend : renderBackends()) {
		const auto device = renderDevice(backend);
		const RenderDeviceInfo info = device->info();
		const QString name = renderBackendDisplayName(backend);
		std::printf("%s: %s (%s) started in %lld ms%s%s\n", qPrintable(name), info.available ? "available" : "unavailable",
			qPrintable(renderDeviceSummary(info)), info.startupMilliseconds, info.available ? "" : ": ",
			info.available ? "" : qPrintable(info.error + QStringLiteral(" [") + info.errorDetail + QStringLiteral("]")));
		std::fflush(stdout);
		if (!info.available) {
			expect(!required.contains(renderBackendId(backend)), name + QStringLiteral(" is required but unavailable: ") + info.error);
			expect(!info.error.isEmpty(), name + QStringLiteral(" says why it is unavailable"));
			continue;
		}
		expect(!info.deviceName.isEmpty() && !info.apiVersion.isEmpty() && info.maxTextureSize >= 2048, name + QStringLiteral(" reports its device"));
		checkFormats(name, *device);
		Run run;
		run.backend = backend;
		run.surface = surfaceScene(*device, false);
		checkSurfaceScene(name, run.surface);
		run.surfaceCulled = surfaceScene(*device, true);
		checkSurfaceScene(name + QStringLiteral(" (fixed-function culling)"), run.surfaceCulled);
		run.translucent = translucentScene(*device);
		checkTranslucentScene(name, run.translucent);
		run.wire = wireScene(*device);
		checkWireScene(name, run.wire);
		const FrameOutput mipFirst = mipScene(*device, 900);
		const FrameOutput mipSecond = mipScene(*device, 900);
		checkMipScene(name, mipFirst, mipSecond);
		checkWinding(name, *device);
		checkMalformed(name, *device);
		device->releaseOwner(4);
		runs.append(run);
	}
	if (runs.size() == 2) {
		// The two backends draw the same pixels.
		const auto compare = [](const QImage& a, const QImage& b, const QString& what) {
			if (!expect(a.size() == b.size(), what + QStringLiteral(": sizes differ"))) {
				return;
			}
			int different = 0;
			for (int y = 0; y < a.height(); ++y) {
				for (int x = 0; x < a.width(); ++x) {
					different += !near(a.pixel(x, y), b.pixel(x, y), 2);
				}
			}
			expect(different == 0, what + QStringLiteral(": %1 pixels differ between OpenGL and Vulkan").arg(different));
		};
		compare(runs[0].surface.color, runs[1].surface.color, QStringLiteral("surfaces"));
		compare(runs[0].translucent.color, runs[1].translucent.color, QStringLiteral("translucency"));
		compare(runs[0].wire.color, runs[1].wire.color, QStringLiteral("wireframe"));
		expect(runs[0].surface.ids == runs[1].surface.ids, QStringLiteral("triangle ids match between OpenGL and Vulkan"));
	}
	RenderDeviceInfo failure;
	const auto active = activeRenderDevice(&failure);
	if (runs.isEmpty()) {
		expect(!active && !failure.error.isEmpty(), QStringLiteral("with no backend, the active device explains why"));
		std::printf("No GPU backend is available here: %s\n", qPrintable(failure.error));
		shutdownRenderBackends();
		return failures == 0 ? 77 : 1;
	}
	expect(active != nullptr, QStringLiteral("an available backend becomes the active device"));
	shutdownRenderBackends();
	if (failures == 0) {
		std::printf("render device smoke: %lld backend(s) passed\n", qlonglong(runs.size()));
	}
	return failures == 0 ? 0 : 1;
}
