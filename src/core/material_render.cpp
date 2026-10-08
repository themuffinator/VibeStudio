#include "core/material_render.h"
#include "core/material_render_p.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QVarLengthArray>

#include <algorithm>
#include <cmath>

namespace vibestudio {
namespace material_render {
namespace {

struct Text {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioMaterials)
};

constexpr double kPi = 3.14159265358979323846;

int wrapCoordinate(int value, int size)
{
	const int m = value % size;
	return m < 0 ? m + size : m;
}

} // namespace

double fract(double value)
{
	return value - std::floor(value);
}

CullMode cullModeFor(MaterialCull cull)
{
	switch (cull) {
	case MaterialCull::Front:
		return CullMode::Front;
	case MaterialCull::Back:
		return CullMode::Back;
	case MaterialCull::None:
		return CullMode::None;
	}
	return CullMode::Front;
}

// ---------------------------------------------------------------------------
// Meshes
// ---------------------------------------------------------------------------

namespace {

struct FaceAxes {
	Vec3 s;
	Vec3 t;
};

// Quake's axial texture projection (textureAxisFromPlane's baseaxis): the
// axes follow the closest world axis to the face normal.
FaceAxes axialAxes(const Vec3& normal)
{
	const double ax = std::abs(normal.x);
	const double ay = std::abs(normal.y);
	const double az = std::abs(normal.z);
	if (az >= ax && az >= ay) {
		return {{1, 0, 0}, {0, -1, 0}};
	}
	if (ax >= ay) {
		return {{0, 1, 0}, {0, 0, -1}};
	}
	return {{1, 0, 0}, {0, 0, -1}};
}

void addGrid(Mesh* mesh, const Vec3& origin, const Vec3& uAxis, const Vec3& vAxis, double uLength, double vLength, const Vec3& normal, int face,
	double segmentSize)
{
	const int columns = std::clamp(static_cast<int>(std::ceil(uLength / segmentSize)), 1, 48);
	const int rows = std::clamp(static_cast<int>(std::ceil(vLength / segmentSize)), 1, 48);
	const FaceAxes axes = axialAxes(normal);
	const int first = static_cast<int>(mesh->vertices.size());
	for (int row = 0; row <= rows; ++row) {
		for (int column = 0; column <= columns; ++column) {
			const double u = uLength * column / columns;
			const double v = vLength * row / rows;
			Vertex vertex;
			vertex.position = origin + uAxis * u + vAxis * v;
			vertex.normal = normal;
			vertex.tangent = axes.s;
			vertex.bitangent = axes.t;
			vertex.s = vertex.position.dot(axes.s) / mesh->repeatS;
			vertex.t = vertex.position.dot(axes.t) / mesh->repeatT;
			vertex.ls = static_cast<double>(column) / columns;
			vertex.lt = static_cast<double>(row) / rows;
			vertex.face = face;
			mesh->vertices.push_back(vertex);
		}
	}
	const bool counterClockwise = uAxis.cross(vAxis).dot(normal) > 0.0;
	for (int row = 0; row < rows; ++row) {
		for (int column = 0; column < columns; ++column) {
			const int i00 = first + row * (columns + 1) + column;
			const int i10 = i00 + 1;
			const int i01 = i00 + columns + 1;
			const int i11 = i01 + 1;
			if (counterClockwise) {
				mesh->triangles.push_back({i00, i10, i11});
				mesh->triangles.push_back({i00, i11, i01});
			} else {
				mesh->triangles.push_back({i00, i11, i10});
				mesh->triangles.push_back({i00, i01, i11});
			}
		}
	}
}

void addBox(Mesh* mesh, double half, bool inside, double segmentSize)
{
	struct Face {
		Vec3 normal;
		Vec3 u;
		Vec3 v;
	};
	const Face faces[6] = {
		{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
		{{-1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
		{{0, 1, 0}, {1, 0, 0}, {0, 0, 1}},
		{{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
		{{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
		{{0, 0, -1}, {1, 0, 0}, {0, 1, 0}},
	};
	for (int index = 0; index < 6; ++index) {
		const Face& face = faces[index];
		const Vec3 normal = inside ? -face.normal : face.normal;
		const Vec3 origin = face.normal * half - face.u * half - face.v * half;
		addGrid(mesh, origin, face.u, face.v, half * 2, half * 2, normal, index, segmentSize);
	}
}

} // namespace

Mesh buildMesh(MaterialPreviewShape shape, double repeatS, double repeatT, double tiling, double segmentSize)
{
	Mesh mesh;
	mesh.shape = shape;
	mesh.repeatS = std::max(1.0, repeatS);
	mesh.repeatT = std::max(1.0, repeatT);
	tiling = std::clamp(tiling, 0.25, 64.0);
	segmentSize = std::max(4.0, segmentSize);
	const double width = mesh.repeatS * tiling;
	const double height = mesh.repeatT * tiling;
	switch (shape) {
	case MaterialPreviewShape::Wall:
		// Corner at the world origin, like a brush face on the grid, so one
		// repeat shows the whole image upright.
		addGrid(&mesh, Vec3 {0, 0, 0}, Vec3 {0, 1, 0}, Vec3 {0, 0, 1}, width, height, Vec3 {1, 0, 0}, 0, segmentSize);
		mesh.center = Vec3 {0, width / 2, height / 2};
		mesh.radius = std::sqrt(width * width + height * height) / 2;
		break;
	case MaterialPreviewShape::Floor:
		addGrid(&mesh, Vec3 {0, 0, 0}, Vec3 {1, 0, 0}, Vec3 {0, 1, 0}, width, width, Vec3 {0, 0, 1}, 0, segmentSize);
		mesh.center = Vec3 {width / 2, width / 2, 0};
		mesh.radius = width * 0.72;
		break;
	case MaterialPreviewShape::Cube:
		addBox(&mesh, width / 2, false, segmentSize);
		mesh.radius = width * 0.87;
		break;
	case MaterialPreviewShape::Room:
		addBox(&mesh, width, true, segmentSize);
		mesh.inside = true;
		mesh.radius = width;
		break;
	case MaterialPreviewShape::Sphere:
	case MaterialPreviewShape::Cylinder: {
		const double radius = width / 2;
		const int around = 48;
		const int rings = shape == MaterialPreviewShape::Sphere ? 24 : std::clamp(static_cast<int>(height / segmentSize), 1, 24);
		const double circumferenceRepeats = std::max(1.0, std::round(2 * kPi * radius / mesh.repeatS));
		const int first = static_cast<int>(mesh.vertices.size());
		for (int ring = 0; ring <= rings; ++ring) {
			for (int step = 0; step <= around; ++step) {
				const double angle = 2 * kPi * step / around;
				Vertex vertex;
				if (shape == MaterialPreviewShape::Sphere) {
					const double latitude = kPi / 2 - kPi * ring / rings;
					vertex.normal = Vec3 {std::cos(latitude) * std::cos(angle), std::cos(latitude) * std::sin(angle), std::sin(latitude)};
					vertex.position = vertex.normal * radius;
					vertex.t = static_cast<double>(ring) / rings * circumferenceRepeats / 2;
				} else {
					vertex.normal = Vec3 {std::cos(angle), std::sin(angle), 0};
					vertex.position = Vec3 {std::cos(angle) * radius, std::sin(angle) * radius, height / 2 - height * ring / rings};
					vertex.t = (height / 2 - vertex.position.z) / mesh.repeatT;
				}
				vertex.s = static_cast<double>(step) / around * circumferenceRepeats;
				vertex.tangent = Vec3 {-std::sin(angle), std::cos(angle), 0};
				vertex.bitangent = vertex.normal.cross(vertex.tangent).normalized() * -1.0;
				if (vertex.bitangent.length() < 0.5) {
					vertex.bitangent = Vec3 {0, 0, -1};
				}
				vertex.ls = static_cast<double>(step) / around;
				vertex.lt = static_cast<double>(ring) / rings;
				mesh.vertices.push_back(vertex);
			}
		}
		for (int ring = 0; ring < rings; ++ring) {
			for (int step = 0; step < around; ++step) {
				const int i00 = first + ring * (around + 1) + step;
				const int i10 = i00 + 1;
				const int i01 = i00 + around + 1;
				const int i11 = i01 + 1;
				// Rings run top to bottom and steps counter-clockwise seen
				// from above, so (i00, i01, i11) faces outward.
				mesh.triangles.push_back({i00, i01, i11});
				mesh.triangles.push_back({i00, i11, i10});
			}
		}
		if (shape == MaterialPreviewShape::Cylinder) {
			for (int cap = 0; cap < 2; ++cap) {
				const double z = cap == 0 ? height / 2 : -height / 2;
				const Vec3 normal {0, 0, cap == 0 ? 1.0 : -1.0};
				const int center = static_cast<int>(mesh.vertices.size());
				Vertex middle;
				middle.position = Vec3 {0, 0, z};
				middle.normal = normal;
				middle.tangent = Vec3 {1, 0, 0};
				middle.bitangent = Vec3 {0, -1, 0};
				middle.ls = middle.lt = 0.5;
				mesh.vertices.push_back(middle);
				for (int step = 0; step <= around; ++step) {
					const double angle = 2 * kPi * step / around;
					Vertex vertex = middle;
					vertex.position = Vec3 {std::cos(angle) * radius, std::sin(angle) * radius, z};
					vertex.s = vertex.position.x / mesh.repeatS;
					vertex.t = -vertex.position.y / mesh.repeatT;
					vertex.ls = 0.5 + 0.5 * std::cos(angle);
					vertex.lt = 0.5 + 0.5 * std::sin(angle);
					mesh.vertices.push_back(vertex);
				}
				for (int step = 0; step < around; ++step) {
					const int a = center + 1 + step;
					const int b = a + 1;
					if (cap == 0) {
						mesh.triangles.push_back({center, a, b});
					} else {
						mesh.triangles.push_back({center, b, a});
					}
				}
				mesh.vertices[center].s = 0;
				mesh.vertices[center].t = 0;
			}
		}
		mesh.radius = shape == MaterialPreviewShape::Sphere ? radius : std::sqrt(radius * radius + height * height / 4);
		break;
	}
	}
	return mesh;
}

// ---------------------------------------------------------------------------
// Camera and framebuffer
// ---------------------------------------------------------------------------

void Camera::project(const Vec3& point, double* sx, double* sy) const
{
	const Vec3 d = point - eye;
	const double x = d.dot(right);
	const double y = d.dot(up);
	if (orthographic) {
		*sx = centerX + x * orthoScale;
		*sy = centerY - y * orthoScale;
		return;
	}
	const double z = std::max(1.0e-6, d.dot(forward));
	*sx = centerX + x * focal / z;
	*sy = centerY - y * focal / z;
}

Vec3 Camera::ray(double px, double py) const
{
	if (orthographic) {
		return forward;
	}
	return (forward + right * ((px - centerX) / focal) + up * (-(py - centerY) / focal)).normalized();
}

Camera makeCamera(const Mesh& mesh, const MaterialRenderOptions& options)
{
	Camera camera;
	camera.width = std::max(1, options.size.width());
	camera.height = std::max(1, options.size.height());
	camera.centerX = camera.width / 2.0;
	camera.centerY = camera.height / 2.0;
	const double yaw = options.yaw * kPi / 180.0;
	const double pitch = std::clamp(options.pitch, -89.0, 89.0) * kPi / 180.0;
	const Vec3 worldUp {0, 0, 1};
	const double zoom = std::clamp(options.zoom, 0.05, 40.0);
	if (options.orthographic) {
		camera.orthographic = true;
		camera.forward = Vec3 {-1, 0, 0};
		if (options.shape == MaterialPreviewShape::Floor) {
			camera.forward = Vec3 {0, 0, -1};
		}
		camera.eye = mesh.center - camera.forward * (mesh.radius * 4 + 16);
		const Vec3 upHint = std::abs(camera.forward.z) > 0.9 ? Vec3 {0, 1, 0} : worldUp;
		camera.right = camera.forward.cross(upHint).normalized();
		camera.up = camera.right.cross(camera.forward).normalized();
		const double extent = mesh.radius * std::sqrt(2.0);
		camera.orthoScale = std::min(camera.width, camera.height) / std::max(1.0, extent) * zoom;
		camera.nearPlane = 0.5;
		return camera;
	}
	const Vec3 orbit {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
	if (mesh.inside) {
		camera.eye = mesh.center;
		camera.forward = -orbit;
	} else {
		const double fov = std::clamp(options.fieldOfView, 20.0, 120.0) * kPi / 180.0;
		const double distance = mesh.radius / std::sin(fov / 2) * 1.05 / zoom;
		camera.eye = mesh.center + orbit * std::max(distance, mesh.radius * 0.2 + 2.0);
		camera.forward = (mesh.center - camera.eye).normalized();
	}
	const Vec3 upHint = std::abs(camera.forward.z) > 0.999 ? Vec3 {1, 0, 0} : worldUp;
	camera.right = camera.forward.cross(upHint).normalized();
	camera.up = camera.right.cross(camera.forward).normalized();
	const double fov = std::clamp(options.fieldOfView, 20.0, 120.0) * kPi / 180.0;
	camera.focal = (camera.height / 2.0) / std::tan(fov / 2);
	camera.nearPlane = 1.0;
	return camera;
}

void Framebuffer::reset(int w, int h)
{
	width = std::max(1, w);
	height = std::max(1, h);
	color.fill(0.0f, static_cast<qsizetype>(width) * height * 4);
	depth.fill(std::numeric_limits<float>::infinity(), static_cast<qsizetype>(width) * height);
}

void Framebuffer::clear(const QColor& background, bool checker, double scale)
{
	const double r = background.redF() * scale;
	const double g = background.greenF() * scale;
	const double b = background.blueF() * scale;
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			float* p = pixel(x, y);
			const double shade = checker && ((x / 12 + y / 12) % 2 == 0) ? 1.18 : 1.0;
			p[0] = static_cast<float>(std::min(1.0, r * shade));
			p[1] = static_cast<float>(std::min(1.0, g * shade));
			p[2] = static_cast<float>(std::min(1.0, b * shade));
			p[3] = 0.0f;
		}
	}
	std::fill(depth.begin(), depth.end(), std::numeric_limits<float>::infinity());
}

QImage Framebuffer::toImage(double displayScale) const
{
	QImage image(width, height, QImage::Format_ARGB32);
	for (int y = 0; y < height; ++y) {
		auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
		for (int x = 0; x < width; ++x) {
			const float* p = pixel(x, y);
			const auto channel = [&](float value) { return static_cast<int>(std::clamp(value * displayScale, 0.0, 1.0) * 255.0 + 0.5); };
			line[x] = qRgb(channel(p[0]), channel(p[1]), channel(p[2]));
		}
	}
	return image;
}

// ---------------------------------------------------------------------------
// Rasterizer
// ---------------------------------------------------------------------------

namespace {

struct ClipVertex {
	Vec3 position;
	double depth = 0.0;
	Attributes attributes {};
};

struct ScreenVertex {
	double x = 0.0;
	double y = 0.0;
	double depth = 0.0;
	double w = 1.0;
	const Attributes* attributes = nullptr;
};

double edgeFunction(const ScreenVertex& a, const ScreenVertex& b, double px, double py)
{
	return (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x);
}

// Exactly one of two triangles sharing an edge owns pixels on it.
bool ownsEdge(const ScreenVertex& a, const ScreenVertex& b)
{
	const double dy = b.y - a.y;
	const double dx = b.x - a.x;
	return dy > 0.0 || (dy == 0.0 && dx > 0.0);
}

void rasterizeProjected(const Camera& camera, ScreenVertex v0, ScreenVertex v1, ScreenVertex v2, int count, bool frontFacing,
	const std::function<void(const FragmentInput&)>& fragment)
{
	double area = edgeFunction(v0, v1, v2.x, v2.y);
	if (std::abs(area) < 1.0e-12) {
		return;
	}
	if (area < 0.0) {
		std::swap(v1, v2);
		area = -area;
	}
	const int minX = std::max(0, static_cast<int>(std::floor(std::min({v0.x, v1.x, v2.x}))));
	const int maxX = std::min(camera.width - 1, static_cast<int>(std::ceil(std::max({v0.x, v1.x, v2.x}))));
	const int minY = std::max(0, static_cast<int>(std::floor(std::min({v0.y, v1.y, v2.y}))));
	const int maxY = std::min(camera.height - 1, static_cast<int>(std::ceil(std::max({v0.y, v1.y, v2.y}))));
	if (minX > maxX || minY > maxY) {
		return;
	}
	const bool own0 = ownsEdge(v1, v2);
	const bool own1 = ownsEdge(v2, v0);
	const bool own2 = ownsEdge(v0, v1);
	Attributes interpolated {};
	FragmentInput input;
	input.frontFacing = frontFacing;
	input.attributes = &interpolated;
	for (int y = minY; y <= maxY; ++y) {
		const double py = y + 0.5;
		for (int x = minX; x <= maxX; ++x) {
			const double px = x + 0.5;
			const double w0 = edgeFunction(v1, v2, px, py);
			const double w1 = edgeFunction(v2, v0, px, py);
			const double w2 = edgeFunction(v0, v1, px, py);
			if (w0 < 0.0 || w1 < 0.0 || w2 < 0.0) {
				continue;
			}
			if ((w0 == 0.0 && !own0) || (w1 == 0.0 && !own1) || (w2 == 0.0 && !own2)) {
				continue;
			}
			const double l0 = w0 / area;
			const double l1 = w1 / area;
			const double l2 = w2 / area;
			const double p0 = l0 * v0.w;
			const double p1 = l1 * v1.w;
			const double p2 = l2 * v2.w;
			const double sum = p0 + p1 + p2;
			if (sum <= 0.0) {
				continue;
			}
			const double inverse = 1.0 / sum;
			for (int k = 0; k < count; ++k) {
				interpolated[static_cast<size_t>(k)] = ((*v0.attributes)[static_cast<size_t>(k)] * p0 + (*v1.attributes)[static_cast<size_t>(k)] * p1
														   + (*v2.attributes)[static_cast<size_t>(k)] * p2)
					* inverse;
			}
			input.x = x;
			input.y = y;
			input.depth = camera.orthographic ? l0 * v0.depth + l1 * v1.depth + l2 * v2.depth : inverse;
			fragment(input);
		}
	}
}

} // namespace

void rasterizeTriangle(const Camera& camera, const std::array<Vec3, 3>& positions, const std::array<Attributes, 3>& attributes, int count,
	CullMode cull, const std::function<void(const FragmentInput&)>& fragment)
{
	const Vec3 normal = (positions[1] - positions[0]).cross(positions[2] - positions[0]);
	const bool front = camera.orthographic ? normal.dot(-camera.forward) > 0.0 : normal.dot(camera.eye - positions[0]) > 0.0;
	if ((cull == CullMode::Front && !front) || (cull == CullMode::Back && front)) {
		return;
	}
	count = std::clamp(count, 0, kMaxAttributes);
	QVarLengthArray<ClipVertex, 8> polygon;
	for (int index = 0; index < 3; ++index) {
		ClipVertex vertex;
		vertex.position = positions[static_cast<size_t>(index)];
		vertex.depth = camera.depthOf(vertex.position);
		vertex.attributes = attributes[static_cast<size_t>(index)];
		polygon.push_back(vertex);
	}
	// Clip against the near plane (Sutherland-Hodgman on one plane).
	const double nearPlane = camera.nearPlane;
	bool needsClip = false;
	bool anyVisible = false;
	for (const ClipVertex& vertex : polygon) {
		needsClip |= vertex.depth < nearPlane;
		anyVisible |= vertex.depth >= nearPlane;
	}
	if (!anyVisible) {
		return;
	}
	if (needsClip) {
		QVarLengthArray<ClipVertex, 8> clipped;
		for (int index = 0; index < polygon.size(); ++index) {
			const ClipVertex& current = polygon[index];
			const ClipVertex& next = polygon[(index + 1) % polygon.size()];
			const bool currentIn = current.depth >= nearPlane;
			const bool nextIn = next.depth >= nearPlane;
			if (currentIn) {
				clipped.push_back(current);
			}
			if (currentIn != nextIn) {
				const double t = (nearPlane - current.depth) / (next.depth - current.depth);
				ClipVertex middle;
				middle.position = current.position + (next.position - current.position) * t;
				middle.depth = nearPlane;
				for (int k = 0; k < count; ++k) {
					middle.attributes[static_cast<size_t>(k)] = current.attributes[static_cast<size_t>(k)]
						+ (next.attributes[static_cast<size_t>(k)] - current.attributes[static_cast<size_t>(k)]) * t;
				}
				clipped.push_back(middle);
			}
		}
		polygon = clipped;
	}
	if (polygon.size() < 3) {
		return;
	}
	QVarLengthArray<ScreenVertex, 8> screen;
	for (const ClipVertex& vertex : polygon) {
		ScreenVertex projected;
		camera.project(vertex.position, &projected.x, &projected.y);
		projected.depth = vertex.depth;
		projected.w = camera.orthographic ? 1.0 : 1.0 / std::max(1.0e-6, vertex.depth);
		projected.attributes = &vertex.attributes;
		screen.push_back(projected);
	}
	for (int index = 1; index + 1 < screen.size(); ++index) {
		rasterizeProjected(camera, screen[0], screen[index], screen[index + 1], count, front, fragment);
	}
}

// ---------------------------------------------------------------------------
// Sampling
// ---------------------------------------------------------------------------

Color sampleTexture(const MaterialTexture& texture, double s, double t, bool bilinear, Wrap wrap, double lod)
{
	if (!texture.isValid()) {
		return {1.0, 0.0, 1.0, 1.0};
	}
	const QVector<QRgb>* pixels = &texture.pixels;
	int width = texture.width;
	int height = texture.height;
	if (lod >= 0.5 && !texture.mipPixels.isEmpty()) {
		const int level = std::min(static_cast<int>(texture.mipPixels.size()), static_cast<int>(std::lround(lod)));
		if (level > 0) {
			pixels = &texture.mipPixels.at(level - 1);
			width = texture.mipSizes.at(level - 1).width();
			height = texture.mipSizes.at(level - 1).height();
		}
	}
	if (!std::isfinite(s) || !std::isfinite(t)) {
		s = 0.0;
		t = 0.0;
	}
	if (std::abs(s) > 1.0e6 || std::abs(t) > 1.0e6) {
		s = fract(s);
		t = fract(t);
	}
	const auto texel = [&](int x, int y) -> Color {
		switch (wrap) {
		case Wrap::Repeat:
			x = wrapCoordinate(x, width);
			y = wrapCoordinate(y, height);
			break;
		case Wrap::Clamp:
			x = std::clamp(x, 0, width - 1);
			y = std::clamp(y, 0, height - 1);
			break;
		case Wrap::ZeroClamp:
			if (x < 0 || y < 0 || x >= width || y >= height) {
				return {0.0, 0.0, 0.0, 0.0};
			}
			break;
		case Wrap::AlphaZeroClamp: {
			const bool outside = x < 0 || y < 0 || x >= width || y >= height;
			x = std::clamp(x, 0, width - 1);
			y = std::clamp(y, 0, height - 1);
			if (outside) {
				Color c = Color::fromRgb(pixels->at(static_cast<qsizetype>(y) * width + x));
				c.a = 0.0;
				return c;
			}
			break;
		}
		}
		return Color::fromRgb(pixels->at(static_cast<qsizetype>(y) * width + x));
	};
	if (!bilinear) {
		return texel(static_cast<int>(std::floor(s * width)), static_cast<int>(std::floor(t * height)));
	}
	const double u = s * width - 0.5;
	const double v = t * height - 0.5;
	const int x0 = static_cast<int>(std::floor(u));
	const int y0 = static_cast<int>(std::floor(v));
	const double fx = u - x0;
	const double fy = v - y0;
	const Color c00 = texel(x0, y0);
	const Color c10 = texel(x0 + 1, y0);
	const Color c01 = texel(x0, y0 + 1);
	const Color c11 = texel(x0 + 1, y0 + 1);
	return (c00 * ((1 - fx) * (1 - fy))) + (c10 * (fx * (1 - fy))) + (c01 * ((1 - fx) * fy)) + (c11 * (fx * fy));
}

int sampleIndex(const MaterialTexture& texture, double s, double t, Wrap wrap)
{
	if (!texture.hasIndices()) {
		return -1;
	}
	if (!std::isfinite(s) || !std::isfinite(t)) {
		return -1;
	}
	int x = static_cast<int>(std::floor(s * texture.width));
	int y = static_cast<int>(std::floor(t * texture.height));
	if (wrap == Wrap::Repeat) {
		x = wrapCoordinate(x, texture.width);
		y = wrapCoordinate(y, texture.height);
	} else {
		if (wrap == Wrap::ZeroClamp && (x < 0 || y < 0 || x >= texture.width || y >= texture.height)) {
			return -1;
		}
		x = std::clamp(x, 0, texture.width - 1);
		y = std::clamp(y, 0, texture.height - 1);
	}
	const qsizetype at = static_cast<qsizetype>(y) * texture.width + x;
	if (qAlpha(texture.pixels.at(at)) == 0) {
		return -1;
	}
	return texture.indices.at(at);
}

namespace {

double component(const Vec3& v, int selector)
{
	const int axis = std::abs(selector) - 1;
	const double value = axis == 0 ? v.x : axis == 1 ? v.y : v.z;
	return selector < 0 ? -value : value;
}

} // namespace

Color sampleSkyBox(const MaterialCubeTexture& cube, const Vec3& direction)
{
	// tr_sky.c vec_to_st: per face, s and t as signed components divided
	// by the major axis; images rt bk lf ft up dn are +X +Y -X -Y +Z -Z.
	static const int vecToSt[6][3] = {{-2, 3, 1}, {2, 3, -1}, {1, 3, 2}, {-1, 3, -2}, {-2, -1, 3}, {-2, 1, -3}};
	const double ax = std::abs(direction.x);
	const double ay = std::abs(direction.y);
	const double az = std::abs(direction.z);
	int axis = 0;
	if (ax >= ay && ax >= az) {
		axis = direction.x < 0 ? 1 : 0;
	} else if (ay >= az) {
		axis = direction.y < 0 ? 3 : 2;
	} else {
		axis = direction.z < 0 ? 5 : 4;
	}
	const double dv = component(direction, vecToSt[axis][2]);
	if (dv <= 1.0e-9) {
		return {0, 0, 0, 1};
	}
	const double s = component(direction, vecToSt[axis][0]) / dv;
	const double t = component(direction, vecToSt[axis][1]) / dv;
	const MaterialTexturePtr& face = cube.faces[static_cast<size_t>(axis)];
	if (!face || !face->usable()) {
		return {0, 0, 0, 1};
	}
	const double u = std::clamp((s + 1) / 2, 0.0, 1.0);
	const double v = std::clamp(1.0 - (t + 1) / 2, 0.0, 1.0);
	return sampleTexture(*face, u, v, true, Wrap::Clamp);
}

Color sampleCubeMap(const MaterialCubeTexture& cube, const Vec3& direction)
{
	// The GL cube map face rules (+X, -X, +Y, -Y, +Z, -Z), with the world
	// direction used as given, as Doom 3's texgens pass it.
	const double ax = std::abs(direction.x);
	const double ay = std::abs(direction.y);
	const double az = std::abs(direction.z);
	int face = 0;
	double sc = 0;
	double tc = 0;
	double ma = 1;
	if (ax >= ay && ax >= az) {
		face = direction.x >= 0 ? 0 : 1;
		ma = ax;
		sc = direction.x >= 0 ? -direction.z : direction.z;
		tc = -direction.y;
	} else if (ay >= az) {
		face = direction.y >= 0 ? 2 : 3;
		ma = ay;
		sc = direction.x;
		tc = direction.y >= 0 ? direction.z : -direction.z;
	} else {
		face = direction.z >= 0 ? 4 : 5;
		ma = az;
		sc = direction.z >= 0 ? direction.x : -direction.x;
		tc = -direction.y;
	}
	const MaterialTexturePtr& texture = cube.faces[static_cast<size_t>(face)];
	if (!texture || !texture->usable() || ma <= 0.0) {
		return {0, 0, 0, 1};
	}
	return sampleTexture(*texture, (sc / ma + 1) / 2, (tc / ma + 1) / 2, true, Wrap::Clamp);
}

void blendInto(float* destination, const Color& source, MaterialBlendFactor sourceFactor, MaterialBlendFactor destinationFactor, bool maskRed,
	bool maskGreen, bool maskBlue, bool maskAlpha)
{
	const Color dst {destination[0], destination[1], destination[2], destination[3]};
	const auto factor = [&](MaterialBlendFactor f) -> Color {
		switch (f) {
		case MaterialBlendFactor::Zero:
			return {0, 0, 0, 0};
		case MaterialBlendFactor::One:
			return {1, 1, 1, 1};
		case MaterialBlendFactor::SourceColor:
			return source;
		case MaterialBlendFactor::OneMinusSourceColor:
			return {1 - source.r, 1 - source.g, 1 - source.b, 1 - source.a};
		case MaterialBlendFactor::DestinationColor:
			return dst;
		case MaterialBlendFactor::OneMinusDestinationColor:
			return {1 - dst.r, 1 - dst.g, 1 - dst.b, 1 - dst.a};
		case MaterialBlendFactor::SourceAlpha:
			return {source.a, source.a, source.a, source.a};
		case MaterialBlendFactor::OneMinusSourceAlpha:
			return {1 - source.a, 1 - source.a, 1 - source.a, 1 - source.a};
		case MaterialBlendFactor::DestinationAlpha:
			return {dst.a, dst.a, dst.a, dst.a};
		case MaterialBlendFactor::OneMinusDestinationAlpha:
			return {1 - dst.a, 1 - dst.a, 1 - dst.a, 1 - dst.a};
		case MaterialBlendFactor::SourceAlphaSaturate: {
			const double f2 = std::min(source.a, 1 - dst.a);
			return {f2, f2, f2, 1};
		}
		}
		return {1, 1, 1, 1};
	};
	const Color result = (source * factor(sourceFactor) + dst * factor(destinationFactor)).clamped();
	if (!maskRed) {
		destination[0] = static_cast<float>(result.r);
	}
	if (!maskGreen) {
		destination[1] = static_cast<float>(result.g);
	}
	if (!maskBlue) {
		destination[2] = static_cast<float>(result.b);
	}
	if (!maskAlpha) {
		destination[3] = static_cast<float>(result.a);
	}
}

Vec3 previewLightPosition(const Mesh& mesh, const MaterialPreviewLighting& lighting, double time)
{
	const double angle = lighting.lightOrbit ? 2 * kPi * time / std::max(0.5, lighting.lightOrbitSeconds) : lighting.lightAngle * kPi / 180.0;
	const double r = mesh.radius;
	if (mesh.inside) {
		return mesh.center + Vec3 {std::cos(angle) * r * 0.55, std::sin(angle) * r * 0.55, r * 0.35};
	}
	// In front of a wall (+X) or circling other shapes above their middle.
	if (mesh.shape == MaterialPreviewShape::Wall) {
		return mesh.center + Vec3 {r * 0.8, std::sin(angle) * r * 0.75, std::cos(angle) * r * 0.45};
	}
	if (mesh.shape == MaterialPreviewShape::Floor) {
		return mesh.center + Vec3 {std::cos(angle) * r * 0.6, std::sin(angle) * r * 0.6, r * 0.55};
	}
	return mesh.center + Vec3 {std::cos(angle) * r * 1.5, std::sin(angle) * r * 1.5, r * 0.7};
}

double previewLightAt(const Vec3& position, const Vec3& normal, const Mesh& mesh, const MaterialPreviewLighting& lighting, double time)
{
	const double level = std::max(0.0, lighting.lightmap);
	if (!lighting.lightmapSpot) {
		return level;
	}
	const Vec3 light = previewLightPosition(mesh, lighting, time);
	const Vec3 toLight = light - position;
	const double distance = toLight.length();
	const double facing = std::max(0.0, normal.dot(toLight * (1.0 / std::max(1.0e-6, distance))));
	const double falloff = std::clamp(1.0 - distance / (mesh.radius * 2.4), 0.0, 1.0);
	return level * (0.42 + 0.95 * facing * falloff);
}

double mipLevel(const Camera& camera, double depth, double obliquity, double texelsPerUnit)
{
	const double unitsPerPixel = camera.orthographic ? 1.0 / std::max(1.0e-6, camera.orthoScale) : depth / std::max(1.0e-6, camera.focal);
	const double texelsPerPixel = unitsPerPixel * texelsPerUnit / std::clamp(obliquity, 0.2, 1.0);
	return texelsPerPixel <= 1.0 ? 0.0 : std::log2(texelsPerPixel);
}

QSize mainImageSize(const MaterialDefinition& definition, const MaterialImageSet& images)
{
	const auto sizeOf = [&](const QString& reference) -> QSize {
		const MaterialTexturePtr texture = images.find(reference);
		return texture && texture->usable() ? QSize(texture->width, texture->height) : QSize();
	};
	if (!definition.classic.kind.isEmpty()) {
		const QSize size = sizeOf(definition.name);
		if (size.isValid()) {
			return size;
		}
		if (definition.classic.size.isValid()) {
			return definition.classic.size;
		}
	}
	for (const MaterialStage& stage : definition.stages) {
		QString reference;
		if (stage.imageKind == MaterialImageKind::File || stage.imageKind == MaterialImageKind::Builtin) {
			reference = stage.imagePath;
		} else if (stage.imageKind == MaterialImageKind::Animation && !stage.animationFrames.isEmpty()) {
			reference = stage.animationFrames.first();
		} else if (stage.imageKind == MaterialImageKind::Program && stage.imageProgram >= 0) {
			reference = materialImageProgramText(definition.imagePrograms, stage.imageProgram);
		}
		if (stage.imageProgram >= 0 && reference.isEmpty()) {
			reference = materialImageProgramText(definition.imagePrograms, stage.imageProgram);
		}
		const QSize size = reference.isEmpty() ? QSize() : sizeOf(reference);
		if (size.isValid() && size.width() > 2) {
			return size;
		}
	}
	const QSize editor = sizeOf(definition.editorImage);
	return editor.isValid() ? editor : QSize(128, 128);
}

// ---------------------------------------------------------------------------
// Quake III
// ---------------------------------------------------------------------------

namespace {

struct Quake3Frame {
	const MaterialRenderOptions* options = nullptr;
	double time = 0.0;
	double identityLight = 0.5;
	double displayScale = 2.0;
	int shift = 1;
	Color lightColor {1, 1, 1, 1};
};

// R_ColorShiftLightingBytes: shift, then scale all channels down together
// when one overflows, keeping the hue.
Color hueClamp(Color color)
{
	const double maximum = std::max({color.r, color.g, color.b});
	if (maximum > 1.0) {
		color.r /= maximum;
		color.g /= maximum;
		color.b /= maximum;
	}
	color.a = 1.0;
	return color;
}

// The lightmap texel a map compiled with mapOverBrightBits 2 holds for a
// light level, after the engine's shift.
Color quake3Lightmap(double level, const Quake3Frame& frame)
{
	const double stored = level / 4.0 * std::pow(2.0, frame.shift);
	return hueClamp(Color {stored * frame.lightColor.r, stored * frame.lightColor.g, stored * frame.lightColor.b, 1.0});
}

double quake3Sin(int index)
{
	return quake3FunctionTable(MaterialWaveFunction::Sin, index);
}

void applyQuake3Deforms(const MaterialDefinition& definition, Mesh* mesh, const Camera& camera, double time, QStringList* notes)
{
	for (const MaterialDeform& deform : definition.deforms) {
		switch (deform.kind) {
		case MaterialDeformKind::Wave:
			for (Vertex& vertex : mesh->vertices) {
				double scale = 0.0;
				if (deform.wave.frequency == 0.0) {
					scale = evaluateQuake3Wave(deform.wave, time);
				} else {
					MaterialWave wave = deform.wave;
					wave.phase += (vertex.position.x + vertex.position.y + vertex.position.z) * deform.spread;
					scale = evaluateQuake3Wave(wave, time);
				}
				vertex.position = vertex.position + vertex.normal * scale;
			}
			break;
		case MaterialDeformKind::Normal:
			for (Vertex& vertex : mesh->vertices) {
				const Vec3 p = vertex.position * 0.98;
				const double t = time * deform.wave.frequency;
				vertex.normal = Vec3 {vertex.normal.x + deform.wave.amplitude * quake3Noise(p.x, p.y, p.z, t),
					vertex.normal.y + deform.wave.amplitude * quake3Noise(p.x + 100, p.y, p.z, t),
					vertex.normal.z + deform.wave.amplitude * quake3Noise(p.x + 200, p.y, p.z, t)}
									.normalized();
			}
			break;
		case MaterialDeformKind::Bulge: {
			const double now = time * deform.values[2];
			for (Vertex& vertex : mesh->vertices) {
				const int off = static_cast<int>(kQuake3FunctionTableSize / (2 * kPi) * (vertex.s * deform.values[0] + now));
				vertex.position = vertex.position + vertex.normal * (quake3Sin(off) * deform.values[1]);
			}
			break;
		}
		case MaterialDeformKind::Move: {
			const double scale = evaluateQuake3Wave(deform.wave, time);
			const Vec3 offset = Vec3 {deform.values[0], deform.values[1], deform.values[2]} * scale;
			for (Vertex& vertex : mesh->vertices) {
				vertex.position = vertex.position + offset;
			}
			break;
		}
		case MaterialDeformKind::Autosprite:
		case MaterialDeformKind::Autosprite2: {
			// The surface turns to face the viewer, about its centre (or its
			// vertical axis for autosprite2).
			Vec3 forward = camera.eye - mesh->center;
			if (deform.kind == MaterialDeformKind::Autosprite2) {
				forward.z = 0;
			}
			forward = forward.normalized();
			const Vec3 up = deform.kind == MaterialDeformKind::Autosprite2 || std::abs(forward.z) > 0.95 ? Vec3 {0, 0, 1} : camera.up;
			const Vec3 right = up.cross(forward).normalized() * -1.0;
			const Vec3 trueUp = forward.cross(right).normalized() * -1.0;
			for (Vertex& vertex : mesh->vertices) {
				const Vec3 local = vertex.position - mesh->center;
				vertex.position = mesh->center + right * local.y + trueUp * local.z;
				vertex.normal = forward;
			}
			break;
		}
		case MaterialDeformKind::ProjectionShadow:
		case MaterialDeformKind::Text:
			notes->push_back(Text::tr("deformVertexes %1 is not shown in the preview.").arg(materialDeformKindId(deform.kind)));
			break;
		default:
			break;
		}
	}
}

void quake3TexCoords(const MaterialStage& stage, const Vertex& vertex, const Camera& camera, const Quake3Frame& frame, double* s, double* t)
{
	switch (stage.tcGen.source) {
	case MaterialTexCoordSource::Lightmap:
		*s = vertex.ls;
		*t = vertex.lt;
		break;
	case MaterialTexCoordSource::Environment: {
		const Vec3 viewer = (camera.eye - vertex.position).normalized();
		const double d = vertex.normal.dot(viewer);
		const Vec3 reflected = vertex.normal * (2 * d) - viewer;
		*s = 0.5 + reflected.y * 0.5;
		*t = 0.5 - reflected.z * 0.5;
		break;
	}
	case MaterialTexCoordSource::Vector:
		*s = vertex.position.dot(Vec3 {stage.tcGen.vectorS[0], stage.tcGen.vectorS[1], stage.tcGen.vectorS[2]});
		*t = vertex.position.dot(Vec3 {stage.tcGen.vectorT[0], stage.tcGen.vectorT[1], stage.tcGen.vectorT[2]});
		break;
	default:
		*s = vertex.s;
		*t = vertex.t;
		break;
	}
	const double time = frame.time;
	for (const MaterialTexMod& mod : stage.tcMods) {
		switch (mod.kind) {
		case MaterialTexModKind::Turbulent: {
			const double now = mod.wave.phase + time * mod.wave.frequency;
			*s += quake3Sin(static_cast<int>(((vertex.position.x + vertex.position.z) / 1024.0 + now) * kQuake3FunctionTableSize)) * mod.wave.amplitude;
			*t += quake3Sin(static_cast<int>((vertex.position.y / 1024.0 + now) * kQuake3FunctionTableSize)) * mod.wave.amplitude;
			break;
		}
		case MaterialTexModKind::Scroll: {
			double os = mod.values[0] * time;
			double ot = mod.values[1] * time;
			os -= std::floor(os);
			ot -= std::floor(ot);
			*s += os;
			*t += ot;
			break;
		}
		case MaterialTexModKind::Scale:
			*s *= mod.values[0];
			*t *= mod.values[1];
			break;
		case MaterialTexModKind::Rotate: {
			const double degrees = -mod.values[0] * time;
			const int index = static_cast<int>(degrees * (kQuake3FunctionTableSize / 360.0));
			const double sn = quake3Sin(index);
			const double cs = quake3Sin(index + kQuake3FunctionTableSize / 4);
			const double s0 = *s;
			const double t0 = *t;
			*s = cs * s0 - sn * t0 + 0.5 - 0.5 * cs + 0.5 * sn;
			*t = sn * s0 + cs * t0 + 0.5 - 0.5 * sn - 0.5 * cs;
			break;
		}
		case MaterialTexModKind::Stretch: {
			const double wave = evaluateQuake3Wave(mod.wave, time);
			const double p = std::abs(wave) < 1.0e-9 ? 1.0e9 : 1.0 / wave;
			*s = p * *s + 0.5 - 0.5 * p;
			*t = p * *t + 0.5 - 0.5 * p;
			break;
		}
		case MaterialTexModKind::Transform: {
			const double s0 = *s;
			const double t0 = *t;
			*s = s0 * mod.values[0] + t0 * mod.values[2] + mod.values[4];
			*t = s0 * mod.values[1] + t0 * mod.values[3] + mod.values[5];
			break;
		}
		default:
			break;
		}
	}
}

Color quake3VertexLight(const Vertex& vertex, const Mesh& mesh, const Quake3Frame& frame)
{
	const MaterialRenderOptions& options = *frame.options;
	if (options.context != MaterialSurfaceContext::World) {
		return {1, 1, 1, 1};
	}
	const double level = previewLightAt(vertex.position, vertex.normal, mesh, options.lighting, frame.time);
	return quake3Lightmap(level, frame);
}

Color quake3VertexColor(const MaterialDefinition& definition, const MaterialStage& stage, const Vertex& vertex, const Mesh& mesh,
	const Camera& camera, const Quake3Frame& frame)
{
	const MaterialRenderOptions& options = *frame.options;
	const double il = frame.identityLight;
	const Color entity {options.eval.parms[0], options.eval.parms[1], options.eval.parms[2], options.eval.parms[3]};
	Color color {1, 1, 1, 1};
	switch (stage.rgbGen.source) {
	case MaterialColorSource::Identity:
	case MaterialColorSource::Skip:
		break;
	case MaterialColorSource::IdentityLighting:
		color.r = color.g = color.b = il;
		break;
	case MaterialColorSource::Constant:
		color.r = stage.rgbGen.constant[0];
		color.g = stage.rgbGen.constant[1];
		color.b = stage.rgbGen.constant[2];
		break;
	case MaterialColorSource::Wave: {
		double glow = 0.0;
		if (stage.rgbGen.wave.function == MaterialWaveFunction::Noise) {
			glow = evaluateQuake3Wave(stage.rgbGen.wave, frame.time);
		} else {
			glow = evaluateQuake3Wave(stage.rgbGen.wave, frame.time) * il;
		}
		glow = std::clamp(glow, 0.0, 1.0);
		color.r = color.g = color.b = glow;
		break;
	}
	case MaterialColorSource::Entity:
		color.r = entity.r;
		color.g = entity.g;
		color.b = entity.b;
		break;
	case MaterialColorSource::OneMinusEntity:
		color.r = 1 - entity.r;
		color.g = 1 - entity.g;
		color.b = 1 - entity.b;
		break;
	case MaterialColorSource::Vertex: {
		const Color light = quake3VertexLight(vertex, mesh, frame);
		color.r = light.r * il;
		color.g = light.g * il;
		color.b = light.b * il;
		break;
	}
	case MaterialColorSource::ExactVertex: {
		const Color light = quake3VertexLight(vertex, mesh, frame);
		color.r = light.r;
		color.g = light.g;
		color.b = light.b;
		break;
	}
	case MaterialColorSource::OneMinusVertex: {
		const Color light = quake3VertexLight(vertex, mesh, frame);
		color.r = (1 - light.r) * il;
		color.g = (1 - light.g) * il;
		color.b = (1 - light.b) * il;
		break;
	}
	case MaterialColorSource::LightingDiffuse: {
		// RB_CalcDiffuseColor: ambient + N.L * directed, per channel, 0..255.
		const Vec3 lightPosition = previewLightPosition(mesh, options.lighting, frame.time);
		const Vec3 direction = (lightPosition - vertex.position).normalized();
		const double d = vertex.normal.dot(direction);
		const double ambient = options.lighting.gridAmbient;
		const double directed = options.lighting.gridDirected;
		const double value = d <= 0 ? ambient : std::min(255.0, ambient + d * directed);
		color.r = value / 255.0 * frame.lightColor.r;
		color.g = value / 255.0 * frame.lightColor.g;
		color.b = value / 255.0 * frame.lightColor.b;
		break;
	}
	default:
		break;
	}
	switch (stage.alphaGen.source) {
	case MaterialColorSource::Constant:
		color.a = stage.alphaGen.constantAlpha;
		break;
	case MaterialColorSource::Wave:
		color.a = evaluateQuake3WaveClamped(stage.alphaGen.wave, frame.time);
		break;
	case MaterialColorSource::Entity:
		// Vanilla skips this alphaGen beside rgbGen identity or
		// lightingDiffuse (a CGEN/AGEN mix-up in ParseStage).
		color.a = (stage.rgbGen.source == MaterialColorSource::Identity || stage.rgbGen.source == MaterialColorSource::LightingDiffuse)
			? 1.0
			: entity.a;
		break;
	case MaterialColorSource::OneMinusEntity:
		color.a = 1 - entity.a;
		break;
	case MaterialColorSource::Vertex:
		color.a = 1.0;
		break;
	case MaterialColorSource::OneMinusVertex:
		color.a = 0.0;
		break;
	case MaterialColorSource::LightingSpecular: {
		// RB_CalcSpecularAlpha: a fixed light at (-960, 1980, 96).
		const Vec3 light = (Vec3 {-960, 1980, 96} - vertex.position).normalized();
		const double d = vertex.normal.dot(light);
		const Vec3 reflected = vertex.normal * (2 * d) - light;
		const Vec3 viewer = (camera.eye - vertex.position).normalized();
		const double l = reflected.dot(viewer);
		color.a = l < 0 ? 0.0 : std::min(1.0, l * l * l * l);
		break;
	}
	case MaterialColorSource::Portal: {
		const double distance = (camera.eye - vertex.position).length();
		color.a = std::clamp(distance / std::max(1.0, stage.alphaGen.portalRange), 0.0, 1.0);
		break;
	}
	default:
		color.a = 1.0;
		break;
	}
	Q_UNUSED(definition);
	return color;
}

bool alphaPasses(MaterialAlphaTest test, double alpha)
{
	switch (test) {
	case MaterialAlphaTest::None:
	case MaterialAlphaTest::Expression:
		return true;
	case MaterialAlphaTest::Greater0:
		return alpha > 0.0;
	case MaterialAlphaTest::Less128:
		return alpha < 0.5;
	case MaterialAlphaTest::GreaterEqual128:
		return alpha >= 0.5;
	}
	return true;
}

// The image a Quake III stage samples at this moment.
MaterialTexturePtr quake3StageTexture(const MaterialStage& stage, const MaterialImageSet& images, double time)
{
	switch (stage.imageKind) {
	case MaterialImageKind::File:
		return images.find(stage.imagePath);
	case MaterialImageKind::White:
		return materialBuiltinTexture(QStringLiteral("_white"));
	case MaterialImageKind::Animation: {
		if (stage.animationFrames.isEmpty()) {
			return {};
		}
		// R_BindAnimatedImage: (int)(time * fps * 1024) >> 10, at least 0.
		long long index = static_cast<long long>(time * stage.animationFrequency * kQuake3FunctionTableSize) >> 10;
		index = std::max(0LL, index) % stage.animationFrames.size();
		return images.find(stage.animationFrames.at(static_cast<int>(index)));
	}
	case MaterialImageKind::Video:
		return materialVideoPlaceholder();
	default:
		return {};
	}
}

// Whether the engine would drop this shader for a missing image.
QString quake3MissingImage(const MaterialDefinition& definition, const MaterialImageSet& images)
{
	for (const MaterialStage& stage : definition.stages) {
		QStringList references;
		if (stage.imageKind == MaterialImageKind::File) {
			references << stage.imagePath;
		} else if (stage.imageKind == MaterialImageKind::Animation) {
			references = stage.animationFrames;
		}
		for (const QString& reference : references) {
			const MaterialTexturePtr texture = images.find(reference);
			if (!texture || !texture->usable()) {
				return reference;
			}
		}
	}
	return QString();
}

void drawFogScene(const Mesh& mesh, const Camera& camera, Framebuffer* framebuffer, const MaterialDefinition& definition, const Quake3Frame& frame)
{
	// The fog volume seen from inside: neutral lit walls, then the fog
	// curve sqrt(min(1, depth / distanceToOpaque)) over planar depth.
	for (const Triangle& triangle : mesh.triangles) {
		const std::array<Vec3, 3> positions {mesh.vertices[triangle.a].position, mesh.vertices[triangle.b].position, mesh.vertices[triangle.c].position};
		std::array<Attributes, 3> attributes {};
		const Vertex* vertices[3] = {&mesh.vertices[triangle.a], &mesh.vertices[triangle.b], &mesh.vertices[triangle.c]};
		for (int index = 0; index < 3; ++index) {
			attributes[static_cast<size_t>(index)][0] = vertices[index]->s;
			attributes[static_cast<size_t>(index)][1] = vertices[index]->t;
		}
		rasterizeTriangle(camera, positions, attributes, 2, CullMode::Front, [&](const FragmentInput& input) {
			float* pixel = framebuffer->pixel(input.x, input.y);
			if (input.depth >= framebuffer->depthAt(input.x, input.y)) {
				return;
			}
			framebuffer->depthAt(input.x, input.y) = static_cast<float>(input.depth);
			const double s = (*input.attributes)[0];
			const double t = (*input.attributes)[1];
			const bool light = (static_cast<int>(std::floor(s * 4)) + static_cast<int>(std::floor(t * 4))) % 2 == 0;
			const double value = (light ? 0.62 : 0.42) * frame.identityLight;
			pixel[0] = pixel[1] = pixel[2] = static_cast<float>(value);
			pixel[3] = 1.0f;
		});
	}
	const double opaque = std::max(1.0, definition.fog.distanceToOpaque);
	const Color fog {definition.fog.color[0] * frame.identityLight, definition.fog.color[1] * frame.identityLight,
		definition.fog.color[2] * frame.identityLight, 1.0};
	for (int y = 0; y < framebuffer->height; ++y) {
		for (int x = 0; x < framebuffer->width; ++x) {
			const double depth = framebuffer->depthAt(x, y);
			const double amount = std::isfinite(depth) ? std::sqrt(std::min(1.0, depth / opaque)) : 1.0;
			float* pixel = framebuffer->pixel(x, y);
			pixel[0] = static_cast<float>(pixel[0] * (1 - amount) + fog.r * amount);
			pixel[1] = static_cast<float>(pixel[1] * (1 - amount) + fog.g * amount);
			pixel[2] = static_cast<float>(pixel[2] * (1 - amount) + fog.b * amount);
		}
	}
	Q_UNUSED(camera);
}

void drawQuake3Sky(const MaterialDefinition& definition, const MaterialImageSet& images, const Mesh& mesh, const Camera& camera,
	Framebuffer* framebuffer, const Quake3Frame& frame, MaterialRenderResult* result)
{
	// Coverage first: sky surfaces show the sky wherever they are seen.
	QVector<char> covered(static_cast<qsizetype>(framebuffer->width) * framebuffer->height, 0);
	for (const Triangle& triangle : mesh.triangles) {
		const std::array<Vec3, 3> positions {mesh.vertices[triangle.a].position, mesh.vertices[triangle.b].position, mesh.vertices[triangle.c].position};
		rasterizeTriangle(camera, positions, {}, 0, CullMode::None, [&](const FragmentInput& input) {
			covered[static_cast<qsizetype>(input.y) * framebuffer->width + input.x] = 1;
		});
	}
	const MaterialCubeTexturePtr box = images.findCube(definition.sky.farBox);
	const double radius = 4096.0;
	const double height = definition.sky.cloudHeight > 0 ? definition.sky.cloudHeight : 512.0;
	for (int y = 0; y < framebuffer->height; ++y) {
		for (int x = 0; x < framebuffer->width; ++x) {
			if (!covered[static_cast<qsizetype>(y) * framebuffer->width + x]) {
				continue;
			}
			float* pixel = framebuffer->pixel(x, y);
			const Vec3 d = camera.ray(x + 0.5, y + 0.5);
			// The far box draws with colour identityLight.
			Color sky {0, 0, 0, 1};
			if (box) {
				sky = sampleSkyBox(*box, d) * frame.identityLight;
			}
			pixel[0] = static_cast<float>(sky.r);
			pixel[1] = static_cast<float>(sky.g);
			pixel[2] = static_cast<float>(sky.b);
			pixel[3] = 1.0f;
			// Clouds: R_InitSkyTexCoords on a sphere of radius 4096 at the
			// cloud height; no clouds below the horizon box face.
			const bool bottom = std::abs(d.z) >= std::max(std::abs(d.x), std::abs(d.y)) && d.z < 0;
			if (bottom) {
				continue;
			}
			const double dd = d.dot(d);
			const double p = (-2 * radius * d.z
								 + 2 * std::sqrt(radius * radius * d.z * d.z + dd * (2 * radius * height + height * height)))
				/ (2 * dd);
			const Vec3 n = (d * p + Vec3 {0, 0, radius}).normalized();
			Vertex cloud;
			cloud.s = std::acos(std::clamp(n.x, -1.0, 1.0));
			cloud.t = std::acos(std::clamp(n.y, -1.0, 1.0));
			cloud.position = d * p;
			cloud.normal = Vec3 {0, 0, -1};
			for (const MaterialStage& stage : definition.stages) {
				const MaterialTexturePtr texture = quake3StageTexture(stage, images, frame.time);
				if (!texture || !texture->usable()) {
					continue;
				}
				double s = 0;
				double t = 0;
				quake3TexCoords(stage, cloud, camera, frame, &s, &t);
				Color fragment = sampleTexture(*texture, s, t, frame.options->filtering != MaterialFiltering::Nearest,
					stage.clamp ? Wrap::Clamp : Wrap::Repeat);
				const Color vertexColor = quake3VertexColor(definition, stage, cloud, mesh, camera, frame);
				fragment = fragment * vertexColor;
				if (!alphaPasses(stage.alphaTest, fragment.a)) {
					continue;
				}
				blendInto(pixel, fragment, stage.blend.source, stage.blend.destination);
			}
		}
	}
	result->stagesDrawn = static_cast<int>(definition.stages.size()) + (box ? 1 : 0);
	if (box && !box->note.isEmpty()) {
		result->notes << box->note;
	}
	if (!box && definition.sky.farBox != QStringLiteral("-") && !definition.sky.farBox.isEmpty()) {
		result->notes << Text::tr("The sky box %1 was not found.").arg(definition.sky.farBox);
	}
}

} // namespace

MaterialRenderResult renderQuake3Material(const MaterialDefinition& source, const MaterialImageSet& images, const MaterialTableSet& tables,
	const MaterialRenderOptions& options, const std::function<bool()>& cancelled)
{
	Q_UNUSED(tables);
	MaterialRenderResult result;
	Quake3Frame frame;
	frame.options = &options;
	frame.time = options.time;
	frame.identityLight = options.lighting.overbright ? 0.5 : 1.0;
	frame.displayScale = options.lighting.overbright ? 2.0 : 1.0;
	frame.shift = options.lighting.overbright ? 1 : 2;
	frame.lightColor = {options.lighting.lightColor.redF(), options.lighting.lightColor.greenF(), options.lighting.lightColor.blueF(), 1.0};

	// What the engine actually draws: the shader, its default when it
	// would be dropped, or the editor image on request.
	MaterialDefinition definition = source;
	if (options.editorImage && !source.editorImage.isEmpty()) {
		definition = implicitQuake3Material(source.name, MaterialSurfaceContext::TwoD, source.editorImage);
		definition.stages.first().blend = MaterialBlend();
	} else if (options.honourRejection) {
		const QString missingImage = quake3MissingImage(source, images);
		if (!source.engineRejection.isEmpty() || !missingImage.isEmpty()) {
			// tr.defaultShader: the 16x16 box, identityLighting, unlit.
			definition = MaterialDefinition();
			definition.name = source.name;
			definition.engine = MaterialEngine::Quake3;
			MaterialStage stage;
			stage.imageKind = MaterialImageKind::File;
			stage.imagePath = QStringLiteral("$quake3default");
			stage.rgbGen.source = MaterialColorSource::IdentityLighting;
			definition.stages.push_back(stage);
			result.fallback = true;
			result.notes << (missingImage.isEmpty() ? source.engineRejection
													: Text::tr("Quake III cannot find %1 and draws its default shader instead.").arg(missingImage));
		}
	}

	const QSize imageSize = mainImageSize(definition, images);
	// Quake III maps default to a texture scale of 0.5: two texels a unit.
	const double repeatS = std::max(8.0, imageSize.width() / 2.0);
	const double repeatT = std::max(8.0, imageSize.height() / 2.0);
	MaterialPreviewShape shape = options.shape;
	if (definition.isSky() || (definition.fog.present && definition.stages.isEmpty())) {
		shape = options.shape == MaterialPreviewShape::Room ? shape : MaterialPreviewShape::Room;
	}
	const double tiling = options.tiling > 0 ? options.tiling : (shape == MaterialPreviewShape::Room ? 2.0 : 2.0);
	const double segment = definition.tessSize > 0 ? definition.tessSize : 32.0;
	Mesh mesh = buildMesh(shape, repeatS, repeatT, tiling, segment);
	MaterialRenderOptions cameraOptions = options;
	cameraOptions.shape = shape;
	const Camera camera = makeCamera(mesh, cameraOptions);
	applyQuake3Deforms(definition, &mesh, camera, frame.time, &result.notes);

	Framebuffer framebuffer;
	framebuffer.reset(camera.width, camera.height);
	framebuffer.clear(options.background, options.checker, frame.identityLight);

	if (definition.isSky()) {
		drawQuake3Sky(definition, images, mesh, camera, &framebuffer, frame, &result);
		result.image = framebuffer.toImage(frame.displayScale);
		return result;
	}
	if (definition.fog.present && definition.stages.isEmpty()) {
		drawFogScene(mesh, camera, &framebuffer, definition, frame);
		result.image = framebuffer.toImage(frame.displayScale);
		result.notes << Text::tr("Fog is shown from inside the volume: opaque at %1 units.").arg(definition.fog.distanceToOpaque);
		return result;
	}
	const CullMode cull = cullModeFor(definition.cull);
	const bool bilinear = options.filtering != MaterialFiltering::Nearest;
	const MaterialTexturePtr defaultBox = quake3DefaultTexture();
	int stageNumber = 0;
	for (const MaterialStage& stage : definition.stages) {
		if (cancelled && cancelled()) {
			result.cancelled = true;
			return result;
		}
		if (++stageNumber > 8 || stage.imageKind == MaterialImageKind::None) {
			continue;
		}
		const bool lightmap = stage.imageKind == MaterialImageKind::Lightmap;
		MaterialTexturePtr texture;
		if (stage.imagePath == QStringLiteral("$quake3default")) {
			texture = defaultBox;
		} else if (!lightmap) {
			texture = quake3StageTexture(stage, images, frame.time);
			if (!texture || !texture->usable()) {
				continue;
			}
		}
		const bool noLightmap = lightmap && options.context != MaterialSurfaceContext::World;
		// Per-vertex coordinates and colours, as the engine computes them.
		QVector<std::array<double, 6>> perVertex(mesh.vertices.size());
		for (int index = 0; index < mesh.vertices.size(); ++index) {
			const Vertex& vertex = mesh.vertices.at(index);
			double s = 0;
			double t = 0;
			quake3TexCoords(stage, vertex, camera, frame, &s, &t);
			const Color color = quake3VertexColor(definition, stage, vertex, mesh, camera, frame);
			perVertex[index] = {s, t, color.r, color.g, color.b, color.a};
		}
		const double texelsPerUnit = texture ? texture->width / std::max(1.0, mesh.repeatS) : 1.0;
		for (const Triangle& triangle : mesh.triangles) {
			const int ids[3] = {triangle.a, triangle.b, triangle.c};
			std::array<Vec3, 3> positions {};
			std::array<Attributes, 3> attributes {};
			for (int k = 0; k < 3; ++k) {
				const Vertex& vertex = mesh.vertices.at(ids[k]);
				positions[static_cast<size_t>(k)] = vertex.position;
				Attributes& a = attributes[static_cast<size_t>(k)];
				for (int c = 0; c < 6; ++c) {
					a[static_cast<size_t>(c)] = perVertex.at(ids[k])[static_cast<size_t>(c)];
				}
				a[6] = vertex.position.x;
				a[7] = vertex.position.y;
				a[8] = vertex.position.z;
				a[9] = vertex.normal.x;
				a[10] = vertex.normal.y;
				a[11] = vertex.normal.z;
			}
			rasterizeTriangle(camera, positions, attributes, 12, cull, [&](const FragmentInput& input) {
				float& depth = framebuffer.depthAt(input.x, input.y);
				const double tolerance = 1.0e-4 * std::max(1.0, input.depth);
				if (stage.depthFunc == MaterialDepthFunc::Equal ? std::abs(input.depth - depth) > tolerance : input.depth > depth + tolerance) {
					return;
				}
				const Attributes& a = *input.attributes;
				Color fragment;
				if (lightmap) {
					const Vec3 position {a[6], a[7], a[8]};
					const Vec3 normal = Vec3 {a[9], a[10], a[11]}.normalized();
					fragment = noLightmap ? Color {1, 1, 1, 1}
										  : quake3Lightmap(previewLightAt(position, normal, mesh, options.lighting, frame.time), frame);
				} else {
					const Vec3 toEye = (camera.eye - Vec3 {a[6], a[7], a[8]}).normalized();
					const double obliquity = std::abs(Vec3 {a[9], a[10], a[11]}.normalized().dot(toEye));
					const double lod = bilinear ? mipLevel(camera, input.depth, obliquity, texelsPerUnit) : 0.0;
					fragment = sampleTexture(*texture, a[0], a[1], bilinear, stage.clamp ? Wrap::Clamp : Wrap::Repeat, lod);
				}
				fragment = fragment * Color {a[2], a[3], a[4], a[5]};
				if (!alphaPasses(stage.alphaTest, fragment.a)) {
					return;
				}
				blendInto(framebuffer.pixel(input.x, input.y), fragment, stage.blend.source, stage.blend.destination);
				if (stage.depthWrite) {
					depth = static_cast<float>(input.depth);
				}
			});
		}
		++result.stagesDrawn;
	}
	result.image = framebuffer.toImage(frame.displayScale);
	return result;
}

} // namespace material_render

using namespace material_render;

QString materialPreviewShapeId(MaterialPreviewShape shape)
{
	switch (shape) {
	case MaterialPreviewShape::Wall:
		return QStringLiteral("wall");
	case MaterialPreviewShape::Floor:
		return QStringLiteral("floor");
	case MaterialPreviewShape::Cube:
		return QStringLiteral("cube");
	case MaterialPreviewShape::Sphere:
		return QStringLiteral("sphere");
	case MaterialPreviewShape::Cylinder:
		return QStringLiteral("cylinder");
	case MaterialPreviewShape::Room:
		return QStringLiteral("room");
	}
	return QStringLiteral("wall");
}

QString materialPreviewShapeDisplayName(MaterialPreviewShape shape)
{
	switch (shape) {
	case MaterialPreviewShape::Wall:
		return QCoreApplication::translate("VibeStudioMaterials", "Wall");
	case MaterialPreviewShape::Floor:
		return QCoreApplication::translate("VibeStudioMaterials", "Floor");
	case MaterialPreviewShape::Cube:
		return QCoreApplication::translate("VibeStudioMaterials", "Cube");
	case MaterialPreviewShape::Sphere:
		return QCoreApplication::translate("VibeStudioMaterials", "Sphere");
	case MaterialPreviewShape::Cylinder:
		return QCoreApplication::translate("VibeStudioMaterials", "Cylinder");
	case MaterialPreviewShape::Room:
		return QCoreApplication::translate("VibeStudioMaterials", "Room");
	}
	return QString();
}

bool materialPreviewShapeFromId(const QString& id, MaterialPreviewShape* shape)
{
	for (MaterialPreviewShape candidate : materialPreviewShapes()) {
		if (materialPreviewShapeId(candidate) == id.trimmed().toLower()) {
			*shape = candidate;
			return true;
		}
	}
	return false;
}

QVector<MaterialPreviewShape> materialPreviewShapes()
{
	return {MaterialPreviewShape::Wall, MaterialPreviewShape::Floor, MaterialPreviewShape::Cube, MaterialPreviewShape::Sphere,
		MaterialPreviewShape::Cylinder, MaterialPreviewShape::Room};
}

QString materialQuakeRendererId(MaterialQuakeRenderer renderer)
{
	switch (renderer) {
	case MaterialQuakeRenderer::GLQuake:
		return QStringLiteral("glquake");
	case MaterialQuakeRenderer::ModernPort:
		return QStringLiteral("modern");
	case MaterialQuakeRenderer::Software:
		return QStringLiteral("software");
	}
	return QStringLiteral("modern");
}

bool materialQuakeRendererFromId(const QString& id, MaterialQuakeRenderer* renderer)
{
	for (MaterialQuakeRenderer candidate : {MaterialQuakeRenderer::GLQuake, MaterialQuakeRenderer::ModernPort, MaterialQuakeRenderer::Software}) {
		if (materialQuakeRendererId(candidate) == id.trimmed().toLower()) {
			*renderer = candidate;
			return true;
		}
	}
	return false;
}

QString materialFilteringId(MaterialFiltering filtering)
{
	switch (filtering) {
	case MaterialFiltering::Engine:
		return QStringLiteral("engine");
	case MaterialFiltering::Nearest:
		return QStringLiteral("nearest");
	case MaterialFiltering::Bilinear:
		return QStringLiteral("bilinear");
	}
	return QStringLiteral("engine");
}

bool materialFilteringFromId(const QString& id, MaterialFiltering* filtering)
{
	for (MaterialFiltering candidate : {MaterialFiltering::Engine, MaterialFiltering::Nearest, MaterialFiltering::Bilinear}) {
		if (materialFilteringId(candidate) == id.trimmed().toLower()) {
			*filtering = candidate;
			return true;
		}
	}
	return false;
}

MaterialPreviewShape defaultMaterialPreviewShape(const MaterialDefinition& definition)
{
	if (definition.isSky() || definition.isFog()) {
		return MaterialPreviewShape::Room;
	}
	if (definition.kind == QStringLiteral("doom-flat")) {
		return MaterialPreviewShape::Floor;
	}
	if (definition.isLight()) {
		return MaterialPreviewShape::Room;
	}
	return MaterialPreviewShape::Wall;
}

double defaultMaterialPreviewPitch(MaterialPreviewShape shape)
{
	switch (shape) {
	case MaterialPreviewShape::Floor:
		return 50.0;
	case MaterialPreviewShape::Room:
		return 22.0;
	case MaterialPreviewShape::Cube:
	case MaterialPreviewShape::Sphere:
	case MaterialPreviewShape::Cylinder:
		return 20.0;
	case MaterialPreviewShape::Wall:
		break;
	}
	return 12.0;
}

MaterialRenderResult renderMaterial(const MaterialDefinition& definition, const MaterialImageSet& images, const MaterialTableSet& tables,
	const MaterialRenderOptions& options, const std::function<bool()>& cancelled)
{
	QElapsedTimer timer;
	timer.start();
	MaterialRenderOptions bounded = options;
	bounded.size = QSize(std::clamp(options.size.width(), 8, 4096), std::clamp(options.size.height(), 8, 4096));
	MaterialRenderResult result;
	switch (definition.engine) {
	case MaterialEngine::Quake3:
		result = renderQuake3Material(definition, images, tables, bounded, cancelled);
		break;
	case MaterialEngine::Doom3:
		result = renderDoom3Material(definition, images, tables, bounded, cancelled);
		break;
	case MaterialEngine::Doom:
	case MaterialEngine::Quake:
	case MaterialEngine::Quake2:
		result = renderClassicMaterial(definition, images, bounded, cancelled);
		break;
	case MaterialEngine::Unknown: {
		Framebuffer framebuffer;
		framebuffer.reset(bounded.size.width(), bounded.size.height());
		framebuffer.clear(bounded.background, bounded.checker, 1.0);
		result.image = framebuffer.toImage(1.0);
		result.notes << QCoreApplication::translate("VibeStudioMaterials", "The material's engine is unknown, so nothing is drawn.");
		break;
	}
	}
	result.notes.removeDuplicates();
	result.milliseconds = timer.nsecsElapsed() / 1.0e6;
	return result;
}

MaterialRenderResult renderMaterialSwatch(const MaterialDefinition& definition, const MaterialImageSet& images, const MaterialTableSet& tables, int side,
	double time, const MaterialPreviewLighting& lighting)
{
	MaterialRenderOptions options;
	options.size = QSize(side, side);
	options.time = time;
	options.orthographic = true;
	options.tiling = 1.0;
	options.lighting = lighting;
	options.lighting.lightmapSpot = false;
	options.lighting.lightOrbit = false;
	options.lighting.lightAngle = 0.0;
	options.lighting.doomDistance = false;
	options.lighting.doomLight = 255;
	options.checker = true;
	options.shape = definition.kind == QStringLiteral("doom-flat") ? MaterialPreviewShape::Floor : MaterialPreviewShape::Wall;
	if (definition.isSky() || definition.isFog()) {
		options.orthographic = false;
		options.shape = MaterialPreviewShape::Room;
		options.pitch = 22.0;
	}
	return renderMaterial(definition, images, tables, options);
}

} // namespace vibestudio
