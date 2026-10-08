#pragma once

// Internal pieces of the material renderer shared by its engine files:
// vector maths, preview meshes, the camera, a float framebuffer, a
// perspective-correct triangle rasterizer with near-plane clipping, and
// texture sampling with the engines' wrap modes.

#include "core/material_render.h"

#include <QColor>
#include <QImage>
#include <QVector>

#include <array>
#include <cmath>
#include <functional>
#include <limits>

namespace vibestudio::material_render {

struct Vec3 {
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;

	Vec3 operator+(const Vec3& other) const { return {x + other.x, y + other.y, z + other.z}; }
	Vec3 operator-(const Vec3& other) const { return {x - other.x, y - other.y, z - other.z}; }
	Vec3 operator*(double scale) const { return {x * scale, y * scale, z * scale}; }
	Vec3 operator-() const { return {-x, -y, -z}; }
	[[nodiscard]] double dot(const Vec3& other) const { return x * other.x + y * other.y + z * other.z; }
	[[nodiscard]] Vec3 cross(const Vec3& other) const
	{
		return {y * other.z - z * other.y, z * other.x - x * other.z, x * other.y - y * other.x};
	}
	[[nodiscard]] double length() const { return std::sqrt(dot(*this)); }
	[[nodiscard]] Vec3 normalized() const
	{
		const double l = length();
		return l > 0.0 ? Vec3 {x / l, y / l, z / l} : Vec3 {0.0, 0.0, 1.0};
	}
};

struct Color {
	double r = 0.0;
	double g = 0.0;
	double b = 0.0;
	double a = 1.0;

	Color operator*(const Color& other) const { return {r * other.r, g * other.g, b * other.b, a * other.a}; }
	Color operator*(double scale) const { return {r * scale, g * scale, b * scale, a * scale}; }
	Color operator+(const Color& other) const { return {r + other.r, g + other.g, b + other.b, a + other.a}; }
	[[nodiscard]] Color clamped() const
	{
		return {std::clamp(r, 0.0, 1.0), std::clamp(g, 0.0, 1.0), std::clamp(b, 0.0, 1.0), std::clamp(a, 0.0, 1.0)};
	}
	static Color fromRgb(QRgb rgb)
	{
		return {qRed(rgb) / 255.0, qGreen(rgb) / 255.0, qBlue(rgb) / 255.0, qAlpha(rgb) / 255.0};
	}
};

struct Vertex {
	Vec3 position;
	Vec3 normal;
	Vec3 tangent;   // increasing s
	Vec3 bitangent; // increasing t
	double s = 0.0; // base texture coordinates in repeats
	double t = 0.0;
	double ls = 0.0; // lightmap coordinates, 0..1 over a face
	double lt = 0.0;
	Color color {1.0, 1.0, 1.0, 1.0};
	int face = 0;
};

struct Triangle {
	int a = 0;
	int b = 0;
	int c = 0;
};

struct Mesh {
	QVector<Vertex> vertices;
	QVector<Triangle> triangles;
	MaterialPreviewShape shape = MaterialPreviewShape::Wall;
	Vec3 center;
	double radius = 128.0;
	bool inside = false;
	// World units per texture repeat.
	double repeatS = 64.0;
	double repeatT = 64.0;
};

// Builds a shape whose texture repeats every repeatS x repeatT world units,
// `tiling` repeats across, with roughly `segmentSize` world units between
// vertices so per-vertex deforms bend smoothly. Z is up; a wall faces +X.
Mesh buildMesh(MaterialPreviewShape shape, double repeatS, double repeatT, double tiling, double segmentSize);

struct Camera {
	Vec3 eye;
	Vec3 forward;
	Vec3 right;
	Vec3 up;
	int width = 1;
	int height = 1;
	double centerX = 0.5;
	double centerY = 0.5;
	double focal = 1.0;
	bool orthographic = false;
	double orthoScale = 1.0;
	double nearPlane = 1.0;

	[[nodiscard]] double depthOf(const Vec3& point) const { return (point - eye).dot(forward); }
	// Screen position (x right, y down) of a point in front of the camera.
	void project(const Vec3& point, double* sx, double* sy) const;
	// World direction through a pixel centre (normalised).
	[[nodiscard]] Vec3 ray(double px, double py) const;
};

Camera makeCamera(const Mesh& mesh, const MaterialRenderOptions& options);

struct Framebuffer {
	int width = 0;
	int height = 0;
	QVector<float> color;
	QVector<float> depth;

	void reset(int w, int h);
	void clear(const QColor& background, bool checker, double scale);
	[[nodiscard]] float* pixel(int x, int y) { return color.data() + (static_cast<qsizetype>(y) * width + x) * 4; }
	[[nodiscard]] const float* pixel(int x, int y) const { return color.data() + (static_cast<qsizetype>(y) * width + x) * 4; }
	[[nodiscard]] float& depthAt(int x, int y) { return depth[static_cast<qsizetype>(y) * width + x]; }
	[[nodiscard]] QImage toImage(double displayScale) const;
};

inline constexpr int kMaxAttributes = 20;
using Attributes = std::array<double, kMaxAttributes>;

enum class CullMode {
	Front, // draw front faces only (the engines' default)
	Back,  // draw back faces only
	None,
};

CullMode cullModeFor(MaterialCull cull);

struct FragmentInput {
	int x = 0;
	int y = 0;
	double depth = 0.0;
	bool frontFacing = true;
	const Attributes* attributes = nullptr;
};

// Rasterizes one triangle given world positions and per-vertex attributes,
// clipping at the camera's near plane. Attributes are interpolated with
// perspective correction. The callback runs once per covered pixel centre.
void rasterizeTriangle(const Camera& camera, const std::array<Vec3, 3>& positions, const std::array<Attributes, 3>& attributes, int count,
	CullMode cull, const std::function<void(const FragmentInput&)>& fragment);

enum class Wrap {
	Repeat,
	Clamp,          // clamp to edge
	ZeroClamp,      // outside is transparent black
	AlphaZeroClamp, // outside keeps colour, alpha 0
};

// Samples straight RGBA at (s, t) in texture repeats; `lod` picks a mip
// level (0 = full size) for minification.
Color sampleTexture(const MaterialTexture& texture, double s, double t, bool bilinear, Wrap wrap, double lod = 0.0);
// The palette index under (s, t), point sampled, -1 when the texture has
// no indices or the texel is transparent.
int sampleIndex(const MaterialTexture& texture, double s, double t, Wrap wrap);

// Samples a cube in idTech sky box orientation (Quake II/III `env` boxes)
// for a world direction (Z up).
Color sampleSkyBox(const MaterialCubeTexture& cube, const Vec3& direction);
// Samples a Doom 3 cube map (GL face convention) for a world direction.
Color sampleCubeMap(const MaterialCubeTexture& cube, const Vec3& direction);

// Blends a fragment into a framebuffer pixel with GL factors and clamps.
void blendInto(float* destination, const Color& source, MaterialBlendFactor sourceFactor, MaterialBlendFactor destinationFactor,
	bool maskRed = false, bool maskGreen = false, bool maskBlue = false, bool maskAlpha = false);

// A soft light falling on a point, for synthetic lightmaps and vertex
// colours: 1.0 is full brightness. Deterministic in position and time.
double previewLightAt(const Vec3& position, const Vec3& normal, const Mesh& mesh, const MaterialPreviewLighting& lighting, double time);
// Where the preview light is, orbiting or fixed.
Vec3 previewLightPosition(const Mesh& mesh, const MaterialPreviewLighting& lighting, double time);

// Mip level for a fragment: texels per pixel from depth, the surface's
// obliquity and the stage's texture density.
double mipLevel(const Camera& camera, double depth, double obliquity, double texelsPerUnit);

double fract(double value);

// The engine paths, implemented in their own files.
MaterialRenderResult renderQuake3Material(const MaterialDefinition& definition, const MaterialImageSet& images, const MaterialTableSet& tables,
	const MaterialRenderOptions& options, const std::function<bool()>& cancelled);
MaterialRenderResult renderDoom3Material(const MaterialDefinition& definition, const MaterialImageSet& images, const MaterialTableSet& tables,
	const MaterialRenderOptions& options, const std::function<bool()>& cancelled);
MaterialRenderResult renderClassicMaterial(const MaterialDefinition& definition, const MaterialImageSet& images,
	const MaterialRenderOptions& options, const std::function<bool()>& cancelled);

// The size of a material's main image, for repeat sizes and mip levels.
QSize mainImageSize(const MaterialDefinition& definition, const MaterialImageSet& images);

} // namespace vibestudio::material_render
