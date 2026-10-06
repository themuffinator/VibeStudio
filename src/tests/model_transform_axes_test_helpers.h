#pragma once

#include "tests/model_uv_test_helpers.h"
#include <QJsonDocument>
#include <algorithm>
#include <cmath>

namespace vibestudio::tests
{
inline ModelMesh transformAxesFixture()
{
	auto mesh = uvSquare();
	auto &surface = mesh.surfaces[0];
	surface.frames[0].positions = {{3, 4, 5}, {3, 14, 5}, {3, 14, 15}, {3, 4, 15}};
	surface.frames[0].normals.fill({1, 0, 0}, 4);
	surface.frames[1].positions = {{23, 4, 5}, {33, 4, 5}, {33, 14, 5}, {23, 14, 5}};
	surface.frames[1].normals.fill({0, 0, 1}, 4);
	const float axes[]{0, 1, 0, 0, 0, 1, 1, 0, 0};
	std::copy(std::begin(axes), std::end(axes), mesh.tags[0].axis);
	mesh.collisionBoxes = {{QStringLiteral("body"), {8, 9, 10}, {2, 4, 6}, {0, 0, 90}}};
	updateEditableModelMetadata(&mesh);
	return mesh;
}
inline bool nearAxesVector(ModelVec3 a, ModelVec3 b, double tolerance = .0001)
{
	return std::abs(double(a.x) - b.x) < tolerance && std::abs(double(a.y) - b.y) < tolerance && std::abs(double(a.z) - b.z) < tolerance;
}
inline QByteArray axesMeshBytes(const ModelMesh &mesh)
{
	return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact);
}
inline bool nearAxesVectors(const auto &a, const auto &b)
{
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](ModelVec3 x, ModelVec3 y) { return nearAxesVector(x, y); });
}
} // namespace vibestudio::tests
