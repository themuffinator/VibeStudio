#include "core/entity_definitions.h"
#include "core/level_map.h"
#include "core/operation_state.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVector>

#include <algorithm>
#include <cstdlib>
#include <iostream>

using namespace vibestudio;

namespace {

bool expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << message << "\n";
		return false;
	}
	return true;
}

bool writeTextFile(const QString& path, const QString& text)
{
	const QString parent = QFileInfo(path).absolutePath();
	if (!QDir().mkpath(parent)) {
		return false;
	}
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	const QByteArray bytes = text.toUtf8();
	const bool ok = file.write(bytes) == bytes.size();
	file.close();
	return ok;
}

int countCode(const EntityValidationReport& report, const char* code)
{
	const QString wanted = QString::fromLatin1(code);
	int count = 0;
	for (const EntityValidationIssue& issue : report.issues) {
		if (issue.code == wanted) {
			++count;
		}
	}
	return count;
}

bool hasWarningContaining(const QStringList& warnings, const char* fragment)
{
	const QString wanted = QString::fromLatin1(fragment);
	for (const QString& warning : warnings) {
		if (warning.contains(wanted, Qt::CaseInsensitive)) {
			return true;
		}
	}
	return false;
}

const EntitySpawnflagDefinition* flagNamed(const EntityClassDefinition& definition, const char* name)
{
	const QString wanted = QString::fromLatin1(name);
	for (const EntitySpawnflagDefinition& flag : definition.spawnflags) {
		if (flag.name.compare(wanted, Qt::CaseInsensitive) == 0) {
			return &flag;
		}
	}
	return nullptr;
}

QString radiantDefinitionText()
{
	return QStringLiteral(
		"/*QUAKED info_player_start (1 0 0) (-16 -16 -24) (16 16 32) INITIAL - SUSPENDED\n"
		"The player starts here.\n"
		"-------- KEYS --------\n"
		"\"targetname\" name this entity is called by\n"
		"angle : \"direction the player faces\"\n"
		"\"target\" what to fire when the level starts\n"
		"-------- SPAWNFLAGS --------\n"
		"INITIAL : the first spawn point\n"
		"NOTFREE : not in free-for-all\n"
		"*/\n"
		"\n"
		"/*QUAKED func_door (0 .5 .8) ? START_OPEN CRUSHER\n"
		"A sliding door.\n"
		"-------- KEYS --------\n"
		"\"speed\" : \"how fast the door moves\"\n"
		"\"targetname\" name for triggering\n"
		"CRUSHER_EXTRA\n"
		"*/\n"
		"\n"
		"/*QUAKED misc_model (0 1 0) (-16 -16 -16) (16 16 16)\n"
		"A static model.\n"
		"-------- KEYS --------\n"
		"\"model\" (required) the model file to place\n"
		"*/\n");
}

QString baseFgdText()
{
	return QStringLiteral(
		"// Shared base class.\n"
		"@BaseClass = Targetname\n"
		"[\n"
		"\ttargetname(target_source) : \"Name\" : \"\" : \"The name other entities refer to this by.\"\n"
		"\tspawnflags(flags) =\n"
		"\t[\n"
		"\t\t1 : \"Start disabled\" : 0\n"
		"\t\t8 : \"Silent\" : 1\n"
		"\t]\n"
		"]\n");
}

QString gameFgdText()
{
	return QStringLiteral(
		"@include \"base.fgd\"\n"
		"\n"
		"@PointClass base(Targetname) size(-16 -16 -16, 16 16 16) color(255 128 0) iconsprite(\"sprites/light.spr\") = light : \"A light source.\"\n"
		"[\n"
		"\tlight(integer) : \"Brightness\" : 300 : \"(required) How bright the light is.\"\n"
		"\tstyle(choices) : \"Appearance\" : 0 =\n"
		"\t[\n"
		"\t\t0 : \"Normal\"\n"
		"\t\t10 : \"Fluorescent flicker\"\n"
		"\t]\n"
		"\t_color(color255) : \"Colour\" : \"255 255 255\"\n"
		"]\n"
		"\n"
		"@SolidClass base(Targetname) = trigger_multiple : \"A brush that fires its target.\"\n"
		"[\n"
		"\ttarget(target_destination) : \"Target\" : : \"What to fire.\"\n"
		"\twait(real) : \"Delay before reset\" : 0.5\n"
		"\tspawnflags(flags) =\n"
		"\t[\n"
		"\t\t2 : \"Monsters\" : 0\n"
		"\t]\n"
		"]\n");
}

QString cycleFgdText()
{
	return QStringLiteral(
		"@BaseClass base(Beta) = Alpha []\n"
		"@BaseClass base(Alpha) = Beta []\n"
		"@PointClass base(Alpha) = cycle_probe : \"Probe.\" []\n");
}

QString selfIncludingFgdText()
{
	return QStringLiteral(
		"@include \"selfref.fgd\"\n"
		"@PointClass = self_probe : \"Probe.\" []\n");
}

QString duplicateDefText()
{
	return QStringLiteral(
		"/*QUAKED light (1 1 0) (-8 -8 -8) (8 8 8)\n"
		"An older light definition.\n"
		"*/\n");
}

QString entListText()
{
	return QStringLiteral(
		"// entity 0\n"
		"{\n"
		"\"classname\" \"worldspawn\"\n"
		"\"message\" \"Test Map\"\n"
		"}\n"
		"// entity 1\n"
		"{\n"
		"\"classname\" \"func_plat\"\n"
		"\"model\" \"*1\"\n"
		"\"targetname\" \"plat1\"\n"
		"}\n"
		"{\n"
		"\"classname\" \"func_plat\"\n"
		"\"model\" \"*2\"\n"
		"\"speed\" \"100\"\n"
		"}\n");
}

bool runFormatDetectionSmoke()
{
	bool ok = true;
	ok &= expect(detectEntityDefinitionFormat(QStringLiteral("a.def"), radiantDefinitionText().toUtf8()) == EntityDefinitionFormat::RadiantDef,
		"A .def file with /*QUAKED blocks is Radiant.");
	ok &= expect(detectEntityDefinitionFormat(QStringLiteral("a.def"), gameFgdText().toUtf8()) == EntityDefinitionFormat::ValveFgd,
		"FGD content wins over a .def extension.");
	ok &= expect(detectEntityDefinitionFormat(QStringLiteral("a.fgd"), radiantDefinitionText().toUtf8()) == EntityDefinitionFormat::RadiantDef,
		"Radiant content wins over a .fgd extension.");
	ok &= expect(detectEntityDefinitionFormat(QStringLiteral("a.ent"), entListText().toUtf8()) == EntityDefinitionFormat::Quake3Ent,
		"A .ent entity list is detected.");
	ok &= expect(detectEntityDefinitionFormat(QStringLiteral("notes.txt"), QByteArray("hello")) == EntityDefinitionFormat::Unknown,
		"Plain text is not an entity definition file.");
	ok &= expect(entityDefinitionFormatId(EntityDefinitionFormat::ValveFgd) == QStringLiteral("valve-fgd"), "Format ids are stable.");
	ok &= expect(entityKeyTypeFromId(QStringLiteral("color255")) == EntityKeyType::Color, "color255 maps to the colour type.");
	ok &= expect(entityKeyTypeFromId(QStringLiteral("float")) == EntityKeyType::Real, "float maps to the real type.");
	ok &= expect(entityKeyTypeId(EntityKeyType::TargetDestination) == QStringLiteral("target_destination"), "Key type ids are stable.");
	return ok;
}

bool runRadiantSmoke()
{
	bool ok = true;
	const EntityDefinitionCatalogue catalogue = parseEntityDefinitions(QStringLiteral("quake.def"), radiantDefinitionText().toUtf8());
	ok &= expect(catalogue.error.isEmpty(), "A well-formed .def parses without an error.");
	ok &= expect(catalogue.classes.size() == 3, "The .def fixture declares three classes.");
	ok &= expect(catalogue.pointClassCount == 2 && catalogue.brushClassCount == 1, "Point and brush classes are counted apart.");

	EntityClassDefinition spawn;
	ok &= expect(catalogue.classForName(QStringLiteral("info_player_start"), &spawn), "The point class is present.");
	ok &= expect(spawn.kind == EntityClassKind::Point, "A (mins) (maxs) header makes a point class.");
	ok &= expect(spawn.hasSize && spawn.mins[2] == -24.0 && spawn.maxs[2] == 32.0, "The header bounding box is kept.");
	ok &= expect(spawn.hasColor && spawn.color[0] == 255 && spawn.color[1] == 0, "A 0..1 header colour scales to 0..255.");
	ok &= expect(spawn.sourceLine == 1, "The block records its source line.");
	ok &= expect(spawn.description.contains(QStringLiteral("player starts")), "Free text becomes the description.");

	EntityKeyDefinition key;
	ok &= expect(spawn.keyForName(QStringLiteral("targetname"), &key) && key.type == EntityKeyType::TargetSource,
		"A \"key\" description line declares a key.");
	ok &= expect(spawn.keyForName(QStringLiteral("angle"), &key) && key.description == QStringLiteral("direction the player faces"),
		"A key : \"description\" line declares a key.");
	ok &= expect(spawn.keyForName(QStringLiteral("target"), &key) && key.type == EntityKeyType::TargetDestination,
		"Target keys are typed by name.");
	ok &= expect(!spawn.keyForName(QStringLiteral("KEYS")), "A -------- KEYS -------- banner is not a key.");

	const EntitySpawnflagDefinition* initial = flagNamed(spawn, "INITIAL");
	const EntitySpawnflagDefinition* suspended = flagNamed(spawn, "SUSPENDED");
	const EntitySpawnflagDefinition* notFree = flagNamed(spawn, "NOTFREE");
	ok &= expect(initial != nullptr && initial->bit == 0, "The first header flag is bit 0.");
	ok &= expect(suspended != nullptr && suspended->bit == 2, "A - header slot still consumes its bit.");
	ok &= expect(initial != nullptr && !initial->description.isEmpty(), "The spawnflags section documents a header flag.");
	ok &= expect(notFree != nullptr && notFree->bit == 3, "A spawnflags-section flag takes the next free bit.");

	EntityClassDefinition door;
	ok &= expect(catalogue.classForName(QStringLiteral("func_door"), &door), "The brush class is present.");
	ok &= expect(door.kind == EntityClassKind::Brush && !door.hasSize, "The ? size marker means a brush entity.");
	const EntitySpawnflagDefinition* extra = flagNamed(door, "CRUSHER_EXTRA");
	ok &= expect(extra != nullptr && extra->bit == 2, "A bare FLAGNAME body line declares the next flag.");
	ok &= expect(door.keyForName(QStringLiteral("speed"), &key) && key.description == QStringLiteral("how fast the door moves"),
		"A \"key\" : \"description\" line parses too.");

	EntityClassDefinition model;
	ok &= expect(catalogue.classForName(QStringLiteral("misc_model"), &model), "The third class is present.");
	ok &= expect(model.keyForName(QStringLiteral("model"), &key) && key.required, "A (required) marker makes a key mandatory.");
	ok &= expect(key.description == QStringLiteral("the model file to place"), "The required marker is stripped from the description.");

	// A truncated block must warn rather than run off the end of the buffer.
	const EntityDefinitionCatalogue truncated = parseEntityDefinitions(QStringLiteral("bad.def"), QByteArray("/*QUAKED broken (0 0 0) ?\nno terminator here\n"));
	ok &= expect(truncated.classes.isEmpty(), "An unterminated block yields no classes.");
	ok &= expect(hasWarningContaining(truncated.warnings, "never closed"), "An unterminated block warns.");
	return ok;
}

bool runFgdSmoke(const QString& directory)
{
	bool ok = true;
	const QString gamePath = QDir(directory).absoluteFilePath(QStringLiteral("game.fgd"));
	const EntityDefinitionCatalogue catalogue = loadEntityDefinitions(QStringList{gamePath});
	ok &= expect(catalogue.error.isEmpty(), "The FGD fixture loads without an error.");
	ok &= expect(catalogue.sourcePaths.size() == 2, "@include pulls in the base file.");
	ok &= expect(catalogue.classes.size() == 3, "The base class and both derived classes are present.");
	ok &= expect(catalogue.classNames() == (QStringList{QStringLiteral("light"), QStringLiteral("Targetname"), QStringLiteral("trigger_multiple")}),
		"Classes are sorted by name.");
	ok &= expect(catalogue.baseClassCount == 1 && catalogue.pointClassCount == 1 && catalogue.brushClassCount == 1,
		"@BaseClass, @PointClass and @SolidClass map to the three kinds.");

	EntityClassDefinition light;
	ok &= expect(catalogue.classForName(QStringLiteral("light"), &light), "The point class is present.");
	ok &= expect(light.kind == EntityClassKind::Point && light.hasSize && light.mins[0] == -16.0 && light.maxs[0] == 16.0,
		"size() sets the bounding box.");
	ok &= expect(light.hasColor && light.color[1] == 128, "color() is read as 0..255.");
	ok &= expect(light.modelHint == QStringLiteral("sprites/light.spr"), "iconsprite() becomes the model hint.");
	ok &= expect(light.description == QStringLiteral("A light source."), "The = classname : \"description\" tail is read.");
	ok &= expect(light.baseClasses == QStringList{QStringLiteral("Targetname")}, "base() is recorded.");

	EntityKeyDefinition key;
	ok &= expect(light.keyForName(QStringLiteral("light"), &key), "A declared key is found.");
	ok &= expect(key.type == EntityKeyType::Integer && key.defaultValue == QStringLiteral("300"), "Key types and defaults are read.");
	ok &= expect(key.required && key.description == QStringLiteral("How bright the light is."), "A (required) description marks the key.");
	ok &= expect(key.displayName == QStringLiteral("Brightness"), "The display name is read.");
	ok &= expect(light.keyForName(QStringLiteral("style"), &key) && key.type == EntityKeyType::Choices, "A choices key keeps its type.");
	ok &= expect(key.choices.size() == 2 && key.choices.at(1).value == QStringLiteral("10")
			&& key.choices.at(1).label == QStringLiteral("Fluorescent flicker"),
		"Choice rows are parsed.");
	ok &= expect(light.keyForName(QStringLiteral("_color"), &key) && key.type == EntityKeyType::Color, "color255 keys are colours.");
	ok &= expect(light.keyForName(QStringLiteral("targetname"), &key) && key.type == EntityKeyType::TargetSource,
		"An inherited key is folded into the derived class.");

	const EntitySpawnflagDefinition* startDisabled = flagNamed(light, "Start disabled");
	const EntitySpawnflagDefinition* silent = flagNamed(light, "Silent");
	ok &= expect(startDisabled != nullptr && startDisabled->bit == 0, "A flags value of 1 is bit 0.");
	ok &= expect(silent != nullptr && silent->bit == 3 && silent->defaultOn, "A flags value of 8 is bit 3, and the default is read.");

	EntityClassDefinition trigger;
	ok &= expect(catalogue.classForName(QStringLiteral("trigger_multiple"), &trigger), "The solid class is present.");
	ok &= expect(trigger.kind == EntityClassKind::Brush, "@SolidClass is a brush class.");
	ok &= expect(trigger.keyForName(QStringLiteral("target"), &key) && key.type == EntityKeyType::TargetDestination
			&& key.defaultValue.isEmpty() && key.description == QStringLiteral("What to fire."),
		"An omitted default between two colons is handled.");
	ok &= expect(trigger.keyForName(QStringLiteral("wait"), &key) && key.type == EntityKeyType::Real && key.defaultValue == QStringLiteral("0.5"),
		"A numeric default is kept verbatim.");
	ok &= expect(trigger.spawnflags.size() == 3, "Own flags and inherited flags are merged.");
	ok &= expect(flagNamed(trigger, "Monsters") != nullptr && flagNamed(trigger, "Monsters")->bit == 1, "A flags value of 2 is bit 1.");
	ok &= expect(flagNamed(trigger, "Silent") != nullptr, "Inherited flags survive on the derived class.");

	const QString helpText = entityKeyHelpText(light, QStringLiteral("style"));
	ok &= expect(helpText.contains(QStringLiteral("Appearance")) && helpText.contains(QStringLiteral("Fluorescent flicker")),
		"Key help lists the display name and the choices.");
	ok &= expect(entityKeyHelpText(light, QStringLiteral("nope")).contains(QStringLiteral("not declared")), "Unknown keys say so.");
	ok &= expect(entityClassSummaryLines(light).size() > 4, "A class summary has content.");

	const QJsonObject json = entityDefinitionCatalogueJson(catalogue);
	ok &= expect(json.value(QStringLiteral("classes")).toArray().size() == 3, "The catalogue serializes its classes.");
	ok &= expect(json.value(QStringLiteral("totals")).toObject().value(QStringLiteral("base")).toInt() == 1, "Totals are serialized.");
	return ok;
}

bool runIncludeGuardSmoke(const QString& directory)
{
	bool ok = true;
	const QString path = QDir(directory).absoluteFilePath(QStringLiteral("selfref.fgd"));
	const EntityDefinitionCatalogue catalogue = loadEntityDefinitions(QStringList{path});
	ok &= expect(catalogue.classes.size() == 1, "A self-including FGD still yields its own class.");
	ok &= expect(catalogue.sourcePaths.size() == 1, "A file is never parsed twice through an include cycle.");
	ok &= expect(hasWarningContaining(catalogue.warnings, "cycle"), "The include cycle is reported.");
	return ok;
}

bool runCycleSmoke(const QString& directory)
{
	bool ok = true;
	const EntityDefinitionCatalogue catalogue = loadEntityDefinitions(QStringList{directory});
	ok &= expect(catalogue.classes.size() == 3, "A cyclic FGD still yields its classes.");
	ok &= expect(hasWarningContaining(catalogue.warnings, "cycle"), "An inheritance cycle is reported.");
	EntityClassDefinition probe;
	ok &= expect(catalogue.classForName(QStringLiteral("cycle_probe"), &probe), "The class below the cycle still loads.");
	return ok;
}

bool runDirectorySmoke(const QString& directory)
{
	bool ok = true;
	const EntityDefinitionCatalogue catalogue = loadEntityDefinitions(QStringList{directory});
	ok &= expect(catalogue.error.isEmpty(), "A directory of definitions loads.");
	ok &= expect(catalogue.sourcePaths.size() == 4, "Every supported file in the directory is read once.");
	ok &= expect(catalogue.classes.size() == 6, "Both catalogues are merged.");
	ok &= expect(catalogue.classForName(QStringLiteral("func_door")) && catalogue.classForName(QStringLiteral("trigger_multiple")),
		"Radiant and FGD classes live in the same catalogue.");
	ok &= expect(hasWarningContaining(catalogue.warnings, "Duplicate entity class"), "A duplicate class name warns.");

	EntityClassDefinition light;
	ok &= expect(catalogue.classForName(QStringLiteral("light"), &light), "The duplicated class is present once.");
	ok &= expect(light.keyForName(QStringLiteral("style")), "The later definition of a duplicate wins.");

	QStringList names = catalogue.classNames();
	QStringList sorted = names;
	std::sort(sorted.begin(), sorted.end(), [](const QString& a, const QString& b) {
		return QString::compare(a, b, Qt::CaseInsensitive) < 0;
	});
	ok &= expect(names == sorted, "The merged catalogue is sorted by class name.");
	return ok;
}

bool runEntSmoke()
{
	bool ok = true;
	const EntityDefinitionCatalogue catalogue = parseEntityDefinitions(QStringLiteral("maps/test.ent"), entListText().toUtf8());
	ok &= expect(catalogue.error.isEmpty(), "A .ent list parses.");
	ok &= expect(catalogue.classes.size() == 2, "Repeated classnames collapse to one class.");
	EntityClassDefinition plat;
	ok &= expect(catalogue.classForName(QStringLiteral("func_plat"), &plat), "A class is recovered from the list.");
	ok &= expect(plat.kind == EntityClassKind::Brush, "An inline *N model means a brush entity.");
	ok &= expect(plat.keyForName(QStringLiteral("targetname")) && plat.keyForName(QStringLiteral("speed")),
		"Keys from every instance of a class are unioned.");
	ok &= expect(hasWarningContaining(catalogue.warnings, "placed entities"), "The .ent reader says what the format cannot carry.");
	return ok;
}

bool runSearchPathSmoke()
{
	bool ok = true;
	const QStringList paths = entityDefinitionSearchPaths(QStringLiteral("/projects/demo"));
	ok &= expect(paths.size() == 6, "There are six conventional definition folders.");
	ok &= expect(paths.first().endsWith(QStringLiteral(".vibestudio/definitions")), "The project-private folder comes first.");
	ok &= expect(paths.at(1).endsWith(QStringLiteral("definitions")) && paths.at(2).endsWith(QStringLiteral("defs")),
		"The conventional order is kept.");
	ok &= expect(paths.last().endsWith(QStringLiteral("entities")), "The last folder is entities.");
	for (const QString& path : paths) {
		ok &= expect(path.contains(QStringLiteral("/projects/demo")), "Search paths are rooted at the project.");
	}
	ok &= expect(entityDefinitionSearchPaths(QString()).size() == 6, "An empty root still lists the relative folders.");
	return ok;
}

LevelMapEntity makeEntity(int id, const QString& className, const QVector<QPair<QString, QString>>& keys)
{
	LevelMapEntity entity;
	entity.id = id;
	entity.className = className;
	if (!className.isEmpty()) {
		entity.properties.append({QStringLiteral("classname"), className, 0});
	}
	for (const QPair<QString, QString>& pair : keys) {
		entity.properties.append({pair.first, pair.second, 0});
	}
	return entity;
}

LevelMapBrush makeBrush(int id, int entityId)
{
	LevelMapBrush brush;
	brush.id = id;
	brush.entityId = entityId;
	brush.faceCount = 6;
	return brush;
}

bool runQuakeValidationSmoke(const EntityDefinitionCatalogue& catalogue)
{
	bool ok = true;
	LevelMapDocument document;
	document.mapName = QStringLiteral("validate");
	document.format = LevelMapFormat::QuakeMap;
	document.engineFamily = QStringLiteral("idTech2");

	document.entities.append(makeEntity(0, QStringLiteral("light"),
		{{QStringLiteral("light"), QStringLiteral("300")},
			{QStringLiteral("style"), QStringLiteral("0")},
			{QStringLiteral("targetname"), QStringLiteral("beacon")},
			{QStringLiteral("spawnflags"), QStringLiteral("256")}}));
	document.entities.append(makeEntity(1, QStringLiteral("trigger_multiple"),
		{{QStringLiteral("target"), QStringLiteral("beacon")}, {QStringLiteral("wait"), QStringLiteral("0.5")}}));
	document.entities.append(makeEntity(2, QStringLiteral("does_not_exist"), {}));

	LevelMapEntity nameless;
	nameless.id = 3;
	nameless.className = QStringLiteral("entity");
	nameless.properties.append({QStringLiteral("light"), QStringLiteral("200"), 0});
	document.entities.append(nameless);

	document.entities.append(makeEntity(4, QStringLiteral("light"),
		{{QStringLiteral("light"), QStringLiteral("abc")},
			{QStringLiteral("style"), QStringLiteral("7")},
			{QStringLiteral("spawnflags"), QStringLiteral("16")}}));
	document.entities.append(makeEntity(5, QStringLiteral("light"), {{QStringLiteral("foo"), QStringLiteral("bar")}}));
	document.entities.append(makeEntity(6, QStringLiteral("func_door"), {}));
	document.entities.append(makeEntity(7, QStringLiteral("info_player_start"), {}));
	document.entities.append(makeEntity(8, QStringLiteral("Targetname"), {}));
	document.entities.append(makeEntity(9, QStringLiteral("trigger_multiple"), {{QStringLiteral("target"), QStringLiteral("nowhere")}}));
	document.entities.append(makeEntity(10, QStringLiteral("light"),
		{{QStringLiteral("light"), QStringLiteral("200")}, {QStringLiteral("targetname"), QStringLiteral("lonely")}}));

	document.brushes.append(makeBrush(0, 1));
	document.brushes.append(makeBrush(1, 7));
	document.brushes.append(makeBrush(2, 9));

	const EntityValidationReport report = validateLevelMapEntities(document, catalogue);
	ok &= expect(report.entityCount == 11, "Every entity is walked.");
	ok &= expect(report.state() == OperationState::Warning, "A map with issues reports Warning.");

	ok &= expect(countCode(report, "entity-missing-classname") == 1, "An entity with no classname key is reported once.");
	ok &= expect(countCode(report, "entity-unknown-class") == 1, "An undefined classname is reported once.");
	ok &= expect(report.unknownClassNames == QStringList{QStringLiteral("does_not_exist")}, "Unknown classnames are collected.");
	ok &= expect(report.unknownClassCount == 1 && report.knownClassCount == 5, "Known and unknown classes are counted.");

	ok &= expect(countCode(report, "entity-undeclared-key") == 1, "Only the one undeclared key is reported.");
	ok &= expect(countCode(report, "entity-required-key-missing") == 1, "A missing required key is reported.");
	ok &= expect(countCode(report, "entity-key-value-invalid") == 2, "A bad integer and a bad choice are both reported.");
	ok &= expect(countCode(report, "entity-unknown-spawnflag-bit") == 1, "Only the undefined spawnflag bit is reported.");
	ok &= expect(countCode(report, "entity-class-kind-mismatch") == 2, "Both point/brush mismatches are reported.");
	ok &= expect(countCode(report, "entity-base-class-used") == 1, "Placing a base class is reported.");
	ok &= expect(countCode(report, "entity-dangling-target") == 1, "A target with no targetname is reported.");
	ok &= expect(report.danglingTargets == QStringList{QStringLiteral("nowhere")}, "Dangling targets are collected.");
	ok &= expect(countCode(report, "entity-unreachable-targetname") == 1, "A targetname nothing targets is reported.");
	ok &= expect(report.unreachableTargetNames == QStringList{QStringLiteral("lonely")}, "Unreachable targetnames are collected.");
	ok &= expect(report.errorCount == 4 && report.warningCount > 0, "Errors and warnings are counted apart.");

	bool sawSkillBit = false;
	bool sawDeclaredKeyComplaint = false;
	for (const EntityValidationIssue& issue : report.issues) {
		if (issue.code == QStringLiteral("entity-unknown-spawnflag-bit") && issue.entityId == 0) {
			sawSkillBit = true;
		}
		if (issue.code == QStringLiteral("entity-undeclared-key")
			&& (issue.key == QStringLiteral("targetname") || issue.key == QStringLiteral("wait") || issue.key == QStringLiteral("style"))) {
			sawDeclaredKeyComplaint = true;
		}
	}
	ok &= expect(!sawSkillBit, "The engine skill bits are never reported as unknown.");
	ok &= expect(!sawDeclaredKeyComplaint, "Declared and inherited keys are never reported as undeclared.");

	const QJsonObject json = entityValidationReportJson(report);
	ok &= expect(json.value(QStringLiteral("issues")).toArray().size() == report.issueCount, "Issues are serialized.");
	ok &= expect(json.value(QStringLiteral("state")).toString() == QStringLiteral("warning"), "The state is serialized.");
	ok &= expect(!entityValidationText(report).isEmpty(), "The validation report renders as text.");

	LevelMapDocument clean;
	clean.mapName = QStringLiteral("clean");
	clean.format = LevelMapFormat::QuakeMap;
	clean.entities.append(makeEntity(0, QStringLiteral("trigger_multiple"), {{QStringLiteral("target"), QStringLiteral("beacon")}}));
	clean.entities.append(makeEntity(1, QStringLiteral("light"),
		{{QStringLiteral("light"), QStringLiteral("300")}, {QStringLiteral("targetname"), QStringLiteral("beacon")}}));
	clean.brushes.append(makeBrush(0, 0));
	const EntityValidationReport cleanReport = validateLevelMapEntities(clean, catalogue);
	ok &= expect(cleanReport.issues.isEmpty(), "A valid map produces no issues.");
	ok &= expect(cleanReport.state() == OperationState::Completed, "A valid map reports Completed.");
	ok &= expect(cleanReport.danglingTargets.isEmpty() && cleanReport.unreachableTargetNames.isEmpty(),
		"A matched target/targetname pair is clean in both directions.");
	return ok;
}

bool runDoomValidationSmoke(const EntityDefinitionCatalogue& catalogue)
{
	bool ok = true;
	LevelMapDocument document;
	document.mapName = QStringLiteral("MAP01");
	document.format = LevelMapFormat::DoomWad;
	document.engineFamily = QStringLiteral("idTech1");
	for (int index = 0; index < 12; ++index) {
		LevelMapEntity entity;
		entity.id = index;
		entity.className = QStringLiteral("thing:%1").arg(index + 1);
		entity.properties = {
			{QStringLiteral("type"), QString::number(index + 1), 0},
			{QStringLiteral("angle"), QStringLiteral("90"), 0},
			{QStringLiteral("flags"), QStringLiteral("7"), 0},
			{QStringLiteral("origin"), QStringLiteral("32 64 0"), 0},
			{QStringLiteral("x"), QStringLiteral("32"), 0},
			{QStringLiteral("y"), QStringLiteral("64"), 0},
		};
		document.entities.append(entity);
	}

	const EntityValidationReport report = validateLevelMapEntities(document, catalogue);
	ok &= expect(report.entityCount == 12, "Doom things are walked as entities.");
	ok &= expect(countCode(report, "entity-unknown-class") == 0, "Doom things are not reported as unknown classes.");
	ok &= expect(countCode(report, "entity-missing-classname") == 0, "Doom things are not reported as nameless.");
	ok &= expect(countCode(report, "entity-undeclared-key") == 0, "Mirrored Doom keys are not reported as undeclared.");
	ok &= expect(report.unknownClassNames.isEmpty(), "No Doom thing type lands in the unknown class list.");
	ok &= expect(report.issues.isEmpty() && report.state() == OperationState::Completed, "A Doom map with no real issues is Completed.");
	ok &= expect(hasWarningContaining(report.warnings, "thing"), "The report says why the things were not checked.");
	return ok;
}

bool runEmptyCatalogueSmoke()
{
	bool ok = true;
	LevelMapDocument document;
	document.format = LevelMapFormat::QuakeMap;
	document.entities.append(makeEntity(0, QStringLiteral("worldspawn"), {}));
	document.entities.append(makeEntity(1, QStringLiteral("light"), {}));
	document.brushes.append(makeBrush(0, 0));

	const EntityDefinitionCatalogue empty;
	const EntityValidationReport report = validateLevelMapEntities(document, empty);
	ok &= expect(countCode(report, "entity-unknown-class") == 0, "With no catalogue loaded, nothing is called unknown.");
	ok &= expect(report.state() == OperationState::Completed, "An unchecked map is not reported as broken.");
	ok &= expect(hasWarningContaining(report.warnings, "No entity definitions"), "The report says the catalogue was empty.");

	const EntityDefinitionCatalogue missing = loadEntityDefinitions(QStringList{QStringLiteral("definitely/not/here")});
	ok &= expect(!missing.error.isEmpty(), "Loading nothing is an error, not a crash.");
	ok &= expect(parseEntityDefinitions(QStringLiteral("x.def"), QByteArray()).error.isEmpty() == false, "An empty file is an error.");
	return ok;
}

} // namespace

int main()
{
	QTemporaryDir temporaryDir;
	if (!temporaryDir.isValid()) {
		std::cerr << "Unable to create a temporary directory.\n";
		return EXIT_FAILURE;
	}

	const QString catalogueDir = QDir(temporaryDir.path()).absoluteFilePath(QStringLiteral("catalogue"));
	const QString cycleDir = QDir(temporaryDir.path()).absoluteFilePath(QStringLiteral("cycle"));
	const QString includeDir = QDir(temporaryDir.path()).absoluteFilePath(QStringLiteral("includes"));
	if (!writeTextFile(QDir(catalogueDir).absoluteFilePath(QStringLiteral("quake.def")), radiantDefinitionText())
		|| !writeTextFile(QDir(catalogueDir).absoluteFilePath(QStringLiteral("base.fgd")), baseFgdText())
		|| !writeTextFile(QDir(catalogueDir).absoluteFilePath(QStringLiteral("game.fgd")), gameFgdText())
		|| !writeTextFile(QDir(catalogueDir).absoluteFilePath(QStringLiteral("dup.def")), duplicateDefText())
		|| !writeTextFile(QDir(cycleDir).absoluteFilePath(QStringLiteral("cycle.fgd")), cycleFgdText())
		|| !writeTextFile(QDir(includeDir).absoluteFilePath(QStringLiteral("selfref.fgd")), selfIncludingFgdText())) {
		std::cerr << "Unable to write the definition fixtures.\n";
		return EXIT_FAILURE;
	}

	bool ok = runFormatDetectionSmoke();
	ok &= runRadiantSmoke();
	ok &= runFgdSmoke(catalogueDir);
	ok &= runIncludeGuardSmoke(includeDir);
	ok &= runCycleSmoke(cycleDir);
	ok &= runDirectorySmoke(catalogueDir);
	ok &= runEntSmoke();
	ok &= runSearchPathSmoke();

	const EntityDefinitionCatalogue catalogue = loadEntityDefinitions(QStringList{catalogueDir});
	ok &= runQuakeValidationSmoke(catalogue);
	ok &= runDoomValidationSmoke(catalogue);
	ok &= runEmptyCatalogueSmoke();
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
