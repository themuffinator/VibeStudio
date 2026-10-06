#include "core/level_map.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool condition, const char* message)
{
	if (!condition) { std::cerr << message << '\n'; ok = false; }
	return condition;
}
void expectUsage(const QVector<LevelMapTextureUse>& actual, const QVector<LevelMapTextureUse>& expected, const char* message)
{
	bool same = actual.size() == expected.size();
	for (int i = 0; same && i < actual.size(); ++i) {
		same = actual[i].name == expected[i].name && actual[i].count == expected[i].count;
	}
	expect(same, message);
}
LevelMapBrush brush(int id, int owner, const QStringList& names)
{
	LevelMapBrush result; result.id = id; result.entityId = owner;
	for (const auto& name : names) { LevelMapBrushFace face; face.textureName = name; result.faces.append(face); }
	return result;
}
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	LevelMapDocument names;
	names.textureReferences = {" Wall ", "wall", "WALL", "-", " ", "Ceil", "ceil", QString::fromUtf8("Σ"), QString::fromUtf8("ς"), QString::fromUtf8("σ")};
	expect(levelMapTextureNames(names) == QStringList{"Ceil", "Wall", QString::fromUtf8("Σ")}, "names keep first spelling, trim, sort and fold Unicode case");
	expect(levelMapTextureLines(names) == levelMapTextureNames(names) && levelMapStatistics(names).uniqueTextureCount == 3,
		"CLI lines and statistics share the same unique material names");
	const auto inspected = levelMapInspectionSummary(names);
	expect(inspected.textureNames == QStringList{"Ceil", "Wall", QString::fromUtf8("Σ")} &&
		inspected.statistics.uniqueTextureCount == 3 && inspected.statistics.textureReferenceCount == 10,
		"inspection shares exact normalized names with their statistics");
	names.textureReferences = {"", " ", "-", " - "};
	expect(levelMapTextureNames(names).isEmpty() && levelMapTextureLines(names).size() == 1, "empty names do not become inspector choices; textual empty state remains");
	const auto empty = levelMapInspectionSummary(names);
	expect(empty.textureNames.isEmpty() && empty.statistics.uniqueTextureCount == 0 && empty.statistics.textureReferenceCount == 4 &&
		inspected.textureNames.size() == 3, "new summaries follow live records without mutating earlier value snapshots");

	LevelMapDocument quake; quake.format = LevelMapFormat::QuakeMap;
	quake.brushes = {brush(11, 7, {"Wall", " wall ", "-", "Roof", "", "  "}), brush(21, 9, {"WALL", "Floor"})};
	LevelMapPatch roof; roof.id = 31; roof.entityId = 7; roof.textureName = " Roof ";
	LevelMapPatch floor; floor.id = 32; floor.entityId = 9; floor.textureName = "floor";
	quake.patches = {roof, floor};
	expectUsage(levelMapTextureUsage(quake), {{"Floor", 2}, {"Roof", 2}, {"Wall", 3}}, "whole-map counts retain first spelling and surface multiplicity");
	quake.selection = {{LevelMapSelectionKind::Entity, 7}, {LevelMapSelectionKind::QuakeBrush, 11}, {LevelMapSelectionKind::QuakePatch, 31}};
	expectUsage(levelMapTextureUsage(quake, true), {{"Roof", 2}, {"Wall", 2}}, "entity and explicit members do not double count their brushes or patches");
	quake.selection = {{LevelMapSelectionKind::QuakePatch, 32}};
	expectUsage(levelMapTextureUsage(quake, true), {{"floor", 1}}, "patch-only selection retains its native spelling");
	quake.selection.clear();
	expect(levelMapTextureUsage(quake, true).isEmpty(), "empty selection has no scoped uses");

	LevelMapDocument doom; doom.format = LevelMapFormat::DoomWad;
	LevelMapDoomSidedef first; first.id = 5; first.upperTexture = "BRICK"; first.lowerTexture = "-"; first.middleTexture = "brick";
	LevelMapDoomSidedef second; second.id = 17; second.upperTexture = "METAL"; second.lowerTexture = "brick"; second.middleTexture.clear();
	LevelMapDoomSidedef unused; unused.id = 99; unused.upperTexture = unused.lowerTexture = "-"; unused.middleTexture = "unused";
	doom.doomSidedefs = {first, second, unused};
	LevelMapDoomLinedef front; front.id = 101; front.frontSidedef = 5; front.backSidedef = 17;
	LevelMapDoomLinedef back; back.id = 109; back.frontSidedef = 17; back.backSidedef = -1;
	doom.doomLinedefs = {front, back};
	LevelMapDoomSector sector; sector.id = 40; sector.floorTexture = "floor"; sector.ceilingTexture = "CEIL";
	LevelMapDoomSector other; other.id = 41; other.floorTexture = "FLOOR"; other.ceilingTexture = "sky";
	doom.doomSectors = {sector, other};
	doom.selection = {{LevelMapSelectionKind::DoomLinedef, 109}, {LevelMapSelectionKind::DoomLinedef, 101},
		{LevelMapSelectionKind::DoomLinedef, 101}, {LevelMapSelectionKind::DoomSector, 40}};
	expectUsage(levelMapTextureUsage(doom, true), {{"BRICK", 3}, {"CEIL", 1}, {"floor", 1}, {"METAL", 1}},
		"Doom selection visits shared sidedefs once and selected flats with sparse native IDs");
	expectUsage(levelMapTextureUsage(doom), {{"BRICK", 3}, {"CEIL", 1}, {"floor", 2}, {"METAL", 1}, {"sky", 1}, {"unused", 1}},
		"whole-map summary ignores selection and includes unreferenced native sides");

	QJsonArray measurements;
	for (const int count : {1000, 10000, 50000}) {
		LevelMapDocument large; large.format = LevelMapFormat::QuakeMap;
		const auto seed = brush(0, 0, {"studio/grid", "STUDIO/GRID", "studio/grid", "-", "", "studio/trim"});
		large.brushes.reserve(count); large.textureReferences.reserve(count * 6);
		for (int i = 0; i < count; ++i) {
			auto copy = seed; copy.id = i * 3 + 11; large.brushes.append(copy);
			for (const auto& face : copy.faces) { large.textureReferences.append(face.textureName); }
		}
		QElapsedTimer elapsed; elapsed.start();
		const auto usage = levelMapTextureUsage(large);
		const double usageMs = elapsed.nsecsElapsed() / 1e6;
		expectUsage(usage, {{"studio/grid", count * 3}, {"studio/trim", count}}, "large repeated-palette summary is complete");
		elapsed.restart();
		const auto unique = levelMapTextureNames(large);
		const double namesMs = elapsed.nsecsElapsed() / 1e6;
		expect(unique == QStringList{"studio/grid", "studio/trim"}, "large repeated-palette names remain complete");
		const auto summary = levelMapInspectionSummary(large);
		expect(summary.textureNames == unique && summary.statistics.brushCount == count &&
			summary.statistics.brushFaceCount == count * 6 && summary.statistics.textureReferenceCount == count * 6 &&
			summary.statistics.uniqueTextureCount == 2, "large inspection preserves full geometry/reference counts and normalized names");
		LevelMapDocument manyNames;
		for (int i = count - 1; i >= 0; --i) { manyNames.textureReferences << QStringLiteral("material/%1").arg(i, 6, 10, QLatin1Char('0')); }
		elapsed.restart(); const auto sorted = levelMapTextureNames(manyNames); const double distinctMs = elapsed.nsecsElapsed() / 1e6;
		expect(sorted.size() == count && sorted.first() == "material/000000" &&
			sorted.last() == QStringLiteral("material/%1").arg(count - 1, 6, 10, QLatin1Char('0')), "large distinct palette sorts without truncation");

		LevelMapDocument manySides; manySides.format = LevelMapFormat::DoomWad;
		LevelMapDoomSidedef shared; shared.id = 1; shared.upperTexture = shared.lowerTexture = "-"; shared.middleTexture = "shared";
		manySides.doomSidedefs.append(shared);
		for (int i = 0; i < count; ++i) {
			LevelMapDoomSidedef side = shared; side.id = i * 3 + 5; side.middleTexture = "BRICK";
			manySides.doomSidedefs.append(side);
			LevelMapDoomLinedef line; line.id = i * 3 + 100; line.frontSidedef = side.id; line.backSidedef = shared.id;
			manySides.doomLinedefs.append(line); manySides.selection.append({LevelMapSelectionKind::DoomLinedef, line.id});
		}
		elapsed.restart(); const auto selected = levelMapTextureUsage(manySides, true); const double selectedMs = elapsed.nsecsElapsed() / 1e6;
		expectUsage(selected, {{"BRICK", count}, {"shared", 1}}, "large selected-linedef summary retains shared-side deduplication");
		measurements << QJsonObject{{"brushes_or_lines", count}, {"uses_ms", usageMs}, {"names_ms", namesMs},
			{"distinct_names_ms", distinctMs}, {"selected_doom_uses_ms", selectedMs}};
	}
	std::cout << QJsonDocument(QJsonObject{{"measurements", measurements}}).toJson(QJsonDocument::Compact).constData() << '\n';
	return ok ? 0 : 1;
}
