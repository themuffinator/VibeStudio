// The model viewport's GPU frames.
//
// A render worker turns its value snapshot into a GpuFrame for the active
// backend (core/render_device.h): world-space geometry for the current pose,
// uploaded once and reused while only the camera moves; per-corner selection
// flags; and the camera as a matrix in the canonical clip space. Filled
// modes draw opaque fragments with depth testing, then peel translucent skin
// texels front to back in up to four layers and composite them in depth
// order. Wireframe draws every visible edge once as an antialiased stroke,
// with a depth-tested id pass beneath it for picking. Every frame reads back
// the colour image and the triangle under each pixel, which is what picking,
// orbit pivots and vertex visibility use.

#include "app/model_viewport_p.h"

#include "core/model_pose.h"
#include "core/render_device.h"
#include "core/render_shaders.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>

#include <atomic>
#include <cstring>
#include <limits>

namespace vibestudio {

using namespace model_viewport_detail;

namespace {

constexpr int kCornerBytes = 44;
constexpr int kTranslucentLayers = 4;
// Images larger than this are reduced before upload; idTech skins and
// textures are far smaller, and every device takes this size.
constexpr int kMaximumSkinSide = 4096;

enum SurfaceTarget {
	Color,
	Ids,
	OpaqueDepth,
	Depth,
	LayerColor,
	LayerDepthA,
	LayerDepthB,
	Accumulated,
};

std::atomic<quint64> nextUploadKey {1};

using Matrix = std::array<std::array<double, 4>, 4>;

void appendMatrix(QByteArray* bytes, const Matrix& matrix)
{
	// std140 mat4: four column vectors.
	for (int column = 0; column < 4; ++column) {
		for (int row = 0; row < 4; ++row) {
			const float value = static_cast<float>(matrix[size_t(row)][size_t(column)]);
			bytes->append(reinterpret_cast<const char*>(&value), sizeof(value));
		}
	}
}

void appendVec4(QByteArray* bytes, double x, double y, double z, double w)
{
	const float values[4] = {float(x), float(y), float(z), float(w)};
	bytes->append(reinterpret_cast<const char*>(values), sizeof(values));
}

void appendColor(QByteArray* bytes, const QColor& color)
{
	appendVec4(bytes, color.redF(), color.greenF(), color.blueF(), color.alphaF());
}

void appendPremultiplied(QByteArray* bytes, const QColor& color)
{
	const double alpha = color.alphaF();
	appendVec4(bytes, color.redF() * alpha, color.greenF() * alpha, color.blueF() * alpha, alpha);
}

void appendIvec4(QByteArray* bytes, int x, int y, int z, int w)
{
	const qint32 values[4] = {x, y, z, w};
	bytes->append(reinterpret_cast<const char*>(values), sizeof(values));
}

template <typename T>
void put(char* destination, const T& value)
{
	std::memcpy(destination, &value, sizeof(T));
}

bool sameTransform(const ModelTransform& a, const ModelTransform& b)
{
	if (!sameVec(a.translation, b.translation) || !sameVec(a.rotation, b.rotation) || !sameVec(a.scale, b.scale) || !sameVec(a.pivot, b.pivot)
		|| a.basis.world != b.basis.world) {
		return false;
	}
	for (size_t axis = 0; axis < 3; ++axis) {
		if (!sameVec(a.basis.axes[axis], b.basis.axes[axis])) {
			return false;
		}
	}
	return true;
}

// The camera as one matrix from world space to canonical clip space,
// reproducing ModelViewport's projection in physical pixels: orthographic
// views scale about the model centre with depth nearness spanning the
// geometry, perspective views divide by distance with an infinite far plane
// and the near plane at clip w = 1.
Matrix viewProjection(const ModelViewport::RasterWork& work, const ModelViewport::GpuGeometry* geometry)
{
	const auto& camera = work.camera;
	const double width = std::max(1, work.size.width());
	const double height = std::max(1, work.size.height());
	const double ratio = work.pixelRatio;
	Matrix m {};
	if (work.perspective) {
		const ModelVec3& p = camera.position;
		const double rightOffset = -dot(camera.right, p);
		const double upOffset = -dot(camera.up, p);
		const double forwardOffset = -dot(camera.forward, p);
		const double sx = 2.0 * ratio * camera.focal / width;
		const double cx = 2.0 * ratio * camera.origin.x() / width - 1.0;
		const double sy = 2.0 * ratio * camera.focal / height;
		const double cy = 2.0 * ratio * camera.origin.y() / height - 1.0;
		const double right[4] = {camera.right.x, camera.right.y, camera.right.z, rightOffset};
		const double up[4] = {camera.up.x, camera.up.y, camera.up.z, upOffset};
		const double forward[4] = {camera.forward.x, camera.forward.y, camera.forward.z, forwardOffset};
		for (size_t column = 0; column < 4; ++column) {
			m[0][column] = sx * right[column] + cx * forward[column];
			m[1][column] = -sy * up[column] + cy * forward[column];
			m[2][column] = 0.0;
			m[3][column] = forward[column];
		}
		m[2][3] = kNearPlane;
		return m;
	}
	const ModelVec3& c = work.center;
	const double scale = camera.scale;
	const double ax = 2.0 * ratio * scale / width;
	const double ay = 2.0 * ratio * scale / height;
	m[0] = {ax * camera.right.x, ax * camera.right.y, ax * camera.right.z, 2.0 * ratio * (camera.origin.x() - scale * dot(camera.right, c)) / width - 1.0};
	m[1] = {-ay * camera.up.x, -ay * camera.up.y, -ay * camera.up.z, 2.0 * ratio * (camera.origin.y() + scale * dot(camera.up, c)) / height - 1.0};
	// Depth toward the viewer, mapped to 0..1 over the geometry's extent.
	double low = -1.0;
	double high = 1.0;
	if (geometry) {
		const double centre = dot(subtract(geometry->centre, c), camera.eye);
		const double reach = geometry->radius * 1.01 + 1.0;
		low = centre - reach;
		high = centre + reach;
	}
	const double span = std::max(high - low, 1.0e-6);
	m[2] = {camera.eye.x / span, camera.eye.y / span, camera.eye.z / span, (-dot(camera.eye, c) - low) / span};
	m[3] = {0.0, 0.0, 0.0, 1.0};
	return m;
}

QByteArray surfaceUniforms(const ModelViewport::RasterWork& work, const Matrix& matrix, bool textured, int pass, int firstTriangle)
{
	QByteArray bytes;
	bytes.reserve(208);
	appendMatrix(&bytes, matrix);
	if (work.perspective) {
		appendVec4(&bytes, work.camera.position.x, work.camera.position.y, work.camera.position.z, 1.0);
	} else {
		appendVec4(&bytes, work.camera.eye.x, work.camera.eye.y, work.camera.eye.z, 0.0);
	}
	appendVec4(&bytes, work.camera.light.x, work.camera.light.y, work.camera.light.z, 0.0);
	const QColor& background = work.palette.background;
	appendVec4(&bytes, background.redF(), background.greenF(), background.blueF(), (work.highContrast ? 200 : 110) / 255.0);
	appendColor(&bytes, work.palette.edge);
	appendColor(&bytes, work.palette.hover);
	appendColor(&bytes, work.palette.highlight);
	appendColor(&bytes, work.palette.highlight);
	appendVec4(&bytes, work.pixelRatio, work.showEdges ? 1.0 : 0.0, textured ? 1.0 : 0.0, 0.0);
	appendIvec4(&bytes, work.hover, firstTriangle, work.backfaceCulling ? 1 : 0, pass);
	return bytes;
}

QByteArray compositeUniforms()
{
	QByteArray bytes;
	appendVec4(&bytes, 1.0, 1.0, 1.0, 1.0);
	appendVec4(&bytes, 0.0, 0.0, 0.0, 0.0);
	return bytes;
}

GpuState opaqueState()
{
	GpuState state;
	state.depthTest = true;
	state.depthWrite = true;
	state.depthCompare = GpuCompare::Greater;
	return state;
}

// Premultiplied "under": what is already in the target stays in front.
GpuState underState()
{
	GpuState state;
	state.blend = true;
	state.sourceColor = GpuBlend::OneMinusDestinationAlpha;
	state.destinationColor = GpuBlend::One;
	state.sourceAlpha = GpuBlend::OneMinusDestinationAlpha;
	state.destinationAlpha = GpuBlend::One;
	return state;
}

GpuSampler skinSampler()
{
	GpuSampler sampler;
	sampler.minFilter = GpuFilter::Linear;
	sampler.magFilter = GpuFilter::Linear;
	sampler.wrapU = GpuWrap::Repeat;
	sampler.wrapV = GpuWrap::Repeat;
	return sampler;
}

} // namespace

QColor modelSurfaceBaseColor(int surfaceIndex, bool highContrast)
{
	if (highContrast) {
		// Maximally separated hues at full value; surfaces also carry their name
		// in the hover readout, so colour is never the only distinction.
		static const QColor kColors[] = {
			QColor(255, 255, 255),
			QColor(255, 255, 0),
			QColor(0, 255, 255),
			QColor(255, 0, 255),
			QColor(0, 255, 0),
			QColor(255, 160, 0),
		};
		const int count = static_cast<int>(sizeof(kColors) / sizeof(kColors[0]));
		return kColors[((surfaceIndex % count) + count) % count];
	}
	const int hue = ((surfaceIndex * 47 + 205) % 360 + 360) % 360;
	return QColor::fromHsv(hue, 92, 214);
}

bool modelSkinHasAlpha(const QImage& image)
{
	if (!image.hasAlphaChannel()) {
		return false;
	}
	for (int y = 0; y < image.height(); ++y) {
		for (int x = 0; x < image.width(); ++x) {
			if (qAlpha(image.pixel(x, y)) != 255) {
				return true;
			}
		}
	}
	return false;
}

bool ModelViewport::GeometryKey::matches(const GeometryKey& other) const
{
	if (mesh != other.mesh || frame != other.frame || blendFrame != other.blendFrame || frameBlend != other.frameBlend || textured != other.textured
		|| highContrast != other.highContrast || editMoveActive != other.editMoveActive || moving != other.moving || resizing != other.resizing) {
		return false;
	}
	if (editMoveActive
		&& (editSurface != other.editSurface || editTagEmpty != other.editTagEmpty || !sameTransform(editTransform, other.editTransform)
			|| editSurfaces != other.editSurfaces || editVertices != other.editVertices)) {
		return false;
	}
	if (moving && !sameVec(moveOffset, other.moveOffset)) {
		return false;
	}
	if (resizing && (!(resizeFrom == other.resizeFrom) || !(resizeTo == other.resizeTo) || resizeOrigins != other.resizeOrigins)) {
		return false;
	}
	return !(moving || resizing) || highlighted == other.highlighted;
}

bool ModelViewport::FlagsKey::matches(const FlagsKey& other) const
{
	return mesh == other.mesh && edgeSelectionSurface == other.edgeSelectionSurface && highlighted == other.highlighted
		&& strokeTriangles == other.strokeTriangles && selectedEdges == other.selectedEdges;
}

std::shared_ptr<const ModelViewport::GpuGeometry> ModelViewport::buildGpuGeometry(const RasterWork& work)
{
	auto geometry = std::make_shared<GpuGeometry>();
	geometry->key = work.geometryKey;
	geometry->cacheKey = nextUploadKey.fetch_add(1);
	const int count = static_cast<int>(work.meshTriangles.size());
	geometry->vertices = QByteArray(qsizetype(count) * 3 * kCornerBytes, Qt::Uninitialized);
	geometry->planes.resize(count);
	geometry->valid.resize(count);
	char* output = geometry->vertices.data();
	const int surfaceCount = std::max(1, static_cast<int>(work.mesh.surfaces.size()));
	double low[3] = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
	double high[3] = {-low[0], -low[1], -low[2]};
	int currentSurface = -1;
	const ModelFrameGeometry* pose = nullptr;
	std::array<quint8, 4> surfaceColor {};
	const auto writeCorner = [&](int triangle, int corner, const ModelVec3& position, QPointF uv, const ModelVec3& normal, float d, quint32 info) {
		char* at = output + (qsizetype(triangle) * 3 + corner) * kCornerBytes;
		const float values[9] = {position.x, position.y, position.z, float(uv.x()), float(uv.y()), normal.x, normal.y, normal.z, d};
		std::memcpy(at, values, sizeof(values));
		std::memcpy(at + 36, surfaceColor.data(), 4);
		put(at + 40, info);
	};
	for (int index = 0; index < count; ++index) {
		if ((index & 4095) == 0 && work.cancelled.load()) {
			return nullptr;
		}
		const MeshTriangle& source = work.meshTriangles.at(index);
		if (source.surface != currentSurface) {
			currentSurface = source.surface;
			pose = nullptr;
			if (currentSurface >= 0 && currentSurface < work.mesh.surfaces.size()) {
				const ModelSurface& surface = work.mesh.surfaces.at(currentSurface);
				const int frameIndex = std::min(work.frame, static_cast<int>(surface.frames.size()) - 1);
				if (frameIndex >= 0) {
					pose = &surface.frames.at(frameIndex);
				}
			}
			const QColor color = modelSurfaceBaseColor(std::clamp(currentSurface, 0, surfaceCount - 1), work.highContrast);
			surfaceColor = {quint8(color.red()), quint8(color.green()), quint8(color.blue()), 255};
		}
		const auto invalid = [&]() {
			geometry->planes[index] = {0.0f, 0.0f, 0.0f, 0.0f};
			for (int corner = 0; corner < 3; ++corner) {
				writeCorner(index, corner, ModelVec3 {}, QPointF(), ModelVec3 {}, 0.0f, 2u);
			}
		};
		if (!pose) {
			invalid();
			continue;
		}
		const QVector<ModelVec3>& positions = pose->positions;
		if (source.a >= positions.size() || source.b >= positions.size() || source.c >= positions.size()) {
			invalid();
			continue;
		}
		std::array<ModelVec3, 3> p {positions.at(source.a), positions.at(source.b), positions.at(source.c)};
		if (work.frameBlend > 0 || (work.editMoveActive && (source.surface == work.editSurface || work.editSurfaces.contains(source.surface)))) {
			p = {work.vertexPosition(source.surface, source.a), work.vertexPosition(source.surface, source.b), work.vertexPosition(source.surface, source.c)};
		}
		if (!finiteVec(p[0]) || !finiteVec(p[1]) || !finiteVec(p[2])) {
			invalid();
			continue;
		}
		const bool highlighted = index < work.highlighted.size() && work.highlighted.at(index);
		if (work.moving && highlighted) {
			for (ModelVec3& corner : p) {
				corner.x += work.moveOffset.x;
				corner.y += work.moveOffset.y;
				corner.z += work.moveOffset.z;
			}
		}
		if (work.resizingSelection && highlighted) {
			const auto origin = work.resizeOrigins.constFind(index);
			BoxResizePoint translation {};
			if (origin != work.resizeOrigins.constEnd()) {
				const auto moved = resizeBoxPoint(work.resizeFrom, work.resizeTo, *origin);
				for (int axis = 0; axis < 3; ++axis) {
					translation[size_t(axis)] = moved[size_t(axis)] - (*origin)[size_t(axis)];
				}
			}
			for (ModelVec3& corner : p) {
				BoxResizePoint target {corner.x, corner.y, corner.z};
				if (origin == work.resizeOrigins.constEnd()) {
					target = resizeBoxPoint(work.resizeFrom, work.resizeTo, target);
				} else {
					for (int axis = 0; axis < 3; ++axis) {
						target[size_t(axis)] += translation[size_t(axis)];
					}
				}
				corner = vec(target[0], target[1], target[2]);
			}
		}
		ModelVec3 normal = cross(subtract(p[1], p[0]), subtract(p[2], p[0]));
		const double normalLength = std::sqrt(dot(normal, normal));
		if (!std::isfinite(normalLength) || normalLength <= 1e-12) {
			// A zero-area triangle has no facing and no shade.
			invalid();
			continue;
		}
		normal = vec(normal.x / normalLength, normal.y / normalLength, normal.z / normalLength);
		// MDL, MD2 and MD3 all wind their triangles counter-clockwise when seen
		// from outside, but decoded files in the wild are not always consistent.
		// When the format carries vertex normals, they settle the argument.
		const bool transforming = work.editMoveActive && work.editTagEmpty
			&& (source.surface == work.editSurface || work.editSurfaces.contains(source.surface));
		if (pose->normals.size() == positions.size()
			&& (!transforming || work.editSurfaces.contains(source.surface) || work.editVertices.size() == positions.size())) {
			const auto& frames = work.mesh.surfaces[currentSurface].frames;
			const auto normalAt = [&](int vertex) {
				auto vertexNormal = pose->normals.at(vertex);
				if (work.frameBlend > 0 && work.blendFrame < frames.size() && frames[work.blendFrame].positions.size() == positions.size()
					&& frames[work.blendFrame].normals.size() == positions.size()) {
					// Opposing or invalid normals have no unique direction; the
					// geometric face normal decides facing for that pose.
					if (!interpolateModelNormal(vertexNormal, frames[work.blendFrame].normals[vertex], work.frameBlend, &vertexNormal)) {
						vertexNormal = {};
					}
				}
				return transforming ? transformModelNormal(vertexNormal, work.editTransform) : vertexNormal;
			};
			const auto na = normalAt(source.a);
			const auto nb = normalAt(source.b);
			const auto nc = normalAt(source.c);
			const ModelVec3 average = vec(double(na.x) + nb.x + nc.x, double(na.y) + nb.y + nc.y, double(na.z) + nb.z + nc.z);
			if (std::sqrt(dot(average, average)) > 1e-6 && dot(normal, average) < 0.0) {
				normal = vec(-normal.x, -normal.y, -normal.z);
			}
		}
		const auto& coords = work.mesh.surfaces.at(source.surface).texCoords;
		bool uvValid = work.textured && source.a < coords.size() && source.b < coords.size() && source.c < coords.size();
		std::array<QPointF, 3> uv {};
		if (uvValid) {
			const int indices[] = {source.a, source.b, source.c};
			for (int corner = 0; corner < 3; ++corner) {
				const auto& value = coords.at(indices[corner]);
				if (!std::isfinite(value.u) || !std::isfinite(value.v)) {
					uvValid = false;
					break;
				}
				uv[size_t(corner)] = QPointF(value.u, value.v);
			}
		}
		if (!uvValid) {
			uv = {};
		}
		const float d = static_cast<float>(dot(normal, p[0]));
		geometry->planes[index] = {normal.x, normal.y, normal.z, d};
		geometry->valid.setBit(index);
		for (int corner = 0; corner < 3; ++corner) {
			writeCorner(index, corner, p[size_t(corner)], uv[size_t(corner)], normal, d, uvValid ? 1u : 0u);
			const double values[3] = {p[size_t(corner)].x, p[size_t(corner)].y, p[size_t(corner)].z};
			for (int axis = 0; axis < 3; ++axis) {
				low[axis] = std::min(low[axis], values[axis]);
				high[axis] = std::max(high[axis], values[axis]);
			}
		}
	}
	if (std::isfinite(low[0])) {
		geometry->centre = vec((low[0] + high[0]) * 0.5, (low[1] + high[1]) * 0.5, (low[2] + high[2]) * 0.5);
		geometry->radius = std::max(1.0, 0.5 * std::hypot(high[0] - low[0], high[1] - low[1], high[2] - low[2]));
	}
	return geometry;
}

std::shared_ptr<const ModelViewport::GpuFlags> ModelViewport::buildGpuFlags(const RasterWork& work)
{
	auto flags = std::make_shared<GpuFlags>();
	flags->key = work.flagsKey;
	flags->cacheKey = nextUploadKey.fetch_add(1);
	const int count = static_cast<int>(work.meshTriangles.size());
	flags->bytes = QByteArray(qsizetype(count) * 3 * 4, Qt::Uninitialized);
	char* output = flags->bytes.data();
	for (int index = 0; index < count; ++index) {
		const MeshTriangle& source = work.meshTriangles.at(index);
		quint32 flag = (index < work.highlighted.size() && work.highlighted.at(index)) || work.surfaceStrokeTriangles.contains(index) ? 1u : 0u;
		if (source.surface == work.edgeSelectionSurface && !work.selectedEdges.isEmpty()) {
			// Edge e is opposite corner e.
			const QPair<int, int> edges[] = {{source.b, source.c}, {source.c, source.a}, {source.a, source.b}};
			for (int edge = 0; edge < 3; ++edge) {
				const auto key = qMakePair(std::min(edges[edge].first, edges[edge].second), std::max(edges[edge].first, edges[edge].second));
				if (work.selectedEdges.contains(key)) {
					flag |= 4u << edge;
				}
			}
		}
		for (int corner = 0; corner < 3; ++corner) {
			put(output + (qsizetype(index) * 3 + corner) * 4, flag);
		}
	}
	return flags;
}

void ModelViewport::renderGpu(RasterWork& work)
{
	if (!work.geometry) {
		work.geometry = buildGpuGeometry(work);
	}
	if (!work.flags && work.geometry) {
		work.flags = buildGpuFlags(work);
	}
	if (work.cancelled.load() || !work.geometry || !work.flags) {
		return;
	}
	const GpuGeometry& geometry = *work.geometry;
	const int count = static_cast<int>(work.meshTriangles.size());

	// Facing for the readouts and the wireframe; the GPU repeats the same test
	// for drawing from the same planes.
	struct Segment {
		ModelVec3 a, b;
	};
	QVector<Segment> ordinary;
	QVector<Segment> selected;
	QSet<quint64> baseEdges;
	QSet<quint64> highlightedEdges;
	int surface = -1;
	work.visibleTriangles = 0;
	work.culledTriangles = 0;
	for (int index = 0; index < count; ++index) {
		if ((index & 4095) == 0 && work.cancelled.load()) {
			return;
		}
		if (!geometry.valid.testBit(index)) {
			continue;
		}
		const auto& plane = geometry.planes.at(index);
		const ModelVec3 normal {plane[0], plane[1], plane[2]};
		const double facing = work.perspective ? dot(normal, work.camera.position) - plane[3] : dot(normal, work.camera.eye);
		if (work.backfaceCulling && !(facing > 0.0)) {
			++work.culledTriangles;
			continue;
		}
		if (work.perspective) {
			bool ahead = false;
			for (int corner = 0; corner < 3 && !ahead; ++corner) {
				ahead = dot(subtract(geometry.corner(index, corner), work.camera.position), work.camera.forward) >= kNearPlane;
			}
			if (!ahead) {
				continue;
			}
		}
		++work.visibleTriangles;
		if (!work.wireframe) {
			continue;
		}
		const MeshTriangle& source = work.meshTriangles.at(index);
		if (source.surface != surface) {
			surface = source.surface;
			baseEdges.clear();
			highlightedEdges.clear();
		}
		const bool highlighted = (index < work.highlighted.size() && work.highlighted.at(index)) || work.surfaceStrokeTriangles.contains(index);
		const int vertices[3] = {source.a, source.b, source.c};
		for (int corner = 0; corner < 3; ++corner) {
			const int next = (corner + 1) % 3;
			const auto edge = qMakePair(std::min(vertices[corner], vertices[next]), std::max(vertices[corner], vertices[next]));
			const quint64 key = (quint64(quint32(edge.first)) << 32) | quint32(edge.second);
			// A legacy face move can separate shared endpoints: keep every
			// segment then. Otherwise a shared edge draws once per surface.
			const bool shared = !work.moving;
			const Segment segment {geometry.corner(index, corner), geometry.corner(index, next)};
			if (!shared || !baseEdges.contains(key)) {
				ordinary.append(segment);
				if (shared) {
					baseEdges.insert(key);
				}
			}
			const bool explicitEdge = source.surface == work.edgeSelectionSurface && work.selectedEdges.contains(edge);
			if (highlighted && !explicitEdge && (!shared || !highlightedEdges.contains(key))) {
				selected.append(segment);
				if (shared) {
					highlightedEdges.insert(key);
				}
			}
		}
	}
	if (work.wireframe && work.edgeSelectionSurface >= 0 && work.edgeSelectionSurface < work.mesh.surfaces.size()) {
		const auto& part = std::as_const(work.mesh).surfaces[work.edgeSelectionSurface];
		const int frame = std::min(work.frame, int(part.frames.size()) - 1);
		if (frame >= 0) {
			for (const auto& edge : std::as_const(work.selectedEdges)) {
				const int vertexCount = part.frames[frame].positions.size();
				if (edge.first < 0 || edge.second >= vertexCount) {
					continue;
				}
				selected.append({work.vertexPosition(work.edgeSelectionSurface, edge.first), work.vertexPosition(work.edgeSelectionSurface, edge.second)});
			}
		}
	}
	if (work.reuseBase) {
		work.success = true;
		return;
	}

	RenderDeviceInfo failure;
	const std::shared_ptr<RenderDevice> device = activeRenderDevice(&failure);
	if (!device) {
		work.error = failure.error;
		work.errorDetail = failure.errorDetail;
		return;
	}
	const RenderDeviceInfo info = device->info();
	work.deviceSummary = vibestudio::renderDeviceSummary(info);
	const int skinLimit = std::max(64, std::min(kMaximumSkinSide, info.maxTextureSize > 0 ? info.maxTextureSize : kMaximumSkinSide));
	const Matrix matrix = viewProjection(work, &geometry);

	GpuFrame frame;
	frame.size = work.size;
	frame.owner = work.renderOwner;
	frame.buffers = {{geometry.vertices, geometry.cacheKey}, {work.flags->bytes, work.flags->cacheKey}};
	QHash<qint64, int> textureIndex;
	const auto textureFor = [&](int surfaceIndex, bool* alpha) {
		*alpha = false;
		if (!work.textured) {
			return -1;
		}
		const auto found = work.surfaceSkins.constFind(surfaceIndex);
		const QImage& image = found == work.surfaceSkins.cend() ? work.skin : found.value();
		if (image.isNull()) {
			return -1;
		}
		*alpha = found == work.surfaceSkins.cend() ? work.skinHasAlpha : work.surfaceSkinAlpha.contains(surfaceIndex);
		const auto known = textureIndex.constFind(image.cacheKey());
		if (known != textureIndex.cend()) {
			return known.value();
		}
		GpuTextureData texture;
		if (image.width() > skinLimit || image.height() > skinLimit) {
			texture.levels = {image.scaled(std::min(image.width(), skinLimit), std::min(image.height(), skinLimit), Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation)};
		} else {
			texture.levels = {image};
			texture.cacheKey = quint64(image.cacheKey());
		}
		const int index = static_cast<int>(frame.textures.size());
		frame.textures.append(texture);
		textureIndex.insert(image.cacheKey(), index);
		return index;
	};
	// One draw per surface run (the triangle list is surface-major).
	struct Run {
		int first = 0;
		int count = 0;
		int texture = -1;
		bool alpha = false;
	};
	QVector<Run> runs;
	for (int index = 0; index < count;) {
		const int runSurface = work.meshTriangles.at(index).surface;
		int end = index + 1;
		while (end < count && work.meshTriangles.at(end).surface == runSurface) {
			++end;
		}
		Run run;
		run.first = index;
		run.count = end - index;
		run.texture = work.wireframe ? -1 : textureFor(runSurface, &run.alpha);
		runs.append(run);
		index = end;
	}
	const auto surfaceDraw = [&](const Run& run, int pass, int nearTexture, int previousTexture) {
		GpuDraw draw;
		draw.program = int(GpuProgram::ModelSurface);
		draw.state = opaqueState();
		draw.vertexBuffers[0] = {0, qint64(run.first) * 3 * kCornerBytes};
		draw.vertexBuffers[1] = {1, qint64(run.first) * 3 * 4};
		draw.count = run.count * 3;
		draw.uniforms = surfaceUniforms(work, matrix, run.texture >= 0, pass, run.first);
		if (run.texture >= 0) {
			draw.textures[0] = GpuTextureRef::texture(run.texture, skinSampler());
		}
		if (nearTexture >= 0) {
			draw.textures[1] = GpuTextureRef::target(nearTexture);
		}
		if (previousTexture >= 0) {
			draw.textures[2] = GpuTextureRef::target(previousTexture);
		}
		return draw;
	};

	int colorResult = Color;
	if (work.wireframe) {
		frame.targets = {{GpuFormat::Rgba8, {}}, {GpuFormat::R32Int, {}}, {GpuFormat::R32Float, {}}, {GpuFormat::Depth32, {}}};
		// Ids for picking, depth tested; no colour.
		GpuPass ids;
		ids.colors = {-1, Ids, -1, -1};
		ids.clear[1].integer = -1;
		ids.depth = Depth;
		for (const Run& run : std::as_const(runs)) {
			ids.draws.append(surfaceDraw(run, 0, -1, -1));
		}
		// Every visible edge once, selected edges after ordinary ones.
		QByteArray instances;
		instances.reserve((ordinary.size() + selected.size()) * 28);
		const auto appendSegment = [&](const Segment& segment, quint32 isSelected) {
			const float values[6] = {segment.a.x, segment.a.y, segment.a.z, segment.b.x, segment.b.y, segment.b.z};
			instances.append(reinterpret_cast<const char*>(values), sizeof(values));
			instances.append(reinterpret_cast<const char*>(&isSelected), sizeof(isSelected));
		};
		for (const Segment& segment : std::as_const(ordinary)) {
			appendSegment(segment, 0u);
		}
		for (const Segment& segment : std::as_const(selected)) {
			appendSegment(segment, 1u);
		}
		GpuPass lines;
		lines.colors = {Color, -1, -1, -1};
		if (!instances.isEmpty()) {
			frame.buffers.append({instances, 0});
			GpuDraw draw;
			draw.program = int(GpuProgram::ModelWire);
			draw.count = 6;
			draw.instances = static_cast<int>(ordinary.size() + selected.size());
			draw.vertexBuffers[1] = {static_cast<int>(frame.buffers.size()) - 1, 0};
			draw.state.blend = true;
			draw.state.sourceColor = GpuBlend::One;
			draw.state.destinationColor = GpuBlend::OneMinusSourceAlpha;
			draw.state.sourceAlpha = GpuBlend::One;
			draw.state.destinationAlpha = GpuBlend::OneMinusSourceAlpha;
			QByteArray uniforms;
			appendMatrix(&uniforms, matrix);
			appendVec4(&uniforms, work.size.width(), work.size.height(), work.perspective ? kNearPlane : -1.0e30, 0.0);
			appendPremultiplied(&uniforms, work.palette.wire);
			appendPremultiplied(&uniforms, work.palette.highlight);
			appendVec4(&uniforms, (work.highContrast ? 1.6 : 1.0) * work.pixelRatio, (work.highContrast ? 3.2 : 2.4) * work.pixelRatio, 4.0, 2.0);
			draw.uniforms = uniforms;
			lines.draws.append(draw);
		}
		frame.passes = {ids, lines};
	} else {
		const bool translucent = std::any_of(runs.cbegin(), runs.cend(), [](const Run& run) { return run.texture >= 0 && run.alpha; });
		frame.targets = {{GpuFormat::Rgba8, {}}, {GpuFormat::R32Int, {}}, {GpuFormat::R32Float, {}}, {GpuFormat::Depth32, {}}};
		GpuPass opaque;
		opaque.colors = {Color, Ids, OpaqueDepth, -1};
		opaque.clear[1].integer = -1;
		opaque.depth = Depth;
		for (const Run& run : std::as_const(runs)) {
			opaque.draws.append(surfaceDraw(run, 0, -1, -1));
		}
		frame.passes = {opaque};
		if (translucent) {
			frame.targets.append({{GpuFormat::Rgba8, {}}, {GpuFormat::R32Float, {}}, {GpuFormat::R32Float, {}}, {GpuFormat::Rgba8, {}}});
			for (int layer = 0; layer < kTranslucentLayers; ++layer) {
				const int layerDepth = layer % 2 == 0 ? LayerDepthA : LayerDepthB;
				const int previousDepth = layer % 2 == 0 ? LayerDepthB : LayerDepthA;
				GpuPass peel;
				peel.colors = {LayerColor, layer == 0 ? Ids : -1, layerDepth, -1};
				peel.colorLoad = {GpuLoad::Clear, GpuLoad::Load, GpuLoad::Clear, GpuLoad::Clear};
				peel.depth = Depth;
				for (const Run& run : std::as_const(runs)) {
					if (run.texture >= 0 && run.alpha) {
						peel.draws.append(surfaceDraw(run, layer == 0 ? 1 : 2, OpaqueDepth, layer == 0 ? -1 : previousDepth));
					}
				}
				GpuDraw under;
				under.program = int(GpuProgram::Composite);
				under.count = 3;
				under.state = underState();
				under.uniforms = compositeUniforms();
				under.textures[0] = GpuTextureRef::target(LayerColor);
				GpuPass accumulate;
				accumulate.colors = {Accumulated, -1, -1, -1};
				accumulate.colorLoad[0] = layer == 0 ? GpuLoad::Clear : GpuLoad::Load;
				accumulate.draws = {under};
				frame.passes.append(peel);
				frame.passes.append(accumulate);
			}
			GpuDraw opaqueUnder;
			opaqueUnder.program = int(GpuProgram::Composite);
			opaqueUnder.count = 3;
			opaqueUnder.state = underState();
			opaqueUnder.uniforms = compositeUniforms();
			opaqueUnder.textures[0] = GpuTextureRef::target(Color);
			GpuPass finish;
			finish.colors = {Accumulated, -1, -1, -1};
			finish.colorLoad[0] = GpuLoad::Load;
			finish.draws = {opaqueUnder};
			frame.passes.append(finish);
			colorResult = Accumulated;
		}
	}
	frame.readbacks = {colorResult, Ids};
	const GpuFrameResult result = device->render(frame, &work.cancelled);
	if (result.cancelled || work.cancelled.load()) {
		return;
	}
	if (!result.success) {
		work.error = result.error;
		work.errorDetail = result.errorDetail;
		return;
	}
	const GpuReadback* color = result.readback(colorResult);
	const GpuReadback* ids = result.readback(Ids);
	if (!color || !ids) {
		work.error = QCoreApplication::translate("VibeStudioRendering", "The graphics device did not return this view's image.");
		return;
	}
	work.result.image = color->image(QImage::Format_ARGB32_Premultiplied);
	work.result.source = ids->integers();
	work.result.pixelRatio = work.pixelRatio;
	work.success = !work.result.image.isNull() && work.result.source.size() == qsizetype(work.size.width()) * work.size.height();
}

QPolygonF ModelViewport::triangleOutline(int triangle) const
{
	QPolygonF outline;
	if (triangle < 0 || triangle >= m_meshTriangles.size()) {
		return outline;
	}
	const MeshTriangle& source = m_meshTriangles.at(triangle);
	if (source.surface < 0 || source.surface >= m_mesh.surfaces.size()) {
		return outline;
	}
	const auto& part = m_mesh.surfaces.at(source.surface);
	if (part.frames.isEmpty()) {
		return outline;
	}
	const int vertexCount = part.frames[std::min(m_frame, int(part.frames.size()) - 1)].positions.size();
	if (source.a >= vertexCount || source.b >= vertexCount || source.c >= vertexCount) {
		return outline;
	}
	const std::array<ModelVec3, 3> corners {editVertexPosition(source.surface, source.a), editVertexPosition(source.surface, source.b),
		editVertexPosition(source.surface, source.c)};
	if (!m_perspective) {
		for (const ModelVec3& corner : corners) {
			outline << projectPoint(m_camera, corner, nullptr);
		}
		return outline;
	}
	// The part in front of the near plane, as the camera sees it.
	for (int corner = 0; corner < 3; ++corner) {
		const int next = (corner + 1) % 3;
		double x[2], y[2], z[2];
		toView(corners[size_t(corner)], &x[0], &y[0], &z[0]);
		toView(corners[size_t(next)], &x[1], &y[1], &z[1]);
		const bool here = z[0] >= kNearPlane;
		const bool there = z[1] >= kNearPlane;
		if (here) {
			outline << fromView(x[0], y[0], z[0]);
		}
		if (here != there) {
			const double t = (kNearPlane - z[0]) / (z[1] - z[0]);
			outline << fromView(x[0] + t * (x[1] - x[0]), y[0] + t * (y[1] - y[0]), kNearPlane);
		}
	}
	return outline.size() >= 3 ? outline : QPolygonF();
}

} // namespace vibestudio
