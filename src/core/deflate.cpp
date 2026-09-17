#include "core/deflate.h"

#include <QCoreApplication>

#include <array>
#include <vector>

namespace vibestudio {

namespace {

QString deflateText(const char* source)
{
	return QCoreApplication::translate("VibeStudioDeflate", source);
}

// ---------------------------------------------------------------------------
// Shared constants and tables.
//
// Every table below is transcribed from RFC 1951 (DEFLATE Compressed Data
// Format Specification version 1.3), https://www.rfc-editor.org/rfc/rfc1951.
// ---------------------------------------------------------------------------

constexpr int kMaxCodeBits = 15;                        // RFC 1951 3.2.7
constexpr int kEndOfBlockSymbol = 256;                  // RFC 1951 3.2.5
constexpr int kLiteralAlphabetMax = 286;                // RFC 1951 3.2.5
constexpr int kDistanceAlphabetMax = 30;                // RFC 1951 3.2.5
constexpr int kCodeLengthAlphabet = 19;                 // RFC 1951 3.2.7
constexpr qsizetype kWindowSize = 32768;                // RFC 1951 3.2.5
constexpr qsizetype kMinMatch = 3;
constexpr qsizetype kMaxMatch = 258;
constexpr qsizetype kMaxStoredBlock = 65535;            // RFC 1951 3.2.4
constexpr qint64 kUnboundedOutputCeiling = qint64(1) << 30; // 1 GiB safety cap
// A conforming deflate stream cannot expand by more than roughly 1032:1 (a
// 258-byte match encoded in two bits), so without a caller-supplied size the
// input length still bounds how much a hostile stream can make us allocate.
constexpr qint64 kMaxExpansionRatio = 1100;

// RFC 1951 3.2.5, length codes 257..285.
constexpr quint16 kLengthBase[29] = {
	3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
	67, 83, 99, 115, 131, 163, 195, 227, 258
};
constexpr quint8 kLengthExtra[29] = {
	0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
	4, 4, 4, 4, 5, 5, 5, 5, 0
};

// RFC 1951 3.2.5, distance codes 0..29.
constexpr quint16 kDistanceBase[30] = {
	1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513,
	769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
constexpr quint8 kDistanceExtra[30] = {
	0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
	9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

// RFC 1951 3.2.7, transmission order of the code length alphabet.
constexpr quint8 kCodeLengthOrder[kCodeLengthAlphabet] = {
	16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

// ---------------------------------------------------------------------------
// Bit reader. Bits are packed starting with the least significant bit of each
// byte (RFC 1951 3.1.1). Every read is range checked; running out of input is
// reported rather than assumed.
// ---------------------------------------------------------------------------

class BitReader {
public:
	BitReader(const uchar* data, qsizetype size)
		: data_(data)
		, size_(size)
	{
	}

	bool readBit(quint32* out)
	{
		if (bitCount_ == 0) {
			if (pos_ >= size_) {
				return false;
			}
			bitBuffer_ = data_[pos_++];
			bitCount_ = 8;
		}
		*out = bitBuffer_ & 1u;
		bitBuffer_ >>= 1;
		--bitCount_;
		return true;
	}

	bool readBits(int count, quint32* out)
	{
		quint32 value = 0;
		for (int i = 0; i < count; ++i) {
			quint32 bit = 0;
			if (!readBit(&bit)) {
				return false;
			}
			value |= bit << i;
		}
		*out = value;
		return true;
	}

	// Discards the remainder of the current byte (RFC 1951 3.2.4).
	void alignToByte()
	{
		bitBuffer_ = 0;
		bitCount_ = 0;
	}

	bool takeBytes(qsizetype count, const uchar** out)
	{
		if (count < 0 || count > size_ - pos_) {
			return false;
		}
		*out = data_ + pos_;
		pos_ += count;
		return true;
	}

	// Number of input bytes touched so far. A partially consumed byte counts as
	// consumed, which is what a ZIP reader needs to locate a data descriptor.
	qsizetype bytesConsumed() const { return pos_; }

private:
	const uchar* data_ = nullptr;
	qsizetype size_ = 0;
	qsizetype pos_ = 0;
	quint32 bitBuffer_ = 0;
	int bitCount_ = 0;
};

// ---------------------------------------------------------------------------
// Canonical Huffman decoding, per the counts/offsets construction described in
// RFC 1951 3.2.2.
// ---------------------------------------------------------------------------

struct HuffmanTable {
	std::array<quint16, kMaxCodeBits + 1> counts {};
	std::vector<quint16> symbols;
	int codeCount = 0;
	bool incomplete = true;
};

bool buildHuffman(const quint8* lengths, int count, HuffmanTable* table)
{
	table->counts.fill(0);
	table->symbols.clear();
	table->codeCount = 0;
	table->incomplete = true;
	for (int i = 0; i < count; ++i) {
		if (lengths[i] > kMaxCodeBits) {
			return false;
		}
		++table->counts[lengths[i]];
	}
	table->codeCount = count - table->counts[0];

	// Kraft check: reject an over-subscribed code set outright.
	int left = 1;
	for (int len = 1; len <= kMaxCodeBits; ++len) {
		left <<= 1;
		left -= table->counts[len];
		if (left < 0) {
			return false;
		}
	}
	table->incomplete = left > 0;

	std::array<int, kMaxCodeBits + 2> offsets {};
	offsets[1] = 0;
	for (int len = 1; len <= kMaxCodeBits; ++len) {
		offsets[len + 1] = offsets[len] + table->counts[len];
	}
	table->symbols.assign(static_cast<size_t>(table->codeCount), 0);
	for (int i = 0; i < count; ++i) {
		if (lengths[i] != 0) {
			table->symbols[static_cast<size_t>(offsets[lengths[i]]++)] = static_cast<quint16>(i);
		}
	}
	return true;
}

constexpr int kDecodeTruncated = -2;
constexpr int kDecodeInvalid = -1;

// Walks the canonical code space one bit at a time (RFC 1951 3.2.2). Returns a
// symbol, or kDecodeTruncated / kDecodeInvalid.
int decodeSymbol(BitReader* reader, const HuffmanTable& table)
{
	int code = 0;
	int first = 0;
	int index = 0;
	for (int len = 1; len <= kMaxCodeBits; ++len) {
		quint32 bit = 0;
		if (!reader->readBit(&bit)) {
			return kDecodeTruncated;
		}
		code |= static_cast<int>(bit);
		const int count = table.counts[len];
		if (code - first < count) {
			return table.symbols[static_cast<size_t>(index + (code - first))];
		}
		index += count;
		first = (first + count) << 1;
		code <<= 1;
	}
	return kDecodeInvalid;
}

struct FixedTables {
	HuffmanTable literal;
	HuffmanTable distance;
};

// RFC 1951 3.2.6, the fixed Huffman code lengths.
FixedTables makeFixedTables()
{
	FixedTables tables;
	quint8 literalLengths[288];
	for (int i = 0; i < 144; ++i) {
		literalLengths[i] = 8;
	}
	for (int i = 144; i < 256; ++i) {
		literalLengths[i] = 9;
	}
	for (int i = 256; i < 280; ++i) {
		literalLengths[i] = 7;
	}
	for (int i = 280; i < 288; ++i) {
		literalLengths[i] = 8;
	}
	quint8 distanceLengths[30];
	for (int i = 0; i < 30; ++i) {
		distanceLengths[i] = 5;
	}
	buildHuffman(literalLengths, 288, &tables.literal);
	buildHuffman(distanceLengths, 30, &tables.distance);
	return tables;
}

const FixedTables& fixedTables()
{
	static const FixedTables tables = makeFixedTables();
	return tables;
}

// ---------------------------------------------------------------------------
// Inflate.
// ---------------------------------------------------------------------------

bool readDynamicTables(BitReader* reader, HuffmanTable* literal, HuffmanTable* distance, QString* error)
{
	// RFC 1951 3.2.7.
	quint32 hlit = 0;
	quint32 hdist = 0;
	quint32 hclen = 0;
	if (!reader->readBits(5, &hlit) || !reader->readBits(5, &hdist) || !reader->readBits(4, &hclen)) {
		*error = deflateText("Deflate stream ended inside a dynamic block header.");
		return false;
	}
	const int literalCount = static_cast<int>(hlit) + 257;
	const int distanceCount = static_cast<int>(hdist) + 1;
	const int codeLengthCount = static_cast<int>(hclen) + 4;
	if (literalCount > kLiteralAlphabetMax) {
		*error = deflateText("Deflate dynamic block declares too many literal codes.");
		return false;
	}
	if (distanceCount > kDistanceAlphabetMax) {
		*error = deflateText("Deflate dynamic block declares too many distance codes.");
		return false;
	}

	quint8 codeLengthLengths[kCodeLengthAlphabet] = {};
	for (int i = 0; i < codeLengthCount; ++i) {
		quint32 value = 0;
		if (!reader->readBits(3, &value)) {
			*error = deflateText("Deflate stream ended inside the code length alphabet.");
			return false;
		}
		codeLengthLengths[kCodeLengthOrder[i]] = static_cast<quint8>(value);
	}

	HuffmanTable codeLengthTable;
	if (!buildHuffman(codeLengthLengths, kCodeLengthAlphabet, &codeLengthTable) || codeLengthTable.codeCount == 0) {
		*error = deflateText("Deflate code length alphabet is malformed.");
		return false;
	}

	const int totalLengths = literalCount + distanceCount;
	std::vector<quint8> lengths(static_cast<size_t>(totalLengths), 0);
	int index = 0;
	while (index < totalLengths) {
		const int symbol = decodeSymbol(reader, codeLengthTable);
		if (symbol == kDecodeTruncated) {
			*error = deflateText("Deflate stream ended inside the code length list.");
			return false;
		}
		if (symbol < 0 || symbol >= kCodeLengthAlphabet) {
			*error = deflateText("Deflate code length list contains an invalid code.");
			return false;
		}
		if (symbol < 16) {
			lengths[static_cast<size_t>(index++)] = static_cast<quint8>(symbol);
			continue;
		}
		int repeat = 0;
		quint8 value = 0;
		quint32 extra = 0;
		if (symbol == 16) {
			if (index == 0) {
				*error = deflateText("Deflate code length repeat has no previous length.");
				return false;
			}
			if (!reader->readBits(2, &extra)) {
				*error = deflateText("Deflate stream ended inside a code length repeat.");
				return false;
			}
			repeat = 3 + static_cast<int>(extra);
			value = lengths[static_cast<size_t>(index - 1)];
		} else if (symbol == 17) {
			if (!reader->readBits(3, &extra)) {
				*error = deflateText("Deflate stream ended inside a code length repeat.");
				return false;
			}
			repeat = 3 + static_cast<int>(extra);
		} else {
			if (!reader->readBits(7, &extra)) {
				*error = deflateText("Deflate stream ended inside a code length repeat.");
				return false;
			}
			repeat = 11 + static_cast<int>(extra);
		}
		if (repeat > totalLengths - index) {
			*error = deflateText("Deflate code length repeat runs past the end of the alphabet.");
			return false;
		}
		for (int i = 0; i < repeat; ++i) {
			lengths[static_cast<size_t>(index++)] = value;
		}
	}

	if (!buildHuffman(lengths.data(), literalCount, literal) || literal->incomplete) {
		*error = deflateText("Deflate literal/length code table is malformed.");
		return false;
	}
	if (lengths[static_cast<size_t>(kEndOfBlockSymbol)] == 0) {
		*error = deflateText("Deflate literal/length code table has no end-of-block code.");
		return false;
	}
	if (!buildHuffman(lengths.data() + literalCount, distanceCount, distance)) {
		*error = deflateText("Deflate distance code table is malformed.");
		return false;
	}
	// A distance table with a single code is incomplete but legal in practice;
	// anything else that is incomplete is rejected.
	if (distance->incomplete && distance->codeCount > 1) {
		*error = deflateText("Deflate distance code table is incomplete.");
		return false;
	}
	return true;
}

bool inflateHuffmanBlock(BitReader* reader,
	const HuffmanTable& literal,
	const HuffmanTable& distance,
	QByteArray* out,
	qint64 cap,
	QString* error)
{
	for (;;) {
		const int symbol = decodeSymbol(reader, literal);
		if (symbol == kDecodeTruncated) {
			*error = deflateText("Deflate stream ended inside a compressed block.");
			return false;
		}
		if (symbol < 0) {
			*error = deflateText("Deflate block contains an invalid literal/length code.");
			return false;
		}
		if (symbol == kEndOfBlockSymbol) {
			return true;
		}
		if (symbol < kEndOfBlockSymbol) {
			if (out->size() + 1 > cap) {
				*error = deflateText("Deflate output is larger than expected.");
				return false;
			}
			out->append(static_cast<char>(static_cast<quint8>(symbol)));
			continue;
		}
		const int lengthIndex = symbol - 257;
		if (lengthIndex >= 29) {
			*error = deflateText("Deflate block uses a reserved length code.");
			return false;
		}
		quint32 lengthExtra = 0;
		if (!reader->readBits(kLengthExtra[lengthIndex], &lengthExtra)) {
			*error = deflateText("Deflate stream ended inside a length code.");
			return false;
		}
		const qsizetype length = static_cast<qsizetype>(kLengthBase[lengthIndex]) + static_cast<qsizetype>(lengthExtra);

		const int distanceSymbol = decodeSymbol(reader, distance);
		if (distanceSymbol == kDecodeTruncated) {
			*error = deflateText("Deflate stream ended inside a distance code.");
			return false;
		}
		if (distanceSymbol < 0) {
			*error = deflateText("Deflate block contains an invalid distance code.");
			return false;
		}
		if (distanceSymbol >= kDistanceAlphabetMax) {
			*error = deflateText("Deflate block uses a reserved distance code.");
			return false;
		}
		quint32 distanceExtra = 0;
		if (!reader->readBits(kDistanceExtra[distanceSymbol], &distanceExtra)) {
			*error = deflateText("Deflate stream ended inside a distance code.");
			return false;
		}
		const qsizetype dist = static_cast<qsizetype>(kDistanceBase[distanceSymbol]) + static_cast<qsizetype>(distanceExtra);
		if (dist > out->size()) {
			*error = deflateText("Deflate back reference points before the start of the output.");
			return false;
		}
		if (out->size() + length > cap) {
			*error = deflateText("Deflate output is larger than expected.");
			return false;
		}
		const qsizetype start = out->size() - dist;
		const qsizetype oldSize = out->size();
		out->resize(oldSize + length);
		char* target = out->data();
		for (qsizetype i = 0; i < length; ++i) {
			target[oldSize + i] = target[start + i];
		}
	}
}

InflateResult inflateStream(const uchar* data, qsizetype size, qint64 expectedSize)
{
	InflateResult result;
	if (size <= 0) {
		result.error = deflateText("Deflate stream is empty.");
		return result;
	}
	const qint64 cap = expectedSize >= 0
		? expectedSize
		: qMin(kUnboundedOutputCeiling, static_cast<qint64>(size) * kMaxExpansionRatio + kMaxStoredBlock);
	QByteArray out;
	const qint64 reserve = qMin<qint64>(cap, qint64(1) << 22);
	if (reserve > 0) {
		out.reserve(static_cast<qsizetype>(reserve));
	}

	BitReader reader(data, size);
	bool finalBlock = false;
	// Every iteration consumes at least three bits of a finite input, so this
	// loop always terminates.
	while (!finalBlock) {
		quint32 bfinal = 0;
		quint32 btype = 0;
		if (!reader.readBits(1, &bfinal) || !reader.readBits(2, &btype)) {
			result.error = deflateText("Deflate stream ended inside a block header.");
			return result;
		}
		finalBlock = bfinal != 0;
		if (btype == 0) {
			// RFC 1951 3.2.4, stored block.
			reader.alignToByte();
			const uchar* header = nullptr;
			if (!reader.takeBytes(4, &header)) {
				result.error = deflateText("Deflate stored block header is truncated.");
				return result;
			}
			const quint32 length = static_cast<quint32>(header[0]) | (static_cast<quint32>(header[1]) << 8);
			const quint32 inverse = static_cast<quint32>(header[2]) | (static_cast<quint32>(header[3]) << 8);
			if (((~length) & 0xffffu) != inverse) {
				result.error = deflateText("Deflate stored block length check failed.");
				return result;
			}
			const uchar* payload = nullptr;
			if (!reader.takeBytes(static_cast<qsizetype>(length), &payload)) {
				result.error = deflateText("Deflate stored block is truncated.");
				return result;
			}
			if (out.size() + static_cast<qsizetype>(length) > cap) {
				result.error = deflateText("Deflate output is larger than expected.");
				return result;
			}
			out.append(reinterpret_cast<const char*>(payload), static_cast<qsizetype>(length));
		} else if (btype == 1) {
			const FixedTables& tables = fixedTables();
			if (!inflateHuffmanBlock(&reader, tables.literal, tables.distance, &out, cap, &result.error)) {
				return result;
			}
		} else if (btype == 2) {
			HuffmanTable literal;
			HuffmanTable distance;
			if (!readDynamicTables(&reader, &literal, &distance, &result.error)) {
				return result;
			}
			if (!inflateHuffmanBlock(&reader, literal, distance, &out, cap, &result.error)) {
				return result;
			}
		} else {
			result.error = deflateText("Deflate stream uses a reserved block type.");
			return result;
		}
	}

	result.ok = true;
	result.data = out;
	result.bytesConsumed = reader.bytesConsumed();
	return result;
}

// ---------------------------------------------------------------------------
// Bit writer and the fixed Huffman encoder.
// ---------------------------------------------------------------------------

class BitWriter {
public:
	void writeBits(quint32 value, int count)
	{
		for (int i = 0; i < count; ++i) {
			bitBuffer_ |= ((value >> i) & 1u) << bitCount_;
			if (++bitCount_ == 8) {
				out_.append(static_cast<char>(static_cast<quint8>(bitBuffer_)));
				bitBuffer_ = 0;
				bitCount_ = 0;
			}
		}
	}

	// Huffman codes are packed most significant bit first (RFC 1951 3.1.1).
	void writeCode(quint32 code, int count)
	{
		for (int i = count - 1; i >= 0; --i) {
			writeBits((code >> i) & 1u, 1);
		}
	}

	void alignToByte()
	{
		if (bitCount_ > 0) {
			out_.append(static_cast<char>(static_cast<quint8>(bitBuffer_)));
			bitBuffer_ = 0;
			bitCount_ = 0;
		}
	}

	void appendAlignedBytes(const char* bytes, qsizetype count) { out_.append(bytes, count); }

	QByteArray finish()
	{
		alignToByte();
		return out_;
	}

private:
	QByteArray out_;
	quint32 bitBuffer_ = 0;
	int bitCount_ = 0;
};

// RFC 1951 3.2.6, fixed literal/length code.
void fixedLiteralCode(int symbol, quint32* code, int* bits)
{
	if (symbol < 144) {
		*code = 0x30u + static_cast<quint32>(symbol);
		*bits = 8;
	} else if (symbol < 256) {
		*code = 0x190u + static_cast<quint32>(symbol - 144);
		*bits = 9;
	} else if (symbol < 280) {
		*code = static_cast<quint32>(symbol - 256);
		*bits = 7;
	} else {
		*code = 0xc0u + static_cast<quint32>(symbol - 280);
		*bits = 8;
	}
}

int lengthCodeIndex(qsizetype length)
{
	for (int i = 28; i >= 0; --i) {
		if (length >= static_cast<qsizetype>(kLengthBase[i])) {
			return i;
		}
	}
	return 0;
}

int distanceCodeIndex(qsizetype distance)
{
	for (int i = 29; i >= 0; --i) {
		if (distance >= static_cast<qsizetype>(kDistanceBase[i])) {
			return i;
		}
	}
	return 0;
}

struct Token {
	quint16 literal = 0;   // literal byte, or the match length when distance > 0
	quint16 length = 0;
	quint16 distance = 0;  // 0 means "literal"
};

struct MatchConfig {
	int maxChain = 0;
	qsizetype niceLength = 0;
};

MatchConfig matchConfigFor(DeflateLevel level)
{
	// Fast walks a short hash chain and stops early; Default searches further.
	if (level == DeflateLevel::Fast) {
		return MatchConfig { 8, 32 };
	}
	return MatchConfig { 128, kMaxMatch };
}

// Hash-chain LZ77 matcher over three-byte keys. Deterministic by construction:
// the chains depend only on the input bytes and the configured limits.
class MatchFinder {
public:
	MatchFinder(const quint8* data, qsizetype size, MatchConfig config)
		: data_(data)
		, size_(size)
		, config_(config)
		, head_(static_cast<size_t>(kHashSize), -1)
		, prev_(static_cast<size_t>(kWindowSize), -1)
	{
	}

	void insert(qsizetype pos)
	{
		if (pos + kMinMatch > size_) {
			return;
		}
		const qsizetype key = hashAt(pos);
		prev_[static_cast<size_t>(pos & (kWindowSize - 1))] = head_[static_cast<size_t>(key)];
		head_[static_cast<size_t>(key)] = static_cast<qint32>(pos);
	}

	bool findMatch(qsizetype pos, qsizetype limit, qsizetype* bestLength, qsizetype* bestDistance) const
	{
		if (limit < kMinMatch || pos + kMinMatch > size_) {
			return false;
		}
		qint32 candidate = head_[static_cast<size_t>(hashAt(pos))];
		int attempts = config_.maxChain;
		qsizetype best = 0;
		qsizetype bestDist = 0;
		while (candidate >= 0 && attempts-- > 0) {
			const qsizetype distance = pos - static_cast<qsizetype>(candidate);
			if (distance <= 0 || distance > kWindowSize) {
				break;
			}
			qsizetype length = 0;
			while (length < limit && data_[static_cast<qsizetype>(candidate) + length] == data_[pos + length]) {
				++length;
			}
			if (length > best) {
				best = length;
				bestDist = distance;
				if (best >= config_.niceLength) {
					break;
				}
			}
			const qint32 next = prev_[static_cast<size_t>(candidate & (kWindowSize - 1))];
			if (next >= candidate) {
				break; // stale chain entry: stop instead of looping forever
			}
			candidate = next;
		}
		if (best < kMinMatch) {
			return false;
		}
		*bestLength = best;
		*bestDistance = bestDist;
		return true;
	}

private:
	static constexpr qsizetype kHashSize = 1 << 15;

	qsizetype hashAt(qsizetype pos) const
	{
		const quint32 value = (static_cast<quint32>(data_[pos]) << 10)
			^ (static_cast<quint32>(data_[pos + 1]) << 5)
			^ static_cast<quint32>(data_[pos + 2]);
		return static_cast<qsizetype>(value & static_cast<quint32>(kHashSize - 1));
	}

	const quint8* data_ = nullptr;
	qsizetype size_ = 0;
	MatchConfig config_;
	std::vector<qint32> head_;
	std::vector<qint32> prev_;
};

void writeStoredBlock(BitWriter* writer, const char* bytes, qsizetype length, bool finalBlock)
{
	writer->writeBits(finalBlock ? 1u : 0u, 1);
	writer->writeBits(0u, 2);
	writer->alignToByte();
	const quint32 len = static_cast<quint32>(length) & 0xffffu;
	const quint32 inverse = (~len) & 0xffffu;
	char header[4];
	header[0] = static_cast<char>(len & 0xffu);
	header[1] = static_cast<char>((len >> 8) & 0xffu);
	header[2] = static_cast<char>(inverse & 0xffu);
	header[3] = static_cast<char>((inverse >> 8) & 0xffu);
	writer->appendAlignedBytes(header, 4);
	if (length > 0) {
		writer->appendAlignedBytes(bytes, length);
	}
}

void writeFixedBlock(BitWriter* writer, const std::vector<Token>& tokens, bool finalBlock)
{
	writer->writeBits(finalBlock ? 1u : 0u, 1);
	writer->writeBits(1u, 2);
	for (const Token& token : tokens) {
		quint32 code = 0;
		int bits = 0;
		if (token.distance == 0) {
			fixedLiteralCode(static_cast<int>(token.literal), &code, &bits);
			writer->writeCode(code, bits);
			continue;
		}
		const int lengthIndex = lengthCodeIndex(token.length);
		fixedLiteralCode(257 + lengthIndex, &code, &bits);
		writer->writeCode(code, bits);
		if (kLengthExtra[lengthIndex] > 0) {
			writer->writeBits(static_cast<quint32>(token.length - kLengthBase[lengthIndex]), kLengthExtra[lengthIndex]);
		}
		const int distanceIndex = distanceCodeIndex(token.distance);
		writer->writeCode(static_cast<quint32>(distanceIndex), 5);
		if (kDistanceExtra[distanceIndex] > 0) {
			writer->writeBits(static_cast<quint32>(token.distance - kDistanceBase[distanceIndex]), kDistanceExtra[distanceIndex]);
		}
	}
	quint32 endCode = 0;
	int endBits = 0;
	fixedLiteralCode(kEndOfBlockSymbol, &endCode, &endBits);
	writer->writeCode(endCode, endBits);
}

// ---------------------------------------------------------------------------
// Checksums.
// ---------------------------------------------------------------------------

std::array<quint32, 256> makeCrcTable()
{
	// CRC-32 with the reflected polynomial 0xedb88320 (ITU-T V.42, RFC 1952,
	// PKWARE .ZIP appnote).
	std::array<quint32, 256> table {};
	for (quint32 i = 0; i < 256; ++i) {
		quint32 value = i;
		for (int bit = 0; bit < 8; ++bit) {
			value = (value & 1u) ? (0xedb88320u ^ (value >> 1)) : (value >> 1);
		}
		table[i] = value;
	}
	return table;
}

const std::array<quint32, 256>& crcTable()
{
	// Function-local static initialisation is thread safe, so the table is
	// built exactly once no matter how many threads race here.
	static const std::array<quint32, 256> table = makeCrcTable();
	return table;
}

} // namespace

InflateResult inflateRaw(const QByteArray& input, qint64 expectedSize)
{
	return inflateStream(reinterpret_cast<const uchar*>(input.constData()), input.size(), expectedSize);
}

InflateResult inflateZlib(const QByteArray& input, qint64 expectedSize)
{
	InflateResult result;
	if (input.size() < 6) {
		result.error = deflateText("Zlib stream is too short.");
		return result;
	}
	// RFC 1950 2.2: CMF/FLG header.
	const auto* data = reinterpret_cast<const uchar*>(input.constData());
	const quint32 cmf = data[0];
	const quint32 flg = data[1];
	if ((cmf & 0x0fu) != 8u) {
		result.error = deflateText("Zlib stream does not use the deflate compression method.");
		return result;
	}
	if (((cmf >> 4) & 0x0fu) > 7u) {
		result.error = deflateText("Zlib stream declares an unsupported window size.");
		return result;
	}
	if (((cmf << 8) + flg) % 31u != 0u) {
		result.error = deflateText("Zlib header check bits are invalid.");
		return result;
	}
	if ((flg & 0x20u) != 0u) {
		result.error = deflateText("Zlib streams with a preset dictionary are not supported.");
		return result;
	}

	result = inflateStream(data + 2, input.size() - 2, expectedSize);
	if (!result.ok) {
		return result;
	}
	const qint64 payloadEnd = 2 + result.bytesConsumed;
	if (payloadEnd + 4 > input.size()) {
		InflateResult failure;
		failure.error = deflateText("Zlib stream is missing its Adler-32 checksum.");
		return failure;
	}
	const auto* tail = data + payloadEnd;
	const quint32 stored = (static_cast<quint32>(tail[0]) << 24)
		| (static_cast<quint32>(tail[1]) << 16)
		| (static_cast<quint32>(tail[2]) << 8)
		| static_cast<quint32>(tail[3]);
	if (stored != adler32Bytes(result.data)) {
		InflateResult failure;
		failure.error = deflateText("Zlib Adler-32 checksum does not match the decompressed data.");
		return failure;
	}
	result.bytesConsumed = payloadEnd + 4;
	return result;
}

QByteArray deflateRaw(const QByteArray& input, DeflateLevel level)
{
	BitWriter writer;
	const qsizetype size = input.size();
	const char* bytes = input.constData();

	qsizetype chunkCount = (size + kMaxStoredBlock - 1) / kMaxStoredBlock;
	if (chunkCount == 0) {
		chunkCount = 1;
	}

	if (level == DeflateLevel::Store) {
		for (qsizetype chunk = 0; chunk < chunkCount; ++chunk) {
			const qsizetype start = chunk * kMaxStoredBlock;
			const qsizetype end = qMin(size, start + kMaxStoredBlock);
			writeStoredBlock(&writer, bytes + start, end - start, chunk == chunkCount - 1);
		}
		return writer.finish();
	}

	const auto* data = reinterpret_cast<const quint8*>(bytes);
	MatchFinder finder(data, size, matchConfigFor(level));
	std::vector<Token> tokens;
	for (qsizetype chunk = 0; chunk < chunkCount; ++chunk) {
		const qsizetype start = chunk * kMaxStoredBlock;
		const qsizetype end = qMin(size, start + kMaxStoredBlock);
		const bool finalBlock = chunk == chunkCount - 1;

		tokens.clear();
		qint64 codedBits = 3; // block header
		qsizetype pos = start;
		while (pos < end) {
			const qsizetype limit = qMin(kMaxMatch, end - pos);
			qsizetype matchLength = 0;
			qsizetype matchDistance = 0;
			if (finder.findMatch(pos, limit, &matchLength, &matchDistance)) {
				Token token;
				token.length = static_cast<quint16>(matchLength);
				token.distance = static_cast<quint16>(matchDistance);
				tokens.push_back(token);
				const int lengthIndex = lengthCodeIndex(matchLength);
				const int distanceIndex = distanceCodeIndex(matchDistance);
				quint32 code = 0;
				int bits = 0;
				fixedLiteralCode(257 + lengthIndex, &code, &bits);
				codedBits += bits + kLengthExtra[lengthIndex] + 5 + kDistanceExtra[distanceIndex];
				for (qsizetype i = 0; i < matchLength; ++i) {
					finder.insert(pos + i);
				}
				pos += matchLength;
				continue;
			}
			Token token;
			token.literal = static_cast<quint16>(data[pos]);
			tokens.push_back(token);
			quint32 code = 0;
			int bits = 0;
			fixedLiteralCode(static_cast<int>(data[pos]), &code, &bits);
			codedBits += bits;
			finder.insert(pos);
			++pos;
		}
		codedBits += 7; // end-of-block symbol

		// Falling back to a stored block keeps incompressible data from growing.
		const qint64 storedBits = 3 + 7 + 32 + 8 * static_cast<qint64>(end - start);
		if (codedBits >= storedBits) {
			writeStoredBlock(&writer, bytes + start, end - start, finalBlock);
		} else {
			writeFixedBlock(&writer, tokens, finalBlock);
		}
	}
	return writer.finish();
}

quint32 crc32Bytes(const QByteArray& bytes, quint32 seed)
{
	const std::array<quint32, 256>& table = crcTable();
	quint32 crc = seed ^ 0xffffffffu;
	const auto* data = reinterpret_cast<const quint8*>(bytes.constData());
	const qsizetype size = bytes.size();
	for (qsizetype i = 0; i < size; ++i) {
		crc = table[(crc ^ data[i]) & 0xffu] ^ (crc >> 8);
	}
	return crc ^ 0xffffffffu;
}

quint32 adler32Bytes(const QByteArray& bytes, quint32 seed)
{
	// RFC 1950 9: two 16-bit sums modulo 65521, deferred reduction every 5552
	// bytes (the largest run that cannot overflow a 32-bit accumulator).
	constexpr quint32 kModulus = 65521u;
	constexpr qsizetype kChunk = 5552;
	quint32 low = seed & 0xffffu;
	quint32 high = (seed >> 16) & 0xffffu;
	const auto* data = reinterpret_cast<const quint8*>(bytes.constData());
	qsizetype remaining = bytes.size();
	qsizetype offset = 0;
	while (remaining > 0) {
		const qsizetype run = qMin(remaining, kChunk);
		for (qsizetype i = 0; i < run; ++i) {
			low += data[offset + i];
			high += low;
		}
		low %= kModulus;
		high %= kModulus;
		offset += run;
		remaining -= run;
	}
	return (high << 16) | low;
}

QString deflateLevelId(DeflateLevel level)
{
	switch (level) {
	case DeflateLevel::Store:
		return QStringLiteral("store");
	case DeflateLevel::Fast:
		return QStringLiteral("fast");
	case DeflateLevel::Default:
		return QStringLiteral("default");
	}
	return QStringLiteral("default");
}

bool deflateLevelFromId(const QString& id, DeflateLevel* out)
{
	const QString normalized = id.trimmed().toLower();
	DeflateLevel level = DeflateLevel::Default;
	if (normalized == QStringLiteral("store")) {
		level = DeflateLevel::Store;
	} else if (normalized == QStringLiteral("fast")) {
		level = DeflateLevel::Fast;
	} else if (normalized == QStringLiteral("default")) {
		level = DeflateLevel::Default;
	} else {
		return false;
	}
	if (out != nullptr) {
		*out = level;
	}
	return true;
}

} // namespace vibestudio
