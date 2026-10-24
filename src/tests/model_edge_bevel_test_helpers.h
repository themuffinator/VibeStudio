#pragma once

#include "core/model_design.h"
#include "core/model_document.h"
#include "core/model_geometric_topology.h"
#include "core/model_geometry_helpers.h"

#include <QJsonDocument>
#include <algorithm>

namespace vibestudio::tests
{
inline ModelMesh bevelCube()
{
	ModelDesign design;
	design.parts.append(ModelDesignPart{});
	auto mesh = buildModelDesignMesh(design);
	mesh.frames.append(mesh.frames.first());
	mesh.frames.last().name = "turned";
	auto &s = mesh.surfaces[0];
	s.frames.append(s.frames.first());
	for (auto &p : s.frames[1].positions)
		p = {-p.y * 1.5f + 10, p.x * 2, p.z * .75f + 3};
	for (auto &n : s.frames[1].normals)
		n = {-n.y, n.x, n.z};
	updateEditableModelMetadata(&mesh);
	return mesh;
}
inline QVector<ModelEdge> bevelCubeEdges(const ModelMesh &mesh)
{
	ModelQuadTopology topology;
	buildModelQuadTopology(mesh.surfaces[0], 0, 40, &topology);
	QVector<ModelEdge> edges;
	for (auto i = topology.sideUses.cbegin(); i != topology.sideUses.cend(); ++i)
	{
		const auto [element, side] = i.value().first();
		const auto &e = topology.elements[element];
		edges.append(modelEdge(e.corners[side], e.corners[(side + 1) % e.sides()]));
	}
	std::sort(edges.begin(), edges.end());
	return edges;
}
inline ModelEdit bevelCubeEdit(const ModelMesh &mesh, int edges = 1, int segments = 1)
{
	ModelEdit edit;
	edit.kind = ModelEditKind::BevelEdges;
	edit.tool.bevelWidth = 8;
	edit.tool.bevelSegments = segments;
	ModelGeometricTopology topology;
	buildModelGeometricTopology(mesh.surfaces[0], &topology);
	const auto available = bevelCubeEdges(mesh);
	const int root = topology.group(available.first().first);
	for (auto edge : available)
		if (edges > 3 || topology.group(edge.first) == root || topology.group(edge.second) == root)
		{
			edit.selection.edges.insert(edge);
			if (edit.selection.edges.size() == edges)
				break;
		}
	return edit;
}
inline QByteArray bevelBytes(const ModelMesh &mesh)
{
	return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact);
}
inline bool closedBevel(const ModelMesh &mesh)
{
	using namespace model_geometry;
	const auto &s = mesh.surfaces[0];
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(s, &topology))
		return false;
	QHash<ModelEdge, int> balance;
	for (const auto &t : s.triangles)
	{
		for (auto edge : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)})
		{
			const int a = topology.group(edge.first), b = topology.group(edge.second);
			balance[modelEdge(a, b)] += a < b ? 1 : -1;
		}
		for (const auto &f : s.frames)
			if (collapsedTriangle(f.positions[t.a], f.positions[t.b], f.positions[t.c]))
				return false;
	}
	for (auto i = topology.edgeFaces.cbegin(); i != topology.edgeFaces.cend(); ++i)
		if (i.value().size() != 2 || balance.value(i.key()) != 0)
			return false;
	int vertices = 0;
	for (int i = 0; i < s.vertexCount; ++i)
		if (!topology.copies[i].isEmpty() && !topology.groupFaces[i].isEmpty())
			++vertices;
	return vertices - topology.edgeFaces.size() + s.triangles.size() == 2;
}
} // namespace vibestudio::tests
