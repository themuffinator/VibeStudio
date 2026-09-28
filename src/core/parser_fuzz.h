#pragma once

// Deterministic corpus generation for parser fuzz tests.
//
// VibeStudio's binary readers (DEFLATE, idTech images, BSP, PAK/WAD/ZIP, Doom
// WAD maps, model meshes) all parse bytes that arrive from somewhere the user
// does not control. This helper turns a handful of valid seed inputs into a
// fixed corpus of corrupted ones so those readers can be exercised against
// hostile input as ordinary Meson tests.
//
// Determinism is the whole point. A CI failure has to be reproducible from the
// test output alone, so:
// - the generator is the small xorshift64* PRNG implemented in parser_fuzz.cpp,
//   never QRandomGenerator and never a clock;
// - the seed is a compile-time constant, or an environment override that falls
//   back to that constant;
// - for a given (seeds, seed, caseCount) triple the corpus is byte-for-byte
//   stable, and every case carries an id that names the seed, the case index
//   and the mutation that produced it.
//
// `FuzzCase::id` and the mutation ids are diagnostic tokens meant for a test
// log and for a human re-running one case, not user-visible prose, so they are
// deliberately not translated.

#include <QByteArray>
#include <QString>
#include <QVector>

namespace vibestudio {

// Marsaglia's xorshift, with Vigna's multiplier scramble (xorshift64*).
// - G. Marsaglia, "Xorshift RNGs", Journal of Statistical Software 8(14), 2003:
//   https://www.jstatsoft.org/article/view/v008i14 (the (12, 25, 27) triple and
//   the shift structure come from that paper's table of full-period triples).
// - S. Vigna, "An experimental exploration of Marsaglia's xorshift generators,
//   scrambled", https://arxiv.org/abs/1402.6246 (the 0x2545F4914F6CDD1D
//   multiplier applied to the state to improve the low bits).
//
// This is a fixture generator, not a source of cryptographic or statistical
// randomness; it is here so the corpus never depends on the host.
class FuzzRandom {
public:
	explicit FuzzRandom(quint64 seed);

	quint64 next();
	quint32 next32();
	// Value in [0, bound). Returns 0 when `bound` is 0. Uses a plain modulo, so
	// it is very slightly biased for bounds that do not divide 2^64; that is
	// irrelevant for picking fixture offsets and keeps the sequence obvious.
	quint64 bounded(quint64 bound);

private:
	quint64 m_state;
};

enum class FuzzMutation {
	// Cut the input at an interesting boundary (header edges, fractions, tail).
	Truncate,
	// Flip one to three bits.
	BitFlip,
	// Zero a 16- or 32-bit field.
	ZeroField,
	// Set a 16- or 32-bit field to all ones.
	MaxField,
	// Set a 32-bit field to 0, 1, 0x7fffffff or 0xffffffff.
	Sentinel,
	// Head of one seed, tail of another.
	Splice,
	// Repeat a slice of the input in place, as a repeated record would be.
	Repeat,
};

// Untranslated token: "truncate", "bitflip", "zero-field", "max-field",
// "sentinel", "splice", "repeat".
QString fuzzMutationId(FuzzMutation mutation);

struct FuzzCase {
	// Index into the generated corpus. Printed on failure so one case can be
	// re-run from the test output.
	int index = 0;
	// Which seed input this case was derived from.
	int seedIndex = 0;
	FuzzMutation mutation = FuzzMutation::BitFlip;
	// Diagnostic token, for example
	// "case=17 seed=2 op=sentinel off=0x0010 value=0xffffffff".
	QString id;
	QByteArray data;
};

// The truncation boundaries worth trying for a buffer of `size` bytes: the
// empty prefix, the first few header-sized prefixes, the quarter points, and
// the last few bytes. Sorted, deduplicated, and all strictly less than `size`.
QVector<qsizetype> fuzzTruncationBoundaries(qsizetype size);

// Values a length or an offset field is forced to: 0, 1, 0x7fffffff and
// 0xffffffff. These are the four that flip a signed/unsigned or an overflow
// check in a reader that has one, and crash a reader that does not.
QVector<quint32> fuzzSentinelValues();

// Builds `caseCount` cases from `seeds`. Mutations rotate so each one gets an
// equal share, and each mutation walks its own list (boundaries, sentinels) in
// order so a small corpus still covers all of them. Returns an empty vector
// when `seeds` is empty or every seed is empty.
QVector<FuzzCase> buildFuzzCorpus(const QVector<QByteArray>& seeds, quint64 seed, int caseCount);

// Reads a seed from `environmentVariable` (decimal, or hexadecimal with a "0x"
// prefix) and falls back to `fallbackSeed` when it is unset, empty, unparsable
// or zero. There is no clock-derived path: an unset variable always yields the
// same constant.
quint64 parserFuzzSeed(const char* environmentVariable, quint64 fallbackSeed);

} // namespace vibestudio
