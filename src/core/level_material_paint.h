#pragma once

#include "core/level_map.h"

namespace vibestudio {

enum class LevelMaterialKind { None, BrushFace, Patch, WallUpper, WallLower, WallMiddle, SectorFloor, SectorCeiling };

// Source identities, independent of preview triangle ordering and selection.
struct LevelMaterialTarget {
	LevelMaterialKind kind = LevelMaterialKind::None;
	int objectId = -1;
	int faceIndex = 0; // Zero-based; only BrushFace uses this field.
	bool operator==(const LevelMaterialTarget&) const = default;
};
inline size_t qHash(const LevelMaterialTarget& target, size_t seed = 0)
{
	return qHashMulti(seed, static_cast<int>(target.kind), target.objectId, target.faceIndex);
}

// Stable selectors: face:brushId:faceNumber (one-based), patch:id,
// side:id:upper|lower|middle, sector:id:floor|ceiling.
QString levelMaterialTargetId(const LevelMaterialTarget& target);
bool parseLevelMaterialTarget(const QString& text, LevelMaterialTarget* target, QString* error = nullptr);
bool sampleLevelMaterial(const LevelMapDocument& document, const LevelMaterialTarget& target, QString* material, QString* error = nullptr);

class LevelMaterialPaintPlan {
public:
	bool ready() const { return m_ready; }
	int changedCount() const { return static_cast<int>(m_changes.size()); }
	int targetCount() const { return m_targetCount; }
	QString material() const { return m_material; }
private:
	friend bool prepareLevelMaterialPaint(const LevelMapDocument&, const QVector<LevelMaterialTarget>&, const QString&, LevelMaterialPaintPlan*, QString*);
	friend bool commitLevelMaterialPaint(LevelMapDocument*, const LevelMaterialPaintPlan&, QString*);
	bool m_ready = false;
	int m_targetCount = 0;
	quint64 m_revision = 0;
	QString m_sourcePath, m_material, m_mapName;
	QByteArray m_sourceHash;
	LevelMapFormat m_format = LevelMapFormat::Unknown;
	LevelMapDoomFormat m_doomFormat = LevelMapDoomFormat::Doom;
	QVector<LevelMapTextureChange> m_changes;
};

// All targets validate before any mutation. Duplicate hits are one target.
// Names, source rewriting and undo use the same map services as the inspector.
// Commit rejects stale plans. A no-op preserves undo/redo, revision and saved state.
bool prepareLevelMaterialPaint(const LevelMapDocument& document, const QVector<LevelMaterialTarget>& targets,
	const QString& material, LevelMaterialPaintPlan* plan, QString* error = nullptr);
bool commitLevelMaterialPaint(LevelMapDocument* document, const LevelMaterialPaintPlan& plan, QString* error = nullptr);

} // namespace vibestudio
