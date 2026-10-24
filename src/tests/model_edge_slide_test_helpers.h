#pragma once
#include "core/model_design.h"
#include "core/model_document.h"
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
#include <numbers>
namespace vibestudio::tests
{
inline bool sameSlideUvs(const QVector<ModelTexCoord> &a, const QVector<ModelTexCoord> &b)
{
	return std::equal(a.cbegin(), a.cend(), b.cbegin(), b.cend(), [](const auto &x, const auto &y) { return x.u == y.u && x.v == y.v; });
}
inline ModelMesh edgeSlideGrid(int columns = 4, int rows = 4)
{
	ModelDesign design;
	ModelDesignPart part;
	part.primitive = "plane";
	design.parts.append(part);
	auto mesh = buildModelDesignMesh(design);
	mesh.frames.append(mesh.frames[0]);
	mesh.frames[1].name = "stretched";
	auto &s = mesh.surfaces[0];
	s.triangles.clear();
	s.texCoords.clear();
	s.uvSeams.clear();
	ModelFrameGeometry pose;
	for (int y = 0; y <= rows; ++y)
		for (int x = 0; x <= columns; ++x)
		{
			pose.positions.append({float(x * 8), float(y * 8), 0});
			pose.normals.append({0, 0, 1});
			s.texCoords.append({float(x) / columns, float(y) / rows});
		}
	const auto at = [columns](int x, int y) { return y * (columns + 1) + x; };
	for (int y = 0; y < rows; ++y)
		for (int x = 0; x < columns; ++x)
		{
			s.triangles.append({at(x, y), at(x + 1, y), at(x + 1, y + 1)});
			s.triangles.append({at(x, y), at(x + 1, y + 1), at(x, y + 1)});
		}
	s.frames = {pose, pose};
	for (auto &p : s.frames[1].positions)
	{
		p.x *= 2;
		p.z = 8;
	}
	s.vertexCount = pose.positions.size();
	updateEditableModelMetadata(&mesh);
	return mesh;
}
inline ModelEdit edgeSlideEdit(int columns = 4, int rows = 4, int column = 2)
{
	ModelEdit edit;
	edit.kind = ModelEditKind::SlideEdges;
	edit.tool.slide = .25;
	for (int y = 0; y < rows; ++y)
		edit.selection.edges.insert(modelEdge(y * (columns + 1) + column, (y + 1) * (columns + 1) + column));
	return edit;
}
inline QByteArray slideBytes(const ModelMesh &mesh)
{
	return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact);
}
inline ModelMesh edgeSlideTube()
{
	auto mesh = edgeSlideGrid(8, 2);
	auto &s = mesh.surfaces[0];
	for (int f = 0; f < s.frames.size(); ++f)
		for (int y = 0; y < 3; ++y)
			for (int x = 0; x <= 8; ++x)
			{
				const double angle = (x % 8) * std::numbers::pi / 4;
				s.frames[f].positions[y * 9 + x] = {float(std::cos(angle) * 16), float(std::sin(angle) * 16), float(y * 8 + f * 2)};
				s.frames[f].normals[y * 9 + x] = {float(std::cos(angle)), float(std::sin(angle)), 0};
			}
	updateEditableModelMetadata(&mesh);
	return mesh;
}
} // namespace vibestudio::tests
