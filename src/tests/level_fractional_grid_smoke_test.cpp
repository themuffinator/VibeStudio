// Fractional grid sizes (0.125, 0.25 and 0.5) without a window: the shared
// size list, the finest grid each map format allows, stepping below 1 unit,
// command ids and text, profile validation, the palette and shortcut
// registries, no default keys (as upstream), the 2D export's fractional grid,
// and the CLI: the editor controls report and fractional --grid values on map
// snap, which binary Doom maps refuse. Workflow reference: NRC radiant/grid.cpp
// at 449778b3 and TB Grid at 4c756be1; see docs/CREDITS.md.

#include "core/editor_profiles.h"
#include "core/level_document.h"
#include "core/level_editor_controls.h"
#include "core/level_map.h"
#include "core/map_render.h"
#include "core/studio_semantics.h"
#include "core/studio_settings.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;

namespace {

bool ok = true;

bool expect(bool condition, const char* message, const QString& detail = {})
{
	if (!condition) {
		ok = false;
		std::cerr << "FAIL: " << message << ": " << detail.toStdString() << '\n';
	}
	return condition;
}

QByteArray fileBytes(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

int countOf(const QString& text, const QString& needle)
{
	return static_cast<int>(text.count(needle));
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return EXIT_FAILURE;
	}
	const QString settingsPath = temp.filePath(QStringLiteral("settings.ini"));
	StudioSettings::setOverrideFilePath(settingsPath);

	// The list starts at TrenchBroom's and NetRadiant Custom's 0.125.
	const QVector<double> sizes = levelEditorGridSizes();
	expect(sizes.size() == 14 && sizes.value(0) == 0.125 && sizes.value(1) == 0.25 && sizes.value(2) == 0.5 && sizes.value(3) == 1.0
			&& sizes.last() == 1024.0,
		"the grid list runs 0.125, 0.25, 0.5, then 1 to 1024");
	for (const double units : {0.125, 0.25, 0.5}) {
		expect(levelEditorGridSizeSupported(units), "fractional sizes are supported", QString::number(units));
	}
	for (const double units : {0.0625, 0.3, 0.75, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
		expect(!levelEditorGridSizeSupported(units), "unlisted fractions are refused", QString::number(units));
	}
	expect(kLevelEditorFinestGridUnits == 0.125, "0.125 is the finest grid");

	// Text and ids stay in C-locale digits, so ids and saved settings are stable.
	expect(levelEditorGridSizeText(0.125) == QStringLiteral("0.125") && levelEditorGridSizeText(0.5) == QStringLiteral("0.5")
			&& levelEditorGridSizeText(16) == QStringLiteral("16") && levelEditorGridSizeText(1024) == QStringLiteral("1024"),
		"sizes print as typed", levelEditorGridSizeText(0.125) + QLatin1Char(' ') + levelEditorGridSizeText(1024));
	expect(levelEditorGridCommandId(0.125) == QStringLiteral("map.grid0.125") && levelEditorGridCommandId(0.25) == QStringLiteral("map.grid0.25")
			&& levelEditorGridCommandId(0.5) == QStringLiteral("map.grid0.5") && levelEditorGridCommandId(64) == QStringLiteral("map.grid64"),
		"command ids follow map.grid<N>", levelEditorGridCommandId(0.125));

	// The finest grid follows the map format: binary Doom and Hexen store
	// whole units, UDMF and Quake-family maps keep fractions.
	{
		LevelMapDocument empty;
		expect(levelEditorFinestGridUnits(empty) == 0.125, "no map allows 0.125");
		QString error;
		LevelMapDocument quake;
		LevelMapCreateRequest request;
		request.game = QStringLiteral("quake");
		expect(createLevelMap(request, &quake, &error) && levelEditorFinestGridUnits(quake) == 0.125, "a Quake map allows 0.125", error);
		LevelMapDocument doom;
		request.game = QStringLiteral("doom");
		expect(createLevelMap(request, &doom, &error) && doom.format == LevelMapFormat::DoomWad && levelEditorFinestGridUnits(doom) == 1.0,
			"a Doom map stops at 1 unit", error);
		LevelMapDocument hexen;
		request.game = QStringLiteral("hexen");
		expect(createLevelMap(request, &hexen, &error) && hexen.doomFormat == LevelMapDoomFormat::Hexen && levelEditorFinestGridUnits(hexen) == 1.0,
			"a Hexen map stops at 1 unit", error);
		LevelMapDocument udmf = doom;
		udmf.doomFormat = LevelMapDoomFormat::Udmf;
		expect(levelEditorFinestGridUnits(udmf) == 0.125, "UDMF keeps fractional coordinates, so it allows 0.125");
	}

	// Smaller Grid halves down to 0.125 (TB decGridSize, NRC GridPrev), and
	// stops at 1 on whole-unit maps; Larger Grid doubles back up.
	expect(levelEditorSteppedGridUnits(1, -1) == 0.5 && levelEditorSteppedGridUnits(0.5, -1) == 0.25 && levelEditorSteppedGridUnits(0.25, -1) == 0.125,
		"halving goes below 1 unit");
	expect(levelEditorSteppedGridUnits(0.125, -1) == 0.125, "0.125 is the end of the line");
	expect(levelEditorSteppedGridUnits(0.125, 1) == 0.25 && levelEditorSteppedGridUnits(0.5, 1) == 1.0, "doubling climbs back to whole units");
	expect(levelEditorSteppedGridUnits(1, -1, 1.0) == 1.0 && levelEditorSteppedGridUnits(2, -1, 1.0) == 1.0, "whole-unit maps stop halving at 1");
	expect(levelEditorSteppedGridUnits(0.5, -1, 1.0) == 1.0 && levelEditorSteppedGridUnits(0.25, 1, 1.0) == 1.0,
		"a fraction on a whole-unit map steps to 1");
	expect(levelEditorSteppedGridUnits(1024, 1) == 1024.0 && levelEditorSteppedGridUnits(512, 1) == 1024.0, "doubling still stops at 1024");
	expect(levelEditorSteppedGridUnits(0.3, -1) == 0.25 && levelEditorSteppedGridUnits(0.3, 1) == 0.5 && levelEditorSteppedGridUnits(48, 1) == 64.0,
		"an unlisted size steps from where it is");
	expect(levelEditorSteppedGridUnits(std::numeric_limits<double>::quiet_NaN(), -1) == 8.0, "a broken size starts again from 16");

	// Snapping lands on exact binary fractions.
	expect(snapLevelMapCoordinate(0.3, 0.125) == 0.25 && snapLevelMapCoordinate(-0.2, 0.25) == -0.25 && snapLevelMapCoordinate(10.74, 0.5) == 10.5,
		"core snapping honours fractional grids");

	// Profile validation accepts the fractional sizes and names the range.
	for (const double units : {0.125, 0.25, 0.5}) {
		LevelEditorControls controls = vibeStudioLevelControls();
		controls.defaultGridUnits = units;
		expect(levelEditorControlProblems(controls).isEmpty(), "a fractional default grid validates", QString::number(units));
	}
	{
		LevelEditorControls controls = vibeStudioLevelControls();
		controls.defaultGridUnits = 0.3;
		const QStringList problems = levelEditorControlProblems(controls);
		expect(problems.size() == 1 && problems.first().contains(QStringLiteral("0.125")) && problems.first().contains(QStringLiteral("1024")),
			"an unlisted default names the 0.125 to 1024 range", problems.join(QLatin1Char(';')));
	}

	// Upstream gives the fractional grids no default key; neither do the
	// profiles that follow it.
	for (const char* id : {"netradiant-custom", "trenchbroom", "gtkradiant-1-6", "vibestudio-default", "eureka"}) {
		const LevelEditorControls controls = levelEditorControlsForProfile(QString::fromLatin1(id));
		for (const LevelEditorKeyBinding& binding : controls.keys) {
			expect(!binding.commandId.startsWith(QStringLiteral("map.grid0.")), "no default key for a fractional grid",
				QString::fromLatin1(id) + QLatin1Char(' ') + binding.commandId);
		}
	}

	// The palette and the shortcut registry know the three commands.
	for (const double units : {0.125, 0.25, 0.5}) {
		const QString size = levelEditorGridSizeText(units);
		const QString id = levelEditorGridCommandId(units);
		CommandPaletteEntry entry;
		expect(commandPaletteEntryForCommandId(id, &entry) && entry.label == QStringLiteral("Grid %1").arg(size)
				&& entry.id == QStringLiteral("map-grid-%1").arg(size) && entry.summary.contains(size) && entry.summary.contains(QStringLiteral("Doom")),
			"palette entry", id + QLatin1Char(' ') + entry.label + QLatin1Char(' ') + entry.summary);
		expect(shellCommandIdExists(id), "registered shell command", id);
	}
	ShortcutDescriptor smaller;
	expect(shortcutForCommandId(QStringLiteral("map.grid-smaller"), &smaller) && smaller.description.contains(QStringLiteral("0.125"))
			&& smaller.description.contains(QStringLiteral("Doom")),
		"Smaller Grid says where it stops", smaller.description);
	QStringList conflicts;
	expect(!shortcutRegistryHasConflicts(&conflicts), "shortcut registry stays conflict-free", conflicts.join(QLatin1Char(';')));

	// The 2D export draws a fractional grid at its own spacing on a small map.
	{
		QString error;
		LevelMapDocument document;
		LevelMapCreateRequest request;
		request.game = QStringLiteral("quake");
		request.starterRoom = false;
		expect(createLevelMap(request, &document, &error)
				&& addLevelMapBoxBrush(&document, {0, 0, 0, true}, {4, 4, 4, true}, QStringLiteral("studio/cache"), nullptr, &error),
			"export fixture", error);
		MapRenderOptions half;
		half.gridSize = 0.5;
		half.width = 256;
		half.height = 256;
		MapRenderReport halfReport;
		const QString halfSvg = renderLevelMapSvg(document, half, &halfReport);
		MapRenderOptions whole = half;
		whole.gridSize = 1;
		MapRenderReport wholeReport;
		const QString wholeSvg = renderLevelMapSvg(document, whole, &wholeReport);
		expect(halfSvg.contains(QStringLiteral("id=\"vs-grid\"")) && countOf(halfSvg, QStringLiteral("<line")) > countOf(wholeSvg, QStringLiteral("<line")),
			"a 0.5-unit export grid draws more lines than a 1-unit one",
			QStringLiteral("%1 / %2 %3").arg(countOf(halfSvg, QStringLiteral("<line"))).arg(countOf(wholeSvg, QStringLiteral("<line"))).arg(halfReport.warnings.join(QLatin1Char(';'))));
		MapRenderOptions broken = half;
		broken.gridSize = std::numeric_limits<double>::quiet_NaN();
		MapRenderReport brokenReport;
		const QString brokenSvg = renderLevelMapSvg(document, broken, &brokenReport);
		expect(!brokenSvg.contains(QStringLiteral("id=\"vs-grid\"")) && !brokenReport.warnings.isEmpty(), "a broken export grid is skipped with a warning");
	}

	// CLI: the editor controls report lists the fractional sizes; map snap and
	// map render take fractional --grid values, and binary Doom maps refuse a
	// fractional snap grid, keeping their whole-unit coordinates.
	if (argc > 1) {
		const QString binary = QString::fromLocal8Bit(argv[1]);
		const auto cli = [&](const QStringList& arguments, bool success) {
			QProcess process;
			process.setWorkingDirectory(temp.path());
			process.start(binary, QStringList{QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--settings-file"), settingsPath} + arguments);
			const bool finished = process.waitForFinished(60000);
			const QByteArray out = process.readAllStandardOutput();
			expect(finished && process.exitStatus() == QProcess::NormalExit && (success ? process.exitCode() == 0 : process.exitCode() != 0), "CLI exit",
				arguments.join(QLatin1Char(' ')) + QLatin1Char(' ') + QString::fromUtf8(out + process.readAllStandardError()).left(600));
			if (!finished) {
				process.kill();
				process.waitForFinished();
			}
			return QJsonDocument::fromJson(out).object();
		};
		QJsonArray expectedSizes;
		for (const double units : sizes) {
			expectedSizes.append(units);
		}
		const QJsonObject tb = cli({QStringLiteral("editor"), QStringLiteral("controls"), QStringLiteral("trenchbroom")}, true).value(QStringLiteral("controls")).toObject();
		expect(tb.value(QStringLiteral("gridSizes")).toArray() == expectedSizes && tb.value(QStringLiteral("gridSizes")).toArray().first().toDouble() == 0.125,
			"editor controls lists 0.125 to 1024", QString::fromUtf8(QJsonDocument(tb.value(QStringLiteral("gridSizes")).toArray()).toJson(QJsonDocument::Compact)));

		// A Quake brush off the grid snaps its lower corner onto 0.25.
		QString error;
		LevelMapDocument quake;
		LevelMapCreateRequest request;
		request.game = QStringLiteral("quake");
		request.starterRoom = false;
		const QString quakePath = temp.filePath(QStringLiteral("fraction.map"));
		const QString snappedPath = temp.filePath(QStringLiteral("fraction-snapped.map"));
		expect(createLevelMap(request, &quake, &error)
				&& addLevelMapBoxBrush(&quake, {0.3, 0.3, 0, true}, {32.3, 32.3, 32, true}, QStringLiteral("studio/cache"), nullptr, &error)
				&& saveLevelMapAs(quake, quakePath).succeeded(),
			"quake fixture", error);
		const QJsonObject snapped = cli({QStringLiteral("map"), QStringLiteral("snap"), quakePath, QStringLiteral("--object"), QStringLiteral("brush:0"),
										   QStringLiteral("--grid"), QStringLiteral("0.25"), QStringLiteral("--output"), snappedPath}, true);
		LevelMapDocument reloaded;
		expect(snapped.value(QStringLiteral("snapped")).toArray() == QJsonArray{QStringLiteral("brush:0")} && loadLevelMap({snappedPath, {}, {}}, &reloaded, &error)
				&& !reloaded.brushes.isEmpty() && std::abs(reloaded.brushes.first().mins.x - 0.25) < 1e-9 && std::abs(reloaded.brushes.first().mins.y - 0.25) < 1e-9,
			"map snap --grid 0.25 lands the brush on a quarter-unit grid", error);

		// A binary Doom map refuses a fractional grid and publishes nothing.
		LevelMapDocument doom;
		request.game = QStringLiteral("doom");
		request.starterRoom = true;
		const QString doomPath = temp.filePath(QStringLiteral("fraction.wad"));
		const QString doomOut = temp.filePath(QStringLiteral("fraction-snapped.wad"));
		expect(createLevelMap(request, &doom, &error) && !doom.doomThings.isEmpty() && saveLevelMapAs(doom, doomPath).succeeded(), "doom fixture", error);
		const QByteArray doomBefore = fileBytes(doomPath);
		cli({QStringLiteral("map"), QStringLiteral("snap"), doomPath, QStringLiteral("--object"), QStringLiteral("thing:0"), QStringLiteral("--grid"),
				QStringLiteral("0.5"), QStringLiteral("--output"), doomOut},
			false);
		expect(!QFile::exists(doomOut) && fileBytes(doomPath) == doomBefore, "a fractional grid on a binary Doom map writes nothing");

		// The 2D export takes the working grid's fractional size too.
		const QString svgPath = temp.filePath(QStringLiteral("fraction.svg"));
		cli({QStringLiteral("map"), QStringLiteral("render"), quakePath, QStringLiteral("--grid"), QStringLiteral("0.5"), QStringLiteral("--width"),
				QStringLiteral("256"), QStringLiteral("--height"), QStringLiteral("256"), QStringLiteral("--output"), svgPath},
			true);
		expect(QString::fromUtf8(fileBytes(svgPath)).contains(QStringLiteral("id=\"vs-grid\"")), "map render accepts --grid 0.5");
	}

	if (!ok) {
		std::cerr << "level fractional grid smoke test failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "level fractional grid smoke test passed\n";
	return EXIT_SUCCESS;
}
