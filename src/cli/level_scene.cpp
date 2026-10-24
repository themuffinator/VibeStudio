#include "cli/level_scene.h"
#include "core/level_linked_groups.h"
#include "core/level_linked_extraction.h"
#include "core/level_scene.h"
#include "core/level_scene_layers.h"
#include "core/level_scene_locks.h"
#include "core/level_scene_trenchbroom.h"
#include "core/level_protected_properties.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <cmath>
#include <algorithm>

namespace vibestudio::cli {
LevelSceneCliResult runLevelScene(const QStringList& arguments) {
	const auto failure = [](int code, const QString& message) { return LevelSceneCliResult{code, message, {}, {}}; };
	const QSet<QString> globals{"--settings-file", "--locale", "--catalog-root"};
	const QSet<QString> switches{"--cli", "--json", "--quiet", "--verbose", "--overwrite", "--dry-run", "--expand-layers"};
	const QSet<QString> options{"--map-name", "--output", "--id", "--name", "--parent", "--objects", "--kind", "--visible", "--locked", "--offset", "--context", "--selection", "--mode", "--keys", "--protected", "--key", "--value", "--scene-format"};
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const auto arg = arguments[i];
		if (!arg.startsWith('-')) {
			positional << arg;
			continue;
		}
		const auto equal = arg.indexOf('=');
		const auto key = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(key)) {
			return failure(2, QCoreApplication::translate("LevelSceneCli", "Repeated option: %1").arg(key));
		}
		seen.insert(key);
		if (switches.contains(key) && equal < 0) {
			continue;
		}
		if (!options.contains(key) && !globals.contains(key)) {
			return failure(2, QCoreApplication::translate("LevelSceneCli", "Unexpected option: %1").arg(arg));
		}
		QString value;
		bool provided = equal >= 0;
		if (equal >= 0) {
			value = arg.mid(equal + 1);
		} else if (i + 1 < arguments.size() && (!arguments[i + 1].startsWith('-') || (key == "--offset" && !arguments[i+1].startsWith("--")))) {
			value = arguments[++i];
			provided = true;
		}
		if (!provided || (key != QStringLiteral("--value") && value.trimmed().isEmpty())) {
			return failure(2, QCoreApplication::translate("LevelSceneCli", "Missing value for %1.").arg(key));
		}
		values.insert(key, value);
	}
	if (positional.size() != 4) {
		return failure(2,
					   QCoreApplication::translate(
						   "LevelSceneCli",
						   "Expected editor scene list|select|translate|create|rename|move|assign|move-to-layer|visibility|lock|remove|reset|save|create-linked-duplicate|select-linked-groups|update-linked-groups|separate-linked-groups|extract-linked-groups|protect-properties|clear-protected-properties|set-protected-property <map> with "
						   "operation options."));
	}
	// Linked group operations take TrenchBroom's names; the earlier short
	// names still work.
	static const QHash<QString, QString> aliases {{QStringLiteral("link"), QStringLiteral("create-linked-duplicate")},
		{QStringLiteral("update-links"), QStringLiteral("update-linked-groups")}, {QStringLiteral("unlink"), QStringLiteral("separate-linked-groups")},
		{QStringLiteral("extract-linked"), QStringLiteral("extract-linked-groups")}};
	const auto action = aliases.value(positional[2], positional[2]);
	const auto map = positional[3];
	QSet<QString> allowed{"--map-name"};
	QSet<QString> required;
	const bool writing = action != QStringLiteral("list") && action != QStringLiteral("select") && action != QStringLiteral("select-linked-groups");
	if (writing) {
		allowed << "--output" << "--overwrite" << "--dry-run" << "--scene-format";
		required << "--output";
	}
	const bool protection = action == QStringLiteral("protect-properties") || action == QStringLiteral("clear-protected-properties")
		|| action == QStringLiteral("set-protected-property");
	if (action == QStringLiteral("extract-linked-groups")) {
		allowed << "--context" << "--objects"; required << "--context" << "--objects";
	} else if (protection) {
		allowed << "--objects" << "--context"; required << "--objects";
		if (action == QStringLiteral("protect-properties")) {
			allowed << "--keys" << "--protected"; required << "--keys" << "--protected";
		} else if (action == QStringLiteral("set-protected-property")) {
			allowed << "--key" << "--value"; required << "--key" << "--value";
		}
	} else if (action == QStringLiteral("select") || action == QStringLiteral("translate")) {
		allowed << "--context" << "--objects"; required << "--objects";
		if (action == QStringLiteral("select")) { allowed << "--selection" << "--mode" << "--expand-layers"; }
		else { allowed << "--offset"; required << "--offset"; }
	} else if (action == QStringLiteral("create")) {
		allowed << "--kind" << "--name" << "--parent";
		required << "--kind" << "--name";
	} else if (action == QStringLiteral("rename")) {
		allowed << "--id" << "--name";
		required << "--id" << "--name";
	} else if (action == QStringLiteral("move")) {
		allowed << "--id" << "--parent";
		required << "--id" << "--parent";
	} else if (action == QStringLiteral("assign") || action == QStringLiteral("move-to-layer")) {
		allowed << "--id" << "--objects";
		required << "--id" << "--objects";
	} else if (action == QStringLiteral("visibility")) {
		allowed << "--id" << "--visible";
		required << "--id" << "--visible";
	} else if (action == QStringLiteral("lock")) {
		allowed << "--id" << "--locked";
		required << "--id" << "--locked";
	} else if (action == QStringLiteral("remove") || action == QStringLiteral("update-linked-groups") || action == QStringLiteral("separate-linked-groups")
		|| action == QStringLiteral("select-linked-groups")) {
		allowed << "--id";
		required << "--id";
	} else if (action == QStringLiteral("create-linked-duplicate")) {
		allowed << "--id" << "--offset";
		required << "--id";
	} else if (action != QStringLiteral("list") && action != QStringLiteral("reset") && action != QStringLiteral("save")) {
		return failure(2, QCoreApplication::translate("LevelSceneCli", "Unknown scene operation: %1.").arg(action));
	}
	for (const auto& key : seen) {
		if (!globals.contains(key) && key != QStringLiteral("--cli") && key != QStringLiteral("--json") &&
			key != QStringLiteral("--quiet") && key != QStringLiteral("--verbose") && !allowed.contains(key)) {
			return failure(2, QCoreApplication::translate("LevelSceneCli", "Option %1 does not apply to %2.").arg(key, action));
		}
	}
	for (const auto& key : required) {
		if (!values.contains(key)) {
			return failure(2, QCoreApplication::translate("LevelSceneCli", "Missing required option %1.").arg(key));
		}
	}
	const bool wad = QFileInfo(map).suffix().compare(QStringLiteral("wad"), Qt::CaseInsensitive) == 0;
	if (wad != values.contains(QStringLiteral("--map-name"))) {
		return failure(2, QCoreApplication::translate("LevelSceneCli", "WAD files require --map-name; text maps do not accept it."));
	}
	LevelSceneStorage storage = LevelSceneStorage::VibeStudio;
	if (values.contains(QStringLiteral("--scene-format"))) {
		if (!levelSceneStorageFromId(values.value(QStringLiteral("--scene-format")), &storage)) {
			return failure(2, QCoreApplication::translate("LevelSceneCli", "Scene format must be vibestudio or trenchbroom."));
		}
		if (wad && storage == LevelSceneStorage::TrenchBroom) {
			return failure(2, QCoreApplication::translate("LevelSceneCli", "TrenchBroom groups need a Quake-family .map file."));
		}
	}
	LevelMapDocument document;
	QString error, created;
	if (!loadLevelMap({map, values.value(QStringLiteral("--map-name")), {}}, &document, &error)) {
		return failure(4, error);
	}
	// Without --scene-format a map keeps the storage it opened with:
	// TrenchBroom's when it had TrenchBroom layers or groups.
	if (values.contains(QStringLiteral("--scene-format"))) {
		document.sceneStorage = storage;
	}
	const auto node = [](QString value) { return value == QStringLiteral("default") ? QString() : value; };
	const auto id = node(values.value(QStringLiteral("--id")));
	bool applied = true;
	int updated = -1;
	int separated = -1;
	LevelLinkedExtractionReport extraction;
	LevelSceneLayerSelectionReport expansion;
	LevelSceneLayerMoveReport layerMove;
	const bool expand = seen.contains(QStringLiteral("--expand-layers"));
	if (action == QStringLiteral("extract-linked-groups")) {
		if (!openLevelSceneGroup(&document, values.value(QStringLiteral("--context")), &error)) { return failure(4, error); }
		QVector<LevelMapSelectionRef> selection;
		for (const auto& token : values.value(QStringLiteral("--objects")).split(',')) {
			const auto object = token.trimmed();
			bool valid = false;
			const int objectId = object.section(':', 1).toInt(&valid);
			const LevelMapSelectionRef ref{levelMapSelectionKindFromId(object.section(':', 0, 0)), objectId};
			if (!valid || object != levelMapSelectionRefId(ref) || !levelMapObjectExists(document, ref)) {
				return failure(2, QCoreApplication::translate("LevelSceneCli", "Use existing entity:N, brush:N or patch:N selectors for extraction."));
			}
			selection << ref;
		}
		setLevelMapSelection(&document, selection);
		applied = extractLevelLinkedGroups(&document, &extraction, &error);
	} else if (protection) {
		const auto context = node(values.value(QStringLiteral("--context")));
		if (!context.isEmpty() && !openLevelSceneGroup(&document, context, &error)) { return failure(4, error); }
		QVector<int> entities;
		QVector<LevelMapSelectionRef> selection;
		for (const auto& token : values.value(QStringLiteral("--objects")).split(',')) {
			const auto object = token.trimmed();
			bool valid = false;
			const int entityId = object.section(':', 1).toInt(&valid);
			const LevelMapSelectionRef ref{LevelMapSelectionKind::Entity, entityId};
			if (!valid || object != levelMapSelectionRefId(ref) || !levelMapObjectExists(document, ref)) {
				return failure(2, QCoreApplication::translate("LevelSceneCli", "Use existing entity:N selectors for property protection."));
			}
			entities << entityId; selection << ref;
		}
		setLevelMapSelection(&document, selection);
		if (action == QStringLiteral("protect-properties")) {
			const auto protect = values.value(QStringLiteral("--protected"));
			if (protect != QStringLiteral("true") && protect != QStringLiteral("false")) {
				return failure(2, QCoreApplication::translate("LevelSceneCli", "Property protection must be true or false."));
			}
			applied = setLevelEntityPropertyProtection(&document, entities, values.value(QStringLiteral("--keys")).split(','), protect == QStringLiteral("true"), &error);
		} else if (action == QStringLiteral("clear-protected-properties")) {
			applied = clearLevelEntityPropertyProtection(&document, entities, &error);
		} else {
			applied = setLevelProtectedEntityProperty(&document, entities, values.value(QStringLiteral("--key")), {values.value(QStringLiteral("--value"))}, &error);
		}
	} else if (action == QStringLiteral("select") || action == QStringLiteral("translate")) {
		const auto contextId = node(values.value(QStringLiteral("--context")));
		if (!contextId.isEmpty() && !openLevelSceneGroup(&document,contextId,&error)) { return failure(4,error); }
		const auto available = levelSceneObjects(document);
		const QSet<QString> objects(available.cbegin(),available.cend());
		const auto parse = [&](const QString& text, QVector<LevelMapSelectionRef>* refs) {
			if (text.isEmpty()) { return true; }
			for (const auto& token : text.split(',')) {
				const auto object = levelSceneCanonicalObject(document,token.trimmed());
				if (!objects.contains(object)) { return false; }
				const auto colon = object.indexOf(':'); refs->append({levelMapSelectionKindFromId(object.left(colon)),object.mid(colon+1).toInt()});
			}
			return true;
		};
		QVector<LevelMapSelectionRef> selected,hits;
		if (!parse(values.value(QStringLiteral("--selection")),&selected) || !parse(values.value(QStringLiteral("--objects")),&hits)) {
			return failure(2,QCoreApplication::translate("LevelSceneCli","Use comma-separated selectors of existing objects."));
		}
		const auto mode = values.value(QStringLiteral("--mode"),QStringLiteral("replace"));
		if (mode != "replace" && mode != "add" && mode != "toggle") { return failure(2,QCoreApplication::translate("LevelSceneCli","Selection mode must be replace, add or toggle.")); }
		const auto context = levelSceneSelectionContext(document);
		if (action == "translate" && std::any_of(hits.cbegin(),hits.cend(),[&](const auto& ref) { return !context.allowedObjects.contains(levelMapSelectionRefId(ref)); })) {
			return failure(4,QCoreApplication::translate("LevelSceneCli","Every object to translate must be inside the chosen group context."));
		}
		setLevelMapSelection(&document,pickLevelSceneObjects(context,selected,hits,mode == "toggle" ? LevelScenePick::Toggle : mode == "add" ? LevelScenePick::Add : LevelScenePick::Replace),&error);
		// NRC's Expand Selection to Layers, after the pick (core/level_scene_layers.h).
		if (expand && !expandLevelMapSelectionToLayers(&document,{},&expansion,&error)) { return failure(4,error); }
		if (action == "translate") {
			const auto parts = values.value(QStringLiteral("--offset")).split(','); bool valid = parts.size() == 3; double xyz[3]{};
			for (int axis = 0; valid && axis < 3; ++axis) { xyz[axis] = parts[axis].toDouble(&valid); valid = valid && std::isfinite(xyz[axis]) && std::abs(xyz[axis]) <= 32768; }
			if (!valid) { return failure(2,QCoreApplication::translate("LevelSceneCli","The offset needs three finite coordinates within ±32768: x,y,z.")); }
			applied = moveLevelMapSelection(&document,xyz[0],xyz[1],xyz[2],{true,false},&error);
		}
	} else if (action == QStringLiteral("create")) {
		const auto kind = values.value(QStringLiteral("--kind"));
		if (kind != QStringLiteral("layer") && kind != QStringLiteral("group")) {
			return failure(2, QCoreApplication::translate("LevelSceneCli", "Scene kind must be layer or group."));
		}
		applied =
			createLevelSceneNode(&document, kind == QStringLiteral("layer") ? LevelSceneNodeKind::Layer : LevelSceneNodeKind::Group,
								 values.value(QStringLiteral("--name")), node(values.value(QStringLiteral("--parent"))), &created, &error);
	} else if (action == QStringLiteral("rename")) {
		applied = renameLevelSceneNode(&document, id, values.value(QStringLiteral("--name")), &error);
	} else if (action == QStringLiteral("move")) {
		applied = reparentLevelSceneNode(&document, id, node(values.value(QStringLiteral("--parent"))), &error);
	} else if (action == QStringLiteral("assign")) {
		auto objects = values.value(QStringLiteral("--objects")).split(',');
		for (auto& object : objects) {
			object = object.trimmed();
		}
		applied = assignLevelSceneObjects(&document, id, objects, &error);
	} else if (action == QStringLiteral("move-to-layer")) {
		const auto available = levelSceneObjects(document);
		const QSet<QString> objects(available.cbegin(), available.cend());
		QVector<LevelMapSelectionRef> selection;
		for (const auto& token : values.value(QStringLiteral("--objects")).split(',')) {
			const auto object = levelSceneCanonicalObject(document, token.trimmed());
			if (!objects.contains(object)) {
				return failure(2, QCoreApplication::translate("LevelSceneCli", "Use comma-separated selectors of existing objects; worldspawn has no layer."));
			}
			const auto colon = object.indexOf(':');
			selection.append({levelMapSelectionKindFromId(object.left(colon)), object.mid(colon + 1).toInt()});
		}
		applied = setLevelMapSelection(&document, selection, &error) && moveLevelMapSelectionToLayer(&document, id, &layerMove, &error);
	} else if (action == QStringLiteral("visibility")) {
		const auto visible = values.value(QStringLiteral("--visible"));
		if (visible != QStringLiteral("true") && visible != QStringLiteral("false")) {
			return failure(2, QCoreApplication::translate("LevelSceneCli", "Visibility must be true or false."));
		}
		applied = setLevelSceneVisible(&document, id, visible == QStringLiteral("true"), &error);
	} else if (action == QStringLiteral("lock")) {
		const auto locked = values.value(QStringLiteral("--locked"));
		if (locked != QStringLiteral("true") && locked != QStringLiteral("false")) {
			return failure(2, QCoreApplication::translate("LevelSceneCli", "Lock state must be true or false."));
		}
		applied = setLevelSceneLocked(&document, id, locked == QStringLiteral("true"), &error);
	} else if (action == QStringLiteral("remove")) {
		applied = removeLevelSceneNode(&document, id, &error);
	} else if (action == QStringLiteral("reset")) {
		applied = resetLevelScene(&document, &error);
	} else if (action == QStringLiteral("create-linked-duplicate")) {
		LevelMapVec3 offset = levelLinkedDuplicateOffset(document, id);
		if (values.contains(QStringLiteral("--offset"))) {
			const auto parts = values.value(QStringLiteral("--offset")).split(QLatin1Char(','));
			bool valid = parts.size() == 3;
			double xyz[3] = {0.0, 0.0, 0.0};
			for (int axis = 0; valid && axis < 3; ++axis) {
				xyz[axis] = parts.at(axis).trimmed().toDouble(&valid);
			}
			if (!valid) {
				return failure(2, QCoreApplication::translate("LevelSceneCli", "The offset must be three numbers: x,y,z."));
			}
			offset = {xyz[0], xyz[1], xyz[2], true};
		}
		applied = createLinkedLevelGroup(&document, id, offset, &created, &error);
	} else if (action == QStringLiteral("update-linked-groups")) {
		applied = updateLinkedLevelGroups(&document, id, &updated, &error);
	} else if (action == QStringLiteral("separate-linked-groups")) {
		QStringList ids;
		for (const auto& token : values.value(QStringLiteral("--id")).split(QLatin1Char(','))) {
			ids << token.trimmed();
		}
		applied = separateLinkedLevelGroups(&document, ids, &separated, &error);
	} else if (action == QStringLiteral("select-linked-groups")) {
		if (levelLinkedGroupNodes(document.scene, id).isEmpty()) {
			return failure(4, QCoreApplication::translate("LevelSceneCli", "The group is not linked."));
		}
		setLevelMapSelection(&document, levelLinkedGroupsSelection(document, {id}));
	}
	if (!applied) {
		return failure(4, error);
	}
	LevelSceneCliResult result;
	result.payload = {{QStringLiteral("operation"), action},
					  {QStringLiteral("map"), document.sourcePath},
					  {QStringLiteral("mapName"), document.mapName},
					  {QStringLiteral("scene"), levelSceneJson(document)}};
	if (action == "select" || action == "translate" || action == "select-linked-groups") {
		QStringList selection; for (const auto& ref : document.selection) { selection << levelMapSelectionRefId(ref); }
		result.payload.insert(QStringLiteral("selection"),QJsonArray::fromStringList(selection));
		result.payload.insert(QStringLiteral("openGroups"),QJsonArray::fromStringList(document.openSceneGroups));
		result.lines << QCoreApplication::translate("LevelSceneCli","Selection: %1").arg(selection.join(','));
		if (expand) {
			QStringList layers;
			for (const auto& layer : std::as_const(expansion.layers)) { layers << (layer.isEmpty() ? QStringLiteral("default") : layer); }
			result.payload.insert(QStringLiteral("layers"),QJsonArray::fromStringList(layers));
			result.payload.insert(QStringLiteral("skippedGroups"),expansion.skippedGroups);
			result.lines << QCoreApplication::translate("LevelSceneCli","Expanded to layers: %1").arg(layers.join(','));
		}
	}
	if (action == QStringLiteral("move-to-layer")) {
		result.payload.insert(QStringLiteral("moved"), QJsonObject{
			{QStringLiteral("layer"), id.isEmpty() ? QStringLiteral("default") : id},
			{QStringLiteral("objects"), QJsonArray::fromStringList(layerMove.objects)},
			{QStringLiteral("released"), QJsonArray::fromStringList(layerMove.released)},
			{QStringLiteral("groups"), QJsonArray::fromStringList(layerMove.groups)},
			{QStringLiteral("alreadyThere"), layerMove.alreadyThere},
			{QStringLiteral("worldspawn"), layerMove.worldspawn},
			{QStringLiteral("changed"), layerMove.changed}});
		const int movedObjects = static_cast<int>(layerMove.objects.size() + layerMove.released.size());
		const int movedGroups = static_cast<int>(layerMove.groups.size());
		if (!layerMove.changed) {
			result.lines << QCoreApplication::translate("LevelSceneCli", "The objects are already in that layer; nothing changed.");
		}
		if (movedObjects > 0) {
			result.lines << QCoreApplication::translate("LevelSceneCli", "Moved %n object(s) to the layer.", nullptr, movedObjects);
		}
		if (movedGroups > 0) {
			result.lines << QCoreApplication::translate("LevelSceneCli", "Moved %n closed group(s) to the layer whole.", nullptr, movedGroups);
		}
	}
	if (!created.isEmpty()) {
		result.payload.insert(QStringLiteral("createdId"), created);
	}
	if (updated >= 0) {
		result.payload.insert(QStringLiteral("updated"), updated);
		result.lines << QCoreApplication::translate("LevelSceneCli", "Updated %n linked group(s) to match.", nullptr, updated);
	}
	if (separated >= 0) {
		result.payload.insert(QStringLiteral("separated"), separated);
		result.lines << QCoreApplication::translate("LevelSceneCli", "Separated %n linked group(s).", nullptr, separated);
	}
	if (action == QStringLiteral("select-linked-groups")) {
		const auto members = levelLinkedGroupNodes(document.scene, id);
		result.payload.insert(QStringLiteral("linkedGroups"), QJsonArray::fromStringList(members));
		result.lines << QCoreApplication::translate("LevelSceneCli", "Selected %n linked group(s).", nullptr, static_cast<int>(members.size()));
	}
	if (!document.linkedGroupNotices.isEmpty()) {
		result.payload.insert(QStringLiteral("linkedGroupNotices"), QJsonArray::fromStringList(document.linkedGroupNotices));
		result.lines << document.linkedGroupNotices;
	}
	if (action == QStringLiteral("extract-linked-groups")) {
		result.payload.insert(QStringLiteral("extraction"), levelLinkedExtractionReportJson(extraction));
		result.lines << QCoreApplication::translate("LevelSceneCli", "Extracted contents across %n linked group(s).", nullptr, static_cast<int>(extraction.createdGroups.size()));
	}
	if (writing) {
		const auto report = saveLevelMapAs(document, values.value(QStringLiteral("--output")), seen.contains(QStringLiteral("--dry-run")),
										   seen.contains(QStringLiteral("--overwrite")));
		if (!report.succeeded()) {
			return failure(4, report.errors.join(QLatin1Char('\n')));
		}
		result.payload.insert(QStringLiteral("output"), report.outputPath);
		result.payload.insert(QStringLiteral("written"), report.written);
		result.payload.insert(QStringLiteral("dryRun"), report.dryRun);
		result.payload.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(report.warnings));
		result.payload.insert(QStringLiteral("sceneStorage"), levelSceneStorageId(report.sceneStorage));
		result.lines << levelMapSaveReportText(report);
	}
	result.lines << QCoreApplication::translate("LevelSceneCli", "Scene nodes: %1").arg(document.scene.nodes.size());
	if (!document.scene.problem.isEmpty()) {
		result.lines << document.scene.problem;
	}
	const auto lockedNodes = levelSceneLockedNodes(document.scene);
	for (const auto& item : document.scene.nodes) {
		QString line = QStringLiteral("%1  %2  %3  %4")
						   .arg(item.id, item.name,
								item.visible ? QCoreApplication::translate("LevelSceneCli", "Visible")
											 : QCoreApplication::translate("LevelSceneCli", "Hidden"),
								lockedNodes.contains(item.id) ? QCoreApplication::translate("LevelSceneCli", "Locked")
																: QCoreApplication::translate("LevelSceneCli", "Editable"));
		if (!item.linkId.isEmpty()) {
			line += QStringLiteral("  ") + QCoreApplication::translate("LevelSceneCli", "Linked %1").arg(item.linkId);
			if (const auto frame = levelLinkFrameText(item.linkFrame); !frame.isEmpty()) {
				line += QStringLiteral("  ") + frame;
			}
			if (item.linkOutOfSync) {
				line += QStringLiteral("  ") + QCoreApplication::translate("LevelSceneCli", "Out of sync");
			}
		}
		result.lines << line;
	}
	for (auto it = document.scene.protectedProperties.cbegin(); it != document.scene.protectedProperties.cend(); ++it) {
		result.lines << QCoreApplication::translate("LevelSceneCli", "Protected %1: %2").arg(it.key(), it.value().join(QStringLiteral(", ")));
	}
	return result;
}
} // namespace vibestudio::cli
