#pragma once
#include "core/level_textured_selection.h"
#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli {
// Independent face picks for map transform-components: repeated --face
// face:brushId:faceNumber selectors (one-based, as map select-textured prints
// them) and an optional --context group opened before validation.
bool parseLevelFaceTransformPicks(const QStringList& selectors, const QString& context, LevelMapDocument* document,
	LevelSurfaceFacePicks* picks, QString* error);
// Adds the remapped picks, their geometry identities and the retired count.
void addLevelFaceTransformDetails(const LevelSurfaceFaceTransformReport& report, QJsonObject* details);
} // namespace vibestudio::cli
