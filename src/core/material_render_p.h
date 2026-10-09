#pragma once

// Internal pieces of the material renderer shared by its engine files:
// vector maths, preview meshes, the camera, and the GPU frame each engine
// path fills with its passes. The per-pixel parts of every engine live in
// shaders (src/core/shaders/material_*.frag); the engine files work out what
// the engines compute per vertex and per stage, and choose the passes.

#include "core/material_render.h"
#include "core/render_device.h"
#include "core/render_shaders.h"

#include <QColor>
#include <QHash>
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

// The engines' culling, in terms of which faces are drawn.
enum class CullMode {
	Front, // draw front faces only (the engines' default)
	Back,  // draw back faces only
	None,
};

CullMode cullModeFor(MaterialCull cull);
GpuCull gpuCull(CullMode cull);

// How a stage's image repeats; the shaders sample with these rules.
enum class Wrap {
	Repeat,
	Clamp,          // clamp to edge
	ZeroClamp,      // outside is transparent black
	AlphaZeroClamp, // outside keeps colour, alpha 0
};

// A soft light falling on a point, for synthetic lightmaps and vertex
// colours: 1.0 is full brightness. Deterministic in position and time.
double previewLightAt(const Vec3& position, const Vec3& normal, const Mesh& mesh, const MaterialPreviewLighting& lighting, double time);
// Where the preview light is, orbiting or fixed.
Vec3 previewLightPosition(const Mesh& mesh, const MaterialPreviewLighting& lighting, double time);


double fract(double value);

// ---------------------------------------------------------------------------
// GPU frames
// ---------------------------------------------------------------------------

// The uniform block every material program reads (material_common.glsl),
// in std140 layout.
struct MaterialUniforms {
	float viewProjection[16] {};
	float eye[4] {};
	float forward[4] {};
	float right[4] {};
	float up[4] {};
	float viewport[4] {};
	float light[4] {};
	float mesh[4] {};
	float lighting[4] {};
	float lightColor[4] {};
	qint32 mode[4] {};
	float color[4] {1.0f, 1.0f, 1.0f, 1.0f};
	float params[8][4] {};
	float matrices[16][4] {};
	qint32 samplerInfo[8][4] {};

	void setColor(const Color& value);
	void setParam(int index, double x, double y = 0.0, double z = 0.0, double w = 0.0);
	// A 2x3 texture matrix for slot `slot` (rows s and t).
	void setMatrix(int slot, const double rows[2][3]);
	void setIdentityMatrix(int slot);
};
static_assert(sizeof(MaterialUniforms) == 752, "MaterialUniforms must match material_common.glsl");

// Per-vertex values a stage supplies: texture coordinates and a colour.
struct StageVertex {
	double s = 0.0;
	double t = 0.0;
	Color color {1.0, 1.0, 1.0, 1.0};
};

// One preview frame on the GPU: a 16-bit colour target (the engines'
// framebuffer, clamped after every blend), depth, and the passes the engine
// path adds. finish() renders it on the active backend and returns the
// display-scaled 8-bit image.
class MaterialGpuFrame {
public:
	MaterialGpuFrame(const Camera& camera, const Mesh& mesh, const MaterialRenderOptions& options);

	// Camera, viewport, preview light and time, ready for a draw to adjust.
	[[nodiscard]] MaterialUniforms uniforms() const;

	// Textures, uploaded once per frame each. -1 for an unusable texture.
	int texture(const MaterialTexture* texture);
	// Palette indices in red, the texel's alpha kept.
	int indices(const MaterialTexture* texture);
	// 256 colours in one row.
	int palette(const QVector<QRgb>& palette);
	// Rows of 256 palette indices (COLORMAP, colormap.lmp) in red.
	int rows(const QByteArray& table, int rowCount);
	// Binds a texture to a sampler slot of a draw: its wrap rule and mip
	// levels go to the uniforms. A negative texture binds nothing.
	void bind(GpuDraw* draw, MaterialUniforms* uniforms, int slot, int texture, Wrap wrap) const;
	// The colour so far, for _currentRender; binds it to `slot`.
	void bindSnapshot(GpuDraw* draw, MaterialUniforms* uniforms, int slot);

	// The mesh as vertex data, with each vertex's stage coordinates and
	// colour (or its own s, t and white when `stage` is null).
	int vertices(const QVector<StageVertex>* stage = nullptr);
	int vertices(const Mesh& mesh, const QVector<StageVertex>* stage = nullptr);

	// Draws `program` over the mesh (or a substitute `mesh` given to
	// vertices()) with this state. Depth compares nearness: Greater for the
	// engines' less-than, GreaterOrEqual for less-or-equal, Equal for equal.
	void draw(GpuProgram program, const MaterialUniforms& uniforms, const GpuState& state, int vertexBuffer, const GpuDraw& textures,
		int indexBuffer = -1, int indexCount = -1);
	// The background: a colour, optionally checkered, times `scale`.
	void background(const QColor& colour, bool checker, double scale);
	// Quake III's fog volume from inside: walls writing their distance, then
	// the fog curve over them.
	void fogScene(int vertexBuffer, const Color& fog, double distanceToOpaque, double identityLight);
	// The index buffer of the frame's main mesh.
	[[nodiscard]] int meshIndices() const { return m_meshIndices; }
	[[nodiscard]] int meshIndexCount() const { return m_meshIndexCount; }

	// Renders and reads back the frame. On failure the result has no image
	// and `error` says why; a cancelled frame sets `cancelled`.
	void finish(double displayScale, MaterialRenderResult* result, const std::function<bool()>& cancelled);

private:
	GpuPass& mainPass();
	void closePass();

	Camera m_camera;
	const Mesh* m_mesh = nullptr;
	const MaterialRenderOptions* m_options = nullptr;
	GpuFrame m_frame;
	QHash<const void*, int> m_textures;
	QHash<const void*, int> m_indexTextures;
	// Mip levels of each uploaded texture, by frame texture index.
	QHash<int, int> m_levels;
	int m_meshIndices = -1;
	int m_meshIndexCount = 0;
	int m_snapshotTarget = -1;
	bool m_passOpen = false;
	bool m_colorWritten = false;
	bool m_depthWritten = false;
	double m_depthLow = 0.5;
	double m_depthHigh = 1.0;
};

// Fixed-function state for an engine's blend, depth function and writes.
GpuBlend gpuBlendFactor(MaterialBlendFactor factor);
GpuState stageState(const MaterialBlend& blend, GpuCompare depth, bool depthWrite, CullMode cull);

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
