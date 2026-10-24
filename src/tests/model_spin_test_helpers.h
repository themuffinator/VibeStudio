#pragma once

#include "core/model_document.h"
#include "core/model_geometric_topology.h"
#include "core/model_geometry_helpers.h"
#include "core/model_obj.h"
#include <QJsonDocument>

namespace vibestudio::tests
{
inline ModelMesh spinProfile(bool poles = false)
{
	const auto obj = QStringLiteral("v %1 0 0\nv 24 0 0\nv 24 0 8\nv %1 0 8\n"
									"vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 1 0\n"
									"f 1/1/1 3/3/1 2/2/1\nf 1/1/1 4/4/1 3/3/1\n")
						 .arg(poles ? 0 : 16);
	auto mesh = decodeModelObj("profile.obj", obj.toUtf8());
	mesh.frames.append(mesh.frames.first());
	mesh.frames.last().name = "raised";
	mesh.surfaces[0].frames.append(mesh.surfaces[0].frames.first());
	for (auto &p : mesh.surfaces[0].frames[1].positions)
	{
		p.x *= 1.25f;
		p.z = p.z * 1.5f + 4;
	}
	updateEditableModelMetadata(&mesh);
	return mesh;
}
inline ModelEdit spinEdit(double angle = 90, int steps = 8)
{
	ModelEdit edit;
	edit.kind = ModelEditKind::Spin;
	edit.selection.faces = {0, 1};
	edit.tool.spinAngle = angle;
	edit.tool.spinSteps = steps;
	return edit;
}
inline QByteArray spinBytes(const ModelMesh &mesh) { return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact); }
inline bool spinClosed(const ModelSurface &surface, int euler)
{
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology))
		return false;
	QHash<ModelEdge, int> balance;
	for (const auto &t : surface.triangles)
	{
		for (auto edge : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)})
		{
			const int a = topology.group(edge.first), b = topology.group(edge.second);
			balance[modelEdge(a, b)] += a < b ? 1 : -1;
		}
		for (const auto &frame : surface.frames)
			if (model_geometry::collapsedTriangle(frame.positions[t.a], frame.positions[t.b], frame.positions[t.c]))
				return false;
	}
	for (auto i = topology.edgeFaces.cbegin(); i != topology.edgeFaces.cend(); ++i)
		if (i->size() != 2 || balance[i.key()] != 0)
			return false;
	int vertices = 0;
	for (int v = 0; v < surface.vertexCount; ++v)
		if (!topology.groupFaces[v].isEmpty())
			++vertices;
	return vertices - topology.edgeFaces.size() + surface.triangles.size() == euler;
}
} // namespace vibestudio::tests
