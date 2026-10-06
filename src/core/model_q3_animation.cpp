#include "core/model_q3_animation.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

// Native roster, frame offset, legacy gesture fallback and loop/reverse timing
// follow id Software's Quake III Arena code/game/bg_public.h and
// code/cgame/cg_players.c (CG_ParseAnimationFile / CG_RunLerpFrame), master
// reviewed 2026-10-06, GPL-2.0-or-later (compatible with this GPL-3.0 project).
// https://github.com/id-Software/Quake-III-Arena/blob/master/code/cgame/cg_players.c
// Original bounded implementation; no upstream parser or game assets copied.
namespace vibestudio
{
namespace
{
constexpr const char *names[]{"BOTH_DEATH1",   "BOTH_DEAD1",	"BOTH_DEATH2",	   "BOTH_DEAD2",   "BOTH_DEATH3",	 "BOTH_DEAD3",
							  "TORSO_GESTURE", "TORSO_ATTACK",	"TORSO_ATTACK2",   "TORSO_DROP",   "TORSO_RAISE",	 "TORSO_STAND",
							  "TORSO_STAND2",  "LEGS_WALKCR",	"LEGS_WALK",	   "LEGS_RUN",	   "LEGS_BACK",		 "LEGS_SWIM",
							  "LEGS_JUMP",	   "LEGS_LAND",		"LEGS_JUMPB",	   "LEGS_LANDB",   "LEGS_IDLE",		 "LEGS_IDLECR",
							  "LEGS_TURN",	   "TORSO_GETFLAG", "TORSO_GUARDBASE", "TORSO_PATROL", "TORSO_FOLLOWME", "TORSO_AFFIRMATIVE",
							  "TORSO_NEGATIVE"};
static_assert(std::size(names) == modelQ3AnimationCount);
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
bool tokens(const QByteArray &bytes, QStringList *result)
{
	// Token positions are bounded by the native file-size limit. Quoted tokens,
	// line comments and block comments match the native configuration grammar.
	for (qsizetype p = 0; p < bytes.size();)
	{
		const auto c = static_cast<unsigned char>(bytes[p]);
		if (!c)
			return false;
		if (c <= ' ')
		{
			++p;
			continue;
		}
		if (bytes.mid(p, 2) == "//")
		{
			while (p < bytes.size() && bytes[p] != '\n')
				++p;
			continue;
		}
		if (bytes.mid(p, 2) == "/*")
		{
			const auto end = bytes.indexOf("*/", p + 2);
			if (end < 0)
				return false;
			p = end + 2;
			continue;
		}
		if (c == '"')
		{
			const auto end = bytes.indexOf('"', p + 1);
			if (end < 0)
				return false;
			result->append(QString::fromLatin1(bytes.mid(p + 1, end - p - 1)));
			p = end + 1;
		}
		else
		{
			const auto start = p;
			while (p < bytes.size() && static_cast<unsigned char>(bytes[p]) > ' ')
				++p;
			result->append(QString::fromLatin1(bytes.mid(start, p - start)));
		}
		if (result->size() > 256)
			return false;
	}
	return true;
}
bool integer(const QString &value, int *result)
{
	static const QRegularExpression pattern(QStringLiteral("^[+-]?[0-9]+$"));
	bool valid = false;
	const int n = value.toInt(&valid);
	if (!valid || !pattern.match(value).hasMatch())
		return false;
	*result = n;
	return true;
}
} // namespace

QString modelQ3AnimationName(int slot)
{
	return slot >= 0 && slot < modelQ3AnimationCount ? QString::fromLatin1(names[slot]) : QString();
}
int modelQ3AnimationSlot(const QString &name)
{
	for (int i = 0; i < modelQ3AnimationCount; ++i)
		if (name == QLatin1String(names[i]))
			return i;
	return -1;
}
bool modelQ3AnimationUsesLower(int slot)
{
	return slot >= 0 && (slot < 6 || (slot >= 13 && slot <= 24));
}
bool modelQ3AnimationUsesUpper(int slot)
{
	return slot >= 0 && slot < modelQ3AnimationCount && !(slot >= 13 && slot <= 24);
}
int modelQ3AnimationFirstFrame(const ModelQ3AnimationConfig &config, int slot)
{
	if (slot < 0 || slot >= modelQ3AnimationCount)
		return -1;
	const qint64 offset = slot >= 13 && slot <= 24 ? qint64(config.clips[13].firstFrame) - config.clips[6].firstFrame : 0;
	const qint64 first = qint64(config.clips[slot].firstFrame) - offset;
	return first < std::numeric_limits<int>::min() || first > std::numeric_limits<int>::max() ? -1 : int(first);
}
int modelQ3AnimationPeriod(const ModelQ3AnimationClip &clip)
{
	// The original engine divides using float and truncates to an integer ms.
	return std::isfinite(clip.framesPerSecond) && clip.framesPerSecond >= .001f && clip.framesPerSecond <= 1000
			   ? int(1000.0f / clip.framesPerSecond)
			   : 0;
}
bool validateModelQ3Animation(const ModelQ3AnimationConfig &config, QString *error, int lowerFrames, int upperFrames)
{
	if (error)
		error->clear();
	if (!QSet<QString>{"normal", "boot", "flesh", "mech", "energy"}.contains(config.footsteps) ||
		!QSet<QString>{"m", "f", "n"}.contains(config.sex) || lowerFrames < -1 || upperFrames < -1)
		return fail(error, QCoreApplication::translate("ModelQ3Animation", "Choose supported footsteps, sex and model frame counts."));
	for (float v : {config.headOffset.x, config.headOffset.y, config.headOffset.z})
		if (!std::isfinite(v) || std::abs(v) > 1000000)
			return fail(error, QCoreApplication::translate("ModelQ3Animation", "Head offsets must be finite and within ±1,000,000."));
	for (int i = 0; i < modelQ3AnimationCount; ++i)
	{
		const auto &clip = config.clips[i];
		if (clip.firstFrame < 0 || clip.firstFrame > 1000000 || clip.frameCount < 1 || clip.frameCount > 1024 || clip.loopFrames < 0 ||
			clip.loopFrames > clip.frameCount || modelQ3AnimationPeriod(clip) < 1)
			return fail(error,
						QCoreApplication::translate("ModelQ3Animation", "%1: use a native first frame from 0 to 1,000,000, 1–1,024 frames, "
																		"a loop tail from 0 to the frame count, and 0.001–1,000 FPS.")
							.arg(modelQ3AnimationName(i)));
	}
	for (int i = 0; i < modelQ3AnimationCount; ++i)
	{
		const int first = modelQ3AnimationFirstFrame(config, i), end = first + config.clips[i].frameCount;
		if (first < 0 || end > 1024 || (modelQ3AnimationUsesLower(i) && lowerFrames >= 0 && end > lowerFrames) ||
			(modelQ3AnimationUsesUpper(i) && upperFrames >= 0 && end > upperFrames))
			return fail(error,
						QCoreApplication::translate("ModelQ3Animation", "%1: adjusted model frames %2–%3 are outside the available poses.")
							.arg(modelQ3AnimationName(i))
							.arg(first)
							.arg(end - 1));
	}
	return true;
}
bool parseModelQ3Animation(const QByteArray &bytes, ModelQ3AnimationConfig *config, QString *error, QStringList *notes)
{
	if (error)
		error->clear();
	const auto malformed = [&] {
		return fail(error,
					QCoreApplication::translate("ModelQ3Animation", "Malformed animation.cfg: use supported header directives followed by "
																	"25–31 complete four-number rows, within 19,998 bytes."));
	};
	QStringList words, warnings;
	if (!config || bytes.isEmpty() || bytes.size() > modelQ3AnimationMaxBytes || bytes.contains('\0') || !tokens(bytes, &words))
		return malformed();
	ModelQ3AnimationConfig result;
	qsizetype p = 0;
	QSet<QString> seen;
	int first = 0;
	while (p < words.size() && !integer(words[p], &first))
	{
		const auto key = words[p++].toLower();
		if (seen.contains(key))
			return malformed();
		seen.insert(key);
		if (key == "fixedlegs")
			result.fixedLegs = true;
		else if (key == "fixedtorso")
			result.fixedTorso = true;
		else if ((key == "footsteps" || key == "sex") && p < words.size())
		{
			const auto value = words[p++].toLower();
			if (key == "sex")
				result.sex = value;
			else
				result.footsteps = value == "default" ? QStringLiteral("normal") : value;
		}
		else if (key == "headoffset" && p + 3 <= words.size())
		{
			for (float *field : {&result.headOffset.x, &result.headOffset.y, &result.headOffset.z})
			{
				bool valid = false;
				*field = words[p++].toFloat(&valid);
				if (!valid)
					return malformed();
			}
		}
		else
			return malformed();
	}
	// CG_ParseAnimationFile recognizes the first row only when its first token
	// starts with an ASCII digit. Do not silently repair a leading plus/minus.
	if (p == words.size() || words[p].isEmpty() || words[p][0] < QLatin1Char('0') || words[p][0] > QLatin1Char('9'))
		return malformed();
	const auto count = (words.size() - p) / 4;
	if ((words.size() - p) % 4 || count < 25 || count > modelQ3AnimationCount)
		return malformed();
	for (int i = 0; i < count; ++i)
	{
		auto &clip = result.clips[i];
		int frames = 0;
		bool valid = false;
		if (!integer(words[p++], &clip.firstFrame) || !integer(words[p++], &frames) || frames < -1024 || frames > 1024 ||
			!integer(words[p++], &clip.loopFrames))
			return malformed();
		clip.frameCount = std::abs(frames);
		clip.reversed = frames < 0;
		clip.framesPerSecond = words[p++].toFloat(&valid);
		if (!valid)
			return malformed();
		if (clip.framesPerSecond == 0)
		{
			clip.framesPerSecond = 1;
			warnings << QCoreApplication::translate("ModelQ3Animation", "%1: native zero FPS was normalized to 1 FPS.")
							.arg(modelQ3AnimationName(i));
		}
	}
	for (int i = int(count); i < modelQ3AnimationCount; ++i)
	{
		result.clips[i] = result.clips[6];
		result.clips[i].reversed = false;
	}
	if (count < modelQ3AnimationCount)
		warnings << QCoreApplication::translate("ModelQ3Animation",
												"Missing legacy torso gestures were filled from TORSO_GESTURE with forward playback.");
	if (!validateModelQ3Animation(result, error))
		return false;
	*config = std::move(result);
	if (notes)
		*notes = std::move(warnings);
	return true;
}
QByteArray exportModelQ3Animation(const ModelQ3AnimationConfig &config, QString *error)
{
	if (!validateModelQ3Animation(config, error))
		return {};
	QByteArray bytes = "// VibeStudio Quake III player animation configuration\nfootsteps " + config.footsteps.toLatin1() + "\nsex " +
					   config.sex.toLatin1() + "\nheadoffset " + QByteArray::number(config.headOffset.x, 'g', 9) + " " +
					   QByteArray::number(config.headOffset.y, 'g', 9) + " " + QByteArray::number(config.headOffset.z, 'g', 9) + "\n";
	if (config.fixedLegs)
		bytes += "fixedlegs\n";
	if (config.fixedTorso)
		bytes += "fixedtorso\n";
	bytes += "\n// first  count  loop-tail  fps\n";
	for (int i = 0; i < modelQ3AnimationCount; ++i)
	{
		const auto &clip = config.clips[i];
		bytes += QByteArray::number(clip.firstFrame) + " " + QByteArray::number(clip.reversed ? -clip.frameCount : clip.frameCount) + " " +
				 QByteArray::number(clip.loopFrames) + " " + QByteArray::number(clip.framesPerSecond, 'g', 9) + " // " + names[i] + "\n";
	}
	return bytes;
}
QJsonObject modelQ3AnimationJson(const ModelQ3AnimationConfig &config)
{
	QJsonArray clips;
	for (int i = 0; i < modelQ3AnimationCount; ++i)
	{
		const auto &clip = config.clips[i];
		clips.append(QJsonObject{{"name", modelQ3AnimationName(i)},
								 {"nativeFirstFrame", clip.firstFrame},
								 {"modelFirstFrame", modelQ3AnimationFirstFrame(config, i)},
								 {"frameCount", clip.frameCount},
								 {"loopFrames", clip.loopFrames},
								 {"framesPerSecond", clip.framesPerSecond},
								 {"periodMilliseconds", modelQ3AnimationPeriod(clip)},
								 {"reversed", clip.reversed},
								 {"lower", modelQ3AnimationUsesLower(i)},
								 {"upper", modelQ3AnimationUsesUpper(i)}});
	}
	return {{"clips", clips},
			{"footsteps", config.footsteps},
			{"sex", config.sex},
			{"headOffset", QJsonArray{config.headOffset.x, config.headOffset.y, config.headOffset.z}},
			{"fixedLegs", config.fixedLegs},
			{"fixedTorso", config.fixedTorso}};
}
bool sampleModelQ3Animation(const ModelQ3AnimationConfig &config, int slot, double seconds, bool interpolate, ModelAnimationSample *sample)
{
	if (!sample || slot < 0 || slot >= modelQ3AnimationCount || !std::isfinite(seconds) || seconds < 0 || seconds > 1000000 ||
		!validateModelQ3Animation(config))
		return false;
	const auto &clip = config.clips[slot];
	const int first = modelQ3AnimationFirstFrame(config, slot);
	double position = seconds * 1000.0 / modelQ3AnimationPeriod(clip);
	// Decimal seconds at an exact engine millisecond boundary can arrive a few
	// double ULPs below an integer step (for example 2.046 / .066). Remove only
	// arithmetic roundoff; times meaningfully before the boundary still blend.
	const double boundary = std::round(position);
	if (std::abs(position - boundary) <= 4 * std::numeric_limits<double>::epsilon() * std::max(1.0, std::abs(position)))
		position = boundary;
	const auto tick = qint64(std::floor(position));
	const auto frame = [&](qint64 index) {
		if (index >= clip.frameCount)
			index = clip.loopFrames ? clip.frameCount - clip.loopFrames + (index - clip.frameCount) % clip.loopFrames : clip.frameCount - 1;
		return first + (clip.reversed ? clip.frameCount - 1 - int(index) : int(index));
	};
	ModelAnimationSample result{frame(tick), frame(interpolate ? tick + 1 : tick), interpolate ? position - double(tick) : 0};
	if (result.frame == result.nextFrame)
		result.fraction = 0;
	*sample = result;
	return true;
}
} // namespace vibestudio
