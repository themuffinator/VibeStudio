#pragma once
#include "tests/model_intersections_test_helpers.h"
#include <QFile>
#include <QJsonDocument>

namespace vibestudio::tests
{
inline ModelMesh damagedImportFixture()
{
	auto mesh = intersectionFixture(true);
	mesh.sourcePath = QStringLiteral("tests/repair-import.mesh.json");
	auto &surface = mesh.surfaces[0];
	surface.texCoords.append({.2f, .3f});
	for (int pose = 0; pose < 3; ++pose)
	{
		surface.frames[pose].positions.append(pose == 2 ? ModelVec3{0, -1, 0} : ModelVec3{0, 0, 2});
		surface.frames[pose].normals.append({0, 1, 0});
	}
	surface.triangles += {{0, 0, 1}, {-1, 0, 1}, {999, 1, 2}, {3, 4, 6}};
	surface.frames[0].normals[0] = {};
	surface.frames[0].normals[1] = {0, 0, 2.5f};
	surface.frames[1].normals[4] = {1e-8f, 0, 0};
	surface.frames[2].normals[6] = {};
	surface.uvSeams.insert({3, 6});
	surface.uvSeams.insert({1, 99});
	mesh.frames[1].origin = {2, 3, 4};
	updateEditableModelMetadata(&mesh);
	return mesh;
}
inline ModelMesh expectedImportRepair()
{
	auto mesh = damagedImportFixture();
	auto &surface = mesh.surfaces[0];
	surface.triangles.resize(2);
	surface.frames[0].normals[0] = {0, 0, 1};
	surface.frames[1].normals[4] = {-1, 0, 0};
	surface.frames[2].normals[6] = {0, 0, 1};
	surface.uvSeams = {{0, 1}};
	updateEditableModelMetadata(&mesh);
	return mesh;
}
inline QByteArray importRepairSource(const ModelMesh &mesh)
{
	return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact);
}
inline bool writeImportRepairFixture(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
inline QByteArray readImportRepairFixture(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
} // namespace vibestudio::tests
