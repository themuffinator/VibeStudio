// Core checks for the materials module: parsing (Quake III, Doom 3,
// ANIMDEFS, SWANTBLS), engine rejection rules, edits that keep comments,
// evaluation (Quake III tables and noise, Doom 3 expressions and tables),
// classic lumps, the library scan, and the renderer on synthetic packages.
// No game data: every image, WAD and script is generated here.

#include "core/material_classic.h"
#include "core/material_eval.h"
#include "core/material_images.h"
#include "core/material_library.h"
#include "core/material_render.h"
#include "core/material_script.h"
#include "core/package_archive.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QtEndian>

#include <cmath>
#include <iostream>

using namespace vibestudio;

namespace {

int failures = 0;

bool expect(bool condition, const std::string& message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
	return condition;
}

bool near(double a, double b, double tolerance = 1.0e-6)
{
	return std::abs(a - b) <= tolerance;
}

void writeFile(const QString& path, const QByteArray& bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(bytes);
	}
}

QByteArray tga(const QImage& source)
{
	// Uncompressed 32-bit, top-left origin.
	const QImage image = source.convertToFormat(QImage::Format_ARGB32);
	QByteArray bytes(18, '\0');
	bytes[2] = 2;
	qToLittleEndian<quint16>(static_cast<quint16>(image.width()), bytes.data() + 12);
	qToLittleEndian<quint16>(static_cast<quint16>(image.height()), bytes.data() + 14);
	bytes[16] = 32;
	bytes[17] = 0x28;
	for (int y = 0; y < image.height(); ++y) {
		const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
		for (int x = 0; x < image.width(); ++x) {
			bytes.append(static_cast<char>(qBlue(line[x])));
			bytes.append(static_cast<char>(qGreen(line[x])));
			bytes.append(static_cast<char>(qRed(line[x])));
			bytes.append(static_cast<char>(qAlpha(line[x])));
		}
	}
	return bytes;
}

QImage solid(int size, QRgb color)
{
	QImage image(size, size, QImage::Format_ARGB32);
	image.fill(color);
	return image;
}

QImage halves(int size, QRgb left, QRgb right)
{
	QImage image(size, size, QImage::Format_ARGB32);
	for (int y = 0; y < size; ++y) {
		for (int x = 0; x < size; ++x) {
			image.setPixel(x, y, x < size / 2 ? left : right);
		}
	}
	return image;
}

QRgb centre(const QImage& image)
{
	return image.pixel(image.width() / 2, image.height() / 2);
}

// MATERIAL_TEST_DUMP=<folder> saves the renders the checks look at.
int dumpCounter = 0;
QImage dumped(const QImage& image, const QString& name)
{
	const QString folder = qEnvironmentVariable("MATERIAL_TEST_DUMP");
	if (!folder.isEmpty()) {
		image.save(QDir(folder).filePath(QStringLiteral("%1-%2.png").arg(++dumpCounter, 2, 10, QLatin1Char('0')).arg(name)));
	}
	return image;
}

// --- WAD builders ----------------------------------------------------------

struct Lump {
	QByteArray name;
	QByteArray data;
	int type = 0;
};

QByteArray doomWad(const QVector<Lump>& lumps)
{
	QByteArray body;
	QByteArray directory;
	int offset = 12;
	for (const Lump& lump : lumps) {
		QByteArray entry(16, '\0');
		qToLittleEndian<qint32>(offset, entry.data());
		qToLittleEndian<qint32>(static_cast<qint32>(lump.data.size()), entry.data() + 4);
		for (int i = 0; i < 8 && i < lump.name.size(); ++i) {
			entry[8 + i] = lump.name.at(i);
		}
		directory.append(entry);
		body.append(lump.data);
		offset += lump.data.size();
	}
	QByteArray header("PWAD", 4);
	header.resize(12);
	qToLittleEndian<qint32>(static_cast<qint32>(lumps.size()), header.data() + 4);
	qToLittleEndian<qint32>(offset, header.data() + 8);
	return header + body + directory;
}

QByteArray quakeWad(const QVector<Lump>& lumps)
{
	QByteArray body;
	QByteArray directory;
	int offset = 12;
	for (const Lump& lump : lumps) {
		QByteArray entry(32, '\0');
		qToLittleEndian<qint32>(offset, entry.data());
		qToLittleEndian<qint32>(static_cast<qint32>(lump.data.size()), entry.data() + 4);
		qToLittleEndian<qint32>(static_cast<qint32>(lump.data.size()), entry.data() + 8);
		entry[12] = static_cast<char>(lump.type);
		for (int i = 0; i < 16 && i < lump.name.size(); ++i) {
			entry[16 + i] = lump.name.at(i);
		}
		directory.append(entry);
		body.append(lump.data);
		offset += lump.data.size();
	}
	QByteArray header("WAD2", 4);
	header.resize(12);
	qToLittleEndian<qint32>(static_cast<qint32>(lumps.size()), header.data() + 4);
	qToLittleEndian<qint32>(offset, header.data() + 8);
	return header + body + directory;
}

QByteArray miptex(const QByteArray& name, int size, int index)
{
	QByteArray bytes(40, '\0');
	for (int i = 0; i < 16 && i < name.size(); ++i) {
		bytes[i] = name.at(i);
	}
	qToLittleEndian<quint32>(static_cast<quint32>(size), bytes.data() + 16);
	qToLittleEndian<quint32>(static_cast<quint32>(size), bytes.data() + 20);
	int at = 40;
	for (int level = 0; level < 4; ++level) {
		qToLittleEndian<quint32>(static_cast<quint32>(at), bytes.data() + 24 + 4 * level);
		at += (size >> level) * (size >> level);
	}
	for (int level = 0; level < 4; ++level) {
		const int side = size >> level;
		for (int y = 0; y < side; ++y) {
			for (int x = 0; x < side; ++x) {
				bytes.append(static_cast<char>(((x / 8 + y / 8) % 2 == 0) ? index : index + 30));
			}
		}
	}
	return bytes;
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

// --- Sections ----------------------------------------------------------------

void checkQuake3Parsing()
{
	const QString text = QStringLiteral(
		"// a comment\n"
		"textures/test/glow\n"
		"{\n"
		"\tqer_editorimage textures/test/glow.tga\n"
		"\tsurfaceparm nolightmap // trailing\n"
		"\tcull none\n"
		"\tdeformVertexes wave 100 sin 0 4 0 0.5\n"
		"\t{\n"
		"\t\tmap textures/test/glow.tga\n"
		"\t\ttcMod scroll 0.5 0\n"
		"\t\ttcMod rotate 90\n"
		"\t}\n"
		"\t{\n"
		"\t\tmap $lightmap\n"
		"\t\tblendFunc filter\n"
		"\t}\n"
		"\t{\n"
		"\t\tanimMap 2 textures/test/a.tga textures/test/b.tga\n"
		"\t\tblendFunc add\n"
		"\t\trgbGen wave sin 0.5 0.5 0 1\n"
		"\t}\n"
		"}\n"
		"textures/test/broken\n"
		"{\n"
		"\t{\n"
		"\t\tmap textures/test/glow.tga\n"
		"\t\tbogusKeyword 1\n"
		"\t}\n"
		"}\n"
		"textures/test/badglobal\n"
		"{\n"
		"\tnotARealKeyword\n"
		"}\n"
		"textures/test/noisealpha\n"
		"{\n"
		"\t{\n"
		"\t\tmap textures/test/glow.tga\n"
		"\t\talphaGen wave noise 0 1 0 1\n"
		"\t}\n"
		"}\n"
		"textures/test/sky\n"
		"{\n"
		"\tsurfaceparm sky\n"
		"\tskyParms env/test - -\n"
		"\t{\n"
		"\t\tmap textures/test/glow.tga\n"
		"\t\ttcMod scale 3 2\n"
		"\t}\n"
		"}\n");
	const MaterialScript script = parseMaterialScript(text, MaterialEngine::Quake3, QStringLiteral("scripts/test.shader"));
	expect(script.materials.size() == 5, "five shaders parse");
	const MaterialDefinition* glow = script.find(QStringLiteral("textures/test/glow"));
	if (!expect(glow != nullptr, "the glow shader is found by name")) {
		return;
	}
	expect(glow->engineRejection.isEmpty(), "a valid shader is not rejected");
	expect(glow->stages.size() == 3, "three stages");
	expect(glow->cull == MaterialCull::None, "cull none is two-sided");
	expect(glow->surfaceParms.contains(QStringLiteral("nolightmap")), "surfaceparm is recorded");
	expect(glow->editorImage == QStringLiteral("textures/test/glow.tga"), "the editor image is read");
	expect(glow->deforms.size() == 1 && near(glow->deforms.first().spread, 0.01), "deformVertexes wave spread is 1/div");
	const MaterialStage& first = glow->stages.at(0);
	expect(first.rgbGen.source == MaterialColorSource::IdentityLighting, "an unblended stage defaults to identityLighting");
	expect(first.depthWrite, "an opaque stage writes depth");
	expect(first.tcMods.size() == 2 && first.tcMods.at(0).kind == MaterialTexModKind::Scroll, "tcMods keep their order");
	expect(first.tcMods.at(0).directive == 1, "a tcMod knows its directive");
	const MaterialStage& lightmap = glow->stages.at(1);
	expect(lightmap.imageKind == MaterialImageKind::Lightmap && lightmap.tcGen.source == MaterialTexCoordSource::Lightmap,
		"$lightmap uses lightmap coordinates");
	expect(lightmap.rgbGen.source == MaterialColorSource::Identity, "a filter stage defaults to rgbGen identity");
	expect(!lightmap.depthWrite, "a blended stage does not write depth");
	const MaterialStage& animated = glow->stages.at(2);
	expect(animated.imageKind == MaterialImageKind::Animation && animated.animationFrames.size() == 2, "animMap frames");
	expect(animated.rgbGen.source == MaterialColorSource::Wave, "rgbGen wave");
	expect(glow->isAnimated(), "scroll, animMap and waves animate");
	const MaterialDefinition* broken = script.find(QStringLiteral("textures/test/broken"));
	expect(broken && !broken->engineRejection.isEmpty(), "an unknown stage keyword drops the shader");
	const MaterialDefinition* badGlobal = script.find(QStringLiteral("textures/test/badglobal"));
	expect(badGlobal && !badGlobal->engineRejection.isEmpty(), "an unknown general keyword drops the shader");
	const MaterialDefinition* noise = script.find(QStringLiteral("textures/test/noisealpha"));
	expect(noise && !noise->engineRejection.isEmpty(), "noise outside rgbGen stops the engine");
	const MaterialDefinition* sky = script.find(QStringLiteral("textures/test/sky"));
	expect(sky && sky->isSky() && sky->sky.farBox == QStringLiteral("env/test") && near(sky->sky.cloudHeight, 512.0),
		"skyParms reads the far box and defaults the cloud height to 512");

	// Enemy Territory's implicit keywords and other derivatives' keywords are
	// read, not rejected.
	const MaterialScript derivatives = parseMaterialScript(QStringLiteral(
		"textures/et/fence\n{\n\tsurfaceparm alphashadow\n\timplicitMask -\n}\n"
		"textures/et/wall\n{\n\timplicitMap textures/et/wall_d.jpg\n}\n"
		"textures/et/glass\n{\n\timplicitBlend -\n}\n"
		"textures/et/foggy\n{\n\tfogvars ( 0.5 0.5 0.5 ) 512\n\tnofog\n\t{\n\t\tmap textures/et/foggy.tga\n\t}\n}\n"), MaterialEngine::Quake3);
	const MaterialDefinition* fence = derivatives.find(QStringLiteral("textures/et/fence"));
	expect(fence && fence->engineRejection.isEmpty() && fence->stages.size() == 2 && fence->stages.at(0).alphaTest == MaterialAlphaTest::GreaterEqual128
			&& fence->stages.at(0).imagePath == QStringLiteral("textures/et/fence.tga") && fence->stages.at(1).imageKind == MaterialImageKind::Lightmap
			&& fence->stages.at(1).depthFunc == MaterialDepthFunc::Equal && fence->cull == MaterialCull::None,
		"implicitMask draws the shader's own image alpha-tested, two-sided, then the lightmap where it drew");
	const MaterialDefinition* etWall = derivatives.find(QStringLiteral("textures/et/wall"));
	expect(etWall && etWall->stages.size() == 2 && etWall->stages.at(0).imageKind == MaterialImageKind::Lightmap
			&& etWall->stages.at(1).imagePath == QStringLiteral("textures/et/wall_d.jpg") && etWall->cull == MaterialCull::Front,
		"implicitMap draws the lightmap, then the named image multiplied in");
	const MaterialDefinition* glass = derivatives.find(QStringLiteral("textures/et/glass"));
	expect(glass && glass->stages.size() == 2 && glass->stages.at(0).blend.source == MaterialBlendFactor::SourceAlpha,
		"implicitBlend blends the image");
	const MaterialDefinition* foggy = derivatives.find(QStringLiteral("textures/et/foggy"));
	expect(foggy && foggy->engineRejection.isEmpty() && foggy->stages.size() == 1 && foggy->warningCount() >= 2,
		"Wolfenstein fog keywords warn instead of dropping the shader");
}

void checkQuake3Edits()
{
	const QString text = QStringLiteral("shader/a // keep\n{\n\t// body comment\n\t{\n\t\tmap foo.tga // map comment\n\t\tblendFunc add\n\t}\n}\n");
	MaterialScript script = parseMaterialScript(text, MaterialEngine::Quake3, QStringLiteral("a.shader"));
	MaterialEdit edit;
	edit.material = QStringLiteral("shader/a");
	edit.stage = 0;
	edit.keyword = QStringLiteral("blendFunc");
	edit.arguments = QStringLiteral("filter");
	MaterialEditResult result = applyMaterialEdit(script, edit);
	expect(result.ok && result.text.contains(QStringLiteral("blendFunc filter")) && result.text.contains(QStringLiteral("// map comment"))
			&& result.text.contains(QStringLiteral("// keep")),
		"set replaces only the directive and keeps comments");
	expect(result.script.materials.first().stages.first().blend.source == MaterialBlendFactor::DestinationColor, "the edit is parsed again");
	edit = MaterialEdit();
	edit.kind = MaterialEditKind::AddDirective;
	edit.material = QStringLiteral("shader/a");
	edit.stage = 0;
	edit.keyword = QStringLiteral("tcMod");
	edit.arguments = QStringLiteral("scroll 1 0");
	result = applyMaterialEdit(result.script, edit);
	expect(result.ok && result.script.materials.first().stages.first().tcMods.size() == 1, "add inserts a directive into the stage");
	expect(result.text.contains(QStringLiteral("\t\ttcMod scroll 1 0\n")), "an added directive takes the block's indentation");
	edit = MaterialEdit();
	edit.kind = MaterialEditKind::AddStage;
	edit.material = QStringLiteral("shader/a");
	edit.text = QStringLiteral("map $lightmap\nrgbGen identity");
	edit.position = 0;
	result = applyMaterialEdit(result.script, edit);
	expect(result.ok && result.script.materials.first().stages.size() == 2
			&& result.script.materials.first().stages.first().imageKind == MaterialImageKind::Lightmap,
		"add-stage inserts before stage 1");
	edit = MaterialEdit();
	edit.kind = MaterialEditKind::MoveStage;
	edit.material = QStringLiteral("shader/a");
	edit.stage = 0;
	edit.position = 1;
	result = applyMaterialEdit(result.script, edit);
	expect(result.ok && result.script.materials.first().stages.at(1).imageKind == MaterialImageKind::Lightmap, "move-stage reorders stages");
	edit = MaterialEdit();
	edit.kind = MaterialEditKind::RemoveStage;
	edit.material = QStringLiteral("shader/a");
	edit.stage = 1;
	result = applyMaterialEdit(result.script, edit);
	expect(result.ok && result.script.materials.first().stages.size() == 1, "remove-stage removes the block");
	expect(result.text.contains(QStringLiteral("// body comment")), "comments outside the removed block remain");
	edit = MaterialEdit();
	edit.kind = MaterialEditKind::RenameDefinition;
	edit.material = QStringLiteral("shader/a");
	edit.text = QStringLiteral("shader/b");
	result = applyMaterialEdit(result.script, edit);
	expect(result.ok && result.script.find(QStringLiteral("shader/b")) && result.text.contains(QStringLiteral("shader/b // keep")),
		"rename changes only the name");
	QVector<MaterialEdit> edits;
	QString error;
	expect(parseMaterialEditsJson(QByteArray(R"([{"op":"set","stage":1,"keyword":"blendFunc","arguments":"blend"}])"), QStringLiteral("shader/b"),
			   &edits, &error)
			&& edits.size() == 1 && edits.first().stage == 0,
		"JSON edits use 1-based stages");
	expect(!parseMaterialEditsJson(QByteArray(R"([{"op":"set","bogus":1}])"), {}, &edits, &error), "unknown JSON fields are refused");
}

void checkDoom3Parsing()
{
	const QString text = QStringLiteral(
		"table flicker { snap { 0, 1, 0.5 } }\n"
		"table single { { 0.25 } }\n"
		"textures/test/panel\n"
		"{\n"
		"\tqer_editorimage textures/test/panel\n"
		"\tdiffusemap textures/test/panel\n"
		"\tbumpmap addnormals( textures/test/panel_local, heightmap( textures/test/panel_h, 4 ) )\n"
		"\t{\n"
		"\t\tblend add\n"
		"\t\tmap textures/test/panel_add\n"
		"\t\trgb 0.5 + 0.5 * flicker[ time ]\n"
		"\t\tscale 2, 2\n"
		"\t\tscroll time * 0.1, 0\n"
		"\t}\n"
		"}\n"
		"textures/test/badgen\n"
		"{\n"
		"\t{\n"
		"\t\tmap _white\n"
		"\t\ttexGen screen\n"
		"\t}\n"
		"}\n"
		"textures/test/badminus { { map _white rgb -time } }\n");
	const MaterialScript script = parseMaterialScript(text, MaterialEngine::Doom3, QStringLiteral("materials/test.mtr"));
	expect(script.tables.size() == 2, "two tables parse");
	const MaterialDefinition* panel = script.find(QStringLiteral("textures/test/panel"));
	if (!expect(panel != nullptr && panel->engineRejection.isEmpty(), "the panel material parses")) {
		return;
	}
	expect(panel->stages.size() == 3, "two shorthand stages and one block");
	expect(panel->stages.at(0).role == MaterialStageRole::Diffuse && panel->stages.at(0).shorthandDirective >= 0, "diffusemap is a shorthand stage");
	expect(panel->stages.at(1).role == MaterialStageRole::Bump && panel->stages.at(1).imageKind == MaterialImageKind::Program,
		"bumpmap holds an image program");
	expect(materialImageProgramText(panel->imagePrograms, panel->stages.at(1).imageProgram)
			== QStringLiteral("addnormals( textures/test/panel_local, heightmap( textures/test/panel_h, 4 ) )"),
		"image programs print back");
	expect(panel->coverage == QStringLiteral("opaque"), "a lit material is opaque");
	expect(panel->stages.at(2).tcMods.size() == 2, "scale and scroll are recorded");
	const MaterialDefinition* badGen = script.find(QStringLiteral("textures/test/badgen"));
	expect(badGen && !badGen->engineRejection.isEmpty(), "texGen screen defaults the material");
	const MaterialDefinition* badMinus = script.find(QStringLiteral("textures/test/badminus"));
	expect(badMinus && !badMinus->engineRejection.isEmpty(), "a minus before a non-number defaults the material");

	QVector<MaterialExpressionNode> nodes;
	QString error;
	const int root = parseMaterialExpression(QStringLiteral("10 - 4 - 3"), &nodes, &error);
	MaterialTableSet tables;
	tables.addAll(script.tables);
	MaterialEvalContext context;
	expect(root >= 0 && near(evaluateMaterialExpression(nodes, root, tables, context), 9.0), "subtraction groups to the right: 10 - (4 - 3)");
	const int printed = parseMaterialExpression(materialExpressionText(nodes, root), &nodes, &error);
	expect(printed >= 0 && near(evaluateMaterialExpression(nodes, printed, tables, context), 9.0), "printing keeps the grouping");
	nodes.clear();
	const int left = parseMaterialExpression(QStringLiteral("( 10 - 4 ) - 3"), &nodes, &error);
	expect(left >= 0 && near(evaluateMaterialExpression(nodes, left, tables, context), 3.0), "parentheses force left grouping");
	expect(materialExpressionText(nodes, left).contains(QLatin1Char('(')), "printing keeps the needed parentheses");
	nodes.clear();
	const int modulo = parseMaterialExpression(QStringLiteral("7.9 % 3"), &nodes, &error);
	expect(modulo >= 0 && near(evaluateMaterialExpression(nodes, modulo, tables, context), 1.0), "% truncates both sides");
	nodes.clear();
	expect(parseMaterialExpression(QStringLiteral("time/2"), &nodes, &error) < 0, "time/2 lexes as one bad name");
	const MaterialTable* flicker = tables.find(QStringLiteral("flicker"));
	const MaterialTable* single = tables.find(QStringLiteral("single"));
	expect(flicker && near(lookupMaterialTable(*flicker, 0.0), 0.0) && near(lookupMaterialTable(*flicker, 1.0 / 3.0 + 0.01), 1.0)
			&& near(lookupMaterialTable(*flicker, 1.0), 0.0) && near(lookupMaterialTable(*flicker, -1.0 / 3.0), 0.5),
		"snapped tables wrap, including negative indexes");
	expect(single && near(lookupMaterialTable(*single, 0.3), 1.0), "a one-value table always returns 1");
	MaterialTable smooth;
	smooth.values = {0.0, 1.0};
	expect(near(lookupMaterialTable(smooth, 0.25), 0.5) && near(lookupMaterialTable(smooth, 0.75), 0.5), "tables interpolate and wrap");
	smooth.clamp = true;
	expect(near(lookupMaterialTable(smooth, 2.0), 1.0) && near(lookupMaterialTable(smooth, -1.0), 0.0), "clamped tables stop at the ends");
	expect(implicitDoom3MaterialText(QStringLiteral("Textures\\Foo.TGA")).contains(QStringLiteral("map \"textures/foo\"")),
		"implicit materials use the canonical name");
}

void checkEvaluation()
{
	expect(near(quake3FunctionTable(MaterialWaveFunction::Square, 0), 1.0) && near(quake3FunctionTable(MaterialWaveFunction::Square, 600), -1.0),
		"square table");
	expect(near(quake3FunctionTable(MaterialWaveFunction::Triangle, 256), 1.0) && near(quake3FunctionTable(MaterialWaveFunction::Triangle, 768), -1.0),
		"triangle table peaks");
	MaterialWave wave;
	wave.function = MaterialWaveFunction::Sin;
	wave.base = 0.5;
	wave.amplitude = 0.5;
	wave.frequency = 1.0;
	expect(near(evaluateQuake3Wave(wave, 0.25), 1.0, 1.0e-3), "sin wave peaks a quarter cycle in");
	wave.function = MaterialWaveFunction::Noise;
	const double a = evaluateQuake3Wave(wave, 1.3);
	const double b = evaluateQuake3Wave(wave, 1.3);
	expect(a == b && a >= -1.0 && a <= 2.0, "noise is deterministic");
	expect(near(quake3Noise(0, 0, 0, 0.5), (quake3Noise(0, 0, 0, 0) + quake3Noise(0, 0, 0, 1)) / 2, 1.0e-9), "noise interpolates linearly in time");
	QVector<MaterialFrame> frames {{QStringLiteral("a"), 0.2, 0.0}, {QStringLiteral("b"), 0.2, 0.0}, {QStringLiteral("c"), 0.2, 0.0}};
	expect(materialFrameIndexAt(frames, 0.1) == 0 && materialFrameIndexAt(frames, 0.3) == 1 && materialFrameIndexAt(frames, 0.61) == 0,
		"frames cycle");
	expect(quakeLightStyleRaw(QStringLiteral("m"), 0.0) == 264 && quakeLightStyleRaw(QStringLiteral("az"), 0.15) == 550,
		"light styles step at 10 Hz with (c - 'a') * 22");
}

void checkClassic()
{
	QVector<DoomAnimationRange> ranges = doomVanillaAnimations();
	expect(ranges.size() == 22 && ranges.first().last == QStringLiteral("NUKAGE3"), "the vanilla animation table");
	QVector<DoomSwitchPair> switches = doomVanillaSwitches();
	expect(switches.size() == 40 && switches.first().off == QStringLiteral("SW1BRCOM"), "the vanilla switch table");
	QVector<DoomAnimationRange> parsed;
	QString error;
	expect(parseBoomAnimatedLump(boomAnimatedLump(ranges), &parsed, &error) && parsed.size() == ranges.size() && parsed.at(9).texture
			&& parsed.at(9).first == QStringLiteral("BLODGR1"),
		"ANIMATED round-trips");
	QVector<DoomSwitchPair> parsedSwitches;
	expect(parseBoomSwitchesLump(boomSwitchesLump(switches), &parsedSwitches, &error) && parsedSwitches.size() == switches.size(),
		"SWITCHES round-trips");
	QVector<MaterialDiagnostic> diagnostics;
	expect(parseSwantblsText(swantblsText(ranges, switches), &parsed, &parsedSwitches, &diagnostics) && parsed.size() == ranges.size()
			&& parsedSwitches.size() == switches.size(),
		"SWANTBLS text round-trips");
	const QVector<DoomAnimdefsEntry> animdefs = parseAnimdefs(QStringLiteral(
		"flat optional NUKAGE1\n  range NUKAGE3 tics 8\n"
		"texture WALL1\n  pic 1 tics 4\n  pic 2 rand 2 6\n  oscillate\n"
		"warp2 flat WATER 2.5\n"
		"switch doom 1 SW1BRCOM on pic SW2BRCOM tics 0\n"));
	expect(animdefs.size() == 4, "ANIMDEFS entries");
	expect(animdefs.at(0).optional && animdefs.at(0).range && animdefs.at(0).rangeLast == QStringLiteral("NUKAGE3"), "optional comes before the name");
	expect(animdefs.at(1).frames.size() == 2 && animdefs.at(1).frames.at(1).maximumTics == 6 && animdefs.at(1).oscillate, "pic lists and rand");
	expect(animdefs.at(2).kind == QStringLiteral("warp2") && near(animdefs.at(2).warpSpeed, 2.5), "warp2 with a speed");
	expect(animdefs.at(3).switchOn == QStringLiteral("SW2BRCOM"), "switch on picture");
	const QuakeTextureName animated = parseQuakeTextureName(QStringLiteral("+1slime"));
	expect(animated.frame == 1 && animated.base == QStringLiteral("slime"), "+N names");
	const MaterialDefinition gap = quakeMaterialDefinition(QStringLiteral("+0lava"), {QStringLiteral("+2lava")});
	expect(gap.errorCount() == 1, "a gap in Quake frames is an error");
	const MaterialDefinition water = quakeMaterialDefinition(QStringLiteral("*water1"), {});
	expect(water.classic.warp == MaterialWarpStyle::QuakeTurbulent && water.isAnimated(), "liquids warp");
	QByteArray wal(100 + 16 * 16, '\0');
	Quake2WalInfo info;
	info.name = QStringLiteral("e1u1/lava1");
	info.nextFrame = QStringLiteral("e1u1/lava2");
	info.surfaceFlags = kQuake2SurfWarp | kQuake2SurfLight;
	info.value = 300;
	expect(rewriteQuake2WalHeader(&wal, info, &error), "WAL headers rewrite");
	Quake2WalInfo read;
	expect(readQuake2WalInfo(wal, &read, &error) && read.nextFrame == info.nextFrame && read.surfaceFlags == info.surfaceFlags && read.value == 300,
		"WAL headers read back");
	Quake2WalInfo json = read;
	expect(parseQuake2WalInfoText(QStringLiteral("{\"flags\": [\"warp\", \"trans33\"], \"value\": 5}"), &json, &error)
			&& json.surfaceFlags == (kQuake2SurfWarp | kQuake2SurfTrans33) && json.value == 5,
		".wal_json flags take names");
	expect(parseQuake2WalInfoText(quake2WalInfoText(read), &json, &error) && json.surfaceFlags == read.surfaceFlags, ".wal_json round-trips");
	const MaterialDefinition chain = quake2MaterialDefinition(QStringLiteral("e1u1/lava1"), read, [](const QString& name, Quake2WalInfo* out) {
		out->name = name;
		out->nextFrame = name == QStringLiteral("e1u1/lava2") ? QStringLiteral("e1u1/lava1") : QString();
		return name == QStringLiteral("e1u1/lava2");
	});
	expect(chain.classic.frames.size() == 2 && near(chain.classic.frames.first().duration, 0.5), "WAL chains animate at 2 Hz");
}

QImage renderOf(const MaterialDefinition& definition, const MaterialImageSet& images, const MaterialTableSet& tables, double time,
	MaterialRenderOptions options = {})
{
	options.time = time;
	if (options.size == QSize(320, 240)) {
		options.size = QSize(96, 96);
		options.orthographic = true;
		options.tiling = 1.0;
		options.checker = false;
		options.background = QColor(0, 0, 0);
		options.lighting.lightmapSpot = false;
	}
	const MaterialRenderResult result = renderMaterial(definition, images, tables, options);
	if (!qEnvironmentVariable("MATERIAL_TEST_DUMP").isEmpty()) {
		std::cerr << definition.name.toStdString() << ": " << result.notes.join(QStringLiteral(" | ")).toStdString() << " missing="
				  << images.missing().join(QLatin1Char(',')).toStdString() << " warnings=" << images.warnings.join(QLatin1Char(',')).toStdString() << '\n';
		for (auto it = images.textures.cbegin(); it != images.textures.cend(); ++it) {
			std::cerr << "  image " << it.key().toStdString() << " status=" << (it.value() ? it.value()->status.toStdString() : std::string("null"))
					  << " note=" << (it.value() ? it.value()->note.toStdString() : std::string()) << '\n';
		}
	}
	return dumped(result.image, definition.name.section(QLatin1Char('/'), -1));
}

void checkRendering(const QString& root)
{
	// A Quake III package on disk.
	const QString q3 = root + QStringLiteral("/q3");
	writeFile(q3 + QStringLiteral("/textures/test/red.tga"), tga(solid(16, qRgb(255, 0, 0))));
	writeFile(q3 + QStringLiteral("/textures/test/green.tga"), tga(solid(16, qRgb(0, 255, 0))));
	writeFile(q3 + QStringLiteral("/textures/test/halves.tga"), tga(halves(16, qRgb(255, 0, 0), qRgb(0, 0, 255))));
	writeFile(q3 + QStringLiteral("/env/test_rt.tga"), tga(solid(8, qRgb(10, 120, 200))));
	for (const char* face : {"bk", "lf", "ft", "up", "dn"}) {
		writeFile(q3 + QStringLiteral("/env/test_%1.tga").arg(QLatin1String(face)), tga(solid(8, qRgb(10, 120, 200))));
	}
	writeFile(q3 + QStringLiteral("/scripts/test.shader"), QByteArray(
		"textures/test/identity\n{\n\t{\n\t\tmap textures/test/red.tga\n\t\trgbGen identity\n\t}\n}\n"
		"textures/test/scroll\n{\n\t{\n\t\tmap textures/test/halves.tga\n\t\trgbGen identity\n\t\ttcMod scroll 0.5 0\n\t}\n}\n"
		"textures/test/anim\n{\n\t{\n\t\tanimMap 1 textures/test/red.tga textures/test/green.tga\n\t\trgbGen identity\n\t}\n}\n"
		"textures/test/pulse\n{\n\t{\n\t\tmap textures/test/red.tga\n\t\trgbGen wave sin 0.5 0.5 0 1\n\t}\n}\n"
		"textures/test/missing\n{\n\t{\n\t\tmap textures/test/nothere.tga\n\t}\n}\n"
		"textures/test/half\n{\n\t{\n\t\tmap textures/test/red.tga\n\t\trgbGen const ( 0.5 0.5 0.5 )\n\t}\n}\n"
		"textures/test/additive\n{\n\t{\n\t\tmap textures/test/red.tga\n\t\trgbGen identity\n\t}\n\t{\n\t\tmap textures/test/green.tga\n\t\tblendFunc add\n\t\trgbGen identity\n\t}\n}\n"
		"textures/test/sky\n{\n\tsurfaceparm sky\n\tskyParms env/test - -\n}\n"));
	PackageArchive archive;
	QString error;
	if (!expect(archive.load(q3, &error), "the Quake III folder loads")) {
		return;
	}
	const MaterialLibrary library = scanMaterialLibrary(archive);
	expect(library.indexOf(QStringLiteral("textures/test/identity")) >= 0, "the library lists scripted shaders");
	expect(library.indexOf(QStringLiteral("textures/test/green")) >= 0, "an image without a shader is an implicit material");
	MaterialImageSource source;
	source.archive = std::make_shared<PackageArchive>(archive);
	const auto render = [&](const QString& name, double time, MaterialRenderOptions options = {}) {
		const MaterialDefinition definition = library.definition(library.indexOf(name));
		const MaterialImageSet images = resolveMaterialImages(definition, source);
		return renderOf(definition, images, library.tables, time, options);
	};
	const QRgb identity = centre(render(QStringLiteral("textures/test/identity"), 0.0));
	expect(qRed(identity) > 240 && qGreen(identity) < 16, "rgbGen identity draws the image at full brightness with overbright");
	const QRgb before = render(QStringLiteral("textures/test/scroll"), 0.0).pixel(24, 48);
	const QRgb after = render(QStringLiteral("textures/test/scroll"), 0.5).pixel(24, 48);
	expect(qRed(before) > 200 && qBlue(after) > 200, "tcMod scroll moves the image half a repeat in a second");
	expect(qRed(centre(render(QStringLiteral("textures/test/anim"), 0.25))) > 200 && qGreen(centre(render(QStringLiteral("textures/test/anim"), 1.25))) > 200,
		"animMap steps frames at its rate");
	const int dim = qRed(centre(render(QStringLiteral("textures/test/pulse"), 0.0)));
	const int bright = qRed(centre(render(QStringLiteral("textures/test/pulse"), 0.25)));
	expect(dim > 100 && dim < 160 && bright > 240, "rgbGen wave follows the sine table with identityLight");
	MaterialRenderOptions windowed;
	windowed.lighting.overbright = false;
	windowed.size = QSize(96, 96);
	windowed.orthographic = true;
	windowed.tiling = 1.0;
	windowed.background = QColor(0, 0, 0);
	windowed.checker = false;
	const int windowedWave = qRed(centre(render(QStringLiteral("textures/test/pulse"), 0.0, windowed)));
	expect(std::abs(windowedWave - dim) <= 3, "identityLight makes waves look the same with and without overbright");
	expect(qRed(centre(render(QStringLiteral("textures/test/half"), 0.0))) > 240, "with overbright a constant 0.5 shows at full brightness");
	const int windowedHalf = qRed(centre(render(QStringLiteral("textures/test/half"), 0.0, windowed)));
	expect(windowedHalf > 110 && windowedHalf < 145, "without overbright a constant 0.5 shows at half brightness");
	const MaterialDefinition missing = library.definition(library.indexOf(QStringLiteral("textures/test/missing")));
	const MaterialImageSet missingImages = resolveMaterialImages(missing, source);
	MaterialRenderOptions plain;
	plain.size = QSize(64, 64);
	plain.orthographic = true;
	const MaterialRenderResult fallback = renderMaterial(missing, missingImages, library.tables, plain);
	expect(fallback.fallback, "a missing image makes Quake III draw its default shader");
	const QRgb sum = centre(render(QStringLiteral("textures/test/additive"), 0.0));
	expect(qRed(sum) > 240 && qGreen(sum) > 240, "blendFunc add sums the stages");
	const QRgb implicit = centre(render(QStringLiteral("textures/test/green"), 0.0));
	expect(qGreen(implicit) > 200, "an implicit lightmapped shader shows the image at lightmap level 1");
	MaterialRenderOptions room;
	room.size = QSize(64, 64);
	room.shape = MaterialPreviewShape::Room;
	room.checker = false;
	room.background = QColor(0, 0, 0);
	const QRgb skyPixel = centre(render(QStringLiteral("textures/test/sky"), 0.0, room));
	expect(qBlue(skyPixel) > 150 && qGreen(skyPixel) > 80, "the sky box fills the room");

	// Doom 3: a lit panel and the depth fill's black.
	const QString d3 = root + QStringLiteral("/d3");
	writeFile(d3 + QStringLiteral("/textures/test/white.tga"), tga(solid(16, qRgb(220, 220, 220))));
	writeFile(d3 + QStringLiteral("/materials/test.mtr"), QByteArray(
		"textures/test/lit\n{\n\tdiffusemap textures/test/white\n\tbumpmap _flat\n}\n"
		"textures/test/glowing\n{\n\t{\n\t\tblend add\n\t\tmap textures/test/white\n\t\trgb 0.5\n\t}\n}\n"));
	PackageArchive d3Archive;
	if (expect(d3Archive.load(d3, &error), "the Doom 3 folder loads")) {
		const MaterialLibrary d3Library = scanMaterialLibrary(d3Archive);
		MaterialImageSource d3Source;
		d3Source.archive = std::make_shared<PackageArchive>(d3Archive);
		const MaterialDefinition lit = d3Library.definition(d3Library.indexOf(QStringLiteral("textures/test/lit")));
		const MaterialImageSet litImages = resolveMaterialImages(lit, d3Source);
		MaterialRenderOptions options;
		options.size = QSize(96, 96);
		options.lighting.lightOrbit = false;
		options.lighting.lightAngle = 0.0;
		options.checker = false;
		options.background = QColor(0, 0, 0);
		const QImage litImage = dumped(renderMaterial(lit, litImages, d3Library.tables, options).image, QStringLiteral("d3-lit"));
		expect(qRed(centre(litImage)) > 40, "the preview light lights a diffuse surface");
		const MaterialDefinition glowing = d3Library.definition(d3Library.indexOf(QStringLiteral("textures/test/glowing")));
		const QImage glowImage = dumped(renderMaterial(glowing, resolveMaterialImages(glowing, d3Source), d3Library.tables, options).image, QStringLiteral("d3-glow"));
		const int glowRed = qRed(centre(glowImage));
		expect(glowRed > 90 && glowRed < 130, "an additive ambient stage adds rgb 0.5 of its image over the black fill");
	}

	// Doom: a PWAD with a palette, a colormap, a composite wall and a flat.
	QByteArray playpal;
	for (int i = 0; i < 256; ++i) {
		playpal.append(static_cast<char>(i));
		playpal.append(static_cast<char>(255 - i));
		playpal.append(static_cast<char>((i * 7) & 255));
	}
	QByteArray colormap(34 * 256, '\0');
	for (int row = 0; row < 34; ++row) {
		for (int i = 0; i < 256; ++i) {
			colormap[row * 256 + i] = static_cast<char>(std::max(0, i - row * 4));
		}
	}
	QByteArray pnames(4, '\0');
	qToLittleEndian<qint32>(1, pnames.data());
	pnames.append(QByteArray("WALLPAT\0", 8));
	QByteArray texture1(8, '\0');
	qToLittleEndian<qint32>(1, texture1.data());
	qToLittleEndian<qint32>(8, texture1.data() + 4);
	QByteArray maptexture(22, '\0');
	maptexture.replace(0, 6, QByteArray("TESTW1"));
	qToLittleEndian<qint16>(32, maptexture.data() + 12);
	qToLittleEndian<qint16>(64, maptexture.data() + 14);
	qToLittleEndian<qint16>(1, maptexture.data() + 20);
	QByteArray patchRecord(10, '\0');
	texture1 += maptexture + patchRecord;
	QByteArray flat(4096, static_cast<char>(200));
	const QByteArray wad = doomWad({{"PLAYPAL", playpal}, {"COLORMAP", colormap}, {"PNAMES", pnames}, {"TEXTURE1", texture1},
		{"P_START", {}}, {"WALLPAT", doomPatch(32, 64, 120)}, {"P_END", {}}, {"F_START", {}}, {"TESTF1", flat}, {"TESTF2", flat},
		{"F_END", {}}});
	writeFile(root + QStringLiteral("/doom.wad"), wad);
	PackageArchive doomArchive;
	if (expect(doomArchive.load(root + QStringLiteral("/doom.wad"), &error), "the Doom WAD loads")) {
		const MaterialLibrary doomLibrary = scanMaterialLibrary(doomArchive);
		expect(doomLibrary.indexOf(QStringLiteral("TESTW1"), MaterialEngine::Doom) >= 0 && doomLibrary.indexOf(QStringLiteral("TESTF1")) >= 0,
			"walls and flats are listed");
		MaterialImageSource doomSource;
		doomSource.archive = std::make_shared<PackageArchive>(doomArchive);
		doomSource.doomCatalog = doomLibrary.doomCatalog;
		const MaterialDefinition wall = doomLibrary.definition(doomLibrary.indexOf(QStringLiteral("TESTW1")));
		const MaterialImageSet wallImages = resolveMaterialImages(wall, doomSource);
		const MaterialTexturePtr composite = wallImages.find(QStringLiteral("TESTW1"));
		expect(composite && composite->usable() && composite->hasIndices() && composite->width == 32, "the wall composes from its patch with indices");
		expect(!wallImages.doomColormapGenerated && !wallImages.paletteGenerated, "the WAD's COLORMAP and PLAYPAL are used");
		MaterialRenderOptions doomOptions;
		doomOptions.size = QSize(64, 64);
		doomOptions.orthographic = true;
		doomOptions.tiling = 1.0;
		doomOptions.lighting.doomDistance = false;
		doomOptions.lighting.doomFakeContrast = false;
		doomOptions.lighting.doomLight = 255;
		const QImage brightWall = dumped(renderMaterial(wall, wallImages, doomLibrary.tables, doomOptions).image, QStringLiteral("doom-bright"));
		doomOptions.lighting.doomLight = 64;
		const QImage darkWall = dumped(renderMaterial(wall, wallImages, doomLibrary.tables, doomOptions).image, QStringLiteral("doom-dark"));
		expect(qRed(centre(brightWall)) > qRed(centre(darkWall)), "lower sector light picks a darker COLORMAP row");
	}

	// Quake: a WAD2 with an animated sequence and a liquid.
	const QByteArray wad2 = quakeWad({{"+0test", miptex("+0test", 16, 10), 0x44}, {"+1test", miptex("+1test", 16, 40), 0x44},
		{"*water", miptex("*water", 64, 80), 0x44}});
	writeFile(root + QStringLiteral("/quake.wad"), wad2);
	PackageArchive quakeArchive;
	if (expect(quakeArchive.load(root + QStringLiteral("/quake.wad"), &error), "the Quake WAD loads")) {
		const MaterialLibrary quakeLibrary = scanMaterialLibrary(quakeArchive);
		const int animatedIndex = quakeLibrary.indexOf(QStringLiteral("+0test"), MaterialEngine::Quake);
		expect(animatedIndex >= 0 && quakeLibrary.definition(animatedIndex).classic.frames.size() == 2, "Quake frames come from sibling names");
		MaterialImageSource quakeSource;
		quakeSource.archive = std::make_shared<PackageArchive>(quakeArchive);
		const MaterialDefinition water = quakeLibrary.definition(quakeLibrary.indexOf(QStringLiteral("*water")));
		const MaterialImageSet waterImages = resolveMaterialImages(water, quakeSource);
		MaterialRenderOptions quakeOptions;
		quakeOptions.size = QSize(64, 64);
		const QImage first = renderMaterial(water, waterImages, quakeLibrary.tables, [&] {
			MaterialRenderOptions o = quakeOptions;
			o.time = 0.0;
			return o;
		}()).image;
		const QImage second = renderMaterial(water, waterImages, quakeLibrary.tables, [&] {
			MaterialRenderOptions o = quakeOptions;
			o.time = 1.0;
			return o;
		}()).image;
		dumped(first, QStringLiteral("water-0"));
		dumped(second, QStringLiteral("water-1"));
		expect(first != second, "liquids warp over time");
	}

	// Timing: a three-stage shader at 256 x 256.
	const MaterialDefinition timed = library.definition(library.indexOf(QStringLiteral("textures/test/additive")));
	const MaterialImageSet timedImages = resolveMaterialImages(timed, source);
	MaterialRenderOptions timedOptions;
	timedOptions.size = QSize(256, 256);
	QElapsedTimer timer;
	timer.start();
	for (int frame = 0; frame < 4; ++frame) {
		timedOptions.time = frame * 0.1;
		renderMaterial(timed, timedImages, library.tables, timedOptions);
	}
	std::cout << "render-ms-per-frame " << timer.elapsed() / 4.0 << '\n';
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	checkQuake3Parsing();
	checkQuake3Edits();
	checkDoom3Parsing();
	checkEvaluation();
	checkClassic();
	QTemporaryDir directory;
	if (expect(directory.isValid(), "a temporary folder")) {
		checkRendering(directory.path());
	}
	if (failures > 0) {
		std::cerr << failures << " material check(s) failed\n";
		return 1;
	}
	std::cout << "material smoke passed\n";
	return 0;
}
