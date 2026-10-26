// The compiler block grid (core/level_block_grid.h): worldspawn _blocksize
// read as q3map2 reads it (one number for every axis, three for x, y and z,
// the legacy two-number form, 0 or negative turning an axis off, the
// blocksize and chopsize aliases, case-insensitive keys, the 1024 default),
// block boundaries, the message texts, the SVG picture's dashed block grid in
// each projection, and `map render --block-grid` in JSON and text.

#include "core/level_block_grid.h"
#include "core/level_map.h"
#include "core/map_render.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <array>
#include <climits>
#include <cmath>
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
		std::cerr << "FAIL: " << label;
		if (!detail.isEmpty()) {
			std::cerr << ": " << qPrintable(detail);
		}
		std::cerr << std::endl;
	}
	return value;
}

using Axes = std::array<int, 3>;

QString axesText(const LevelMapBlockSize& size)
{
	return QStringLiteral("%1 %2 %3 (%4 numbers)").arg(size.axes[0]).arg(size.axes[1]).arg(size.axes[2]).arg(size.numbers);
}

QByteArray mapText(const QByteArray& worldspawnKeys)
{
	// A 2048 x 2048 x 128 slab centred on the origin, so every projection
	// frames x and y from -1074 to 1074 units and the block lines can be
	// counted by hand.
	return QByteArrayLiteral("{\n\"classname\" \"worldspawn\"\n") + worldspawnKeys
		+ QByteArrayLiteral("{\n"
							"( -1024 -1024 -64 ) ( -1024 -1023 -64 ) ( -1024 -1024 -63 ) base/wall 0 0 0 1 1 0 0 0\n"
							"( 1024 -1024 -64 ) ( 1024 -1024 -63 ) ( 1024 -1023 -64 ) base/wall 0 0 0 1 1 0 0 0\n"
							"( -1024 -1024 -64 ) ( -1024 -1024 -63 ) ( -1023 -1024 -64 ) base/wall 0 0 0 1 1 0 0 0\n"
							"( -1024 1024 -64 ) ( -1023 1024 -64 ) ( -1024 1024 -63 ) base/wall 0 0 0 1 1 0 0 0\n"
							"( -1024 -1024 -64 ) ( -1023 -1024 -64 ) ( -1024 -1023 -64 ) base/wall 0 0 0 1 1 0 0 0\n"
							"( -1024 -1024 64 ) ( -1024 -1023 64 ) ( -1023 -1024 64 ) base/wall 0 0 0 1 1 0 0 0\n"
							"}\n}\n");
}

LevelMapDocument load(const QByteArray& bytes, const char* engine = "idtech3")
{
	LevelMapLoadRequest request;
	request.path = QStringLiteral("blocks.map");
	request.engineHint = QString::fromLatin1(engine);
	LevelMapDocument document;
	QString error;
	expect(loadLevelMapBytes(request, bytes, &document, &error), "fixture loads", error);
	return document;
}

void parsing()
{
	const std::pair<const char*, Axes> cases[] = {
		{"512", {512, 512, 512}},
		{"1024 1024 0", {1024, 1024, 0}},
		{"  -64\t128 2048", {0, 128, 2048}},
		{"256 512", {256, 256, 256}},
		{"+2048", {2048, 2048, 2048}},
		{"1024x 512", {1024, 1024, 1024}},
		{"1024-512 8", {1024, 0, 8}},
		{"abc", {1024, 1024, 1024}},
		{"", {1024, 1024, 1024}},
		{"0", {0, 0, 0}},
		{"99999999999999 1 1", {INT_MAX, 1, 1}},
	};
	for (const auto& [text, axes] : cases) {
		const LevelMapBlockSize size = levelBlockSizeFromValue(QString::fromLatin1(text));
		expect(size.axes == axes && size.value == QString::fromLatin1(text), "value read as q3map2's sscanf reads it",
			QString::fromLatin1(text) + QStringLiteral(" -> ") + axesText(size));
	}
	expect(levelBlockSizeFromValue(QStringLiteral("abc")).numbers == 0 && levelBlockSizeFromValue(QStringLiteral("256 512")).numbers == 2
			&& levelBlockSizeFromValue(QStringLiteral("1 2 3 4")).numbers == 3,
		"numbers read are counted");
	const LevelMapBlockSize off = levelBlockSizeFromValue(QStringLiteral("0 0 -1"));
	expect(!off.anyAxis() && off.along(0) == 0 && off.along(3) == 0 && off.along(-1) == 0, "every axis off");
	expect(LevelMapBlockSize {}.anyAxis() && LevelMapBlockSize {}.along(2) == kLevelBlockSizeDefault, "default splits every 1024 units");
}

void documents()
{
	struct Case {
		QByteArray keys;
		Axes axes;
		const char* key;
	};
	const Case cases[] = {
		{"", {1024, 1024, 1024}, ""},
		{"\"_blocksize\" \"512 512 0\"\n", {512, 512, 0}, "_blocksize"},
		{"\"_BlockSize\" \"2048\"\n", {2048, 2048, 2048}, "_BlockSize"},
		{"\"blocksize\" \"256 0 0\"\n", {256, 0, 0}, "blocksize"},
		{"\"chopsize\" \"4096\"\n", {4096, 4096, 4096}, "chopsize"},
		// An empty _blocksize gives way to the next name, as q3map2 reads it.
		{"\"_blocksize\" \"\"\n\"chopsize\" \"768\"\n", {768, 768, 768}, "chopsize"},
		// _blocksize wins over the aliases wherever it is written.
		{"\"chopsize\" \"768\"\n\"_blocksize\" \"1536\"\n", {1536, 1536, 1536}, "_blocksize"},
		// A key with no number keeps the default but still names its source.
		{"\"_blocksize\" \"none\"\n", {1024, 1024, 1024}, "_blocksize"},
	};
	for (const Case& entry : cases) {
		const LevelMapDocument document = load(mapText(entry.keys));
		const LevelMapBlockSize size = levelMapBlockSize(document);
		expect(levelMapHasBlockGrid(document) && size.axes == entry.axes && size.key == QString::fromLatin1(entry.key),
			"worldspawn block size", QString::fromUtf8(entry.keys) + QStringLiteral(" -> ") + axesText(size) + QLatin1Char(' ') + size.key);
	}
	const LevelMapDocument quake = load(mapText("\"_blocksize\" \"512\"\n"), "idtech2");
	expect(quake.format == LevelMapFormat::QuakeMap && levelMapHasBlockGrid(quake) && levelMapBlockSize(quake).along(1) == 512,
		"Quake and Quake II maps read the key too");
	LevelMapDocument doom;
	doom.format = LevelMapFormat::DoomWad;
	LevelMapEntity world;
	world.className = QStringLiteral("worldspawn");
	world.properties.append({QStringLiteral("_blocksize"), QStringLiteral("256"), 0});
	doom.entities.append(world);
	expect(!levelMapHasBlockGrid(doom) && levelMapBlockSize(doom) == LevelMapBlockSize {}, "Doom maps have no block grid");
	expect(levelMapBlockSize(LevelMapDocument {}) == LevelMapBlockSize {}, "no map reports the default");
}

void boundaries()
{
	expect(levelBlockBoundaries(-1500, 2100, 1024) == QVector<double> {-1024, 0, 1024, 2048}, "multiples inside the range");
	expect(levelBlockBoundaries(-1024, 1024, 1024) == QVector<double> {-1024, 0, 1024}, "ends are inclusive");
	expect(levelBlockBoundaries(-0.5, 0.5, 256) == QVector<double> {0} && !std::signbit(levelBlockBoundaries(-0.5, 0.5, 256).first()), "never -0");
	expect(levelBlockBoundaries(1, 1000, 1024).isEmpty(), "no boundary inside");
	expect(levelBlockBoundaries(-4096, 4096, 0).isEmpty() && levelBlockBoundaries(-4096, 4096, -64).isEmpty(), "an unsplit axis has none");
	expect(levelBlockBoundaries(5, -5, 1).isEmpty() && levelBlockBoundaries(0, qInf(), 256).isEmpty(), "bad ranges have none");
	bool truncated = false;
	expect(levelBlockBoundaries(-1e9, 1e9, 256, 4096, &truncated).isEmpty() && truncated, "too many boundaries draw none and say so");
	expect(levelBlockBoundaries(-512, 512, 256, 5, &truncated).size() == 5 && !truncated, "exactly at the limit");
	expect(levelBlockBoundaries(1e20, 1e20, 1) == QVector<double> {1e20}, "extreme finite coordinates terminate");
}

void texts()
{
	const LevelMapBlockSize mixed = levelBlockSizeFromValue(QStringLiteral("1024 512 0"));
	const QString text = levelBlockSizeText(mixed);
	expect(text.contains(QStringLiteral("1024")) && text.contains(QStringLiteral("512")) && text.contains(QStringLiteral("off"))
			&& text.contains(QChar(0x00d7)),
		"size text names each axis", text);
	expect(levelBlockSizeSourceText(LevelMapBlockSize {}).contains(QStringLiteral("default")), "default source");
	LevelMapBlockSize keyed = levelBlockSizeFromValue(QStringLiteral("512 512 0"));
	keyed.key = QStringLiteral("_blocksize");
	expect(levelBlockSizeSourceText(keyed).contains(QStringLiteral("_blocksize")) && levelBlockSizeSourceText(keyed).contains(QStringLiteral("512 512 0")),
		"keyed source", levelBlockSizeSourceText(keyed));
	LevelMapBlockSize legacy = levelBlockSizeFromValue(QStringLiteral("256 512"));
	legacy.key = QStringLiteral("_blocksize");
	expect(levelBlockSizeSourceText(legacy).contains(QStringLiteral("ignores")), "a lone second number is called out", levelBlockSizeSourceText(legacy));
	LevelMapBlockSize unreadable = levelBlockSizeFromValue(QStringLiteral("none"));
	unreadable.key = QStringLiteral("_blocksize");
	expect(levelBlockSizeSourceText(unreadable).contains(QStringLiteral("default")), "an unreadable key keeps the default", levelBlockSizeSourceText(unreadable));
}

int blockLines(const QString& svg)
{
	const qsizetype start = svg.indexOf(QStringLiteral("<g id=\"vs-block-grid\">"));
	if (start < 0) {
		return 0;
	}
	const qsizetype end = svg.indexOf(QStringLiteral("</g>"), start);
	return static_cast<int>(svg.mid(start, end - start).count(QStringLiteral("<line ")));
}

void pictures()
{
	const LevelMapDocument document = load(mapText("\"_blocksize\" \"512 512 0\"\n"));
	MapRenderOptions plain;
	MapRenderReport report;
	const QString before = renderLevelMapSvg(document, plain, &report);
	expect(!before.contains(QStringLiteral("vs-block-grid")) && report.drawnBlockLineCount == 0, "off by default");
	MapRenderOptions blocks = plain;
	blocks.showBlockGrid = true;
	const std::pair<MapRenderProjection, int> projections[] = {
		// x and y split every 512 units from -1024 to 1024: five lines each.
		{MapRenderProjection::TopXY, 10},
		// z is not split, so the front and side pictures have upright lines only.
		{MapRenderProjection::FrontXZ, 5},
		{MapRenderProjection::SideZY, 5},
	};
	for (const auto& [projection, lines] : projections) {
		blocks.projection = projection;
		MapRenderReport drawn;
		const QString svg = renderLevelMapSvg(document, blocks, &drawn);
		expect(drawn.drawnBlockLineCount == lines && blockLines(svg) == lines && svg.contains(QStringLiteral("stroke-dasharray=\"8 4\"")),
			"dashed block lines in the picture", mapRenderProjectionId(projection) + QStringLiteral(": %1/%2").arg(drawn.drawnBlockLineCount).arg(blockLines(svg)));
		expect(mapRenderReportText(drawn).contains(QStringLiteral("Block grid lines: %1").arg(lines)), "text report counts block lines");
	}
	blocks.projection = MapRenderProjection::TopXY;
	MapRenderReport coarse;
	renderLevelMapSvg(load(mapText("\"_blocksize\" \"1024\"\n")), blocks, &coarse);
	expect(coarse.drawnBlockLineCount == 6, "the default-sized grid", QString::number(coarse.drawnBlockLineCount));
	MapRenderReport none;
	const QString unsplit = renderLevelMapSvg(load(mapText("\"_blocksize\" \"0\"\n")), blocks, &none);
	expect(none.drawnBlockLineCount == 0 && !unsplit.contains(QStringLiteral("vs-block-grid")) && none.warnings.isEmpty(), "every axis off draws nothing");
	MapRenderOptions off = plain;
	off.showBlockGrid = false;
	expect(renderLevelMapSvg(document, off) == before, "pictures without the block grid are unchanged");
	LevelMapDocument doom;
	doom.format = LevelMapFormat::DoomWad;
	MapRenderReport doomReport;
	const QString doomSvg = renderLevelMapSvg(doom, blocks, &doomReport);
	expect(doomReport.drawnBlockLineCount == 0 && !doomSvg.contains(QStringLiteral("vs-block-grid"))
			&& doomReport.warnings.join(QLatin1Char('\n')).contains(QStringLiteral("block grid")),
		"Doom maps skip the block grid with a warning", doomReport.warnings.join(QLatin1Char('\n')));
}

void cli(const QString& executable, const QTemporaryDir& temp)
{
	const QString path = temp.filePath(QStringLiteral("blocks.map"));
	QFile file(path);
	const QByteArray bytes = mapText("\"_blocksize\" \"512 512 0\"\n");
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "CLI fixture")) {
		return;
	}
	file.close();
	const QString settingsPath = temp.filePath(QStringLiteral("cli-settings.ini"));
	const auto run = [&](const QStringList& args, bool json = true) {
		QProcess process;
		process.setWorkingDirectory(temp.path());
		QStringList command {QStringLiteral("--cli")};
		if (json) {
			command << QStringLiteral("--json");
		}
		command << QStringLiteral("--settings-file") << settingsPath << QStringLiteral("map") << QStringLiteral("render") << path;
		process.start(executable, command + args);
		const bool done = process.waitForStarted() && process.waitForFinished(60000);
		const QByteArray out = process.readAllStandardOutput();
		const QByteArray err = process.readAllStandardError();
		expect(done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0, "CLI exit status",
			args.join(QLatin1Char(' ')) + QLatin1String(" -> ") + QString::fromUtf8(out + err));
		if (!done) {
			process.kill();
			process.waitForFinished();
		}
		return std::make_pair(QJsonDocument::fromJson(out).object(), QString::fromUtf8(out + err));
	};
	auto [report, text] = run({QStringLiteral("--block-grid")});
	QJsonObject grid = report.value(QStringLiteral("blockGrid")).toObject();
	expect(grid.value(QStringLiteral("size")).toArray() == QJsonArray {512, 512, 0} && grid.value(QStringLiteral("key")) == QStringLiteral("_blocksize")
			&& grid.value(QStringLiteral("value")) == QStringLiteral("512 512 0") && grid.value(QStringLiteral("numbersRead")) == 3
			&& grid.value(QStringLiteral("available")) == true && grid.value(QStringLiteral("drawnLines")) == 10
			&& report.value(QStringLiteral("svg")).toString().contains(QStringLiteral("vs-block-grid")),
		"JSON reports the block grid it drew", text);
	std::tie(report, text) = run({});
	expect(!report.contains(QStringLiteral("blockGrid")) && !report.value(QStringLiteral("svg")).toString().contains(QStringLiteral("vs-block-grid")),
		"no block grid unless asked", text);
	std::tie(report, text) = run({QStringLiteral("--block-grid"), QStringLiteral("--projection"), QStringLiteral("front")});
	expect(report.value(QStringLiteral("blockGrid")).toObject().value(QStringLiteral("drawnLines")) == 5, "front projection", text);
	const QString output = temp.filePath(QStringLiteral("blocks.svg"));
	std::tie(report, text) = run({QStringLiteral("--block-grid"), QStringLiteral("--output"), output});
	QFile written(output);
	expect(report.value(QStringLiteral("blockGrid")).toObject().value(QStringLiteral("drawnLines")) == 10 && written.open(QIODevice::ReadOnly)
			&& written.readAll().contains("vs-block-grid"),
		"written picture holds the block grid", text);
	written.close();
	std::tie(report, text) = run({QStringLiteral("--block-grid"), QStringLiteral("--output"), output, QStringLiteral("--overwrite")}, false);
	expect(text.contains(QStringLiteral("Block grid lines: 10")), "text report", text);
	QFile source(path);
	expect(source.open(QIODevice::ReadOnly) && source.readAll() == bytes, "the map is never written");
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	parsing();
	documents();
	boundaries();
	texts();
	pictures();
	if (argc == 2) {
		cli(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath(), temp);
	}
	if (ok) {
		std::cout << "Level block grid: key parsing, aliases, boundaries, texts, pictures" << (argc == 2 ? " and CLI" : "") << " passed." << std::endl;
	}
	return ok ? 0 : 1;
}
