#include "core/level_material_paint.h"

#include <QCoreApplication>
#include <QRegularExpression>

namespace vibestudio {

QString levelMaterialTargetId(const LevelMaterialTarget& target)
{
	switch (target.kind) {
	case LevelMaterialKind::BrushFace: return QStringLiteral("face:%1:%2").arg(target.objectId).arg(qint64(target.faceIndex) + 1);
	case LevelMaterialKind::Patch: return QStringLiteral("patch:%1").arg(target.objectId);
	case LevelMaterialKind::WallUpper: return QStringLiteral("side:%1:upper").arg(target.objectId);
	case LevelMaterialKind::WallLower: return QStringLiteral("side:%1:lower").arg(target.objectId);
	case LevelMaterialKind::WallMiddle: return QStringLiteral("side:%1:middle").arg(target.objectId);
	case LevelMaterialKind::SectorFloor: return QStringLiteral("sector:%1:floor").arg(target.objectId);
	case LevelMaterialKind::SectorCeiling: return QStringLiteral("sector:%1:ceiling").arg(target.objectId);
	default: return {};
	}
}

bool parseLevelMaterialTarget(const QString& text, LevelMaterialTarget* target, QString* error)
{
	if (error) { error->clear(); }
	if (target) { *target = {}; }
	static const QRegularExpression expression(QStringLiteral("^(face|patch|side|sector):([0-9]+)(?::([0-9]+|upper|lower|middle|floor|ceiling))?$"));
	const auto match = expression.match(text);
	bool ok = false;
	const int id = match.captured(2).toInt(&ok);
	LevelMaterialTarget result;
	result.objectId = id;
	const auto kind = match.captured(1), part = match.captured(3);
	if (match.hasMatch() && ok) {
		if (kind == QStringLiteral("face")) {
			const int face = part.toInt(&ok);
			if (ok && face > 0) { result.kind = LevelMaterialKind::BrushFace; result.faceIndex = face - 1; }
		} else if (kind == QStringLiteral("patch") && part.isEmpty()) { result.kind = LevelMaterialKind::Patch; }
		else if (kind == QStringLiteral("side")) {
			if (part == QStringLiteral("upper")) { result.kind = LevelMaterialKind::WallUpper; }
			if (part == QStringLiteral("lower")) { result.kind = LevelMaterialKind::WallLower; }
			if (part == QStringLiteral("middle")) { result.kind = LevelMaterialKind::WallMiddle; }
		} else if (kind == QStringLiteral("sector")) {
			if (part == QStringLiteral("floor")) { result.kind = LevelMaterialKind::SectorFloor; }
			if (part == QStringLiteral("ceiling")) { result.kind = LevelMaterialKind::SectorCeiling; }
		}
	}
	if (!target || result.kind == LevelMaterialKind::None) {
		if (error) { *error = QCoreApplication::translate("LevelMaterialPaint", "Use face:brushId:faceNumber (one-based), patch:id, side:id:upper|lower|middle or sector:id:floor|ceiling."); }
		return false;
	}
	*target = result;
	return true;
}

bool sampleLevelMaterial(const LevelMapDocument& document, const LevelMaterialTarget& target, QString* material, QString* error)
{
	if (error) { error->clear(); }
	if (material) { material->clear(); }
	const auto found = [material](const QString& value) { if (material) { *material = value; } return true; };
	const bool textMap = document.format == LevelMapFormat::QuakeMap || document.format == LevelMapFormat::Quake3Map;
	const bool doom = document.format == LevelMapFormat::DoomWad && document.doomFormat != LevelMapDoomFormat::Udmf;
	if (target.objectId >= 0 && (target.faceIndex == 0 || target.kind == LevelMaterialKind::BrushFace)) {
		if (textMap && target.kind == LevelMaterialKind::BrushFace) {
			for (const auto& brush : document.brushes) {
				if (brush.id == target.objectId && target.faceIndex >= 0 && target.faceIndex < brush.faces.size()) {
					return found(brush.faces[target.faceIndex].textureName);
				}
			}
		} else if (textMap && target.kind == LevelMaterialKind::Patch) {
			for (const auto& patch : document.patches) { if (patch.id == target.objectId) { return found(patch.textureName); } }
		} else if (doom && (target.kind == LevelMaterialKind::WallUpper || target.kind == LevelMaterialKind::WallLower || target.kind == LevelMaterialKind::WallMiddle)) {
			for (const auto& side : document.doomSidedefs) {
				if (side.id == target.objectId) {
					return found(target.kind == LevelMaterialKind::WallUpper ? side.upperTexture : target.kind == LevelMaterialKind::WallLower ? side.lowerTexture : side.middleTexture);
				}
			}
		} else if (doom && (target.kind == LevelMaterialKind::SectorFloor || target.kind == LevelMaterialKind::SectorCeiling)) {
			for (const auto& sector : document.doomSectors) {
				if (sector.id == target.objectId) { return found(target.kind == LevelMaterialKind::SectorFloor ? sector.floorTexture : sector.ceilingTexture); }
			}
		}
	}
	if (error) { *error = QCoreApplication::translate("LevelMaterialPaint", "The material target %1 is unavailable in this map.").arg(levelMaterialTargetId(target)); }
	return false;
}

} // namespace vibestudio
