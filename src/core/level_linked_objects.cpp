#include "core/level_linked_objects.h"
#include "core/level_linked_groups.h"
#include "core/level_scene.h"

#include <QCoreApplication>
#include <QUuid>

// Original correspondence implementation. The behavioural reference is
// TrenchBroom v2026.2 Map_Groups.cpp (per-node link IDs), GPL-3.0-or-later:
// https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/Map_Groups.cpp
// VibeStudio uses its own scene codec and UUID mapping; see docs/CREDITS.md.
namespace vibestudio
{

QSet<QString> levelLinkedObjects(const LevelMapDocument& document, const QStringList& members)
{
	const QSet<QString> explicitObjects(members.cbegin(), members.cend());
	QSet<QString> objects;
	QSet<int> owners;
	for (const auto& entity : document.entities) {
		const auto object = levelMapSelectionRefId({LevelMapSelectionKind::Entity, entity.id});
		if (explicitObjects.contains(object) && entity.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) != 0) {
			objects.insert(object);
			owners.insert(entity.id);
		}
	}
	const auto include = [&](const auto& primitives, LevelMapSelectionKind kind) {
		for (const auto& primitive : primitives) {
			const auto object = levelMapSelectionRefId({kind, primitive.id});
			if (owners.contains(primitive.entityId) || explicitObjects.contains(object)) {
				objects.insert(object);
			}
		}
	};
	include(document.brushes, LevelMapSelectionKind::QuakeBrush);
	include(document.patches, LevelMapSelectionKind::QuakePatch);
	return objects;
}

QMap<QString, QString> levelLinkObjectIdentities(const LevelMapDocument& document, const LevelSceneState& state, const LevelSceneNode& node,
                                                 const QHash<QString, QString>& remap)
{
	const auto objects = levelLinkedObjects(document, node.id.isEmpty() ? node.objects : levelLinkedGroupMembers(state, node.id));
	QMap<QString, QString> identities;
	for (auto it = node.linkObjects.cbegin(); it != node.linkObjects.cend(); ++it) {
		const auto now = remap.value(it.key(), it.key());
		if (objects.contains(now)) {
			identities.insert(now, it.value());
		}
	}
	for (const auto& object : objects) {
		if (!identities.contains(object)) {
			identities.insert(object, QUuid::createUuid().toString(QUuid::WithoutBraces));
		}
	}
	return identities;
}

void reconcileLevelLinkedObjects(const LevelMapDocument& document, LevelSceneState* state, const QHash<QString, QString>& remap)
{
	if (!state) {
		return;
	}
	for (auto& node : state->nodes) {
		if (node.linkId.isEmpty()) {
			node.linkObjects.clear();
		} else if (!node.linkObjects.isEmpty()) {
			node.linkObjects = levelLinkObjectIdentities(document, *state, node, remap);
		}
	}
}

bool validateLevelLinkedObjects(const LevelMapDocument& document, const LevelSceneState& state, QString* error)
{
	QHash<QString, QString> kinds;
	qsizetype count = 0;
	for (const auto& node : state.nodes) {
		if (node.linkObjects.isEmpty()) {
			continue;
		}
		if (node.linkId.isEmpty()) {
			if (error) {
				*error = QCoreApplication::translate("LevelScene", "Only linked groups can have linked object identities.");
			}
			return false;
		}
		const auto objects = levelLinkedObjects(document, levelLinkedGroupMembers(state, node.id));
		QSet<QString> used;
		for (auto it = node.linkObjects.cbegin(); it != node.linkObjects.cend(); ++it) {
			const QUuid uuid(it.value());
			if (++count > kLevelSceneMaxMembers || !objects.contains(it.key()) || uuid.isNull() ||
			    uuid.toString(QUuid::WithoutBraces) != it.value() || used.contains(it.value())) {
				if (error) {
					*error = QCoreApplication::translate("LevelScene", "Linked object identities must be canonical UUIDs, unique within "
					                                                   "each group, refer to its content and total at most 100,000.");
				}
				return false;
			}
			used.insert(it.value());
			const auto identity = node.linkId + QLatin1Char('/') + it.value();
			const auto kind = it.key().section(QLatin1Char(':'), 0, 0);
			if (kinds.contains(identity) && kinds.value(identity) != kind) {
				if (error) {
					*error = QCoreApplication::translate("LevelScene", "Corresponding linked objects must have the same native kind.");
				}
				return false;
			}
			kinds.insert(identity, kind);
		}
	}
	return true;
}

} // namespace vibestudio
