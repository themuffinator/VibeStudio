#pragma once

#include "tests/model_skin_binding_test_helpers.h"
#include "tests/model_skin_source_test_helpers.h"
#include "core/texture_export.h"

#include <QDir>
#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMap>

namespace vibestudio::tests
{
struct ModelAppearanceFixture
{
	QString root;
	ModelMesh mesh;
	QMap<QString, QByteArray> files;
	IdTechPalette palette;
	QString path(const QString& relative) const { return QDir(root).filePath(relative); }
	static QByteArray read(const QString& path) {
		QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
	}
	bool create(const QString& directory, QString* error) {
		root = directory;
		mesh = skinBindingMesh();
		for (int index = 0; index < 256; ++index) { palette.colors << qRgb(index, 255 - index, index ^ 85); }
		palette.id = "synthetic";
		files.insert("assets/gfx/palette.lmp", modelMdlPaletteBytes(palette));
		files.insert("assets/models/test.mdl", groupedMdlFixture().bytes);
		files.insert("assets/models/test.md3", exportEditableModel(mesh, "md3", 0, error));
		if (files.value("assets/models/test.md3").isEmpty()) { return false; }
		auto md2 = mesh; md2.surfaces = {md2.surfaces.first()}; md2.tags.clear();
		md2.md2SkinSize = QSize(32, 32);
		md2.surfaces[0].skinPaths = {"models/md2_body.pcx", "models/md2_alternate.pcx"};
		updateEditableModelMetadata(&md2);
		files.insert("assets/models/test.md2", exportEditableModel(md2, "md2", 0, error));
		if (files.value("assets/models/test.md2").isEmpty()) { return false; }
		files.insert("source.mesh.json", QJsonDocument(editableModelJson(mesh, error)).toJson());
		files.insert("assets/models/test.skin", skinBindings());
		files.insert("assets/models/broken.skin", "body,models/unknown\n");
		const QVector<QPair<QString, QColor>> colours{{"old_body", Qt::red}, {"alternate", Qt::blue}, {"old_head", Qt::green},
			{"new_body", Qt::cyan}, {"new_head", Qt::magenta}};
		for (const auto& [name, colour] : colours) {
			QImage image(32, 32, QImage::Format_RGB32); image.fill(colour);
			files.insert("assets/models/" + name + ".png", skinPng(image));
		}
		TextureExportOptions pcx;
		pcx.format = TextureExportFormat::Pcx;
		IdTechPaletteResolution pcxPalette;
		pcxPalette.palette.colors.fill(qRgb(0, 0, 0), 256);
		pcxPalette.palette.colors[0] = qRgb(255, 0, 0);
		pcxPalette.palette.colors[1] = qRgb(0, 0, 255);
		for (const auto& [name, colour] : QVector<QPair<QString, QColor>>{{"md2_body", Qt::red}, {"md2_alternate", Qt::blue}}) {
			QImage image(32, 32, QImage::Format_RGB32); image.fill(colour);
			const auto encoded = encodeTextureExport(image, pcx, pcxPalette);
			if (!encoded.succeeded) { if (error) { *error = encoded.error; } return false; }
			files.insert("assets/models/" + name + ".pcx", encoded.bytes);
		}
		for (auto it = files.cbegin(); it != files.cend(); ++it) {
			if (it.value().isEmpty() || !QDir().mkpath(QFileInfo(path(it.key())).absolutePath())) { return false; }
			QFile file(path(it.key()));
			if (!file.open(QIODevice::WriteOnly) || file.write(it.value()) != it.value().size()) { return false; }
		}
		return true;
	}
	bool unchanged() const {
		for (auto it = files.cbegin(); it != files.cend(); ++it) { if (read(path(it.key())) != it.value()) { return false; } }
		return true;
	}
};
} // namespace vibestudio::tests
