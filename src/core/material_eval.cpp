#include "core/material_eval.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio {
namespace {

struct Text {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioMaterials)
};

constexpr double kPi = 3.14159265358979323846;

struct Quake3Tables {
	std::array<double, kQuake3FunctionTableSize> sine {};
	std::array<double, kQuake3FunctionTableSize> square {};
	std::array<double, kQuake3FunctionTableSize> triangle {};
	std::array<double, kQuake3FunctionTableSize> sawtooth {};
	std::array<double, kQuake3FunctionTableSize> inverseSawtooth {};

	Quake3Tables()
	{
		// tr_init.c R_Init.
		constexpr int size = kQuake3FunctionTableSize;
		for (int i = 0; i < size; ++i) {
			sine[i] = std::sin(i * 360.0 / static_cast<double>(size - 1) * kPi / 180.0);
			square[i] = i < size / 2 ? 1.0 : -1.0;
			sawtooth[i] = static_cast<double>(i) / size;
			inverseSawtooth[i] = 1.0 - sawtooth[i];
			if (i < size / 2) {
				if (i < size / 4) {
					triangle[i] = static_cast<double>(i) / (size / 4);
				} else {
					triangle[i] = 1.0 - triangle[i - size / 4];
				}
			} else {
				triangle[i] = -triangle[i - size / 2];
			}
		}
	}
};

const Quake3Tables& quake3Tables()
{
	static const Quake3Tables tables;
	return tables;
}

// R_NoiseInit: srand(1001) and the Microsoft C runtime's rand(), which id's
// Windows build used: holdrand = holdrand * 214013 + 2531011, value
// (holdrand >> 16) & 0x7fff, RAND_MAX 32767.
struct Quake3Noise {
	std::array<double, 256> table {};
	std::array<int, 256> permutation {};

	Quake3Noise()
	{
		quint32 hold = 1001;
		const auto rand = [&hold]() {
			hold = hold * 214013u + 2531011u;
			return static_cast<int>((hold >> 16) & 0x7fff);
		};
		for (int i = 0; i < 256; ++i) {
			table[i] = (rand() / 32767.0f) * 2.0 - 1.0;
			permutation[i] = static_cast<unsigned char>(rand() / 32767.0f * 255);
		}
	}

	[[nodiscard]] int value(int a) const { return permutation[a & 255]; }
	[[nodiscard]] double at(int x, int y, int z, int t) const { return table[value(x + value(y + value(z + value(t))))]; }
};

const Quake3Noise& quake3NoiseTables()
{
	static const Quake3Noise noise;
	return noise;
}

double lerp(double a, double b, double w)
{
	return a * (1.0 - w) + b * w;
}

int wrappedIndex(double value)
{
	// Truncate like ioquake3's (int) cast, then wrap with the table mask.
	if (!std::isfinite(value)) {
		return 0;
	}
	if (std::abs(value) > 1.0e9) {
		value = std::fmod(value, static_cast<double>(kQuake3FunctionTableSize));
	}
	return static_cast<int>(value) & (kQuake3FunctionTableSize - 1);
}

double sanitize(double value)
{
	return std::isfinite(value) ? value : 0.0;
}

// A deterministic 0..1 value for frame `occurrence` of a cycle.
double unitHash(quint32 seed, quint32 occurrence)
{
	quint32 x = seed * 0x9E3779B1u ^ (occurrence + 0x7F4A7C15u);
	x ^= x >> 16;
	x *= 0x85EBCA6Bu;
	x ^= x >> 13;
	x *= 0xC2B2AE35u;
	x ^= x >> 16;
	return (x & 0xFFFFFF) / static_cast<double>(0xFFFFFF);
}

} // namespace

double quake3FunctionTable(MaterialWaveFunction function, int index)
{
	const Quake3Tables& tables = quake3Tables();
	const int i = index & (kQuake3FunctionTableSize - 1);
	switch (function) {
	case MaterialWaveFunction::Sin:
		return tables.sine[i];
	case MaterialWaveFunction::Triangle:
		return tables.triangle[i];
	case MaterialWaveFunction::Square:
		return tables.square[i];
	case MaterialWaveFunction::Sawtooth:
		return tables.sawtooth[i];
	case MaterialWaveFunction::InverseSawtooth:
		return tables.inverseSawtooth[i];
	case MaterialWaveFunction::Noise:
		break;
	}
	return 0.0;
}

double quake3Noise(double x, double y, double z, double t)
{
	const Quake3Noise& noise = quake3NoiseTables();
	const int ix = static_cast<int>(std::floor(x));
	const int iy = static_cast<int>(std::floor(y));
	const int iz = static_cast<int>(std::floor(z));
	const int it = static_cast<int>(std::floor(t));
	const double fx = x - ix;
	const double fy = y - iy;
	const double fz = z - iz;
	const double ft = t - it;
	double value[2] = {0.0, 0.0};
	for (int i = 0; i < 2; ++i) {
		const double front0 = noise.at(ix, iy, iz, it + i);
		const double front1 = noise.at(ix + 1, iy, iz, it + i);
		const double front2 = noise.at(ix, iy + 1, iz, it + i);
		const double front3 = noise.at(ix + 1, iy + 1, iz, it + i);
		const double back0 = noise.at(ix, iy, iz + 1, it + i);
		const double back1 = noise.at(ix + 1, iy, iz + 1, it + i);
		const double back2 = noise.at(ix, iy + 1, iz + 1, it + i);
		const double back3 = noise.at(ix + 1, iy + 1, iz + 1, it + i);
		const double front = lerp(lerp(front0, front1, fx), lerp(front2, front3, fx), fy);
		const double back = lerp(lerp(back0, back1, fx), lerp(back2, back3, fx), fy);
		value[i] = lerp(front, back, fz);
	}
	return lerp(value[0], value[1], ft);
}

double evaluateQuake3Wave(const MaterialWave& wave, double time)
{
	if (wave.function == MaterialWaveFunction::Noise) {
		// The phase is added before the frequency multiplies, unlike the
		// table waves.
		return wave.base + quake3Noise(0.0, 0.0, 0.0, (time + wave.phase) * wave.frequency) * wave.amplitude;
	}
	const int index = wrappedIndex((wave.phase + time * wave.frequency) * kQuake3FunctionTableSize);
	return wave.base + quake3FunctionTable(wave.function, index) * wave.amplitude;
}

double evaluateQuake3WaveClamped(const MaterialWave& wave, double time)
{
	return std::clamp(evaluateQuake3Wave(wave, time), 0.0, 1.0);
}

void MaterialTableSet::add(const MaterialTable& table)
{
	const QString key = table.name.toLower();
	// The first definition of a decl wins, as in idDeclManager; a real table
	// replaces a generated stand-in.
	const auto existing = m_tables.constFind(key);
	if (existing != m_tables.constEnd() && !existing->generated) {
		return;
	}
	m_tables.insert(key, table);
}

void MaterialTableSet::addAll(const QVector<MaterialTable>& tables)
{
	for (const MaterialTable& table : tables) {
		add(table);
	}
}

const MaterialTable* MaterialTableSet::find(const QString& name) const
{
	const auto found = m_tables.constFind(name.toLower());
	return found == m_tables.constEnd() ? nullptr : &found.value();
}

QStringList MaterialTableSet::names() const
{
	QStringList list;
	for (const MaterialTable& table : m_tables) {
		list << table.name;
	}
	list.sort(Qt::CaseInsensitive);
	return list;
}

void MaterialTableSet::addStandIns()
{
	const auto standIn = [this](const QString& name, const QVector<double>& values, bool snap) {
		if (find(name)) {
			return;
		}
		MaterialTable table;
		table.name = name;
		table.values = values;
		table.snap = snap;
		table.generated = true;
		m_tables.insert(name.toLower(), table);
	};
	QVector<double> sine;
	QVector<double> cosine;
	QVector<double> saw;
	QVector<double> inverse;
	constexpr int samples = 256;
	for (int i = 0; i < samples; ++i) {
		sine << std::sin(2.0 * kPi * i / samples);
		cosine << std::cos(2.0 * kPi * i / samples);
		saw << static_cast<double>(i) / samples;
		inverse << 1.0 - static_cast<double>(i) / samples;
	}
	standIn(QStringLiteral("sinTable"), sine, false);
	standIn(QStringLiteral("cosTable"), cosine, false);
	standIn(QStringLiteral("squareTable"), {1.0, -1.0}, true);
	standIn(QStringLiteral("triangleTable"), {0.0, 1.0, 0.0, -1.0}, false);
	standIn(QStringLiteral("sawtoothTable"), saw, true);
	standIn(QStringLiteral("inverseSawtoothTable"), inverse, true);
}

double lookupMaterialTable(const MaterialTable& table, double index)
{
	// idDeclTable::Parse appends values[0], so the domain is the original
	// count; with one value or none every lookup returns 1.
	const int domain = static_cast<int>(table.values.size());
	if (domain <= 1) {
		return 1.0;
	}
	if (!std::isfinite(index)) {
		index = 0.0;
	}
	const auto value = [&](int at) { return table.values.at(at % domain); };
	int whole = 0;
	double fraction = 0.0;
	if (table.clamp) {
		index *= domain - 1;
		if (index >= domain - 1) {
			return table.values.at(domain - 1);
		}
		if (index <= 0.0) {
			return table.values.at(0);
		}
		whole = static_cast<int>(index);
		fraction = index - whole;
	} else {
		index *= domain;
		if (index < 0.0) {
			index += domain * std::ceil(-index / domain);
		}
		const double floored = std::floor(index);
		fraction = index - floored;
		whole = static_cast<int>(std::fmod(floored, static_cast<double>(domain)));
	}
	if (!table.snap) {
		return value(whole) * (1.0 - fraction) + value(whole + 1) * fraction;
	}
	return value(whole);
}

namespace {

double evaluateNode(const QVector<MaterialExpressionNode>& nodes, int index, const MaterialTableSet& tables, const MaterialEvalContext& context,
	QVector<double>* cache, QVector<char>* done, QStringList* missingTables, int depth)
{
	if (index < 0 || index >= nodes.size() || depth > 512) {
		return 0.0;
	}
	if ((*done)[index]) {
		return (*cache)[index];
	}
	const MaterialExpressionNode& node = nodes.at(index);
	const auto child = [&](int at) { return evaluateNode(nodes, at, tables, context, cache, done, missingTables, depth + 1); };
	double result = 0.0;
	switch (node.op) {
	case MaterialExpressionOp::Constant:
		result = node.value;
		break;
	case MaterialExpressionOp::Time:
		result = context.time;
		break;
	case MaterialExpressionOp::Parm:
		result = node.index >= 0 && node.index < 12 ? context.parms[static_cast<size_t>(node.index)] : 0.0;
		break;
	case MaterialExpressionOp::Global:
		result = node.index >= 0 && node.index < 8 ? context.globals[static_cast<size_t>(node.index)] : 0.0;
		break;
	case MaterialExpressionOp::Sound:
		result = context.sound;
		break;
	case MaterialExpressionOp::FragmentPrograms:
		result = context.fragmentPrograms ? 1.0 : 0.0;
		break;
	case MaterialExpressionOp::Table: {
		const double argument = child(node.a);
		if (const MaterialTable* table = tables.find(node.name)) {
			result = lookupMaterialTable(*table, argument);
		} else if (missingTables && !missingTables->contains(node.name, Qt::CaseInsensitive)) {
			missingTables->push_back(node.name);
		}
		break;
	}
	case MaterialExpressionOp::Negate:
		result = -child(node.a);
		break;
	case MaterialExpressionOp::Add:
		result = child(node.a) + child(node.b);
		break;
	case MaterialExpressionOp::Subtract:
		result = child(node.a) - child(node.b);
		break;
	case MaterialExpressionOp::Multiply:
		result = child(node.a) * child(node.b);
		break;
	case MaterialExpressionOp::Divide:
		result = child(node.a) / child(node.b);
		break;
	case MaterialExpressionOp::Modulo: {
		// Both sides truncate to integers; a zero divisor counts as 1.
		const double left = child(node.a);
		const double right = child(node.b);
		const auto truncated = [](double v) {
			if (!std::isfinite(v)) {
				return 0LL;
			}
			return static_cast<long long>(std::clamp(v, -9.0e18, 9.0e18));
		};
		long long divisor = truncated(right);
		if (divisor == 0) {
			divisor = 1;
		}
		result = static_cast<double>(truncated(left) % divisor);
		break;
	}
	case MaterialExpressionOp::Greater:
		result = child(node.a) > child(node.b) ? 1.0 : 0.0;
		break;
	case MaterialExpressionOp::GreaterEqual:
		result = child(node.a) >= child(node.b) ? 1.0 : 0.0;
		break;
	case MaterialExpressionOp::Less:
		result = child(node.a) < child(node.b) ? 1.0 : 0.0;
		break;
	case MaterialExpressionOp::LessEqual:
		result = child(node.a) <= child(node.b) ? 1.0 : 0.0;
		break;
	case MaterialExpressionOp::Equal:
		result = child(node.a) == child(node.b) ? 1.0 : 0.0;
		break;
	case MaterialExpressionOp::NotEqual:
		result = child(node.a) != child(node.b) ? 1.0 : 0.0;
		break;
	case MaterialExpressionOp::And: {
		// Both sides are evaluated; there is no short circuit.
		const double left = child(node.a);
		const double right = child(node.b);
		result = (left != 0.0 && right != 0.0) ? 1.0 : 0.0;
		break;
	}
	case MaterialExpressionOp::Or: {
		const double left = child(node.a);
		const double right = child(node.b);
		result = (left != 0.0 || right != 0.0) ? 1.0 : 0.0;
		break;
	}
	}
	(*cache)[index] = result;
	(*done)[index] = 1;
	return result;
}

} // namespace

QVector<double> evaluateMaterialExpressions(const MaterialDefinition& definition, const MaterialTableSet& tables,
	const MaterialEvalContext& context, QStringList* missingTables)
{
	const int count = static_cast<int>(definition.expressions.size());
	QVector<double> cache(count, 0.0);
	QVector<char> done(count, 0);
	for (int index = 0; index < count; ++index) {
		evaluateNode(definition.expressions, index, tables, context, &cache, &done, missingTables, 0);
	}
	return cache;
}

double evaluateMaterialExpression(const QVector<MaterialExpressionNode>& nodes, int root, const MaterialTableSet& tables,
	const MaterialEvalContext& context)
{
	QVector<double> cache(nodes.size(), 0.0);
	QVector<char> done(nodes.size(), 0);
	return sanitize(evaluateNode(nodes, root, tables, context, &cache, &done, nullptr, 0));
}

double materialFramesCycleSeconds(const QVector<MaterialFrame>& frames)
{
	double total = 0.0;
	for (const MaterialFrame& frame : frames) {
		total += std::max(0.0, frame.duration);
	}
	return total;
}

int materialFrameIndexAt(const QVector<MaterialFrame>& frames, double time, quint32 seed)
{
	if (frames.isEmpty()) {
		return -1;
	}
	if (time < 0.0 || !std::isfinite(time)) {
		time = 0.0;
	}
	bool random = false;
	for (const MaterialFrame& frame : frames) {
		random |= frame.maximumDuration > frame.duration;
	}
	const double cycle = materialFramesCycleSeconds(frames);
	if (cycle <= 0.0) {
		return 0;
	}
	if (!random) {
		double position = std::fmod(time, cycle);
		for (int index = 0; index < frames.size(); ++index) {
			const double duration = std::max(0.0, frames.at(index).duration);
			if (position < duration) {
				return index;
			}
			position -= duration;
		}
		return static_cast<int>(frames.size()) - 1;
	}
	// Random durations: walk the timeline with one deterministic pick per
	// frame shown, bounded so long scrubs stay cheap.
	double elapsed = 0.0;
	quint32 occurrence = 0;
	for (int step = 0; step < 200000; ++step) {
		const int index = step % frames.size();
		const MaterialFrame& frame = frames.at(index);
		double duration = frame.duration;
		if (frame.maximumDuration > frame.duration) {
			duration += (frame.maximumDuration - frame.duration) * unitHash(seed, occurrence);
		}
		++occurrence;
		if (time < elapsed + duration) {
			return index;
		}
		elapsed += std::max(duration, 1.0e-4);
	}
	return static_cast<int>(std::fmod(time, cycle) / cycle * frames.size()) % frames.size();
}

QVector<QuakeLightStyle> quakeLightStyles()
{
	// Quake's world.qc (id Software, GPL-2.0-or-later): lightstyle(n, "...").
	return {
		{0, QStringLiteral("normal"), Text::tr("Normal"), QStringLiteral("m")},
		{1, QStringLiteral("flicker-1"), Text::tr("Flicker"), QStringLiteral("mmnmmommommnonmmonqnmmo")},
		{2, QStringLiteral("slow-strong-pulse"), Text::tr("Slow strong pulse"), QStringLiteral("abcdefghijklmnopqrstuvwxyzyxwvutsrqponmlkjihgfedcba")},
		{3, QStringLiteral("candle-1"), Text::tr("Candle"), QStringLiteral("mmmmmaaaaammmmmaaaaaabcdefgabcdefg")},
		{4, QStringLiteral("fast-strobe"), Text::tr("Fast strobe"), QStringLiteral("mamamamamama")},
		{5, QStringLiteral("gentle-pulse"), Text::tr("Gentle pulse"), QStringLiteral("jklmnopqrstuvwxyzyxwvutsrqponmlkj")},
		{6, QStringLiteral("flicker-2"), Text::tr("Flicker (second variety)"), QStringLiteral("nmonqnmomnmomomno")},
		{7, QStringLiteral("candle-2"), Text::tr("Candle (second variety)"), QStringLiteral("mmmaaaabcdefgmmmmaaaammmaamm")},
		{8, QStringLiteral("candle-3"), Text::tr("Candle (third variety)"), QStringLiteral("mmmaaammmaaammmabcdefaaaammmmabcdefmmmaaaa")},
		{9, QStringLiteral("slow-strobe"), Text::tr("Slow strobe"), QStringLiteral("aaaaaaaazzzzzzzz")},
		{10, QStringLiteral("fluorescent-flicker"), Text::tr("Fluorescent flicker"), QStringLiteral("mmamammmmammamamaaamammma")},
		{11, QStringLiteral("slow-pulse"), Text::tr("Slow pulse, not to black"), QStringLiteral("abcdefghijklmnopqrrqponmlkjihgfedcba")},
	};
}

int quakeLightStyleRaw(const QString& pattern, double time)
{
	if (pattern.isEmpty()) {
		return 256;
	}
	if (time < 0.0 || !std::isfinite(time)) {
		time = 0.0;
	}
	// R_AnimateLight: i = (int)(cl.time * 10), one letter a tenth of a second.
	const long long step = static_cast<long long>(time * 10.0);
	const QChar c = pattern.at(static_cast<int>(step % pattern.size())).toLower();
	const int value = c.unicode() - 'a';
	return std::clamp(value, 0, 25) * 22;
}

double quakeLightStyleValue(const QString& pattern, double time)
{
	return quakeLightStyleRaw(pattern, time) / 264.0;
}

} // namespace vibestudio
