#include "core/deflate.h"

#include <QByteArray>
#include <QString>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>
#include <vector>

using namespace vibestudio;

namespace {

int fail(const char* message)
{
	std::cerr << message << "\n";
	return EXIT_FAILURE;
}

bool expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << message << "\n";
		return false;
	}
	return true;
}

// Test-local bit writer used to hand-build streams that VibeStudio's own
// encoder never produces (stored headers, dynamic Huffman blocks, deliberately
// corrupt back references). Bit packing follows RFC 1951 3.1.1: values are
// least significant bit first, Huffman codes most significant bit first.
class TestBits {
public:
	void bits(quint32 value, int count)
	{
		for (int i = 0; i < count; ++i) {
			buffer_ |= ((value >> i) & 1u) << count_;
			if (++count_ == 8) {
				out_.append(static_cast<char>(static_cast<quint8>(buffer_)));
				buffer_ = 0;
				count_ = 0;
			}
		}
	}

	void code(quint32 value, int count)
	{
		for (int i = count - 1; i >= 0; --i) {
			bits((value >> i) & 1u, 1);
		}
	}

	QByteArray finish()
	{
		if (count_ > 0) {
			out_.append(static_cast<char>(static_cast<quint8>(buffer_)));
			buffer_ = 0;
			count_ = 0;
		}
		return out_;
	}

private:
	QByteArray out_;
	quint32 buffer_ = 0;
	int count_ = 0;
};

QByteArray repeat(const QByteArray& unit, int times)
{
	QByteArray bytes;
	bytes.reserve(unit.size() * times);
	for (int i = 0; i < times; ++i) {
		bytes.append(unit);
	}
	return bytes;
}

// Deterministic pseudo-random bytes so the fixtures never depend on the host.
QByteArray pseudoRandom(int size, quint32 seed)
{
	QByteArray bytes;
	bytes.reserve(size);
	quint32 state = seed;
	for (int i = 0; i < size; ++i) {
		state = state * 1664525u + 1013904223u;
		bytes.append(static_cast<char>(static_cast<quint8>((state >> 24) & 0xffu)));
	}
	return bytes;
}

// Prose-like bytes: a small vocabulary, spaces and punctuation, which is the
// shape of input where a dynamic Huffman code pays for its table.
QByteArray textPayload(int size)
{
	static const char* const words[] = {
		"the", "compiler", "emitted", "a", "leak", "file", "for", "base1", "and",
		"the", "editor", "reloaded", "it", "before", "the", "next", "build",
		"texture", "wall", "trim", "light", "panel", "sky", "clip", "hint",
		"brush", "entity", "worldspawn", "func_door", "info_player_start"
	};
	constexpr int kWordCount = static_cast<int>(sizeof(words) / sizeof(words[0]));
	QByteArray bytes;
	bytes.reserve(size + 64);
	quint32 state = 0x2f6e73c1u;
	int sentence = 0;
	while (bytes.size() < size) {
		state = state * 1664525u + 1013904223u;
		bytes.append(words[static_cast<int>((state >> 13) % static_cast<quint32>(kWordCount))]);
		if (++sentence >= 9) {
			bytes.append(".\n");
			sentence = 0;
		} else {
			bytes.append(' ');
		}
	}
	return bytes.left(size);
}

// A payload whose literal frequencies force an unconstrained Huffman code past
// fifteen bits, so the encoder has to length-limit it (RFC 1951 3.2.7 caps
// literal/length and distance codes at fifteen bits).
//
// Fibonacci weights are the extremal case for Huffman depth, so twenty-one
// symbols get Fibonacci counts. Two things keep that skew intact through LZ77
// tokenisation: the symbols are shuffled rather than left in runs, and every
// one of them is separated by two bytes drawn from a wide filler alphabet, so
// three-byte sequences almost never repeat and the bytes survive as literals.
// Measured on this fixture, a plain Huffman construction reaches seventeen bits
// while the encoder codes it in fifteen.
QByteArray deepCodeSkew()
{
	QByteArray symbols;
	quint32 a = 1;
	quint32 b = 1;
	for (int symbol = 0; symbol < 21; ++symbol) {
		const quint32 count = a;
		const quint32 next = a + b;
		a = b;
		b = next;
		symbols.append(QByteArray(static_cast<int>(count), static_cast<char>(static_cast<quint8>(1 + symbol))));
	}
	quint32 state = 0x13579bdfu;
	for (int i = static_cast<int>(symbols.size()) - 1; i > 0; --i) {
		state = state * 1664525u + 1013904223u;
		const int j = static_cast<int>((state >> 8) % static_cast<quint32>(i + 1));
		const char swap = symbols.at(i);
		symbols[i] = symbols.at(j);
		symbols[j] = swap;
	}

	QByteArray bytes;
	bytes.reserve(symbols.size() * 3);
	for (qsizetype i = 0; i < symbols.size(); ++i) {
		for (int filler = 0; filler < 2; ++filler) {
			state = state * 1664525u + 1013904223u;
			bytes.append(static_cast<char>(static_cast<quint8>(56u + ((state >> 11) % 200u))));
		}
		bytes.append(symbols.at(i));
	}
	return bytes;
}

QByteArray mixedPayload(int size)
{
	QByteArray bytes;
	bytes.reserve(size);
	quint32 state = 7u;
	while (bytes.size() < size) {
		state = state * 1103515245u + 12345u;
		if ((state >> 28) < 6u) {
			bytes.append("vibestudio/textures/base_wall/");
		} else if ((state >> 28) < 11u) {
			bytes.append(QByteArray(17, static_cast<char>('a' + static_cast<int>((state >> 8) % 26u))));
		} else {
			bytes.append(pseudoRandom(23, state));
		}
	}
	return bytes.left(size);
}

const char* levelName(DeflateLevel level)
{
	switch (level) {
	case DeflateLevel::Store:
		return "store";
	case DeflateLevel::Fast:
		return "fast";
	case DeflateLevel::Default:
		return "default";
	case DeflateLevel::Best:
		return "best";
	}
	return "?";
}

// BTYPE of the first block: 0 stored, 1 fixed Huffman, 2 dynamic Huffman
// (RFC 1951 3.2.3). BFINAL is the low bit of the first byte.
int firstBlockType(const QByteArray& stream)
{
	if (stream.isEmpty()) {
		return -1;
	}
	return static_cast<int>((static_cast<quint8>(stream.at(0)) >> 1) & 0x03u);
}

// A valid fixed-Huffman encoding of `payload` built from literals only
// (RFC 1951 3.2.6): symbols 0-143 are eight bits from 0x30, symbols 144-255 are
// nine bits from 0x190, and end-of-block is seven zero bits.
QByteArray fixedLiteralStream(const QByteArray& payload)
{
	TestBits bits;
	bits.bits(1, 1); // BFINAL
	bits.bits(1, 2); // BTYPE = 01
	for (qsizetype i = 0; i < payload.size(); ++i) {
		const int symbol = static_cast<quint8>(payload.at(i));
		if (symbol < 144) {
			bits.code(static_cast<quint32>(0x30 + symbol), 8);
		} else {
			bits.code(static_cast<quint32>(0x190 + symbol - 144), 9);
		}
	}
	bits.code(0, 7); // end-of-block
	return bits.finish();
}

bool roundTrip(const QByteArray& payload, DeflateLevel level, const char* label)
{
	bool ok = true;
	const QByteArray stream = deflateRaw(payload, level);
	ok &= expect(!stream.isEmpty(), "Deflate should always emit at least one block.");

	const InflateResult sized = inflateRaw(stream, payload.size());
	if (!sized.ok) {
		std::cerr << label << " (" << levelName(level) << ") sized inflate failed: "
			<< qUtf8Printable(sized.error) << "\n";
		return false;
	}
	ok &= expect(sized.data == payload, "Round trip with an expected size must reproduce the input.");
	ok &= expect(sized.bytesConsumed == stream.size(), "Round trip should consume the whole stream.");

	const InflateResult unsized = inflateRaw(stream);
	ok &= expect(unsized.ok && unsized.data == payload, "Round trip without an expected size must reproduce the input.");

	// Trailing bytes stand in for a ZIP data descriptor: they must be ignored
	// and must not be counted as consumed.
	QByteArray withTrailer = stream;
	withTrailer.append("PK\x07\x08TRAILER");
	const InflateResult trailing = inflateRaw(withTrailer, payload.size());
	ok &= expect(trailing.ok && trailing.data == payload, "Trailing bytes must not break decoding.");
	ok &= expect(trailing.bytesConsumed == stream.size(), "bytesConsumed must locate the end of the deflate data.");

	// Determinism: package writers depend on byte-identical output.
	ok &= expect(deflateRaw(payload, level) == stream, "Deflate output must be deterministic.");

	if (!ok) {
		std::cerr << "  failing payload: " << label << " at level " << levelName(level) << "\n";
	}
	return ok;
}

bool runChecksumSmoke()
{
	bool ok = true;
	// The CRC-32 check value published with the algorithm (ITU-T V.42 / ZIP).
	ok &= expect(crc32Bytes(QByteArrayLiteral("123456789")) == 0xcbf43926u, "CRC-32 of \"123456789\" should be 0xCBF43926.");
	ok &= expect(crc32Bytes(QByteArray()) == 0u, "CRC-32 of empty input should be 0.");
	ok &= expect(crc32Bytes(QByteArrayLiteral("The quick brown fox jumps over the lazy dog")) == 0x414fa339u,
		"CRC-32 of the pangram should be 0x414FA339.");
	// Streaming in two parts must equal the single-shot result.
	const QByteArray whole = QByteArrayLiteral("123456789");
	const quint32 partial = crc32Bytes(whole.left(4));
	ok &= expect(crc32Bytes(whole.mid(4), partial) == 0xcbf43926u, "CRC-32 should chain through a seed.");

	// RFC 1950 Adler-32 check values.
	ok &= expect(adler32Bytes(QByteArray()) == 1u, "Adler-32 of empty input should be 1.");
	ok &= expect(adler32Bytes(QByteArrayLiteral("123456789")) == 0x091e01deu, "Adler-32 of \"123456789\" should be 0x091E01DE.");
	ok &= expect(adler32Bytes(QByteArrayLiteral("abc")) == 0x024d0127u, "Adler-32 of \"abc\" should be 0x024D0127.");
	const quint32 adlerPartial = adler32Bytes(QByteArrayLiteral("1234"));
	ok &= expect(adler32Bytes(QByteArrayLiteral("56789"), adlerPartial) == 0x091e01deu, "Adler-32 should chain through a seed.");
	// Long input exercises the deferred modulo reduction.
	ok &= expect(adler32Bytes(QByteArray(100000, '\xff')) != 0u, "Adler-32 of a long buffer should compute.");
	return ok;
}

bool runLevelIdSmoke()
{
	bool ok = true;
	ok &= expect(deflateLevelId(DeflateLevel::Store) == QStringLiteral("store"), "Store level id mismatch.");
	ok &= expect(deflateLevelId(DeflateLevel::Fast) == QStringLiteral("fast"), "Fast level id mismatch.");
	ok &= expect(deflateLevelId(DeflateLevel::Default) == QStringLiteral("default"), "Default level id mismatch.");
	ok &= expect(deflateLevelId(DeflateLevel::Best) == QStringLiteral("best"), "Best level id mismatch.");

	DeflateLevel level = DeflateLevel::Default;
	ok &= expect(deflateLevelFromId(QStringLiteral(" STORE "), &level) && level == DeflateLevel::Store, "Level id parsing should trim and fold case.");
	ok &= expect(deflateLevelFromId(QStringLiteral("fast"), &level) && level == DeflateLevel::Fast, "Fast level id should parse.");
	ok &= expect(deflateLevelFromId(QStringLiteral("default"), &level) && level == DeflateLevel::Default, "Default level id should parse.");
	ok &= expect(deflateLevelFromId(QStringLiteral(" Best "), &level) && level == DeflateLevel::Best, "Best level id should parse.");
	ok &= expect(deflateLevelFromId(QStringLiteral("store"), nullptr), "Level id parsing should accept a null out pointer.");
	level = DeflateLevel::Fast;
	ok &= expect(!deflateLevelFromId(QStringLiteral("ultra"), &level), "Unknown level id should fail.");
	ok &= expect(level == DeflateLevel::Fast, "Failed level id parsing must not modify the output.");
	return ok;
}

bool runRoundTripSmoke()
{
	bool ok = true;
	const DeflateLevel levels[4] = { DeflateLevel::Store, DeflateLevel::Fast, DeflateLevel::Default, DeflateLevel::Best };

	const QByteArray empty;
	const QByteArray tiny = QByteArrayLiteral("a");
	const QByteArray repetitive = repeat(QByteArrayLiteral("quake2/maps/base1.bsp "), 4000);
	const QByteArray runs(30000, 'Z');
	const QByteArray randomish = pseudoRandom(40000, 0x5eed1234u);
	const QByteArray large = mixedPayload(200000);
	const QByteArray blockEdge = mixedPayload(65535);
	const QByteArray blockEdgePlusOne = mixedPayload(65536);
	const QByteArray text = textPayload(40000);
	const QByteArray hugeText = textPayload(180000);

	for (DeflateLevel level : levels) {
		ok &= roundTrip(empty, level, "empty");
		ok &= roundTrip(tiny, level, "single byte");
		ok &= roundTrip(repetitive, level, "repetitive");
		ok &= roundTrip(runs, level, "single byte runs");
		ok &= roundTrip(randomish, level, "incompressible");
		ok &= roundTrip(large, level, "large multi-block");
		ok &= roundTrip(blockEdge, level, "exactly one stored block");
		ok &= roundTrip(blockEdgePlusOne, level, "one byte past a stored block");
		ok &= roundTrip(text, level, "text");
		ok &= roundTrip(hugeText, level, "text past one block");
	}

	// Compressing levels must actually compress repetitive data, and must never
	// blow up incompressible data by more than the stored block overhead.
	ok &= expect(deflateRaw(repetitive, DeflateLevel::Default).size() < repetitive.size() / 8,
		"Default level should compress highly repetitive input.");
	ok &= expect(deflateRaw(repetitive, DeflateLevel::Fast).size() < repetitive.size() / 4,
		"Fast level should compress highly repetitive input.");
	const qsizetype randomBlocks = (randomish.size() + 65534) / 65535;
	ok &= expect(deflateRaw(randomish, DeflateLevel::Default).size() <= randomish.size() + 5 * randomBlocks,
		"Incompressible input must fall back to stored blocks.");

	// Store level emits stored blocks only: 5 bytes of header per block.
	const QByteArray stored = deflateRaw(large, DeflateLevel::Store);
	const qsizetype storedBlocks = (large.size() + 65534) / 65535;
	ok &= expect(stored.size() == large.size() + 5 * storedBlocks, "Store level should emit 5 bytes of header per block.");
	ok &= expect((static_cast<quint8>(stored.at(0)) & 0x06u) == 0u, "Store level should emit BTYPE 00.");
	const QByteArray emptyStored = deflateRaw(empty, DeflateLevel::Store);
	ok &= expect(emptyStored.size() == 5 && static_cast<quint8>(emptyStored.at(0)) == 0x01u,
		"Empty store stream should be one final, empty stored block.");
	return ok;
}

bool runBlockChoiceSmoke()
{
	bool ok = true;
	const DeflateLevel compressing[3] = { DeflateLevel::Fast, DeflateLevel::Default, DeflateLevel::Best };

	// Text: the encoder should pay for a dynamic table and come out ahead of a
	// fixed-Huffman encoding of the same bytes.
	const QByteArray text = textPayload(40000);
	const QByteArray fixedEncoding = fixedLiteralStream(text);
	const InflateResult fixedCheck = inflateRaw(fixedEncoding, text.size());
	ok &= expect(fixedCheck.ok && fixedCheck.data == text,
		"The hand-built fixed Huffman encoding should be a valid encoding of the same input.");
	for (DeflateLevel level : compressing) {
		const QByteArray stream = deflateRaw(text, level);
		ok &= expect(firstBlockType(stream) == 2, "Text should be coded as a dynamic Huffman block.");
		ok &= expect(stream.size() < fixedEncoding.size(),
			"A dynamic Huffman block should beat the fixed Huffman encoding of the same input.");
	}

	// Incompressible bytes: stored, and never larger than the payload plus the
	// five-byte stored block header.
	const QByteArray randomish = pseudoRandom(40000, 0x5eed1234u);
	for (DeflateLevel level : compressing) {
		const QByteArray stream = deflateRaw(randomish, level);
		ok &= expect(firstBlockType(stream) == 0, "Incompressible input should fall back to a stored block.");
		ok &= expect(static_cast<quint8>(stream.at(0)) == 0x01u, "A single stored block should be marked final.");
		ok &= expect(stream.size() == randomish.size() + 5, "A stored block must not expand its payload.");
	}

	// Nothing may expand, whatever the shape of the input.
	const QByteArray shapes[5] = {
		QByteArray(),
		QByteArrayLiteral("a"),
		pseudoRandom(1, 0x11u),
		pseudoRandom(255, 0x2222u),
		mixedPayload(70000)
	};
	for (const QByteArray& payload : shapes) {
		for (DeflateLevel level : compressing) {
			const QByteArray stream = deflateRaw(payload, level);
			const qsizetype blocks = qMax<qsizetype>(1, (payload.size() + 65534) / 65535);
			ok &= expect(stream.size() <= payload.size() + 5 * blocks, "Deflate must never expand beyond stored block overhead.");
		}
	}

	// Determinism at every level, including the dynamic path: package writers
	// depend on byte-identical repeat runs.
	const QByteArray fixtures[3] = { text, randomish, mixedPayload(90000) };
	const DeflateLevel allLevels[4] = { DeflateLevel::Store, DeflateLevel::Fast, DeflateLevel::Default, DeflateLevel::Best };
	for (const QByteArray& payload : fixtures) {
		for (DeflateLevel level : allLevels) {
			ok &= expect(deflateRaw(payload, level) == deflateRaw(payload, level),
				"Two runs at the same level must produce byte-identical output.");
		}
	}
	return ok;
}

bool runLengthLimitSmoke()
{
	bool ok = true;
	const QByteArray skewed = deepCodeSkew();
	ok &= expect(skewed.size() > 65535, "The skewed fixture should span more than one block.");

	const DeflateLevel levels[4] = { DeflateLevel::Store, DeflateLevel::Fast, DeflateLevel::Default, DeflateLevel::Best };
	for (DeflateLevel level : levels) {
		ok &= roundTrip(skewed, level, "pathological frequencies");
	}

	// The decoder refuses any code longer than fifteen bits, so a dynamic block
	// that inflates back to the input is proof that the encoder length-limited a
	// distribution whose unconstrained Huffman code would have needed seventeen.
	for (DeflateLevel level : { DeflateLevel::Fast, DeflateLevel::Default, DeflateLevel::Best }) {
		const QByteArray stream = deflateRaw(skewed, level);
		ok &= expect(firstBlockType(stream) == 2, "A skewed block should still be coded as a dynamic Huffman block.");
		const InflateResult result = inflateRaw(stream, skewed.size());
		if (!result.ok) {
			std::cerr << "skewed stream failed: " << qUtf8Printable(result.error) << "\n";
		}
		ok &= expect(result.ok && result.data == skewed, "A length-limited dynamic block must round trip.");
		ok &= expect(stream.size() < skewed.size(), "A skewed distribution should still compress.");
	}

	// A payload too short and too varied to produce any back reference: whatever
	// block type wins, it has to round trip.
	QByteArray noMatches;
	for (int i = 0; i < 200; ++i) {
		noMatches.append(static_cast<char>(static_cast<quint8>((i * 37) & 0xffu)));
	}
	for (DeflateLevel level : { DeflateLevel::Fast, DeflateLevel::Default, DeflateLevel::Best }) {
		const QByteArray stream = deflateRaw(noMatches, level);
		const InflateResult result = inflateRaw(stream, noMatches.size());
		ok &= expect(result.ok && result.data == noMatches, "A block without back references must round trip.");
	}
	return ok;
}

bool runKnownStreamSmoke()
{
	bool ok = true;

	// The canonical two-byte empty stream: BFINAL=1, BTYPE=01, end-of-block.
	const QByteArray emptyFixed = QByteArray::fromHex("0300");
	InflateResult result = inflateRaw(emptyFixed, 0);
	ok &= expect(result.ok && result.data.isEmpty(), "The two-byte empty fixed stream should inflate to nothing.");
	ok &= expect(result.bytesConsumed == 2, "The two-byte empty fixed stream should consume both bytes.");

	// Fixed Huffman literals for "abc": codes 0x91, 0x92, 0x93 then the
	// seven-zero-bit end-of-block symbol (RFC 1951 3.2.6).
	const QByteArray fixedAbc = QByteArray::fromHex("4b4c4a0600");
	result = inflateRaw(fixedAbc, 3);
	ok &= expect(result.ok && result.data == QByteArrayLiteral("abc"), "Known fixed Huffman stream should inflate to \"abc\".");
	ok &= expect(result.bytesConsumed == 5, "Known fixed Huffman stream should consume five bytes.");

	// Stored block: BFINAL=1, BTYPE=00, LEN=2, NLEN=~2, "hi".
	QByteArray storedStream = QByteArray::fromHex("010200fdff");
	storedStream.append("hi");
	result = inflateRaw(storedStream, 2);
	ok &= expect(result.ok && result.data == QByteArrayLiteral("hi"), "Known stored block should inflate to \"hi\".");
	ok &= expect(result.bytesConsumed == 7, "Known stored block should consume seven bytes.");

	// Multi-block stream: a non-final stored block followed by a final fixed
	// block, which also exercises the BFINAL bit.
	QByteArray multi = QByteArray::fromHex("000200fdff");
	multi.append("hi");
	multi.append(fixedAbc);
	result = inflateRaw(multi, 5);
	ok &= expect(result.ok && result.data == QByteArrayLiteral("hiabc"), "Multi-block stream should inflate across blocks.");

	// A hand-built dynamic Huffman block (RFC 1951 3.2.7) that exercises the
	// code length alphabet including repeat codes 16, 17 and 18.
	//
	// Literal/length alphabet: 262 codes, of which 'a' (97) has length 1,
	// end-of-block (256) and length code 261 (match length 7) have length 2.
	// Distance alphabet: 4 codes of length 2; only distance code 0 is used.
	// Code length alphabet: 16/17/18 have length 2, symbols 1 and 2 length 3.
	TestBits dynamicBits;
	dynamicBits.bits(1, 1); // BFINAL
	dynamicBits.bits(2, 2); // BTYPE = 10, dynamic Huffman
	dynamicBits.bits(262 - 257, 5); // HLIT
	dynamicBits.bits(4 - 1, 5);     // HDIST
	dynamicBits.bits(18 - 4, 4);    // HCLEN
	{
		// Code lengths for the code length alphabet, in RFC 1951 order.
		const int order[18] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1 };
		for (int i = 0; i < 18; ++i) {
			int length = 0;
			if (order[i] == 16 || order[i] == 17 || order[i] == 18) {
				length = 2;
			} else if (order[i] == 1 || order[i] == 2) {
				length = 3;
			}
			dynamicBits.bits(static_cast<quint32>(length), 3);
		}
	}
	// Canonical codes over that alphabet: 16 -> 00, 17 -> 01, 18 -> 10,
	// 1 -> 110, 2 -> 111.
	dynamicBits.code(0x2, 2); dynamicBits.bits(97 - 11, 7);  // 97 zero lengths
	dynamicBits.code(0x6, 3);                                // literal 'a' length 1
	dynamicBits.code(0x2, 2); dynamicBits.bits(138 - 11, 7); // 138 zero lengths
	dynamicBits.code(0x2, 2); dynamicBits.bits(20 - 11, 7);  // 20 zero lengths
	dynamicBits.code(0x7, 3);                                // end-of-block length 2
	dynamicBits.code(0x1, 2); dynamicBits.bits(4 - 3, 3);    // 4 zero lengths
	dynamicBits.code(0x7, 3);                                // length code 261 length 2
	dynamicBits.code(0x7, 3);                                // distance code 0 length 2
	dynamicBits.code(0x0, 2); dynamicBits.bits(3 - 3, 2);    // repeat that length 3x
	// Payload: literal 'a', then match length 7 at distance 1, then end-of-block.
	dynamicBits.code(0x0, 1); // 'a'
	dynamicBits.code(0x3, 2); // length symbol 261 => 7 bytes
	dynamicBits.code(0x0, 2); // distance symbol 0 => distance 1
	dynamicBits.code(0x2, 2); // end-of-block
	const QByteArray dynamicStream = dynamicBits.finish();
	result = inflateRaw(dynamicStream, 8);
	if (!result.ok) {
		std::cerr << "dynamic stream failed: " << qUtf8Printable(result.error) << "\n";
	}
	ok &= expect(result.ok && result.data == QByteArrayLiteral("aaaaaaaa"), "Hand-built dynamic Huffman block should inflate.");

	// Every strict prefix of the dynamic block must fail cleanly.
	for (qsizetype cut = 0; cut < dynamicStream.size(); ++cut) {
		const InflateResult truncated = inflateRaw(dynamicStream.left(cut), 8);
		ok &= expect(!truncated.ok && !truncated.error.isEmpty(), "Truncated dynamic block should fail with an error.");
	}
	return ok;
}

bool runZlibSmoke()
{
	bool ok = true;
	// RFC 1950 wrapper around the known fixed Huffman stream for "abc", with
	// the Adler-32 trailer 0x024D0127.
	const QByteArray zlibAbc = QByteArray::fromHex("789c4b4c4a0600024d0127");
	InflateResult result = inflateZlib(zlibAbc, 3);
	ok &= expect(result.ok && result.data == QByteArrayLiteral("abc"), "Known zlib stream should inflate to \"abc\".");
	ok &= expect(result.bytesConsumed == zlibAbc.size(), "Zlib inflate should consume header, payload and checksum.");

	QByteArray badAdler = zlibAbc;
	badAdler[badAdler.size() - 1] = static_cast<char>(0x28);
	ok &= expect(!inflateZlib(badAdler, 3).ok, "A wrong Adler-32 must be rejected.");

	QByteArray badMethod = zlibAbc;
	badMethod[0] = static_cast<char>(0x77);
	ok &= expect(!inflateZlib(badMethod, 3).ok, "A non-deflate compression method must be rejected.");

	QByteArray badCheck = zlibAbc;
	badCheck[1] = static_cast<char>(0x9d);
	ok &= expect(!inflateZlib(badCheck, 3).ok, "A bad FCHECK must be rejected.");

	QByteArray presetDictionary = zlibAbc;
	// 0x78 0xbb: FDICT set with header bits that still satisfy the modulo check.
	presetDictionary[1] = static_cast<char>(0xbb);
	ok &= expect(!inflateZlib(presetDictionary, 3).ok, "A preset dictionary must be rejected.");

	ok &= expect(!inflateZlib(QByteArray(), 0).ok, "An empty zlib stream must be rejected.");
	ok &= expect(!inflateZlib(zlibAbc.left(zlibAbc.size() - 2), 3).ok, "A zlib stream without its full checksum must be rejected.");
	return ok;
}

bool runHostileInputSmoke()
{
	bool ok = true;
	const QByteArray fixedAbc = QByteArray::fromHex("4b4c4a0600");

	ok &= expect(!inflateRaw(QByteArray()).ok, "Empty input must fail.");
	for (qsizetype cut = 0; cut < fixedAbc.size(); ++cut) {
		ok &= expect(!inflateRaw(fixedAbc.left(cut), 3).ok, "A truncated fixed block must fail.");
	}

	// BTYPE 11 is reserved.
	ok &= expect(!inflateRaw(QByteArray::fromHex("0700")).ok, "Reserved block type must fail.");

	// Stored block with a broken length complement.
	QByteArray brokenStored = QByteArray::fromHex("010200ffff");
	brokenStored.append("hi");
	ok &= expect(!inflateRaw(brokenStored, 2).ok, "A stored block with a bad length complement must fail.");

	// Stored block that claims more data than the stream holds.
	ok &= expect(!inflateRaw(QByteArray::fromHex("01ff0000ff"), -1).ok, "A truncated stored block must fail.");

	// A back reference that points before the start of the output: fixed block,
	// literal 'a', length code 257 (3 bytes) at distance 2.
	TestBits bad;
	bad.bits(1, 1);
	bad.bits(1, 2);
	bad.code(0x91, 8); // literal 'a'
	bad.code(0x01, 7); // length symbol 257 => 3
	bad.code(0x01, 5); // distance symbol 1 => distance 2
	const InflateResult farBack = inflateRaw(bad.finish());
	ok &= expect(!farBack.ok && !farBack.error.isEmpty(), "A distance before the start of the output must fail.");

	// Reserved length code 286 and reserved distance code 30.
	TestBits reservedLength;
	reservedLength.bits(1, 1);
	reservedLength.bits(1, 2);
	reservedLength.code(0xc6, 8); // symbol 286
	ok &= expect(!inflateRaw(reservedLength.finish()).ok, "Reserved length code 286 must fail.");

	TestBits reservedDistance;
	reservedDistance.bits(1, 1);
	reservedDistance.bits(1, 2);
	reservedDistance.code(0x91, 8); // literal 'a'
	reservedDistance.code(0x01, 7); // length 3
	reservedDistance.code(0x1e, 5); // distance symbol 30
	ok &= expect(!inflateRaw(reservedDistance.finish()).ok, "Reserved distance code 30 must fail.");

	// An expected size smaller than the real output is a hard cap.
	const QByteArray repetitive = repeat(QByteArrayLiteral("pak0/sound/"), 500);
	const QByteArray stream = deflateRaw(repetitive, DeflateLevel::Default);
	ok &= expect(!inflateRaw(stream, 16).ok, "An over-long output must be rejected against the expected size.");
	ok &= expect(inflateRaw(stream, repetitive.size() + 1024).ok, "A generous expected size should still decode.");

	// A dynamic header claiming more literal codes than the alphabet allows.
	TestBits tooManyLiterals;
	tooManyLiterals.bits(1, 1);
	tooManyLiterals.bits(2, 2);
	tooManyLiterals.bits(31, 5); // HLIT => 288 codes
	tooManyLiterals.bits(0, 5);
	tooManyLiterals.bits(15, 4);
	ok &= expect(!inflateRaw(tooManyLiterals.finish()).ok, "An oversized literal alphabet must fail.");

	// A dynamic header claiming more distance codes than the alphabet allows.
	TestBits tooManyDistances;
	tooManyDistances.bits(1, 1);
	tooManyDistances.bits(2, 2);
	tooManyDistances.bits(0, 5);
	tooManyDistances.bits(31, 5); // HDIST => 32 codes
	tooManyDistances.bits(15, 4);
	ok &= expect(!inflateRaw(tooManyDistances.finish()).ok, "An oversized distance alphabet must fail.");

	// Random and mutated bytes: the decoder must return, never crash or hang,
	// and must always explain itself when it refuses.
	for (quint32 seed = 1; seed <= 64; ++seed) {
		const QByteArray noise = pseudoRandom(64, seed * 2654435761u);
		const InflateResult noisy = inflateRaw(noise, -1);
		ok &= expect(noisy.ok || !noisy.error.isEmpty(), "A rejected stream must carry an error message.");
	}
	const QByteArray valid = deflateRaw(mixedPayload(4096), DeflateLevel::Default);
	for (qsizetype i = 0; i < valid.size(); i += 7) {
		QByteArray mutated = valid;
		mutated[i] = static_cast<char>(static_cast<quint8>(mutated.at(i)) ^ 0xa5u);
		const InflateResult broken = inflateRaw(mutated, 4096);
		ok &= expect(broken.ok || !broken.error.isEmpty(), "A mutated stream must fail cleanly.");
		const InflateResult brokenUnsized = inflateRaw(mutated, -1);
		ok &= expect(brokenUnsized.ok || !brokenUnsized.error.isEmpty(), "A mutated stream must fail cleanly without a size hint.");
	}
	return ok;
}

} // namespace

int main()
{
	QTemporaryDir tempDir;
	if (!tempDir.isValid()) {
		return fail("Expected temporary directory.");
	}
	bool ok = true;
	ok &= runChecksumSmoke();
	ok &= runLevelIdSmoke();
	ok &= runRoundTripSmoke();
	ok &= runBlockChoiceSmoke();
	ok &= runLengthLimitSmoke();
	ok &= runKnownStreamSmoke();
	ok &= runZlibSmoke();
	ok &= runHostileInputSmoke();
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
