#include "core/parser_fuzz.h"

#include <QStringList>
#include <QtGlobal>

#include <algorithm>

namespace vibestudio {

namespace {

// Mutations rotate in this order so a corpus of any size covers them evenly.
constexpr int kMutationCount = 7;

FuzzMutation mutationForOrdinal(int ordinal)
{
	switch (ordinal % kMutationCount) {
	case 0:
		return FuzzMutation::Truncate;
	case 1:
		return FuzzMutation::BitFlip;
	case 2:
		return FuzzMutation::ZeroField;
	case 3:
		return FuzzMutation::MaxField;
	case 4:
		return FuzzMutation::Sentinel;
	case 5:
		return FuzzMutation::Splice;
	default:
		return FuzzMutation::Repeat;
	}
}

QString hex(quint64 value, int digits)
{
	return QStringLiteral("0x%1").arg(value, digits, 16, QLatin1Char('0'));
}

void writeLe(QByteArray* data, qsizetype offset, quint32 value, int width)
{
	for (int byte = 0; byte < width; ++byte) {
		const qsizetype at = offset + byte;
		if (at < 0 || at >= data->size()) {
			return;
		}
		(*data)[at] = static_cast<char>(static_cast<quint8>((value >> (8 * byte)) & 0xffu));
	}
}

// A field offset that leaves room for `width` bytes. Biased towards the first
// 64 bytes, because that is where headers - and therefore lengths, counts and
// offsets - live in every format covered here.
qsizetype fieldOffset(FuzzRandom* random, qsizetype size, int width)
{
	if (size < width) {
		return 0;
	}
	const qsizetype limit = size - width + 1;
	const qsizetype headerLimit = std::min<qsizetype>(limit, 64);
	if (headerLimit > 0 && random->bounded(4) != 0) {
		return static_cast<qsizetype>(random->bounded(static_cast<quint64>(headerLimit)));
	}
	return static_cast<qsizetype>(random->bounded(static_cast<quint64>(limit)));
}

} // namespace

FuzzRandom::FuzzRandom(quint64 seed)
	: m_state(seed != 0 ? seed : 0x9e3779b97f4a7c15ull)
{
}

quint64 FuzzRandom::next()
{
	// xorshift64* (see the header for the citations).
	m_state ^= m_state >> 12;
	m_state ^= m_state << 25;
	m_state ^= m_state >> 27;
	return m_state * 0x2545f4914f6cdd1dull;
}

quint32 FuzzRandom::next32()
{
	return static_cast<quint32>(next() >> 32);
}

quint64 FuzzRandom::bounded(quint64 bound)
{
	if (bound == 0) {
		return 0;
	}
	return next() % bound;
}

QString fuzzMutationId(FuzzMutation mutation)
{
	switch (mutation) {
	case FuzzMutation::Truncate:
		return QStringLiteral("truncate");
	case FuzzMutation::BitFlip:
		return QStringLiteral("bitflip");
	case FuzzMutation::ZeroField:
		return QStringLiteral("zero-field");
	case FuzzMutation::MaxField:
		return QStringLiteral("max-field");
	case FuzzMutation::Sentinel:
		return QStringLiteral("sentinel");
	case FuzzMutation::Splice:
		return QStringLiteral("splice");
	case FuzzMutation::Repeat:
		return QStringLiteral("repeat");
	}
	return QStringLiteral("unknown");
}

QVector<qsizetype> fuzzTruncationBoundaries(qsizetype size)
{
	QVector<qsizetype> boundaries;
	if (size <= 0) {
		return boundaries;
	}
	// Header edges first: almost every format here has a magic, a version, a
	// count and an offset inside the first 32 bytes, so cutting there exercises
	// the "read a field that is not there" path of each reader in turn.
	const qsizetype fixed[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 11, 12, 15, 16, 17, 18, 20, 22, 24, 26, 28, 30, 32, 40, 46, 56, 64, 84, 100, 128};
	for (const qsizetype candidate : fixed) {
		if (candidate < size) {
			boundaries.push_back(candidate);
		}
	}
	const qsizetype fractions[] = {size / 8, size / 4, size / 3, size / 2, (size * 2) / 3, (size * 3) / 4, (size * 7) / 8};
	for (const qsizetype candidate : fractions) {
		if (candidate > 0 && candidate < size) {
			boundaries.push_back(candidate);
		}
	}
	for (qsizetype back = 1; back <= 4; ++back) {
		if (size - back > 0) {
			boundaries.push_back(size - back);
		}
	}
	std::sort(boundaries.begin(), boundaries.end());
	boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
	return boundaries;
}

QVector<quint32> fuzzSentinelValues()
{
	return {0u, 1u, 0x7fffffffu, 0xffffffffu};
}

QVector<FuzzCase> buildFuzzCorpus(const QVector<QByteArray>& seeds, quint64 seed, int caseCount)
{
	QVector<FuzzCase> cases;
	if (seeds.isEmpty() || caseCount <= 0) {
		return cases;
	}
	QVector<QByteArray> usable;
	for (const QByteArray& candidate : seeds) {
		if (!candidate.isEmpty()) {
			usable.push_back(candidate);
		}
	}
	if (usable.isEmpty()) {
		return cases;
	}

	FuzzRandom random(seed);
	const QVector<quint32> sentinels = fuzzSentinelValues();
	// Each of these walks its own list in order, so even a short corpus visits
	// every truncation boundary and every sentinel value at least once.
	int truncateOrdinal = 0;
	int sentinelOrdinal = 0;

	cases.reserve(caseCount);
	for (int index = 0; index < caseCount; ++index) {
		const FuzzMutation mutation = mutationForOrdinal(index);
		const int seedIndex = static_cast<int>((index / kMutationCount) % usable.size());
		const QByteArray& base = usable.at(seedIndex);

		FuzzCase generated;
		generated.index = index;
		generated.seedIndex = seedIndex;
		generated.mutation = mutation;
		QString detail;

		switch (mutation) {
		case FuzzMutation::Truncate: {
			const QVector<qsizetype> boundaries = fuzzTruncationBoundaries(base.size());
			if (boundaries.isEmpty()) {
				generated.data = QByteArray();
				detail = QStringLiteral("cut=0");
				break;
			}
			const qsizetype cut = boundaries.at(truncateOrdinal % static_cast<int>(boundaries.size()));
			++truncateOrdinal;
			generated.data = base.left(cut);
			detail = QStringLiteral("cut=%1 of=%2").arg(cut).arg(base.size());
			break;
		}
		case FuzzMutation::BitFlip: {
			generated.data = base;
			const int flips = 1 + static_cast<int>(random.bounded(3));
			QStringList positions;
			for (int flip = 0; flip < flips; ++flip) {
				const auto offset = static_cast<qsizetype>(random.bounded(static_cast<quint64>(base.size())));
				const int bit = static_cast<int>(random.bounded(8));
				generated.data[offset] = static_cast<char>(static_cast<quint8>(generated.data.at(offset)) ^ static_cast<quint8>(1u << bit));
				positions << QStringLiteral("%1/%2").arg(hex(static_cast<quint64>(offset), 4)).arg(bit);
			}
			detail = QStringLiteral("flips=%1").arg(positions.join(QLatin1Char(',')));
			break;
		}
		case FuzzMutation::ZeroField:
		case FuzzMutation::MaxField: {
			generated.data = base;
			const int width = random.bounded(2) == 0 ? 2 : 4;
			const qsizetype offset = fieldOffset(&random, generated.data.size(), width);
			const quint32 value = mutation == FuzzMutation::ZeroField ? 0u : 0xffffffffu;
			writeLe(&generated.data, offset, value, width);
			detail = QStringLiteral("off=%1 width=%2").arg(hex(static_cast<quint64>(offset), 4)).arg(width);
			break;
		}
		case FuzzMutation::Sentinel: {
			generated.data = base;
			const quint32 value = sentinels.at(sentinelOrdinal % static_cast<int>(sentinels.size()));
			++sentinelOrdinal;
			const qsizetype offset = fieldOffset(&random, generated.data.size(), 4);
			writeLe(&generated.data, offset, value, 4);
			detail = QStringLiteral("off=%1 value=%2").arg(hex(static_cast<quint64>(offset), 4), hex(value, 8));
			break;
		}
		case FuzzMutation::Splice: {
			const int otherIndex = static_cast<int>(random.bounded(static_cast<quint64>(usable.size())));
			const QByteArray& other = usable.at(otherIndex);
			const auto head = static_cast<qsizetype>(random.bounded(static_cast<quint64>(base.size()) + 1));
			const auto tail = static_cast<qsizetype>(random.bounded(static_cast<quint64>(other.size()) + 1));
			generated.data = base.left(head) + other.mid(tail);
			detail = QStringLiteral("head=%1 other=%2 tail=%3").arg(head).arg(otherIndex).arg(tail);
			break;
		}
		case FuzzMutation::Repeat: {
			const auto start = static_cast<qsizetype>(random.bounded(static_cast<quint64>(base.size())));
			const qsizetype maximumChunk = std::min<qsizetype>(base.size() - start, 256);
			const qsizetype chunk = 1 + static_cast<qsizetype>(random.bounded(static_cast<quint64>(maximumChunk)));
			const int times = 2 + static_cast<int>(random.bounded(6));
			QByteArray repeated;
			repeated.reserve(chunk * times);
			for (int copy = 0; copy < times; ++copy) {
				repeated.append(base.mid(start, chunk));
			}
			generated.data = base.left(start) + repeated + base.mid(start);
			detail = QStringLiteral("at=%1 chunk=%2 times=%3").arg(start).arg(chunk).arg(times);
			break;
		}
		}

		generated.id = QStringLiteral("case=%1 seed-input=%2 op=%3 %4")
			.arg(index)
			.arg(seedIndex)
			.arg(fuzzMutationId(mutation), detail);
		cases.push_back(generated);
	}
	return cases;
}

quint64 parserFuzzSeed(const char* environmentVariable, quint64 fallbackSeed)
{
	if (!environmentVariable || environmentVariable[0] == '\0') {
		return fallbackSeed;
	}
	const QByteArray raw = qgetenv(environmentVariable).trimmed();
	if (raw.isEmpty()) {
		return fallbackSeed;
	}
	const QString text = QString::fromLatin1(raw);
	bool ok = false;
	const quint64 value = text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)
		? text.mid(2).toULongLong(&ok, 16)
		: text.toULongLong(&ok, 10);
	if (!ok || value == 0) {
		return fallbackSeed;
	}
	return value;
}

} // namespace vibestudio
