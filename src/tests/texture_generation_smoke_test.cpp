#include "core/texture_generation.h"

#include "core/package_archive.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace vibestudio;

namespace {

int failures = 0;
constexpr double kPi = 3.14159265358979323846;

void expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << "\n";
		++failures;
	}
}

// A picture with a hard seam: brightness ramps across and down.
QImage rampImage(QSize size)
{
	QImage image(size, QImage::Format_ARGB32);
	for (int y = 0; y < size.height(); ++y) {
		for (int x = 0; x < size.width(); ++x) {
			image.setPixel(x, y, qRgb(40 + 180 * x / size.width(), 60 + 120 * y / size.height(), 90));
		}
	}
	return image;
}

// A picture that already tiles: whole periods of a wave in both directions.
QImage periodicImage(QSize size, int periods = 4)
{
	QImage image(size, QImage::Format_ARGB32);
	for (int y = 0; y < size.height(); ++y) {
		for (int x = 0; x < size.width(); ++x) {
			const double u = std::sin(2.0 * kPi * periods * x / size.width());
			const double v = std::cos(2.0 * kPi * periods * y / size.height());
			const int value = int(127 + 60 * u + 60 * v);
			image.setPixel(x, y, qRgb(value, value * 3 / 4, value / 2));
		}
	}
	return image;
}

IdTechPaletteResolution generatedPalette(const QString& id)
{
	IdTechPaletteResolution resolution;
	resolution.palette = generatedIdTechPalette(id);
	resolution.requestedPaletteId = id;
	return resolution;
}

TextureGenerationSpec spec(const QString& game, const QString& surface, const QString& prompt = QStringLiteral("rusted riveted metal plate"))
{
	TextureGenerationSpec value;
	value.game = game;
	value.surface = surface;
	value.prompt = prompt;
	return value;
}

void checkProfiles()
{
	const QStringList ids = textureGameProfileIds();
	expect(ids.contains(QStringLiteral("quake")) && ids.contains(QStringLiteral("quake2")) && ids.contains(QStringLiteral("quake3")) && ids.contains(QStringLiteral("doom"))
			&& ids.contains(QStringLiteral("heretic")) && ids.contains(QStringLiteral("hexen")) && ids.contains(QStringLiteral("generic")),
		"Every idTech family and a generic PNG profile are offered.");
	TextureGameProfile profile;
	expect(textureGameProfileForId(QStringLiteral("Q3"), &profile) && profile.id == QStringLiteral("quake3") && profile.normalSuffix == QStringLiteral("_n"),
		"Quake III takes ioquake3's _n suffix, and the alias resolves.");
	expect(textureGameProfileForId(QStringLiteral("idtech1"), &profile) && profile.id == QStringLiteral("doom") && profile.maxNameLength == 8, "Doom names hold 8 characters.");
	expect(!textureGameProfileForId(QStringLiteral("unreal")), "Unknown games are refused.");
	expect(textureGenerationSurfaceIds().contains(QStringLiteral("liquid")) && textureGenerationSurfaceIds().contains(QStringLiteral("sky")), "Liquid and sky surfaces are offered.");
}

void checkNamesAndSizes()
{
	expect(textureGenerationName(spec(QStringLiteral("quake"), QStringLiteral("wall"))) == QStringLiteral("rusted_riveted"),
		"A Quake name is derived from the prompt's words within 15 characters.");
	expect(textureGenerationName(spec(QStringLiteral("quake"), QStringLiteral("liquid"), QStringLiteral("bubbling lava"))) == QStringLiteral("*bubbling_lava"),
		"A Quake liquid's name starts with *.");
	const QString sky = textureGenerationName(spec(QStringLiteral("quake"), QStringLiteral("sky"), QStringLiteral("stormy purple clouds")));
	expect(sky.startsWith(QStringLiteral("sky")) && sky.size() <= 15, "A Quake sky's name starts with sky.");
	expect(textureGenerationName(spec(QStringLiteral("doom"), QStringLiteral("floor"), QStringLiteral("cracked green marble"))) == QStringLiteral("CRACKEDG"),
		"A Doom name is 8 upper-case characters.");
	TextureGenerationSpec named = spec(QStringLiteral("quake2"), QStringLiteral("wall"));
	named.name = QStringLiteral("Hull Plate 01!");
	named.directory = QStringLiteral("e1u1");
	expect(textureGenerationMapName(named) == QStringLiteral("e1u1/hull_plate_01"), "A Quake II map name is folder/name, cleaned.");
	expect(textureGenerationMapName(spec(QStringLiteral("quake3"), QStringLiteral("wall"))) == QStringLiteral("vibestudio/rusted_riveted_metal"),
		"Quake III textures default to the vibestudio folder.");

	expect(textureGenerationOutputSize(spec(QStringLiteral("quake"), QStringLiteral("wall"))) == QSize(64, 64), "Quake walls default to 64x64.");
	expect(textureGenerationOutputSize(spec(QStringLiteral("quake"), QStringLiteral("sky"))) == QSize(256, 128), "Quake skies are 256x128.");
	TextureGenerationSpec doomFloor = spec(QStringLiteral("doom"), QStringLiteral("floor"));
	doomFloor.size = QSize(128, 128);
	expect(textureGenerationOutputSize(doomFloor) == QSize(64, 64), "Doom flats are 64x64 whatever was asked.");
	expect(textureGenerationOutputSize(spec(QStringLiteral("doom"), QStringLiteral("wall"))) == QSize(64, 128), "Doom walls default to 64x128 patches.");
	expect(textureGenerationRequestSize(spec(QStringLiteral("doom"), QStringLiteral("wall"))) == QSize(512, 1024), "A tall wall asks the model for a tall picture.");
	expect(textureGenerationOutputSize(spec(QStringLiteral("quake"), QStringLiteral("trim"))) == QSize(64, 16), "Quake trims are a quarter of a wall high.");
}

void checkPrompts()
{
	const QString quake = textureGenerationPrompt(spec(QStringLiteral("quake"), QStringLiteral("floor")));
	expect(quake.contains(QStringLiteral("rusted riveted metal plate")) && quake.contains(QStringLiteral("seen from directly above"))
			&& quake.contains(QStringLiteral("tile seamlessly")) && quake.contains(QStringLiteral("Quake (1996)")) && quake.contains(QStringLiteral("64x64 pixels"))
			&& quake.contains(QStringLiteral("No text")),
		"The Quake prompt carries the user's words, the surface, tiling, the era, the target size, and the rules.");
	TextureGenerationSpec styled = spec(QStringLiteral("quake3"), QStringLiteral("wall"));
	styled.style = QStringLiteral("gothic");
	styled.seamless = false;
	const QString quake3 = textureGenerationPrompt(styled);
	expect(quake3.contains(QStringLiteral("plate, gothic")) && !quake3.contains(QStringLiteral("256-colour")) && !quake3.contains(QStringLiteral("tile seamlessly")),
		"A true-colour, non-tiling texture prompt leaves out the palette and tiling lines.");
	expect(textureGenerationPrompt(spec(QStringLiteral("quake"), QStringLiteral("liquid"), QStringLiteral("glowing toxic sludge"))).contains(QStringLiteral("The liquid is slime")),
		"A liquid's kind is read from its words.");
	expect(textureGenerationNegativePrompt(spec(QStringLiteral("quake"), QStringLiteral("wall"))).contains(QStringLiteral("perspective"))
			&& !textureGenerationNegativePrompt(spec(QStringLiteral("quake"), QStringLiteral("sky"))).contains(QStringLiteral("perspective")),
		"The negative prompt keeps perspective out of surfaces but not skies.");
}

void checkImageSteps()
{
	const QImage ramp = rampImage(QSize(256, 256));
	const double before = textureSeamScore(ramp);
	const QImage seamless = makeTextureSeamless(ramp, 20);
	const double after = textureSeamScore(seamless);
	expect(before > 10.0 && after < 2.0 && seamless.size() == ramp.size(), "Blending turns a hard seam into one like any neighbour step.");
	expect(textureSeamScore(periodicImage(QSize(256, 256))) < 1.5, "A picture made of whole periods already tiles.");

	const QImage small = resampleTexture(periodicImage(QSize(256, 256)), QSize(64, 64), true);
	expect(small.size() == QSize(64, 64) && textureSeamScore(small) < 1.6, "Wrap-around resampling keeps a tiling picture tiling.");
	QImage checker(2, 2, QImage::Format_ARGB32);
	checker.setPixel(0, 0, qRgb(0, 0, 0));
	checker.setPixel(1, 1, qRgb(0, 0, 0));
	checker.setPixel(1, 0, qRgb(255, 255, 255));
	checker.setPixel(0, 1, qRgb(255, 255, 255));
	const QRgb average = resampleTexture(checker, QSize(1, 1), true).pixel(0, 0);
	expect(std::abs(qRed(average) - 128) <= 1 && std::abs(qGreen(average) - 128) <= 1, "Shrinking averages the pixels covered.");
	QImage edge(2, 1, QImage::Format_ARGB32);
	edge.setPixel(0, 0, qRgba(0, 255, 0, 0));
	edge.setPixel(1, 0, qRgba(255, 0, 0, 255));
	const QRgb mixed = resampleTexture(edge, QSize(1, 1), false).pixel(0, 0);
	expect(qGreen(mixed) == 0 && qRed(mixed) == 255 && std::abs(qAlpha(mixed) - 128) <= 1, "A transparent pixel's colour never bleeds into the average.");
	const QImage big = resampleTexture(checker, QSize(8, 8), true);
	expect(big.size() == QSize(8, 8), "Enlarging works too.");
	expect(cropTextureToAspect(QImage(1024, 512, QImage::Format_ARGB32), QSize(64, 64)).size() == QSize(512, 512)
			&& cropTextureToAspect(QImage(1024, 1024, QImage::Format_ARGB32), QSize(64, 128)).size() == QSize(512, 1024),
		"Cropping keeps the largest centred region of the wanted shape.");

	QImage flat(16, 16, QImage::Format_ARGB32);
	flat.fill(qRgb(100, 100, 100));
	const QRgb up = deriveTextureNormalMap(flat, 2.5).pixel(5, 5);
	expect(std::abs(qRed(up) - 128) <= 1 && std::abs(qGreen(up) - 128) <= 1 && qBlue(up) >= 254, "A flat picture's normals face straight out.");
	QImage slope(16, 16, QImage::Format_ARGB32);
	for (int y = 0; y < 16; ++y) {
		for (int x = 0; x < 16; ++x) {
			slope.setPixel(x, y, qRgb(10 * x, 10 * x, 10 * x));
		}
	}
	const QRgb tilted = deriveTextureNormalMap(slope, 2.5, false).pixel(8, 8);
	expect(qRed(tilted) < 120 && std::abs(qGreen(tilted) - 128) <= 2 && qAlpha(tilted) > 0, "Brightness rising to the right tilts normals left; height rides in alpha.");
	QImage grey(4, 4, QImage::Format_ARGB32);
	grey.fill(qRgb(200, 200, 200));
	QImage red(4, 4, QImage::Format_ARGB32);
	red.fill(qRgb(255, 120, 120));
	expect(qRed(deriveTextureGlossMap(grey).pixel(1, 1)) > qRed(deriveTextureGlossMap(red).pixel(1, 1)), "Grey metal shines more than coloured paint.");
	QImage dark(8, 8, QImage::Format_ARGB32);
	dark.fill(qRgb(20, 20, 30));
	expect(deriveTextureGlowMap(dark, 0.82).isNull(), "Nothing glows in a dark picture.");
	dark.setPixel(3, 3, qRgb(255, 230, 40));
	const QImage glow = deriveTextureGlowMap(dark, 0.82);
	expect(!glow.isNull() && glow.pixel(3, 3) == qRgb(255, 230, 40) && glow.pixel(0, 0) == qRgb(0, 0, 0), "A bright yellow light glows on black.");
}

void checkProcessing()
{
	const QImage picture = rampImage(QSize(1024, 1024));

	TextureGenerationSpec quakeSpec = spec(QStringLiteral("quake"), QStringLiteral("wall"));
	quakeSpec.companions = true;
	GeneratedTexture quake = processGeneratedTexture(picture, quakeSpec, generatedPalette(QStringLiteral("quake")));
	expect(quake.ok && quake.exportOptions.format == TextureExportFormat::QuakeMiptex && quake.encoded.mipLevels.size() == 4 && quake.preview.size() == QSize(64, 64)
			&& quake.name == QStringLiteral("rusted_riveted"),
		"A Quake texture becomes a 64x64 miptex with four mip levels.");
	expect(quake.seamScoreBefore > 5.0 && quake.seamScoreAfter < 2.5 && !quake.steps.isEmpty(), "The seam is measured before and after blending.");
	expect(quake.exportOptions.fullbright == TextureFullbrightMode::Exclude, "Fullbright colours are kept out unless asked for.");
	int generatedWarnings = 0;
	for (const QString& warning : quake.warnings) {
		generatedWarnings += warning.contains(QStringLiteral("stand-in")) || warning.contains(QStringLiteral("Generated palette")) ? 1 : 0;
	}
	expect(generatedWarnings == 1, "A generated stand-in palette is warned about once.");
	expect(quake.companions.size() >= 2 && quake.companions.first().suffix == QStringLiteral("_norm") && quake.companions.at(1).suffix == QStringLiteral("_gloss"),
		"Quake companions are _norm and _gloss for DarkPlaces-style ports.");

	GeneratedTexture sky = processGeneratedTexture(picture, spec(QStringLiteral("quake"), QStringLiteral("sky"), QStringLiteral("stormy purple clouds")), generatedPalette(QStringLiteral("quake")));
	expect(sky.ok && sky.image.size() == QSize(256, 128) && sky.name.startsWith(QStringLiteral("sky")), "A Quake sky is a 256x128 two-layer miptex.");

	GeneratedTexture lava = processGeneratedTexture(picture, spec(QStringLiteral("quake2"), QStringLiteral("liquid"), QStringLiteral("bubbling lava")),
		generatedPalette(QStringLiteral("quake2")));
	expect(lava.ok && lava.exportOptions.format == TextureExportFormat::Quake2Wal && lava.exportOptions.contentFlags == 8u && lava.exportOptions.surfaceFlags == 8u
			&& lava.exportOptions.name == QStringLiteral("vibestudio/bubbling_lava"),
		"A Quake II lava texture is a warping WAL with lava contents and its folder in its name.");

	TextureGenerationSpec quake3Spec = spec(QStringLiteral("quake3"), QStringLiteral("wall"));
	quake3Spec.companions = true;
	GeneratedTexture quake3 = processGeneratedTexture(picture, quake3Spec, IdTechPaletteResolution());
	expect(quake3.ok && quake3.exportOptions.format == TextureExportFormat::Targa && quake3.image.size() == QSize(256, 256)
			&& quake3.companions.size() >= 2 && quake3.companions.first().suffix == QStringLiteral("_n"),
		"A Quake III texture is a 256x256 TGA with ioquake3's _n map.");

	GeneratedTexture flat = processGeneratedTexture(picture, spec(QStringLiteral("doom"), QStringLiteral("floor"), QStringLiteral("cracked green marble")),
		generatedPalette(QStringLiteral("doom")));
	expect(flat.ok && flat.exportOptions.format == TextureExportFormat::DoomFlat && flat.encoded.bytes.size() == 64 * 64, "A Doom floor is a raw 64x64 flat.");
	GeneratedTexture wall = processGeneratedTexture(picture, spec(QStringLiteral("doom"), QStringLiteral("wall")), generatedPalette(QStringLiteral("doom")));
	expect(wall.ok && wall.exportOptions.format == TextureExportFormat::DoomPatch && wall.image.size() == QSize(64, 128), "A Doom wall is a 64x128 patch.");
	TextureGenerationSpec doomCompanions = spec(QStringLiteral("doom"), QStringLiteral("wall"));
	doomCompanions.companions = true;
	expect(processGeneratedTexture(picture, doomCompanions, generatedPalette(QStringLiteral("doom"))).companions.isEmpty(), "Doom gets no companion maps.");

	GeneratedTexture generic = processGeneratedTexture(periodicImage(QSize(512, 512)), spec(QStringLiteral("generic"), QStringLiteral("wall")), IdTechPaletteResolution());
	expect(generic.ok && generic.exportOptions.format == TextureExportFormat::Png && generic.encoded.bytes.startsWith("\x89PNG")
			&& generic.steps.join(QLatin1Char(' ')).contains(QStringLiteral("already tile")),
		"A generic texture is a PNG, and a tiling picture is left unblended.");

	expect(!processGeneratedTexture(picture, spec(QStringLiteral("unreal"), QStringLiteral("wall")), {}).ok, "Unknown games are refused.");
	expect(!processGeneratedTexture(picture, spec(QStringLiteral("quake2"), QStringLiteral("sky")), generatedPalette(QStringLiteral("quake2"))).ok,
		"Quake II skies are skyboxes, not one texture.");
	expect(!processGeneratedTexture(QImage(), spec(QStringLiteral("quake"), QStringLiteral("wall")), generatedPalette(QStringLiteral("quake"))).ok, "No picture, no texture.");
	TextureGenerationSpec odd = spec(QStringLiteral("quake"), QStringLiteral("wall"));
	odd.size = QSize(70, 64);
	expect(!processGeneratedTexture(picture, odd, generatedPalette(QStringLiteral("quake"))).ok, "Quake sizes must be multiples of 16.");
}

bool archiveHas(const QString& path, const QString& name, const QString& typeHint = QString())
{
	PackageArchive archive;
	if (!archive.load(path)) {
		return false;
	}
	for (const PackageEntry& entry : archive.entries()) {
		if (entry.virtualPath.compare(name, Qt::CaseInsensitive) == 0 && (typeHint.isEmpty() || entry.typeHint == typeHint)) {
			return true;
		}
	}
	return false;
}

void checkWriting()
{
	QTemporaryDir folder;
	expect(folder.isValid(), "A temporary folder is needed.");
	const QImage picture = rampImage(QSize(1024, 1024));

	// Quake: a WAD2, created, then refused, then replaced.
	TextureGenerationSpec quakeSpec = spec(QStringLiteral("quake"), QStringLiteral("wall"));
	quakeSpec.companions = true;
	const GeneratedTexture quake = processGeneratedTexture(picture, quakeSpec, generatedPalette(QStringLiteral("quake")));
	TextureGenerationOutput output;
	output.folder = folder.path();
	output.provenance = QJsonObject {{QStringLiteral("provider"), QStringLiteral("test")}};
	TextureGenerationOutput dry = output;
	dry.dryRun = true;
	const TextureGenerationWriteReport dryRun = writeGeneratedTexture(quake, quakeSpec, dry);
	const QString wad = folder.filePath(QStringLiteral("wads/vibestudio_generated.wad"));
	expect(dryRun.ok && dryRun.writtenPaths.contains(wad) && !QFileInfo::exists(wad), "A dry run lists the outputs and writes nothing.");
	TextureGenerationWriteReport report = writeGeneratedTexture(quake, quakeSpec, output);
	expect(report.ok && QFileInfo::exists(wad) && archiveHas(wad, QStringLiteral("rusted_riveted"), QStringLiteral("wad-texture")) && report.mapTextureName == QStringLiteral("rusted_riveted"),
		"A Quake texture is added to a new WAD2 under wads/.");
	expect(QFileInfo::exists(folder.filePath(QStringLiteral("textures/rusted_riveted_norm.tga"))) && QFileInfo::exists(folder.filePath(QStringLiteral("textures/rusted_riveted_gloss.tga"))),
		"Companions go to textures/ where DarkPlaces-style ports read them.");
	const QString record = folder.filePath(QStringLiteral(".vibestudio/generated/textures/rusted_riveted.json"));
	QFile recordFile(record);
	const bool recordOpened = recordFile.open(QIODevice::ReadOnly);
	const QJsonObject provenance = QJsonDocument::fromJson(recordFile.readAll()).object();
	expect(recordOpened && provenance.value(QStringLiteral("provider")).toString() == QStringLiteral("test")
			&& provenance.value(QStringLiteral("spec")).toObject().value(QStringLiteral("prompt")).toString() == QStringLiteral("rusted riveted metal plate")
			&& provenance.value(QStringLiteral("sha256")).toString().size() == 64,
		"The provenance record keeps the provider, the spec, and the texture's digest.");
	report = writeGeneratedTexture(quake, quakeSpec, output);
	expect(!report.ok && report.error.contains(QStringLiteral("rusted_riveted")), "Writing the same name again is refused without replacing.");
	output.replaceExisting = true;
	TextureGenerationSpec second = quakeSpec;
	second.name = QStringLiteral("second_plate");
	second.companions = false;
	report = writeGeneratedTexture(processGeneratedTexture(picture, second, generatedPalette(QStringLiteral("quake"))), second, output);
	expect(report.ok && archiveHas(wad, QStringLiteral("rusted_riveted")) && archiveHas(wad, QStringLiteral("second_plate")), "A second texture joins the same WAD.");

	// A Quake liquid's companions use # for *.
	TextureGenerationSpec slime = spec(QStringLiteral("quake"), QStringLiteral("liquid"), QStringLiteral("toxic slime"));
	slime.companions = true;
	report = writeGeneratedTexture(processGeneratedTexture(picture, slime, generatedPalette(QStringLiteral("quake"))), slime, output);
	expect(report.ok && archiveHas(wad, QStringLiteral("*toxic_slime")) && QFileInfo::exists(folder.filePath(QStringLiteral("textures/#toxic_slime_norm.tga"))),
		"A liquid keeps its * in the WAD and takes # in companion file names.");

	// Quake II: a WAL under textures/<folder>.
	const TextureGenerationSpec quake2Spec = spec(QStringLiteral("quake2"), QStringLiteral("wall"));
	report = writeGeneratedTexture(processGeneratedTexture(picture, quake2Spec, generatedPalette(QStringLiteral("quake2"))), quake2Spec, output);
	const QString wal = folder.filePath(QStringLiteral("textures/vibestudio/rusted_riveted_metal.wal"));
	QFile walFile(wal);
	const bool walOpened = walFile.open(QIODevice::ReadOnly);
	expect(report.ok && walOpened && walFile.read(32).startsWith("vibestudio/rusted_riveted_metal") && report.mapTextureName == QStringLiteral("vibestudio/rusted_riveted_metal"),
		"A Quake II texture is a WAL whose header names its path.");

	// Quake III: a TGA, and a shader for a glowing texture, written once.
	QImage lights = rampImage(QSize(1024, 1024));
	for (int y = 400; y < 600; ++y) {
		for (int x = 400; x < 600; ++x) {
			lights.setPixel(x, y, qRgb(255, 240, 60));
		}
	}
	TextureGenerationSpec quake3Spec = spec(QStringLiteral("quake3"), QStringLiteral("panel"), QStringLiteral("tech panel with yellow lights"));
	quake3Spec.companions = true;
	const GeneratedTexture panel = processGeneratedTexture(lights, quake3Spec, {});
	report = writeGeneratedTexture(panel, quake3Spec, output);
	const QString shaderPath = folder.filePath(QStringLiteral("scripts/vibestudio_generated.shader"));
	QFile shaderFile(shaderPath);
	const bool shaderOpened = shaderFile.open(QIODevice::ReadOnly);
	const QString shader = QString::fromUtf8(shaderFile.readAll());
	shaderFile.close();
	expect(report.ok && shaderOpened && QFileInfo::exists(folder.filePath(QStringLiteral("textures/vibestudio/tech_panel_yellow.tga")))
			&& QFileInfo::exists(folder.filePath(QStringLiteral("textures/vibestudio/tech_panel_yellow_glow.tga")))
			&& shader.contains(QStringLiteral("textures/vibestudio/tech_panel_yellow\n{")) && shader.contains(QStringLiteral("blendFunc GL_ONE GL_ONE")),
		"A glowing Quake III texture gets a TGA, a _glow map, and a shader with an additive stage.");
	report = writeGeneratedTexture(panel, quake3Spec, output);
	const bool shaderReopened = shaderFile.open(QIODevice::ReadOnly);
	expect(report.ok && shaderReopened && QString::fromUtf8(shaderFile.readAll()).count(QStringLiteral("textures/vibestudio/tech_panel_yellow\n{")) == 1,
		"The shader is not written twice.");
	shaderFile.close();

	// Doom: a flat inside F_START/F_END of a PWAD.
	const TextureGenerationSpec doomSpec = spec(QStringLiteral("doom"), QStringLiteral("floor"), QStringLiteral("cracked green marble"));
	TextureGenerationOutput doomOutput = output;
	doomOutput.wadPath = folder.filePath(QStringLiteral("doom/textures.wad"));
	report = writeGeneratedTexture(processGeneratedTexture(picture, doomSpec, generatedPalette(QStringLiteral("doom"))), doomSpec, doomOutput);
	expect(report.ok && archiveHas(doomOutput.wadPath, QStringLiteral("CRACKEDG"), QStringLiteral("wad-flat")), "A Doom flat lands between the flat markers of a PWAD.");

	// Generic: a PNG in the folder.
	const TextureGenerationSpec genericSpec = spec(QStringLiteral("generic"), QStringLiteral("wall"));
	report = writeGeneratedTexture(processGeneratedTexture(picture, genericSpec, {}), genericSpec, output);
	expect(report.ok && QFileInfo::exists(folder.filePath(QStringLiteral("rusted_riveted_metal.png"))), "A generic texture is a PNG in the folder.");
	expect(!writeGeneratedTexture(GeneratedTexture(), genericSpec, output).ok, "An unfinished texture is not written.");
}

void checkJson()
{
	TextureGenerationSpec original = spec(QStringLiteral("quake2"), QStringLiteral("liquid"), QStringLiteral("murky water"));
	original.size = QSize(128, 64);
	original.companions = true;
	original.directory = QStringLiteral("e2u1");
	TextureGenerationSpec parsed;
	QString error;
	expect(textureGenerationSpecFromJson(textureGenerationSpecJson(original), &parsed, &error) && parsed.prompt == original.prompt && parsed.size == original.size
			&& parsed.companions && parsed.directory == QStringLiteral("e2u1") && parsed.surface == QStringLiteral("liquid"),
		"A spec survives JSON.");
	QJsonObject bad = textureGenerationSpecJson(original);
	bad.insert(QStringLiteral("surface"), QStringLiteral("roof"));
	expect(!textureGenerationSpecFromJson(bad, &parsed, &error) && error.contains(QStringLiteral("roof")), "An unknown surface in JSON is refused.");
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	checkProfiles();
	checkNamesAndSizes();
	checkPrompts();
	checkImageSteps();
	checkProcessing();
	checkWriting();
	checkJson();
	if (failures > 0) {
		std::cerr << failures << " texture generation check(s) failed.\n";
		return EXIT_FAILURE;
	}
	std::cout << "Texture generation checks passed.\n";
	return EXIT_SUCCESS;
}
