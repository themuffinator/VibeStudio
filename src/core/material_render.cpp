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
// Camera
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

int alphaTestCode(MaterialAlphaTest test)
{
	switch (test) {
	case MaterialAlphaTest::Greater0:
		return 1;
	case MaterialAlphaTest::Less128:
		return 2;
	case MaterialAlphaTest::GreaterEqual128:
		return 3;
	default:
		return 0;
	}
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

// The fog volume seen from inside: neutral lit walls, then the fog curve
// sqrt(min(1, depth / distanceToOpaque)) over planar depth.
void drawFogScene(MaterialGpuFrame* gpu, const MaterialDefinition& definition, const Quake3Frame& frame)
{
	const Color fog {definition.fog.color[0] * frame.identityLight, definition.fog.color[1] * frame.identityLight,
		definition.fog.color[2] * frame.identityLight, 1.0};
	gpu->fogScene(gpu->vertices(), fog, std::max(1.0, definition.fog.distanceToOpaque), frame.identityLight);
}

// The per-pixel colour of a cloud layer: constant parts are worked out here,
// the rest (lighting at the cloud point) by the shader. Returns the codes the
// shader's mode.w expects.
int cloudColorCodes(const MaterialDefinition& definition, const MaterialStage& stage, const Mesh& mesh, const Camera& camera,
	const Quake3Frame& frame, MaterialUniforms* uniforms)
{
	const MaterialRenderOptions& options = *frame.options;
	Vertex cloud;
	cloud.normal = Vec3 {0, 0, -1};
	uniforms->setColor(quake3VertexColor(definition, stage, cloud, mesh, camera, frame));
	int rgb = 0;
	if (options.context == MaterialSurfaceContext::World) {
		switch (stage.rgbGen.source) {
		case MaterialColorSource::Vertex:
			rgb = 1;
			break;
		case MaterialColorSource::ExactVertex:
			rgb = 2;
			break;
		case MaterialColorSource::OneMinusVertex:
			rgb = 3;
			break;
		default:
			break;
		}
	}
	if (stage.rgbGen.source == MaterialColorSource::LightingDiffuse) {
		rgb = 4;
	}
	int alpha = 0;
	if (stage.alphaGen.source == MaterialColorSource::LightingSpecular) {
		alpha = 1;
	} else if (stage.alphaGen.source == MaterialColorSource::Portal) {
		alpha = 2;
	}
	uniforms->setParam(7, options.lighting.gridAmbient, options.lighting.gridDirected, std::max(1.0, stage.alphaGen.portalRange));
	return rgb | (alpha << 4);
}

// A cloud layer's tcMods for the shader: turbulence stays per pixel, the
// rest are affine maps of this moment (quake3TexCoords).
void cloudTexMods(const MaterialStage& stage, const Quake3Frame& frame, MaterialUniforms* uniforms)
{
	const double time = frame.time;
	int count = 0;
	for (const MaterialTexMod& mod : stage.tcMods) {
		if (count >= 4) {
			break;
		}
		double m[2][3] = {{1, 0, 0}, {0, 1, 0}};
		bool affine = true;
		switch (mod.kind) {
		case MaterialTexModKind::Turbulent:
			uniforms->setParam(count, 1, mod.wave.amplitude, mod.wave.phase + time * mod.wave.frequency);
			affine = false;
			break;
		case MaterialTexModKind::Scroll: {
			double os = mod.values[0] * time;
			double ot = mod.values[1] * time;
			os -= std::floor(os);
			ot -= std::floor(ot);
			m[0][2] = os;
			m[1][2] = ot;
			break;
		}
		case MaterialTexModKind::Scale:
			m[0][0] = mod.values[0];
			m[1][1] = mod.values[1];
			break;
		case MaterialTexModKind::Rotate: {
			const double degrees = -mod.values[0] * time;
			const int index = static_cast<int>(degrees * (kQuake3FunctionTableSize / 360.0));
			const double sn = quake3Sin(index);
			const double cs = quake3Sin(index + kQuake3FunctionTableSize / 4);
			m[0][0] = cs;
			m[0][1] = -sn;
			m[0][2] = 0.5 - 0.5 * cs + 0.5 * sn;
			m[1][0] = sn;
			m[1][1] = cs;
			m[1][2] = 0.5 - 0.5 * sn - 0.5 * cs;
			break;
		}
		case MaterialTexModKind::Stretch: {
			const double wave = evaluateQuake3Wave(mod.wave, time);
			const double p = std::abs(wave) < 1.0e-9 ? 1.0e9 : 1.0 / wave;
			m[0][0] = p;
			m[0][2] = 0.5 - 0.5 * p;
			m[1][1] = p;
			m[1][2] = 0.5 - 0.5 * p;
			break;
		}
		case MaterialTexModKind::Transform:
			m[0][0] = mod.values[0];
			m[0][1] = mod.values[2];
			m[0][2] = mod.values[4];
			m[1][0] = mod.values[1];
			m[1][1] = mod.values[3];
			m[1][2] = mod.values[5];
			break;
		default:
			continue;
		}
		if (affine) {
			uniforms->setParam(count, 2);
			uniforms->setMatrix(count, m);
		}
		++count;
	}
}

void drawQuake3Sky(MaterialGpuFrame* gpu, const MaterialDefinition& definition, const MaterialImageSet& images, const Mesh& mesh,
	const Camera& camera, const Quake3Frame& frame, MaterialRenderResult* result)
{
	// Sky surfaces show the sky wherever they are seen: the box first, then
	// each cloud layer blended over it, worked out per pixel from the ray.
	const MaterialCubeTexturePtr box = images.findCube(definition.sky.farBox);
	const int vertices = gpu->vertices();
	GpuState coverage;
	coverage.cull = GpuCull::None;
	{
		MaterialUniforms u = gpu->uniforms();
		u.mode[0] = 5;
		u.viewport[3] = static_cast<float>(frame.identityLight);
		GpuDraw textures;
		for (int face = 0; face < 6; ++face) {
			const MaterialTexturePtr& texture = box ? box->faces[size_t(face)] : MaterialTexturePtr();
			gpu->bind(&textures, &u, face, texture && texture->usable() ? gpu->texture(texture.get()) : -1, Wrap::Clamp);
		}
		gpu->draw(GpuProgram::MaterialQuake3, u, coverage, vertices, textures);
	}
	const double height = definition.sky.cloudHeight > 0 ? definition.sky.cloudHeight : 512.0;
	const bool bilinear = frame.options->filtering != MaterialFiltering::Nearest;
	for (const MaterialStage& stage : definition.stages) {
		const MaterialTexturePtr texture = quake3StageTexture(stage, images, frame.time);
		if (!texture || !texture->usable()) {
			continue;
		}
		MaterialUniforms u = gpu->uniforms();
		u.mode[0] = 6;
		u.mode[1] = alphaTestCode(stage.alphaTest);
		u.mode[2] = bilinear ? 1 : 0;
		u.mode[3] = cloudColorCodes(definition, stage, mesh, camera, frame, &u);
		u.viewport[3] = static_cast<float>(frame.identityLight);
		u.lighting[1] = static_cast<float>(frame.shift);
		int source = 0;
		switch (stage.tcGen.source) {
		case MaterialTexCoordSource::Lightmap:
			source = 1;
			break;
		case MaterialTexCoordSource::Environment:
			source = 2;
			break;
		case MaterialTexCoordSource::Vector:
			source = 3;
			u.setParam(5, stage.tcGen.vectorS[0], stage.tcGen.vectorS[1], stage.tcGen.vectorS[2]);
			u.setParam(6, stage.tcGen.vectorT[0], stage.tcGen.vectorT[1], stage.tcGen.vectorT[2]);
			break;
		default:
			break;
		}
		u.setParam(4, height, source);
		cloudTexMods(stage, frame, &u);
		GpuDraw textures;
		gpu->bind(&textures, &u, 0, gpu->texture(texture.get()), stage.clamp ? Wrap::Clamp : Wrap::Repeat);
		GpuState state = coverage;
		state.blend = !stage.blend.isOpaqueReplace();
		state.sourceColor = gpuBlendFactor(stage.blend.source);
		state.destinationColor = gpuBlendFactor(stage.blend.destination);
		state.sourceAlpha = state.sourceColor == GpuBlend::SourceAlphaSaturate ? GpuBlend::One : state.sourceColor;
		state.destinationAlpha = state.destinationColor;
		gpu->draw(GpuProgram::MaterialQuake3, u, state, vertices, textures);
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

	MaterialGpuFrame gpu(camera, mesh, options);
	gpu.background(options.background, options.checker, frame.identityLight);

	if (definition.isSky()) {
		drawQuake3Sky(&gpu, definition, images, mesh, camera, frame, &result);
		gpu.finish(frame.displayScale, &result, cancelled);
		return result;
	}
	if (definition.fog.present && definition.stages.isEmpty()) {
		drawFogScene(&gpu, definition, frame);
		gpu.finish(frame.displayScale, &result, cancelled);
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
		QVector<StageVertex> perVertex(mesh.vertices.size());
		for (int index = 0; index < mesh.vertices.size(); ++index) {
			const Vertex& vertex = mesh.vertices.at(index);
			StageVertex& values = perVertex[index];
			quake3TexCoords(stage, vertex, camera, frame, &values.s, &values.t);
			values.color = quake3VertexColor(definition, stage, vertex, mesh, camera, frame);
		}
		const double texelsPerUnit = texture ? texture->width / std::max(1.0, mesh.repeatS) : 1.0;
		MaterialUniforms u = gpu.uniforms();
		u.mode[0] = lightmap ? (noLightmap ? 2 : 1) : 0;
		u.mode[1] = alphaTestCode(stage.alphaTest);
		u.mode[2] = bilinear ? 1 : 0;
		u.lighting[1] = static_cast<float>(frame.shift);
		u.lighting[3] = static_cast<float>(texelsPerUnit);
		u.viewport[3] = static_cast<float>(frame.identityLight);
		GpuDraw textures;
		if (!lightmap) {
			gpu.bind(&textures, &u, 0, gpu.texture(texture.get()), stage.clamp ? Wrap::Clamp : Wrap::Repeat);
		}
		// Equal-depth stages line up exactly: every pass draws the same
		// positions with the same (invariant) transform.
		const GpuCompare depth = stage.depthFunc == MaterialDepthFunc::Equal ? GpuCompare::Equal : GpuCompare::GreaterOrEqual;
		gpu.draw(GpuProgram::MaterialQuake3, u, stageState(stage.blend, depth, stage.depthWrite, cull), gpu.vertices(&perVertex), textures);
		++result.stagesDrawn;
	}
	gpu.finish(frame.displayScale, &result, cancelled);
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
		const Mesh mesh = buildMesh(MaterialPreviewShape::Wall, 64.0, 64.0, 1.0, 64.0);
		MaterialGpuFrame gpu(makeCamera(mesh, bounded), mesh, bounded);
		gpu.background(bounded.background, bounded.checker, 1.0);
		gpu.finish(1.0, &result, cancelled);
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
