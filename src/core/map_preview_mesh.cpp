#include "core/map_preview_mesh.h"
#include "core/level_model_appearance.h"
#include "core/level_scene.h"
#include "core/doom_preview_geometry.h"

#include "core/map_geometry.h"
#include "core/map_geometry_cache.h"
#include "core/level_texture_mapping.h"

#include <QCoreApplication>
#include <QHash>
#include <QMatrix4x4>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio {

namespace {

// Collects triangles into one surface per texture name, stopping at the
// triangle limit.
class MeshBuilder {
public:
	MeshBuilder(ModelMesh* mesh, int limit)
		: m_mesh(mesh)
		, m_limit(limit)
	{
	}

	// A convex polygon, as a fan from its first corner, facing `normal`. False
	// once the limit is reached.
	bool addPolygon(const QString& texture, const QVector<LevelMapVec3>& points, const LevelMapVec3& normal, const LevelMapSelectionRef& owner,
		int face = -1, const QVector<QPointF>& uv = {}, LevelMaterialTarget material = {})
	{
		if (owner.kind == LevelMapSelectionKind::QuakeBrush && face >= 0) { material = {LevelMaterialKind::BrushFace, owner.objectId, face}; }
		if (owner.kind == LevelMapSelectionKind::QuakePatch) { material = {LevelMaterialKind::Patch, owner.objectId}; }
		if (points.size() < 3) {
			return true;
		}
		const int count = static_cast<int>(points.size()) - 2;
		if (m_triangles + count > m_limit) {
			m_truncated = true;
			return false;
		}
		ModelSurface& surface = surfaceFor(texture);
		const int base = surface.vertexCount;
		for (int i = 0; i < points.size(); ++i) {
			const LevelMapVec3& point = points.at(i);
			surface.frames.first().positions.push_back(toModel(point));
			surface.frames.first().normals.push_back(toModel(normal));
			surface.texCoords.push_back({static_cast<float>(uv.value(i).x()), static_cast<float>(uv.value(i).y())});
			include(point);
		}
		surface.vertexCount += static_cast<int>(points.size());
		for (int corner = 1; corner + 1 < points.size(); ++corner) {
			surface.triangles.push_back({base, base + corner, base + corner + 1});
			m_owners[surface.index].push_back(owner);
			m_faces[surface.index].push_back(face);
			m_materials[surface.index].push_back(material);
		}
		m_triangles += count;
		return true;
	}

	[[nodiscard]] int triangles() const
	{
		return m_triangles;
	}

	[[nodiscard]] bool truncated() const
	{
		return m_truncated;
	}

	// The owners in the order a viewer flattens the triangles.
	[[nodiscard]] QVector<LevelMapSelectionRef> flattenedOwners() const
	{
		QVector<LevelMapSelectionRef> owners;
		owners.reserve(m_triangles);
		for (const ModelSurface& surface : m_mesh->surfaces) {
			owners += m_owners.value(surface.index);
		}
		return owners;
	}

	// The face of each triangle, in the same order.
	[[nodiscard]] QVector<int> flattenedFaces() const
	{
		QVector<int> faces;
		faces.reserve(m_triangles);
		for (const ModelSurface& surface : m_mesh->surfaces) {
			faces += m_faces.value(surface.index);
		}
		return faces;
	}

	[[nodiscard]] QVector<LevelMaterialTarget> flattenedMaterials() const
	{
		QVector<LevelMaterialTarget> targets;
		targets.reserve(m_triangles);
		for (const auto& surface : m_mesh->surfaces) { targets += m_materials.value(surface.index); }
		return targets;
	}

	void finish()
	{
		int vertices = 0;
		for (const ModelSurface& surface : m_mesh->surfaces) {
			vertices += surface.vertexCount;
		}
		m_mesh->surfaceCount = static_cast<int>(m_mesh->surfaces.size());
		m_mesh->vertexCount = vertices;
		m_mesh->triangleCount = m_triangles;
		m_mesh->frameCount = 1;
		ModelFrameInfo frame;
		frame.index = 0;
		frame.name = QCoreApplication::translate("VibeStudioMapPreview", "map");
		frame.mins = m_mesh->mins;
		frame.maxs = m_mesh->maxs;
		m_mesh->frames = {frame};
		m_mesh->geometryAvailable = m_triangles > 0;
	}

private:
	static ModelVec3 toModel(const LevelMapVec3& point)
	{
		return ModelVec3 {static_cast<float>(point.x), static_cast<float>(point.y), static_cast<float>(point.z)};
	}

	ModelSurface& surfaceFor(const QString& texture)
	{
		const QString name = texture.trimmed().isEmpty() ? QCoreApplication::translate("VibeStudioMapPreview", "(no texture)") : texture.trimmed();
		auto found = m_surfaces.constFind(name.toLower());
		if (found == m_surfaces.cend()) {
			ModelSurface surface;
			surface.index = static_cast<int>(m_mesh->surfaces.size());
			surface.name = name;
			surface.frames.resize(1);
			m_mesh->surfaces.push_back(surface);
			found = m_surfaces.insert(name.toLower(), surface.index);
		}
		return m_mesh->surfaces[*found];
	}

	void include(const LevelMapVec3& point)
	{
		const ModelVec3 value = toModel(point);
		if (!m_hasBounds) {
			m_mesh->mins = value;
			m_mesh->maxs = value;
			m_hasBounds = true;
			return;
		}
		m_mesh->mins = {std::min(m_mesh->mins.x, value.x), std::min(m_mesh->mins.y, value.y), std::min(m_mesh->mins.z, value.z)};
		m_mesh->maxs = {std::max(m_mesh->maxs.x, value.x), std::max(m_mesh->maxs.y, value.y), std::max(m_mesh->maxs.z, value.z)};
	}

	ModelMesh* m_mesh = nullptr;
	QHash<QString, int> m_surfaces;
	QHash<int, QVector<LevelMapSelectionRef>> m_owners;
	QHash<int, QVector<int>> m_faces;
	QHash<int, QVector<LevelMaterialTarget>> m_materials;
	int m_limit = 0;
	int m_triangles = 0;
	bool m_truncated = false;
	bool m_hasBounds = false;
};

// The facing of a polygon from its winding, (b - a) x (c - a), at unit
// length: what a viewer would work out itself.
LevelMapVec3 windingNormal(const QVector<LevelMapVec3>& points)
{
	if (points.size() < 3) {
		return {0.0, 0.0, 1.0, true};
	}
	const LevelMapVec3& a = points.at(0);
	const LevelMapVec3& b = points.at(1);
	const LevelMapVec3& c = points.at(2);
	const LevelMapVec3 u {b.x - a.x, b.y - a.y, b.z - a.z, true};
	const LevelMapVec3 v {c.x - a.x, c.y - a.y, c.z - a.z, true};
	LevelMapVec3 n {u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x, true};
	const double length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
	if (length > 0.0) {
		n.x /= length;
		n.y /= length;
		n.z /= length;
	}
	return n;
}

} // namespace

QString levelModelSurfaceMaterialKey(const QString& modelPath, int surfaceIndex)
{
	return modelPath.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/')).toCaseFolded() + QStringLiteral(":surface:%1").arg(surfaceIndex);
}

LevelMapPreviewMesh buildLevelMapPreviewMesh(const LevelMapDocument& document, const LevelMapPreviewMeshOptions& options)
{
	LevelMapPreviewMesh preview;
	const auto hidden = levelSceneHiddenObjects(document);
	ModelMesh& mesh = preview.mesh;
	mesh.sourcePath = document.sourcePath;
	mesh.formatId = QStringLiteral("level-map-preview");
	mesh.formatName = QCoreApplication::translate("VibeStudioMapPreview", "Level map preview");
	MeshBuilder builder(&mesh, std::max(0, options.triangleLimit));
	if (options.brushGeometryCache) { options.brushGeometryCache->beginBuild(document); }
	const auto cancelled = [&] {
		if (options.isCancelled && options.isCancelled()) { preview.cancelled = true; return true; }
		return false;
	};

	if (document.format == LevelMapFormat::DoomWad) {
		const auto doom = buildDoomPreviewGeometry(document, options);
		preview.walls = doom.walls; preview.floors = doom.floors; preview.ceilings = doom.ceilings;
		preview.warnings = doom.warnings; preview.truncated = doom.truncated; preview.cancelled = doom.cancelled;
		for (const auto& polygon : doom.polygons) {
			if (cancelled() || !builder.addPolygon(polygon.material, polygon.points, polygon.normal, polygon.owner, -1, polygon.uv, polygon.target)) { break; }
		}
	} else {
		bool room = true;
		for (const auto& sourceBrush : document.brushes) {
			if (hidden.contains(levelMapSelectionRefId({LevelMapSelectionKind::QuakeBrush, sourceBrush.id}))) { continue; }
			if (cancelled() || !room) { break; }
			const MapBrushGeometry brush = options.brushGeometryCache
				? options.brushGeometryCache->resolve(sourceBrush, MapGeometryPrecision::CompilerCompatible, options.isCancelled)
				: solveBrushGeometry(sourceBrush.faces, sourceBrush.id, sourceBrush.entityId, MapGeometryPrecision::CompilerCompatible, options.isCancelled);
			if (brush.cancelled) { preview.cancelled = true; break; }
			if (!brush.solved || !room) {
				continue;
			}
			for (int face = 0; face < brush.faces.size(); ++face) {
				const MapFacePolygon& polygon = brush.faces.at(face);
				if (!polygon.isValid()) {
					continue;
				}
				++preview.brushFaces;
				const LevelMapVec3 outward {polygon.plane.normalX, polygon.plane.normalY, polygon.plane.normalZ, true};
				QVector<QPointF> uv;
				const int sourceFace = polygon.faceIndex;
				if (sourceFace >= 0 && sourceFace < sourceBrush.faces.size()) {
					const auto projection = levelTextureProjection(sourceBrush.faces[sourceFace]);
					const auto size = options.textureSizes.value(polygon.textureName.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/')).toCaseFolded());
					if (projection.valid && (projection.normalizedCoordinates || (size.width() > 0 && size.height() > 0))) {
						for (const auto& point : polygon.points) {
							const auto at = projection.at(point);
							uv << (projection.normalizedCoordinates ? at : QPointF(at.x() / size.width(), at.y() / size.height()));
						}
					}
				}
				if (!builder.addPolygon(polygon.textureName, polygon.points, outward, {LevelMapSelectionKind::QuakeBrush, brush.brushId}, sourceFace, uv)) {
					room = false;
					break;
				}
			}
		}
		for (const LevelMapPatch& patch : document.patches) {
			if (hidden.contains(levelMapSelectionRefId({LevelMapSelectionKind::QuakePatch, patch.id}))) { continue; }
			if (!room || cancelled()) {
				break;
			}
			const QVector<QVector<LevelMapVec3>> grid = tessellatePatchMesh(patch, 3);
			const auto uv = tessellatePatchTexCoords(patch, 3);
			if (grid.size() < 2) {
				continue;
			}
			++preview.patches;
			for (int row = 0; row + 1 < grid.size() && room; ++row) {
				const int columns = static_cast<int>(std::min(grid.at(row).size(), grid.at(row + 1).size()));
				for (int column = 0; column + 1 < columns && room; ++column) {
					const QVector<LevelMapVec3> quad {grid.at(row).at(column), grid.at(row).at(column + 1), grid.at(row + 1).at(column + 1),
						grid.at(row + 1).at(column)};
					QVector<QPointF> corners;
					if (uv.size() == grid.size() && uv.at(row).size() == grid.at(row).size() && uv.at(row + 1).size() == grid.at(row + 1).size()) {
						corners = {uv.at(row).at(column), uv.at(row).at(column + 1), uv.at(row + 1).at(column + 1), uv.at(row + 1).at(column)};
					}
					room = builder.addPolygon(patch.textureName, quad, windingNormal(quad), {LevelMapSelectionKind::QuakePatch, patch.id}, -1, corners);
				}
			}
		}
		for (const auto& entity : document.entities) {
			if (hidden.contains(levelMapSelectionRefId({LevelMapSelectionKind::Entity, entity.id}))) { continue; }
			if (!room || cancelled()) { break; }
			const auto property = [&entity](const QString& key) {
				for (const auto& item : entity.properties) { if (item.key.compare(key, Qt::CaseInsensitive) == 0) { return item.value; } }
				return QString();
			};
			const QString path = levelModelAppearance(document, entity).cacheKey;
			const auto found = options.modelMeshes.constFind(path);
			if (found == options.modelMeshes.cend() || !found->geometryAvailable) { continue; }
			QMatrix4x4 transform;
			transform.translate(entity.origin.x, entity.origin.y, entity.origin.z);
			const auto angles = property(QStringLiteral("angles")).split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
			if (angles.size() == 3) {
				transform.rotate(angles[1].toFloat(), 0, 0, 1); transform.rotate(angles[0].toFloat(), 0, 1, 0); transform.rotate(angles[2].toFloat(), 1, 0, 0);
			} else { transform.rotate(property(QStringLiteral("angle")).toFloat(), 0, 0, 1); }
			const auto scales = property(QStringLiteral("modelscale_vec")).split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
			if (scales.size() == 3) { transform.scale(scales[0].toFloat(), scales[1].toFloat(), scales[2].toFloat()); }
			else { bool ok = false; const float scale = property(QStringLiteral("modelscale")).toFloat(&ok); if (ok) { transform.scale(scale); } }
			++preview.modelInstances;
			for (const auto& surface : found->surfaces) {
				if (!room || surface.frames.isEmpty()) { break; }
				const auto& vertices = surface.frames[0].positions;
				for (const auto& triangle : surface.triangles) {
					QVector<LevelMapVec3> polygon;
					QVector<QPointF> uv;
					for (int index : {triangle.a, triangle.b, triangle.c}) {
						if (index < 0 || index >= vertices.size()) { break; }
						const auto& v = vertices[index]; const auto p = transform.map(QVector3D(v.x, v.y, v.z));
						if (!std::isfinite(p.x()) || !std::isfinite(p.y()) || !std::isfinite(p.z())) { break; }
						polygon << LevelMapVec3 {p.x(), p.y(), p.z(), true};
						const auto st = surface.texCoords.value(index);
						uv << QPointF(st.u, st.v);
					}
					if (polygon.size() != 3) { continue; }
					room = builder.addPolygon(levelModelSurfaceMaterialKey(path, surface.index), polygon, windingNormal(polygon), {LevelMapSelectionKind::Entity, entity.id}, -1, uv);
					if (!room) { break; }
				}
			}
		}
	}
	builder.finish();
	preview.owners = builder.flattenedOwners();
	preview.ownerFaces = builder.flattenedFaces();
	preview.materialTargets = builder.flattenedMaterials();
	preview.triangles = builder.triangles();
	preview.truncated = preview.truncated || builder.truncated();
	return preview;
}

} // namespace vibestudio
