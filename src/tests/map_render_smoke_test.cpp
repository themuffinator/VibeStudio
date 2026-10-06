#include "core/level_map.h"
#include "core/map_render.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace vibestudio;

namespace {

int fail(const char* message)
{
	std::cerr << message << "\n";
	return EXIT_FAILURE;
}

bool expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << message << "\n";
		return false;
	}
	return true;
}

int countOccurrences(const QString& haystack, const QString& needle)
{
	if (needle.isEmpty()) {
		return 0;
	}
	int count = 0;
	qsizetype index = haystack.indexOf(needle, 0);
	while (index >= 0) {
		++count;
		index = haystack.indexOf(needle, index + needle.size());
	}
	return count;
}

// Every `&` in the document must open a known XML entity, otherwise the SVG is
// not well-formed XML.
bool ampersandsAreEscaped(const QString& svg)
{
	qsizetype index = svg.indexOf(QLatin1Char('&'), 0);
	while (index >= 0) {
		const QString tail = svg.mid(index, 6);
		const bool known = tail.startsWith(QStringLiteral("&amp;")) || tail.startsWith(QStringLiteral("&lt;"))
			|| tail.startsWith(QStringLiteral("&gt;")) || tail.startsWith(QStringLiteral("&quot;"))
			|| tail.startsWith(QStringLiteral("&apos;")) || tail.startsWith(QStringLiteral("&#"));
		if (!known) {
			return false;
		}
		index = svg.indexOf(QLatin1Char('&'), index + 1);
	}
	return true;
}

LevelMapVec3 vec3(double x, double y, double z)
{
	LevelMapVec3 point;
	point.x = x;
	point.y = y;
	point.z = z;
	point.valid = true;
	return point;
}

LevelMapDocument buildDoomDocument()
{
	LevelMapDocument document;
	document.mapName = QStringLiteral("MAP01");
	document.engineFamily = QStringLiteral("idtech1");
	document.format = LevelMapFormat::DoomWad;

	const double coordinates[4][2] = {{0.0, 0.0}, {512.0, 0.0}, {512.0, 512.0}, {0.0, 512.0}};
	for (int index = 0; index < 4; ++index) {
		LevelMapDoomVertex vertex;
		vertex.id = index;
		vertex.x = coordinates[index][0];
		vertex.y = coordinates[index][1];
		document.doomVertices.push_back(vertex);
	}
	for (int index = 0; index < 4; ++index) {
		LevelMapDoomLinedef linedef;
		linedef.id = index;
		linedef.startVertex = index;
		linedef.endVertex = (index + 1) % 4;
		linedef.frontSidedef = 0;
		linedef.backSidedef = index == 2 ? 1 : -1;
		linedef.flags = index == 2 ? 0x0004 : 0;
		document.doomLinedefs.push_back(linedef);
	}
	for (int index = 0; index < 2; ++index) {
		LevelMapDoomSidedef sidedef;
		sidedef.id = index;
		sidedef.sector = 0;
		document.doomSidedefs.push_back(sidedef);
	}
	LevelMapDoomSector sector;
	sector.id = 0;
	sector.floorHeight = 0;
	sector.ceilingHeight = 128;
	sector.lightLevel = 160;
	document.doomSectors.push_back(sector);

	LevelMapDoomThing thing;
	thing.id = 0;
	thing.x = 256.0;
	thing.y = 256.0;
	thing.angle = 90;
	thing.type = 1;
	document.doomThings.push_back(thing);
	return document;
}

// A Radiant-style axis-aligned box. The three points per face follow the id
// `.map` convention so the half-space normals point out of the brush
// (https://quakewiki.org/wiki/Quake_Map_Format).
LevelMapBrush buildBoxBrush(int brushId, int entityId, const QString& textureName)
{
	const double x1 = -64.0;
	const double y1 = -64.0;
	const double z1 = -16.0;
	const double x2 = 64.0;
	const double y2 = 64.0;
	const double z2 = 16.0;
	const LevelMapVec3 facePoints[6][3] = {
		{vec3(x1, y2, z2), vec3(x2, y2, z2), vec3(x2, y1, z2)},
		{vec3(x1, y1, z1), vec3(x2, y1, z1), vec3(x2, y2, z1)},
		{vec3(x1, y2, z2), vec3(x1, y1, z2), vec3(x1, y1, z1)},
		{vec3(x2, y1, z2), vec3(x2, y2, z2), vec3(x2, y2, z1)},
		{vec3(x2, y2, z2), vec3(x1, y2, z2), vec3(x1, y2, z1)},
		{vec3(x1, y1, z2), vec3(x2, y1, z2), vec3(x2, y1, z1)},
	};
	LevelMapBrush brush;
	brush.id = brushId;
	brush.entityId = entityId;
	for (int index = 0; index < 6; ++index) {
		LevelMapBrushFace face;
		face.id = index;
		face.p0 = facePoints[index][0];
		face.p1 = facePoints[index][1];
		face.p2 = facePoints[index][2];
		face.textureName = textureName;
		brush.faces.push_back(face);
		brush.textureNames << textureName;
	}
	brush.faceCount = static_cast<int>(brush.faces.size());
	return brush;
}

LevelMapDocument buildQuakeDocument()
{
	LevelMapDocument document;
	document.mapName = QStringLiteral("start");
	document.engineFamily = QStringLiteral("idtech2");
	document.format = LevelMapFormat::QuakeMap;

	LevelMapEntity world;
	world.id = 0;
	world.className = QStringLiteral("worldspawn");
	document.entities.push_back(world);

	LevelMapEntity player;
	player.id = 1;
	player.className = QStringLiteral("info&player<start");
	player.origin = vec3(32.0, 32.0, 24.0);
	document.entities.push_back(player);

	document.brushes.push_back(buildBoxBrush(0, 0, QStringLiteral("wall&rock<1")));
	return document;
}

MapRenderOptions plainDoomOptions()
{
	MapRenderOptions options;
	options.projection = MapRenderProjection::TopXY;
	options.width = 512;
	options.height = 512;
	options.margin = 24;
	options.showGrid = false;
	options.showSectorFill = false;
	options.showVertices = false;
	options.showThings = true;
	options.showEntities = true;
	options.showLabels = false;
	return options;
}

bool runProjectionIdSmoke()
{
	bool ok = true;
	ok &= expect(mapRenderProjectionIds().size() == 3, "Expected three map render projections.");
	ok &= expect(mapRenderProjectionId(MapRenderProjection::FrontXZ) == QStringLiteral("front-xz"), "Expected front-xz projection id.");
	MapRenderProjection projection = MapRenderProjection::TopXY;
	ok &= expect(mapRenderProjectionFromId(QStringLiteral("Side_ZY"), &projection), "Expected side-zy id to parse.");
	ok &= expect(projection == MapRenderProjection::SideZY, "Expected side-zy id to map to SideZY.");
	ok &= expect(!mapRenderProjectionFromId(QStringLiteral("isometric"), &projection), "Expected unknown projection id to be rejected.");
	ok &= expect(projection == MapRenderProjection::SideZY, "Expected failed parse to leave the projection untouched.");
	return ok;
}

bool runDoomRenderSmoke()
{
	bool ok = true;
	const LevelMapDocument document = buildDoomDocument();
	const MapRenderOptions options = plainDoomOptions();
	MapRenderReport report;
	const QString svg = renderLevelMapSvg(document, options, &report);

	ok &= expect(!svg.isEmpty(), "Expected a non-empty SVG document.");
	ok &= expect(svg.startsWith(QStringLiteral("<svg")), "Expected the SVG document to start with an <svg root.");
	ok &= expect(svg.trimmed().endsWith(QStringLiteral("</svg>")), "Expected the SVG document to close its root element.");
	ok &= expect(svg.contains(QStringLiteral("xmlns=\"http://www.w3.org/2000/svg\"")), "Expected the SVG namespace declaration.");
	ok &= expect(svg.contains(QStringLiteral("<title>MAP01</title>")), "Expected the map name as the document title.");
	ok &= expect(report.succeeded() && report.rendered, "Expected a successful render report.");
	ok &= expect(report.width == 512 && report.height == 512, "Expected the requested image size in the report.");

	// Four linedefs plus one direction tick for the single thing.
	ok &= expect(countOccurrences(svg, QStringLiteral("<line")) == 5, "Expected five line elements for four linedefs and one thing tick.");
	ok &= expect(countOccurrences(svg, QStringLiteral("<circle")) == 1, "Expected one circle element for the single thing.");
	ok &= expect(countOccurrences(svg, QStringLiteral("<polygon")) == 0, "Expected no polygon elements without sector fill.");
	ok &= expect(report.drawnLinedefCount == 4, "Expected four drawn linedefs.");
	ok &= expect(report.drawnThingCount == 1, "Expected one drawn thing.");
	ok &= expect(report.drawnSectorCount == 0, "Expected no sector fills when sector fill is disabled.");

	// One-sided and two-sided linedefs must differ in stroke width, not only in
	// colour.
	ok &= expect(countOccurrences(svg, QStringLiteral("stroke-width=\"1.800\"")) == 3, "Expected three thick one-sided linedefs.");
	ok &= expect(countOccurrences(svg, QStringLiteral("stroke-width=\"0.800\"")) == 1, "Expected one thin two-sided linedef.");
	ok &= expect(svg.contains(QStringLiteral("<title>Linedef 2 (two-sided)")), "Expected a title element on the two-sided linedef.");

	// The square is 512 units wide and the usable area is 512 - 2 * 24 pixels.
	const double expectedUnitsPerPixel = 512.0 / (512.0 - 48.0);
	ok &= expect(std::abs(report.unitsPerPixel - expectedUnitsPerPixel) < 1e-6, "Expected a sane units-per-pixel value.");

	// World Y grows up, SVG Y grows down: vertex (0, 512) must be above (0, 0).
	const qsizetype topLeftIndex = svg.indexOf(QStringLiteral("y1=\"24.000\""));
	ok &= expect(topLeftIndex >= 0, "Expected the highest world vertex to land on the top margin.");
	ok &= expect(svg.contains(QStringLiteral("y2=\"488.000\"")) || svg.contains(QStringLiteral("y1=\"488.000\"")),
		"Expected the lowest world vertex to land on the bottom margin.");

	// Determinism: identical inputs must produce byte-identical output.
	MapRenderReport secondReport;
	const QString again = renderLevelMapSvg(document, options, &secondReport);
	ok &= expect(again == svg, "Expected the renderer to be deterministic.");
	ok &= expect(secondReport.drawnLinedefCount == report.drawnLinedefCount, "Expected deterministic report counts.");

	MapRenderOptions detailed = options;
	detailed.showVertices = true;
	detailed.showSectorFill = true;
	MapRenderReport detailedReport;
	const QString detailedSvg = renderLevelMapSvg(document, detailed, &detailedReport);
	// The clip-path rect and the background rect, plus one rect per vertex.
	ok &= expect(countOccurrences(detailedSvg, QStringLiteral("<rect")) == 6, "Expected four vertex markers plus the clip and background rects.");
	ok &= expect(detailedReport.drawnSectorCount >= 1, "Expected at least one filled sector.");
	ok &= expect(detailedSvg.contains(QStringLiteral("light 160")), "Expected the sector title to name its light level.");

	MapRenderOptions highlighted = options;
	highlighted.highlightKind = LevelMapSelectionKind::DoomLinedef;
	highlighted.highlightObjectId = 1;
	const QString highlightedSvg = renderLevelMapSvg(document, highlighted, nullptr);
	ok &= expect(highlightedSvg.contains(QStringLiteral("id=\"vs-highlight\"")), "Expected a highlight overlay group.");
	ok &= expect(highlightedSvg.contains(QStringLiteral("Highlighted linedef 1")), "Expected the highlighted linedef title.");

	MapRenderOptions missingHighlight = options;
	missingHighlight.highlightKind = LevelMapSelectionKind::DoomThing;
	missingHighlight.highlightObjectId = 99;
	MapRenderReport missingReport;
	const QString missingSvg = renderLevelMapSvg(document, missingHighlight, &missingReport);
	ok &= expect(!missingSvg.contains(QStringLiteral("id=\"vs-highlight\"")), "Expected no overlay group for a missing highlight target.");
	ok &= expect(!missingReport.warnings.isEmpty(), "Expected a warning for a missing highlight target.");

	MapRenderOptions gridded = options;
	gridded.showGrid = true;
	gridded.gridSize = 64;
	MapRenderReport gridReport;
	const QString gridSvg = renderLevelMapSvg(document, gridded, &gridReport);
	ok &= expect(gridSvg.contains(QStringLiteral("id=\"vs-grid\"")), "Expected a grid group when the grid is enabled.");
	ok &= expect(countOccurrences(gridSvg, QStringLiteral("<line")) > 5, "Expected extra line elements for the grid.");

	MapRenderOptions tinyGrid = gridded;
	tinyGrid.gridSize = 1;
	MapRenderReport tinyGridReport;
	const QString tinyGridSvg = renderLevelMapSvg(document, tinyGrid, &tinyGridReport);
	ok &= expect(!tinyGridSvg.contains(QStringLiteral("id=\"vs-grid\"")), "Expected an absurdly dense grid to be skipped.");
	ok &= expect(!tinyGridReport.warnings.isEmpty(), "Expected a warning when the grid is skipped.");

	MapRenderOptions badGrid = gridded;
	badGrid.gridSize = 0;
	MapRenderReport badGridReport;
	renderLevelMapSvg(document, badGrid, &badGridReport);
	ok &= expect(!badGridReport.warnings.isEmpty(), "Expected a warning for a non-positive grid size.");

	// The elevation projections must still produce a document.
	MapRenderOptions front = options;
	front.projection = MapRenderProjection::FrontXZ;
	MapRenderReport frontReport;
	const QString frontSvg = renderLevelMapSvg(document, front, &frontReport);
	ok &= expect(frontSvg.startsWith(QStringLiteral("<svg")), "Expected a valid SVG for the front projection.");
	ok &= expect(!frontReport.warnings.isEmpty(), "Expected a flatness warning for a Doom map in the front projection.");
	return ok;
}

bool runQuakeRenderSmoke()
{
	bool ok = true;
	const LevelMapDocument document = buildQuakeDocument();
	MapRenderOptions options;
	options.width = 400;
	options.height = 400;
	options.showGrid = false;
	options.showLabels = true;
	MapRenderReport report;
	const QString svg = renderLevelMapSvg(document, options, &report);

	ok &= expect(svg.startsWith(QStringLiteral("<svg")), "Expected an SVG root for the Quake document.");
	ok &= expect(report.drawnBrushCount == 1, "Expected one drawn brush footprint.");
	ok &= expect(report.drawnEntityCount == 1, "Expected one drawn point entity.");
	ok &= expect(countOccurrences(svg, QStringLiteral("<polygon")) == 2, "Expected one brush polygon and one entity marker polygon.");

	// Escaping: `&` and `<` must never reach the document raw.
	ok &= expect(ampersandsAreEscaped(svg), "Expected every ampersand in the SVG to be an XML entity.");
	ok &= expect(svg.contains(QStringLiteral("info&amp;player&lt;start")), "Expected the entity classname to be XML-escaped.");
	ok &= expect(!svg.contains(QStringLiteral("info&player")), "Expected no raw ampersand in the entity classname.");
	ok &= expect(!svg.contains(QStringLiteral("player<start")), "Expected no raw less-than in the entity classname.");
	ok &= expect(svg.contains(QStringLiteral("wall&amp;rock&lt;1")), "Expected the brush texture name to be XML-escaped.");
	ok &= expect(!svg.contains(QStringLiteral("rock<1")), "Expected no raw less-than in the texture name.");
	ok &= expect(countOccurrences(svg, QStringLiteral("info&amp;player&lt;start")) >= 2, "Expected the classname in both a title and a label.");

	MapRenderReport highlightReport;
	MapRenderOptions highlighted = options;
	highlighted.highlightKind = LevelMapSelectionKind::QuakeBrush;
	highlighted.highlightObjectId = 0;
	const QString highlightedSvg = renderLevelMapSvg(document, highlighted, &highlightReport);
	ok &= expect(highlightedSvg.contains(QStringLiteral("id=\"vs-highlight\"")), "Expected a highlight overlay for the brush.");

	// A leak trail is drawn over the map and widens the framing to reach it.
	MapRenderOptions leaking = options;
	for (const double y : {0.0, 400.0, 900.0}) {
		LevelMapVec3 point;
		point.x = 0.0;
		point.y = y;
		point.z = 24.0;
		point.valid = true;
		leaking.leakTrail.push_back(point);
	}
	MapRenderReport leakReport;
	const QString leakSvg = renderLevelMapSvg(document, leaking, &leakReport);
	ok &= expect(leakSvg.contains(QStringLiteral("id=\"vs-leak\"")) && leakReport.drawnLeakPointCount == 3, "Expected a three-point leak trail layer.");
	ok &= expect(leakReport.unitsPerPixel > report.unitsPerPixel, "Expected the framing to widen to include the leak trail.");
	ok &= expect(mapRenderReportText(leakReport).contains(QStringLiteral("Leak trail points: 3")), "Expected the report to count the leak trail.");

	// Target links are opt-in, so pictures made before them stay identical.
	ok &= expect(!svg.contains(QStringLiteral("id=\"vs-links\"")) && report.drawnTargetLinkCount == 0, "Expected no link layer by default.");
	LevelMapDocument linked = document;
	LevelMapEntity lamp;
	lamp.id = 7;
	lamp.className = QStringLiteral("light");
	lamp.origin = {200.0, 200.0, 24.0, true};
	lamp.properties = {{QStringLiteral("classname"), QStringLiteral("light"), 0}, {QStringLiteral("targetname"), QStringLiteral("lamp"), 0}};
	linked.entities.push_back(lamp);
	for (LevelMapEntity& entity : linked.entities) {
		if (entity.id != lamp.id) {
			entity.properties.push_back({QStringLiteral("killtarget"), QStringLiteral("lamp"), 0});
		}
	}
	MapRenderOptions withLinks = options;
	withLinks.showTargetLinks = true;
	MapRenderReport linkReport;
	const QString linkSvg = renderLevelMapSvg(linked, withLinks, &linkReport);
	ok &= expect(linkSvg.contains(QStringLiteral("id=\"vs-links\"")) && linkReport.drawnTargetLinkCount >= 1, "Expected a link layer when links are on.");
	ok &= expect(linkSvg.indexOf(QStringLiteral("id=\"vs-links\"")) < linkSvg.indexOf(QStringLiteral("id=\"vs-entities\"")),
		"Expected links under the entity markers.");
	ok &= expect(linkSvg.contains(QStringLiteral("stroke-dasharray")), "Expected a killtarget link to be dashed.");
	ok &= expect(mapRenderReportText(linkReport).contains(QStringLiteral("Target links:")), "Expected the report to count links.");

	// Dark and light colour sets must differ, and the background must always be
	// explicit.
	MapRenderOptions light = options;
	light.darkBackground = false;
	light.highContrast = true;
	const QString lightSvg = renderLevelMapSvg(document, light, nullptr);
	ok &= expect(lightSvg.contains(QStringLiteral("id=\"vs-background\"")), "Expected an explicit background rect.");
	ok &= expect(lightSvg != svg, "Expected the high-contrast light palette to differ from the dark palette.");
	return ok;
}

bool runEmptyDocumentSmoke()
{
	bool ok = true;
	const LevelMapDocument document;
	MapRenderOptions options;
	MapRenderReport report;
	const QString svg = renderLevelMapSvg(document, options, &report);
	ok &= expect(svg.startsWith(QStringLiteral("<svg")), "Expected an SVG root for an empty document.");
	ok &= expect(report.succeeded(), "Expected an empty document to render without an error.");
	ok &= expect(report.rendered, "Expected an empty document to still report a render.");
	ok &= expect(!report.warnings.isEmpty(), "Expected a warning for an empty document.");
	ok &= expect(report.drawnLinedefCount == 0 && report.drawnBrushCount == 0 && report.drawnEntityCount == 0,
		"Expected no drawn elements for an empty document.");
	ok &= expect(report.unitsPerPixel > 0.0, "Expected a positive units-per-pixel value for an empty document.");
	ok &= expect(!mapRenderReportText(report).isEmpty(), "Expected report text for an empty document.");
	return ok;
}

bool runWriteSmoke(const QDir& root)
{
	bool ok = true;
	const LevelMapDocument document = buildDoomDocument();
	const MapRenderOptions options = plainDoomOptions();
	const QString outputPath = root.filePath(QStringLiteral("render/map01.svg"));

	const MapRenderReport dryRun = writeLevelMapSvg(document, options, outputPath, true, false);
	ok &= expect(dryRun.succeeded(), "Expected a dry run to succeed.");
	ok &= expect(!dryRun.rendered, "Expected a dry run not to report a written file.");
	ok &= expect(!dryRun.warnings.isEmpty(), "Expected a dry run warning.");
	ok &= expect(!QFileInfo::exists(outputPath), "Expected a dry run to write nothing.");

	const MapRenderReport written = writeLevelMapSvg(document, options, outputPath, false, false);
	ok &= expect(written.succeeded() && written.rendered, "Expected the SVG to be written.");
	ok &= expect(QFileInfo::exists(outputPath), "Expected the SVG file to exist after writing.");
	ok &= expect(written.outputPath == QFileInfo(outputPath).absoluteFilePath(), "Expected an absolute output path in the report.");

	QFile file(outputPath);
	ok &= expect(file.open(QIODevice::ReadOnly), "Expected to reopen the written SVG.");
	const QByteArray bytes = file.readAll();
	file.close();
	ok &= expect(bytes.startsWith("<svg"), "Expected the written file to start with an <svg root.");

	const MapRenderReport refused = writeLevelMapSvg(document, options, outputPath, false, false);
	ok &= expect(!refused.succeeded(), "Expected an existing file to be refused without overwrite.");
	ok &= expect(!refused.rendered, "Expected no write when the output is refused.");

	const MapRenderReport overwritten = writeLevelMapSvg(document, options, outputPath, false, true);
	ok &= expect(overwritten.succeeded() && overwritten.rendered, "Expected an overwrite to succeed.");

	const MapRenderReport missingPath = writeLevelMapSvg(document, options, QString(), false, true);
	ok &= expect(!missingPath.succeeded(), "Expected an empty output path to be refused.");
	return ok;
}

} // namespace

int main()
{
	QTemporaryDir tempDir;
	if (!tempDir.isValid()) {
		return fail("Expected temporary directory.");
	}
	const QDir root(tempDir.path());
	bool ok = true;
	ok &= runProjectionIdSmoke();
	ok &= runDoomRenderSmoke();
	ok &= runQuakeRenderSmoke();
	ok &= runEmptyDocumentSmoke();
	ok &= runWriteSmoke(root);
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
