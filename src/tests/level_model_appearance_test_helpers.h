#pragma once

#include "tests/model_appearance_test_helpers.h"
#include "core/level_document.h"
#include "core/level_model_appearance.h"

namespace vibestudio::tests {
struct LevelModelAppearanceFixture : ModelAppearanceFixture {
	bool create(const QString& directory, QString* error) {
		if (!ModelAppearanceFixture::create(directory, error)) { return false; }
		for (const auto& pair : QVector<QPair<QString, QByteArray>>{
			{"assets/models/test_blue.skin", "models/old_body,models/new_body\nmodels/old_head,models/new_head\n"},
			{"assets/models/test_body.skin", "replace models/old_body models/alternate\n"},
			{"assets/models/test.md3_14.skin", "models/old_body,models/new_head\n"},
			{"assets/models/test_native.skin", "BODY_1,models/new_body\nhead,models/new_head\n"}}) {
			files.insert(pair.first, pair.second);
			QFile file(path(pair.first));
			if (!file.open(QIODevice::WriteOnly) || file.write(pair.second) != pair.second.size()) { return false; }
		}
		return true;
	}
	LevelMapDocument map(QString* error) const {
		LevelMapCreateRequest request; request.game = QStringLiteral("quake3"); request.starterRoom = false;
		LevelMapDocument document;
		if (!createLevelMap(request, &document, error)) { return {}; }
		for (int i = 1; i <= 3; ++i) {
			LevelMapEntity entity; entity.id = i; entity.className = QStringLiteral("misc_model"); entity.origin = {float(i * 128), 0, 0, true};
			entity.properties = {{QStringLiteral("classname"), entity.className}, {QStringLiteral("model"), QStringLiteral("models/test.md3")},
				{QStringLiteral("origin"), QStringLiteral("%1 0 0").arg(i * 128)}};
			document.entities << entity;
		}
		return document;
	}
};
inline QStringList appearanceMaterials(const LevelModelAppearanceResult& result) { return result.mesh.skinPaths; }
} // namespace vibestudio::tests
