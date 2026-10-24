#include "core/level_object_selection.h"
#include "core/level_entity_selection_p.h"
#include "core/level_visibility.h"
#include "core/level_history_p.h"
#include "core/level_placement_control_p.h"
#include "core/level_scene.h"
#include "core/level_scene_layers.h"
#include "core/level_scene_locks.h"
#include <QCoreApplication>
#include <QRegularExpression>
#include <algorithm>
#include <limits>

namespace vibestudio {
namespace {
struct Text { Q_DECLARE_TR_FUNCTIONS(LevelObjectSelection) };
constexpr qsizetype maxLines = 16384;
struct Object {
	LevelMapSelectionRef ref;
	QString owner, parent, ownerParent, group;
	int first = 0, last = 0;
	bool world = false;
};
QString sceneParent(const QString& id) { return QStringLiteral("scene:") + id; }
}

bool parseLevelMapSourceLines(const QString& text, QVector<int>* lines, QString* error)
{
	if (error) { error->clear(); }
	const auto fail = [&] { if (error) { *error = Text::tr("Enter 1–16,384 positive line numbers separated by commas or spaces."); } return false; };
	if (!lines || text.size() > 256 * 1024) { return fail(); }
	const auto tokens = text.split(QRegularExpression(QStringLiteral("[,\\s]+")),Qt::SkipEmptyParts);
	if (tokens.isEmpty() || tokens.size() > maxLines) { return fail(); }
	QVector<int> parsed; parsed.reserve(tokens.size());
	for (const auto& token : tokens) {
		int value = 0;
		for (QChar character : token) {
			// Decimal digits also admit Arabic/Indic and full-width keyboards.
			// Signs, grouping separators and exponent notation are not positions.
			const int digit = character.digitValue();
			if (!character.isDigit() || digit < 0 || value > (std::numeric_limits<int>::max()-digit)/10) { return fail(); }
			value = value*10 + digit;
		}
		if (value < 1) { return fail(); } parsed.append(value);
	}
	std::sort(parsed.begin(),parsed.end()); parsed.erase(std::unique(parsed.begin(),parsed.end()),parsed.end());
	*lines = std::move(parsed); return true;
}

// Original implementation from TB v2026.2 Map_Selection.cpp's selectSiblingNodes
// and selectNodesWithFilePosition behaviour, adapted to VibeStudio's native
// object ids and scene metadata. See docs/CREDITS.md for source and licence.
bool selectLevelMapObjects(LevelMapDocument* document, const LevelObjectSelectionRequest& request,
	LevelObjectSelectionReport* report, QString* error)
{
	if (request.mode == LevelObjectSelectionMode::ConnectedEntities || request.mode == LevelObjectSelectionMode::RadiantType) {
		return detail::selectLevelMapEntityQuery(document,request,report,error);
	}
	if (request.mode == LevelObjectSelectionMode::Layers) {
		if (request.material || request.typeScope != LevelTypeSelectionScope::Objects || !request.lines.isEmpty()) {
			if (error) { *error = Text::tr("Expand Selection to Layers takes no material, component scope or source lines."); }
			return false;
		}
		LevelSceneLayerSelectionReport layers;
		if (!expandLevelMapSelectionToLayers(document,{request.excludedObjects,request.undoSelection},&layers,error)) { return false; }
		if (report) { *report = {}; report->selection = layers.selection; report->layers = layers.layers; report->skippedGroups = layers.skippedGroups; }
		return true;
	}
	if (error) { error->clear(); }
	const auto fail = [&](const QString& message) { if (error) { *error = message; } return false; };
	if (request.material || request.typeScope != LevelTypeSelectionScope::Objects) {
		return fail(Text::tr("Material and component scope are only valid for Select All of Type."));
	}
	if (!document || (document->format != LevelMapFormat::QuakeMap && document->format != LevelMapFormat::Quake3Map)) {
		return fail(Text::tr("Object hierarchy and source-line selection require a Quake-family text map."));
	}
	if (request.mode != LevelObjectSelectionMode::Siblings && request.mode != LevelObjectSelectionMode::SourceLines) {
		return fail(Text::tr("Choose sibling or source-line selection."));
	}
	const bool sourceLines = request.mode == LevelObjectSelectionMode::SourceLines;
	if (sourceLines && (request.lines.isEmpty() || request.lines.size() > maxLines
		|| std::any_of(request.lines.cbegin(),request.lines.cend(),[](int line) { return line < 1; }))) {
		return fail(Text::tr("Choose 1–16,384 positive source line numbers."));
	}
	if (!sourceLines && !request.lines.isEmpty()) { return fail(Text::tr("Sibling selection does not accept source line numbers.")); }
	if (!document->scene.opaqueMetadata.isEmpty() || !document->scene.problem.isEmpty()) {
		return fail(Text::tr("Resolve the map's unrecognised scene metadata before selecting by hierarchy or source line."));
	}
	detail::placementCheckpoint(LevelPlacementPhase::SelectingObjects);
	const auto context = levelSceneSelectionContext(*document);
	auto excluded = levelMapHiddenObjects(*document) | levelSceneLockedObjects(*document) | request.excludedObjects;
	QHash<QString,QString> membership;
	QHash<QString,const LevelSceneNode*> nodes;
	for (const auto& node : document->scene.nodes) {
		nodes.insert(node.id,&node);
		for (const auto& key : node.objects) { membership.insert(key,node.id); }
	}
	QHash<QString,Object> objects;
	QHash<QString,QStringList> children;
	QStringList order;
	const auto append = [&](Object object) { const auto key = levelMapSelectionRefId(object.ref); order.append(key); objects.insert(key,std::move(object)); };
	for (const auto& entity : document->entities) {
		detail::placementCancellationCheckpoint();
		Object object; object.ref = {LevelMapSelectionKind::Entity,entity.id}; object.first = entity.startLine; object.last = entity.endLine;
		object.world = entity.className.compare(QLatin1String("worldspawn"),Qt::CaseInsensitive) == 0;
		object.parent = sceneParent(membership.value(levelMapSelectionRefId(object.ref))); append(std::move(object));
	}
	const auto appendChildren = [&](const auto& records, LevelMapSelectionKind kind) {
		for (const auto& record : records) {
			detail::placementCancellationCheckpoint();
			Object object; object.ref = {kind,record.id}; object.first = record.startLine; object.last = record.endLine;
			const auto key = levelMapSelectionRefId(object.ref);
			object.owner = levelMapSelectionRefId({LevelMapSelectionKind::Entity,record.entityId});
			const auto owner = objects.constFind(object.owner);
			const auto node = membership.value(key,membership.value(object.owner));
			// Explicit child scene membership takes precedence over its owner's
			// inherited membership. VibeStudio allows this unlike TB's node tree.
			object.parent = owner != objects.cend() && !owner->world ? object.owner + QLatin1Char('/') + node : sceneParent(node);
			if (owner != objects.cend() && !owner->world) { object.ownerParent = sceneParent(node); }
			if (excluded.contains(object.owner)) { excluded.insert(key); }
			children[object.owner].append(key); append(std::move(object));
		}
	};
	appendChildren(document->brushes,LevelMapSelectionKind::QuakeBrush);
	appendChildren(document->patches,LevelMapSelectionKind::QuakePatch);
	QSet<QString> excludedGroups;
	for (const auto& key : excluded) {
		const auto group = context.closedGroups.value(key); if (!group.isEmpty()) { excludedGroups.insert(group); }
	}
	for (auto it = objects.begin(); it != objects.end(); ++it) {
		it->group = context.closedGroups.value(it.key());
		if (!it->group.isEmpty()) {
			const auto* node = nodes.value(it->group);
			it->parent = sceneParent(node ? node->parentId : QString{}); it->ownerParent.clear();
		}
	}
	// A closed group's entity wrapper also transforms its owned primitives.
	// Never let that compact representation bypass an excluded child's lock,
	// visibility, filter or independently assigned scene boundary.
	for (auto it = context.groupSelections.cbegin(); it != context.groupSelections.cend(); ++it) {
		for (const auto& ref : it.value()) {
			for (const auto& child : children.value(levelMapSelectionRefId(ref))) {
				if (excluded.contains(child) || !context.allowedObjects.contains(child) || context.closedGroups.value(child) != it.key()) {
					excludedGroups.insert(it.key()); break;
				}
			}
		}
	}
	const auto selectable = [&](const QString& key) {
		const auto it = objects.constFind(key);
		return it != objects.cend() && !it->world && context.allowedObjects.contains(key) && !excluded.contains(key)
			&& !excludedGroups.contains(it->group);
	};
	QSet<QString> hits;
	if (!sourceLines) {
		if (document->selection.isEmpty()) { if (report) { report->selection.clear(); } return true; }
		QSet<QString> parents;
		for (const auto& ref : document->selection) {
			const auto key = levelMapSelectionRefId(ref);
			if (!selectable(key)) { return fail(Text::tr("A selected object is missing, hidden, locked, filtered or outside the open group: %1.").arg(key)); }
			const auto& object = objects[key];
			// A selected brush-entity wrapper represents its children in TB.
			if (object.group.isEmpty() && children.contains(key)) {
				for (const auto& child : children.value(key)) { if (selectable(child)) { parents.insert(objects[child].parent); } }
			} else { parents.insert(object.parent); }
		}
		for (const auto& key : order) {
			const auto& object = objects[key];
			if (parents.contains(object.parent) || (!object.ownerParent.isEmpty() && parents.contains(object.ownerParent))) { hits.insert(key); }
		}
	} else {
		auto lines = request.lines; std::sort(lines.begin(),lines.end());
		const auto matches = [&](const Object& object) {
			if (object.first < 1 || object.last < object.first) { return false; }
			const auto line = std::lower_bound(lines.cbegin(),lines.cend(),object.first);
			return line != lines.cend() && *line <= object.last;
		};
		for (const auto& key : order) {
			detail::placementCancellationCheckpoint();
			const auto& object = objects[key];
			if (!object.world && !children.contains(key) && matches(object) && selectable(key)) { hits.insert(key); }
		}
		for (const auto& entity : document->entities) {
			detail::placementCancellationCheckpoint();
			const auto key = levelMapSelectionRefId({LevelMapSelectionKind::Entity,entity.id});
			if (objects[key].world || !children.contains(key) || !matches(objects[key])) { continue; }
			const auto owned = children.value(key);
			// A property/bracket line on a brush entity selects all its selectable
			// children; a line inside a child selects that child alone.
			if (std::none_of(owned.cbegin(),owned.cend(),[&](const auto& child) { return hits.contains(child); })) {
				for (const auto& child : owned) { if (selectable(child)) { hits.insert(child); } }
			}
		}
	}
	QVector<LevelMapSelectionRef> selection; QSet<QString> visited;
	qint64 completed = 0;
	for (const auto& key : order) {
		detail::placementCheckpoint(LevelPlacementPhase::SelectingObjects,completed++,order.size());
		if (!hits.contains(key) || !selectable(key)) { continue; }
		const auto& object = objects[key];
		if (object.group.isEmpty() && children.contains(key)) { continue; }
		const auto unit = object.group.isEmpty() ? key : QStringLiteral("group:") + object.group;
		if (visited.contains(unit)) { continue; } visited.insert(unit);
		if (object.group.isEmpty()) { selection.append(object.ref); }
		else { selection += context.groupSelections.value(object.group); }
	}
	LevelMapDocument candidate = *document;
	if (!setLevelMapSelection(&candidate,selection,error)) { return false; }
	if (request.undoSelection) {
		detail::recordLevelMapSelectionHistory(&candidate,document->selection,
			{document->openSceneGroups,document->activeSceneNode,document->sceneCreationBeforeOpen},
			sourceLines ? Text::tr("Select by Source Line Number") : Text::tr("Select Siblings"));
	}
	detail::placementCheckpoint(LevelPlacementPhase::Finalizing,1,1);
	*document = std::move(candidate); if (report) { report->selection = std::move(selection); } return true;
}
} // namespace vibestudio
