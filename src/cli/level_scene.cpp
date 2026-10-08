#include "cli/level_scene.h"
#include "core/level_linked_groups.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>

namespace vibestudio::cli {
LevelSceneCliResult runLevelScene(const QStringList& arguments) {
	const auto failure = [](int code, const QString& message) { return LevelSceneCliResult{code, message, {}, {}}; };
	const QSet<QString> globals{"--settings-file", "--locale", "--catalog-root"};
	const QSet<QString> switches{"--cli", "--json", "--quiet", "--verbose", "--overwrite", "--dry-run"};
	const QSet<QString> options{"--map-name", "--output", "--id", "--name", "--parent", "--objects", "--kind", "--visible", "--locked", "--offset"};
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
		if (equal >= 0) {
			value = arg.mid(equal + 1);
		} else if (i + 1 < arguments.size() && !arguments[i + 1].startsWith('-')) {
			value = arguments[++i];
		}
		if (value.trimmed().isEmpty()) {
			return failure(2, QCoreApplication::translate("LevelSceneCli", "Missing value for %1.").arg(key));
		}
		values.insert(key, value);
	}
	if (positional.size() != 4) {
		return failure(2,
					   QCoreApplication::translate(
						   "LevelSceneCli",
						   "Expected editor scene list|create|rename|move|assign|visibility|lock|remove|reset|link|update-links|unlink <map> with "
						   "operation options."));
	}
	const auto action = positional[2];
	const auto map = positional[3];
	QSet<QString> allowed{"--map-name"};
	QSet<QString> required;
	const bool writing = action != QStringLiteral("list");
	if (writing) {
		allowed << "--output" << "--overwrite" << "--dry-run";
		required << "--output";
	}
	if (action == QStringLiteral("create")) {
		allowed << "--kind" << "--name" << "--parent";
		required << "--kind" << "--name";
	} else if (action == QStringLiteral("rename")) {
		allowed << "--id" << "--name";
		required << "--id" << "--name";
	} else if (action == QStringLiteral("move")) {
		allowed << "--id" << "--parent";
		required << "--id" << "--parent";
	} else if (action == QStringLiteral("assign")) {
		allowed << "--id" << "--objects";
		required << "--id" << "--objects";
	} else if (action == QStringLiteral("visibility")) {
		allowed << "--id" << "--visible";
		required << "--id" << "--visible";
	} else if (action == QStringLiteral("lock")) {
		allowed << "--id" << "--locked";
		required << "--id" << "--locked";
	} else if (action == QStringLiteral("remove") || action == QStringLiteral("update-links") || action == QStringLiteral("unlink")) {
		allowed << "--id";
		required << "--id";
	} else if (action == QStringLiteral("link")) {
		allowed << "--id" << "--offset";
		required << "--id";
	} else if (action != QStringLiteral("list") && action != QStringLiteral("reset")) {
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
	LevelMapDocument document;
	QString error, created;
	if (!loadLevelMap({map, values.value(QStringLiteral("--map-name")), {}}, &document, &error)) {
		return failure(4, error);
	}
	const auto node = [](QString value) { return value == QStringLiteral("default") ? QString() : value; };
	const auto id = node(values.value(QStringLiteral("--id")));
	bool applied = true;
	int updated = -1;
	if (action == QStringLiteral("create")) {
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
	} else if (action == QStringLiteral("link")) {
		LevelMapVec3 offset = levelLinkedCopyOffset(document, id);
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
	} else if (action == QStringLiteral("update-links")) {
		applied = updateLinkedLevelGroups(&document, id, &updated, &error);
	} else if (action == QStringLiteral("unlink")) {
		applied = unlinkLevelGroup(&document, id, &error);
	}
	if (!applied) {
		return failure(4, error);
	}
	LevelSceneCliResult result;
	result.payload = {{QStringLiteral("operation"), action},
					  {QStringLiteral("map"), document.sourcePath},
					  {QStringLiteral("mapName"), document.mapName},
					  {QStringLiteral("scene"), levelSceneJson(document)}};
	if (!created.isEmpty()) {
		result.payload.insert(QStringLiteral("createdId"), created);
	}
	if (updated >= 0) {
		result.payload.insert(QStringLiteral("updated"), updated);
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
		}
		result.lines << line;
	}
	return result;
}
} // namespace vibestudio::cli
