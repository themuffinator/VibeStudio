#include "core/level_repeat.h"
#include "core/level_brush.h"
#include "core/level_history_p.h"
#include "core/level_linked_groups_p.h"
#include "core/level_placement_control_p.h"
#include "core/level_repeat_p.h"
#include "core/level_scene_locks.h"
#include "core/level_texture_mapping.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <algorithm>
#include <cmath>

// Independent implementation of the RepeatStack/Map repeat workflow reviewed
// against TrenchBroom v2026.2, GPL-3.0-or-later. See docs/CREDITS.md.
namespace vibestudio
{
namespace
{
thread_local int captureDepth = 0;
bool fail(QString* error, const char* message)
{
	if (error) {
		*error = QCoreApplication::translate("LevelRepeat", message);
	}
	return false;
}
bool mayCapture()
{
	return captureDepth == 0 && !detail::levelLinkedUpdatesDeferred();
}
template <size_t N> QJsonArray array(const std::array<double, N>& values)
{
	QJsonArray result;
	for (const auto value : values) {
		result.append(value);
	}
	return result;
}
template <size_t N> bool readArray(const QJsonValue& value, std::array<double, N>* target)
{
	if (!value.isArray() || value.toArray().size() != N) {
		return false;
	}
	for (size_t i = 0; i < N; ++i) {
		if (!value.toArray()[i].isDouble()) {
			return false;
		}
		(*target)[i] = value.toArray()[i].toDouble();
	}
	return true;
}
} // namespace

detail::LevelRepeatCaptureScope::LevelRepeatCaptureScope(LevelMapDocument* document, bool capture)
    : m_document(document), m_capture(document && capture && mayCapture())
{
	++captureDepth;
}
detail::LevelRepeatCaptureScope::~LevelRepeatCaptureScope()
{
	--captureDepth;
}
void detail::LevelRepeatCaptureScope::commit(const LevelRepeatAction& action, bool selectionChanged)
{
	if (!m_capture) {
		return;
	}
	auto& repeat = m_document->repeat;
	if (repeat.clearOnNextAction || selectionChanged) {
		repeat.actions.clear();
	}
	repeat.clearOnNextAction = false;
	repeat.actions << action;
	repeat.transforms = levelRepeatTransformsAfter(repeat.transforms, action);
	++repeat.revision;
	m_capture = false;
}
void detail::LevelRepeatCaptureScope::commitTransform(const LevelRepeatAction& action)
{
	if (!m_capture) {
		return;
	}
	auto& repeat = m_document->repeat;
	const auto updated = levelRepeatTransformsAfter(repeat.transforms, action);
	if (updated != repeat.transforms) {
		repeat.transforms = updated;
		++repeat.revision;
	}
	m_capture = false;
}
LevelRepeatTransforms detail::levelRepeatTransformsAfter(const LevelRepeatTransforms& current, const LevelRepeatAction& action)
{
	const auto& m = action.matrix;
	if (std::any_of(m.cbegin(), m.cend(), [](double n) { return !std::isfinite(n); })) {
		return current;
	}
	auto result = current;
	const std::array<double, 3> translation{m[3], m[7], m[11]};
	const bool moves = translation != std::array<double, 3>{0, 0, 0};
	if (action.kind == LevelRepeatAction::Kind::Move) {
		if (moves) {
			result.translation = translation;
		}
		return result;
	}
	if (action.kind != LevelRepeatAction::Kind::Transform) {
		return result;
	}
	if (action.rotationAxis >= 0) {
		if (action.rotationAxis <= 2 && std::isfinite(action.rotationDegrees) && std::remainder(action.rotationDegrees, 360.0) != 0) {
			result.rotationAxis = action.rotationAxis;
			result.rotationDegrees = action.rotationDegrees;
		}
		return result;
	}
	int offDiagonal = 0, row = -1, column = -1;
	for (int r = 0; r < 3; ++r) {
		for (int c = 0; c < 3; ++c) {
			if (r != c && m[4 * r + c] != 0) {
				++offDiagonal;
				row = r;
				column = c;
			}
		}
	}
	const bool unitDiagonal = m[0] == 1 && m[5] == 1 && m[10] == 1;
	if (offDiagonal == 0) {
		if (unitDiagonal) {
			// A resize that keeps the size only moves.
			if (moves) {
				result.translation = translation;
			}
		} else if (m[0] != 0 && m[5] != 0 && m[10] != 0) {
			result.scale = {m[0], m[5], m[10]};
		}
	} else if (offDiagonal == 1 && unitDiagonal) {
		result.skewAxis = row;
		result.skewAlong = column;
		result.skewAmount = m[4 * row + column];
	}
	return result;
}
void detail::levelRepeatSelectionChanged(LevelMapDocument* document)
{
	if (document && mayCapture() && !document->repeat.clearOnNextAction) {
		document->repeat.clearOnNextAction = true;
		++document->repeat.revision;
	}
}
void detail::levelRepeatHistoryNavigated(LevelMapDocument* document)
{
	if (mayCapture()) {
		clearLevelMapRepeatCommands(document);
	}
}
bool canRepeatLevelMapCommands(const LevelMapDocument& document)
{
	return !document.repeat.actions.isEmpty() && !document.selection.isEmpty();
}
void clearLevelMapRepeatCommands(LevelMapDocument* document)
{
	if (document && (!document->repeat.actions.isEmpty() || document->repeat.clearOnNextAction)) {
		document->repeat.actions.clear();
		document->repeat.clearOnNextAction = false;
		++document->repeat.revision;
	}
}
bool validateLevelRepeatActions(const QVector<LevelRepeatAction>& actions, QString* error)
{
	if (error) {
		error->clear();
	}
	if (actions.isEmpty()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelRepeat", "There are no repeatable commands."));
	}
	for (const auto& action : actions) {
		if (action.kind != LevelRepeatAction::Kind::Transform && action.kind != LevelRepeatAction::Kind::Move &&
		    action.kind != LevelRepeatAction::Kind::Duplicate && action.kind != LevelRepeatAction::Kind::DuplicateUnique) {
			return fail(error, QT_TRANSLATE_NOOP("LevelRepeat", "Unknown repeat action kind."));
		}
		for (double n : action.matrix) {
			if (!std::isfinite(n)) {
				return fail(error, QT_TRANSLATE_NOOP("LevelRepeat", "A repeat transform needs finite matrix values."));
			}
		}
		const auto& m = action.matrix;
		const auto determinant =
		    m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]);
		if (!std::isfinite(determinant) || determinant == 0 || action.mirror != (determinant < 0)) {
			return fail(error, QT_TRANSLATE_NOOP("LevelRepeat", "A repeat transform must be invertible with consistent winding."));
		}
		for (int row = 0; row < 3; ++row) {
			for (int column = 0; column < 3; ++column) {
				const int r1 = (row + 1) % 3, r2 = (row + 2) % 3, c1 = (column + 1) % 3, c2 = (column + 2) % 3;
				if (!std::isfinite((m[4 * r1 + c1] * m[4 * r2 + c2] - m[4 * r1 + c2] * m[4 * r2 + c1]) / determinant)) {
					return fail(error, QT_TRANSLATE_NOOP("LevelRepeat", "A repeat transform needs a finite inverse."));
				}
			}
		}
		if (action.rotationAxis < -1 || action.rotationAxis > 2 || !std::isfinite(action.rotationDegrees)) {
			return fail(error, QT_TRANSLATE_NOOP("LevelRepeat", "A repeat rotation needs a valid axis and finite angle."));
		}
		for (double n : action.rotationPivot) {
			if (!std::isfinite(n)) {
				return fail(error, QT_TRANSLATE_NOOP("LevelRepeat", "A repeat rotation needs a finite pivot."));
			}
		}
		if (!std::isfinite(action.yaw[0]) || !std::isfinite(action.yaw[1])) {
			return fail(error, QT_TRANSLATE_NOOP("LevelRepeat", "A repeat transform needs finite entity-angle values."));
		}
		const auto close = [](double a, double b) { return std::abs(a - b) <= 1e-10 * std::max({1.0, std::abs(a), std::abs(b)}); };
		if (action.rotationAxis >= 0) {
			const auto x = rotateLevelVector({1, 0, 0, true}, action.rotationAxis, action.rotationDegrees);
			const auto y = rotateLevelVector({0, 1, 0, true}, action.rotationAxis, action.rotationDegrees);
			const auto z = rotateLevelVector({0, 0, 1, true}, action.rotationAxis, action.rotationDegrees);
			const auto t =
			    rotateLevelPoint({0, 0, 0, true}, {action.rotationPivot[0], action.rotationPivot[1], action.rotationPivot[2], true},
			                     action.rotationAxis, action.rotationDegrees);
			const std::array<double, 12> rotation{x.x, y.x, z.x, t.x, x.y, y.y, z.y, t.y, x.z, y.z, z.z, t.z};
			for (int i = 0; i < 12; ++i) {
				if (!close(m[i], rotation[i])) {
					return fail(error, QT_TRANSLATE_NOOP("LevelRepeat", "Rotation metadata must match the repeat transform."));
				}
			}
			const bool yaw = action.rotationAxis == 2;
			if (action.transformYaw != yaw || (yaw && (!close(action.yaw[0], 1) || !close(action.yaw[1], action.rotationDegrees)))) {
				return fail(error, QT_TRANSLATE_NOOP("LevelRepeat", "Rotation metadata must match the repeat entity angles."));
			}
		} else if (action.rotationDegrees != 0 || action.rotationPivot != std::array<double, 3>{}) {
			return fail(error, QT_TRANSLATE_NOOP("LevelRepeat", "A non-rotation must not contain rotation metadata."));
		}
		if ((!action.transformYaw && action.yaw != std::array<double, 2>{1, 0}) ||
		    (action.transformYaw && action.yaw[0] != 1 && action.yaw[0] != -1)) {
			return fail(error, QT_TRANSLATE_NOOP("LevelRepeat", "Invalid repeat entity-angle transform."));
		}
		if (action.doomAllowed && (m[2] != 0 || m[6] != 0 || m[8] != 0 || m[9] != 0)) {
			return fail(error, QT_TRANSLATE_NOOP("LevelRepeat", "A Doom repeat transform must preserve the XY plane."));
		}
		if (action.kind == LevelRepeatAction::Kind::Duplicate || action.kind == LevelRepeatAction::Kind::DuplicateUnique || action.kind == LevelRepeatAction::Kind::Move) {
			if (action.rotationAxis != -1 || action.transformYaw || action.mirror || action.doomAllowed == (action.kind == LevelRepeatAction::Kind::DuplicateUnique)) {
				return fail(error, QT_TRANSLATE_NOOP("LevelRepeat", "A repeat move or duplication accepts translation metadata only."));
			}
			for (int row = 0; row < 3; ++row) {
				for (int column = 0; column < 3; ++column) {
					if (m[row * 4 + column] != (row == column ? 1 : 0)) {
						return fail(error, QT_TRANSLATE_NOOP("LevelRepeat", "A repeat move or duplication accepts a translation only."));
					}
				}
			}
		}
	}
	return true;
}
bool repeatLevelMapCommands(LevelMapDocument* document, const LevelMapTextureLockOptions& textures, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!document) {
		return fail(error, QT_TRANSLATE_NOOP("LevelRepeat", "No map is open."));
	}
	if (!canRepeatLevelMapCommands(*document) || !validateLevelRepeatActions(document->repeat.actions, error)) {
		if (error && error->isEmpty()) {
			*error = QCoreApplication::translate("LevelRepeat", "Select objects and record a transform or duplication to repeat.");
		}
		return false;
	}
	auto candidate = *document;
	const auto apply = [&](LevelMapDocument* working) {
		const detail::LevelMapHistoryScope history(*working);
		const detail::LevelRepeatCaptureScope suppress(working, false);
		const auto before = working->undoStack.size();
		const auto actions = working->repeat.actions;
		for (qsizetype i = 0; i < actions.size(); ++i) {
			detail::placementCheckpoint(LevelPlacementPhase::Repeating, i, actions.size());
			const auto& action = actions[i];
			const bool success =
			    (action.kind == LevelRepeatAction::Kind::Duplicate || action.kind == LevelRepeatAction::Kind::DuplicateUnique)
			        ? duplicateLevelMapObjects(working, working->selection, action.matrix[3], action.matrix[7], action.matrix[11], textures,
			                                   error, true, false, action.kind == LevelRepeatAction::Kind::DuplicateUnique ? LevelDuplicateMode::MakeUnique : LevelDuplicateMode::Ordinary)
			    : action.kind == LevelRepeatAction::Kind::Move
			        ? moveLevelMapSelection(working, action.matrix[3], action.matrix[7], action.matrix[11], textures, error)
			        : detail::applyLevelRepeatTransform(working, action, textures, error);
			if (!success) {
				return false;
			}
		}
		detail::placementCheckpoint(LevelPlacementPhase::Finalizing);
		return collapseLevelMapUndoSteps(working, static_cast<int>(working->undoStack.size() - before),
		                                 QCoreApplication::translate("LevelRepeat", "Repeat commands"),
		                                 QCoreApplication::translate("LevelRepeat", "Undo repeated commands"), error);
	};
	if (const auto guarded = guardLevelSceneEdit(&candidate, error, apply); guarded.has_value()) {
		if (!*guarded) {
			return false;
		}
	} else if (!apply(&candidate)) {
		return false;
	}
	detail::placementCancellationCheckpoint();
	*document = std::move(candidate);
	return true;
}
QJsonObject levelRepeatCommandsJson(const LevelMapDocument& document)
{
	QJsonArray actions;
	for (const auto& action : document.repeat.actions) {
		actions.append(QJsonObject{{"kind", action.kind == LevelRepeatAction::Kind::Duplicate ? "duplicate"
		                                    : action.kind == LevelRepeatAction::Kind::DuplicateUnique ? "duplicate-unique"
		                                    : action.kind == LevelRepeatAction::Kind::Move    ? "move"
		                                                                                      : "transform"},
		                           {"matrix", array(action.matrix)},
		                           {"rotationAxis", action.rotationAxis},
		                           {"rotationDegrees", action.rotationDegrees},
		                           {"rotationPivot", array(action.rotationPivot)},
		                           {"transformYaw", action.transformYaw},
		                           {"yaw", array(action.yaw)},
		                           {"mirror", action.mirror},
		                           {"doomAllowed", action.doomAllowed}});
	}
	return {{"version", 1}, {"actions", actions}};
}
bool parseLevelRepeatCommands(const QJsonObject& json, QVector<LevelRepeatAction>* actions, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!actions || json.size() != 2 || json.value("version") != QJsonValue(1) || !json.value("actions").isArray()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelRepeat", "Expected a version 1 repeat recipe with an actions array."));
	}
	QVector<LevelRepeatAction> result;
	for (const auto& entry : json.value("actions").toArray()) {
		const auto object = entry.toObject();
		const auto kind = object.value("kind").toString();
		LevelRepeatAction action;
		if (object.size() != 9 || (kind != "duplicate" && kind != "duplicate-unique" && kind != "move" && kind != "transform") ||
		    !readArray(object.value("matrix"), &action.matrix) || !readArray(object.value("rotationPivot"), &action.rotationPivot) ||
		    !readArray(object.value("yaw"), &action.yaw) || !object.value("rotationAxis").isDouble() ||
		    !object.value("rotationDegrees").isDouble() || !object.value("transformYaw").isBool() || !object.value("mirror").isBool() ||
		    !object.value("doomAllowed").isBool()) {
			return fail(error, QT_TRANSLATE_NOOP("LevelRepeat", "A repeat action is malformed."));
		}
		action.kind = kind == "duplicate" ? LevelRepeatAction::Kind::Duplicate
		              : kind == "duplicate-unique" ? LevelRepeatAction::Kind::DuplicateUnique
		              : kind == "move"    ? LevelRepeatAction::Kind::Move
		                                  : LevelRepeatAction::Kind::Transform;
		action.rotationAxis = object.value("rotationAxis").toInt(-2);
		action.rotationDegrees = object.value("rotationDegrees").toDouble();
		action.transformYaw = object.value("transformYaw").toBool();
		action.mirror = object.value("mirror").toBool();
		action.doomAllowed = object.value("doomAllowed").toBool();
		result << action;
	}
	if (!validateLevelRepeatActions(result, error)) {
		return false;
	}
	*actions = std::move(result);
	return true;
}

// NetRadiant Custom's Repeat Transforms. Workflow reference: NRC
// radiant/selection.cpp RadiantSelectionSystem::repeatTransforms (clone in
// primitive mode, then transform about the current pivot) and
// libs/transformlib.h matrix4_transform_for_components (rotation of the
// scaled shape, then translation) at 449778b3 (GPL-2.0-or-later). Original
// implementation; see docs/CREDITS.md.
namespace
{
bool textMap(const LevelMapDocument& document)
{
	return document.format == LevelMapFormat::QuakeMap || document.format == LevelMapFormat::Quake3Map;
}
QString axisName(int axis)
{
	return QString(QLatin1Char("XYZ"[std::clamp(axis, 0, 2)]));
}
bool usableTransforms(const LevelRepeatTransforms& transforms, QString* error)
{
	const auto finiteArray = [](const std::array<double, 3>& values) {
		return std::all_of(values.cbegin(), values.cend(), [](double n) { return std::isfinite(n) && std::abs(n) <= 65536; });
	};
	if (!finiteArray(transforms.translation) || !finiteArray(transforms.scale) ||
	    std::any_of(transforms.scale.cbegin(), transforms.scale.cend(), [](double n) { return n == 0; }) ||
	    (transforms.hasRotation() && (transforms.rotationAxis > 2 || !std::isfinite(transforms.rotationDegrees))) ||
	    (transforms.hasSkew() && (transforms.skewAxis > 2 || transforms.skewAlong > 2 || transforms.skewAxis == transforms.skewAlong ||
	                              !std::isfinite(transforms.skewAmount) || std::abs(transforms.skewAmount) > 1024))) {
		return fail(error, QT_TRANSLATE_NOOP("LevelRepeat", "The recorded transforms cannot be repeated. Reset them and record a new transform."));
	}
	return true;
}
// The linear part of the scale, skew and turn, applied in that order.
std::array<LevelMapVec3, 3> repeatLinearColumns(const LevelRepeatTransforms& transforms)
{
	std::array<LevelMapVec3, 3> columns{{{1, 0, 0, true}, {0, 1, 0, true}, {0, 0, 1, true}}};
	for (int c = 0; c < 3; ++c) {
		auto& column = columns[c];
		if (transforms.hasScale()) {
			column.x *= transforms.scale[0];
			column.y *= transforms.scale[1];
			column.z *= transforms.scale[2];
		}
		if (transforms.hasSkew()) {
			// Row skewAxis gains skewAmount times row skewAlong.
			const double along = transforms.skewAlong == 0 ? column.x : transforms.skewAlong == 1 ? column.y : column.z;
			(transforms.skewAxis == 0 ? column.x : transforms.skewAxis == 1 ? column.y : column.z) += transforms.skewAmount * along;
		}
		if (transforms.hasRotation()) {
			column = rotateLevelVector(column, transforms.rotationAxis, transforms.rotationDegrees);
		}
	}
	return columns;
}
} // namespace

bool canRepeatLevelMapTransforms(const LevelMapDocument& document)
{
	return textMap(document) && !document.selection.isEmpty() && !document.repeat.transforms.isIdentity();
}
void resetLevelMapRepeatTransforms(LevelMapDocument* document)
{
	if (document && !document->repeat.transforms.isIdentity()) {
		document->repeat.transforms = {};
		++document->repeat.revision;
	}
}
QStringList levelRepeatTransformsDescription(const LevelRepeatTransforms& transforms)
{
	QStringList lines;
	if (transforms.hasScale()) {
		lines << QCoreApplication::translate("LevelRepeat", "Scale %1, %2, %3")
		             .arg(transforms.scale[0], 0, 'g', 8)
		             .arg(transforms.scale[1], 0, 'g', 8)
		             .arg(transforms.scale[2], 0, 'g', 8);
	}
	if (transforms.hasSkew()) {
		lines << QCoreApplication::translate("LevelRepeat", "Shear along %1 by %2 per unit of %3")
		             .arg(axisName(transforms.skewAxis))
		             .arg(transforms.skewAmount, 0, 'g', 8)
		             .arg(axisName(transforms.skewAlong));
	}
	if (transforms.hasRotation()) {
		lines << QCoreApplication::translate("LevelRepeat", "Rotate %1° about %2")
		             .arg(transforms.rotationDegrees, 0, 'g', 8)
		             .arg(axisName(transforms.rotationAxis));
	}
	if (transforms.hasTranslation()) {
		lines << QCoreApplication::translate("LevelRepeat", "Translate %1, %2, %3")
		             .arg(transforms.translation[0], 0, 'g', 8)
		             .arg(transforms.translation[1], 0, 'g', 8)
		             .arg(transforms.translation[2], 0, 'g', 8);
	}
	return lines;
}
QJsonObject levelRepeatTransformsJson(const LevelRepeatTransforms& transforms)
{
	const auto axis = [](int value) { return QString(QLatin1Char("xyz"[std::clamp(value, 0, 2)])); };
	return {{"identity", transforms.isIdentity()},
	        {"translation", array(transforms.translation)},
	        {"rotation", transforms.hasRotation() ? QJsonValue(QJsonObject{{"axis", axis(transforms.rotationAxis)}, {"degrees", transforms.rotationDegrees}})
	                                              : QJsonValue()},
	        {"scale", array(transforms.scale)},
	        {"shear", transforms.hasSkew() ? QJsonValue(QJsonObject{{"axis", axis(transforms.skewAxis)},
	                                                                {"along", axis(transforms.skewAlong)},
	                                                                {"factor", transforms.skewAmount}})
	                                       : QJsonValue()}};
}
bool repeatLevelMapTransforms(LevelMapDocument* document, const LevelMapTextureLockOptions& textures, const LevelMapVec3& pivot,
                              LevelMapVec3* usedPivot, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!document) {
		return fail(error, QT_TRANSLATE_NOOP("LevelRepeat", "No map is open."));
	}
	if (!textMap(*document)) {
		return fail(error, QT_TRANSLATE_NOOP("LevelRepeat", "Repeat Transforms works on Quake-family .map files; use Repeat Last Commands on a Doom map."));
	}
	if (document->selection.isEmpty()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelRepeat", "Select objects to repeat the transforms on."));
	}
	const auto transforms = document->repeat.transforms;
	if (transforms.isIdentity()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelRepeat", "There is no transform to repeat yet. Move, rotate, scale or shear something first."));
	}
	if (!usableTransforms(transforms, error)) {
		return false;
	}
	if (pivot.valid && (!std::isfinite(pivot.x) || !std::isfinite(pivot.y) || !std::isfinite(pivot.z) ||
	                    std::max({std::abs(pivot.x), std::abs(pivot.y), std::abs(pivot.z)}) > 32768)) {
		return fail(error, QT_TRANSLATE_NOOP("LevelRepeat", "The repeat pivot must be finite and within ±32768 units."));
	}
	auto candidate = *document;
	LevelMapVec3 centre = pivot;
	const auto apply = [&](LevelMapDocument* working) {
		const detail::LevelMapHistoryScope history(*working);
		const detail::LevelRepeatCaptureScope suppress(working, false);
		const auto before = working->undoStack.size();
		const qint64 total = 1 + int(transforms.hasScale()) + int(transforms.hasSkew()) + int(transforms.hasRotation()) + int(transforms.hasTranslation());
		qint64 step = 0;
		detail::placementCheckpoint(LevelPlacementPhase::Repeating, step++, total);
		// NRC clones in primitive mode, so each press leaves the last copy behind.
		const auto selection = working->selection;
		if (!duplicateLevelMapObjects(working, selection, 0, 0, 0, textures, error, true, false, LevelDuplicateMode::Ordinary)) {
			return false;
		}
		if (!centre.valid) {
			LevelMapVec3 low, high;
			if (!levelMapSelectionBounds(*working, &low, &high)) {
				return fail(error, QT_TRANSLATE_NOOP("LevelRepeat", "The copies have no position to transform about."));
			}
			centre = {(low.x + high.x) * 0.5, (low.y + high.y) * 0.5, (low.z + high.z) * 0.5, true};
		}
		// A linear map about the pivot, through the transform every repeated
		// object edit shares (texture lock, entity origins and angles).
		const auto about = [&](const std::array<LevelMapVec3, 3>& columns, bool mirror, const std::array<double, 2>* yaw) {
			LevelRepeatAction action;
			action.kind = LevelRepeatAction::Kind::Transform;
			action.doomAllowed = false;
			action.mirror = mirror;
			const std::array<double, 3> p{centre.x, centre.y, centre.z};
			for (int r = 0; r < 3; ++r) {
				const auto value = [&](int c) { return r == 0 ? columns[c].x : r == 1 ? columns[c].y : columns[c].z; };
				action.matrix[4 * r] = value(0);
				action.matrix[4 * r + 1] = value(1);
				action.matrix[4 * r + 2] = value(2);
				action.matrix[4 * r + 3] = p[r] - (value(0) * p[0] + value(1) * p[1] + value(2) * p[2]);
			}
			if (yaw) {
				action.transformYaw = true;
				action.yaw = *yaw;
			}
			return detail::applyLevelRepeatTransform(working, action, textures, error);
		};
		if (transforms.hasScale()) {
			detail::placementCheckpoint(LevelPlacementPhase::Repeating, step++, total);
			const auto& s = transforms.scale;
			const std::array<LevelMapVec3, 3> columns{{{s[0], 0, 0, true}, {0, s[1], 0, true}, {0, 0, s[2], true}}};
			// A mirror reflects headings as Flip X (180 - a) and Flip Y (-a) do.
			const std::array<double, 2> reflected = s[0] < 0 && s[1] < 0 ? std::array<double, 2>{1, 180}
			                                       : s[0] < 0             ? std::array<double, 2>{-1, 180}
			                                                              : std::array<double, 2>{-1, 0};
			if (!about(columns, s[0] * s[1] * s[2] < 0, s[0] < 0 || s[1] < 0 ? &reflected : nullptr)) {
				return false;
			}
		}
		if (transforms.hasSkew()) {
			detail::placementCheckpoint(LevelPlacementPhase::Repeating, step++, total);
			LevelRepeatTransforms skew;
			skew.skewAxis = transforms.skewAxis;
			skew.skewAlong = transforms.skewAlong;
			skew.skewAmount = transforms.skewAmount;
			if (!about(repeatLinearColumns(skew), false, nullptr)) {
				return false;
			}
		}
		if (transforms.hasRotation()) {
			detail::placementCheckpoint(LevelPlacementPhase::Repeating, step++, total);
			LevelMapRotationRequest rotation;
			rotation.axis = transforms.rotationAxis;
			rotation.degrees = transforms.rotationDegrees;
			rotation.pivot = centre;
			rotation.textureLock = textures.enabled;
			rotation.allowValve220 = textures.allowValve220;
			if (!rotateLevelMapSelection(working, rotation, error)) {
				return false;
			}
		}
		if (transforms.hasTranslation()) {
			detail::placementCheckpoint(LevelPlacementPhase::Repeating, step++, total);
			const auto& t = transforms.translation;
			if (!moveLevelMapSelection(working, t[0], t[1], t[2], textures, error)) {
				return false;
			}
		}
		detail::placementCheckpoint(LevelPlacementPhase::Finalizing);
		const auto applied = levelRepeatTransformsDescription(transforms).join(QStringLiteral("; "));
		return collapseLevelMapUndoSteps(working, static_cast<int>(working->undoStack.size() - before),
		                                 QCoreApplication::translate("LevelRepeat", "Repeat transforms: %1").arg(applied),
		                                 QCoreApplication::translate("LevelRepeat", "Remove repeated copies"), error);
	};
	if (const auto guarded = guardLevelSceneEdit(&candidate, error, apply); guarded.has_value()) {
		if (!*guarded) {
			return false;
		}
	} else if (!apply(&candidate)) {
		return false;
	}
	detail::placementCancellationCheckpoint();
	*document = std::move(candidate);
	if (usedPivot) {
		*usedPivot = centre;
	}
	return true;
}
bool levelRepeatTransformsComponentRequest(const LevelRepeatTransforms& transforms, LevelComponentTransformRequest* request, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!request) {
		return fail(error, QT_TRANSLATE_NOOP("LevelRepeat", "Repeat Transforms needs a component transform to fill."));
	}
	if (transforms.isIdentity()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelRepeat", "There is no transform to repeat yet. Move, rotate, scale or shear something first."));
	}
	if (!usableTransforms(transforms, error)) {
		return false;
	}
	request->operation = LevelComponentTransformKind::Affine;
	request->linear = repeatLinearColumns(transforms);
	request->delta = {transforms.translation[0], transforms.translation[1], transforms.translation[2], true};
	return true;
}
} // namespace vibestudio
