#pragma once
#include "core/model_obj.h"
#include "core/model_rip.h"
#include "core/model_skeleton.h"
#include <QJsonDocument>

namespace vibestudio::tests
{
inline ModelMesh ripGrid(int side = 3)
{
	QByteArray obj;
	for (int y = 0; y < side; ++y)
		for (int x = 0; x < side; ++x)
			obj += QString("v %1 %2 0\nvt %3 %4\n")
					   .arg((x - side / 2) * 16)
					   .arg((y - side / 2) * 16)
					   .arg(double(x) / (side - 1))
					   .arg(double(y) / (side - 1))
					   .toUtf8();
	obj += "vn 0 0 1\n";
	for (int y = 0; y < side - 1; ++y)
		for (int x = 0; x < side - 1; ++x)
		{
			const int a = y * side + x + 1, b = a + 1, c = a + side, d = c + 1;
			for (const auto face : {std::array<int, 3>{a, b, d}, std::array<int, 3>{a, d, c}})
				obj += QString("f %1/%1/1 %2/%2/1 %3/%3/1\n").arg(face[0]).arg(face[1]).arg(face[2]).toUtf8();
		}
	auto mesh = decodeModelObj("rip-grid.obj", obj);
	mesh.frames.append(mesh.frames[0]);
	mesh.frames.last().name = "raised";
	mesh.surfaces[0].frames.append(mesh.surfaces[0].frames[0]);
	for (auto &p : mesh.surfaces[0].frames[1].positions)
	{
		p.x *= 1.25f;
		p.z = p.y * .125f;
	}
	updateEditableModelMetadata(&mesh);
	return mesh;
}
inline int ripVertex(const ModelMesh &mesh, float x, float y)
{
	for (int v = 0; v < mesh.surfaces[0].vertexCount; ++v)
	{
		const auto p = mesh.surfaces[0].frames[0].positions[v];
		if (p.x == x && p.y == y)
			return v;
	}
	return -1;
}
inline ModelEdit ripEdit(const ModelMesh &mesh, bool edge = false, bool fill = false)
{
	ModelEdit edit;
	edit.kind = ModelEditKind::Rip;
	const int centre = ripVertex(mesh, 0, 0);
	if (edge)
		edit.selection.edges.insert(modelEdge(centre, ripVertex(mesh, 0, -16)));
	else
		edit.selection.vertices.insert(centre);
	edit.translation = {4, 0, 0};
	edit.tool.ripFill = fill;
	for (int f = 0; f < mesh.surfaces[0].triangles.size(); ++f)
	{
		const auto t = mesh.surfaces[0].triangles[f];
		const auto &p = mesh.surfaces[0].frames[0].positions;
		if ((t.a == centre || t.b == centre || t.c == centre) && p[t.a].x + p[t.b].x + p[t.c].x > 0 && p[t.a].y + p[t.b].y + p[t.c].y < 0)
		{
			edit.tool.ripFace = f;
			break;
		}
	}
	return edit;
}
inline QByteArray ripBytes(const ModelMesh &mesh) { return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact); }
inline ModelMesh ripRig()
{
	auto mesh = ripGrid();
	mesh.frames.resize(1);
	mesh.frames[0].name = "bindpose";
	auto &surface = mesh.surfaces[0];
	surface.frames.resize(1);
	ModelJoint root, moving;
	root.name = "root";
	moving.name = "moving";
	root.bind = modelJointFromQuakeAngles(0, 0, 0, {2, 3, 0});
	moving.bind = modelJointFromQuakeAngles(0, 15, 0, {-3, 4, 2});
	mesh.skeleton.joints = {root, moving};
	ModelSkeletalClip clip;
	clip.name = "bend";
	clip.frames = {{root.bind, modelJointFromQuakeAngles(0, 30, 0, {-2, 4, 4})}};
	mesh.skeleton.clips = {clip};
	for (int v = 0; v < surface.vertexCount; ++v)
	{
		surface.skinning.first.append(surface.skinning.influences.size());
		surface.skinning.count.append(2);
		for (int j = 0; j < 2; ++j)
		{
			const auto inverse = modelJointInverse(mesh.skeleton.joints[j].bind);
			auto p = surface.frames[0].positions[v];
			p.x += j == 0 ? 3.f : -1.f;
			surface.skinning.influences.append({j, j == 0 ? .25f : .75f, modelJointTransformPoint(inverse, p),
												modelJointTransformVector(inverse, surface.frames[0].normals[v])});
		}
	}
	QString error;
	if (!bakeModelSkeleton(&mesh, {}, &error))
		return {};
	updateEditableModelMetadata(&mesh);
	return mesh;
}
inline ModelMesh ripSeams()
{
	auto mesh = ripGrid();
	auto &s = mesh.surfaces[0];
	const auto before = s;
	s.texCoords.clear();
	s.triangles.clear();
	s.uvSeams.clear();
	for (auto &frame : s.frames)
	{
		frame.positions.clear();
		frame.normals.clear();
	}
	for (int f = 0; f < before.triangles.size(); ++f)
	{
		const auto t = before.triangles[f];
		const int base = s.texCoords.size();
		for (int v : {t.a, t.b, t.c})
		{
			auto uv = before.texCoords[v];
			uv.u += float(f);
			s.texCoords.append(uv);
			for (int pose = 0; pose < s.frames.size(); ++pose)
			{
				s.frames[pose].positions.append(before.frames[pose].positions[v]);
				s.frames[pose].normals.append(before.frames[pose].normals[v]);
			}
		}
		s.triangles.append({base, base + 1, base + 2});
		s.uvSeams << modelEdge(base, base + 1) << modelEdge(base + 1, base + 2) << modelEdge(base, base + 2);
	}
	s.vertexCount = s.texCoords.size();
	updateEditableModelMetadata(&mesh);
	return mesh;
}
} // namespace vibestudio::tests
