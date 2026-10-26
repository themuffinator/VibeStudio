// Data-driven view filter tags and entity class filters
// (core/level_filter_tags.h), the Hint, Skip and Fog kinds
// (core/level_view_filters.h) and the CLI routes over them:
// - Hint takes a hint face only; Skip takes brushes skipped all over and the
//   skip faces of others one by one; Quake's skip stays caulk and its
//   hintskip is skip; Quake II reads SURF_SKIP; Fog takes Quake III fog by
//   shader, the fog content bit or a fog folder;
// - tags parse from TrenchBroom's "tags" layout, a whole game configuration
//   with "faceattribs", or a plain list, with warnings for unusable entries;
//   tag file text may be strict JSON or written as TrenchBroom writes its
//   configurations (comments, bare names, trailing commas, "\**" keeping its
//   backslash), and errors name their line;
//   texture globs, surfaceparms, content and surface flags by name, number
//   or the data's own bit order, and brush entity classes all match, face
//   tags hiding brushes whole only when every face matches;
// - class filters list the definitions' and the map's classes with counts
//   and hide every entity of a class, never a class whose name holds a ',';
//   the combined hidden list leaves out faces of brushes hidden whole and
//   hides a brush whose faces all go under several kinds or tags; Fog by
//   name takes fog folders and fog names only; ids, generated commands, palette entries
//   and the saved list behave; the project manifest holds or names tags;
// - `map filters --tags/--project/--class/--classes` and `map visibility
//   --class` report the same matches without writing anything.
// With the vibestudio binary as the first argument the CLI is checked too.

#include "core/entity_builtin_catalogue.h"
#include "core/level_document.h"
#include "core/level_filter_tags.h"
#include "core/level_map.h"
#include "core/level_view_filters.h"
#include "core/project_manifest.h"
#include "core/studio_semantics.h"
#include "tests/level_view_filter_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>

#include <algorithm>
#include <iostream>
#include <utility>

using namespace vibestudio;

namespace {

bool ok = true;

bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << label << ": " << detail.toUtf8().constData() << std::endl;
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

QStringList faceIds(const QVector<LevelSurfaceFace>& faces)
{
	QStringList ids;
	for (const LevelSurfaceFace& face : faces) {
		ids << QStringLiteral("%1/%2").arg(face.brushId).arg(face.faceIndex);
	}
	return ids;
}

QStringList brushes(std::initializer_list<int> ids)
{
	QStringList refs;
	for (int id : ids) {
		refs << QStringLiteral("brush:%1").arg(id);
	}
	return refs;
}

bool load(const QByteArray& bytes, const QString& engine, LevelMapDocument* document)
{
	QString error;
	return expect(loadLevelMapBytes({QStringLiteral("tags.map"), {}, engine}, bytes, document, &error), "load a fixture", error);
}

LevelMapDocument quake3Document()
{
	LevelMapDocument document;
	load(tests::viewFilterTagsFixture(), QStringLiteral("idTech3"), &document);
	return document;
}

QString flaggedBox(int x0, int x1, const QStringList& textures, qint64 contents = 0, qint64 surface = 0)
{
	return tests::viewFilterFlaggedBox(x0, x1, textures, contents, surface);
}

void kinds()
{
	LevelMapDocument document = quake3Document();
	if (!expect(document.format == LevelMapFormat::Quake3Map && document.brushes.size() == 10 && document.patches.size() == 1 && document.entities.size() == 7,
			"Quake III fixture shape",
			QStringLiteral("%1 brushes, %2 patches, %3 entities").arg(document.brushes.size()).arg(document.patches.size()).arg(document.entities.size()))) {
		return;
	}
	QStringList ids;
	for (const LevelViewFilter& kind : levelViewFilterKinds()) {
		ids << kind.id;
	}
	expect(ids.indexOf(QStringLiteral("skip")) == ids.indexOf(QStringLiteral("hint")) + 1 && ids.indexOf(QStringLiteral("fog")) == ids.indexOf(QStringLiteral("liquids")) + 1,
		"Skip follows Hint and Fog follows Liquids", ids.join(QLatin1Char(',')));
	expect(levelViewFilterCommandId(QStringLiteral("skip")) == QLatin1String("map.filterSkip") && levelViewFilterCommandId(QStringLiteral("fog")) == QLatin1String("map.filterFog"),
		"kind commands");
	expect(selectors(levelViewFilterMatches(document, QStringLiteral("hint"))) == brushes({1}), "Hint takes the hint brush only",
		selectors(levelViewFilterMatches(document, QStringLiteral("hint"))).join(QLatin1Char(',')));
	expect(selectors(levelViewFilterMatches(document, QStringLiteral("skip"))) == brushes({0}), "Skip takes a brush skipped all over",
		selectors(levelViewFilterMatches(document, QStringLiteral("skip"))).join(QLatin1Char(',')));
	const QStringList skipFaces {QStringLiteral("1/0"), QStringLiteral("1/2"), QStringLiteral("1/3"), QStringLiteral("1/4"), QStringLiteral("1/5")};
	expect(faceIds(levelViewFilterFaceMatches(document, QStringLiteral("skip"))) == skipFaces, "Skip hides a hint brush's skip faces one by one",
		faceIds(levelViewFilterFaceMatches(document, QStringLiteral("skip"))).join(QLatin1Char(',')));
	expect(faceIds(levelViewFilteredFaces(document, {QStringLiteral("skip"), QStringLiteral("hint")})).isEmpty(),
		"no skip faces are left once Hint hides the brush whole");
	expect(selectors(levelViewFilterMatches(document, QStringLiteral("fog"))) == brushes({2, 3}), "Fog by folder name and content bit",
		selectors(levelViewFilterMatches(document, QStringLiteral("fog"))).join(QLatin1Char(',')));
	// A package's shader decides: a fog shader under another name counts, a
	// fog-named shader without fog does not.
	LevelViewFilterMaterials materials;
	LevelViewFilterShader fog;
	fog.fog = true;
	fog.translucent = true;
	fog.surfaceParms = {QStringLiteral("fog"), QStringLiteral("nonsolid"), QStringLiteral("trans")};
	materials.shaders.insert(QStringLiteral("textures/base_wall/wall"), LevelViewFilterShader());
	materials.shaders.insert(QStringLiteral("textures/sfx/fog_red"), LevelViewFilterShader());
	materials.shaders.insert(QStringLiteral("textures/liquids/water_calm"), fog);
	expect(levelViewFilterReadsMaterials(QStringLiteral("fog")), "Fog reads shaders");
	expect(selectors(levelViewFilterMatches(document, QStringLiteral("fog"), materials)) == QStringList {QStringLiteral("brush:3"), QStringLiteral("brush:4"),
			   QStringLiteral("brush:5")},
		"with shaders Fog follows them; the content bit still counts",
		selectors(levelViewFilterMatches(document, QStringLiteral("fog"), materials)).join(QLatin1Char(',')));
	// Without a shader, a fog or fogs folder or a name that starts with the
	// word fog counts; fog inside another word does not.
	const QString fogNames = QStringLiteral("// Game: Quake 3\n{\n\"classname\" \"worldspawn\"\n") + flaggedBox(0, 64, {QStringLiteral("fogs/mist")})
		+ flaggedBox(64, 128, {QStringLiteral("fog/murk")}) + flaggedBox(128, 192, {QStringLiteral("base_wall/fogged_glass")})
		+ flaggedBox(192, 256, {QStringLiteral("sfx/kc_fogcloud")}) + flaggedBox(256, 320, {QStringLiteral("sfx/fog2")}) + QStringLiteral("}\n");
	LevelMapDocument fogDocument;
	if (load(fogNames.toUtf8(), QStringLiteral("idTech3"), &fogDocument)) {
		expect(selectors(levelViewFilterMatches(fogDocument, QStringLiteral("fog"))) == brushes({0, 1, 4}), "Fog by name takes fog folders and fog names only",
			selectors(levelViewFilterMatches(fogDocument, QStringLiteral("fog"))).join(QLatin1Char(',')));
	}

	// Quake: skip is caulk, hintskip is Skip; Fog is Quake III only.
	const QString quake = QStringLiteral("{\n\"classname\" \"worldspawn\"\n\"wad\" \"gfx/base.wad\"\n")
		+ flaggedBox(0, 64, {QStringLiteral("skip")}).replace(QStringLiteral(" 0 0 0\n"), QStringLiteral("\n"))
		+ flaggedBox(64, 128, {QStringLiteral("hintskip"), QStringLiteral("hint"), QStringLiteral("hintskip")}).replace(QStringLiteral(" 0 0 0\n"), QStringLiteral("\n"))
		+ flaggedBox(128, 192, {QStringLiteral("hintskip")}).replace(QStringLiteral(" 0 0 0\n"), QStringLiteral("\n")) + QStringLiteral("}\n");
	LevelMapDocument quake1;
	if (load(quake.toUtf8(), QStringLiteral("idTech2"), &quake1) && expect(quake1.brushes.size() == 3, "Quake fixture shape")) {
		expect(selectors(levelViewFilterMatches(quake1, QStringLiteral("caulk"))) == brushes({0}), "Quake's skip is caulk");
		expect(selectors(levelViewFilterMatches(quake1, QStringLiteral("skip"))) == brushes({2}), "Quake's hintskip all over is Skip",
			selectors(levelViewFilterMatches(quake1, QStringLiteral("skip"))).join(QLatin1Char(',')));
		expect(faceIds(levelViewFilterFaceMatches(quake1, QStringLiteral("skip"))).size() == 5, "hintskip sides go face by face");
		expect(selectors(levelViewFilterMatches(quake1, QStringLiteral("hint"))) == brushes({1}), "hintskip is not hint");
		expect(!levelViewFilterForId(quake1, QStringLiteral("fog")) && levelViewFilterForId(quake1, QStringLiteral("skip")), "Quake offers Skip, not Fog");
	}
	// Quake II: SURF_SKIP and SURF_HINT.
	QString quake2 = QString::fromLatin1(kQuake2MapTargetHeader) + QStringLiteral("{\n\"classname\" \"worldspawn\"\n");
	quake2 += flaggedBox(0, 64, {QStringLiteral("e1u1/clip")}, 0, 0x200);
	quake2 += flaggedBox(64, 128, {QStringLiteral("e1u1/clip")}, 0, 0x100);
	quake2 += QStringLiteral("}\n");
	LevelMapDocument q2;
	if (load(quake2.toUtf8(), QStringLiteral("idTech2"), &q2)) {
		expect(selectors(levelViewFilterMatches(q2, QStringLiteral("skip"))) == brushes({0}) && selectors(levelViewFilterMatches(q2, QStringLiteral("hint"))) == brushes({1}),
			"Quake II reads the skip and hint surface flags");
	}
}

void parsing()
{
	LevelFilterTagSet set;
	QString error;
	const QJsonDocument layout = QJsonDocument::fromJson(tests::viewFilterTagLayout());
	expect(parseLevelFilterTags(layout.object(), QStringLiteral("tags.json"), &set, &error), "TrenchBroom tag layout parses", error);
	expect(set.ids() == QStringList {QStringLiteral("tag:triggers"), QStringLiteral("tag:water"), QStringLiteral("tag:fog-volume"), QStringLiteral("tag:detail"),
							QStringLiteral("tag:mist"), QStringLiteral("tag:skip-faces")},
		"usable tags in order, brush tags first", set.ids().join(QLatin1Char(',')));
	expect(set.warnings.size() == 5, "five entries skipped with a warning each", set.warnings.join(QStringLiteral(" | ")));
	const LevelFilterTag* triggers = set.find(QStringLiteral("tag:triggers"));
	expect(triggers && triggers->match == LevelFilterTagMatch::ClassName && triggers->patterns == QStringList {QStringLiteral("trigger_*")}
			&& triggers->source == QLatin1String("tags.json"),
		"a class tag keeps its pattern and source");
	const LevelFilterTag* mist = set.find(QStringLiteral("tag:mist"));
	expect(mist && mist->flagBits == 0x40 && mist->flagNames.isEmpty(), "numeric flags");
	expect(!levelFilterTagDescription(*set.find(QStringLiteral("tag:water"))).isEmpty(), "tags describe themselves");
	LevelFilterTagSet translated;
	const QJsonArray translatedNames {
		QJsonObject {{QStringLiteral("name"), QString::fromUtf8("\xe6\xb0\xb4")}, {QStringLiteral("match"), QStringLiteral("texture")}, {QStringLiteral("pattern"), QStringLiteral("*water*")}},
		QJsonObject {{QStringLiteral("name"), QString::fromUtf8("\xe9\x9c\xa7")}, {QStringLiteral("match"), QStringLiteral("texture")}, {QStringLiteral("pattern"), QStringLiteral("*fog*")}}};
	expect(parseLevelFilterTags(translatedNames, QStringLiteral("translated.json"), &translated, &error) && translated.tags.size() == 2,
		"non-Latin tag names remain usable", error);
	if (translated.tags.size() == 2) {
		LevelFilterTagSet repeated;
		parseLevelFilterTags(translatedNames, QStringLiteral("translated-again.json"), &repeated, &error);
		expect(translated.ids() == repeated.ids() && translated.tags.at(0).id != translated.tags.at(1).id
			&& isLevelFilterTagId(translated.tags.at(0).id) && !levelFilterTagCommandId(translated.tags.at(0).id).isEmpty(),
			"non-Latin tag ids are distinct, stable and valid commands");
	}

	// A whole game configuration names its flags in bit order.
	LevelFilterTagSet config;
	const QByteArray gameConfig = R"({ "version": 9, "name": "Example",
		"faceattribs": { "surfaceflags": [ { "name": "light" }, { "name": "slick" } ],
						 "contentflags": [ { "name": "solid" }, { "unused": true }, { "name": "murk" } ] },
		"tags": { "brushface": [ { "name": "Murk", "match": "contentflag", "flags": [ "murk" ] },
								 { "name": "Slick", "match": "surfaceflag", "flags": "slick" } ] } })";
	expect(parseLevelFilterTags(QJsonDocument::fromJson(gameConfig).object(), QStringLiteral("Example.cfg"), &config, &error) && config.tags.size() == 2,
		"a game configuration's tags", error);
	LevelMapDocument quake3 = quake3Document();
	if (config.tags.size() == 2) {
		expect(levelFilterTagFlagBits(config.tags.at(0), quake3) == 0x4 && levelFilterTagFlagBits(config.tags.at(1), quake3) == 0x2,
			"flag names follow the configuration's bit order");
	}
	// A plain list, the scope taken from the match.
	LevelFilterTagSet plain;
	expect(parseLevelFilterTags(QJsonDocument::fromJson(R"([ { "name": "Doors", "match": "classname", "pattern": "func_door*" },
		{ "name": "Hints", "match": "texture", "pattern": "hint*" } ])").array(), QStringLiteral("list.json"), &plain, &error)
			&& plain.ids() == QStringList {QStringLiteral("tag:doors"), QStringLiteral("tag:hints")},
		"a plain list of tags", plain.ids().join(QLatin1Char(',')));
	LevelFilterTagSet nothing;
	expect(!parseLevelFilterTags(QJsonDocument::fromJson(R"({ "name": "Not tags" })").object(), QStringLiteral("x.json"), &nothing, &error) && !error.isEmpty(),
		"an object without tag lists is refused");
	// At most 64 tags.
	QJsonArray many;
	for (int index = 0; index < 70; ++index) {
		many.append(QJsonObject {{QStringLiteral("name"), QStringLiteral("Tag %1").arg(index)}, {QStringLiteral("match"), QStringLiteral("texture")},
			{QStringLiteral("pattern"), QStringLiteral("t%1").arg(index)}});
	}
	LevelFilterTagSet capped;
	expect(parseLevelFilterTags(many, QStringLiteral("many.json"), &capped, &error) && capped.tags.size() == kLevelFilterTagLimit && capped.warnings.size() == 6,
		"tags stop at the limit", QString::number(capped.tags.size()));
	// Merging keeps the first of a repeated id.
	LevelFilterTagSet merged = set;
	mergeLevelFilterTags(&merged, plain);
	LevelFilterTagSet again;
	parseLevelFilterTags(QJsonDocument::fromJson(R"([ { "name": "WATER", "match": "texture", "pattern": "x" } ])").array(), QStringLiteral("b.json"), &again, &error);
	const int before = static_cast<int>(merged.warnings.size());
	mergeLevelFilterTags(&merged, again);
	expect(merged.tags.size() == 8 && merged.find(QStringLiteral("tag:water"))->patterns == QStringList {QStringLiteral("*water*")}
			&& merged.warnings.size() == before + 1 && merged.sources.size() == 3,
		"merging keeps the first tag of an id");

	// Tag file text: strict JSON reads as before.
	LevelFilterTagSet strict;
	expect(parseLevelFilterTagText(tests::viewFilterTagLayout(), QStringLiteral("tags.json"), &strict, &error) && strict.ids() == set.ids()
			&& strict.warnings.size() == set.warnings.size(),
		"strict JSON text reads as the JSON value does", error);
	LevelFilterTagSet escapedJson;
	expect(parseLevelFilterTagText(R"({ "brushface": [ { "name": "Liquid", "match": "texture", "pattern": "\\**" },
		{ "name": "Slash", "match": "texture", "pattern": "a\/b*" } ] })", QStringLiteral("e.json"), &escapedJson, &error)
			&& escapedJson.tags.size() == 2 && escapedJson.tags.at(0).patterns == QStringList {QStringLiteral("\\**")}
			&& escapedJson.tags.at(1).patterns == QStringList {QStringLiteral("a/b*")},
		"JSON escapes decode as JSON", error);
	// A game configuration as TrenchBroom writes them, which is not strict
	// JSON: comments, bare names, a comma before a closing bracket, an
	// exponent and "\**", an escaped '*' and then any run.
	LevelFilterTagSet stock;
	expect(parseLevelFilterTagText(tests::viewFilterTrenchBroomConfig(), QStringLiteral("Example.cfg"), &stock, &error)
			&& stock.ids() == QStringList {QStringLiteral("tag:trigger"), QStringLiteral("tag:liquid"), QStringLiteral("tag:hint"), QStringLiteral("tag:murk")}
			&& stock.warnings.isEmpty(),
		"a TrenchBroom game configuration reads as it is", error + QLatin1Char(' ') + stock.ids().join(QLatin1Char(',')));
	if (stock.tags.size() == 4) {
		expect(stock.tags.at(1).patterns == QStringList {QStringLiteral("\\**")}, "the backslash stays, as TrenchBroom keeps it", stock.tags.at(1).patterns.join(QLatin1Char(',')));
		expect(levelFilterTagFlagBits(stock.tags.at(3), quake3) == 0x2, "its faceattribs name the bits past a comment");
		LevelMapDocument quake;
		if (load(tests::viewFilterQuakeLiquidFixture(), QStringLiteral("idTech2"), &quake) && expect(quake.brushes.size() == 3, "Quake liquid fixture shape")) {
			expect(selectors(levelFilterTagMatches(quake, stock.tags.at(1))) == brushes({0}) && faceIds(levelFilterTagFaceMatches(quake, stock.tags.at(1))) == QStringList {QStringLiteral("1/1")},
				"\\** takes textures that start with '*', not every texture",
				selectors(levelFilterTagMatches(quake, stock.tags.at(1))).join(QLatin1Char(',')) + QLatin1Char(' ')
					+ faceIds(levelFilterTagFaceMatches(quake, stock.tags.at(1))).join(QLatin1Char(',')));
		}
	}
	LevelFilterTagSet broken;
	expect(!parseLevelFilterTagText("{\n\"tags\": {\n\"brush\": [ \"never closed ]\n}\n}", QStringLiteral("a.cfg"), &broken, &error) && error.endsWith(QLatin1String("line 3.")),
		"an unclosed string names its line", error);
	expect(!parseLevelFilterTagText("{\n\"tags\": /* never closed\n}", QStringLiteral("b.cfg"), &broken, &error) && error.endsWith(QLatin1String("line 2.")),
		"an unclosed comment names its line", error);
	expect(!parseLevelFilterTagText("{\n// fine\n\"tags\": {\n\"brush\": [ } }\n", QStringLiteral("c.cfg"), &broken, &error) && error.endsWith(QLatin1String("line 4.")),
		"a JSON error keeps its line past comments", error);
	expect(broken.tags.isEmpty(), "nothing is read from broken text");
}

void ids()
{
	expect(levelFilterTagId(QStringLiteral("  Fog Volume!! ")) == QLatin1String("tag:fog-volume") && levelFilterTagId(QStringLiteral("***")).isEmpty(), "tag ids");
	expect(isLevelFilterTagId(QStringLiteral("tag:fog-volume")) && !isLevelFilterTagId(QStringLiteral("tag:Fog")) && !isLevelFilterTagId(QStringLiteral("fog")),
		"tag id shape");
	QString tagId;
	expect(levelFilterTagCommandId(QStringLiteral("tag:fog-volume")) == QLatin1String("map.filterTag.fog-volume")
			&& levelFilterTagForCommand(QStringLiteral("map.filter-tag.fog-volume"), &tagId) && tagId == QLatin1String("tag:fog-volume")
			&& !levelFilterTagForCommand(QStringLiteral("map.filterTag.")) && levelFilterTagCommandId(QStringLiteral("hint")).isEmpty(),
		"tag commands");
	QString className;
	expect(levelFilterClassId(QStringLiteral(" Light ")) == QLatin1String("class:light") && levelFilterClassForId(QStringLiteral("class:light"), &className)
			&& className == QLatin1String("light") && !levelFilterClassForId(QStringLiteral("class:worldspawn")) && !levelFilterClassForId(QStringLiteral("class:a b")),
		"class ids");
	// ',' separates the saved and command-line lists, so such a class gets no filter.
	expect(levelFilterClassId(QStringLiteral("odd,name")).isEmpty() && !levelFilterClassForId(QStringLiteral("class:odd,name"))
			&& levelFilterClassCommandId(QStringLiteral("odd,name")).isEmpty() && !levelFilterClassForCommand(QStringLiteral("map.filterClass.odd,name")),
		"a class name with a comma has no filter id or command");
	expect(levelFilterClassCommandId(QStringLiteral("info_player_start")) == QLatin1String("map.filterClass.info_player_start")
			&& levelFilterClassForCommand(QStringLiteral("map.filter-class.light"), &className) && className == QLatin1String("light")
			&& levelFilterClassCommandId(QStringLiteral("worldspawn")).isEmpty(),
		"class commands");
	CommandPaletteEntry entry;
	expect(commandPaletteEntryForCommandId(QStringLiteral("map.filterClass.light"), &entry) && entry.label.contains(QLatin1String("light"))
			&& entry.category == QCoreApplication::translate("VibeStudioSemantics", "Levels") && !entry.summary.isEmpty() && shellCommandIdExists(QStringLiteral("map.filterClass.light")),
		"class commands are in the shared registry");
	expect(commandPaletteEntryForCommandId(QStringLiteral("map.filterTag.fog-volume"), &entry) && !entry.summary.isEmpty()
			&& shellCommandIdExists(QStringLiteral("map.filter-tag.fog-volume")),
		"tag commands are in the shared registry");
	for (const char* id : {"map.filterSkip", "map.filterFog", "map.filterHint"}) {
		expect(commandPaletteEntryForCommandId(QString::fromLatin1(id), &entry) && !entry.label.isEmpty(), "kind palette entries", QString::fromLatin1(id));
	}
	expect(levelViewFiltersWithoutClasses({QStringLiteral("world"), QStringLiteral("class:light"), QStringLiteral("tag:fog"), QStringLiteral("world")})
			== QStringList {QStringLiteral("world"), QStringLiteral("tag:fog")},
		"class filters are not saved");
}

void matching()
{
	LevelMapDocument document = quake3Document();
	if (document.brushes.size() != 10) {
		return;
	}
	const QByteArray before = serializeLevelMap(document).bytes;
	LevelFilterTagSet set;
	QString error;
	parseLevelFilterTags(QJsonDocument::fromJson(tests::viewFilterTagLayout()).object(), QStringLiteral("tags.json"), &set, &error);
	const auto tag = [&](const char* id) { return *set.find(QString::fromLatin1(id)); };
	expect(selectors(levelFilterTagMatches(document, tag("tag:water"))) == QStringList {QStringLiteral("brush:4"), QStringLiteral("patch:0")},
		"a texture tag hides brushes textured all over and patches", selectors(levelFilterTagMatches(document, tag("tag:water"))).join(QLatin1Char(',')));
	expect(faceIds(levelFilterTagFaceMatches(document, tag("tag:water"))) == QStringList {QStringLiteral("5/1")}, "and faces elsewhere one by one",
		faceIds(levelFilterTagFaceMatches(document, tag("tag:water"))).join(QLatin1Char(',')));
	expect(selectors(levelFilterTagMatches(document, tag("tag:skip-faces"))) == brushes({0})
			&& faceIds(levelFilterTagFaceMatches(document, tag("tag:skip-faces"))).size() == 5,
		"a pattern with a folder matches the path");
	expect(selectors(levelFilterTagMatches(document, tag("tag:triggers"))) == QStringList {QStringLiteral("entity:1"), QStringLiteral("entity:2")},
		"a class tag hides brush entities of matching classes");
	expect(selectors(levelFilterTagMatches(document, tag("tag:detail"))) == brushes({6}), "a content flag by the game's name",
		selectors(levelFilterTagMatches(document, tag("tag:detail"))).join(QLatin1Char(',')));
	expect(selectors(levelFilterTagMatches(document, tag("tag:mist"))) == brushes({3}), "a content flag by number");
	expect(levelFilterTagMatches(document, tag("tag:fog-volume")).isEmpty(), "surfaceparm tags need the package's shaders");
	LevelViewFilterMaterials materials;
	LevelViewFilterShader fog;
	fog.fog = true;
	fog.surfaceParms = {QStringLiteral("fog"), QStringLiteral("trans")};
	materials.shaders.insert(QStringLiteral("textures/sfx/fog_red"), fog);
	expect(selectors(levelFilterTagMatches(document, tag("tag:fog-volume"), materials)) == brushes({2}), "surfaceparm tags read the shaders");
	QStringList unresolved;
	LevelFilterTag unknown = tag("tag:detail");
	unknown.flagNames = {QStringLiteral("nosuchflag")};
	expect(levelFilterTagFlagBits(unknown, document, &unresolved) == 0 && unresolved == QStringList {QStringLiteral("nosuchflag")}, "unknown flag names are reported");

	// Class filters.
	const EntityDefinitionCatalogue catalogue = builtinEntityDefinitions(BuiltinEntityGame::Quake3);
	const QVector<LevelFilterClass> classes = levelFilterClasses(document, catalogue);
	QHash<QString, LevelFilterClass> byId;
	for (const LevelFilterClass& entry : classes) {
		byId.insert(entry.id, entry);
	}
	expect(byId.value(QStringLiteral("class:light")).count == 2 && byId.value(QStringLiteral("class:light")).defined
			&& byId.value(QStringLiteral("class:light")).group == QLatin1String("light"),
		"a defined class with its count, whatever the case");
	expect(byId.value(QStringLiteral("class:my_custom_thing")).count == 1 && !byId.value(QStringLiteral("class:my_custom_thing")).defined
			&& byId.value(QStringLiteral("class:my_custom_thing")).group == QLatin1String("my"),
		"a class only the map uses");
	expect(byId.value(QStringLiteral("class:trigger_multiple")).kind == EntityClassKind::Brush && !byId.contains(QStringLiteral("class:worldspawn")),
		"brush classes, never worldspawn");
	expect(classes.size() > 20 && std::is_sorted(classes.cbegin(), classes.cend(), [](const LevelFilterClass& a, const LevelFilterClass& b) {
		return a.className.compare(b.className, Qt::CaseInsensitive) < 0;
	}), "classes sorted ignoring case", QString::number(classes.size()));
	expect(selectors(levelFilterClassMatches(document, QStringLiteral("light"))) == QStringList {QStringLiteral("entity:3"), QStringLiteral("entity:4")},
		"a class filter takes every entity of the class");
	LevelMapDocument odd = document;
	LevelMapEntity oddEntity;
	oddEntity.id = 99;
	oddEntity.className = QStringLiteral("odd,name");
	odd.entities.push_back(oddEntity);
	const QVector<LevelFilterClass> oddClasses = levelFilterClasses(odd, catalogue);
	expect(oddClasses.size() == classes.size()
			&& std::none_of(oddClasses.cbegin(), oddClasses.cend(), [](const LevelFilterClass& entry) { return entry.className.contains(QLatin1Char(',')); }),
		"a class whose name holds a comma is not listed");

	// Faces hidden one by one under different kinds or tags add up: a brush
	// whose faces all go is hidden whole, plan views included.
	LevelFilterTagSet hints;
	parseLevelFilterTags(QJsonDocument::fromJson(R"([ { "name": "Hints", "match": "texture", "pattern": "common/hint" } ])").array(), QStringLiteral("h.json"), &hints, &error);
	const LevelViewFilterHidden skipAndHints = levelViewFilterHidden(document, {QStringLiteral("skip"), QStringLiteral("tag:hints")}, {}, hints);
	const QStringList skipAndHintsObjects = selectors(skipAndHints.objects);
	expect(skipAndHintsObjects.contains(QStringLiteral("brush:0")) && skipAndHintsObjects.contains(QStringLiteral("brush:1"))
			&& std::none_of(skipAndHints.faces.cbegin(), skipAndHints.faces.cend(), [](const LevelSurfaceFace& face) { return face.brushId == 1; }),
		"skip sides and a hint tag together hide the hint brush whole",
		skipAndHintsObjects.join(QLatin1Char(',')) + QLatin1Char(' ') + faceIds(skipAndHints.faces).join(QLatin1Char(',')));
	const LevelViewFilterHidden skipOnly = levelViewFilterHidden(document, {QStringLiteral("skip")}, {}, hints);
	expect(!selectors(skipOnly.objects).contains(QStringLiteral("brush:1")) && faceIds(skipOnly.faces).size() == 5,
		"Skip alone leaves the hint face, so the brush keeps its outline", faceIds(skipOnly.faces).join(QLatin1Char(',')));

	// Everything together: faces of brushes hidden whole drop out.
	const LevelViewFilterHidden hidden = levelViewFilterHidden(document,
		{QStringLiteral("hint"), QStringLiteral("skip"), QStringLiteral("tag:water"), QStringLiteral("class:light"), QStringLiteral("tag:unknown")}, {}, set);
	const QStringList hiddenObjects = selectors(hidden.objects);
	expect(QSet<QString>(hiddenObjects.cbegin(), hiddenObjects.cend())
			== QSet<QString> {QStringLiteral("brush:1"), QStringLiteral("brush:0"), QStringLiteral("brush:4"), QStringLiteral("patch:0"), QStringLiteral("entity:3"),
				QStringLiteral("entity:4")},
		"kinds, tags and classes combine", selectors(hidden.objects).join(QLatin1Char(',')));
	expect(faceIds(hidden.faces) == QStringList {QStringLiteral("5/1")}, "only faces of brushes still drawn", faceIds(hidden.faces).join(QLatin1Char(',')));
	expect(levelFilterTagsInverted(set, {QStringLiteral("world"), QStringLiteral("tag:water")}).contains(QStringLiteral("tag:fog-volume"))
			&& !levelFilterTagsInverted(set, {QStringLiteral("world"), QStringLiteral("tag:water")}).contains(QStringLiteral("tag:water")),
		"Invert flips tags");
	// Doom maps take neither.
	LevelMapDocument doom;
	doom.format = LevelMapFormat::DoomWad;
	expect(!levelFilterTagsApply(doom) && levelFilterClassMatches(doom, QStringLiteral("light")).isEmpty() && levelFilterClasses(doom, catalogue).isEmpty(),
		"Doom maps have no tags or classes");
	expect(serializeLevelMap(document).bytes == before && document.undoStack.isEmpty(), "tags and classes never change the map");
}

void project(const QString& root)
{
	ProjectManifest manifest = defaultProjectManifest(root);
	manifest.extraFields.insert(levelFilterTagsManifestKey(), QJsonDocument::fromJson(tests::viewFilterTagLayout()).object());
	LevelFilterTagSet inline_ = levelFilterTagsForProject(manifest);
	expect(inline_.tags.size() == 6 && inline_.tags.first().source == projectManifestPath(root), "a manifest holds tags", QString::number(inline_.tags.size()));
	QDir().mkpath(QDir(root).filePath(QStringLiteral("config")));
	QFile file(QDir(root).filePath(QStringLiteral("config/tags.json")));
	expect(file.open(QIODevice::WriteOnly) && file.write(R"([ { "name": "Doors", "match": "classname", "pattern": "func_door*" } ])") > 0, "tag file");
	file.close();
	manifest.extraFields.insert(levelFilterTagsManifestKey(), QStringLiteral("config/tags.json"));
	const LevelFilterTagSet named = levelFilterTagsForProject(manifest);
	expect(named.ids() == QStringList {QStringLiteral("tag:doors")} && named.warnings.isEmpty(), "a manifest names a tag file beside the project");
	manifest.extraFields.insert(levelFilterTagsManifestKey(), QStringLiteral("config/missing.json"));
	expect(levelFilterTagsForProject(manifest).isEmpty() && levelFilterTagsForProject(manifest).warnings.size() == 1, "a missing file is a warning");
	manifest.extraFields.insert(levelFilterTagsManifestKey(), QStringLiteral("config/tags.json"));
	QString error;
	expect(saveProjectManifest(manifest, &error), "save the project", error);
}

void cli(const QString& executable, const QString& root)
{
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "CLI temporary folder")) {
		return;
	}
	const QString path = temp.filePath(QStringLiteral("tags.map"));
	const QString tags = temp.filePath(QStringLiteral("tags.json"));
	QFile mapFile(path);
	QFile tagFile(tags);
	const QByteArray mapBytes = tests::viewFilterTagsFixture();
	if (!expect(mapFile.open(QIODevice::WriteOnly) && mapFile.write(mapBytes) == mapBytes.size() && tagFile.open(QIODevice::WriteOnly)
				&& tagFile.write(tests::viewFilterTagLayout()) > 0,
			"CLI fixtures")) {
		return;
	}
	mapFile.close();
	tagFile.close();
	const QString settingsPath = temp.filePath(QStringLiteral("settings.ini"));
	const auto run = [&](const QString& command, const QStringList& args, int code = 0) {
		QProcess process;
		process.setWorkingDirectory(temp.path());
		process.start(executable, QStringList {QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--settings-file"), settingsPath,
									  QStringLiteral("map"), command} + args);
		const bool done = process.waitForStarted() && process.waitForFinished(30000);
		const QByteArray out = process.readAllStandardOutput();
		expect(done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == code, "CLI exit status",
			command + QLatin1Char(' ') + args.join(QLatin1Char(' ')) + QLatin1String(" -> ") + QString::fromUtf8(out + process.readAllStandardError()));
		if (!done) {
			process.kill();
			process.waitForFinished();
		}
		return QJsonDocument::fromJson(out).object();
	};
	const auto strings = [](const QJsonValue& value) {
		QStringList list;
		for (const QJsonValue& item : value.toArray()) {
			list << item.toString();
		}
		return list;
	};
	const QJsonObject report = run(QStringLiteral("filters"), {path, QStringLiteral("--tags"), tags, QStringLiteral("--off"), QStringLiteral("tag:water,skip"),
															   QStringLiteral("--class"), QStringLiteral("light"), QStringLiteral("--objects")});
	QJsonObject water;
	for (const QJsonValue& value : report.value(QStringLiteral("tags")).toArray()) {
		if (value.toObject().value(QStringLiteral("id")).toString() == QLatin1String("tag:water")) {
			water = value.toObject();
		}
	}
	expect(report.value(QStringLiteral("tags")).toArray().size() == 6 && report.value(QStringLiteral("tagWarnings")).toArray().size() == 5,
		"map filters reports the tags and their warnings");
	expect(water.value(QStringLiteral("count")).toInt() == 2 && water.value(QStringLiteral("faceCount")).toInt() == 1 && !water.value(QStringLiteral("shown")).toBool()
			&& water.value(QStringLiteral("command")).toString() == QLatin1String("map.filterTag.water") && strings(water.value(QStringLiteral("faces"))).size() == 1,
		"a tag's counts, state and command");
	LevelMapDocument document = quake3Document();
	LevelFilterTagSet set;
	QString error;
	parseLevelFilterTags(QJsonDocument::fromJson(tests::viewFilterTagLayout()).object(), tags, &set, &error);
	const LevelViewFilterHidden hidden = levelViewFilterHidden(document, {QStringLiteral("tag:water"), QStringLiteral("skip"), QStringLiteral("class:light")}, {}, set);
	expect(strings(report.value(QStringLiteral("filteredObjects"))) == selectors(hidden.objects)
			&& report.value(QStringLiteral("filteredFaceCount")).toInt() == hidden.faces.size()
			&& report.value(QStringLiteral("classes")).toArray().size() == 1 && report.value(QStringLiteral("mapWritten")) == false,
		"the CLI hides what the service hides", strings(report.value(QStringLiteral("filteredObjects"))).join(QLatin1Char(',')));
	const QJsonObject listed = run(QStringLiteral("filters"), {path, QStringLiteral("--classes")});
	expect(listed.value(QStringLiteral("classes")).toArray().size() == listed.value(QStringLiteral("classCount")).toInt()
			&& listed.value(QStringLiteral("classCount")).toInt() > 20,
		"--classes lists the game's classes and the map's");
	const QJsonObject fromProject = run(QStringLiteral("filters"), {path, QStringLiteral("--project"), root, QStringLiteral("--off"), QStringLiteral("tag:doors")});
	expect(fromProject.value(QStringLiteral("tags")).toArray().size() == 1 && strings(fromProject.value(QStringLiteral("filteredObjects"))) == QStringList {QStringLiteral("entity:6")},
		"--project reads the manifest's tags", strings(fromProject.value(QStringLiteral("filteredObjects"))).join(QLatin1Char(',')));
	const QJsonObject inverted = run(QStringLiteral("filters"), {path, QStringLiteral("--tags"), tags, QStringLiteral("--off"), QStringLiteral("tag:water"), QStringLiteral("--invert")});
	expect(!strings(inverted.value(QStringLiteral("off"))).contains(QStringLiteral("tag:water")) && strings(inverted.value(QStringLiteral("off"))).contains(QStringLiteral("tag:detail")),
		"--invert flips tags");
	run(QStringLiteral("filters"), {path, QStringLiteral("--off"), QStringLiteral("tag:water")}, 2);
	run(QStringLiteral("filters"), {path, QStringLiteral("--class"), QStringLiteral("worldspawn")}, 2);
	run(QStringLiteral("filters"), {path, QStringLiteral("--tags"), temp.filePath(QStringLiteral("missing.json"))}, 4);
	// A game configuration as TrenchBroom writes them reads as it is.
	const QString config = temp.filePath(QStringLiteral("Example.cfg"));
	const QString quakePath = temp.filePath(QStringLiteral("liquid.map"));
	QFile configFile(config);
	QFile quakeFile(quakePath);
	const QByteArray quakeBytes = tests::viewFilterQuakeLiquidFixture();
	if (expect(configFile.open(QIODevice::WriteOnly) && configFile.write(tests::viewFilterTrenchBroomConfig()) > 0 && quakeFile.open(QIODevice::WriteOnly)
				&& quakeFile.write(quakeBytes) == quakeBytes.size(),
			"configuration fixtures")) {
		configFile.close();
		quakeFile.close();
		const QJsonObject liquid = run(QStringLiteral("filters"), {quakePath, QStringLiteral("--tags"), config, QStringLiteral("--off"), QStringLiteral("tag:liquid")});
		expect(liquid.value(QStringLiteral("tags")).toArray().size() == 4 && strings(liquid.value(QStringLiteral("filteredObjects"))) == QStringList {QStringLiteral("brush:0")}
				&& strings(liquid.value(QStringLiteral("filteredFaces"))) == QStringList {QStringLiteral("face:1:2")},
			"map filters --tags reads a TrenchBroom configuration and its \\** pattern",
			strings(liquid.value(QStringLiteral("filteredObjects"))).join(QLatin1Char(',')) + QLatin1Char(' ')
				+ strings(liquid.value(QStringLiteral("filteredFaces"))).join(QLatin1Char(',')));
		QFile brokenFile(temp.filePath(QStringLiteral("broken.cfg")));
		if (brokenFile.open(QIODevice::WriteOnly)) {
			brokenFile.write("{\n\"tags\": /* never closed\n}");
			brokenFile.close();
		}
		run(QStringLiteral("filters"), {quakePath, QStringLiteral("--tags"), brokenFile.fileName()}, 4);
	}
	const QJsonObject visibility = run(QStringLiteral("visibility"), {path, QStringLiteral("--mode"), QStringLiteral("inspect"), QStringLiteral("--class"),
																	 QStringLiteral("light"), QStringLiteral("--class"), QStringLiteral("trigger_hurt")});
	expect(strings(visibility.value(QStringLiteral("classFiltered"))) == QStringList {QStringLiteral("entity:2"), QStringLiteral("entity:3"), QStringLiteral("entity:4")}
			&& visibility.value(QStringLiteral("classFilters")).toArray().size() == 2 && strings(visibility.value(QStringLiteral("viewHidden"))).size() == 3
			&& strings(visibility.value(QStringLiteral("hidden"))).isEmpty(),
		"map visibility --class reports what class filters hide, apart from session hiding",
		strings(visibility.value(QStringLiteral("classFiltered"))).join(QLatin1Char(',')));
	run(QStringLiteral("visibility"), {path, QStringLiteral("--mode"), QStringLiteral("inspect"), QStringLiteral("--class"), QStringLiteral("bad name")}, 2);
	QFile check(path);
	expect(check.open(QIODevice::ReadOnly) && check.readAll() == mapBytes, "no CLI route writes the map");
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	kinds();
	parsing();
	ids();
	matching();
	QTemporaryDir projectRoot;
	if (expect(projectRoot.isValid(), "project folder")) {
		project(projectRoot.path());
	}
	const QStringList arguments = QCoreApplication::arguments();
	if (arguments.size() > 1 && projectRoot.isValid()) {
		cli(arguments.at(1), projectRoot.path());
	}
	if (ok) {
		std::cout << "Filter tags: kinds, tags, classes, ids, project data and CLI passed." << std::endl;
	}
	return ok ? 0 : 1;
}
