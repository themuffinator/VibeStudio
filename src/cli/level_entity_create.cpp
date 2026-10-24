// map create-entity: TrenchBroom's Create <class> commands for automation.
// The class must be a point or brush class of the entity definitions
// (--definitions, or the map's game's built-in catalogue). A point class is
// placed at --origin exactly, or the way the Levels page places it at the
// pointer: --camera, --plane-point with --plane-normal and --ray-direction,
// or --ray-origin with --ray-direction and --view-direction. A brush class
// takes the --object brushes and patches. One undo step through
// core/level_entity_create.h, the Levels page's service; saves to a new path.
//
// Workflow reference: TB ActionManager::createEntityDefinitionActions and
// MapViewBase::createEntity at 4c756be1 (GPL-3.0-or-later). Original
// implementation; see docs/CREDITS.md.

#include "cli/level_entity_create.h"
#include "core/entity_builtin_catalogue.h"
#include "core/level_entity_create.h"
#include "core/level_scene.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>

#include <cmath>

namespace vibestudio::cli {
namespace {
struct Text {
	Q_DECLARE_TR_FUNCTIONS(LevelEntityCreateCli)
};

bool parseVector(const QString& value, LevelMapVec3* vector)
{
	static const QRegularExpression separators(QStringLiteral("[,\\s]+"));
	const QStringList parts = value.trimmed().split(separators, Qt::SkipEmptyParts);
	if (parts.size() != 3) {
		return false;
	}
	double xyz[3] = {0.0, 0.0, 0.0};
	for (int axis = 0; axis < 3; ++axis) {
		bool valid = false;
		xyz[axis] = parts.at(axis).toDouble(&valid);
		if (!valid || !std::isfinite(xyz[axis])) {
			return false;
		}
	}
	*vector = {xyz[0], xyz[1], xyz[2], true};
	return true;
}

QJsonArray vectorJson(const LevelMapVec3& vector)
{
	return QJsonArray {vector.x, vector.y, vector.z};
}
} // namespace

LevelEntityCreateCliResult runLevelEntityCreate(const QStringList& arguments)
{
	LevelEntityCreateCliResult result;
	result.payload.insert(QStringLiteral("mapWritten"), false);
	const auto fail = [&result](int code, const QString& message) {
		result.exitCode = code;
		result.error = message;
		return result;
	};
	const QSet<QString> flags {"--cli", "--json", "--quiet", "--verbose", "--overwrite", "--dry-run", "--orthographic"};
	const QSet<QString> options {"--class", "--definitions", "--origin", "--camera", "--plane-point", "--plane-normal", "--ray-direction", "--ray-origin",
		"--view-direction", "--grid", "--object", "--select", "--key", "--context", "--output", "--engine", "--map-name", "--settings-file", "--locale",
		"--catalog-root"};
	const QSet<QString> repeated {"--object", "--select", "--key"};
	QSet<QString> seen;
	QHash<QString, QStringList> values;
	QStringList positional;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const QString argument = arguments[i];
		if (!argument.startsWith('-')) {
			positional << argument;
			continue;
		}
		const auto equals = argument.indexOf('=');
		const QString name = equals < 0 ? argument : argument.left(equals);
		if (seen.contains(name) && !repeated.contains(name)) {
			return fail(2, Text::tr("Repeated option: %1. Only --object, --select and --key may be repeated.").arg(name));
		}
		seen.insert(name);
		if (flags.contains(name)) {
			if (equals >= 0) {
				return fail(2, Text::tr("Flag %1 does not take a value.").arg(name));
			}
			continue;
		}
		if (!options.contains(name)) {
			return fail(2, Text::tr("Unknown option for map create-entity: %1").arg(name));
		}
		QString value;
		if (equals >= 0) {
			value = argument.mid(equals + 1);
		} else if (i + 1 < arguments.size() && !arguments[i + 1].startsWith(QStringLiteral("--"))) {
			// A vector may start with a minus sign: --origin -64,0,32.
			value = arguments[++i];
		}
		if (value.trimmed().isEmpty()) {
			return fail(2, Text::tr("Missing value for %1.").arg(name));
		}
		values[name] << value;
	}
	if (positional.size() != 3) {
		return fail(2, Text::tr("map create-entity needs one Quake-family .map, --class <classname> and --output <save-as path>."));
	}
	const QString path = positional[2];
	const QString className = values.value(QStringLiteral("--class")).value(0).trimmed();
	const QString output = values.value(QStringLiteral("--output")).value(0);
	if (className.isEmpty() || output.isEmpty()) {
		return fail(2, Text::tr("map create-entity requires --class <classname> and --output <save-as path>."));
	}
	const bool origin = seen.contains(QStringLiteral("--origin")), camera = seen.contains(QStringLiteral("--camera")),
		plane = seen.contains(QStringLiteral("--plane-point")), ray = seen.contains(QStringLiteral("--ray-origin"));
	const int targets = int(origin) + int(camera) + int(plane) + int(ray);
	const bool objects = seen.contains(QStringLiteral("--object")) || seen.contains(QStringLiteral("--select"));
	QVector<LevelMapProperty> properties;
	for (const QString& pair : values.value(QStringLiteral("--key"))) {
		const qsizetype split = pair.indexOf(QLatin1Char('='));
		if (split <= 0) {
			return fail(2, Text::tr("Each --key must be key=value: %1").arg(pair));
		}
		properties.push_back({pair.left(split).trimmed(), pair.mid(split + 1), 0});
	}

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap({QFileInfo(path).absoluteFilePath(), values.value(QStringLiteral("--map-name")).value(0), values.value(QStringLiteral("--engine")).value(0)},
			&document, &error)) {
		return fail(QFileInfo::exists(path) ? 1 : 3, error);
	}
	// The project's definitions when named, else the map's game's built-in ones.
	const QString definitionsPath = values.value(QStringLiteral("--definitions")).value(0);
	const EntityDefinitionCatalogue catalogue = definitionsPath.isEmpty() ? builtinEntityDefinitions(builtinEntityGameForMap(document))
																		   : loadEntityDefinitions({definitionsPath});
	if (!definitionsPath.isEmpty() && catalogue.isEmpty()) {
		return fail(4, Text::tr("No entity definitions could be loaded from %1. %2").arg(definitionsPath, catalogue.error));
	}
	LevelEntityCreateRequest request;
	if (!levelEntityCreateRequestForClass(catalogue, className, &request, &error)) {
		return fail(4, error);
	}
	request.properties = properties;
	if (request.kind == EntityClassKind::Point) {
		if (objects) {
			return fail(2, Text::tr("%1 is a point class; --object applies to brush classes, which take those brushes.").arg(request.className));
		}
		if (targets != 1) {
			return fail(2, Text::tr("Place point class %1 with one of: --origin x,y,z; --camera x,y,z; --plane-point with --plane-normal and --ray-direction; "
									"or --ray-origin with --ray-direction and --view-direction.")
							   .arg(request.className));
		}
		LevelPastePlacement& placement = request.placement;
		const auto vector = [&values](const char* option, LevelMapVec3* target) { return parseVector(values.value(QLatin1String(option)).value(0), target); };
		if ((origin && !vector("--origin", &placement.offset)) || (camera && !vector("--camera", &placement.point))
			|| (plane && (!vector("--plane-point", &placement.point) || !vector("--plane-normal", &placement.normal) || !vector("--ray-direction", &placement.direction)))
			|| (ray && (!vector("--ray-origin", &placement.rayOrigin) || !vector("--ray-direction", &placement.direction)
					|| !vector("--view-direction", &placement.viewDirection)))
			|| (!plane && seen.contains(QStringLiteral("--plane-normal"))) || (!plane && !ray && seen.contains(QStringLiteral("--ray-direction")))
			|| (!ray && (seen.contains(QStringLiteral("--view-direction")) || seen.contains(QStringLiteral("--orthographic"))))
			|| (origin && seen.contains(QStringLiteral("--grid")))) {
			return fail(2, Text::tr("Each placement vector is x,y,z. --plane-point needs --plane-normal and --ray-direction; --ray-origin needs --ray-direction and "
									"--view-direction (and alone takes --orthographic); --grid applies to the targeted placements, not --origin."));
		}
		placement.mode = origin ? LevelPasteMode::Offset : camera ? LevelPasteMode::Camera : plane ? LevelPasteMode::Plane : LevelPasteMode::ViewRay;
		placement.rayForwardOnly = !seen.contains(QStringLiteral("--orthographic"));
		if (seen.contains(QStringLiteral("--grid"))) {
			bool valid = false;
			placement.grid = values.value(QStringLiteral("--grid")).value(0).toDouble(&valid);
			if (!valid || !std::isfinite(placement.grid) || placement.grid < 0 || placement.grid > 32768) {
				return fail(2, Text::tr("--grid must be between 0 and 32768."));
			}
		}
	} else {
		if (targets > 0 || seen.contains(QStringLiteral("--grid")) || seen.contains(QStringLiteral("--plane-normal"))
			|| seen.contains(QStringLiteral("--ray-direction")) || seen.contains(QStringLiteral("--view-direction")) || seen.contains(QStringLiteral("--orthographic"))) {
			return fail(2, Text::tr("%1 is a brush class: name the brushes and patches it takes with --object; placement options apply to point classes.")
							   .arg(request.className));
		}
		if (!objects) {
			return fail(2, Text::tr("Brush class %1 needs --object kind:id for the brushes and patches it takes.").arg(request.className));
		}
	}
	// The layer or group new objects go to: "default", a layer, or a group
	// to open, as map paste takes it.
	if (seen.contains(QStringLiteral("--context"))) {
		const QString context = values.value(QStringLiteral("--context")).first();
		const LevelSceneNode* node = levelSceneNode(document.scene, context);
		if (context == QLatin1String("default")) {
			document.activeSceneNode.clear();
		} else if (!node) {
			return fail(4, Text::tr("The layer or group %1 does not exist.").arg(context));
		} else if (node->kind == LevelSceneNodeKind::Group) {
			if (!openLevelSceneGroup(&document, context, &error)) {
				return fail(4, error);
			}
		} else {
			document.activeSceneNode = context;
		}
	}
	if (objects) {
		QVector<LevelMapSelectionRef> selection;
		for (const QString& value : values.value(QStringLiteral("--object")) + values.value(QStringLiteral("--select"))) {
			for (const QString& selector : value.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
				const QStringList parts = selector.trimmed().split(QLatin1Char(':'));
				bool valid = false;
				const int id = parts.value(1).toInt(&valid);
				const LevelMapSelectionKind kind = parts.size() == 2 ? levelMapSelectionKindFromId(parts.first().trimmed().toLower()) : LevelMapSelectionKind::None;
				if (kind == LevelMapSelectionKind::None || !valid || id < 0) {
					return fail(2, Text::tr("Unknown map object selector: %1").arg(selector.trimmed()));
				}
				selection.removeAll(LevelMapSelectionRef {kind, id});
				selection << LevelMapSelectionRef {kind, id};
			}
		}
		if (!setLevelMapSelection(&document, selection, &error)) {
			return fail(2, Text::tr("Unable to select map objects: %1").arg(error));
		}
	}
	LevelEntityCreateReport report;
	if (!createLevelMapEntityOfClass(&document, request, &report, &error)) {
		return fail(4, error);
	}
	const LevelMapSaveReport saved = saveLevelMapAs(document, output, seen.contains(QStringLiteral("--dry-run")), seen.contains(QStringLiteral("--overwrite")));
	if (!saved.succeeded()) {
		return fail(4, saved.errors.join(QLatin1Char('\n')));
	}
	const bool point = report.kind == EntityClassKind::Point;
	result.payload = QJsonObject {{QStringLiteral("className"), report.className}, {QStringLiteral("kind"), entityClassKindId(report.kind)},
		{QStringLiteral("entity"), QStringLiteral("entity:%1").arg(report.entityId)}, {QStringLiteral("entityId"), report.entityId},
		{QStringLiteral("commandId"), levelEntityCreateCommandId(report.className)},
		{QStringLiteral("definitions"), definitionsPath.isEmpty() ? QStringLiteral("built-in") : QFileInfo(definitionsPath).absoluteFilePath()},
		{QStringLiteral("output"), saved.outputPath}, {QStringLiteral("written"), saved.written}, {QStringLiteral("mapWritten"), saved.written},
		{QStringLiteral("dryRun"), saved.dryRun}, {QStringLiteral("warnings"), QJsonArray::fromStringList(saved.warnings)},
		{QStringLiteral("context"), document.activeSceneNode}};
	if (point) {
		result.payload.insert(QStringLiteral("origin"), vectorJson(report.origin));
		result.payload.insert(QStringLiteral("placement"), origin ? QStringLiteral("origin") : camera ? QStringLiteral("camera") : plane ? QStringLiteral("plane")
																												: QStringLiteral("view-ray"));
		result.lines << Text::tr("Created %1 as entity:%2 at %3 %4 %5.")
							.arg(report.className)
							.arg(report.entityId)
							.arg(report.origin.x, 0, 'g', 8)
							.arg(report.origin.y, 0, 'g', 8)
							.arg(report.origin.z, 0, 'g', 8);
	} else {
		result.payload.insert(QStringLiteral("brushes"), report.brushes);
		result.payload.insert(QStringLiteral("patches"), report.patches);
		const int held = report.brushes + report.patches;
		result.lines << Text::tr("Made %n brush(es) and patch(es) into %1 as entity:%2.", nullptr, held).arg(report.className).arg(report.entityId);
	}
	result.lines << levelMapSaveReportText(saved);
	return result;
}
} // namespace vibestudio::cli
