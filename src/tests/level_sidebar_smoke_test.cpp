// Checks the Levels sidebar arrangement data: every profile places every tab
// exactly once, families name their tools their own way, normalization repairs
// a saved arrangement, and JSON round trips.

#include "core/editor_profiles.h"
#include "core/level_sidebar.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QSet>

#include <iostream>

using namespace vibestudio;

namespace {

bool expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << "\n";
	}
	return condition;
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true;

	const QVector<LevelSidebarTab> tabs = levelSidebarTabs();
	ok &= expect(tabs.size() == 14, "fourteen sidebar tabs");
	QSet<QString> ids;
	for (const LevelSidebarTab& tab : tabs) {
		ok &= expect(!tab.id.isEmpty() && !tab.iconName.isEmpty() && !tab.title.isEmpty() && !tab.description.isEmpty(), "every tab is described");
		ok &= expect(tab.commandId == QStringLiteral("map.sidebar.") + tab.id, "every tab has a show command");
		ids.insert(tab.id);
	}
	ok &= expect(ids.size() == tabs.size(), "tab ids are unique");
	for (const QString& wanted : {QStringLiteral("outliner"), QStringLiteral("entities"), QStringLiteral("textures"), QStringLiteral("models"),
			 QStringLiteral("sounds"), QStringLiteral("shapes"), QStringLiteral("tools"), QStringLiteral("inspector")}) {
		ok &= expect(ids.contains(wanted), "the browsers and the inspector have tabs");
	}

	// Every profile's arrangement is complete and valid.
	for (const QString& profile : editorProfileIds()) {
		const LevelSidebarArrangement arrangement = levelSidebarArrangementForProfile(profile);
		QString error;
		ok &= expect(validateLevelSidebarArrangement(arrangement, &error), "each profile places every tab exactly once");
		if (!error.isEmpty()) {
			std::cerr << "  " << profile.toStdString() << ": " << error.toStdString() << "\n";
		}
		ok &= expect(!arrangement.trailing.isEmpty(), "each profile has a trailing sidebar");
		ok &= expect(levelSidebarFamilies().contains(levelSidebarFamilyForProfile(profile)), "each profile belongs to a family");
	}

	// Families keep their own places and names.
	ok &= expect(levelSidebarFamilyForProfile(QStringLiteral("netradiant-custom")) == QStringLiteral("radiant"), "NetRadiant Custom is a Radiant");
	ok &= expect(levelSidebarFamilyForProfile(QStringLiteral("doomedit")) == QStringLiteral("radiant"), "DoomEdit is Doom 3's Radiant");
	ok &= expect(levelSidebarFamilyForProfile(QStringLiteral("eureka")) == QStringLiteral("doom-builder"), "Eureka edits Doom maps");
	ok &= expect(levelSidebarFamilyForProfile(QStringLiteral("TrenchBroom")) == QStringLiteral("trenchbroom"), "profile ids are normalized");
	ok &= expect(levelSidebarFamilyForProfile(QStringLiteral("no-such-editor")) == QStringLiteral("studio"), "unknown profiles use the studio family");
	const LevelSidebarArrangement studio = levelSidebarArrangementForProfile(defaultEditorProfileId());
	ok &= expect(studio.leading.first() == QStringLiteral("outliner") && studio.trailing.first() == QStringLiteral("inspector"),
		"the studio browses on one side and inspects on the other");
	const LevelSidebarArrangement trenchbroom = levelSidebarArrangementForProfile(QStringLiteral("trenchbroom"));
	ok &= expect(trenchbroom.leading.isEmpty() && trenchbroom.trailing.mid(0, 3) == QStringList({QStringLiteral("map"), QStringLiteral("inspector"), QStringLiteral("surfaces")}),
		"TrenchBroom keeps Map, Entity and Face in one inspector");
	ok &= expect(levelSidebarTabTitle(QStringLiteral("surfaces"), QStringLiteral("trenchbroom")) == QStringLiteral("Face"), "TrenchBroom calls surfaces Face");
	ok &= expect(levelSidebarTabTitle(QStringLiteral("outliner"), QStringLiteral("gtkradiant-1-6")) == QStringLiteral("Entity List"), "Radiant has an Entity List");
	ok &= expect(levelSidebarTabTitle(QStringLiteral("entities"), QStringLiteral("ultimate-doom-builder")) == QStringLiteral("Things"), "Doom Builder places things");
	ok &= expect(levelSidebarTabTitle(QStringLiteral("entities"), defaultEditorProfileId(), true) == QStringLiteral("Things"), "a Doom map calls entities things");
	ok &= expect(levelSidebarTabTitle(QStringLiteral("entities"), defaultEditorProfileId(), false) == QStringLiteral("Entities"), "a Quake map has entities");
	ok &= expect(levelSidebarTabTitle(QStringLiteral("inspector"), QStringLiteral("blender")) == QStringLiteral("Item"), "Blender calls the inspector Item");
	ok &= expect(levelSidebarTabTitle(QStringLiteral("shapes"), QStringLiteral("hammer")) == QStringLiteral("Primitives"), "Hammer chooses primitives");

	// Normalization repairs saved arrangements.
	LevelSidebarArrangement broken;
	broken.leading = {QStringLiteral("textures"), QStringLiteral("bogus"), QStringLiteral("textures"), QStringLiteral("inspector")};
	broken.trailing = {QStringLiteral("history")};
	broken.groupStarts = {QStringLiteral("history"), QStringLiteral("bogus")};
	broken.leadingCurrent = QStringLiteral("history");
	const LevelSidebarArrangement repaired = normalizedLevelSidebarArrangement(broken, defaultEditorProfileId());
	ok &= expect(validateLevelSidebarArrangement(repaired), "normalization yields a valid arrangement");
	ok &= expect(repaired.leading.mid(0, 2) == QStringList({QStringLiteral("textures"), QStringLiteral("inspector")}), "kept tabs keep their side and order");
	ok &= expect(repaired.leading.contains(QStringLiteral("outliner")) && repaired.trailing.contains(QStringLiteral("map")),
		"missing tabs join the profile's side for them");
	ok &= expect(repaired.groupStarts == QStringList({QStringLiteral("history")}), "unknown group starts go");
	ok &= expect(repaired.leadingCurrent == QStringLiteral("textures") && repaired.trailingCurrent == QStringLiteral("history"),
		"a current tab on the wrong side falls back to the first");
	QString error;
	ok &= expect(!validateLevelSidebarArrangement(broken, &error) && !error.isEmpty(), "a broken arrangement is refused with a reason");

	// JSON round trip.
	const QJsonObject json = levelSidebarArrangementJson(studio, defaultEditorProfileId());
	ok &= expect(json.value(QStringLiteral("family")).toString() == QStringLiteral("studio"), "JSON names the family");
	ok &= expect(json.value(QStringLiteral("leading")).toArray().first().toObject().value(QStringLiteral("title")).toString() == QStringLiteral("Outliner"),
		"JSON carries each tab's title");
	LevelSidebarArrangement read;
	ok &= expect(levelSidebarArrangementFromJson(json, &read, &error), "JSON reads back");
	ok &= expect(read.leading == studio.leading && read.trailing == studio.trailing && read.leadingCurrent == studio.leadingCurrent
			&& QSet<QString>(read.groupStarts.cbegin(), read.groupStarts.cend()) == QSet<QString>(studio.groupStarts.cbegin(), studio.groupStarts.cend()),
		"JSON round trips");
	QJsonObject missing = json;
	missing.remove(QStringLiteral("trailing"));
	ok &= expect(!levelSidebarArrangementFromJson(missing, &read, &error), "JSON without a side is refused");
	const QStringList lines = levelSidebarArrangementLines(studio, defaultEditorProfileId());
	ok &= expect(lines.size() == 3 && lines.at(1).contains(QStringLiteral("Outliner (open)")), "text lines name the open tabs");

	std::cout << (ok ? "level sidebar smoke passed" : "level sidebar smoke failed") << "\n";
	return ok ? 0 : 1;
}
