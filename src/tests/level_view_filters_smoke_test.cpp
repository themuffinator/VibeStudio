// The Levels view filters (core/level_view_filters.h) and `map filters`:
// every kind's classification on a Quake III map with one object of each
// kind, Radiant's world rule (worldspawn, func_group and func_detail), point
// entities without lights and models, Brushes across owners, the formats
// that offer each kind, Doom thing categories, Invert Filters, the saved
// list, the command ids, the Radiant profiles' Filter keys, and the CLI
// query over the same service. With the vibestudio binary as the first
// argument the CLI is checked too.

#include "core/editor_profiles.h"
#include "core/level_map.h"
#include "core/level_view_filters.h"
#include "core/studio_settings.h"
#include "tests/level_view_filter_test_helpers.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>

#include <iostream>
#include <tuple>
#include <utility>

using namespace vibestudio;

namespace {

bool ok = true;

bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << label << ": " << detail.toStdString() << std::endl;
	}
	return value;
}

QStringList selectors(const QVector<LevelMapSelectionRef>& refs)
{
	QStringList ids;
	for (const LevelMapSelectionRef& ref : refs) {
		ids << levelMapSelectionRefId(ref);
	}
	return ids;
}

QStringList kindIds()
{
	return {QStringLiteral("world"), QStringLiteral("entities"), QStringLiteral("brushes"), QStringLiteral("brush-entities"),
		QStringLiteral("func-groups"), QStringLiteral("patches"), QStringLiteral("detail"), QStringLiteral("structural"), QStringLiteral("clip"),
		QStringLiteral("player-clip"), QStringLiteral("monster-clip"), QStringLiteral("weapon-clip"), QStringLiteral("botclip"), QStringLiteral("hint"),
		QStringLiteral("caulk"), QStringLiteral("nodraw"), QStringLiteral("areaportals"), QStringLiteral("clusterportals"), QStringLiteral("lightgrid"),
		QStringLiteral("sky"), QStringLiteral("liquids"), QStringLiteral("translucent"), QStringLiteral("decals"), QStringLiteral("point-entities"),
		QStringLiteral("lights"), QStringLiteral("triggers"), QStringLiteral("monsters"), QStringLiteral("items"), QStringLiteral("starts"),
		QStringLiteral("paths"), QStringLiteral("models")};
}

// What each kind names in tests::viewFilterFixture().
QHash<QString, QStringList> expectedMatches()
{
	return {
		{QStringLiteral("world"), {"brush:0", "brush:1", "brush:2", "brush:3", "brush:4", "brush:5", "patch:0", "entity:1", "entity:2", "entity:13"}},
		{QStringLiteral("entities"), {"entity:3", "entity:4", "entity:5", "entity:6", "entity:7", "entity:8", "entity:9", "entity:10", "entity:11", "entity:12", "entity:14"}},
		{QStringLiteral("brushes"), {"brush:0", "brush:1", "brush:2", "brush:3", "brush:4", "brush:5", "brush:6", "brush:7", "brush:8", "brush:9"}},
		{QStringLiteral("brush-entities"), {"entity:3", "entity:4", "entity:14"}},
		{QStringLiteral("func-groups"), {"entity:1", "entity:13"}},
		{QStringLiteral("patches"), {"patch:0", "patch:1"}},
		{QStringLiteral("detail"), {"brush:5", "brush:7"}},
		{QStringLiteral("structural"), {"brush:0", "brush:1", "brush:2", "brush:3", "brush:4", "brush:6", "brush:8", "brush:9"}},
		{QStringLiteral("clip"), {"brush:2"}},
		{QStringLiteral("player-clip"), {"brush:2"}},
		{QStringLiteral("monster-clip"), {}},
		{QStringLiteral("weapon-clip"), {}},
		{QStringLiteral("botclip"), {}},
		{QStringLiteral("areaportals"), {}},
		{QStringLiteral("clusterportals"), {}},
		{QStringLiteral("lightgrid"), {}},
		{QStringLiteral("decals"), {}},
		{QStringLiteral("hint"), {"brush:3"}},
		{QStringLiteral("caulk"), {"brush:0"}},
		{QStringLiteral("nodraw"), {}},
		{QStringLiteral("sky"), {"brush:1"}},
		{QStringLiteral("liquids"), {"brush:4"}},
		{QStringLiteral("translucent"), {}},
		{QStringLiteral("point-entities"), {"entity:7", "entity:8", "entity:9", "entity:10", "entity:11", "entity:12"}},
		{QStringLiteral("lights"), {"entity:5"}},
		{QStringLiteral("triggers"), {"entity:4"}},
		{QStringLiteral("monsters"), {"entity:8"}},
		{QStringLiteral("items"), {"entity:9", "entity:10"}},
		{QStringLiteral("starts"), {"entity:7"}},
		{QStringLiteral("paths"), {"entity:11"}},
		{QStringLiteral("models"), {"entity:6"}},
	};
}

bool loadFixture(LevelMapDocument* document)
{
	QString error;
	return expect(loadLevelMapBytes({QStringLiteral("filters.map"), {}, QStringLiteral("idTech3")}, tests::viewFilterFixture(), document, &error),
		"load the filter fixture", error);
}

void kinds()
{
	QStringList ids;
	for (const LevelViewFilter& kind : levelViewFilterKinds()) {
		ids << kind.id;
		expect(!kind.title.isEmpty() && !kind.description.isEmpty(), "every kind has a title and a description", kind.id);
	}
	expect(ids == kindIds(), "kinds in menu order", ids.join(QLatin1Char(',')));
	expect(levelViewFilterCommandId(QStringLiteral("world")) == QLatin1String("map.filterWorld")
			&& levelViewFilterCommandId(QStringLiteral("brush-entities")) == QLatin1String("map.filterBrushEntities")
			&& levelViewFilterCommandId(QStringLiteral("func-groups")) == QLatin1String("map.filterFuncGroups")
			&& levelViewFilterCommandId(QStringLiteral("point-entities")) == QLatin1String("map.filterPointEntities"),
		"command ids follow the kind ids");
	expect(levelViewFilterCommandId(QStringLiteral("things:0")).isEmpty() && levelViewFilterCommandId(QString()).isEmpty(),
		"Doom categories have no command");
	expect(levelViewFilterWorldClass(QStringLiteral("worldspawn")) && levelViewFilterWorldClass(QStringLiteral("FUNC_GROUP"))
			&& levelViewFilterWorldClass(QStringLiteral("func_detail_wall")) && !levelViewFilterWorldClass(QStringLiteral("func_door"))
			&& !levelViewFilterWorldClass(QStringLiteral("func_groups")) && !levelViewFilterWorldClass(QStringLiteral("light")),
		"Radiant's world classes");
}

void quake3()
{
	LevelMapDocument document;
	if (!loadFixture(&document)) {
		return;
	}
	expect(document.format == LevelMapFormat::Quake3Map && document.entities.size() == 15 && document.brushes.size() == 10 && document.patches.size() == 2,
		"fixture shape", QStringLiteral("%1 entities, %2 brushes, %3 patches").arg(document.entities.size()).arg(document.brushes.size()).arg(document.patches.size()));
	const QByteArray before = serializeLevelMap(document).bytes;
	const auto revision = document.revision;
	QStringList offered;
	for (const LevelViewFilter& filter : levelViewFilters(document)) {
		offered << filter.id;
	}
	expect(offered == kindIds(), "a Quake III map offers every kind", offered.join(QLatin1Char(',')));
	const auto expected = expectedMatches();
	const QHash<QString, int> counts = levelViewFilterCounts(document);
	for (const QString& id : kindIds()) {
		const QStringList matched = selectors(levelViewFilterMatches(document, id));
		expect(matched == expected.value(id), "kind matches", id + QLatin1String(": ") + matched.join(QLatin1Char(',')));
		expect(counts.value(id) == expected.value(id).size(), "count matches", id);
		LevelViewFilter filter;
		expect(levelViewFilterForId(document, id, &filter) && filter.id == id && !filter.title.isEmpty(), "filter found by id", id);
	}
	// Entities is Radiant's inverted world rule: together they name every entity.
	QSet<QString> entities;
	for (const QString& ref : expected.value(QStringLiteral("entities"))) {
		entities.insert(ref);
	}
	for (const QString& ref : expected.value(QStringLiteral("world"))) {
		expect(!entities.contains(ref), "world and entities are disjoint", ref);
	}
	const auto union_ = selectors(levelViewFilteredObjects(document, {QStringLiteral("world"), QStringLiteral("brushes"), QStringLiteral("world")}));
	expect(union_.size() == 14 && QSet<QString>(union_.cbegin(), union_.cend()).size() == 14 && union_.first() == QLatin1String("brush:0")
			&& union_.last() == QLatin1String("brush:9"),
		"switched-off kinds combine without repeats", union_.join(QLatin1Char(',')));
	expect(levelViewFilteredObjects(document, {}).isEmpty() && levelViewFilteredObjects(document, {QStringLiteral("things:0")}).isEmpty(),
		"nothing switched off hides nothing; foreign ids name nothing");
	expect(levelViewFilterMatches(document, QStringLiteral("bogus")).isEmpty() && !levelViewFilterForId(document, QStringLiteral("bogus")),
		"unknown kinds name nothing");
	expect(serializeLevelMap(document).bytes == before && document.revision == revision && document.undoStack.isEmpty(),
		"classifying never changes the map");

	// A Quake or Quake II map has no patches, bot clip, cluster portals, light
	// grid or decals to filter; every other kind stays.
	LevelMapDocument quake = document;
	quake.format = LevelMapFormat::QuakeMap;
	QStringList quakeIds;
	for (const LevelViewFilter& filter : levelViewFilters(quake)) {
		quakeIds << filter.id;
	}
	QStringList withoutQuake3 = kindIds();
	for (const char* id : {"patches", "botclip", "clusterportals", "lightgrid", "decals"}) {
		withoutQuake3.removeAll(QString::fromLatin1(id));
	}
	expect(quakeIds == withoutQuake3, "Quake maps offer every kind but the Quake III ones", quakeIds.join(QLatin1Char(',')));
	LevelMapDocument unknown;
	expect(levelViewFilters(unknown).isEmpty() && levelViewFilterMatches(unknown, QStringLiteral("world")).isEmpty(), "no map, no filters");
}

void doom()
{
	LevelMapDocument document;
	document.format = LevelMapFormat::DoomWad;
	document.doomFormat = LevelMapDoomFormat::Doom;
	const QVector<LevelMapDoomThingType> types = levelMapDoomThingTypes(document.doomFormat);
	if (!expect(!types.isEmpty(), "Doom thing table")) {
		return;
	}
	LevelMapDoomThing known;
	known.id = 0;
	known.type = types.first().type;
	LevelMapDoomThing other;
	other.id = 1;
	other.type = 31999;
	document.doomThings = {known, other};
	const QVector<LevelViewFilter> filters = levelViewFilters(document);
	expect(!filters.isEmpty() && filters.first().id == QLatin1String("things:0") && filters.first().title == types.first().category
			&& filters.last().id == QLatin1String("things:other"),
		"Doom maps filter things by category");
	const QVector<LevelMapSelectionRef> category = levelViewFilterMatches(document, QStringLiteral("things:0"));
	expect(category.size() == 1 && category.first().kind == LevelMapSelectionKind::DoomThing && category.first().objectId == 0,
		"a thing files under its category");
	expect(levelViewFilterMatches(document, QStringLiteral("things:other")).size() == 1
			&& levelViewFilterMatches(document, QStringLiteral("things:other")).first().objectId == 1,
		"unknown types file under Other Things");
	expect(levelViewFilterMatches(document, QStringLiteral("world")).isEmpty() && levelViewFilterMatches(document, QStringLiteral("things:999")).isEmpty(),
		"Quake kinds and missing categories name nothing on a Doom map");
	QStringList all;
	for (const LevelViewFilter& filter : filters) {
		all << filter.id;
	}
	expect(levelViewFiltersInverted(document, {}) == all, "Invert on a Doom map hides every category");
}

void invert()
{
	LevelMapDocument document;
	if (!loadFixture(&document)) {
		return;
	}
	QStringList everyKind = kindIds();
	expect(levelViewFiltersInverted(document, {}) == everyKind, "inverting nothing hides every kind");
	expect(levelViewFiltersInverted(document, everyKind).isEmpty(), "inverting everything shows every kind");
	const QStringList off {QStringLiteral("world"), QStringLiteral("things:3"), QStringLiteral("world"), QStringLiteral("lights")};
	QStringList expected {QStringLiteral("things:3")};
	for (const QString& id : kindIds()) {
		if (id != QLatin1String("world") && id != QLatin1String("lights")) {
			expected << id;
		}
	}
	const QStringList inverted = levelViewFiltersInverted(document, off);
	expect(inverted == expected, "every offered kind flips; other formats' kinds stay", inverted.join(QLatin1Char(',')));
	const QStringList twice = levelViewFiltersInverted(document, inverted);
	expect(QSet<QString>(twice.cbegin(), twice.cend()) == QSet<QString>(off.cbegin(), off.cend()), "inverting twice restores the list",
		twice.join(QLatin1Char(',')));
	LevelMapDocument none;
	expect(levelViewFiltersInverted(none, {QStringLiteral("clip"), QStringLiteral("clip")}) == QStringList {QStringLiteral("clip")},
		"without a map the list only loses repeats");
}

void saved()
{
	expect(levelViewFiltersSettingsKey() == QLatin1String("levelView/filtersOff"), "the View tab's settings key");
	expect(levelViewFiltersFromSaved(" world, clip,,world ") == QStringList {QStringLiteral("world"), QStringLiteral("clip")},
		"saved lists are trimmed and lose repeats");
	expect(levelViewFiltersSavedValue({QStringLiteral("world"), QStringLiteral("clip"), QStringLiteral("world")}) == QByteArrayLiteral("world,clip"),
		"lists save as comma-separated ids");
	expect(levelViewFiltersFromSaved(QByteArray()).isEmpty() && levelViewFiltersSavedValue({}).isEmpty(), "an empty list saves empty");
}

// NetRadiant Custom's Filter keys, which the NetRadiant profile shares; the
// TrenchBroom and studio profiles name none, and Patches never takes Ctrl+P.
void profileKeys()
{
	const QHash<QString, QStringList> radiant {{QStringLiteral("map.filterWorld"), {QStringLiteral("Alt+1")}},
		{QStringLiteral("map.filterEntities"), {QStringLiteral("Alt+2")}}, {QStringLiteral("map.filterLiquids"), {QStringLiteral("Alt+5")}},
		{QStringLiteral("map.filterCaulk"), {QStringLiteral("Alt+6")}}, {QStringLiteral("map.filterClip"), {QStringLiteral("Alt+7")}},
		{QStringLiteral("map.filterPaths"), {QStringLiteral("Alt+8")}}, {QStringLiteral("map.filterLights"), {QStringLiteral("Alt+0")}},
		{QStringLiteral("map.filterDetail"), {QStringLiteral("Ctrl+D")}}, {QStringLiteral("map.filterHint"), {QStringLiteral("Ctrl+H")}},
		{QStringLiteral("map.filterModels"), {QStringLiteral("Shift+M")}}, {QStringLiteral("map.filterTriggers"), {QStringLiteral("Ctrl+Shift+T")}},
		{QStringLiteral("map.filterAreaportals"), {QStringLiteral("Alt+3")}}, {QStringLiteral("map.filterClusterportals"), {QStringLiteral("Alt+9")}},
		{QStringLiteral("map.filterStructural"), {QStringLiteral("Ctrl+Shift+D")}}, {QStringLiteral("map.filterBotclip"), {QStringLiteral("Alt+M")}},
		{QStringLiteral("map.filterDecals"), {QStringLiteral("Shift+D")}}, {QStringLiteral("map.filterTranslucent"), {QStringLiteral("Alt+4")}}};
	const auto filterKeys = [](const QString& profile) {
		QHash<QString, QStringList> keys;
		for (const LevelEditorKeyBinding& binding : levelEditorControlsForProfile(profile).keys) {
			if ((binding.commandId.startsWith(QLatin1String("map.filter")) || binding.commandId.endsWith(QLatin1String("Filters")))
				&& !binding.keys.isEmpty()) {
				keys.insert(binding.commandId, binding.keys);
			}
		}
		return keys;
	};
	for (const QString& profile : {QStringLiteral("netradiant-custom"), QStringLiteral("netradiant")}) {
		expect(filterKeys(profile) == radiant, "Radiant's Filter keys", profile);
	}
	for (const QString& profile : {QStringLiteral("trenchbroom"), QStringLiteral("vibestudio-default")}) {
		expect(filterKeys(profile).isEmpty(), "no filter keys outside the Radiant profiles", profile);
	}
}

void cli(const QString& executable)
{
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "CLI temporary folder")) {
		return;
	}
	const QString path = temp.filePath(QStringLiteral("filters.map"));
	const QByteArray source = tests::viewFilterFixture();
	QFile file(path);
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(), "CLI fixture")) {
		return;
	}
	file.close();
	const QString settingsPath = temp.filePath(QStringLiteral("settings.ini"));
	const auto run = [&](const QStringList& args, int code = 0, bool json = true) {
		QProcess process;
		process.setWorkingDirectory(temp.path());
		QStringList command {QStringLiteral("--cli")};
		if (json) {
			command << QStringLiteral("--json");
		}
		command << QStringLiteral("--settings-file") << settingsPath << QStringLiteral("map") << QStringLiteral("filters");
		process.start(executable, command + args);
		const bool done = process.waitForStarted() && process.waitForFinished(30000);
		const QByteArray out = process.readAllStandardOutput();
		const QByteArray err = process.readAllStandardError();
		expect(done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == code, "CLI exit status",
			args.join(QLatin1Char(' ')) + QLatin1String(" -> ") + QString::fromUtf8(out + err));
		if (!done) {
			process.kill();
			process.waitForFinished();
		}
		return std::make_pair(QJsonDocument::fromJson(out).object(), QString::fromUtf8(out));
	};
	const auto entry = [](const QJsonObject& report, const QString& id) {
		for (const QJsonValue& value : report.value(QStringLiteral("filters")).toArray()) {
			if (value.toObject().value(QStringLiteral("id")).toString() == id) {
				return value.toObject();
			}
		}
		return QJsonObject();
	};
	auto [report, text] = run({path, QStringLiteral("--engine"), QStringLiteral("idtech3")});
	const auto expected = expectedMatches();
	expect(report.value(QStringLiteral("filters")).toArray().size() == kindIds().size() && report.value(QStringLiteral("mapWritten")) == false
			&& report.value(QStringLiteral("queryOnly")) == true && report.value(QStringLiteral("filteredCount")).toInt() == 0
			&& report.value(QStringLiteral("off")).toArray().isEmpty() && report.value(QStringLiteral("sourceSha256")).toString().size() == 64,
		"CLI lists every kind, all shown", text);
	const QJsonObject world = entry(report, QStringLiteral("world"));
	expect(world.value(QStringLiteral("count")).toInt() == expected.value(QStringLiteral("world")).size() && world.value(QStringLiteral("shown")) == true
			&& world.value(QStringLiteral("command")) == QLatin1String("map.filterWorld") && !world.value(QStringLiteral("title")).toString().isEmpty()
			&& !world.contains(QStringLiteral("objects")),
		"CLI World Brushes entry", text);
	std::tie(report, text) = run({path, QStringLiteral("--off"), QStringLiteral("world,lights"), QStringLiteral("--objects")});
	QStringList worldObjects;
	for (const QJsonValue& value : entry(report, QStringLiteral("world")).value(QStringLiteral("objects")).toArray()) {
		worldObjects << value.toString();
	}
	expect(worldObjects == expected.value(QStringLiteral("world")) && entry(report, QStringLiteral("world")).value(QStringLiteral("shown")) == false
			&& entry(report, QStringLiteral("lights")).value(QStringLiteral("shown")) == false
			&& report.value(QStringLiteral("filteredCount")).toInt() == expected.value(QStringLiteral("world")).size() + 1,
		"CLI --off and --objects report what the views hide", text);
	std::tie(report, text) = run({path, QStringLiteral("--off"), QStringLiteral("world"), QStringLiteral("--invert")});
	QStringList off;
	for (const QJsonValue& value : report.value(QStringLiteral("off")).toArray()) {
		off << value.toString();
	}
	QStringList allButWorld = kindIds();
	allButWorld.removeAll(QStringLiteral("world"));
	expect(off == allButWorld && report.value(QStringLiteral("inverted")) == true && entry(report, QStringLiteral("world")).value(QStringLiteral("shown")) == true
			&& entry(report, QStringLiteral("clip")).value(QStringLiteral("shown")) == false,
		"CLI --invert applies Invert Filters", off.join(QLatin1Char(',')));
	// --saved reads the View tab's list from the same settings store; kinds
	// saved for other formats are left out, as the views leave them.
	{
		StudioSettings::setOverrideFilePath(settingsPath);
		StudioSettings settings;
		settings.setShellLayoutState(levelViewFiltersSettingsKey(), QByteArrayLiteral("clip,things:2"));
		settings.sync();
	}
	std::tie(report, text) = run({path, QStringLiteral("--saved")});
	expect(report.value(QStringLiteral("off")).toArray() == QJsonArray {QStringLiteral("clip")} && report.value(QStringLiteral("saved")) == true
			&& report.value(QStringLiteral("filteredObjects")).toArray() == QJsonArray {QStringLiteral("brush:2")},
		"CLI --saved uses the View tab's list", text);
	std::tie(report, text) = run({path, QStringLiteral("--saved"), QStringLiteral("--off"), QStringLiteral("sky")});
	expect(report.value(QStringLiteral("off")).toArray() == QJsonArray({QStringLiteral("clip"), QStringLiteral("sky")}), "CLI --off adds to --saved", text);
	std::tie(report, text) = run({path, QStringLiteral("--off"), QStringLiteral("world")}, 0, false);
	expect(text.contains(QStringLiteral("(world)")) && text.contains(QStringLiteral("(clip)")) && !text.trimmed().isEmpty(), "CLI text report", text);
	for (const QStringList& bad : QVector<QStringList> {{}, {path, QStringLiteral("--off"), QStringLiteral("bogus")},
			 {path, QStringLiteral("--off")}, {path, QStringLiteral("--invert"), QStringLiteral("--invert")}, {path, QStringLiteral("--output"), QStringLiteral("x.map")},
			 {path, QStringLiteral("extra")}, {path, QStringLiteral("--saved=yes")}}) {
		run(bad, 2);
	}
	run({temp.filePath(QStringLiteral("missing.map"))}, 4);
	expect(file.open(QIODevice::ReadOnly) && file.readAll() == source, "the CLI never changes the map");
	file.close();
	StudioSettings settings;
	expect(settings.shellLayoutState(levelViewFiltersSettingsKey()) == QByteArrayLiteral("clip,things:2"), "the CLI never changes the saved filters");
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	kinds();
	quake3();
	doom();
	invert();
	saved();
	profileKeys();
	if (argc == 2) {
		cli(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath());
	}
	if (ok) {
		std::cout << "Level view filters: every kind, invert, saved list, profile keys" << (argc == 2 ? " and CLI" : "") << " passed." << std::endl;
	}
	return ok ? 0 : 1;
}
