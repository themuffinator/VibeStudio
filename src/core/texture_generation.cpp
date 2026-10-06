#include "core/texture_generation.h"

#include "core/package_archive.h"
#include "core/package_staging.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace vibestudio {

namespace {

constexpr double kSeamlessEnough = 1.35;

struct Rgba {
	double r = 0.0;
	double g = 0.0;
	double b = 0.0;
	double a = 0.0;
};

// Premultiplied floating-point pixels, so averaging never bleeds the colour
// of transparent pixels into their neighbours.
struct FloatImage {
	int width = 0;
	int height = 0;
	std::vector<Rgba> pixels;

	Rgba& at(int x, int y)
	{
		return pixels[size_t(y) * size_t(width) + size_t(x)];
	}
	[[nodiscard]] const Rgba& at(int x, int y) const
	{
		return pixels[size_t(y) * size_t(width) + size_t(x)];
	}
};

FloatImage toFloat(const QImage& source)
{
	const QImage image = source.convertToFormat(QImage::Format_ARGB32);
	FloatImage out;
	out.width = image.width();
	out.height = image.height();
	out.pixels.resize(size_t(out.width) * size_t(out.height));
	for (int y = 0; y < out.height; ++y) {
		const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
		for (int x = 0; x < out.width; ++x) {
			const double alpha = qAlpha(row[x]) / 255.0;
			out.at(x, y) = {qRed(row[x]) / 255.0 * alpha, qGreen(row[x]) / 255.0 * alpha, qBlue(row[x]) / 255.0 * alpha, alpha};
		}
	}
	return out;
}

QImage fromFloat(const FloatImage& source)
{
	QImage image(source.width, source.height, QImage::Format_ARGB32);
	for (int y = 0; y < source.height; ++y) {
		auto* row = reinterpret_cast<QRgb*>(image.scanLine(y));
		for (int x = 0; x < source.width; ++x) {
			const Rgba& pixel = source.at(x, y);
			const double alpha = std::clamp(pixel.a, 0.0, 1.0);
			const auto channel = [alpha](double value) {
				return alpha <= 0.0 ? 0 : std::clamp(int(std::lround(value / alpha * 255.0)), 0, 255);
			};
			row[x] = qRgba(channel(pixel.r), channel(pixel.g), channel(pixel.b), int(std::lround(alpha * 255.0)));
		}
	}
	return image;
}

int wrapIndex(int index, int length)
{
	const int wrapped = index % length;
	return wrapped < 0 ? wrapped + length : wrapped;
}

int sourceIndex(int index, int length, bool wrap)
{
	return wrap ? wrapIndex(index, length) : std::clamp(index, 0, length - 1);
}

// One row or column, resampled. Shrinking averages the source pixels each
// output pixel covers, weighted by how much of each it covers; enlarging
// interpolates between the two nearest.
void resampleLine(const std::vector<Rgba>& source, std::vector<Rgba>* out, int outLength, bool wrap)
{
	const int inLength = int(source.size());
	out->assign(size_t(outLength), Rgba());
	const double scale = double(inLength) / double(outLength);
	for (int index = 0; index < outLength; ++index) {
		Rgba sum;
		if (scale >= 1.0) {
			const double start = index * scale;
			const double end = start + scale;
			double total = 0.0;
			for (int sample = int(std::floor(start)); sample < int(std::ceil(end)); ++sample) {
				const double weight = std::min(end, double(sample + 1)) - std::max(start, double(sample));
				if (weight <= 0.0) {
					continue;
				}
				const Rgba& pixel = source[size_t(sourceIndex(sample, inLength, wrap))];
				sum.r += pixel.r * weight;
				sum.g += pixel.g * weight;
				sum.b += pixel.b * weight;
				sum.a += pixel.a * weight;
				total += weight;
			}
			if (total > 0.0) {
				sum = {sum.r / total, sum.g / total, sum.b / total, sum.a / total};
			}
		} else {
			const double position = (index + 0.5) * scale - 0.5;
			const int left = int(std::floor(position));
			const double t = position - left;
			const Rgba& a = source[size_t(sourceIndex(left, inLength, wrap))];
			const Rgba& b = source[size_t(sourceIndex(left + 1, inLength, wrap))];
			sum = {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
		}
		(*out)[size_t(index)] = sum;
	}
}

double luminance(QRgb color)
{
	return (0.2126 * qRed(color) + 0.7152 * qGreen(color) + 0.0722 * qBlue(color)) / 255.0;
}

double saturation(QRgb color)
{
	const int high = std::max({qRed(color), qGreen(color), qBlue(color)});
	const int low = std::min({qRed(color), qGreen(color), qBlue(color)});
	return high == 0 ? 0.0 : double(high - low) / double(high);
}

double colourStep(QRgb a, QRgb b)
{
	return (std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b)) + std::abs(qBlue(a) - qBlue(b))) / (3.0 * 255.0);
}

// Smoothstep from 1 at an edge to 0 at the inside of the band.
double edgeWeight(int position, int length, int band)
{
	const int distance = std::min(position, length - 1 - position);
	if (band <= 0 || distance >= band) {
		return 0.0;
	}
	const double t = 1.0 - double(distance) / double(band);
	return t * t * (3.0 - 2.0 * t);
}

QString surfaceWords(const QString& surface)
{
	// Prompt text: kept in English, like the assistant's instructions.
	if (surface == QStringLiteral("floor")) {
		return QStringLiteral("A floor surface, seen from directly above");
	}
	if (surface == QStringLiteral("ceiling")) {
		return QStringLiteral("A ceiling surface, seen from directly below");
	}
	if (surface == QStringLiteral("trim")) {
		return QStringLiteral("A horizontal trim strip that repeats from left to right");
	}
	if (surface == QStringLiteral("panel")) {
		return QStringLiteral("A single decorative wall panel, seen straight on");
	}
	if (surface == QStringLiteral("liquid")) {
		return QStringLiteral("A liquid surface seen from above, with swirling detail");
	}
	if (surface == QStringLiteral("sky")) {
		return QStringLiteral("A sky of clouds or space, with no ground, horizon, or sun disc");
	}
	return QStringLiteral("A wall surface, seen straight on");
}

QString liquidKind(const TextureGenerationSpec& spec)
{
	const QString words = (spec.prompt + QLatin1Char(' ') + spec.name).toLower();
	if (words.contains(QStringLiteral("lava")) || words.contains(QStringLiteral("magma"))) {
		return QStringLiteral("lava");
	}
	if (words.contains(QStringLiteral("slime")) || words.contains(QStringLiteral("acid")) || words.contains(QStringLiteral("toxic")) || words.contains(QStringLiteral("nukage"))) {
		return QStringLiteral("slime");
	}
	return QStringLiteral("water");
}

QString cleanedName(const QString& text, bool upper, int maxLength)
{
	QString cleaned;
	bool lastUnderscore = true;
	for (const QChar character : text) {
		const ushort c = character.toLower().unicode();
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
			cleaned += upper ? QChar(c).toUpper() : QChar(c);
			lastUnderscore = false;
		} else if (!lastUnderscore) {
			cleaned += QLatin1Char('_');
			lastUnderscore = true;
		}
	}
	while (cleaned.endsWith(QLatin1Char('_'))) {
		cleaned.chop(1);
	}
	cleaned = cleaned.left(std::max(1, maxLength));
	while (cleaned.endsWith(QLatin1Char('_'))) {
		cleaned.chop(1);
	}
	return cleaned;
}

QString derivedBaseName(const QString& prompt)
{
	static const QStringList skipped = {
		QStringLiteral("a"),
		QStringLiteral("an"),
		QStringLiteral("the"),
		QStringLiteral("of"),
		QStringLiteral("with"),
		QStringLiteral("and"),
		QStringLiteral("in"),
		QStringLiteral("on"),
		QStringLiteral("for"),
		QStringLiteral("texture"),
		QStringLiteral("seamless"),
		QStringLiteral("tileable"),
		QStringLiteral("tiling"),
		QStringLiteral("game"),
	};
	QStringList words;
	static const QRegularExpression separators(QStringLiteral("[^A-Za-z0-9]+"));
	for (const QString& word : prompt.toLower().split(separators, Qt::SkipEmptyParts)) {
		if (!skipped.contains(word)) {
			words << word;
		}
		if (words.size() == 3) {
			break;
		}
	}
	return words.join(QLatin1Char('_'));
}

QString profileDirectory(const TextureGenerationSpec& spec)
{
	const QString directory = cleanedName(spec.directory, false, 24);
	return directory.isEmpty() ? QStringLiteral("vibestudio") : directory;
}

bool writeBytes(const QString& path, const QByteArray& bytes, bool replace, QString* error)
{
	if (QFileInfo::exists(path) && !replace) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioTextureGeneration", "%1 already exists. Allow replacing to write over it.").arg(QDir::toNativeSeparators(path));
		}
		return false;
	}
	if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioTextureGeneration", "Could not create the folder for %1.").arg(QDir::toNativeSeparators(path));
		}
		return false;
	}
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioTextureGeneration", "Could not write %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
		}
		return false;
	}
	return true;
}

QByteArray targaBytes(const QImage& image, QString* error)
{
	TextureExportOptions options;
	options.format = TextureExportFormat::Targa;
	const TextureExportResult encoded = encodeTextureExport(image, options);
	if (!encoded.succeeded && error) {
		*error = encoded.error;
	}
	return encoded.succeeded ? encoded.bytes : QByteArray();
}

// A Quake III shader for the surfaces that need one: a liquid's contents and
// motion, or the additive stage that makes a glow map glow.
QString quake3Shader(const QString& mapName, const QString& liquid, bool glow, const QString& glowSuffix)
{
	QStringList lines;
	lines << QStringLiteral("textures/%1").arg(mapName) << QStringLiteral("{");
	lines << QStringLiteral("\tqer_editorimage textures/%1.tga").arg(mapName);
	if (!liquid.isEmpty()) {
		lines << QStringLiteral("\tqer_trans 0.5") << QStringLiteral("\tsurfaceparm nonsolid") << QStringLiteral("\tsurfaceparm trans")
			  << QStringLiteral("\tsurfaceparm %1").arg(liquid) << QStringLiteral("\tsurfaceparm nomarks")
			  << QStringLiteral("\tdeformVertexes wave 64 sin 0 2 0 0.4");
		if (liquid == QStringLiteral("lava")) {
			lines << QStringLiteral("\tsurfaceparm noimpact") << QStringLiteral("\tq3map_surfacelight 300");
		}
		lines << QStringLiteral("\t{") << QStringLiteral("\t\tmap textures/%1.tga").arg(mapName) << QStringLiteral("\t\ttcMod turb 0 0.15 0 0.1");
		if (liquid == QStringLiteral("water")) {
			lines << QStringLiteral("\t\tblendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA") << QStringLiteral("\t\talphaGen const 0.6");
		}
		lines << QStringLiteral("\t\trgbGen identity") << QStringLiteral("\t}");
		if (liquid == QStringLiteral("water")) {
			lines << QStringLiteral("\t{") << QStringLiteral("\t\tmap $lightmap") << QStringLiteral("\t\tblendFunc GL_DST_COLOR GL_ZERO")
				  << QStringLiteral("\t\trgbGen identity") << QStringLiteral("\t}");
		}
	} else {
		lines << QStringLiteral("\t{") << QStringLiteral("\t\tmap $lightmap") << QStringLiteral("\t\trgbGen identity") << QStringLiteral("\t}");
		lines << QStringLiteral("\t{") << QStringLiteral("\t\tmap textures/%1.tga").arg(mapName) << QStringLiteral("\t\tblendFunc GL_DST_COLOR GL_ZERO")
			  << QStringLiteral("\t\trgbGen identity") << QStringLiteral("\t}");
	}
	if (glow) {
		lines << QStringLiteral("\t{") << QStringLiteral("\t\tmap textures/%1%2.tga").arg(mapName, glowSuffix) << QStringLiteral("\t\tblendFunc GL_ONE GL_ONE")
			  << QStringLiteral("\t\trgbGen identity") << QStringLiteral("\t}");
	}
	lines << QStringLiteral("}");
	return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

} // namespace

QVector<TextureGameProfile> textureGameProfiles()
{
	// Era hints are prompt text, so they stay in English.
	return {
		{QStringLiteral("quake"), QCoreApplication::translate("VibeStudioTextureGeneration", "Quake"), QStringLiteral("quake"), QSize(64, 64), QSize(64, 64), 15,
			QStringLiteral("the gritty, dark gothic-industrial look of Quake (1996): muted browns, rust, slate and cold metal greys, with chunky shapes that stay readable at 64 pixels"),
			QStringLiteral("_norm"), QStringLiteral("_gloss"), QStringLiteral("_glow")},
		{QStringLiteral("quake2"), QCoreApplication::translate("VibeStudioTextureGeneration", "Quake II"), QStringLiteral("quake2"), QSize(128, 128), QSize(64, 64), 31,
			QStringLiteral("the industrial alien-base look of Quake II (1997): rusted steel, orange and brown tones, grime, rivets, grates and pipes"),
			QStringLiteral("_norm"), QStringLiteral("_gloss"), QStringLiteral("_glow")},
		{QStringLiteral("quake3"), QCoreApplication::translate("VibeStudioTextureGeneration", "Quake III Arena"), QString(), QSize(256, 256), QSize(256, 256), 63,
			QStringLiteral("the arena look of Quake III Arena (1999): gothic stone, ornate bronze and steel, crisp painted detail"),
			QStringLiteral("_n"), QStringLiteral("_s"), QStringLiteral("_glow")},
		{QStringLiteral("doom"), QCoreApplication::translate("VibeStudioTextureGeneration", "Doom"), QStringLiteral("doom"), QSize(64, 128), QSize(64, 64), 8,
			QStringLiteral("the hand-painted look of Doom (1993): bold shapes, strong contrast and few colours, readable at 64 pixels"), QString(), QString(), QString()},
		{QStringLiteral("heretic"), QCoreApplication::translate("VibeStudioTextureGeneration", "Heretic"), QStringLiteral("heretic"), QSize(64, 128), QSize(64, 64), 8,
			QStringLiteral("the dark fantasy look of Heretic (1994): weathered stone, wood, iron and moss"), QString(), QString(), QString()},
		{QStringLiteral("hexen"), QCoreApplication::translate("VibeStudioTextureGeneration", "Hexen"), QStringLiteral("hexen"), QSize(64, 128), QSize(64, 64), 8,
			QStringLiteral("the dark fantasy look of Hexen (1995): ancient stone, wood, bronze and stained glass"), QString(), QString(), QString()},
		{QStringLiteral("generic"), QCoreApplication::translate("VibeStudioTextureGeneration", "Generic (PNG)"), QString(), QSize(256, 256), QSize(256, 256), 64, QString(),
			QStringLiteral("_norm"), QStringLiteral("_gloss"), QStringLiteral("_glow")},
	};
}

QStringList textureGameProfileIds()
{
	QStringList ids;
	for (const TextureGameProfile& profile : textureGameProfiles()) {
		ids << profile.id;
	}
	return ids;
}

bool textureGameProfileForId(const QString& id, TextureGameProfile* out)
{
	QString normalized = id.trimmed().toLower();
	if (normalized == QStringLiteral("quake1") || normalized == QStringLiteral("q1") || normalized == QStringLiteral("idtech2")) {
		normalized = QStringLiteral("quake");
	} else if (normalized == QStringLiteral("q2")) {
		normalized = QStringLiteral("quake2");
	} else if (normalized == QStringLiteral("q3") || normalized == QStringLiteral("quake3arena") || normalized == QStringLiteral("idtech3")) {
		normalized = QStringLiteral("quake3");
	} else if (normalized == QStringLiteral("doom2") || normalized == QStringLiteral("idtech1")) {
		normalized = QStringLiteral("doom");
	} else if (normalized == QStringLiteral("png")) {
		normalized = QStringLiteral("generic");
	}
	for (const TextureGameProfile& profile : textureGameProfiles()) {
		if (profile.id == normalized) {
			if (out) {
				*out = profile;
			}
			return true;
		}
	}
	return false;
}

QStringList textureGenerationSurfaceIds()
{
	return {
		QStringLiteral("wall"),
		QStringLiteral("floor"),
		QStringLiteral("ceiling"),
		QStringLiteral("trim"),
		QStringLiteral("panel"),
		QStringLiteral("liquid"),
		QStringLiteral("sky"),
	};
}

QString textureGenerationName(const TextureGenerationSpec& spec)
{
	TextureGameProfile profile;
	textureGameProfileForId(spec.game, &profile);
	QString base = spec.name.trimmed();
	if (base.startsWith(QLatin1Char('*'))) {
		base = base.mid(1);
	}
	if (base.isEmpty()) {
		base = derivedBaseName(spec.prompt);
	}
	const bool doomFamily = profile.id == QStringLiteral("doom") || profile.id == QStringLiteral("heretic") || profile.id == QStringLiteral("hexen");
	if (doomFamily) {
		QString name = cleanedName(base, true, 64);
		name.remove(QLatin1Char('_'));
		name = name.left(8);
		return name.isEmpty() ? QStringLiteral("GENTEX") : name;
	}
	if (profile.id == QStringLiteral("quake")) {
		QString prefix;
		if (spec.surface == QStringLiteral("liquid")) {
			prefix = QStringLiteral("*");
		} else if (spec.surface == QStringLiteral("sky") && !base.toLower().startsWith(QStringLiteral("sky"))) {
			prefix = QStringLiteral("sky");
		}
		QString name = cleanedName(base, false, profile.maxNameLength - int(prefix.size()));
		if (name.isEmpty()) {
			name = QStringLiteral("generated");
		}
		return prefix + name;
	}
	int room = profile.maxNameLength;
	if (profile.id == QStringLiteral("quake2") || profile.id == QStringLiteral("quake3")) {
		room -= int(profileDirectory(spec).size()) + 1;
	}
	const QString name = cleanedName(base, false, room);
	return name.isEmpty() ? QStringLiteral("generated") : name;
}

QString textureGenerationMapName(const TextureGenerationSpec& spec)
{
	TextureGameProfile profile;
	textureGameProfileForId(spec.game, &profile);
	const QString name = textureGenerationName(spec);
	if (profile.id == QStringLiteral("quake2") || profile.id == QStringLiteral("quake3")) {
		return QStringLiteral("%1/%2").arg(profileDirectory(spec), name);
	}
	return name;
}

TextureGenerationSpec textureGenerationVariantSpec(const TextureGenerationSpec& spec, int index, int count)
{
	if (count <= 1) {
		return spec;
	}
	TextureGameProfile profile;
	textureGameProfileForId(spec.game, &profile);
	QString base = textureGenerationName(spec);
	base.remove(QLatin1Char('*'));
	if (spec.surface == QStringLiteral("sky") && base.startsWith(QStringLiteral("sky"))) {
		base = base.mid(3);
	}
	const bool doomFamily = profile.id == QStringLiteral("doom") || profile.id == QStringLiteral("heretic") || profile.id == QStringLiteral("hexen");
	const QString suffix = QString::number(index + 1);
	// Room left for the base once prefixes (*, sky) and folders come back.
	int room = profile.maxNameLength - int(suffix.size());
	if (profile.id == QStringLiteral("quake")) {
		room -= 3;
	} else if (profile.id == QStringLiteral("quake2") || profile.id == QStringLiteral("quake3")) {
		room -= int(profileDirectory(spec).size()) + 1;
	} else if (doomFamily) {
		room = 8 - int(suffix.size());
	}
	TextureGenerationSpec variant = spec;
	variant.name = base.left(std::max(1, room)) + suffix;
	return variant;
}

QSize textureGenerationOutputSize(const TextureGenerationSpec& spec)
{
	TextureGameProfile profile;
	textureGameProfileForId(spec.game, &profile);
	const bool doomFamily = profile.id == QStringLiteral("doom") || profile.id == QStringLiteral("heretic") || profile.id == QStringLiteral("hexen");
	const bool flat = spec.surface == QStringLiteral("floor") || spec.surface == QStringLiteral("ceiling") || spec.surface == QStringLiteral("liquid");
	// Fixed by the format, whatever was asked.
	if (doomFamily && flat) {
		return QSize(64, 64);
	}
	if (profile.id == QStringLiteral("quake") && spec.surface == QStringLiteral("sky")) {
		return QSize(256, 128);
	}
	if (spec.size.isValid() && !spec.size.isEmpty()) {
		return spec.size;
	}
	if (spec.surface == QStringLiteral("trim")) {
		return QSize(profile.wallSize.width(), std::max(16, profile.wallSize.width() / 4));
	}
	return flat ? profile.floorSize : profile.wallSize;
}

QSize textureGenerationRequestSize(const TextureGenerationSpec& spec)
{
	const QSize output = textureGenerationOutputSize(spec);
	// A Quake sky's layers are square halves; ask for one square layer.
	if (spec.surface == QStringLiteral("sky")) {
		return QSize(1024, 1024);
	}
	if (output.width() >= output.height()) {
		return QSize(1024, std::max(1, int(std::lround(1024.0 * output.height() / output.width()))));
	}
	return QSize(std::max(1, int(std::lround(1024.0 * output.width() / output.height()))), 1024);
}

QString textureGenerationPrompt(const TextureGenerationSpec& spec)
{
	TextureGameProfile profile;
	textureGameProfileForId(spec.game, &profile);
	const QSize output = textureGenerationOutputSize(spec);
	QString subject = spec.prompt.trimmed();
	if (!spec.style.trimmed().isEmpty()) {
		subject += QStringLiteral(", %1").arg(spec.style.trimmed());
	}
	QStringList parts;
	parts << QStringLiteral("%1, as a texture for a 1990s first-person shooter: %2.").arg(surfaceWords(spec.surface), subject);
	if (spec.surface == QStringLiteral("sky")) {
		parts << QStringLiteral("Fill the whole square with sky, evenly, so it can wrap around the player.");
	} else {
		parts << QStringLiteral("Show only the material, filling the whole frame edge to edge, viewed straight on with no perspective, evenly lit with no cast shadows, highlights or vignette.");
	}
	if (spec.seamless) {
		parts << QStringLiteral("It must tile seamlessly: the left edge continues into the right edge and the top into the bottom.");
	}
	if (!profile.eraHint.isEmpty()) {
		parts << QStringLiteral("Match %1.").arg(profile.eraHint);
	}
	if (!profile.paletteId.isEmpty()) {
		parts << QStringLiteral("It will be reduced to %1x%2 pixels and a 256-colour palette, so use clear shapes and moderate contrast rather than fine noise.")
					 .arg(output.width())
					 .arg(output.height());
	}
	if (spec.surface == QStringLiteral("liquid")) {
		parts << QStringLiteral("The liquid is %1.").arg(liquidKind(spec));
	}
	parts << QStringLiteral("No text, letters, numbers, logos, signatures, watermarks, borders, frames, people or creatures.");
	return parts.join(QLatin1Char(' '));
}

QString textureGenerationNegativePrompt(const TextureGenerationSpec& spec)
{
	QString negative = QStringLiteral("text, letters, numbers, logo, watermark, signature, border, frame, people, creatures, blurry, low contrast");
	if (spec.surface != QStringLiteral("sky")) {
		negative += QStringLiteral(", perspective, vanishing point, room, scene, cast shadows, vignette, lens flare");
	}
	return negative;
}

double textureSeamScore(const QImage& source)
{
	const QImage image = source.convertToFormat(QImage::Format_ARGB32);
	const int width = image.width();
	const int height = image.height();
	if (width < 3 || height < 3) {
		return 0.0;
	}
	double edge = 0.0;
	for (int y = 0; y < height; ++y) {
		edge += colourStep(image.pixel(width - 1, y), image.pixel(0, y));
	}
	for (int x = 0; x < width; ++x) {
		edge += colourStep(image.pixel(x, height - 1), image.pixel(x, 0));
	}
	edge /= double(width + height);
	double interior = 0.0;
	qint64 samples = 0;
	for (int y = 0; y < height - 1; ++y) {
		const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
		const auto* next = reinterpret_cast<const QRgb*>(image.constScanLine(y + 1));
		for (int x = 0; x < width - 1; ++x) {
			interior += colourStep(row[x], row[x + 1]) + colourStep(row[x], next[x]);
			samples += 2;
		}
	}
	interior = samples > 0 ? interior / double(samples) : 0.0;
	// A flat image has no steps anywhere; any seam on it is all the step there is.
	return edge / std::max(interior, 1.0 / 255.0);
}

QImage makeTextureSeamless(const QImage& source, int bandPercent)
{
	if (source.isNull()) {
		return {};
	}
	FloatImage image = toFloat(source);
	const int width = image.width;
	const int height = image.height;
	const int bandX = std::clamp(width * std::clamp(bandPercent, 1, 45) / 100, 1, std::max(1, width / 2 - 1));
	const int bandY = std::clamp(height * std::clamp(bandPercent, 1, 45) / 100, 1, std::max(1, height / 2 - 1));
	// Across: each edge band fades into the image's own middle, shifted half
	// a width, so the left and right edges meet pixels that were neighbours.
	FloatImage across = image;
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			const double weight = edgeWeight(x, width, bandX);
			if (weight <= 0.0) {
				continue;
			}
			const Rgba& own = image.at(x, y);
			const Rgba& shifted = image.at(wrapIndex(x + width / 2, width), y);
			across.at(x, y) = {own.r + (shifted.r - own.r) * weight, own.g + (shifted.g - own.g) * weight, own.b + (shifted.b - own.b) * weight,
				own.a + (shifted.a - own.a) * weight};
		}
	}
	// Then down, the same way; rows mix with one weight, so across stays seamless.
	FloatImage down = across;
	for (int y = 0; y < height; ++y) {
		const double weight = edgeWeight(y, height, bandY);
		if (weight <= 0.0) {
			continue;
		}
		const int shiftedRow = wrapIndex(y + height / 2, height);
		for (int x = 0; x < width; ++x) {
			const Rgba& own = across.at(x, y);
			const Rgba& shifted = across.at(x, shiftedRow);
			down.at(x, y) = {own.r + (shifted.r - own.r) * weight, own.g + (shifted.g - own.g) * weight, own.b + (shifted.b - own.b) * weight,
				own.a + (shifted.a - own.a) * weight};
		}
	}
	return fromFloat(down);
}

QImage resampleTexture(const QImage& source, QSize size, bool wrap)
{
	if (source.isNull() || !size.isValid() || size.isEmpty()) {
		return {};
	}
	const FloatImage image = toFloat(source);
	if (image.width == size.width() && image.height == size.height()) {
		return source.convertToFormat(QImage::Format_ARGB32);
	}
	// Rows first, then columns.
	FloatImage wide;
	wide.width = size.width();
	wide.height = image.height;
	wide.pixels.resize(size_t(wide.width) * size_t(wide.height));
	std::vector<Rgba> line;
	std::vector<Rgba> resampled;
	for (int y = 0; y < image.height; ++y) {
		line.assign(image.pixels.begin() + ptrdiff_t(y) * image.width, image.pixels.begin() + ptrdiff_t(y + 1) * image.width);
		resampleLine(line, &resampled, wide.width, wrap);
		std::copy(resampled.begin(), resampled.end(), wide.pixels.begin() + ptrdiff_t(y) * wide.width);
	}
	FloatImage out;
	out.width = size.width();
	out.height = size.height();
	out.pixels.resize(size_t(out.width) * size_t(out.height));
	line.resize(size_t(wide.height));
	for (int x = 0; x < wide.width; ++x) {
		for (int y = 0; y < wide.height; ++y) {
			line[size_t(y)] = wide.at(x, y);
		}
		resampleLine(line, &resampled, out.height, wrap);
		for (int y = 0; y < out.height; ++y) {
			out.at(x, y) = resampled[size_t(y)];
		}
	}
	return fromFloat(out);
}

QImage cropTextureToAspect(const QImage& image, QSize aspect)
{
	if (image.isNull() || !aspect.isValid() || aspect.isEmpty()) {
		return image;
	}
	const double wanted = double(aspect.width()) / double(aspect.height());
	const double have = double(image.width()) / double(image.height());
	if (std::abs(wanted - have) < 1e-3) {
		return image;
	}
	if (have > wanted) {
		const int width = std::max(1, int(std::lround(image.height() * wanted)));
		return image.copy((image.width() - width) / 2, 0, width, image.height());
	}
	const int height = std::max(1, int(std::lround(image.width() / wanted)));
	return image.copy(0, (image.height() - height) / 2, image.width(), height);
}

QImage deriveTextureNormalMap(const QImage& source, double strength, bool wrap)
{
	if (source.isNull()) {
		return {};
	}
	const QImage image = source.convertToFormat(QImage::Format_ARGB32);
	const int width = image.width();
	const int height = image.height();
	std::vector<double> heights(size_t(width) * size_t(height));
	for (int y = 0; y < height; ++y) {
		const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
		for (int x = 0; x < width; ++x) {
			heights[size_t(y) * size_t(width) + size_t(x)] = luminance(row[x]);
		}
	}
	const auto h = [&](int x, int y) {
		return heights[size_t(sourceIndex(y, height, wrap)) * size_t(width) + size_t(sourceIndex(x, width, wrap))];
	};
	QImage out(width, height, QImage::Format_ARGB32);
	for (int y = 0; y < height; ++y) {
		auto* row = reinterpret_cast<QRgb*>(out.scanLine(y));
		for (int x = 0; x < width; ++x) {
			// Sobel: brightness rising to the right tilts the normal left, and
			// rising down the image tilts it up (green up, OpenGL style).
			const double dx = (h(x + 1, y - 1) + 2.0 * h(x + 1, y) + h(x + 1, y + 1)) - (h(x - 1, y - 1) + 2.0 * h(x - 1, y) + h(x - 1, y + 1));
			const double dy = (h(x - 1, y + 1) + 2.0 * h(x, y + 1) + h(x + 1, y + 1)) - (h(x - 1, y - 1) + 2.0 * h(x, y - 1) + h(x + 1, y - 1));
			double nx = -dx * strength / 4.0;
			double ny = dy * strength / 4.0;
			double nz = 1.0;
			const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
			nx /= length;
			ny /= length;
			nz /= length;
			const auto encode = [](double value) { return std::clamp(int(std::lround((value * 0.5 + 0.5) * 255.0)), 0, 255); };
			row[x] = qRgba(encode(nx), encode(ny), encode(nz), std::clamp(int(std::lround(h(x, y) * 255.0)), 0, 255));
		}
	}
	return out;
}

QImage deriveTextureGlossMap(const QImage& source)
{
	if (source.isNull()) {
		return {};
	}
	const QImage image = source.convertToFormat(QImage::Format_ARGB32);
	QImage out(image.size(), QImage::Format_ARGB32);
	for (int y = 0; y < image.height(); ++y) {
		const auto* in = reinterpret_cast<const QRgb*>(image.constScanLine(y));
		auto* row = reinterpret_cast<QRgb*>(out.scanLine(y));
		for (int x = 0; x < image.width(); ++x) {
			// Bare metal is bright and grey; paint, rust and stone are darker
			// or coloured, and shine less.
			const double gloss = std::clamp(0.08 + 0.85 * std::pow(luminance(in[x]), 1.6) * (1.0 - 0.6 * saturation(in[x])), 0.0, 1.0);
			const int value = int(std::lround(gloss * 255.0));
			row[x] = qRgb(value, value, value);
		}
	}
	return out;
}

QImage deriveTextureGlowMap(const QImage& source, double threshold, const QImage& indexed, int fullbrightStart)
{
	if (source.isNull()) {
		return {};
	}
	const QImage image = source.convertToFormat(QImage::Format_ARGB32);
	QImage out(image.size(), QImage::Format_ARGB32);
	out.fill(Qt::black);
	bool any = false;
	const bool fromPalette = !indexed.isNull() && indexed.format() == QImage::Format_Indexed8 && indexed.size() == image.size() && fullbrightStart > 0;
	for (int y = 0; y < image.height(); ++y) {
		const auto* in = reinterpret_cast<const QRgb*>(image.constScanLine(y));
		auto* row = reinterpret_cast<QRgb*>(out.scanLine(y));
		const uchar* indices = fromPalette ? indexed.constScanLine(y) : nullptr;
		for (int x = 0; x < image.width(); ++x) {
			bool glows = false;
			if (indices) {
				glows = indices[x] >= fullbrightStart;
			} else {
				const double light = luminance(in[x]);
				glows = (light >= threshold && saturation(in[x]) >= 0.3) || light >= std::min(0.98, threshold + 0.12);
			}
			if (glows) {
				row[x] = indices ? (indexed.color(indices[x]) | 0xff000000u) : (in[x] | 0xff000000u);
				any = true;
			}
		}
	}
	return any ? out : QImage();
}

GeneratedTexture processGeneratedTexture(const QImage& raw, const TextureGenerationSpec& spec, const IdTechPaletteResolution& palette)
{
	GeneratedTexture out;
	TextureGameProfile profile;
	if (!textureGameProfileForId(spec.game, &profile)) {
		out.error = QCoreApplication::translate("VibeStudioTextureGeneration", "Unknown texture game \"%1\". Use one of: %2.").arg(spec.game, textureGameProfileIds().join(QStringLiteral(", ")));
		return out;
	}
	if (!textureGenerationSurfaceIds().contains(spec.surface)) {
		out.error = QCoreApplication::translate("VibeStudioTextureGeneration", "Unknown surface \"%1\". Use one of: %2.").arg(spec.surface, textureGenerationSurfaceIds().join(QStringLiteral(", ")));
		return out;
	}
	if (raw.isNull() || raw.width() < 4 || raw.height() < 4) {
		out.error = QCoreApplication::translate("VibeStudioTextureGeneration", "There is no picture to make a texture from.");
		return out;
	}
	const bool doomFamily = profile.id == QStringLiteral("doom") || profile.id == QStringLiteral("heretic") || profile.id == QStringLiteral("hexen");
	const bool quakeSky = profile.id == QStringLiteral("quake") && spec.surface == QStringLiteral("sky");
	if (spec.surface == QStringLiteral("sky") && profile.id != QStringLiteral("quake") && profile.id != QStringLiteral("generic")) {
		out.error = QCoreApplication::translate("VibeStudioTextureGeneration", "%1 skies are skyboxes or shaders, not one texture; generate a sky for Quake or as a generic PNG.").arg(profile.displayName);
		return out;
	}
	out.name = textureGenerationName(spec);
	out.mapName = textureGenerationMapName(spec);
	const QSize outputSize = textureGenerationOutputSize(spec);
	if (!profile.paletteId.isEmpty() && profile.id != QStringLiteral("quake3") && (outputSize.width() % 16 || outputSize.height() % 16) && !doomFamily) {
		out.error = QCoreApplication::translate("VibeStudioTextureGeneration", "%1 textures need a width and height divisible by 16.").arg(profile.displayName);
		return out;
	}
	const QSize layerSize = quakeSky ? QSize(128, 128) : outputSize;

	// 1. Shape.
	QImage working = cropTextureToAspect(raw.convertToFormat(QImage::Format_ARGB32), layerSize);
	if (working.size() != raw.size()) {
		out.steps << QCoreApplication::translate("VibeStudioTextureGeneration", "Cropped %1x%2 to %3x%4 for the texture's shape.")
						 .arg(raw.width())
						 .arg(raw.height())
						 .arg(working.width())
						 .arg(working.height());
	}
	// Blend at up to four times the final size: wide enough bands, little work.
	const QSize blendSize(std::min(working.width(), layerSize.width() * 4), std::min(working.height(), layerSize.height() * 4));
	if (blendSize != working.size()) {
		working = resampleTexture(working, blendSize, false);
	}
	// 2. Seams.
	out.seamScoreBefore = textureSeamScore(working);
	if (spec.seamless && out.seamScoreBefore > kSeamlessEnough) {
		working = makeTextureSeamless(working, spec.seamBlendPercent);
		out.steps << QCoreApplication::translate("VibeStudioTextureGeneration", "Blended the edges to tile (seam %1 before, %2 after).")
						 .arg(out.seamScoreBefore, 0, 'f', 2)
						 .arg(textureSeamScore(working), 0, 'f', 2);
	} else if (spec.seamless) {
		out.steps << QCoreApplication::translate("VibeStudioTextureGeneration", "The edges already tile (seam %1).").arg(out.seamScoreBefore, 0, 'f', 2);
	}
	// 3. Size.
	QImage layer = resampleTexture(working, layerSize, spec.seamless);
	out.seamScoreAfter = textureSeamScore(layer);
	out.steps << QCoreApplication::translate("VibeStudioTextureGeneration", "Resampled to %1x%2.").arg(layerSize.width()).arg(layerSize.height());
	if (quakeSky) {
		// Quake draws the right half as the far layer and the left half as
		// clouds over it, black showing through.
		QImage sky(outputSize, QImage::Format_ARGB32);
		sky.fill(Qt::black);
		for (int y = 0; y < 128; ++y) {
			const auto* in = reinterpret_cast<const QRgb*>(layer.constScanLine(y));
			auto* row = reinterpret_cast<QRgb*>(sky.scanLine(y));
			for (int x = 0; x < 128; ++x) {
				const double light = luminance(in[x]);
				row[x] = light > 0.55 ? (in[x] | 0xff000000u) : qRgb(0, 0, 0);
				const QRgb far = in[x];
				row[x + 128] = qRgb(qRed(far) * 3 / 5, qGreen(far) * 3 / 5, qBlue(far) * 3 / 5);
			}
		}
		layer = sky;
		out.steps << QCoreApplication::translate("VibeStudioTextureGeneration", "Made the cloud layer (left) and the darker far layer (right) of a Quake sky.");
	}
	out.image = layer;

	// 4. The game's format and palette.
	TextureExportOptions options;
	options.name = out.name;
	options.dither = spec.dither;
	options.alpha = TextureExportAlpha::Matte;
	options.matte = Qt::black;
	options.allowGeneratedPalette = palette.palette.generated;
	const QString liquid = spec.surface == QStringLiteral("liquid") ? liquidKind(spec) : QString();
	if (profile.id == QStringLiteral("quake")) {
		options.format = TextureExportFormat::QuakeMiptex;
		options.fullbright = spec.fullbrights ? TextureFullbrightMode::Allow : TextureFullbrightMode::Exclude;
	} else if (profile.id == QStringLiteral("quake2")) {
		options.format = TextureExportFormat::Quake2Wal;
		options.name = out.mapName;
		if (!liquid.isEmpty()) {
			// Quake II's qfiles.h: CONTENTS_LAVA 8, CONTENTS_SLIME 16,
			// CONTENTS_WATER 32; SURF_WARP 8 makes the surface ripple.
			options.contentFlags = liquid == QStringLiteral("lava") ? 8u : liquid == QStringLiteral("slime") ? 16u : 32u;
			options.surfaceFlags = 8u;
		}
	} else if (profile.id == QStringLiteral("quake3")) {
		options.format = TextureExportFormat::Targa;
	} else if (doomFamily) {
		const bool flat = outputSize == QSize(64, 64) && spec.surface != QStringLiteral("wall") && spec.surface != QStringLiteral("trim") && spec.surface != QStringLiteral("panel");
		options.format = flat ? TextureExportFormat::DoomFlat : TextureExportFormat::DoomPatch;
	} else {
		options.format = TextureExportFormat::Png;
	}
	out.exportOptions = options;
	out.encoded = encodeTextureExport(out.image, options, palette);
	if (!out.encoded.succeeded) {
		out.error = out.encoded.error;
		return out;
	}
	out.warnings << out.encoded.warnings;
	// The encoder's first warning for a stand-in palette is its short form of
	// the one added below, which also says where the real palette comes from.
	if (palette.palette.generated && !out.encoded.warnings.isEmpty()) {
		out.warnings.removeAt(out.warnings.size() - out.encoded.warnings.size());
	}
	out.preview = out.encoded.preview.format() == QImage::Format_Indexed8 ? out.encoded.preview.convertToFormat(QImage::Format_ARGB32) : out.encoded.preview;
	if (!profile.paletteId.isEmpty()) {
		out.steps << QCoreApplication::translate("VibeStudioTextureGeneration", "Converted to the %1 palette%2 (%3 pixels changed colour).")
						 .arg(palette.palette.displayName.isEmpty() ? profile.displayName : palette.palette.displayName,
							 spec.dither ? QCoreApplication::translate("VibeStudioTextureGeneration", " with dithering") : QString())
						 .arg(out.encoded.colorChangedPixels);
		if (palette.palette.generated) {
			out.warnings << QCoreApplication::translate("VibeStudioTextureGeneration",
				"No %1 palette was found, so a generated stand-in was used: colours will be wrong in the game. Point VibeStudio at the game's palette (a project or installation with %2).")
								.arg(profile.displayName, idTechPaletteCandidatePaths(profile.paletteId).join(QStringLiteral(", ")));
		}
	}

	// 5. Companions for source ports.
	if (spec.companions && !profile.normalSuffix.isEmpty()) {
		out.companions.push_back({QStringLiteral("normal"), profile.normalSuffix, deriveTextureNormalMap(out.image, spec.normalStrength, spec.seamless)});
		out.companions.push_back({QStringLiteral("gloss"), profile.glossSuffix, deriveTextureGlossMap(out.image)});
		const bool quake = profile.id == QStringLiteral("quake");
		const QImage glow = deriveTextureGlowMap(quake ? out.preview : out.image, spec.glowThreshold, quake ? out.encoded.preview : QImage(), quake ? 224 : -1);
		if (!glow.isNull()) {
			out.companions.push_back({QStringLiteral("glow"), profile.glowSuffix, glow});
		}
		out.steps << QCoreApplication::translate("VibeStudioTextureGeneration", "Derived %n companion map(s) for source ports.", nullptr, int(out.companions.size()));
	} else if (spec.companions) {
		out.warnings << QCoreApplication::translate("VibeStudioTextureGeneration", "%1 ports have no common file names for companion maps, so none were made.").arg(profile.displayName);
	}
	if (out.seamScoreAfter > 2.0 && spec.seamless) {
		out.warnings << QCoreApplication::translate("VibeStudioTextureGeneration", "The seam is still visible (score %1); try a wider blend or another picture.").arg(out.seamScoreAfter, 0, 'f', 2);
	}
	out.ok = true;
	return out;
}

TextureGenerationWriteReport writeGeneratedTexture(const GeneratedTexture& texture, const TextureGenerationSpec& spec, const TextureGenerationOutput& output)
{
	TextureGenerationWriteReport report;
	const auto fail = [&report](const QString& message) {
		report.ok = false;
		report.error = message;
		return report;
	};
	if (!texture.ok) {
		return fail(texture.error.isEmpty() ? QCoreApplication::translate("VibeStudioTextureGeneration", "There is no finished texture to write.") : texture.error);
	}
	TextureGameProfile profile;
	textureGameProfileForId(spec.game, &profile);
	const QString folder = QDir::cleanPath(output.folder.trimmed().isEmpty() ? QDir::currentPath() : output.folder.trimmed());
	report.mapTextureName = texture.mapName;
	const bool doomFamily = profile.id == QStringLiteral("doom") || profile.id == QStringLiteral("heretic") || profile.id == QStringLiteral("hexen");
	const QString companionFolder = profile.id == QStringLiteral("quake")
		? QDir(folder).filePath(QStringLiteral("textures"))
		: QDir(folder).filePath(QStringLiteral("textures/%1").arg(profileDirectory(spec)));
	QString error;
	const auto companionPath = [&](const GeneratedTextureMap& companion) {
		// Ports read a liquid's `*` as `#`, which every file system accepts.
		QString fileName = QFileInfo(texture.mapName).fileName();
		fileName.replace(QLatin1Char('*'), QLatin1Char('#'));
		const QString base = profile.id == QStringLiteral("generic") ? QDir(folder).filePath(texture.name) : QDir(companionFolder).filePath(fileName);
		return QStringLiteral("%1%2.tga").arg(base, companion.suffix);
	};
	// Refuse before writing anything, so a refusal leaves no half-written set.
	if (!output.replaceExisting) {
		for (const GeneratedTextureMap& companion : texture.companions) {
			if (QFileInfo::exists(companionPath(companion))) {
				report.alreadyExists = true;
				return fail(QCoreApplication::translate("VibeStudioTextureGeneration", "%1 already exists.").arg(QDir::toNativeSeparators(companionPath(companion))));
			}
		}
	}

	// The texture itself.
	if (profile.id == QStringLiteral("quake") || doomFamily) {
		const QString wadPath = output.wadPath.trimmed().isEmpty() ? QDir(folder).filePath(QStringLiteral("wads/vibestudio_generated.wad")) : output.wadPath.trimmed();
		PackageStagingModel staging;
		const bool exists = QFileInfo::exists(wadPath);
		if (exists) {
			PackageArchive archive;
			if (!archive.load(wadPath, &error)) {
				return fail(QCoreApplication::translate("VibeStudioTextureGeneration", "Could not open %1: %2").arg(QDir::toNativeSeparators(wadPath), error));
			}
			if (!staging.loadBaseArchive(archive, &error)) {
				return fail(error);
			}
			if (!output.replaceExisting) {
				for (const PackageEntry& entry : archive.entries()) {
					if (entry.virtualPath.compare(texture.exportOptions.name, Qt::CaseInsensitive) == 0) {
						report.alreadyExists = true;
						return fail(QCoreApplication::translate("VibeStudioTextureGeneration", "%1 already holds a texture called %2.")
										.arg(QDir::toNativeSeparators(wadPath), texture.exportOptions.name));
					}
				}
			}
		} else if (!staging.createEmpty(PackageArchiveFormat::Wad, profile.id == QStringLiteral("quake") ? QStringLiteral("WAD2") : QStringLiteral("PWAD"), &error)) {
			return fail(error);
		}
		if (!stageTextureExport(texture.encoded, texture.exportOptions, texture.exportOptions.name, &staging, output.replaceExisting, &error)) {
			return fail(QCoreApplication::translate("VibeStudioTextureGeneration", "Could not add %1 to %2: %3 Allow replacing, or pick another name.")
							.arg(texture.exportOptions.name, QDir::toNativeSeparators(wadPath), error));
		}
		if (!output.dryRun) {
			QDir().mkpath(QFileInfo(wadPath).absolutePath());
		}
		PackageWriteRequest request;
		request.format = PackageArchiveFormat::Wad;
		request.destinationPath = wadPath;
		request.allowOverwrite = exists;
		request.allowInPlaceOverwrite = exists;
		request.dryRun = output.dryRun;
		const PackageWriteReport written = staging.writeArchive(request);
		if (!written.succeeded()) {
			return fail(QCoreApplication::translate("VibeStudioTextureGeneration", "Could not write %1: %2")
							.arg(QDir::toNativeSeparators(wadPath), (written.blockedMessages + written.warnings).join(QStringLiteral(" "))));
		}
		report.writtenPaths << wadPath;
		if (exists && !written.backupPath.isEmpty() && !output.dryRun) {
			report.notes << QCoreApplication::translate("VibeStudioTextureGeneration", "The WAD's previous contents were kept in %1.").arg(QDir::toNativeSeparators(written.backupPath));
		}
		if (profile.id == QStringLiteral("quake")) {
			report.notes << QCoreApplication::translate("VibeStudioTextureGeneration", "Add %1 to worldspawn's \"wad\" key so the compiler finds %2.").arg(QFileInfo(wadPath).fileName(), texture.name);
		} else if (texture.exportOptions.format == TextureExportFormat::DoomPatch) {
			report.notes << QCoreApplication::translate("VibeStudioTextureGeneration",
				"%1 is a patch: list it in TEXTURE1 and PNAMES, or in a ZDoom TEXTURES lump, before maps can use it on walls.").arg(texture.name);
		}
	} else {
		const QString suffix = profile.id == QStringLiteral("quake2") ? QStringLiteral("wal") : profile.id == QStringLiteral("quake3") ? QStringLiteral("tga") : QStringLiteral("png");
		const QString path = profile.id == QStringLiteral("generic") ? QDir(folder).filePath(QStringLiteral("%1.%2").arg(texture.name, suffix))
																	: QDir(folder).filePath(QStringLiteral("textures/%1.%2").arg(texture.mapName, suffix));
		if (QFileInfo::exists(path) && !output.replaceExisting) {
			report.alreadyExists = true;
			return fail(QCoreApplication::translate("VibeStudioTextureGeneration", "%1 already exists.").arg(QDir::toNativeSeparators(path)));
		}
		if (!output.dryRun && !writeBytes(path, texture.encoded.bytes, true, &error)) {
			return fail(error);
		}
		report.writtenPaths << path;
	}

	// Companions, as TGA beside where the game's ports look.
	bool glow = false;
	for (const GeneratedTextureMap& companion : texture.companions) {
		const QString path = companionPath(companion);
		const QByteArray bytes = targaBytes(companion.image, &error);
		if (bytes.isEmpty()) {
			return fail(error);
		}
		if (!output.dryRun && !writeBytes(path, bytes, true, &error)) {
			return fail(error);
		}
		report.writtenPaths << path;
		glow = glow || companion.kind == QStringLiteral("glow");
	}
	if (!texture.companions.isEmpty() && profile.id == QStringLiteral("quake")) {
		report.notes << QCoreApplication::translate("VibeStudioTextureGeneration",
			"Source ports read the companions from textures/: DarkPlaces and FTE all three, QuakeSpasm-family ports the _glow map as the fullbright layer.");
	}

	// A Quake III shader where the surface needs one.
	const QString liquid = spec.surface == QStringLiteral("liquid") ? liquidKind(spec) : QString();
	if (profile.id == QStringLiteral("quake3") && (glow || !liquid.isEmpty())) {
		report.shaderText = quake3Shader(texture.mapName, liquid, glow, profile.glowSuffix);
		const QString shaderPath = QDir(folder).filePath(QStringLiteral("scripts/vibestudio_generated.shader"));
		QString existing;
		QFile shaderFile(shaderPath);
		if (shaderFile.exists() && shaderFile.open(QIODevice::ReadOnly)) {
			existing = QString::fromUtf8(shaderFile.readAll());
			shaderFile.close();
		}
		const QString header = QStringLiteral("textures/%1").arg(texture.mapName);
		static const QRegularExpression lineBreak(QStringLiteral("\\r?\\n"));
		if (existing.split(lineBreak).contains(header)) {
			report.notes << QCoreApplication::translate("VibeStudioTextureGeneration", "%1 already defines %2; its shader was left as it was.").arg(QStringLiteral("scripts/vibestudio_generated.shader"), header);
		} else {
			const QString combined = existing + (existing.isEmpty() || existing.endsWith(QLatin1Char('\n')) ? QString() : QStringLiteral("\n")) + report.shaderText;
			if (!output.dryRun && !writeBytes(shaderPath, combined.toUtf8(), true, &error)) {
				return fail(error);
			}
			report.writtenPaths << shaderPath;
			report.notes << QCoreApplication::translate("VibeStudioTextureGeneration", "List vibestudio_generated in scripts/shaderlist.txt if your compiler or editor reads that list.");
		}
	}

	// What made it, for review and to make it again.
	QJsonObject provenance = output.provenance;
	provenance.insert(QStringLiteral("schema"), QStringLiteral("vibestudio.generated-texture/1"));
	provenance.insert(QStringLiteral("createdUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
	provenance.insert(QStringLiteral("spec"), textureGenerationSpecJson(spec));
	provenance.insert(QStringLiteral("texture"), generatedTextureJson(texture));
	provenance.insert(QStringLiteral("outputs"), QJsonArray::fromStringList(report.writtenPaths));
	provenance.insert(QStringLiteral("sha256"), QString::fromLatin1(QCryptographicHash::hash(texture.encoded.bytes, QCryptographicHash::Sha256).toHex()));
	QString recordName = texture.name;
	recordName.remove(QLatin1Char('*'));
	const QString recordPath = QDir(folder).filePath(QStringLiteral(".vibestudio/generated/textures/%1.json").arg(recordName));
	if (!output.dryRun && !writeBytes(recordPath, QJsonDocument(provenance).toJson(QJsonDocument::Indented), true, &error)) {
		return fail(error);
	}
	report.writtenPaths << recordPath;
	report.ok = true;
	return report;
}

QJsonObject textureGenerationSpecJson(const TextureGenerationSpec& spec)
{
	QJsonObject object {
		{QStringLiteral("prompt"), spec.prompt},
		{QStringLiteral("game"), spec.game},
		{QStringLiteral("surface"), spec.surface},
		{QStringLiteral("style"), spec.style},
		{QStringLiteral("seamless"), spec.seamless},
		{QStringLiteral("seamBlendPercent"), spec.seamBlendPercent},
		{QStringLiteral("dither"), spec.dither},
		{QStringLiteral("fullbrights"), spec.fullbrights},
		{QStringLiteral("name"), spec.name},
		{QStringLiteral("directory"), spec.directory},
		{QStringLiteral("companions"), spec.companions},
		{QStringLiteral("normalStrength"), spec.normalStrength},
		{QStringLiteral("glowThreshold"), spec.glowThreshold},
	};
	if (spec.size.isValid() && !spec.size.isEmpty()) {
		object.insert(QStringLiteral("width"), spec.size.width());
		object.insert(QStringLiteral("height"), spec.size.height());
	}
	return object;
}

bool textureGenerationSpecFromJson(const QJsonObject& object, TextureGenerationSpec* spec, QString* error)
{
	if (!spec) {
		return false;
	}
	TextureGenerationSpec parsed;
	parsed.prompt = object.value(QStringLiteral("prompt")).toString();
	parsed.game = object.value(QStringLiteral("game")).toString(parsed.game);
	parsed.surface = object.value(QStringLiteral("surface")).toString(parsed.surface);
	parsed.style = object.value(QStringLiteral("style")).toString();
	parsed.seamless = object.value(QStringLiteral("seamless")).toBool(parsed.seamless);
	parsed.seamBlendPercent = object.value(QStringLiteral("seamBlendPercent")).toInt(parsed.seamBlendPercent);
	parsed.dither = object.value(QStringLiteral("dither")).toBool(parsed.dither);
	parsed.fullbrights = object.value(QStringLiteral("fullbrights")).toBool(parsed.fullbrights);
	parsed.name = object.value(QStringLiteral("name")).toString();
	parsed.directory = object.value(QStringLiteral("directory")).toString();
	parsed.companions = object.value(QStringLiteral("companions")).toBool(parsed.companions);
	parsed.normalStrength = object.value(QStringLiteral("normalStrength")).toDouble(parsed.normalStrength);
	parsed.glowThreshold = object.value(QStringLiteral("glowThreshold")).toDouble(parsed.glowThreshold);
	if (object.contains(QStringLiteral("width")) || object.contains(QStringLiteral("height"))) {
		parsed.size = QSize(object.value(QStringLiteral("width")).toInt(), object.value(QStringLiteral("height")).toInt());
	}
	if (!textureGameProfileForId(parsed.game)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioTextureGeneration", "Unknown texture game \"%1\".").arg(parsed.game);
		}
		return false;
	}
	if (!textureGenerationSurfaceIds().contains(parsed.surface)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioTextureGeneration", "Unknown surface \"%1\".").arg(parsed.surface);
		}
		return false;
	}
	*spec = parsed;
	return true;
}

QJsonObject generatedTextureJson(const GeneratedTexture& texture)
{
	QJsonArray companions;
	for (const GeneratedTextureMap& companion : texture.companions) {
		companions.append(QJsonObject {
			{QStringLiteral("kind"), companion.kind},
			{QStringLiteral("suffix"), companion.suffix},
			{QStringLiteral("width"), companion.image.width()},
			{QStringLiteral("height"), companion.image.height()},
		});
	}
	return QJsonObject {
		{QStringLiteral("ok"), texture.ok},
		{QStringLiteral("error"), texture.error},
		{QStringLiteral("name"), texture.name},
		{QStringLiteral("mapName"), texture.mapName},
		{QStringLiteral("width"), texture.image.width()},
		{QStringLiteral("height"), texture.image.height()},
		{QStringLiteral("format"), textureExportFormatId(texture.exportOptions.format)},
		{QStringLiteral("bytes"), double(texture.encoded.bytes.size())},
		{QStringLiteral("seamScoreBefore"), texture.seamScoreBefore},
		{QStringLiteral("seamScoreAfter"), texture.seamScoreAfter},
		{QStringLiteral("colorChangedPixels"), double(texture.encoded.colorChangedPixels)},
		{QStringLiteral("companions"), companions},
		{QStringLiteral("steps"), QJsonArray::fromStringList(texture.steps)},
		{QStringLiteral("warnings"), QJsonArray::fromStringList(texture.warnings)},
	};
}

QJsonObject textureGenerationWriteReportJson(const TextureGenerationWriteReport& report)
{
	return QJsonObject {
		{QStringLiteral("ok"), report.ok},
		{QStringLiteral("error"), report.error},
		{QStringLiteral("written"), QJsonArray::fromStringList(report.writtenPaths)},
		{QStringLiteral("mapTextureName"), report.mapTextureName},
		{QStringLiteral("shader"), report.shaderText},
		{QStringLiteral("notes"), QJsonArray::fromStringList(report.notes)},
	};
}

} // namespace vibestudio
