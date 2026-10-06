#pragma once

#include "core/model_player_bundle.h"
#include "core/texture_export.h"
#include "tests/model_q3_animation_test_helpers.h"

#include <QBuffer>

namespace vibestudio::tests
{
struct PlayerBundleFixture
{
	ModelAssembly assembly;
	ModelAssemblyContext context;
	ModelAssemblyResolved resolved;
	ModelPlayerBundleOptions options;
	QString sourcePath, iconPath, directory;
	QByteArray texture;
	bool create(const QString &root, QString *error)
	{
		directory = root;
		const QDir dir(root);
		if (!QDir().mkpath(dir.filePath("assets/textures/player")) || !QDir().mkpath(dir.filePath("assets/scripts")))
			return false;
		QImage image(32, 32, QImage::Format_ARGB32);
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 32; ++x)
				image.setPixelColor(x, y, QColor(x < 16 ? 240 : 30, y < 16 ? 220 : 40, 80));
		TextureExportOptions tga;
		tga.format = TextureExportFormat::Targa;
		texture = encodeTextureExport(image, tga).bytes;
		iconPath = dir.filePath("icon.png");
		QByteArray png;
		QBuffer buffer(&png);
		buffer.open(QIODevice::WriteOnly);
		if (!image.save(&buffer, "PNG") || !q3Write(iconPath, png) || !q3Write(dir.filePath("assets/textures/player/a.tga"), texture) ||
			!q3Write(dir.filePath("assets/textures/player/b.tga"), texture) ||
			!q3Write(dir.filePath("assets/scripts/player.shader"),
					 "models/player/body\n{\n cull none\n {\n  animMap 2 textures/player/a.tga textures/player/b.tga\n  rgbGen identity\n "
					 "}\n}\n"))
			return false;
		for (const auto &role : {QStringLiteral("lower"), QStringLiteral("upper"), QStringLiteral("head")})
		{
			auto model = q3Model();
			model.surfaces[0].name = "body_1";
			model.surfaces[0].skinPaths = {role == "lower" ? QStringLiteral("models/unassigned") : QStringLiteral("textures/player/a.tga"),
										   QStringLiteral("textures/player/b.tga")};
			if (role == "upper")
				for (auto &tag : model.tags)
					tag.name = "tag_head";
			if (role == "head")
			{
				model.frames.resize(1);
				model.surfaces[0].frames.resize(1);
				model.tags.clear();
				model.animations.clear();
			}
			updateEditableModelMetadata(&model);
			if (!q3Write(dir.filePath(role + ".mesh.json"), QJsonDocument(editableModelJson(model)).toJson()))
				return false;
		}
		if (!q3Write(dir.filePath("lower.skin"), "body,models/player/body\n"))
			return false;
		assembly = q3Assembly("lower.mesh.json");
		assembly.parts[0].source = "upper.mesh.json";
		assembly.parts[0].translation = {2, -3, 4};
		assembly.parts[0].rotation = {10, 5, -12};
		assembly.parts[0].scale = .8;
		assembly.parts[1].translation = {-5, 2, 3};
		assembly.parts[1].rotation = {7, -11, 23};
		assembly.parts[1].scale = 1.25;
		assembly.parts[1].skin = ModelAssemblySkin{"lower.skin"};
		ModelAssemblyPart head;
		head.id = "head";
		head.source = "head.mesh.json";
		head.parent = "upper";
		head.tag = "tag_head";
		head.translation = {1, 2, -1};
		head.rotation = {-9, 17, 5};
		head.scale = 1.1;
		assembly.parts.append(head);
		auto archive = std::make_shared<PackageArchive>();
		if (!archive->load(dir.filePath("assets"), error))
			return false;
		context = {root, archive, {}};
		options = {"synthetic", "default", "head", iconPath};
		sourcePath = dir.filePath("player.assembly.json");
		return q3Write(sourcePath, QJsonDocument(modelAssemblyJson(assembly)).toJson()) &&
			   resolveModelAssembly(assembly, context, &resolved, error);
	}
};
} // namespace vibestudio::tests
