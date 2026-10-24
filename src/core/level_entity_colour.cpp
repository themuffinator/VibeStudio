#include "core/level_entity_colour.h"

// Workflow reference: NRC radiant/entity.cpp Entity_setColour,
// Entity_normalizeColor and NormalizeColor at 449778b3 (GPL-2.0-or-later).
// Original Qt implementation; see docs/CREDITS.md.

#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_visibility.h"

#include <QCoreApplication>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <optional>

namespace vibestudio {

namespace {

bool colourMapFormat(const LevelMapDocument& document)
{
	return document.format == LevelMapFormat::QuakeMap || document.format == LevelMapFormat::Quake3Map;
}

bool isWorldspawn(const LevelMapEntity& entity)
{
	return entity.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) == 0;
}

QString fractionText(double value)
{
	QString text = QString::number(value, 'f', 6);
	while (text.contains(QLatin1Char('.')) && (text.endsWith(QLatin1Char('0')) || text.endsWith(QLatin1Char('.')))) {
		text.chop(1);
	}
	return text == QStringLiteral("-0") ? QStringLiteral("0") : text;
}

QString byteText(double value)
{
	return QString::number(std::lround(value));
}

// Which entity holds each brush and patch, built only once a selected
// primitive needs it, and each entity's place in the document.
class Owners {
public:
	explicit Owners(const LevelMapDocument& document)
		: m_document(document)
	{
		for (int index = 0; index < document.entities.size(); ++index) {
			m_entities.insert(document.entities.at(index).id, index);
		}
	}

	const LevelMapEntity* entity(int id) const
	{
		const auto found = m_entities.constFind(id);
		return found == m_entities.cend() ? nullptr : &m_document.entities.at(found.value());
	}

	// The entity a selected item belongs to, or -1.
	int owner(const LevelMapSelectionRef& ref)
	{
		switch (ref.kind) {
		case LevelMapSelectionKind::Entity:
			return m_entities.contains(ref.objectId) ? ref.objectId : -1;
		case LevelMapSelectionKind::QuakeBrush:
		case LevelMapSelectionKind::QuakePatch:
			if (!m_primitivesIndexed) {
				for (const LevelMapBrush& brush : m_document.brushes) {
					m_brushes.insert(brush.id, brush.entityId);
				}
				for (const LevelMapPatch& patch : m_document.patches) {
					m_patches.insert(patch.id, patch.entityId);
				}
				m_primitivesIndexed = true;
			}
			return (ref.kind == LevelMapSelectionKind::QuakeBrush ? m_brushes : m_patches).value(ref.objectId, -1);
		default:
			return -1;
		}
	}

	// The entity the colour commands write for this item: -1 for anything
	// else, including worldspawn reached through one of its brushes.
	int target(const LevelMapSelectionRef& ref, bool* worldPrimitive = nullptr)
	{
		const int id = owner(ref);
		const LevelMapEntity* found = id >= 0 ? entity(id) : nullptr;
		if (!found) {
			return -1;
		}
		if (ref.kind != LevelMapSelectionKind::Entity && isWorldspawn(*found)) {
			if (worldPrimitive) {
				*worldPrimitive = true;
			}
			return -1;
		}
		return id;
	}

private:
	const LevelMapDocument& m_document;
	QHash<int, int> m_entities;
	QHash<int, int> m_brushes;
	QHash<int, int> m_patches;
	bool m_primitivesIndexed = false;
};

std::optional<QString> propertyValue(const LevelMapEntity& entity, const QString& key)
{
	for (const LevelMapProperty& property : entity.properties) {
		if (property.key.compare(key, Qt::CaseInsensitive) == 0) {
			return property.value;
		}
	}
	return std::nullopt;
}

bool entityColour(const LevelMapEntity& entity, const QString& key, LevelEntityColour* colour)
{
	const std::optional<QString> value = propertyValue(entity, key);
	return value && parseLevelEntityColour(*value, colour);
}

QString noTargetsText(const LevelEntityColourTargets& targets)
{
	if (targets.hidden > 0) {
		return QCoreApplication::translate("VibeStudioLevelEntityColour", "The selected entities are hidden. Show them before changing their colour.");
	}
	if (targets.worldPrimitives > 0) {
		return QCoreApplication::translate("VibeStudioLevelEntityColour",
			"World brushes have no entity colour. Select lights or other entities, or the brushes of a brush entity.");
	}
	return QCoreApplication::translate("VibeStudioLevelEntityColour", "Select the entities to colour, or the brushes of a brush entity.");
}

// Names the history step after the command rather than the key.
void nameColourStep(LevelMapDocument* document, const QString& description, const QString& undoDescription)
{
	if (!document->undoStack.isEmpty()) {
		document->undoStack.last().description = description;
		document->undoStack.last().undoDescription = undoDescription;
	}
}

// Writes `values` to `ids` as one step and records which ones changed.
bool writeColours(LevelMapDocument* document, const QVector<int>& ids, const QString& key, const QStringList& values, QVector<int>* changed,
	QString* error)
{
	Owners owners(*document);
	for (qsizetype index = 0; index < ids.size(); ++index) {
		const LevelMapEntity* entity = owners.entity(ids.at(index));
		const std::optional<QString> existing = entity ? propertyValue(*entity, key) : std::nullopt;
		if (!existing || *existing != values.at(index)) {
			changed->push_back(ids.at(index));
		}
	}
	if (changed->isEmpty()) {
		return true;
	}
	return setLevelMapEntitiesProperty(document, ids, key, values, error);
}

} // namespace

QString levelEntityColourKey()
{
	return QStringLiteral("_color");
}

bool isLevelEntityColourKeyName(const QString& key)
{
	const QString trimmed = key.trimmed();
	return trimmed.compare(QStringLiteral("_color"), Qt::CaseInsensitive) == 0 || trimmed.compare(QStringLiteral("color"), Qt::CaseInsensitive) == 0;
}

std::array<double, 3> LevelEntityColour::unit() const
{
	const double scale = fractions ? 1.0 : 255.0;
	return {channels[0] / scale, channels[1] / scale, channels[2] / scale};
}

QColor LevelEntityColour::color() const
{
	const std::array<double, 3> values = unit();
	const auto channel = [](double value) {
		return std::clamp(static_cast<int>(std::lround(value * 255.0)), 0, 255);
	};
	return QColor(channel(values[0]), channel(values[1]), channel(values[2]));
}

bool parseLevelEntityColour(const QString& value, LevelEntityColour* parsed)
{
	static const QRegularExpression spaces(QStringLiteral(R"(\s+)"));
	const QStringList parts = value.split(spaces, Qt::SkipEmptyParts);
	if (parts.size() < 3) {
		return false;
	}
	LevelEntityColour result;
	for (int index = 0; index < 3; ++index) {
		bool ok = false;
		result.channels[index] = parts.at(index).toDouble(&ok);
		if (!ok || !std::isfinite(result.channels[index]) || result.channels[index] < 0.0) {
			return false;
		}
	}
	result.fractions = std::all_of(result.channels.cbegin(), result.channels.cend(), [](double channel) { return channel <= 1.0; });
	result.rest = parts.mid(3).join(QLatin1Char(' '));
	if (parsed) {
		*parsed = result;
	}
	return true;
}

QString formatLevelEntityColour(const LevelEntityColour& colour)
{
	QStringList parts;
	for (const double channel : colour.channels) {
		parts << (colour.fractions ? fractionText(channel) : byteText(channel));
	}
	if (!colour.rest.isEmpty()) {
		parts << colour.rest;
	}
	return parts.join(QLatin1Char(' '));
}

QString formatLevelEntityColourLike(const std::array<double, 3>& unit, const LevelEntityColour* like)
{
	LevelEntityColour colour;
	colour.fractions = !like || like->fractions;
	colour.rest = like ? like->rest : QString();
	for (int index = 0; index < 3; ++index) {
		const double value = std::clamp(std::isfinite(unit[index]) ? unit[index] : 0.0, 0.0, 1.0);
		colour.channels[index] = colour.fractions ? value : std::round(value * 255.0);
	}
	return formatLevelEntityColour(colour);
}

LevelEntityColour normalisedLevelEntityColour(const LevelEntityColour& colour)
{
	// NRC NormalizeColor: the brightest channel becomes 1; black becomes white.
	LevelEntityColour result;
	result.rest = colour.rest;
	result.fractions = true;
	const double brightest = *std::max_element(colour.channels.cbegin(), colour.channels.cend());
	if (brightest <= 0.0) {
		result.channels = {1.0, 1.0, 1.0};
		return result;
	}
	for (int index = 0; index < 3; ++index) {
		result.channels[index] = colour.channels[index] / brightest;
	}
	return result;
}

LevelEntityColourTargets levelMapEntityColourTargets(const LevelMapDocument& document)
{
	LevelEntityColourTargets result;
	if (!colourMapFormat(document) || document.selection.isEmpty()) {
		return result;
	}
	Owners owners(document);
	std::optional<QSet<QString>> hiddenObjects;
	QSet<int> seen;
	QSet<int> targets;
	for (const LevelMapSelectionRef& ref : document.selection) {
		bool worldPrimitive = false;
		const int id = owners.target(ref, &worldPrimitive);
		if (worldPrimitive) {
			++result.worldPrimitives;
		}
		if (id < 0 || seen.contains(id)) {
			continue;
		}
		seen.insert(id);
		if (!hiddenObjects) {
			hiddenObjects = levelMapHiddenObjects(document);
		}
		if (hiddenObjects->contains(levelMapSelectionRefId({LevelMapSelectionKind::Entity, id}))) {
			++result.hidden;
			continue;
		}
		targets.insert(id);
		result.entities.push_back(id);
	}
	// The colour comes from the last-selected item's entity, as NRC reads the
	// primary selection's.
	for (auto it = document.selection.crbegin(); it != document.selection.crend(); ++it) {
		const int id = owners.target(*it);
		if (targets.contains(id)) {
			result.primaryEntity = id;
			break;
		}
	}
	return result;
}

bool levelMapPrimaryEntityColour(const LevelMapDocument& document, const QString& key, LevelEntityColour* colour, int* entityId)
{
	const LevelEntityColourTargets targets = levelMapEntityColourTargets(document);
	if (entityId) {
		*entityId = targets.primaryEntity;
	}
	const Owners owners(document);
	const LevelMapEntity* primary = targets.primaryEntity >= 0 ? owners.entity(targets.primaryEntity) : nullptr;
	return primary && entityColour(*primary, key.trimmed(), colour);
}

QString levelMapEntityColourBlockedReason(const LevelMapDocument& document, const LevelEntityColourTargets& targets)
{
	if (targets.entities.isEmpty()) {
		return noTargetsText(targets);
	}
	const QSet<QString> locked = levelSceneLockedObjects(document);
	std::optional<LevelSceneSelectionContext> context;
	if (!document.openSceneGroups.isEmpty()) {
		context = levelSceneSelectionContext(document);
	}
	for (const int id : targets.entities) {
		const QString object = levelMapSelectionRefId({LevelMapSelectionKind::Entity, id});
		if (locked.contains(object)) {
			return QCoreApplication::translate("VibeStudioLevelEntityColour", "Entity %1 is in a locked layer or group. Unlock it before changing its colour.").arg(id);
		}
		if (context && !context->openGroup.isEmpty() && !context->allowedObjects.contains(object)) {
			return QCoreApplication::translate("VibeStudioLevelEntityColour", "Entity %1 is outside the open group. Close the group before changing its colour.").arg(id);
		}
	}
	return {};
}

bool setLevelMapSelectionEntityColour(LevelMapDocument* document, const LevelEntityColourRequest& request, LevelEntityColourReport* report, QString* error)
{
	LevelEntityColourReport staged;
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
			return setLevelMapSelectionEntityColour(candidate, request, &staged, error);
		});
		guarded.has_value()) {
		if (*guarded && report) {
			*report = staged;
		}
		return *guarded;
	}
	if (report) {
		*report = {};
	}
	if (error) {
		error->clear();
	}
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!document || !colourMapFormat(*document)) {
		return fail(QCoreApplication::translate("VibeStudioLevelEntityColour", "Entity colours are keys of Quake-family .map entities."));
	}
	const QString key = request.key.trimmed();
	if (key.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioLevelEntityColour", "Name the key that holds the colour, such as _color."));
	}
	if (!std::all_of(request.unit.cbegin(), request.unit.cend(), [](double channel) { return std::isfinite(channel); })) {
		return fail(QCoreApplication::translate("VibeStudioLevelEntityColour", "A colour channel is not a number."));
	}
	const LevelEntityColourTargets targets = levelMapEntityColourTargets(*document);
	staged.worldPrimitives = targets.worldPrimitives;
	staged.hidden = targets.hidden;
	if (targets.entities.isEmpty()) {
		return fail(noTargetsText(targets));
	}
	const Owners owners(*document);
	QStringList values;
	for (const int id : targets.entities) {
		LevelEntityColour current;
		const LevelMapEntity* entity = owners.entity(id);
		const bool hasColour = entity && entityColour(*entity, key, &current);
		values << formatLevelEntityColourLike(request.unit, hasColour ? &current : nullptr);
	}
	staged.entities = targets.entities;
	staged.sourceEntity = targets.primaryEntity;
	staged.value = values.value(static_cast<int>(targets.entities.indexOf(targets.primaryEntity)));
	if (!writeColours(document, targets.entities, key, values, &staged.changed, error)) {
		return false;
	}
	if (!staged.changed.isEmpty()) {
		const int changed = static_cast<int>(staged.changed.size());
		nameColourStep(document,
			QCoreApplication::translate("VibeStudioLevelEntityColour", "Set entity colour %1 on %n entit(y)(ies)", nullptr, changed)
				.arg(formatLevelEntityColourLike(request.unit)),
			QCoreApplication::translate("VibeStudioLevelEntityColour", "Restore entity colour on %n entit(y)(ies)", nullptr, changed));
	}
	if (report) {
		*report = staged;
	}
	return true;
}

bool normaliseLevelMapSelectionEntityColour(LevelMapDocument* document, LevelEntityColourNormalise mode, const QString& key, LevelEntityColourReport* report,
	QString* error)
{
	LevelEntityColourReport staged;
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
			return normaliseLevelMapSelectionEntityColour(candidate, mode, key, &staged, error);
		});
		guarded.has_value()) {
		if (*guarded && report) {
			*report = staged;
		}
		return *guarded;
	}
	if (report) {
		*report = {};
	}
	if (error) {
		error->clear();
	}
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!document || !colourMapFormat(*document)) {
		return fail(QCoreApplication::translate("VibeStudioLevelEntityColour", "Entity colours are keys of Quake-family .map entities."));
	}
	const QString trimmedKey = key.trimmed();
	if (trimmedKey.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioLevelEntityColour", "Name the key that holds the colour, such as _color."));
	}
	const LevelEntityColourTargets targets = levelMapEntityColourTargets(*document);
	staged.worldPrimitives = targets.worldPrimitives;
	staged.hidden = targets.hidden;
	if (targets.entities.isEmpty()) {
		return fail(noTargetsText(targets));
	}
	const Owners owners(*document);
	QVector<int> ids;
	QStringList values;
	if (mode == LevelEntityColourNormalise::Primary) {
		LevelEntityColour source;
		const LevelMapEntity* primary = owners.entity(targets.primaryEntity);
		if (!primary || !entityColour(*primary, trimmedKey, &source)) {
			return fail(primary && propertyValue(*primary, trimmedKey)
					? QCoreApplication::translate("VibeStudioLevelEntityColour", "The %1 of entity %2 is not a colour of three numbers.").arg(trimmedKey).arg(targets.primaryEntity)
					: QCoreApplication::translate("VibeStudioLevelEntityColour", "Entity %1, the last one selected, has no %2 to normalise.").arg(targets.primaryEntity).arg(trimmedKey));
		}
		const LevelEntityColour normalised = normalisedLevelEntityColour(source);
		staged.sourceEntity = targets.primaryEntity;
		for (const int id : targets.entities) {
			LevelEntityColour written = normalised;
			LevelEntityColour current;
			const LevelMapEntity* entity = owners.entity(id);
			written.rest = entity && entityColour(*entity, trimmedKey, &current) ? current.rest : QString();
			ids << id;
			values << formatLevelEntityColour(written);
		}
	} else {
		for (const int id : targets.entities) {
			LevelEntityColour current;
			const LevelMapEntity* entity = owners.entity(id);
			if (!entity || !entityColour(*entity, trimmedKey, &current)) {
				staged.skipped << id;
				continue;
			}
			ids << id;
			values << formatLevelEntityColour(normalisedLevelEntityColour(current));
		}
		if (ids.isEmpty()) {
			return fail(QCoreApplication::translate("VibeStudioLevelEntityColour", "None of the selected entities has a %1 to normalise.").arg(trimmedKey));
		}
		staged.sourceEntity = ids.contains(targets.primaryEntity) ? targets.primaryEntity : ids.constLast();
	}
	staged.entities = ids;
	staged.value = values.value(static_cast<int>(ids.indexOf(staged.sourceEntity)));
	if (!writeColours(document, ids, trimmedKey, values, &staged.changed, error)) {
		return false;
	}
	if (!staged.changed.isEmpty()) {
		const int changed = static_cast<int>(staged.changed.size());
		nameColourStep(document, QCoreApplication::translate("VibeStudioLevelEntityColour", "Normalise entity colour on %n entit(y)(ies)", nullptr, changed),
			QCoreApplication::translate("VibeStudioLevelEntityColour", "Restore entity colour on %n entit(y)(ies)", nullptr, changed));
	}
	if (report) {
		*report = staged;
	}
	return true;
}

bool levelMapSelectionHasEntityColourTargets(const LevelMapDocument& document)
{
	if (!colourMapFormat(document)) {
		return false;
	}
	Owners owners(document);
	return std::any_of(document.selection.cbegin(), document.selection.cend(), [&owners](const LevelMapSelectionRef& ref) { return owners.target(ref) >= 0; });
}

bool levelMapSelectionEntityColourNormalisable(const LevelMapDocument& document, const QString& key)
{
	if (!colourMapFormat(document)) {
		return false;
	}
	Owners owners(document);
	for (auto it = document.selection.crbegin(); it != document.selection.crend(); ++it) {
		const int id = owners.target(*it);
		if (id >= 0) {
			LevelEntityColour colour;
			return entityColour(*owners.entity(id), key.trimmed(), &colour);
		}
	}
	return false;
}

} // namespace vibestudio
