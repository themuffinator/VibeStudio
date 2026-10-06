#pragma once

#include "core/level_document.h"
#include "core/level_materials.h"
#include "core/level_patch.h"
#include "core/model_design.h"
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace vibestudio::tests
{

inline QByteArray materialImage(int width, int height, bool alternate = false)
{
	QImage image(width, height, QImage::Format_RGB32);
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			const bool cell = ((x / 8) + (y / 8)) % 2 != 0;
			image.setPixelColor(x, y, cell ? QColor(alternate ? "#d65c7c" : "#54c6da") : QColor("#25303e"));
		}
	}
	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	image.save(&buffer, "PNG");
	return bytes;
}
inline bool putMaterialFile(const QString &path, const QByteArray &bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
inline bool createMaterialFixture(const QString &root, LevelMapDocument *map, QString *error)
{
	const auto file = [&](const QString &path, const QByteArray &bytes) {
		return putMaterialFile(QDir(root).filePath(QStringLiteral("assets/") + path), bytes);
	};
	bool ok = file(QStringLiteral("textures/studio/grid.png"), materialImage(128, 64));
	ok &= file(QStringLiteral("textures/studio/editor.png"), materialImage(64, 32, true));
	ok &= file(QStringLiteral("textures/studio/stage.png"), materialImage(32, 64));
	ok &= file(QStringLiteral("scripts/studio.shader"),
			   "textures/studio/shader\n{\nqer_editorimage textures/studio/editor.png\n{ map textures/studio/stage.png }\n}\n"
			   "textures/studio/animated\n{\n{ map $lightmap }\n{ animMap 2 textures/studio/stage.png textures/studio/editor.png }\n}\n");
	ModelDesign design;
	ModelDesignPart part;
	part.material = QStringLiteral("textures/studio/shader");
	part.size = {32, 32, 48};
	design.parts = {part};
	ok &= file(QStringLiteral("models/studio/prop.md3"), exportModelDesign(design, QStringLiteral("md3"), error));
	LevelMapCreateRequest create;
	create.starterRoom = false;
	ok &= createLevelMap(create, map, error);
	ok &= addLevelMapBoxBrush(map, {-64, -64, -16, true}, {64, 64, 0, true}, QStringLiteral("studio/grid"));
	ok &= addLevelMapBoxBrush(map, {40, -64, 0, true}, {64, 64, 80, true}, QStringLiteral("studio/shader"));
	LevelPatchCreateRequest patchRequest;
	patchRequest.center = {-24, 16, 30, true};
	patchRequest.size = {48, 48, 48, true};
	patchRequest.texture = QStringLiteral("studio/animated");
	LevelMapPatch patch;
	ok &= createLevelPatch(patchRequest, &patch, error) && addLevelMapPatch(map, patch, nullptr, error);
	ok &= placeLevelModel(map, QStringLiteral("models/studio/prop.md3"), {-24, -28, 24, true}, nullptr, error);
	return ok;
}
inline const LevelPreviewMaterial *materialNamed(const LevelPreviewAssets &assets, const QString &key)
{
	for (const auto &material : assets.materials) {
		if (material.key == key) {
			return &material;
		}
	}
	return nullptr;
}
} // namespace vibestudio::tests
