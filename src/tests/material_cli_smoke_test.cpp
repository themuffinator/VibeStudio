// The `material` CLI family end to end: list, inspect, validate, render,
// graph, edit, templates, new, doom-tables and wal, run as a separate
// process against packages generated here (no game data).

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>

#include <iostream>

namespace {

int failures = 0;

bool expect(bool condition, const std::string& message, const QString& detail = QString())
{
	if (!condition) {
		std::cerr << "FAIL: " << message;
		if (!detail.isEmpty()) {
			std::cerr << "\n  " << detail.toStdString();
		}
		std::cerr << '\n';
		++failures;
	}
	return condition;
}

void writeFile(const QString& path, const QByteArray& bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(bytes);
	}
}

QByteArray readFile(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QImage checker(int size, QRgb a, QRgb b, int cell)
{
	QImage image(size, size, QImage::Format_ARGB32);
	for (int y = 0; y < size; ++y) {
		for (int x = 0; x < size; ++x) {
			image.setPixel(x, y, ((x / cell + y / cell) % 2) == 0 ? a : b);
		}
	}
	return image;
}

// Uncompressed 32-bit TGA, top-left origin.
QByteArray tga(const QImage& source)
{
	const QImage image = source.convertToFormat(QImage::Format_ARGB32);
	QByteArray bytes(18, '\0');
	bytes[2] = 2;
	qToLittleEndian<quint16>(static_cast<quint16>(image.width()), bytes.data() + 12);
	qToLittleEndian<quint16>(static_cast<quint16>(image.height()), bytes.data() + 14);
	bytes[16] = 32;
	bytes[17] = 0x28;
	for (int y = 0; y < image.height(); ++y) {
		for (int x = 0; x < image.width(); ++x) {
			const QRgb pixel = image.pixel(x, y);
			bytes.append(static_cast<char>(qBlue(pixel)));
			bytes.append(static_cast<char>(qGreen(pixel)));
			bytes.append(static_cast<char>(qRed(pixel)));
			bytes.append(static_cast<char>(qAlpha(pixel)));
		}
	}
	return bytes;
}

struct Lump {
	QByteArray name;
	QByteArray data;
};

QByteArray doomWad(const QVector<Lump>& lumps)
{
	QByteArray body;
	QByteArray directory;
	int offset = 12;
	for (const Lump& lump : lumps) {
		QByteArray entry(16, '\0');
		qToLittleEndian<qint32>(lump.data.isEmpty() ? 0 : offset, entry.data());
		qToLittleEndian<qint32>(static_cast<qint32>(lump.data.size()), entry.data() + 4);
		for (int i = 0; i < 8 && i < lump.name.size(); ++i) {
			entry[8 + i] = lump.name.at(i);
		}
		directory.append(entry);
		body.append(lump.data);
		offset += static_cast<int>(lump.data.size());
	}
	QByteArray header("PWAD", 4);
	header.resize(12);
	qToLittleEndian<qint32>(static_cast<qint32>(lumps.size()), header.data() + 4);
	qToLittleEndian<qint32>(offset, header.data() + 8);
	return header + body + directory;
}

QByteArray doomPatch(int width, int height, int index)
{
	QByteArray bytes(8 + 4 * width, '\0');
	qToLittleEndian<qint16>(static_cast<qint16>(width), bytes.data());
	qToLittleEndian<qint16>(static_cast<qint16>(height), bytes.data() + 2);
	for (int x = 0; x < width; ++x) {
		qToLittleEndian<quint32>(static_cast<quint32>(bytes.size()), bytes.data() + 8 + 4 * x);
		bytes.append(static_cast<char>(0));
		bytes.append(static_cast<char>(height));
		bytes.append(static_cast<char>(0));
		for (int y = 0; y < height; ++y) {
			bytes.append(static_cast<char>(index + (y / 8) % 2));
		}
		bytes.append(static_cast<char>(0));
		bytes.append(static_cast<char>(0xFF));
	}
	return bytes;
}

QByteArray doomFixture()
{
	QByteArray palette;
	for (int index = 0; index < 256; ++index) {
		palette.append(static_cast<char>(index));
		palette.append(static_cast<char>((index * 7) % 256));
		palette.append(static_cast<char>(255 - index));
	}
	QByteArray colormap;
	for (int level = 0; level < 34; ++level) {
		for (int index = 0; index < 256; ++index) {
			colormap.append(static_cast<char>(level < 32 ? std::max(0, index - level * 4) : index));
		}
	}
	QByteArray pnames(4, '\0');
	qToLittleEndian<qint32>(1, pnames.data());
	pnames.append(QByteArray("WALLPAT\0", 8));
	QByteArray texture1(8, '\0');
	qToLittleEndian<qint32>(1, texture1.data());
	qToLittleEndian<qint32>(8, texture1.data() + 4);
	QByteArray record(22 + 10, '\0');
	record.replace(0, 8, QByteArray("TESTWALL", 8));
	qToLittleEndian<qint16>(64, record.data() + 12);
	qToLittleEndian<qint16>(128, record.data() + 14);
	qToLittleEndian<qint16>(1, record.data() + 20);
	qToLittleEndian<qint16>(1, record.data() + 22 + 6);
	texture1.append(record);
	const auto flat = [](int index) { return QByteArray(64 * 64, static_cast<char>(index)); };
	return doomWad({{"PLAYPAL", palette.repeated(14)}, {"COLORMAP", colormap}, {"PNAMES", pnames}, {"TEXTURE1", texture1}, {"P_START", {}},
		{"WALLPAT", doomPatch(64, 128, 40)}, {"P_END", {}}, {"F_START", {}}, {"NUKAGE1", flat(100)}, {"NUKAGE2", flat(140)},
		{"NUKAGE3", flat(180)}, {"F_END", {}}});
}

QByteArray quake2Wal(const QByteArray& name, const QByteArray& next, quint32 flags, qint32 value)
{
	QByteArray bytes(100, '\0');
	bytes.replace(0, name.size(), name);
	qToLittleEndian<quint32>(32, bytes.data() + 32);
	qToLittleEndian<quint32>(32, bytes.data() + 36);
	quint32 offset = 100;
	for (int level = 0; level < 4; ++level) {
		qToLittleEndian<quint32>(offset, bytes.data() + 40 + 4 * level);
		offset += static_cast<quint32>((32 >> level) * (32 >> level));
	}
	bytes.replace(56, next.size(), next);
	qToLittleEndian<quint32>(flags, bytes.data() + 88);
	qToLittleEndian<qint32>(value, bytes.data() + 96);
	for (int level = 0; level < 4; ++level) {
		const int side = 32 >> level;
		for (int i = 0; i < side * side; ++i) {
			bytes.append(static_cast<char>((i % side + i / side) % 256));
		}
	}
	return bytes;
}

const char* kShaders = R"(// CLI fixture
textures/test/wall
{
	{
		map $lightmap
	}
	{
		map textures/test/wall.tga
		blendFunc filter
	}
}

textures/test/glow
{
	surfaceparm nolightmap
	{
		map textures/test/glow.tga
		tcMod scroll 0.5 0
		rgbGen wave sin 0.5 0.5 0 1
	}
}

textures/test/anim
{
	{
		animMap 4 textures/test/anim0.tga textures/test/anim1.tga
	}
}

textures/test/missing
{
	{
		map textures/test/not_here.tga
	}
}

textures/test/bad
{
	{
		map textures/test/wall.tga
		frobnicate 1
	}
}
)";

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid() || argc != 2) {
		std::cerr << "usage: material_cli_smoke_test <vibestudio>\n";
		return 1;
	}
	const QString cli = QString::fromLocal8Bit(argv[1]);
	const QDir root(temp.path());
	int lastExit = 0;
	QString lastOutput;
	const auto run = [&](const QStringList& arguments) {
		QProcess process;
		process.setWorkingDirectory(temp.path());
		process.start(cli, QStringList {QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--settings-file"),
							   root.filePath(QStringLiteral("cli.ini")), QStringLiteral("material")}
				+ arguments);
		const bool ended = process.waitForStarted() && process.waitForFinished(120000);
		if (!ended) {
			process.kill();
			process.waitForFinished();
		}
		const QByteArray out = process.readAllStandardOutput();
		lastOutput = QString::fromUtf8(out + process.readAllStandardError());
		lastExit = ended && process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
		return QJsonDocument::fromJson(out).object();
	};

	// Fixtures.
	const QString q3 = root.filePath(QStringLiteral("q3"));
	writeFile(q3 + QStringLiteral("/textures/test/wall.tga"), tga(checker(32, qRgb(180, 60, 40), qRgb(90, 30, 20), 8)));
	writeFile(q3 + QStringLiteral("/textures/test/glow.tga"), tga(checker(32, qRgb(255, 220, 120), qRgb(0, 0, 0), 8)));
	writeFile(q3 + QStringLiteral("/textures/test/anim0.tga"), tga(checker(32, qRgb(255, 0, 0), qRgb(255, 0, 0), 8)));
	writeFile(q3 + QStringLiteral("/textures/test/anim1.tga"), tga(checker(32, qRgb(0, 0, 255), qRgb(0, 0, 255), 8)));
	writeFile(q3 + QStringLiteral("/scripts/test.shader"), QByteArray(kShaders));
	const QString wad = root.filePath(QStringLiteral("test.wad"));
	writeFile(wad, doomFixture());
	const QString wal = root.filePath(QStringLiteral("q2/textures/e1u1/anim_1.wal"));
	writeFile(wal, quake2Wal("e1u1/anim_1", "e1u1/anim_2", 0x1, 300));
	writeFile(root.filePath(QStringLiteral("q2/textures/e1u1/anim_2.wal")), quake2Wal("e1u1/anim_2", "e1u1/anim_1", 0x1, 300));

	// list
	QJsonObject result = run({QStringLiteral("list"), q3});
	QStringList names;
	for (const QJsonValue& entry : result.value(QStringLiteral("entries")).toArray()) {
		names << entry.toObject().value(QStringLiteral("name")).toString();
	}
	expect(lastExit == 0 && names.contains(QStringLiteral("textures/test/glow")) && names.contains(QStringLiteral("textures/test/bad")),
		"material list reads every shader", lastOutput);
	result = run({QStringLiteral("list"), q3, QStringLiteral("--where"), QStringLiteral("animated=yes kind=shader")});
	names.clear();
	for (const QJsonValue& entry : result.value(QStringLiteral("entries")).toArray()) {
		names << entry.toObject().value(QStringLiteral("name")).toString();
	}
	names.sort();
	expect(names == QStringList {QStringLiteral("textures/test/anim"), QStringLiteral("textures/test/glow")}, "--where keeps the animated shaders",
		names.join(QLatin1Char(',')));
	run({QStringLiteral("list"), q3, QStringLiteral("--where"), QStringLiteral("nosuchkey=1")});
	expect(lastExit == 2, "a --where key no material has is a usage error", lastOutput);
	result = run({QStringLiteral("list"), wad});
	expect(lastExit == 0 && result.value(QStringLiteral("matched")).toInt() == 4, "material list reads Doom walls and flats", lastOutput);

	// inspect
	result = run({QStringLiteral("inspect"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/glow")});
	const QJsonArray images = result.value(QStringLiteral("images")).toArray();
	expect(lastExit == 0 && images.size() == 1 && images.first().toObject().value(QStringLiteral("status")).toString() == QStringLiteral("image")
			&& result.value(QStringLiteral("text")).toString().contains(QStringLiteral("tcMod scroll")),
		"material inspect resolves the image and shows the text", lastOutput);
	run({QStringLiteral("inspect"), q3, QStringLiteral("--material"), QStringLiteral("textures/none")});
	expect(lastExit == 3, "inspecting a material that is not there is not-found", lastOutput);
	run({QStringLiteral("inspect"), q3});
	expect(lastExit == 2, "inspect without --material on a package of many is a usage error", lastOutput);

	// validate
	result = run({QStringLiteral("validate"), q3});
	QStringList codes;
	for (const QJsonValue& problem : result.value(QStringLiteral("problems")).toArray()) {
		codes << problem.toObject().value(QStringLiteral("code")).toString();
	}
	expect(lastExit == 4 && codes.contains(QStringLiteral("missing-image")) && codes.contains(QStringLiteral("engine-rejects")),
		"material validate reports the missing image and the rejected shader", lastOutput);
	run({QStringLiteral("validate"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/glow")});
	expect(lastExit == 0, "a clean material validates", lastOutput);

	// render
	const QString sheet = root.filePath(QStringLiteral("anim.png"));
	result = run({QStringLiteral("render"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/anim"), QStringLiteral("--frames"),
		QStringLiteral("2"), QStringLiteral("--fps"), QStringLiteral("4"), QStringLiteral("--columns"), QStringLiteral("2"), QStringLiteral("--size"),
		QStringLiteral("64x64"), QStringLiteral("--orthographic"), QStringLiteral("--tiling"), QStringLiteral("1"), QStringLiteral("--output"), sheet});
	if (lastExit == 5) {
		// No OpenGL or Vulkan for the CLI here: the command says why and
		// writes nothing; the drawing checks need a renderer.
		expect(!QFile::exists(sheet) && lastOutput.contains(QStringLiteral("could not be drawn")),
			"material render without a 3D renderer says why and writes nothing", lastOutput);
		expect(qEnvironmentVariableIntValue("VIBESTUDIO_RENDER_REQUIRE") == 0, "VIBESTUDIO_RENDER_REQUIRE is set, but material render has no 3D renderer",
			lastOutput);
		std::cout << "No 3D renderer starts for the CLI here: material render drawing checks skipped.\n";
	} else {
		const QImage rendered(sheet);
		expect(lastExit == 0 && rendered.size() == QSize(128, 64), "material render writes a two-frame sheet", lastOutput);
		expect(!result.value(QStringLiteral("renderer")).toString().isEmpty(), "material render names the renderer that drew it", lastOutput);
		if (rendered.size() == QSize(128, 64)) {
			const QRgb first = rendered.pixel(32, 32);
			const QRgb second = rendered.pixel(96, 32);
			expect(qRed(first) > qBlue(first) && qBlue(second) > qRed(second), "animMap frames follow time in the sheet");
		}
		run({QStringLiteral("render"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/anim"), QStringLiteral("--output"), sheet});
		expect(lastExit == 1, "render will not replace a file without --overwrite", lastOutput);
		const QString dry = root.filePath(QStringLiteral("dry.png"));
		result = run({QStringLiteral("render"), wad, QStringLiteral("--material"), QStringLiteral("NUKAGE1"), QStringLiteral("--output"), dry,
			QStringLiteral("--dry-run")});
		expect(lastExit == 0 && !QFile::exists(dry) && !result.value(QStringLiteral("written")).toBool()
				&& result.value(QStringLiteral("shape")).toString() == QStringLiteral("floor"),
			"a dry render writes nothing and puts flats on the floor", lastOutput);
		result = run({QStringLiteral("render"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/bad"), QStringLiteral("--dry-run")});
		expect(result.value(QStringLiteral("fallback")).toBool(), "a shader Quake III drops renders as its fallback", lastOutput);
	}
	run({QStringLiteral("render"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/anim"), QStringLiteral("--renderer"),
		QStringLiteral("software"), QStringLiteral("--dry-run")});
	expect(lastExit == 2, "material render refuses an unknown --renderer", lastOutput);

	// graph
	result = run({QStringLiteral("graph"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/glow")});
	QStringList nodes;
	for (const QJsonValue& node : result.value(QStringLiteral("graph")).toObject().value(QStringLiteral("nodes")).toArray()) {
		nodes << node.toObject().value(QStringLiteral("id")).toString();
	}
	expect(nodes.contains(QStringLiteral("stage/0/tcmod/0")) && nodes.contains(QStringLiteral("material")), "material graph prints the nodes",
		nodes.join(QLatin1Char(',')));
	const QString graphEdits = root.filePath(QStringLiteral("graph.json"));
	writeFile(graphEdits, R"([{"edit": "set", "node": "stage/0/rgbgen", "property": "frequency", "value": "2"}])");
	const QString graphOut = root.filePath(QStringLiteral("graph.shader"));
	run({QStringLiteral("graph"), q3, QStringLiteral("--material"), QStringLiteral("textures/test/glow"), QStringLiteral("--edits"), graphEdits,
		QStringLiteral("--output"), graphOut});
	expect(lastExit == 0 && readFile(graphOut).contains("rgbGen wave sin 0.5 0.5 0 2"), "a graph edit rewrites the shader text", lastOutput);

	// edit
	const QString script = root.filePath(QStringLiteral("edit.shader"));
	writeFile(script, QByteArray(kShaders));
	const QString textEdits = root.filePath(QStringLiteral("edits.json"));
	writeFile(textEdits, R"([{"op": "set", "material": "textures/test/glow", "stage": 1, "keyword": "tcMod", "arguments": "scroll 1 0.25"}])");
	result = run({QStringLiteral("edit"), script, QStringLiteral("--edits"), textEdits, QStringLiteral("--dry-run")});
	expect(lastExit == 0 && result.value(QStringLiteral("text")).toString().contains(QStringLiteral("tcMod scroll 1 0.25"))
			&& readFile(script) == QByteArray(kShaders),
		"a dry edit shows the text and leaves the script alone", lastOutput);
	run({QStringLiteral("edit"), script, QStringLiteral("--edits"), textEdits, QStringLiteral("--output"), script, QStringLiteral("--overwrite")});
	expect(lastExit == 0 && readFile(script).contains("tcMod scroll 1 0.25") && readFile(script).contains("// CLI fixture"),
		"an edit in place keeps the rest of the script", lastOutput);

	// templates and new
	result = run({QStringLiteral("templates"), QStringLiteral("--engine"), QStringLiteral("doom3")});
	QStringList templates;
	for (const QJsonValue& each : result.value(QStringLiteral("templates")).toArray()) {
		templates << each.toObject().value(QStringLiteral("id")).toString();
	}
	expect(templates.contains(QStringLiteral("d3-standard")) && !templates.contains(QStringLiteral("q3-glow")), "templates filter by engine",
		templates.join(QLatin1Char(',')));
	run({QStringLiteral("new"), QStringLiteral("--template"), QStringLiteral("q3-glow"), QStringLiteral("--name"), QStringLiteral("textures/test/lamp"),
		QStringLiteral("--output"), script, QStringLiteral("--append")});
	expect(lastExit == 0 && readFile(script).contains("textures/test/lamp"), "new --append adds the material", lastOutput);
	run({QStringLiteral("new"), QStringLiteral("--template"), QStringLiteral("q3-glow"), QStringLiteral("--name"), QStringLiteral("textures/test/lamp"),
		QStringLiteral("--output"), script, QStringLiteral("--append")});
	expect(lastExit == 4, "new --append refuses a name the script already defines", lastOutput);

	// doom-tables
	const QString lumps = root.filePath(QStringLiteral("lumps"));
	result = run({QStringLiteral("doom-tables"), wad, QStringLiteral("--output-dir"), lumps});
	expect(lastExit == 0 && result.value(QStringLiteral("ranges")).toArray().size() == 22 && QFile::exists(lumps + QStringLiteral("/ANIMATED.lmp"))
			&& QFile::exists(lumps + QStringLiteral("/SWITCHES.lmp")),
		"doom-tables writes the Boom lumps from the vanilla tables", lastOutput);
	expect(QString::fromUtf8(readFile(lumps + QStringLiteral("/SWANTBLS.txt"))).simplified().contains(QStringLiteral("8 NUKAGE3 NUKAGE1")),
		"SWANTBLS text uses Boom's speed last first columns");
	result = run({QStringLiteral("doom-tables"), lumps + QStringLiteral("/SWANTBLS.txt")});
	expect(lastExit == 0 && result.value(QStringLiteral("ranges")).toArray().size() == 22
			&& result.value(QStringLiteral("switches")).toArray().size() == 40,
		"SWANTBLS text reads back", lastOutput);

	// wal
	const QString metadata = root.filePath(QStringLiteral("anim.wal_json"));
	writeFile(metadata, R"({"flags": ["light", "slick"], "value": 1200})");
	const QString walOut = root.filePath(QStringLiteral("anim_1.wal"));
	run({QStringLiteral("wal"), wal, QStringLiteral("--metadata"), metadata, QStringLiteral("--output"), walOut});
	result = run({QStringLiteral("wal"), walOut});
	const QJsonArray flags = result.value(QStringLiteral("surfaceFlagIds")).toArray();
	expect(lastExit == 0 && result.value(QStringLiteral("value")).toInt() == 1200 && flags.contains(QStringLiteral("slick"))
			&& result.value(QStringLiteral("nextFrame")).toString() == QStringLiteral("e1u1/anim_2"),
		"wal --metadata rewrites the header and keeps the chain", lastOutput);
	expect(readFile(walOut).mid(100) == readFile(wal).mid(100), "rewriting a WAL header leaves its pixels alone");
	result = run({QStringLiteral("wal"), root.filePath(QStringLiteral("q2")), QStringLiteral("--material"), QStringLiteral("e1u1/anim_2")});
	expect(lastExit == 0 && result.value(QStringLiteral("name")).toString() == QStringLiteral("e1u1/anim_2"), "wal reads a texture in a package",
		lastOutput);

	// usage
	run({QStringLiteral("render")});
	expect(lastExit == 2, "render without a source is a usage error", lastOutput);
	run({QStringLiteral("list"), q3, QStringLiteral("--frobnicate")});
	expect(lastExit == 2, "an unknown option is a usage error", lastOutput);
	run({QStringLiteral("nonsense")});
	expect(lastExit == 2, "an unknown material command is a usage error", lastOutput);

	if (failures > 0) {
		std::cerr << failures << " material CLI check(s) failed\n";
		return 1;
	}
	std::cout << "material CLI smoke passed\n";
	return 0;
}
