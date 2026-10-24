// Checks the entity colour service (core/level_entity_colour.h): parsing and
// writing _color in 0-1 and 0-255 forms, NetRadiant Custom's Set Entity Colour
// (selected entities and the owners of selected brushes, never worldspawn
// through a world brush) and Normalise Entity Colour (the primary's colour to
// every target, or each its own). Every edit is one undo step back to the
// exact bytes; locked, hidden and world-only selections are refused or left
// out; linked copies take the colour in the same step; the open group is
// respected. K sets the colour in the NetRadiant Custom and NetRadiant
// profiles only, and Normalise has no key, as upstream. With the vibestudio
// binary as its argument it drives `map entity-colour` too.

#include "core/editor_profiles.h"
#include "core/level_document.h"
#include "core/level_entity_colour.h"
#include "core/level_linked_groups.h"
#include "core/level_map.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_visibility.h"
#include "tests/level_entity_ownership_test_helpers.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>

using namespace vibestudio;

namespace {

bool ok = true;

bool expect(bool condition, const char* label, const QString& detail = QString())
{
	if (!condition) {
		ok = false;
		std::cerr << "FAIL: " << label;
		if (!detail.isEmpty()) {
			std::cerr << " (" << detail.toStdString() << ")";
		}
		std::cerr << std::endl;
	}
	return condition;
}

LevelMapSelectionRef brush(int id) { return {LevelMapSelectionKind::QuakeBrush, id}; }
LevelMapSelectionRef entity(int id) { return {LevelMapSelectionKind::Entity, id}; }

QByteArray bytesOf(const LevelMapDocument& document)
{
	return serializeLevelMap(document).bytes;
}

QString keyOf(const LevelMapDocument& document, int entityId, const QString& key = QStringLiteral("_color"))
{
	for (const LevelMapEntity& value : document.entities) {
		if (value.id == entityId) {
			for (const LevelMapProperty& property : value.properties) {
				if (property.key.compare(key, Qt::CaseInsensitive) == 0) {
					return property.value;
				}
			}
		}
	}
	return QString();
}

bool hasKey(const LevelMapDocument& document, int entityId, const QString& key = QStringLiteral("_color"))
{
	for (const LevelMapEntity& value : document.entities) {
		if (value.id == entityId) {
			for (const LevelMapProperty& property : value.properties) {
				if (property.key.compare(key, Qt::CaseInsensitive) == 0) {
					return true;
				}
			}
		}
	}
	return false;
}

void select(LevelMapDocument* document, const QVector<LevelMapSelectionRef>& refs)
{
	QString error;
	expect(setLevelMapSelection(document, refs, &error), "select fixture objects", error);
}

// The ownership fixture (worldspawn with floor and w1, a func_door holding d1
// to d3, a func_wall holding c1, and a light), reloaded so every object has
// its source lines, with the light's _color set to `lightColour` and saved
// into the text, so the history starts empty.
struct Fixture {
	LevelMapDocument map;
	int world = -1, door = -1, wall = -1, light = -1;
	int floor = -1, w1 = -1, d1 = -1, d2 = -1, c1 = -1;
};

Fixture fixture(const QString& lightColour = QStringLiteral("255 0 0"))
{
	Fixture result;
	LevelMapLoadRequest request;
	request.path = QStringLiteral("colour.map");
	request.engineHint = QStringLiteral("idtech2");
	QString error;
	LevelMapDocument seeded;
	expect(loadLevelMapBytes(request, tests::entityOwnershipFixtureBytes(), &seeded, &error), "load the fixture", error);
	const int light = tests::entityOwnershipEntity(seeded, QStringLiteral("light"));
	if (!lightColour.isEmpty()) {
		expect(setLevelMapEntityProperty(&seeded, light, QStringLiteral("_color"), lightColour, &error), "seed the light colour", error);
	}
	expect(loadLevelMapBytes(request, bytesOf(seeded), &result.map, &error), "reload the fixture", error);
	result.world = tests::entityOwnershipEntity(result.map, QStringLiteral("worldspawn"));
	result.door = tests::entityOwnershipEntity(result.map, QStringLiteral("func_door"));
	result.wall = tests::entityOwnershipEntity(result.map, QStringLiteral("func_wall"));
	result.light = tests::entityOwnershipEntity(result.map, QStringLiteral("light"));
	result.floor = tests::entityOwnershipBrush(result.map, QStringLiteral("floor"));
	result.w1 = tests::entityOwnershipBrush(result.map, QStringLiteral("w1"));
	result.d1 = tests::entityOwnershipBrush(result.map, QStringLiteral("d1"));
	result.d2 = tests::entityOwnershipBrush(result.map, QStringLiteral("d2"));
	result.c1 = tests::entityOwnershipBrush(result.map, QStringLiteral("c1"));
	expect(result.world >= 0 && result.door >= 0 && result.wall >= 0 && result.light >= 0 && result.d1 >= 0 && result.c1 >= 0 && result.w1 >= 0
			&& result.map.undoStack.isEmpty(),
		"fixture shape");
	return result;
}

bool near(double left, double right)
{
	return std::abs(left - right) < 1e-6;
}

void parsing()
{
	LevelEntityColour colour;
	expect(parseLevelEntityColour(QStringLiteral("1 0.5 0"), &colour) && colour.fractions && colour.rest.isEmpty() && colour.color() == QColor(255, 128, 0),
		"fractions parse and show as 8-bit");
	expect(parseLevelEntityColour(QStringLiteral("  255\t128 0 300 "), &colour) && !colour.fractions && colour.rest == QStringLiteral("300")
			&& near(colour.unit()[1], 128.0 / 255.0),
		"0-255 parses with its brightness kept");
	expect(!parseLevelEntityColour(QStringLiteral("1 0.5"), nullptr) && !parseLevelEntityColour(QStringLiteral("red green blue"), nullptr)
			&& !parseLevelEntityColour(QStringLiteral("-1 0 0"), nullptr) && !parseLevelEntityColour(QStringLiteral("inf 0 0"), nullptr),
		"short, wordy, negative and infinite values are not colours");
	expect(parseLevelEntityColour(QStringLiteral("1 1 1"), &colour) && colour.fractions && parseLevelEntityColour(QStringLiteral("0 0 0"), &colour) && colour.fractions,
		"white and black read as fractions");

	expect(formatLevelEntityColour({{1.0, 0.5, 0.0}, true, {}}) == QStringLiteral("1 0.5 0"), "fractions lose trailing zeros");
	expect(formatLevelEntityColourLike({1.0, 128.0 / 255.0, 0.0}) == QStringLiteral("1 0.501961 0"), "a new key is written in fractions to six decimals");
	parseLevelEntityColour(QStringLiteral("255 0 0 300"), &colour);
	expect(formatLevelEntityColourLike({0.0, 0.5, 1.0}, &colour) == QStringLiteral("0 128 255 300"), "a 0-255 key stays 0-255 and keeps its brightness");
	expect(formatLevelEntityColourLike({2.0, -1.0, 0.25}) == QStringLiteral("1 0 0.25"), "out-of-range channels are clamped");

	parseLevelEntityColour(QStringLiteral("255 128 0"), &colour);
	expect(formatLevelEntityColour(normalisedLevelEntityColour(colour)) == QStringLiteral("1 0.501961 0"), "0-255 normalises to fractions, brightest at 1");
	parseLevelEntityColour(QStringLiteral("0.5 0.25 0 200"), &colour);
	expect(formatLevelEntityColour(normalisedLevelEntityColour(colour)) == QStringLiteral("1 0.5 0 200"), "fractions normalise and keep the brightness");
	parseLevelEntityColour(QStringLiteral("0 0 0"), &colour);
	expect(formatLevelEntityColour(normalisedLevelEntityColour(colour)) == QStringLiteral("1 1 1"), "black normalises to white, as NetRadiant Custom");

	expect(isLevelEntityColourKeyName(QStringLiteral("_COLOR")) && isLevelEntityColourKeyName(QStringLiteral(" color ")) && !isLevelEntityColourKeyName(QStringLiteral("_light")),
		"colour key names");
	expect(levelEntityColourKey() == QStringLiteral("_color"), "the commands write _color");
}

void targets()
{
	Fixture f = fixture();
	select(&f.map, {entity(f.light), brush(f.d1), brush(f.d2), brush(f.w1)});
	LevelEntityColourTargets found = levelMapEntityColourTargets(f.map);
	expect(found.entities == QVector<int> {f.light, f.door} && found.worldPrimitives == 1 && found.hidden == 0,
		"the light and the door once each; the world brush adds nothing");
	expect(found.primaryEntity == f.door, "the primary comes from the last item that reaches a target, past the world brush");
	expect(levelMapSelectionHasEntityColourTargets(f.map), "the cheap check sees the targets");
	select(&f.map, {brush(f.w1), brush(f.floor)});
	found = levelMapEntityColourTargets(f.map);
	expect(found.entities.isEmpty() && found.worldPrimitives == 2 && found.primaryEntity == -1 && !levelMapSelectionHasEntityColourTargets(f.map)
			&& !levelMapSelectionEntityColourNormalisable(f.map),
		"world brushes alone have no target");
	select(&f.map, {entity(f.world)});
	expect(levelMapEntityColourTargets(f.map).entities == QVector<int> {f.world}, "worldspawn selected itself is a target");

	LevelEntityColour colour;
	int source = -1;
	select(&f.map, {brush(f.d1), entity(f.light)});
	expect(levelMapPrimaryEntityColour(f.map, levelEntityColourKey(), &colour, &source) && source == f.light && colour.color() == QColor(255, 0, 0)
			&& levelMapSelectionEntityColourNormalisable(f.map),
		"the primary light's colour seeds the picker");
	select(&f.map, {entity(f.light), brush(f.d1)});
	expect(!levelMapPrimaryEntityColour(f.map, levelEntityColourKey(), &colour, &source) && source == f.door && !levelMapSelectionEntityColourNormalisable(f.map),
		"a primary door without a colour has none to seed or normalise");
}

void setColour()
{
	Fixture f = fixture();
	const QByteArray before = bytesOf(f.map);
	QString error;
	LevelEntityColourReport report;
	LevelEntityColourRequest request;
	request.unit = {0.0, 0.5, 1.0};
	select(&f.map, {entity(f.light), brush(f.d1), brush(f.w1)});
	const auto depth = f.map.undoStack.size();
	expect(setLevelMapSelectionEntityColour(&f.map, request, &report, &error), "set the colour", error);
	expect(keyOf(f.map, f.light) == QStringLiteral("0 128 255") && keyOf(f.map, f.door) == QStringLiteral("0 0.5 1") && !hasKey(f.map, f.world),
		"the light keeps its 0-255 form, the door gets fractions, the world is untouched", keyOf(f.map, f.light) + QStringLiteral(" / ") + keyOf(f.map, f.door));
	expect(report.changed == QVector<int> {f.light, f.door} && report.entities == report.changed && report.worldPrimitives == 1 && report.sourceEntity == f.door
			&& report.value == QStringLiteral("0 0.5 1"),
		"the report names the targets and the primary's value");
	expect(f.map.undoStack.size() == depth + 1 && f.map.undoStack.last().description.contains(QStringLiteral("colour")), "one undo step named for the colour",
		f.map.undoStack.isEmpty() ? QString() : f.map.undoStack.last().description);
	const QByteArray after = bytesOf(f.map);
	expect(undoLevelMapEdit(&f.map, &error) && bytesOf(f.map) == before, "undo restores the exact bytes", error);
	expect(redoLevelMapEdit(&f.map, &error) && bytesOf(f.map) == after, "redo writes the colour again", error);

	// The same colour again changes nothing and adds no step.
	select(&f.map, {entity(f.light), brush(f.d1)});
	const auto again = f.map.undoStack.size();
	expect(setLevelMapSelectionEntityColour(&f.map, request, &report, &error) && report.changed.isEmpty() && f.map.undoStack.size() == again && bytesOf(f.map) == after,
		"an unchanged colour is no step", error);

	// The saved text carries the keys.
	LevelMapDocument reopened;
	LevelMapLoadRequest load;
	load.path = QStringLiteral("colour-saved.map");
	load.engineHint = QStringLiteral("idtech2");
	expect(loadLevelMapBytes(load, after, &reopened, &error) && keyOf(reopened, f.door) == QStringLiteral("0 0.5 1"), "the colour survives a save and reopen", error);

	// World brushes alone are refused without touching the map.
	select(&f.map, {brush(f.w1)});
	const auto refusedDepth = f.map.undoStack.size();
	expect(!setLevelMapSelectionEntityColour(&f.map, request, &report, &error) && error.contains(QStringLiteral("World")) && bytesOf(f.map) == after
			&& f.map.undoStack.size() == refusedDepth,
		"world brushes alone are refused", error);
	request.key = QStringLiteral("  ");
	select(&f.map, {entity(f.light)});
	expect(!setLevelMapSelectionEntityColour(&f.map, request, &report, &error) && bytesOf(f.map) == after, "an empty key is refused", error);

	// Another key can be named, such as color.
	request.key = QStringLiteral("color");
	expect(setLevelMapSelectionEntityColour(&f.map, request, &report, &error) && keyOf(f.map, f.light, QStringLiteral("color")) == QStringLiteral("0 0.5 1")
			&& keyOf(f.map, f.light) == QStringLiteral("0 128 255"),
		"a named key is written beside _color", error);
}

void normalise()
{
	QString error;
	LevelEntityColourReport report;
	// NetRadiant Custom: the primary's colour, normalised, goes to every target.
	Fixture f = fixture(QStringLiteral("255 128 0"));
	expect(setLevelMapEntityProperty(&f.map, f.door, QStringLiteral("_color"), QStringLiteral("0.2 0.1 0 300"), &error), "door colour", error);
	const QByteArray before = bytesOf(f.map);
	const auto depth = f.map.undoStack.size();
	select(&f.map, {brush(f.d1), entity(f.light)});
	expect(normaliseLevelMapSelectionEntityColour(&f.map, LevelEntityColourNormalise::Primary, levelEntityColourKey(), &report, &error), "normalise", error);
	expect(keyOf(f.map, f.light) == QStringLiteral("1 0.501961 0") && keyOf(f.map, f.door) == QStringLiteral("1 0.501961 0 300"),
		"the light's normalised colour goes to both, the door keeping its brightness", keyOf(f.map, f.light) + QStringLiteral(" / ") + keyOf(f.map, f.door));
	expect(report.sourceEntity == f.light && report.changed.size() == 2 && f.map.undoStack.size() == depth + 1, "one step from the primary light");
	expect(undoLevelMapEdit(&f.map, &error) && bytesOf(f.map) == before, "undo restores both colours exactly", error);

	// A primary without a colour, or with one that does not parse, is refused.
	select(&f.map, {entity(f.light), entity(f.wall)});
	expect(!normaliseLevelMapSelectionEntityColour(&f.map, LevelEntityColourNormalise::Primary, levelEntityColourKey(), &report, &error)
			&& error.contains(QStringLiteral("_color")) && bytesOf(f.map) == before,
		"a primary without _color is refused", error);
	expect(setLevelMapEntityProperty(&f.map, f.wall, QStringLiteral("_color"), QStringLiteral("red"), &error), "unreadable colour", error);
	const QByteArray unreadable = bytesOf(f.map);
	expect(!normaliseLevelMapSelectionEntityColour(&f.map, LevelEntityColourNormalise::Primary, levelEntityColourKey(), &report, &error)
			&& error.contains(QStringLiteral("not a colour")) && bytesOf(f.map) == unreadable,
		"an unreadable primary colour is refused", error);
	expect(undoLevelMapEdit(&f.map, &error), "undo the unreadable colour", error);

	// Already normalised: nothing changes.
	Fixture done = fixture(QStringLiteral("1 0.5 0"));
	select(&done.map, {entity(done.light)});
	expect(normaliseLevelMapSelectionEntityColour(&done.map, LevelEntityColourNormalise::Primary, levelEntityColourKey(), &report, &error) && report.changed.isEmpty()
			&& done.map.undoStack.isEmpty(),
		"a normalised colour is no step", error);
	// Black becomes white.
	Fixture black = fixture(QStringLiteral("0 0 0"));
	select(&black.map, {entity(black.light)});
	expect(normaliseLevelMapSelectionEntityColour(&black.map, LevelEntityColourNormalise::Primary, levelEntityColourKey(), &report, &error)
			&& keyOf(black.map, black.light) == QStringLiteral("1 1 1"),
		"black normalises to white", error);

	// Each: every target normalises its own; one without a colour is skipped.
	Fixture each = fixture(QStringLiteral("255 128 0"));
	expect(setLevelMapEntityProperty(&each.map, each.door, QStringLiteral("_color"), QStringLiteral("0 0 0.5"), &error), "door colour", error);
	const QByteArray eachBefore = bytesOf(each.map);
	const auto eachDepth = each.map.undoStack.size();
	select(&each.map, {entity(each.light), brush(each.d1), brush(each.c1)});
	expect(normaliseLevelMapSelectionEntityColour(&each.map, LevelEntityColourNormalise::Each, levelEntityColourKey(), &report, &error), "normalise each", error);
	expect(keyOf(each.map, each.light) == QStringLiteral("1 0.501961 0") && keyOf(each.map, each.door) == QStringLiteral("0 0 1") && !hasKey(each.map, each.wall)
			&& report.skipped == QVector<int> {each.wall} && report.changed.size() == 2 && each.map.undoStack.size() == eachDepth + 1,
		"each entity normalises its own colour in one step", keyOf(each.map, each.door));
	expect(undoLevelMapEdit(&each.map, &error) && bytesOf(each.map) == eachBefore, "undo restores each colour", error);
	select(&each.map, {brush(each.c1)});
	expect(!normaliseLevelMapSelectionEntityColour(&each.map, LevelEntityColourNormalise::Each, levelEntityColourKey(), &report, &error) && bytesOf(each.map) == eachBefore,
		"nothing to normalise is refused", error);
}

void sceneRules()
{
	QString error;
	LevelEntityColourReport report;
	LevelEntityColourRequest request;
	request.unit = {0.0, 1.0, 0.0};

	// A locked door refuses the whole edit.
	Fixture locked = fixture();
	QString layer;
	expect(createLevelSceneNode(&locked.map, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
			&& assignLevelSceneObjects(&locked.map, layer, {QStringLiteral("entity:%1").arg(locked.door)}, &error)
			&& setLevelSceneLocked(&locked.map, layer, true, &error),
		"lock the door", error);
	const QByteArray lockedBefore = bytesOf(locked.map);
	const auto lockedDepth = locked.map.undoStack.size();
	select(&locked.map, {entity(locked.light), brush(locked.d1)});
	expect(levelMapEntityColourBlockedReason(locked.map, levelMapEntityColourTargets(locked.map)).contains(QString::number(locked.door)),
		"the picker is not offered for a locked door", levelMapEntityColourBlockedReason(locked.map, levelMapEntityColourTargets(locked.map)));
	select(&locked.map, {entity(locked.light)});
	expect(levelMapEntityColourBlockedReason(locked.map, levelMapEntityColourTargets(locked.map)).isEmpty(), "the unlocked light alone may be coloured");
	select(&locked.map, {brush(locked.w1)});
	expect(levelMapEntityColourBlockedReason(locked.map, levelMapEntityColourTargets(locked.map)).contains(QStringLiteral("World")), "a world brush alone names why");
	select(&locked.map, {entity(locked.light), brush(locked.d1)});
	expect(!setLevelMapSelectionEntityColour(&locked.map, request, &report, &error) && !error.isEmpty() && bytesOf(locked.map) == lockedBefore
			&& locked.map.undoStack.size() == lockedDepth && !hasKey(locked.map, locked.door) && keyOf(locked.map, locked.light) == QStringLiteral("255 0 0"),
		"a locked door refuses the colour for every target", error);

	// A hidden light is left out and counted.
	Fixture hidden = fixture();
	select(&hidden.map, {entity(hidden.light)});
	LevelVisibilityRequest hide;
	expect(changeLevelMapVisibility(&hidden.map, hide, nullptr, &error), "hide the light", error);
	QString selectError;
	if (expect(setLevelMapSelection(&hidden.map, {entity(hidden.light), brush(hidden.d1)}, &selectError), "select the hidden light with the door", selectError)) {
		expect(setLevelMapSelectionEntityColour(&hidden.map, request, &report, &error) && report.hidden == 1 && report.entities == QVector<int> {hidden.door}
				&& keyOf(hidden.map, hidden.light) == QStringLiteral("255 0 0") && keyOf(hidden.map, hidden.door) == QStringLiteral("0 1 0"),
			"the hidden light keeps its colour; the door takes it", error);
		select(&hidden.map, {entity(hidden.light)});
		expect(!setLevelMapSelectionEntityColour(&hidden.map, request, &report, &error) && error.contains(QStringLiteral("hidden")), "hidden alone is refused with a reason",
			error);
	}

	// Inside an open group the light is coloured; the door outside is not.
	Fixture open = fixture();
	QString group;
	expect(createLevelSceneNode(&open.map, LevelSceneNodeKind::Group, QStringLiteral("Lamp"), {}, &group, &error)
			&& assignLevelSceneObjects(&open.map, group, {QStringLiteral("entity:%1").arg(open.light)}, &error) && openLevelSceneGroup(&open.map, group, &error),
		"open the lamp group", error);
	select(&open.map, {entity(open.light)});
	expect(levelMapEntityColourBlockedReason(open.map, levelMapEntityColourTargets(open.map)).isEmpty(), "the light inside the open group is not blocked");
	expect(setLevelMapSelectionEntityColour(&open.map, request, &report, &error) && keyOf(open.map, open.light) == QStringLiteral("0 255 0"),
		"the light inside the open group takes the colour", error);
	const QByteArray openAfter = bytesOf(open.map);
	if (expect(setLevelMapSelection(&open.map, {brush(open.d1)}, &selectError), "select the door outside the open group", selectError)) {
		expect(levelMapEntityColourBlockedReason(open.map, levelMapEntityColourTargets(open.map)).contains(QStringLiteral("open group")),
			"the door outside the open group is blocked before the picker", levelMapEntityColourBlockedReason(open.map, levelMapEntityColourTargets(open.map)));
		expect(!setLevelMapSelectionEntityColour(&open.map, request, &report, &error) && bytesOf(open.map) == openAfter, "the door outside the open group is refused", error);
	}

	// A linked copy of the wall takes the colour in the same step.
	Fixture linked = fixture();
	QString wallGroup;
	QString copy;
	expect(createLevelSceneNode(&linked.map, LevelSceneNodeKind::Group, QStringLiteral("Wall"), {}, &wallGroup, &error)
			&& assignLevelSceneObjects(&linked.map, wallGroup, {QStringLiteral("entity:%1").arg(linked.wall)}, &error)
			&& createLinkedLevelGroup(&linked.map, wallGroup, {0, 256, 0, true}, &copy, &error),
		"link the wall", error);
	const QByteArray linkedBefore = bytesOf(linked.map);
	const auto linkedDepth = linked.map.undoStack.size();
	select(&linked.map, {brush(linked.c1)});
	expect(setLevelMapSelectionEntityColour(&linked.map, request, &report, &error) && linked.map.undoStack.size() == linkedDepth + 1, "colour the linked wall", error);
	int coloured = 0;
	for (const LevelMapEntity& value : linked.map.entities) {
		if (value.className == QStringLiteral("func_wall") && keyOf(linked.map, value.id) == QStringLiteral("0 1 0")) {
			++coloured;
		}
	}
	expect(coloured == 2, "both copies of the wall take the colour", QString::number(coloured));
	expect(undoLevelMapEdit(&linked.map, &error) && bytesOf(linked.map) == linkedBefore, "one undo takes it off both copies", error);
}

QStringList keysFor(const QString& profile, const QString& command)
{
	for (const LevelEditorKeyBinding& binding : levelEditorControlsForProfile(profile).keys) {
		if (binding.commandId == command) {
			return binding.keys;
		}
	}
	return {};
}

void profileKeys()
{
	// NRC entity.cpp EntityColorSet at 449778b3 and NetRadiant entity.cpp
	// EntityColor at b4b295d7 bind K; EntityColorNormalize has no key.
	for (const QString& profile : {QStringLiteral("netradiant-custom"), QStringLiteral("netradiant")}) {
		expect(keysFor(profile, QStringLiteral("map.setEntityColour")) == QStringList {QStringLiteral("K")}, "K sets the entity colour", profile);
	}
	for (const QString& profile : {QStringLiteral("trenchbroom"), QStringLiteral("vibestudio-default")}) {
		expect(keysFor(profile, QStringLiteral("map.setEntityColour")).isEmpty(), "no Set Entity Colour key outside the NetRadiant family", profile);
	}
	for (const QString& profile : {QStringLiteral("netradiant-custom"), QStringLiteral("netradiant"), QStringLiteral("trenchbroom"), QStringLiteral("vibestudio-default")}) {
		expect(keysFor(profile, QStringLiteral("map.normaliseEntityColour")).isEmpty(), "Normalise Entity Colour has no key", profile);
		EditorProfileDescriptor descriptor;
		QString conflict;
		expect(editorProfileForId(profile, &descriptor) && !editorProfileHasShortcutConflict(descriptor, &conflict), "no shortcut conflict", profile + QStringLiteral(": ") + conflict);
	}
}

void cli(const QString& executable)
{
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "CLI temporary directory")) {
		return;
	}
	const Fixture f = fixture();
	const QString path = temp.filePath(QStringLiteral("colour.map"));
	QFile file(path);
	const QByteArray source = bytesOf(f.map);
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(), "CLI fixture")) {
		return;
	}
	file.close();
	const auto run = [&](const QStringList& args, int code = 0) {
		QProcess process;
		process.setWorkingDirectory(temp.path());
		process.start(executable, QStringList {QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--settings-file"),
									  temp.filePath(QStringLiteral("settings.ini")), QStringLiteral("map"), QStringLiteral("entity-colour")}
								+ args);
		const bool done = process.waitForStarted() && process.waitForFinished(60000);
		const QByteArray out = process.readAllStandardOutput();
		const QByteArray err = process.readAllStandardError();
		expect(done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == code, "CLI exit status",
			args.join(QLatin1Char(' ')) + QStringLiteral(": ") + QString::fromUtf8(out + err));
		if (!done) {
			process.kill();
			process.waitForFinished();
		}
		return QJsonDocument::fromJson(out).object();
	};
	const auto load = [&](const QString& output) {
		LevelMapDocument result;
		QString error;
		expect(loadLevelMap({output, {}, {}}, &result, &error), "load CLI output", error);
		return result;
	};
	const QString light = QStringLiteral("entity:%1").arg(f.light);
	const QString door = QStringLiteral("entity:%1").arg(f.door);
	QString output = temp.filePath(QStringLiteral("set.map"));
	QJsonObject report = run({path, QStringLiteral("--object"), light, QStringLiteral("--object"), QStringLiteral("brush:%1").arg(f.d1), QStringLiteral("--object"),
		QStringLiteral("brush:%1").arg(f.w1), QStringLiteral("--colour"), QStringLiteral("0 0.5 1"), QStringLiteral("--output"), output});
	expect(report.value(QStringLiteral("mode")).toString() == QStringLiteral("set") && report.value(QStringLiteral("changed")).toArray() == QJsonArray {light, door}
			&& report.value(QStringLiteral("worldPrimitives")).toInt() == 1 && report.value(QStringLiteral("source")).toString() == door,
		"CLI set reports the targets", QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact)));
	LevelMapDocument set = load(output);
	expect(keyOf(set, f.light) == QStringLiteral("0 128 255") && keyOf(set, f.door) == QStringLiteral("0 0.5 1") && !hasKey(set, f.world), "CLI output holds the colours");

	output = temp.filePath(QStringLiteral("hex.map"));
	run({path, QStringLiteral("--object"), light, QStringLiteral("--colour"), QStringLiteral("#ff8000"), QStringLiteral("--output"), output});
	expect(keyOf(load(output), f.light) == QStringLiteral("255 128 0"), "CLI takes #rrggbb");
	output = temp.filePath(QStringLiteral("key.map"));
	run({path, QStringLiteral("--object"), light, QStringLiteral("--key"), QStringLiteral("color"), QStringLiteral("--colour"), QStringLiteral("1,1,0"), QStringLiteral("--output"),
		output});
	expect(keyOf(load(output), f.light, QStringLiteral("color")) == QStringLiteral("1 1 0"), "CLI --key names another key and takes commas");

	output = temp.filePath(QStringLiteral("normalised.map"));
	report = run({path, QStringLiteral("--object"), door, QStringLiteral("--object"), light, QStringLiteral("--normalise"), QStringLiteral("--output"), output});
	LevelMapDocument normalised = load(output);
	expect(report.value(QStringLiteral("mode")).toString() == QStringLiteral("normalise") && keyOf(normalised, f.light) == QStringLiteral("1 0 0")
			&& keyOf(normalised, f.door) == QStringLiteral("1 0 0"),
		"CLI --normalise gives the primary light's colour to both");
	output = temp.filePath(QStringLiteral("each.map"));
	report = run({path, QStringLiteral("--where"), QStringLiteral("class=light"), QStringLiteral("--normalize"), QStringLiteral("--each"), QStringLiteral("--output"), output});
	expect(report.value(QStringLiteral("mode")).toString() == QStringLiteral("normalise-each") && report.value(QStringLiteral("entities")).toArray() == QJsonArray {light}
			&& keyOf(load(output), f.light) == QStringLiteral("1 0 0"),
		"CLI --where with --each normalises every light", QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact)));

	run({}, 2);
	run({path, QStringLiteral("--object"), light, QStringLiteral("--colour"), QStringLiteral("1 0 0"), QStringLiteral("--normalise"), QStringLiteral("--output"),
			temp.filePath(QStringLiteral("both.map"))},
		2);
	run({path, QStringLiteral("--object"), light, QStringLiteral("--colour"), QStringLiteral("1 0 0"), QStringLiteral("--each"), QStringLiteral("--output"),
			temp.filePath(QStringLiteral("each-set.map"))},
		2);
	run({path, QStringLiteral("--object"), light, QStringLiteral("--colour"), QStringLiteral("red"), QStringLiteral("--output"), temp.filePath(QStringLiteral("red.map"))}, 2);
	run({path, QStringLiteral("--object"), light, QStringLiteral("--colour"), QStringLiteral("1 0 0")}, 2);
	run({path, QStringLiteral("--object"), QStringLiteral("brush:%1").arg(f.w1), QStringLiteral("--colour"), QStringLiteral("1 0 0"), QStringLiteral("--output"),
			temp.filePath(QStringLiteral("world.map"))},
		1);
	expect(!QFileInfo::exists(temp.filePath(QStringLiteral("world.map"))), "a refused CLI edit writes nothing");
	run({path, QStringLiteral("--object"), QStringLiteral("entity:%1").arg(f.wall), QStringLiteral("--normalise"), QStringLiteral("--output"),
			temp.filePath(QStringLiteral("wall.map"))},
		1);
	QFile check(path);
	expect(check.open(QIODevice::ReadOnly) && check.readAll() == source, "the CLI never rewrites its input");
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	parsing();
	targets();
	setColour();
	normalise();
	sceneRules();
	profileKeys();
	if (argc == 2) {
		cli(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath());
	}
	std::cout << (ok ? "Entity colour checks passed" : "Entity colour checks failed") << std::endl;
	return ok ? 0 : 1;
}
