#pragma once

// Private pieces of ModelViewport shared by its source files: the colour set,
// the value snapshot a render worker owns, and the GPU geometry the worker
// builds from it (model_viewport_render.cpp).

#include "app/model_viewport.h"

#include <QBitArray>
#include <QByteArray>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>

namespace vibestudio {

namespace model_viewport_detail {

constexpr double kNearPlane = 1.0;
// Flat shading uses 24 steps; far below what the eye separates on a face.
constexpr int kShadeSteps = 24;

inline ModelVec3 vec(double x, double y, double z)
{
	return ModelVec3 {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
}

inline double dot(const ModelVec3& a, const ModelVec3& b)
{
	return static_cast<double>(a.x) * b.x + static_cast<double>(a.y) * b.y + static_cast<double>(a.z) * b.z;
}

inline ModelVec3 cross(const ModelVec3& a, const ModelVec3& b)
{
	return vec(static_cast<double>(a.y) * b.z - static_cast<double>(a.z) * b.y, static_cast<double>(a.z) * b.x - static_cast<double>(a.x) * b.z,
		static_cast<double>(a.x) * b.y - static_cast<double>(a.y) * b.x);
}

inline ModelVec3 subtract(const ModelVec3& a, const ModelVec3& b)
{
	return vec(static_cast<double>(a.x) - b.x, static_cast<double>(a.y) - b.y, static_cast<double>(a.z) - b.z);
}

inline bool finiteVec(const ModelVec3& v)
{
	return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

inline bool sameVec(const ModelVec3& a, const ModelVec3& b)
{
	return a.x == b.x && a.y == b.y && a.z == b.z;
}

} // namespace model_viewport_detail

// Colour set for one paint pass. Every distinction the viewport draws is also
// carried by line weight, marker shape or text so nothing depends on colour
// alone.
struct ModelViewport::Palette {
	QColor background;
	QColor grid;
	QColor gridMajor;
	QColor axisX;
	QColor axisY;
	QColor axisZ;
	QColor wire;
	QColor edge;
	QColor hover;
	QColor text;
	QColor subtleText;
	QColor focus;
	QColor highlight;
};

// Everything the GPU geometry depends on besides the camera: the pose and
// every preview that moves vertices. Equal keys share one upload.
struct ModelViewport::GeometryKey {
	quint64 mesh = 0;
	int frame = 0;
	int blendFrame = 0;
	double frameBlend = 0;
	bool textured = false;
	bool highContrast = false;
	bool editMoveActive = false;
	bool editTagEmpty = true;
	int editSurface = -1;
	ModelTransform editTransform;
	QSet<int> editSurfaces;
	QSet<int> editVertices;
	bool moving = false;
	ModelVec3 moveOffset;
	bool resizing = false;
	ResizeBox resizeFrom;
	ResizeBox resizeTo;
	QHash<int, BoxResizePoint> resizeOrigins;
	// Moves and resizes act on the highlighted triangles.
	QVector<bool> highlighted;

	[[nodiscard]] bool matches(const GeometryKey& other) const;
};

// World-space corners for the whole mesh at one pose: three unshared corners
// a triangle, in the flattened triangle order, laid out for the model-surface
// program (position, uv, face plane, surface colour, info; 44 bytes).
struct ModelViewport::GpuGeometry {
	GeometryKey key;
	quint64 cacheKey = 0;
	QByteArray vertices;
	// Per triangle: unit face normal (agreeing with the format's vertex
	// normals) and its dot product with the first corner; and drawability.
	QVector<std::array<float, 4>> planes;
	QBitArray valid;
	// Bounding sphere of the drawable corners, for orthographic depth.
	ModelVec3 centre;
	double radius = 1.0;

	[[nodiscard]] ModelVec3 corner(int triangle, int index) const
	{
		const auto* floats = reinterpret_cast<const float*>(vertices.constData() + (qsizetype(triangle) * 3 + index) * 44);
		return ModelVec3 {floats[0], floats[1], floats[2]};
	}
};

// Selection flags a corner (highlight bit 0, selected edges bits 2..4).
struct ModelViewport::FlagsKey {
	quint64 mesh = 0;
	QVector<bool> highlighted;
	QSet<int> strokeTriangles;
	int edgeSelectionSurface = -1;
	QSet<QPair<int, int>> selectedEdges;

	[[nodiscard]] bool matches(const FlagsKey& other) const;
};

struct ModelViewport::GpuFlags {
	FlagsKey key;
	quint64 cacheKey = 0;
	QByteArray bytes;
};

struct ModelViewport::RasterWork {
	std::atomic_bool cancelled {false};
	// Value snapshots only. Workers never read the widget or GUI-owned state.
	ModelMesh mesh;
	QVector<MeshTriangle> meshTriangles;
	Camera camera;
	ModelVec3 center, eye, moveOffset;
	bool perspective = false, backfaceCulling = true, textured = false, wireframe = false, highContrast = false;
	bool showEdges = false;
	bool moving = false, editMoveActive = false, editTagEmpty = true;
	bool resizingSelection = false;
	ResizeBox resizeFrom, resizeTo;
	QHash<int, BoxResizePoint> resizeOrigins;
	int frame = 0, blendFrame = 0, editSurface = -1, edgeSelectionSurface = -1, hover = -1;
	double frameBlend = 0;
	ModelTransform editTransform;
	QSet<int> editVertices, editSurfaces, surfaceStrokeTriangles;
	QVector<bool> highlighted;
	QSet<QPair<int, int>> selectedEdges;
	QImage skin;
	bool skinHasAlpha = false;
	QHash<int, QImage> surfaceSkins;
	QSet<int> surfaceSkinAlpha;
	Palette palette;
	int visibleTriangles = 0, culledTriangles = 0;
	QVector<TagOverlay> tags;
	QVector<CollisionOverlay> collision;
	QSize logicalSize;
	// Physical pixels of the read-back frame and its pixels per logical pixel.
	QSize size;
	double pixelRatio = 1.0;
	ModelRenderFrame result;
	// GPU frame inputs: cached uploads are reused when their keys match.
	quint64 renderOwner = 0;
	GeometryKey geometryKey;
	std::shared_ptr<const GpuGeometry> geometry;
	FlagsKey flagsKey;
	std::shared_ptr<const GpuFlags> flags;
	QString error;
	QString errorDetail;
	QString deviceSummary;
	QImage vertexOverlay;
	std::shared_ptr<const ModelVertexProjection> vertices;
	QColor vertexAccent;
	bool vertexPicking = false, xrayVertices = false, reuseBase = false;
	quint64 revision = 0;
	quint64 baseRevision = 0;
	quint64 contentRevision = 0;
	quint64 projectionRevision = 0;
	bool success = false;

	ModelVec3 vertexPosition(int surface, int vertex) const
	{
		const auto& part = mesh.surfaces[surface];
		const auto& pose = part.frames[std::min(frame, int(part.frames.size()) - 1)];
		auto point = pose.positions[vertex];
		if (frameBlend > 0 && blendFrame < part.frames.size() && part.frames[blendFrame].positions.size() == pose.positions.size()) {
			point = interpolateModelPosition(point, part.frames[blendFrame].positions[vertex], frameBlend);
		}
		return editMoveActive && (editSurfaces.contains(surface) || (surface == editSurface && editVertices.contains(vertex)))
			? transformModelPoint(point, editTransform)
			: point;
	}
	void toView(const ModelVec3& point, double* x, double* y, double* z) const
	{
		const auto relative = model_viewport_detail::subtract(point, camera.position);
		*x = model_viewport_detail::dot(relative, camera.right);
		*y = model_viewport_detail::dot(relative, camera.up);
		*z = model_viewport_detail::dot(relative, camera.forward);
	}
	QPointF fromView(double x, double y, double z) const
	{
		const double depth = std::max(z, model_viewport_detail::kNearPlane);
		return {camera.origin.x() + camera.focal * x / depth, camera.origin.y() - camera.focal * y / depth};
	}
	QPointF projectPoint(const ModelVec3& point, double* depthOut) const
	{
		const auto relative = model_viewport_detail::subtract(point, center);
		if (depthOut) {
			*depthOut = model_viewport_detail::dot(relative, camera.eye);
		}
		return {camera.origin.x() + model_viewport_detail::dot(relative, camera.right) * camera.scale,
			camera.origin.y() - model_viewport_detail::dot(relative, camera.up) * camera.scale};
	}
	bool projectSegment(const ModelVec3& a, const ModelVec3& b, QPointF* screenA, QPointF* screenB) const
	{
		using model_viewport_detail::kNearPlane;
		if (!perspective) {
			*screenA = projectPoint(a, nullptr);
			*screenB = projectPoint(b, nullptr);
			return true;
		}
		double ax, ay, az, bx, by, bz;
		toView(a, &ax, &ay, &az);
		toView(b, &bx, &by, &bz);
		if (az < kNearPlane && bz < kNearPlane) {
			return false;
		}
		if (az < kNearPlane) {
			const double t = (kNearPlane - az) / (bz - az);
			ax += t * (bx - ax);
			ay += t * (by - ay);
			az = kNearPlane;
		} else if (bz < kNearPlane) {
			const double t = (kNearPlane - bz) / (az - bz);
			bx += t * (ax - bx);
			by += t * (ay - by);
			bz = kNearPlane;
		}
		*screenA = fromView(ax, ay, az);
		*screenB = fromView(bx, by, bz);
		return true;
	}
};

// The flat colour of a surface, as the viewport has always drawn it.
QColor modelSurfaceBaseColor(int surfaceIndex, bool highContrast);
// Whether any pixel of an image is not fully opaque.
bool modelSkinHasAlpha(const QImage& image);

} // namespace vibestudio
