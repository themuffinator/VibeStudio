#include "core/level_linked_extraction.h"
#include "core/level_history_p.h"
#include "core/level_linked_groups.h"
#include "core/level_linked_groups_p.h"
#include "core/level_linked_objects.h"
#include "core/level_placement_control_p.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QUuid>
#include <algorithm>
#include <cmath>

// Independently implemented workflow reference: TrenchBroom v2026.2,
// Map_Groups.cpp::extractLinkedGroups, GPL-3.0-or-later. See docs/CREDITS.md.
// Clone each instance before pruning so local entity properties survive.
// Nested groups move whole and keep their links, as TrenchBroom's do.
namespace vibestudio
{
namespace
{
bool fail(QString* error, const char* message)
{
	if (error) {
		*error = QCoreApplication::translate("LevelLinkedExtraction", message);
	}
	return false;
}
LevelMapSelectionRef ref(const QString& object)
{
	return {levelMapSelectionKindFromId(object.section(':', 0, 0)), object.section(':', 1).toInt()};
}
QString entityObject(int id)
{
	return QStringLiteral("entity:%1").arg(id);
}
struct Index {
	QHash<QString, QString> owners;
	QHash<QString, QSet<QString>> children;
	QHash<QString, QString> memberships;
	QSet<QString> world;
	explicit Index(const LevelMapDocument& document)
	{
		for (const auto& entity : document.entities) {
			if (entity.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) == 0) {
				world.insert(entityObject(entity.id));
			}
		}
		const auto append = [&](const auto& primitives, LevelMapSelectionKind kind) {
			for (const auto& primitive : primitives) {
				const auto object = levelMapSelectionRefId({kind, primitive.id}), owner = entityObject(primitive.entityId);
				owners.insert(object, owner);
				children[owner].insert(object);
			}
		};
		append(document.brushes, LevelMapSelectionKind::QuakeBrush);
		append(document.patches, LevelMapSelectionKind::QuakePatch);
		for (const auto& node : document.scene.nodes) {
			for (const auto& object : node.objects) {
				memberships.insert(object, node.id);
			}
		}
	}
};
// The content of a group and of the groups nested in it.
QSet<QString> contentOf(const LevelMapDocument& document, const LevelSceneState& state, const QString& id)
{
	return levelLinkedObjects(document, levelLinkedGroupMembers(state, id));
}
// The linked groups holding `id`, innermost first.
QStringList linkedAncestors(const LevelSceneState& state, const QString& id)
{
	QStringList ancestors;
	const auto* node = levelSceneNode(state, id);
	for (QString at = node ? node->parentId : QString(); !at.isEmpty();) {
		const auto* ancestor = levelSceneNode(state, at);
		if (!ancestor) {
			break;
		}
		if (!ancestor->linkId.isEmpty()) {
			ancestors << at;
		}
		at = ancestor->parentId;
	}
	return ancestors;
}
// Whether a linked group's baseline still describes its content, with no
// change waiting to reach the rest of its set.
bool settled(const LevelMapDocument& document, const LevelSceneNode& node)
{
	const auto content = levelLinkedGroupContent(document, node.id);
	const auto same = [](double a, double b) { return std::round(a * 1000.0) == std::round(b * 1000.0); };
	return !node.linkShape.isEmpty() && content.shape == node.linkShape && content.surface == node.linkSurface &&
	       same(content.anchor.x, node.linkAnchor[0]) && same(content.anchor.y, node.linkAnchor[1]) &&
	       same(content.anchor.z, node.linkAnchor[2]);
}
struct Instance {
	LevelSceneNode node;
	QSet<QString> chosen, keptOwners;
	// Groups nested in the instance that move into its extracted group whole.
	QStringList moved;
	QVector<LevelMapSelectionRef> removeOriginal;
};
struct Plan {
	QString source;
	QVector<Instance> instances;
};

bool plan(const LevelMapDocument& document, Plan* result, QString* error)
{
	if (error) {
		error->clear();
	}
	if (document.format != LevelMapFormat::QuakeMap && document.format != LevelMapFormat::Quake3Map) {
		return fail(error, QT_TRANSLATE_NOOP("LevelLinkedExtraction", "Linked extraction needs a Quake-family map."));
	}
	const auto* source = document.openSceneGroups.isEmpty() ? nullptr : levelSceneNode(document.scene, document.openSceneGroups.last());
	if (!source || source->linkId.isEmpty() || document.selection.isEmpty()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelLinkedExtraction", "Open a linked group and select some of its contents to extract."));
	}
	if (!validateLevelScene(document, document.scene, error)) {
		return false;
	}
	const auto family = levelLinkedGroupNodes(document.scene, source->id);
	if (family.size() < 2) {
		return fail(error, QT_TRANSLATE_NOOP("LevelLinkedExtraction", "The open group needs at least one linked counterpart."));
	}
	if (document.scene.nodes.size() + family.size() > kLevelSceneMaxNodes) {
		return fail(error, QT_TRANSLATE_NOOP("LevelLinkedExtraction", "The extracted groups would exceed the scene's 1,000-node limit."));
	}
	// The linked groups around the set's members take the same new structure
	// only when each holds as many of them as its counterparts do.
	QHash<QString, int> held;
	for (const auto& id : family) {
		for (const auto& ancestor : linkedAncestors(document.scene, id)) {
			++held[ancestor];
		}
	}
	for (auto it = held.cbegin(); it != held.cend(); ++it) {
		for (const auto& peer : levelLinkedGroupNodes(document.scene, it.key())) {
			if (held.value(peer) != it.value()) {
				return fail(error, QT_TRANSLATE_NOOP("LevelLinkedExtraction",
				                                     "A linked group around the open group is missing its counterparts. Use Update Linked "
				                                     "Groups before extraction."));
			}
		}
	}
	const auto sourceObjects = levelLinkedObjects(document, source->objects);
	const Index index(document);
	const auto context = levelSceneSelectionContext(document);
	QSet<QString> selectedObjects;
	for (const auto& selected : document.selection) {
		selectedObjects.insert(levelMapSelectionRefId(selected));
	}
	QSet<QString> selectedIdentities;
	QStringList units;
	for (const auto& object : selectedObjects) {
		// A closed group nested in the open one is selected as a unit.
		if (const auto unit = context.closedGroups.value(object); !unit.isEmpty()) {
			if (!units.contains(unit)) {
				units << unit;
			}
			continue;
		}
		if (!sourceObjects.contains(object)) {
			return fail(error, QT_TRANSLATE_NOOP("LevelLinkedExtraction", "Select contents from the open linked group only."));
		}
		// Selecting an owner already selects its entire geometry in every
		// instance. A redundant child must not impose an extra correspondence.
		if (selectedObjects.contains(index.owners.value(object))) {
			continue;
		}
		if (!source->linkObjects.contains(object)) {
			return fail(error, QT_TRANSLATE_NOOP("LevelLinkedExtraction",
			                                     "Use Update Linked Groups to establish object correspondence before extraction."));
		}
		selectedIdentities.insert(source->linkObjects.value(object));
	}
	// Each instance's counterpart of a nested group shares its link and holds
	// the same objects, by identity.
	struct Unit {
		QString link;
		QSet<QString> identities;
	};
	QVector<Unit> chosenUnits;
	for (const auto& id : std::as_const(units)) {
		const auto members = context.groupSelections.value(id);
		if (!std::all_of(members.cbegin(), members.cend(),
		                 [&](const auto& member) { return selectedObjects.contains(levelMapSelectionRefId(member)); })) {
			return fail(error, QT_TRANSLATE_NOOP("LevelLinkedExtraction",
			                                     "Select nested groups whole to extract them, or open a nested group to extract part of it."));
		}
		Unit unit{levelSceneNode(document.scene, id)->linkId, {}};
		for (const auto& object : contentOf(document, document.scene, id)) {
			if (!source->linkObjects.contains(object)) {
				unit.link.clear();
				break;
			}
			unit.identities.insert(source->linkObjects.value(object));
		}
		if (unit.link.isEmpty()) {
			return fail(error, QT_TRANSLATE_NOOP("LevelLinkedExtraction",
			                                     "Use Update Linked Groups to establish object correspondence before extraction."));
		}
		chosenUnits << unit;
	}
	const auto lockedNodes = levelSceneLockedNodes(document.scene), lockedObjects = levelSceneLockedObjects(document);
	Plan prepared;
	prepared.source = source->id;
	for (const auto& id : family) {
		detail::placementCancellationCheckpoint();
		Instance instance;
		instance.node = *levelSceneNode(document.scene, id);
		const auto direct = levelLinkedObjects(document, instance.node.objects);
		if (lockedNodes.contains(id) || direct.intersects(lockedObjects)) {
			return fail(error, QT_TRANSLATE_NOOP("LevelLinkedExtraction",
			                                     "Unlock every affected linked group and its owned geometry before extraction."));
		}
		const auto objects = contentOf(document, document.scene, id);
		const QSet<QString> known(instance.node.linkObjects.keyBegin(), instance.node.linkObjects.keyEnd());
		if (known != objects) {
			return fail(error, QT_TRANSLATE_NOOP("LevelLinkedExtraction",
			                                     "Use Update Linked Groups to establish object correspondence before extraction."));
		}
		QHash<QString, QString> correspondence;
		for (auto it = instance.node.linkObjects.cbegin(); it != instance.node.linkObjects.cend(); ++it) {
			correspondence.insert(it.value(), it.key());
		}
		for (const auto& identity : selectedIdentities) {
			if (!direct.contains(correspondence.value(identity))) {
				return fail(error,
				            QT_TRANSLATE_NOOP("LevelLinkedExtraction",
				                              "A linked group is missing the selected contents. Use Update Linked Groups before extraction."));
			}
			const auto object = correspondence.value(identity);
			instance.chosen.insert(object);
			instance.chosen.unite(index.children.value(object));
		}
		QStringList nested;
		for (const auto& node : document.scene.nodes) {
			if (node.parentId == id && node.kind == LevelSceneNodeKind::Group) {
				nested << node.id;
			}
		}
		for (const auto& unit : std::as_const(chosenUnits)) {
			const auto match = std::find_if(nested.cbegin(), nested.cend(), [&](const QString& child) {
				if (levelSceneNode(document.scene, child)->linkId != unit.link || instance.moved.contains(child)) {
					return false;
				}
				QSet<QString> identities;
				for (const auto& object : contentOf(document, document.scene, child)) {
					identities.insert(instance.node.linkObjects.value(object));
				}
				return identities == unit.identities;
			});
			if (match == nested.cend()) {
				return fail(error,
				            QT_TRANSLATE_NOOP("LevelLinkedExtraction",
				                              "A linked group is missing the selected contents. Use Update Linked Groups before extraction."));
			}
			// Moving a group changes everything in it.
			QStringList subtree = levelLinkedGroupDescendants(document.scene, *match);
			subtree << *match;
			if (contentOf(document, document.scene, *match).intersects(lockedObjects) ||
			    std::any_of(subtree.cbegin(), subtree.cend(), [&](const QString& node) { return lockedNodes.contains(node); })) {
				return fail(error, QT_TRANSLATE_NOOP("LevelLinkedExtraction",
				                                     "Unlock every affected linked group and its owned geometry before extraction."));
			}
			instance.moved << *match;
		}
		bool remainder = false;
		for (const auto& object : objects) {
			detail::placementCancellationCheckpoint();
			const auto owner = index.owners.value(object);
			if (!owner.isEmpty() && !index.world.contains(owner)) {
				if (!objects.contains(owner)) {
					return fail(error, QT_TRANSLATE_NOOP("LevelLinkedExtraction",
					                                     "Assign each complete brush entity to its linked group before extraction."));
				}
				const auto membership = index.memberships.value(object);
				if (!membership.isEmpty() && membership != index.memberships.value(owner)) {
					return fail(
					    error,
					    QT_TRANSLATE_NOOP(
					        "LevelLinkedExtraction",
					        "A brush entity's geometry is assigned outside its linked group. Reconcile its membership before extraction."));
				}
				if (instance.chosen.contains(object)) {
					instance.keptOwners.insert(owner);
				}
			}
			if (direct.contains(object) && !instance.chosen.contains(object) && index.children.value(object).isEmpty()) {
				remainder = true;
			}
		}
		for (const auto& child : std::as_const(nested)) {
			remainder |= !instance.moved.contains(child) && !levelLinkedGroupMembers(document.scene, child).isEmpty();
		}
		if (!remainder) {
			return fail(
			    error, QT_TRANSLATE_NOOP("LevelLinkedExtraction",
			                             "Leave some contents in every original instance; extracting the whole group would not split it."));
		}
		for (const auto& object : instance.chosen) {
			if (!instance.chosen.contains(index.owners.value(object))) {
				instance.removeOriginal << ref(object);
			}
		}
		prepared.instances << std::move(instance);
	}
	*result = std::move(prepared);
	return true;
}
QString extractedName(const LevelSceneState& state, const LevelSceneNode& source)
{
	auto base = QCoreApplication::translate("LevelLinkedExtraction", "%1 Extracted").arg(source.name).left(112).trimmed();
	if (!base.isEmpty() && base.back().isHighSurrogate()) {
		base.chop(1);
	}
	QString name = base;
	for (int number = 2;; ++number) {
		const bool used = std::any_of(state.nodes.cbegin(), state.nodes.cend(), [&](const auto& node) {
			return node.parentId == source.parentId && node.name.compare(name, Qt::CaseInsensitive) == 0;
		});
		if (!used) {
			return name;
		}
		name = QStringLiteral("%1 %2").arg(base).arg(number);
	}
}
LevelSceneEditingContext context(const LevelMapDocument& document)
{
	return {document.openSceneGroups, document.activeSceneNode, document.sceneCreationBeforeOpen};
}
} // namespace

bool canExtractLevelLinkedGroups(const LevelMapDocument& document, QString* error)
{
	Plan prepared;
	return plan(document, &prepared, error);
}

bool extractLevelLinkedGroups(LevelMapDocument* document, LevelLinkedExtractionReport* report, QString* error)
{
	if (!document) {
		return fail(error, QT_TRANSLATE_NOOP("LevelLinkedExtraction", "No map is open."));
	}
	Plan prepared;
	if (!plan(*document, &prepared, error)) {
		return false;
	}
	// The linked groups around the instances all change the same way. Those
	// with no change pending take the new structure as their baseline; a
	// deferred edit to another stays visible to its propagation.
	QSet<QString> settledGroups;
	for (const auto& instance : prepared.instances) {
		for (const auto& ancestor : linkedAncestors(document->scene, instance.node.id)) {
			if (!settledGroups.contains(ancestor) && settled(*document, *levelSceneNode(document->scene, ancestor))) {
				settledGroups.insert(ancestor);
			}
		}
	}
	const auto beforeContext = context(*document);
	const auto beforeSelection = document->selection;
	const Index originalIndex(*document);
	auto candidate = *document;
	// Extraction deliberately leaves the child editing context to change its
	// corresponding instances. Ordinary lock guards still protect every step.
	closeLevelSceneGroup(&candidate, true);
	setLevelMapSelection(&candidate, beforeSelection);
	LevelLinkedExtractionReport staged;
	staged.sourceGroup = prepared.source;
	const auto link = QUuid::createUuid().toString(QUuid::WithoutBraces);
	const auto apply = [&](LevelMapDocument* working) {
		const detail::LevelMapHistoryScope history(*working);
		const detail::LevelLinkedUpdateScope quiet;
		const auto before = working->undoStack.size();
		// A split owner's new half has one identity in each set around it.
		QHash<QString, QString> splitIdentities;
		int completed = 0;
		for (const auto& instance : prepared.instances) {
			detail::placementCheckpoint(LevelPlacementPhase::Extracting, completed, prepared.instances.size());
			const auto sceneBefore = working->scene;
			QHash<QString, QString> origins;
			QSet<QString> keptCopies;
			if (!instance.chosen.isEmpty()) {
				const auto roots = levelLinkedContent(*working, instance.node.objects).roots;
				if (!duplicateLevelMapObjects(working, roots, 0, 0, 0, {true, false}, error, false)) {
					return false;
				}
				origins = working->undoStack.last().sceneObjectOrigins;
				QVector<LevelMapSelectionRef> removeCopies;
				for (auto it = origins.cbegin(); it != origins.cend(); ++it) {
					const auto source = it.value();
					if (instance.chosen.contains(source) || instance.keptOwners.contains(source)) {
						keptCopies.insert(it.key());
					} else if (instance.chosen.contains(originalIndex.owners.value(source)) ||
					           instance.keptOwners.contains(originalIndex.owners.value(source)) ||
					           originalIndex.world.contains(originalIndex.owners.value(source)) ||
					           originalIndex.owners.value(source).isEmpty()) {
						removeCopies << ref(it.key());
					}
				}
				if ((!removeCopies.isEmpty() && !deleteLevelMapObjects(working, removeCopies, error)) ||
				    !deleteLevelMapObjects(working, instance.removeOriginal, error)) {
					return false;
				}
			}
			LevelSceneState state = working->scene;
			LevelSceneNode extracted = instance.node;
			extracted.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
			extracted.name = extractedName(state, instance.node);
			extracted.linkId = link;
			extracted.objects.clear();
			extracted.linkObjects.clear();
			for (auto it = origins.cbegin(); it != origins.cend(); ++it) {
				if (!keptCopies.contains(it.key())) {
					continue;
				}
				extracted.linkObjects.insert(it.key(), instance.node.linkObjects.value(it.value()));
				if (instance.node.objects.contains(it.value())) {
					extracted.objects << it.key();
				}
			}
			extracted.objects.sort();
			// Nested groups keep their objects, so those keep their identities.
			for (const auto& id : instance.moved) {
				for (const auto& object : contentOf(*working, sceneBefore, id)) {
					extracted.linkObjects.insert(object, instance.node.linkObjects.value(object));
				}
			}
			int sourceIndex = -1;
			for (int i = 0; i < state.nodes.size(); ++i) {
				auto& node = state.nodes[i];
				node.objects.removeIf([&](const auto& object) { return keptCopies.contains(object); });
				if (node.id == instance.node.id) {
					sourceIndex = i;
					node.linkObjects = instance.node.linkObjects;
				} else if (instance.moved.contains(node.id)) {
					node.parentId = extracted.id;
				}
			}
			state.nodes.insert(sourceIndex + 1, extracted);
			// The linked groups around the instance know each copy by the
			// identity its original had there. Every counterpart makes the same
			// copies, so the sets stay matched.
			for (const auto& ancestor : linkedAncestors(sceneBefore, instance.node.id)) {
				const auto known = levelSceneNode(sceneBefore, ancestor)->linkObjects;
				auto node = std::find_if(state.nodes.begin(), state.nodes.end(), [&](const auto& each) { return each.id == ancestor; });
				if (known.isEmpty() || node == state.nodes.end()) {
					continue;
				}
				for (auto it = origins.cbegin(); it != origins.cend(); ++it) {
					if (!keptCopies.contains(it.key()) || !known.contains(it.value())) {
						continue;
					}
					auto identity = known.value(it.value());
					if (levelMapObjectExists(*working, ref(it.value()))) {
						const auto key = node->linkId + QLatin1Char('/') + identity;
						if (!splitIdentities.contains(key)) {
							splitIdentities.insert(key, QUuid::createUuid().toString(QUuid::WithoutBraces));
						}
						identity = splitIdentities.value(key);
					}
					node->linkObjects.insert(it.key(), identity);
				}
			}
			// Commit reconciles surviving source IDs. Refresh only the two halves;
			// accepting another family's pending edit here would conceal it from
			// the caller's deferred propagation after a compound transaction.
			refreshLevelLinkBaselines(*working, &state, QSet<QString>{instance.node.id, extracted.id});
			if (!commitLevelSceneState(working, state, QCoreApplication::translate("LevelLinkedExtraction", "Split linked group"),
			                           error)) {
				return false;
			}
			staged.createdGroups << extracted.id;
			staged.nestedGroups += static_cast<int>(instance.moved.size());
			if (instance.node.id == prepared.source) {
				staged.extractedGroup = extracted.id;
			}
			for (const auto& owner : instance.keptOwners) {
				if (levelMapObjectExists(*working, ref(owner))) {
					++staged.splitEntities;
				}
			}
			++completed;
		}
		if (!settledGroups.isEmpty()) {
			LevelSceneState state = working->scene;
			refreshLevelLinkBaselines(*working, &state, settledGroups);
			if (!commitLevelSceneState(working, state, QCoreApplication::translate("LevelLinkedExtraction", "Split linked group"),
			                           error)) {
				return false;
			}
		}
		detail::placementCheckpoint(LevelPlacementPhase::Finalizing);
		working->openSceneGroups = beforeContext.openGroups;
		working->openSceneGroups.removeLast();
		working->activeSceneNode =
		    working->openSceneGroups.isEmpty()
		        ? (levelSceneNode(working->scene, beforeContext.previousCreationNode) ? beforeContext.previousCreationNode : QString{})
		        : working->openSceneGroups.last();
		working->sceneCreationBeforeOpen = working->openSceneGroups.isEmpty() ? QString{} : beforeContext.previousCreationNode;
		setLevelMapSelection(working, levelSceneSelection(*working, staged.extractedGroup));
		if (!collapseLevelMapUndoSteps(working, static_cast<int>(working->undoStack.size() - before),
		                               QCoreApplication::translate("LevelLinkedExtraction", "Extract linked groups"),
		                               QCoreApplication::translate("LevelLinkedExtraction", "Restore linked groups"), error)) {
			return false;
		}
		auto& command = working->undoStack.last();
		command.hasSceneContextSnapshot = true;
		command.sceneContextBefore = beforeContext;
		command.sceneContextAfter = context(*working);
		command.selectionSnapshot = beforeSelection;
		command.selectionResult = working->selection;
		return true;
	};
	if (const auto guarded = guardLevelSceneEdit(&candidate, error, apply); guarded.has_value()) {
		if (!*guarded) {
			return false;
		}
	} else if (!apply(&candidate)) {
		return false;
	}
	if (!validateLevelScene(candidate, candidate.scene, error)) {
		return false;
	}
	QHash<QString, QString> emitted;
	for (const auto& object : levelSceneObjects(candidate)) {
		emitted.insert(object, object);
	}
	QString encodingError;
	encodeLevelScene(candidate, QByteArray(32, '\0'), emitted, &encodingError);
	if (!encodingError.isEmpty()) {
		if (error) {
			*error = encodingError;
		}
		return false;
	}
	detail::placementCancellationCheckpoint();
	*document = std::move(candidate);
	if (report) {
		*report = std::move(staged);
	}
	return true;
}
QJsonObject levelLinkedExtractionReportJson(const LevelLinkedExtractionReport& report)
{
	QJsonArray groups;
	for (const auto& id : report.createdGroups) {
		groups.append(id);
	}
	return {{QStringLiteral("sourceGroup"), report.sourceGroup},
	        {QStringLiteral("extractedGroup"), report.extractedGroup},
	        {QStringLiteral("createdGroups"), groups},
	        {QStringLiteral("instances"), groups.size()},
	        {QStringLiteral("nestedGroups"), report.nestedGroups},
	        {QStringLiteral("splitEntities"), report.splitEntities}};
}
} // namespace vibestudio
