#include "core/level_entity_connections_p.h"
#include "core/level_duplicate_p.h"
#include "core/level_placement_control_p.h"
#include "core/entity_builtin_catalogue.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>
#include <algorithm>

// Original adaptation of NRC Entity_connectSelected/Entity_killconnectSelected
// and Quake3EntityCreator::connectEntities, revision 449778b3840f8f2062daab15c87cb012f2ac945c,
// GPL-2.0-or-later. See docs/CREDITS.md for the pinned sources and licence review.
namespace vibestudio {
namespace {
bool fail(QString* error, const char* message)
{
	if (error) { *error = QCoreApplication::translate("LevelEntityConnections",message); }
	return false;
}
struct Selection {
	QHash<int,int> indexes;
	QVector<int> entities;
};
template<typename Records> int recordIndex(const Records& records, int id)
{
	if (id < 0) { return -1; }
	if (id < records.size() && records[id].id == id) { return id; }
	for (qsizetype i = 0; i < records.size(); ++i) {
		detail::placementCancellationCheckpoint();
		if (records[i].id == id) { return static_cast<int>(i); }
	}
	return -1;
}
bool resolve(const LevelMapDocument& document, const LevelEntityConnectionRequest& request, Selection* result, QString* error)
{
	if (document.format != LevelMapFormat::QuakeMap && document.format != LevelMapFormat::Quake3Map) {
		return fail(error,QT_TRANSLATE_NOOP("LevelEntityConnections","Entities are connected in Quake-family text maps."));
	}
	const bool legacy = request.mode == LevelEntityConnectionMode::SourcesToLast;
	if (!legacy && request.mode != LevelEntityConnectionMode::RadiantTarget && request.mode != LevelEntityConnectionMode::RadiantKillTarget) {
		return fail(error,QT_TRANSLATE_NOOP("LevelEntityConnections","Unknown entity connection mode."));
	}
	if (!legacy && document.selection.size() != 2) {
		return fail(error,QT_TRANSLATE_NOOP("LevelEntityConnections","Select exactly two objects, the source first and the destination last."));
	}
	if (document.selection.size() < 2) {
		return fail(error,QT_TRANSLATE_NOOP("LevelEntityConnections","Select the entities to link, the destination last; worldspawn cannot take part in a multi-source connection."));
	}
	if (request.mode == LevelEntityConnectionMode::RadiantKillTarget && !levelEntityKillConnectionSupported(document,request.game)) {
		return fail(error,QT_TRANSLATE_NOOP("LevelEntityConnections","KillConnect requires a Quake, Nexuiz or Xonotic game context."));
	}
	QHash<int,int> brushOwners, patchOwners;
	// Action enablement usually resolves a pair of dense native IDs. Do not
	// allocate whole-map indexes on every selection refresh. Bulk selections
	// use indexes to keep owner resolution linear, including sparse native IDs.
	const bool pair = document.selection.size() == 2;
	if (!pair) {
		for (qsizetype i = 0; i < document.entities.size(); ++i) {
			detail::placementCancellationCheckpoint(); result->indexes.insert(document.entities[i].id,static_cast<int>(i));
		}
		for (const auto& brush : document.brushes) { detail::placementCancellationCheckpoint(); brushOwners.insert(brush.id,brush.entityId); }
		for (const auto& patch : document.patches) { detail::placementCancellationCheckpoint(); patchOwners.insert(patch.id,patch.entityId); }
	}
	for (const auto& ref : document.selection) {
		detail::placementCancellationCheckpoint();
		int id = ref.kind == LevelMapSelectionKind::Entity ? ref.objectId
			: ref.kind == LevelMapSelectionKind::QuakeBrush ? brushOwners.value(ref.objectId,-1)
			: ref.kind == LevelMapSelectionKind::QuakePatch ? patchOwners.value(ref.objectId,-1) : -1;
		if (pair) {
			if (ref.kind == LevelMapSelectionKind::QuakeBrush) {
				const int index = recordIndex(document.brushes,ref.objectId); id = index < 0 ? -1 : document.brushes[index].entityId;
			} else if (ref.kind == LevelMapSelectionKind::QuakePatch) {
				const int index = recordIndex(document.patches,ref.objectId); id = index < 0 ? -1 : document.patches[index].entityId;
			}
			const int index = recordIndex(document.entities,id);
			if (index >= 0) { result->indexes.insert(id,index); }
		}
		if (!result->indexes.contains(id)) { return fail(error,QT_TRANSLATE_NOOP("LevelEntityConnections","A selected object has no valid entity owner.")); }
		const auto& owner = document.entities[result->indexes.value(id)];
		// NRC resolves world primitives to worldspawn just like other owners.
		// The existing multi-source adaptation excludes worldspawn.
		if (legacy && owner.className.compare(QLatin1String("worldspawn"),Qt::CaseInsensitive) == 0) { continue; }
		result->entities << id;
	}
	if (legacy) {
		QSet<int> seen; QVector<int> unique;
		for (auto it = result->entities.crbegin(); it != result->entities.crend(); ++it) {
			detail::placementCancellationCheckpoint();
			if (!seen.contains(*it)) { seen.insert(*it); unique << *it; }
		}
		std::reverse(unique.begin(),unique.end()); result->entities = std::move(unique);
	}
	if (result->entities.size() < 2) {
		return fail(error,QT_TRANSLATE_NOOP("LevelEntityConnections","Select the entities to link, the destination last; worldspawn cannot take part in a multi-source connection."));
	}
	if (!legacy && result->entities[0] == result->entities[1]) {
		return fail(error,QT_TRANSLATE_NOOP("LevelEntityConnections","The two selected objects belong to the same entity."));
	}
	return true;
}
QString property(const LevelMapEntity& entity, const QString& key, Qt::CaseSensitivity sensitivity)
{
	for (const auto& p : entity.properties) { if (p.key.compare(key,sensitivity) == 0) { return p.value; } }
	return {};
}
bool setProperty(LevelMapEntity* entity, const QString& key, const QString& value, Qt::CaseSensitivity sensitivity)
{
	for (auto& p : entity->properties) {
		if (p.key.compare(key,sensitivity) == 0) {
			if (p.value == value) { return false; }
			p.value = value; return true;
		}
	}
	entity->properties << LevelMapProperty{key,value,0}; return true;
}
} // namespace

QString levelEntityConnectionGame(const LevelMapDocument& document, const QString& requested)
{
	auto game = requested.trimmed().toLower();
	if (game.isEmpty() || game == "auto" || game == "custom") { return builtinEntityGameId(builtinEntityGameForMap(document)); }
	if (game == "q1") { return QStringLiteral("quake"); }
	if (game == "q2") { return QStringLiteral("quake2"); }
	if (game == "q3") { return QStringLiteral("quake3"); }
	return game;
}
bool levelEntityKillConnectionSupported(const LevelMapDocument& document, const QString& game)
{
	if (document.format != LevelMapFormat::QuakeMap && document.format != LevelMapFormat::Quake3Map) { return false; }
	const auto id = levelEntityConnectionGame(document,game);
	return id == "quake" || id == "nexuiz" || id == "xonotic";
}
bool canConnectLevelMapEntities(const LevelMapDocument& document, const LevelEntityConnectionRequest& request)
{
	Selection selection; return resolve(document,request,&selection,nullptr);
}
bool detail::prepareLevelEntityConnection(const LevelMapDocument& document, const LevelEntityConnectionRequest& request,
	LevelMapUndoCommand* command, LevelEntityConnectionReport* report, QString* error)
{
	if (error) { error->clear(); }
	Selection selection;
	if (!resolve(document,request,&selection,error)) { return false; }
	const bool legacy = request.mode == LevelEntityConnectionMode::SourcesToLast;
	const auto sensitivity = legacy ? Qt::CaseInsensitive : Qt::CaseSensitive;
	report->game = levelEntityConnectionGame(document,request.game);
	report->property = request.mode == LevelEntityConnectionMode::RadiantKillTarget ? QStringLiteral("killtarget") : QStringLiteral("target");
	report->targetEntity = selection.entities.takeLast(); report->sourceEntities = selection.entities;
	const auto& target = document.entities[selection.indexes.value(report->targetEntity)];
	auto name = property(target,QStringLiteral("targetname"),sensitivity);
	if (legacy) { name = name.trimmed(); }
	if (!legacy || name.isEmpty()) {
		for (int id : selection.entities) {
			auto existing = property(document.entities[selection.indexes.value(id)],report->property,sensitivity);
			if (legacy) { existing = existing.trimmed(); }
			if (!existing.isEmpty()) { name = existing; break; }
		}
	}
	if (name.isEmpty()) {
		if (legacy) {
			QSet<QString> used;
			for (const auto& entity : document.entities) {
				for (const auto& p : entity.properties) { placementCancellationCheckpoint(); used.insert(p.value.trimmed().toLower()); }
			}
			quint64 next = 1;
			do { placementCancellationCheckpoint(); name = QStringLiteral("t%1").arg(next++); } while (used.contains(name));
		} else {
			const auto className = property(target,QStringLiteral("classname"),Qt::CaseSensitive);
			name = makeLevelMapNameUnique(document.entities,(className.isEmpty() ? QStringLiteral("t") : className)+QLatin1Char('1'));
		}
	}
	report->targetName = name;
	command->commandKind = QStringLiteral("transform-objects"); command->objectKind = QStringLiteral("entity"); command->objectId = target.id;
	const auto update = [&](const LevelMapEntity& entity, const QString& key) {
		placementCancellationCheckpoint();
		auto changed = entity;
		if (setProperty(&changed,key,name,sensitivity)) { command->entitySnapshots << entity; command->entityResults << changed; }
	};
	if (!legacy || property(target,QStringLiteral("targetname"),sensitivity).trimmed().isEmpty()) { update(target,QStringLiteral("targetname")); }
	for (int id : selection.entities) { update(document.entities[selection.indexes.value(id)],report->property); }
	report->changedEntities = static_cast<int>(command->entityResults.size());
	if (!report->changedEntities && legacy) {
		if (error) { *error = QCoreApplication::translate("VibeStudioLevelMap","Those entities already target %1.").arg(name); } return false;
	}
	command->description = selection.entities.size() == 1
		? QCoreApplication::translate("VibeStudioLevelMap","Connect entity:%1 to entity:%2 as %3").arg(selection.entities.first()).arg(target.id).arg(name)
		: QCoreApplication::translate("VibeStudioLevelMap","Connect %1 entities to entity:%2 as %3").arg(selection.entities.size()).arg(target.id).arg(name);
	if (request.mode == LevelEntityConnectionMode::RadiantKillTarget) {
		command->description = QCoreApplication::translate("LevelEntityConnections","KillConnect entity:%1 to entity:%2 as %3").arg(selection.entities.first()).arg(target.id).arg(name);
	}
	command->undoDescription = QCoreApplication::translate("VibeStudioLevelMap","Disconnect them from entity:%1 again").arg(target.id);
	return true;
}
} // namespace vibestudio
