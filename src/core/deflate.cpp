#include "core/deflate.h"

#include <QCoreApplication>
#include <QIODevice>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>
#include <vector>

namespace vibestudio {

namespace {

// ---------------------------------------------------------------------------
// Shared constants and tables.
//
// Every table below is transcribed from RFC 1951 (DEFLATE Compressed Data
// Format Specification version 1.3), https://www.rfc-editor.org/rfc/rfc1951.
// ---------------------------------------------------------------------------

constexpr int kMaxCodeBits = 15;                        // RFC 1951 3.2.7
constexpr int kMaxCodeLengthBits = 7;                   // RFC 1951 3.2.7
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
	BitReader(QIODevice& device, qint64 size, const std::function<bool()>& isCancelled)
		: size_(size), device_(&device), isCancelled_(isCancelled)
	{
	}

	bool readBit(quint32* out)
	{
		if (bitCount_ == 0) {
			if (pos_ >= size_) {
				return false;
			}
			const uchar* byte = nullptr;
			if (!takeBytes(1, &byte)) { return false; }
			bitBuffer_ = *byte;
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
		if (device_) {
			if (count > buffer_.size() - bufferPos_) {
				if (isCancelled_ && isCancelled_()) { return false; }
				buffer_.remove(0, bufferPos_);
				bufferPos_ = 0;
				const qint64 remaining = size_ - pos_ - buffer_.size();
				const qint64 wanted = qMin<qint64>(remaining, qMax<qint64>(65536, count - buffer_.size()));
				if (wanted > 0) { buffer_.append(device_->read(wanted)); }
				if (count > buffer_.size()) { return false; }
			}
			*out = reinterpret_cast<const uchar*>(buffer_.constData() + bufferPos_);
			bufferPos_ += count;
		} else {
			*out = data_ + pos_;
		}
		pos_ += count;
		return true;
	}

	// Number of input bytes touched so far. A partially consumed byte counts as
	// consumed, which is what a ZIP reader needs to locate a data descriptor.
	qint64 bytesConsumed() const { return pos_; }

private:
	const uchar* data_ = nullptr;
	qint64 size_ = 0;
	qint64 pos_ = 0;
	QIODevice* device_ = nullptr;
	QByteArray buffer_;
	qsizetype bufferPos_ = 0;
	std::function<bool()> isCancelled_;
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
		*error = QCoreApplication::translate("VibeStudioDeflate", "Deflate stream ended inside a dynamic block header.");
		return false;
	}
	const int literalCount = static_cast<int>(hlit) + 257;
	const int distanceCount = static_cast<int>(hdist) + 1;
	const int codeLengthCount = static_cast<int>(hclen) + 4;
	if (literalCount > kLiteralAlphabetMax) {
		*error = QCoreApplication::translate("VibeStudioDeflate", "Deflate dynamic block declares too many literal codes.");
		return false;
	}
	if (distanceCount > kDistanceAlphabetMax) {
		*error = QCoreApplication::translate("VibeStudioDeflate", "Deflate dynamic block declares too many distance codes.");
		return false;
	}

	quint8 codeLengthLengths[kCodeLengthAlphabet] = {};
	for (int i = 0; i < codeLengthCount; ++i) {
		quint32 value = 0;
		if (!reader->readBits(3, &value)) {
			*error = QCoreApplication::translate("VibeStudioDeflate", "Deflate stream ended inside the code length alphabet.");
			return false;
		}
		codeLengthLengths[kCodeLengthOrder[i]] = static_cast<quint8>(value);
	}

	HuffmanTable codeLengthTable;
	if (!buildHuffman(codeLengthLengths, kCodeLengthAlphabet, &codeLengthTable) || codeLengthTable.codeCount == 0) {
		*error = QCoreApplication::translate("VibeStudioDeflate", "Deflate code length alphabet is malformed.");
		return false;
	}

	const int totalLengths = literalCount + distanceCount;
	std::vector<quint8> lengths(static_cast<size_t>(totalLengths), 0);
	int index = 0;
	while (index < totalLengths) {
		const int symbol = decodeSymbol(reader, codeLengthTable);
		if (symbol == kDecodeTruncated) {
			*error = QCoreApplication::translate("VibeStudioDeflate", "Deflate stream ended inside the code length list.");
			return false;
		}
		if (symbol < 0 || symbol >= kCodeLengthAlphabet) {
			*error = QCoreApplication::translate("VibeStudioDeflate", "Deflate code length list contains an invalid code.");
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
				*error = QCoreApplication::translate("VibeStudioDeflate", "Deflate code length repeat has no previous length.");
				return false;
			}
			if (!reader->readBits(2, &extra)) {
				*error = QCoreApplication::translate("VibeStudioDeflate", "Deflate stream ended inside a code length repeat.");
				return false;
			}
			repeat = 3 + static_cast<int>(extra);
			value = lengths[static_cast<size_t>(index - 1)];
		} else if (symbol == 17) {
			if (!reader->readBits(3, &extra)) {
				*error = QCoreApplication::translate("VibeStudioDeflate", "Deflate stream ended inside a code length repeat.");
				return false;
			}
			repeat = 3 + static_cast<int>(extra);
		} else {
			if (!reader->readBits(7, &extra)) {
				*error = QCoreApplication::translate("VibeStudioDeflate", "Deflate stream ended inside a code length repeat.");
				return false;
			}
			repeat = 11 + static_cast<int>(extra);
		}
		if (repeat > totalLengths - index) {
			*error = QCoreApplication::translate("VibeStudioDeflate", "Deflate code length repeat runs past the end of the alphabet.");
			return false;
		}
		for (int i = 0; i < repeat; ++i) {
			lengths[static_cast<size_t>(index++)] = value;
		}
	}

	if (!buildHuffman(lengths.data(), literalCount, literal) || literal->incomplete) {
		*error = QCoreApplication::translate("VibeStudioDeflate", "Deflate literal/length code table is malformed.");
		return false;
	}
	if (lengths[static_cast<size_t>(kEndOfBlockSymbol)] == 0) {
		*error = QCoreApplication::translate("VibeStudioDeflate", "Deflate literal/length code table has no end-of-block code.");
		return false;
	}
	if (!buildHuffman(lengths.data() + literalCount, distanceCount, distance)) {
		*error = QCoreApplication::translate("VibeStudioDeflate", "Deflate distance code table is malformed.");
		return false;
	}
	// A distance table with a single code is incomplete but legal in practice;
	// anything else that is incomplete is rejected.
	if (distance->incomplete && distance->codeCount > 1) {
		*error = QCoreApplication::translate("VibeStudioDeflate", "Deflate distance code table is incomplete.");
		return false;
	}
	return true;
}

class InflateOutput {
public:
	InflateOutput(qint64 cap, const std::function<bool(QByteArrayView)>& sink,
		const std::function<bool()>& isCancelled, QString* error)
		: cap_(cap), sink_(sink), isCancelled_(isCancelled), error_(error)
	{
		pending_.reserve(65536);
	}

	qint64 size() const { return size_; }
	bool append(char byte)
	{
		if (size_ >= cap_) {
			*error_ = QCoreApplication::translate("VibeStudioDeflate", "Deflate output is larger than expected.");
			return false;
		}
		window_[static_cast<size_t>(size_ % kWindowSize)] = byte;
		++size_;
		pending_.append(byte);
		return pending_.size() < 65536 || flush();
	}
	bool append(const uchar* bytes, qsizetype length)
	{
		for (qsizetype i = 0; i < length; ++i) {
			if (!append(static_cast<char>(bytes[i]))) { return false; }
		}
		return true;
	}
	bool copy(qsizetype distance, qsizetype length)
	{
		for (qsizetype i = 0; i < length; ++i) {
			const char byte = window_[static_cast<size_t>((size_ - distance) % kWindowSize)];
			if (!append(byte)) { return false; }
		}
		return true;
	}
	bool flush()
	{
		if (isCancelled_ && isCancelled_()) {
			*error_ = QCoreApplication::translate("VibeStudioDeflate", "Decompression cancelled.");
			return false;
		}
		if (!pending_.isEmpty() && !sink_(QByteArrayView(pending_))) {
			*error_ = QCoreApplication::translate("VibeStudioDeflate", "Unable to deliver decompressed bytes.");
			return false;
		}
		pending_.clear();
		return true;
	}

private:
	qint64 cap_;
	qint64 size_ = 0;
	std::array<char, kWindowSize> window_ {};
	QByteArray pending_;
	const std::function<bool(QByteArrayView)>& sink_;
	const std::function<bool()>& isCancelled_;
	QString* error_;
};

bool inflateHuffmanBlock(BitReader* reader,
	const HuffmanTable& literal,
	const HuffmanTable& distance,
	InflateOutput* out,
	qint64 cap,
	QString* error)
{
	for (;;) {
		const int symbol = decodeSymbol(reader, literal);
		if (symbol == kDecodeTruncated) {
			*error = QCoreApplication::translate("VibeStudioDeflate", "Deflate stream ended inside a compressed block.");
			return false;
		}
		if (symbol < 0) {
			*error = QCoreApplication::translate("VibeStudioDeflate", "Deflate block contains an invalid literal/length code.");
			return false;
		}
		if (symbol == kEndOfBlockSymbol) {
			return true;
		}
		if (symbol < kEndOfBlockSymbol) {
			if (out->size() + 1 > cap) {
				*error = QCoreApplication::translate("VibeStudioDeflate", "Deflate output is larger than expected.");
				return false;
			}
			if (!out->append(static_cast<char>(static_cast<quint8>(symbol)))) { return false; }
			continue;
		}
		const int lengthIndex = symbol - 257;
		if (lengthIndex >= 29) {
			*error = QCoreApplication::translate("VibeStudioDeflate", "Deflate block uses a reserved length code.");
			return false;
		}
		quint32 lengthExtra = 0;
		if (!reader->readBits(kLengthExtra[lengthIndex], &lengthExtra)) {
			*error = QCoreApplication::translate("VibeStudioDeflate", "Deflate stream ended inside a length code.");
			return false;
		}
		const qsizetype length = static_cast<qsizetype>(kLengthBase[lengthIndex]) + static_cast<qsizetype>(lengthExtra);

		const int distanceSymbol = decodeSymbol(reader, distance);
		if (distanceSymbol == kDecodeTruncated) {
			*error = QCoreApplication::translate("VibeStudioDeflate", "Deflate stream ended inside a distance code.");
			return false;
		}
		if (distanceSymbol < 0) {
			*error = QCoreApplication::translate("VibeStudioDeflate", "Deflate block contains an invalid distance code.");
			return false;
		}
		if (distanceSymbol >= kDistanceAlphabetMax) {
			*error = QCoreApplication::translate("VibeStudioDeflate", "Deflate block uses a reserved distance code.");
			return false;
		}
		quint32 distanceExtra = 0;
		if (!reader->readBits(kDistanceExtra[distanceSymbol], &distanceExtra)) {
			*error = QCoreApplication::translate("VibeStudioDeflate", "Deflate stream ended inside a distance code.");
			return false;
		}
		const qsizetype dist = static_cast<qsizetype>(kDistanceBase[distanceSymbol]) + static_cast<qsizetype>(distanceExtra);
		if (dist > out->size()) {
			*error = QCoreApplication::translate("VibeStudioDeflate", "Deflate back reference points before the start of the output.");
			return false;
		}
		if (out->size() + length > cap) {
			*error = QCoreApplication::translate("VibeStudioDeflate", "Deflate output is larger than expected.");
			return false;
		}
		if (!out->copy(dist, length)) { return false; }
	}
}

InflateStreamResult decodeStream(BitReader& reader, qint64 size, qint64 expectedSize,
	const std::function<bool(QByteArrayView)>& sink, const std::function<bool()>& isCancelled)
{
	InflateStreamResult result;
	if (size <= 0) {
		result.error = QCoreApplication::translate("VibeStudioDeflate", "Deflate stream is empty.");
		return result;
	}
	const qint64 cap = expectedSize >= 0
		? expectedSize
		: qMin(kUnboundedOutputCeiling, static_cast<qint64>(size) * kMaxExpansionRatio + kMaxStoredBlock);
	InflateOutput out(cap, sink, isCancelled, &result.error);
	bool finalBlock = false;
	// Every iteration consumes at least three bits of a finite input, so this
	// loop always terminates.
	while (!finalBlock) {
		if (isCancelled && isCancelled()) {
			result.error = QCoreApplication::translate("VibeStudioDeflate", "Decompression cancelled.");
			return result;
		}
		quint32 bfinal = 0;
		quint32 btype = 0;
		if (!reader.readBits(1, &bfinal) || !reader.readBits(2, &btype)) {
			result.error = QCoreApplication::translate("VibeStudioDeflate", "Deflate stream ended inside a block header.");
			return result;
		}
		finalBlock = bfinal != 0;
		if (btype == 0) {
			// RFC 1951 3.2.4, stored block.
			reader.alignToByte();
			const uchar* header = nullptr;
			if (!reader.takeBytes(4, &header)) {
				result.error = QCoreApplication::translate("VibeStudioDeflate", "Deflate stored block header is truncated.");
				return result;
			}
			const quint32 length = static_cast<quint32>(header[0]) | (static_cast<quint32>(header[1]) << 8);
			const quint32 inverse = static_cast<quint32>(header[2]) | (static_cast<quint32>(header[3]) << 8);
			if (((~length) & 0xffffu) != inverse) {
				result.error = QCoreApplication::translate("VibeStudioDeflate", "Deflate stored block length check failed.");
				return result;
			}
			const uchar* payload = nullptr;
			if (!reader.takeBytes(static_cast<qsizetype>(length), &payload)) {
				result.error = QCoreApplication::translate("VibeStudioDeflate", "Deflate stored block is truncated.");
				return result;
			}
			if (out.size() + static_cast<qsizetype>(length) > cap) {
				result.error = QCoreApplication::translate("VibeStudioDeflate", "Deflate output is larger than expected.");
				return result;
			}
			if (!out.append(payload, static_cast<qsizetype>(length))) { return result; }
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
			result.error = QCoreApplication::translate("VibeStudioDeflate", "Deflate stream uses a reserved block type.");
			return result;
		}
	}

	if (!out.flush()) { return result; }
	result.ok = true;
	result.bytesWritten = out.size();
	result.bytesConsumed = reader.bytesConsumed();
	return result;
}

InflateResult inflateStream(const uchar* data, qsizetype size, qint64 expectedSize, const std::function<bool()>& isCancelled = {})
{
	BitReader reader(data, size);
	QByteArray bytes;
	const auto decoded = decodeStream(reader, size, expectedSize, [&bytes](QByteArrayView chunk) {
		bytes.append(chunk.data(), chunk.size());
		return true;
	}, isCancelled);
	InflateResult result;
	result.ok = decoded.ok;
	result.error = decoded.error;
	result.bytesConsumed = decoded.bytesConsumed;
	if (result.ok) { result.data = std::move(bytes); }
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

	// Bits written so far. The block chooser needs this to know how much
	// padding a stored block would cost at this point in the stream.
	qint64 bitPosition() const { return static_cast<qint64>(out_.size()) * 8 + bitCount_; }

	QByteArray finish()
	{
		alignToByte();
		return out_;
	}

	// Retain the partial byte between blocks. Removing whole bytes preserves
	// the alignment used by bitPosition() when choosing the next block.
	QByteArray takeBytes() { return std::exchange(out_, QByteArray()); }

private:
	QByteArray out_;
	quint32 bitBuffer_ = 0;
	int bitCount_ = 0;
};

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
	quint16 literal = 0;      // literal byte when distance == 0
	quint16 length = 0;
	quint16 distance = 0;     // 0 means "literal"
	quint8 lengthCode = 0;    // index into kLengthBase / kLengthExtra
	quint8 distanceCode = 0;  // index into kDistanceBase / kDistanceExtra
};

struct MatchConfig {
	int maxChain = 0;
	qsizetype niceLength = 0;  // stop searching once a match this long is found
	qsizetype goodLength = 0;  // shorten the chain once a match this long is held
	bool lazy = false;         // defer a match by one byte to look for a longer one
	qsizetype lazyLimit = 0;   // skip the lazy look-ahead once the held match is this long
};

MatchConfig matchConfigFor(DeflateLevel level)
{
	// Fast walks a short hash chain greedily; Default and Best walk further and
	// use lazy matching. Every field is a fixed constant so that the token
	// stream depends only on the input bytes.
	switch (level) {
	case DeflateLevel::Store:
	case DeflateLevel::Fast:
		return MatchConfig { 8, 32, 0, false, 0 };
	case DeflateLevel::Default:
		return MatchConfig { 96, 128, 16, true, 128 };
	case DeflateLevel::Best:
		return MatchConfig { 512, kMaxMatch, 24, true, kMaxMatch };
	}
	return MatchConfig { 96, 128, 16, true, 128 };
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

	// `heldLength` is the length of a match the caller is already holding (the
	// lazy-matching case). A good match in hand means the chain is walked less
	// far, which bounds the work on inputs full of medium-length repeats.
	bool findMatch(qsizetype pos, qsizetype limit, qsizetype heldLength, qsizetype* bestLength, qsizetype* bestDistance) const
	{
		if (limit < kMinMatch || pos + kMinMatch > size_) {
			return false;
		}
		qint32 candidate = head_[static_cast<size_t>(hashAt(pos))];
		int attempts = config_.maxChain;
		if (config_.goodLength > 0 && heldLength >= config_.goodLength) {
			attempts >>= 2;
			if (attempts < 1) {
				attempts = 1;
			}
		}
		qsizetype best = 0;
		qsizetype bestDist = 0;
		while (candidate >= 0 && attempts-- > 0) {
			if (best >= limit) {
				break; // nothing left to gain
			}
			const qsizetype distance = pos - static_cast<qsizetype>(candidate);
			if (distance <= 0 || distance > kWindowSize) {
				break;
			}
			// A candidate can only beat the current best if the byte just past
			// the current best matches, so check that before the full compare.
			// Both indices stay in range: candidate < pos and best < limit
			// <= size_ - pos.
			if (best == 0 || data_[static_cast<qsizetype>(candidate) + best] == data_[pos + best]) {
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

// ---------------------------------------------------------------------------
// LZ77 tokenisation.
// ---------------------------------------------------------------------------

struct BlockTokens {
	std::vector<Token> tokens;
	std::array<quint32, kLiteralAlphabetMax> literalFrequencies {};
	std::array<quint32, kDistanceAlphabetMax> distanceFrequencies {};
	qint64 extraBits = 0; // length/distance extra bits, identical for every code
};

void appendLiteral(BlockTokens* block, quint8 value)
{
	Token token;
	token.literal = value;
	block->tokens.push_back(token);
	++block->literalFrequencies[value];
}

void appendMatch(BlockTokens* block, qsizetype length, qsizetype distance)
{
	Token token;
	token.length = static_cast<quint16>(length);
	token.distance = static_cast<quint16>(distance);
	token.lengthCode = static_cast<quint8>(lengthCodeIndex(length));
	token.distanceCode = static_cast<quint8>(distanceCodeIndex(distance));
	block->tokens.push_back(token);
	++block->literalFrequencies[257 + token.lengthCode];
	++block->distanceFrequencies[token.distanceCode];
	block->extraBits += kLengthExtra[token.lengthCode] + kDistanceExtra[token.distanceCode];
}

// Turns [start, end) into literals and back references. The match finder keeps
// its history across calls, so a block may reference data emitted by an earlier
// block; that is legal because the 32 KiB window spans block boundaries
// (RFC 1951 3.2.5).
bool tokenizeChunk(MatchFinder* finder,
	const quint8* data,
	qsizetype start,
	qsizetype end,
	const MatchConfig& config,
	BlockTokens* block,
	const std::function<bool()>& isCancelled = {})
{
	block->tokens.clear();
	block->literalFrequencies.fill(0);
	block->distanceFrequencies.fill(0);
	block->extraBits = 0;
	block->literalFrequencies[kEndOfBlockSymbol] = 1;

	qsizetype pos = start;
	qsizetype heldLength = 0;
	qsizetype heldDistance = 0;
	bool held = false;
	qsizetype nextCancelCheck = start;
	while (pos < end) {
		if (pos >= nextCancelCheck) {
			if (isCancelled && isCancelled()) return false;
			nextCancelCheck = pos + 256;
		}
		const qsizetype limit = qMin(kMaxMatch, end - pos);
		qsizetype length = 0;
		qsizetype distance = 0;
		if (!config.lazy || heldLength < config.lazyLimit) {
			if (!finder->findMatch(pos, limit, heldLength, &length, &distance)) {
				length = 0;
				distance = 0;
			}
		}
		finder->insert(pos);

		if (!config.lazy) {
			if (length >= kMinMatch) {
				appendMatch(block, length, distance);
				for (qsizetype i = 1; i < length; ++i) {
					finder->insert(pos + i);
				}
				pos += length;
			} else {
				appendLiteral(block, data[pos]);
				++pos;
			}
			continue;
		}

		// Lazy matching: a match found at pos - 1 is held for one byte to see
		// whether pos starts a longer one. Emitting the extra literal usually
		// costs less than the shorter match does.
		if (held && heldLength >= kMinMatch && length <= heldLength) {
			appendMatch(block, heldLength, heldDistance);
			const qsizetype matchEnd = (pos - 1) + heldLength;
			for (qsizetype p = pos + 1; p < matchEnd; ++p) {
				finder->insert(p);
			}
			pos = matchEnd; // always > pos, so the loop makes progress
			heldLength = 0;
			heldDistance = 0;
			held = false;
			continue;
		}
		if (held) {
			appendLiteral(block, data[pos - 1]);
		}
		heldLength = length;
		heldDistance = distance;
		held = true;
		++pos;
	}
	if (held) {
		// The last position can never hold a usable match: it was searched with
		// a limit of one byte, which is below the minimum match length.
		appendLiteral(block, data[pos - 1]);
	}
	return true;
}

// ---------------------------------------------------------------------------
// Huffman code construction for the encoder.
//
// Code lengths come from the package-merge algorithm (Larmore and Hirschberg,
// "A Fast Algorithm for Optimal Length-Limited Huffman Codes", Journal of the
// ACM 37(3), 1990, https://doi.org/10.1145/79147.79150), in the formulation
// where each level of the coin-collector problem is a list built by merging the
// sorted leaves with the packages of the level below. It produces an optimal
// prefix code whose longest code respects a hard limit, which is exactly what
// DEFLATE needs: 15 bits for the literal/length and distance alphabets and 7
// bits for the code length alphabet (RFC 1951 3.2.7). Doing the limiting inside
// the construction means a skewed block is coded optimally within the limit
// instead of being rejected or repaired after the fact.
// ---------------------------------------------------------------------------

struct CodeTable {
	std::vector<quint8> lengths;
	std::vector<quint16> codes;
};

// One entry of a list in the package-merge construction. `leaves` is the number
// of leaves in the prefix of the list that ends at this entry, which is all the
// bookkeeping needed to recover the solution: the leaves chosen at a level are
// always the lightest ones, so a prefix count identifies them.
struct PackageEntry {
	quint64 weight = 0;
	qint32 leaves = 0;
};

// Fills lengths[0..count) with an optimal prefix code limited to `limit` bits.
// Symbols with a zero frequency get length zero. Returns false when the
// alphabet cannot be coded within the limit at all.
bool buildLimitedLengths(const quint32* frequencies, int count, int limit, quint8* lengths)
{
	for (int i = 0; i < count; ++i) {
		lengths[i] = 0;
	}
	if (limit < 1 || limit > kMaxCodeBits) {
		return false;
	}

	// Leaves sorted by weight, ties broken by symbol: the encoder must be
	// byte-identical from run to run.
	std::vector<std::pair<quint64, int>> leaves;
	leaves.reserve(static_cast<size_t>(count));
	for (int i = 0; i < count; ++i) {
		if (frequencies[i] != 0) {
			leaves.emplace_back(static_cast<quint64>(frequencies[i]), i);
		}
	}
	std::stable_sort(leaves.begin(), leaves.end(),
		[](const std::pair<quint64, int>& a, const std::pair<quint64, int>& b) { return a.first < b.first; });

	const int used = static_cast<int>(leaves.size());
	if (used == 0) {
		return true;
	}
	if (used == 1) {
		lengths[leaves[0].second] = 1;
		return true;
	}
	if (static_cast<qint64>(used) > (qint64(1) << limit)) {
		return false; // more symbols than the limit can distinguish
	}

	// lists[0] holds the leaves alone; each further list merges the leaves with
	// the packages formed from consecutive pairs of the list below it.
	std::vector<std::vector<PackageEntry>> lists(static_cast<size_t>(limit));
	lists[0].resize(static_cast<size_t>(used));
	for (int i = 0; i < used; ++i) {
		lists[0][static_cast<size_t>(i)] = PackageEntry { leaves[static_cast<size_t>(i)].first, i + 1 };
	}
	for (int level = 1; level < limit; ++level) {
		const std::vector<PackageEntry>& previous = lists[static_cast<size_t>(level - 1)];
		std::vector<PackageEntry>& current = lists[static_cast<size_t>(level)];
		const size_t packages = previous.size() / 2;
		current.reserve(static_cast<size_t>(used) + packages);
		size_t leafIndex = 0;
		size_t packageIndex = 0;
		qint32 leafCount = 0;
		while (leafIndex < static_cast<size_t>(used) || packageIndex < packages) {
			bool takeLeaf = leafIndex < static_cast<size_t>(used);
			quint64 packageWeight = 0;
			if (packageIndex < packages) {
				packageWeight = previous[2 * packageIndex].weight + previous[2 * packageIndex + 1].weight;
				if (takeLeaf) {
					// Leaves win ties, which keeps the construction stable.
					takeLeaf = leaves[leafIndex].first <= packageWeight;
				}
			}
			if (takeLeaf) {
				++leafCount;
				current.push_back(PackageEntry { leaves[leafIndex].first, leafCount });
				++leafIndex;
			} else {
				current.push_back(PackageEntry { packageWeight, leafCount });
				++packageIndex;
			}
		}
	}

	// The optimal solution is the cheapest 2n-2 entries of the top list. Walking
	// back down, the packages inside a prefix identify the prefix of the list
	// below that produced them.
	std::vector<qint32> leavesUsed(static_cast<size_t>(limit), 0);
	qint64 take = 2 * static_cast<qint64>(used) - 2;
	for (int level = limit - 1; level >= 0; --level) {
		const std::vector<PackageEntry>& list = lists[static_cast<size_t>(level)];
		if (take <= 0) {
			leavesUsed[static_cast<size_t>(level)] = 0;
			take = 0;
			continue;
		}
		if (take > static_cast<qint64>(list.size())) {
			return false;
		}
		const qint32 chosenLeaves = list[static_cast<size_t>(take - 1)].leaves;
		leavesUsed[static_cast<size_t>(level)] = chosenLeaves;
		take = 2 * (take - chosenLeaves);
	}

	// A leaf's code length is the number of levels whose prefix reaches it.
	for (int level = 0; level < limit; ++level) {
		const qint32 reach = leavesUsed[static_cast<size_t>(level)];
		for (qint32 rank = 0; rank < reach; ++rank) {
			++lengths[leaves[static_cast<size_t>(rank)].second];
		}
	}
	for (int i = 0; i < count; ++i) {
		if (frequencies[i] != 0 && (lengths[i] == 0 || lengths[i] > limit)) {
			return false;
		}
	}
	return true;
}

// Assigns canonical codes to a length table (RFC 1951 3.2.2) and verifies that
// the code is complete. A decoder is entitled to reject an under- or
// over-subscribed literal/length or code length table, so an incomplete table
// is treated as a construction failure rather than emitted.
bool canonicaliseCodes(CodeTable* table, int limit)
{
	const int count = static_cast<int>(table->lengths.size());
	table->codes.assign(static_cast<size_t>(count), 0);
	std::vector<int> lengthCounts(static_cast<size_t>(limit) + 1, 0);
	int used = 0;
	for (int i = 0; i < count; ++i) {
		const int length = table->lengths[static_cast<size_t>(i)];
		if (length == 0) {
			continue;
		}
		if (length > limit) {
			return false;
		}
		++lengthCounts[static_cast<size_t>(length)];
		++used;
	}
	if (used == 0) {
		return false;
	}
	quint64 kraft = 0;
	for (int length = 1; length <= limit; ++length) {
		kraft += static_cast<quint64>(lengthCounts[static_cast<size_t>(length)]) << (limit - length);
	}
	if (kraft != (quint64(1) << limit)) {
		return false;
	}

	quint32 code = 0;
	std::vector<quint32> nextCode(static_cast<size_t>(limit) + 1, 0);
	for (int length = 1; length <= limit; ++length) {
		code = (code + static_cast<quint32>(lengthCounts[static_cast<size_t>(length - 1)])) << 1;
		nextCode[static_cast<size_t>(length)] = code;
	}
	for (int i = 0; i < count; ++i) {
		const int length = table->lengths[static_cast<size_t>(i)];
		if (length != 0) {
			table->codes[static_cast<size_t>(i)] = static_cast<quint16>(nextCode[static_cast<size_t>(length)]++);
		}
	}
	return true;
}

// An alphabet with a single code cannot form a complete Huffman code, so give
// the unused low symbols a nominal frequency instead. The cost is a handful of
// bits in the table description.
void ensureTwoSymbols(quint32* frequencies, int count)
{
	int used = 0;
	for (int i = 0; i < count; ++i) {
		if (frequencies[i] != 0) {
			++used;
		}
	}
	for (int i = 0; i < count && used < 2; ++i) {
		if (frequencies[i] == 0) {
			frequencies[i] = 1;
			++used;
		}
	}
}

struct CodeLengthItem {
	quint8 symbol = 0;
	quint8 extraBits = 0;
	quint16 extraValue = 0;
};

// RFC 1951 3.2.7: the two code length sequences are sent as one run-length
// encoded list. Symbol 16 repeats the previous length 3-6 times, 17 repeats a
// zero length 3-10 times and 18 repeats a zero length 11-138 times.
std::vector<CodeLengthItem> runLengthEncodeLengths(const std::vector<quint8>& lengths)
{
	std::vector<CodeLengthItem> items;
	const int count = static_cast<int>(lengths.size());
	items.reserve(static_cast<size_t>(count));
	int index = 0;
	while (index < count) {
		const int value = lengths[static_cast<size_t>(index)];
		int run = 1;
		while (index + run < count && lengths[static_cast<size_t>(index + run)] == value) {
			++run;
		}
		index += run;
		if (value == 0) {
			while (run >= 11) {
				const int take = qMin(run, 138);
				items.push_back(CodeLengthItem { 18, 7, static_cast<quint16>(take - 11) });
				run -= take;
			}
			while (run >= 3) {
				const int take = qMin(run, 10);
				items.push_back(CodeLengthItem { 17, 3, static_cast<quint16>(take - 3) });
				run -= take;
			}
			while (run > 0) {
				items.push_back(CodeLengthItem { 0, 0, 0 });
				--run;
			}
			continue;
		}
		// Runs are maximal, so the length that symbol 16 copies always has to be
		// written out literally first.
		items.push_back(CodeLengthItem { static_cast<quint8>(value), 0, 0 });
		--run;
		while (run >= 3) {
			const int take = qMin(run, 6);
			items.push_back(CodeLengthItem { 16, 2, static_cast<quint16>(take - 3) });
			run -= take;
		}
		while (run > 0) {
			items.push_back(CodeLengthItem { static_cast<quint8>(value), 0, 0 });
			--run;
		}
	}
	return items;
}

struct DynamicTrees {
	bool valid = false;
	CodeTable literal;
	CodeTable distance;
	CodeTable codeLength;
	std::vector<CodeLengthItem> items;
	int hlit = 257;
	int hdist = 1;
	int hclen = 4;
	qint64 headerBits = 0; // block header plus the whole table description
};

DynamicTrees buildDynamicTrees(const std::array<quint32, kLiteralAlphabetMax>& literalFrequencies,
	const std::array<quint32, kDistanceAlphabetMax>& distanceFrequencies)
{
	DynamicTrees trees;
	std::array<quint32, kLiteralAlphabetMax> literalCounts = literalFrequencies;
	std::array<quint32, kDistanceAlphabetMax> distanceCounts = distanceFrequencies;
	ensureTwoSymbols(literalCounts.data(), kLiteralAlphabetMax);
	ensureTwoSymbols(distanceCounts.data(), kDistanceAlphabetMax);

	trees.literal.lengths.assign(kLiteralAlphabetMax, 0);
	trees.distance.lengths.assign(kDistanceAlphabetMax, 0);
	if (!buildLimitedLengths(literalCounts.data(), kLiteralAlphabetMax, kMaxCodeBits, trees.literal.lengths.data())) {
		return trees;
	}
	if (!buildLimitedLengths(distanceCounts.data(), kDistanceAlphabetMax, kMaxCodeBits, trees.distance.lengths.data())) {
		return trees;
	}
	if (!canonicaliseCodes(&trees.literal, kMaxCodeBits) || !canonicaliseCodes(&trees.distance, kMaxCodeBits)) {
		return trees;
	}

	// HLIT counts at least 257 literal/length codes and HDIST at least one
	// distance code (RFC 1951 3.2.7).
	trees.hlit = 257;
	for (int i = kLiteralAlphabetMax - 1; i >= 257; --i) {
		if (trees.literal.lengths[static_cast<size_t>(i)] != 0) {
			trees.hlit = i + 1;
			break;
		}
	}
	trees.hdist = 1;
	for (int i = kDistanceAlphabetMax - 1; i >= 0; --i) {
		if (trees.distance.lengths[static_cast<size_t>(i)] != 0) {
			trees.hdist = i + 1;
			break;
		}
	}

	std::vector<quint8> combined;
	combined.reserve(static_cast<size_t>(trees.hlit + trees.hdist));
	combined.insert(combined.end(), trees.literal.lengths.begin(), trees.literal.lengths.begin() + trees.hlit);
	combined.insert(combined.end(), trees.distance.lengths.begin(), trees.distance.lengths.begin() + trees.hdist);
	trees.items = runLengthEncodeLengths(combined);

	std::array<quint32, kCodeLengthAlphabet> codeLengthCounts {};
	codeLengthCounts.fill(0);
	for (const CodeLengthItem& item : trees.items) {
		++codeLengthCounts[item.symbol];
	}
	ensureTwoSymbols(codeLengthCounts.data(), kCodeLengthAlphabet);
	trees.codeLength.lengths.assign(kCodeLengthAlphabet, 0);
	if (!buildLimitedLengths(codeLengthCounts.data(), kCodeLengthAlphabet, kMaxCodeLengthBits, trees.codeLength.lengths.data())) {
		return trees;
	}
	if (!canonicaliseCodes(&trees.codeLength, kMaxCodeLengthBits)) {
		return trees;
	}

	trees.hclen = kCodeLengthAlphabet;
	while (trees.hclen > 4 && trees.codeLength.lengths[kCodeLengthOrder[trees.hclen - 1]] == 0) {
		--trees.hclen;
	}

	qint64 bits = 3 + 5 + 5 + 4 + 3 * static_cast<qint64>(trees.hclen);
	for (const CodeLengthItem& item : trees.items) {
		bits += trees.codeLength.lengths[item.symbol] + item.extraBits;
	}
	trees.headerBits = bits;
	trees.valid = true;
	return trees;
}

// RFC 1951 3.2.6, the fixed code as an ordinary canonical table. The literal
// alphabet is defined over 288 symbols and the distance alphabet over 32, even
// though the last few of each are never used.
struct FixedEncoderTables {
	CodeTable literal;
	CodeTable distance;
};

FixedEncoderTables makeFixedEncoderTables()
{
	FixedEncoderTables tables;
	tables.literal.lengths.assign(288, 8);
	for (int i = 144; i < 256; ++i) {
		tables.literal.lengths[static_cast<size_t>(i)] = 9;
	}
	for (int i = 256; i < 280; ++i) {
		tables.literal.lengths[static_cast<size_t>(i)] = 7;
	}
	tables.distance.lengths.assign(32, 5);
	canonicaliseCodes(&tables.literal, kMaxCodeBits);
	canonicaliseCodes(&tables.distance, kMaxCodeBits);
	return tables;
}

const FixedEncoderTables& fixedEncoderTables()
{
	static const FixedEncoderTables tables = makeFixedEncoderTables();
	return tables;
}

// Bits the token stream costs under a given pair of code tables, including the
// end-of-block symbol (which is counted in the literal frequencies).
qint64 tokenBits(const BlockTokens& block, const CodeTable& literal, const CodeTable& distance)
{
	qint64 bits = block.extraBits;
	for (int i = 0; i < kLiteralAlphabetMax; ++i) {
		const quint32 frequency = block.literalFrequencies[static_cast<size_t>(i)];
		if (frequency != 0) {
			bits += static_cast<qint64>(frequency) * literal.lengths[static_cast<size_t>(i)];
		}
	}
	for (int i = 0; i < kDistanceAlphabetMax; ++i) {
		const quint32 frequency = block.distanceFrequencies[static_cast<size_t>(i)];
		if (frequency != 0) {
			bits += static_cast<qint64>(frequency) * distance.lengths[static_cast<size_t>(i)];
		}
	}
	return bits;
}

void writeTokens(BitWriter* writer, const BlockTokens& block, const CodeTable& literal, const CodeTable& distance)
{
	for (const Token& token : block.tokens) {
		if (token.distance == 0) {
			writer->writeCode(literal.codes[token.literal], literal.lengths[token.literal]);
			continue;
		}
		const int lengthCode = token.lengthCode;
		const size_t lengthSymbol = static_cast<size_t>(257 + lengthCode);
		writer->writeCode(literal.codes[lengthSymbol], literal.lengths[lengthSymbol]);
		if (kLengthExtra[lengthCode] > 0) {
			writer->writeBits(static_cast<quint32>(token.length - kLengthBase[lengthCode]), kLengthExtra[lengthCode]);
		}
		const int distanceCode = token.distanceCode;
		writer->writeCode(distance.codes[static_cast<size_t>(distanceCode)], distance.lengths[static_cast<size_t>(distanceCode)]);
		if (kDistanceExtra[distanceCode] > 0) {
			writer->writeBits(static_cast<quint32>(token.distance - kDistanceBase[distanceCode]), kDistanceExtra[distanceCode]);
		}
	}
	writer->writeCode(literal.codes[kEndOfBlockSymbol], literal.lengths[kEndOfBlockSymbol]);
}

void writeFixedBlock(BitWriter* writer, const BlockTokens& block, const FixedEncoderTables& fixed, bool finalBlock)
{
	writer->writeBits(finalBlock ? 1u : 0u, 1);
	writer->writeBits(1u, 2);
	writeTokens(writer, block, fixed.literal, fixed.distance);
}

void writeDynamicBlock(BitWriter* writer, const BlockTokens& block, const DynamicTrees& trees, bool finalBlock)
{
	// RFC 1951 3.2.7.
	writer->writeBits(finalBlock ? 1u : 0u, 1);
	writer->writeBits(2u, 2);
	writer->writeBits(static_cast<quint32>(trees.hlit - 257), 5);
	writer->writeBits(static_cast<quint32>(trees.hdist - 1), 5);
	writer->writeBits(static_cast<quint32>(trees.hclen - 4), 4);
	for (int i = 0; i < trees.hclen; ++i) {
		writer->writeBits(trees.codeLength.lengths[kCodeLengthOrder[i]], 3);
	}
	for (const CodeLengthItem& item : trees.items) {
		writer->writeCode(trees.codeLength.codes[item.symbol], trees.codeLength.lengths[item.symbol]);
		if (item.extraBits > 0) {
			writer->writeBits(item.extraValue, item.extraBits);
		}
	}
	writeTokens(writer, block, trees.literal, trees.distance);
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

InflateStreamResult inflateRawToSink(QIODevice& input, qint64 inputBytes, qint64 expectedSize,
	const std::function<bool(QByteArrayView)>& sink, const std::function<bool()>& isCancelled)
{
	InflateStreamResult result;
	if (inputBytes < 0 || expectedSize < 0 || !sink || !input.isReadable()) {
		result.error = QCoreApplication::translate("VibeStudioDeflate", "Invalid streaming decompression request.");
		return result;
	}
	BitReader reader(input, inputBytes, isCancelled);
	result = decodeStream(reader, inputBytes, expectedSize, sink, isCancelled);
	if (!result.ok && isCancelled && isCancelled()) {
		result.cancelled = true;
		result.error = QCoreApplication::translate("VibeStudioDeflate", "Decompression cancelled.");
	}
	return result;
}

InflateResult inflateZlib(const QByteArray& input, qint64 expectedSize, const std::function<bool()>& isCancelled)
{
	InflateResult result;
	if (input.size() < 6) {
		result.error = QCoreApplication::translate("VibeStudioDeflate", "Zlib stream is too short.");
		return result;
	}
	// RFC 1950 2.2: CMF/FLG header.
	const auto* data = reinterpret_cast<const uchar*>(input.constData());
	const quint32 cmf = data[0];
	const quint32 flg = data[1];
	if ((cmf & 0x0fu) != 8u) {
		result.error = QCoreApplication::translate("VibeStudioDeflate", "Zlib stream does not use the deflate compression method.");
		return result;
	}
	if (((cmf >> 4) & 0x0fu) > 7u) {
		result.error = QCoreApplication::translate("VibeStudioDeflate", "Zlib stream declares an unsupported window size.");
		return result;
	}
	if (((cmf << 8) + flg) % 31u != 0u) {
		result.error = QCoreApplication::translate("VibeStudioDeflate", "Zlib header check bits are invalid.");
		return result;
	}
	if ((flg & 0x20u) != 0u) {
		result.error = QCoreApplication::translate("VibeStudioDeflate", "Zlib streams with a preset dictionary are not supported.");
		return result;
	}

	result = inflateStream(data + 2, input.size() - 2, expectedSize, isCancelled);
	if (!result.ok) {
		return result;
	}
	const qint64 payloadEnd = 2 + result.bytesConsumed;
	if (payloadEnd + 4 > input.size()) {
		InflateResult failure;
		failure.error = QCoreApplication::translate("VibeStudioDeflate", "Zlib stream is missing its Adler-32 checksum.");
		return failure;
	}
	const auto* tail = data + payloadEnd;
	const quint32 stored = (static_cast<quint32>(tail[0]) << 24)
		| (static_cast<quint32>(tail[1]) << 16)
		| (static_cast<quint32>(tail[2]) << 8)
		| static_cast<quint32>(tail[3]);
	if (stored != adler32Bytes(result.data)) {
		InflateResult failure;
		failure.error = QCoreApplication::translate("VibeStudioDeflate", "Zlib Adler-32 checksum does not match the decompressed data.");
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
	const MatchConfig config = matchConfigFor(level);
	MatchFinder finder(data, size, config);
	const FixedEncoderTables& fixed = fixedEncoderTables();
	BlockTokens block;
	for (qsizetype chunk = 0; chunk < chunkCount; ++chunk) {
		const qsizetype start = chunk * kMaxStoredBlock;
		const qsizetype end = qMin(size, start + kMaxStoredBlock);
		const bool finalBlock = chunk == chunkCount - 1;

		tokenizeChunk(&finder, data, start, end, config, &block);

		// Measure all three block types and keep the smallest. A stored block
		// has to pad to a byte boundary first, so its cost depends on where the
		// stream currently stands.
		const qint64 padding = (8 - ((writer.bitPosition() + 3) % 8)) % 8;
		const qint64 storedBits = 3 + padding + 32 + 8 * static_cast<qint64>(end - start);
		const qint64 fixedBits = 3 + tokenBits(block, fixed.literal, fixed.distance);
		const DynamicTrees trees = buildDynamicTrees(block.literalFrequencies, block.distanceFrequencies);
		const qint64 dynamicBits = trees.valid
			? trees.headerBits + tokenBits(block, trees.literal, trees.distance)
			: std::numeric_limits<qint64>::max();

		// Ties go to the cheaper-to-decode block, and stored wins outright, so a
		// block is never larger than storing its bytes would be.
		if (storedBits <= fixedBits && storedBits <= dynamicBits) {
			writeStoredBlock(&writer, bytes + start, end - start, finalBlock);
		} else if (dynamicBits < fixedBits) {
			writeDynamicBlock(&writer, block, trees, finalBlock);
		} else {
			writeFixedBlock(&writer, block, fixed, finalBlock);
		}
	}
	return writer.finish();
}

struct DeflateStreamEncoder::State {
	qint64 expected = 0;
	DeflateLevel level;
	std::function<bool(QByteArrayView)> sink;
	std::function<bool()> isCancelled;
	DeflateStreamResult status;
	QByteArray history;
	QByteArray pending;
	BitWriter writer;
	bool finished = false;
	bool wroteBlock = false;

	bool fail(const QString& error)
	{
		status.ok = false;
		if (status.error.isEmpty()) status.error = error;
		return false;
	}

	bool checkCancelled()
	{
		if (!status.error.isEmpty()) return true;
		if (isCancelled && isCancelled()) {
			status.cancelled = true;
			fail(QCoreApplication::translate("VibeStudioDeflate", "Compression cancelled."));
			return true;
		}
		return false;
	}

	bool drain()
	{
		if (checkCancelled()) return false;
		const QByteArray bytes = writer.takeBytes();
		if (bytes.isEmpty()) return true;
		if (bytes.size() > std::numeric_limits<qint64>::max() - status.bytesWritten)
			return fail(QCoreApplication::translate("VibeStudioDeflate", "Compressed stream is too large."));
		if (!sink(bytes))
			return fail(QCoreApplication::translate("VibeStudioDeflate", "The compressed data consumer stopped the stream."));
		status.bytesWritten += bytes.size();
		return !checkCancelled();
	}

	bool writeBlock()
	{
		if (checkCancelled()) return false;
		const bool finalBlock = status.bytesConsumed == expected;
		if (level == DeflateLevel::Store) {
			writeStoredBlock(&writer, pending.constData(), pending.size(), finalBlock);
		} else {
			// Rebase the dictionary for each block. Every offset fits in the
			// matcher's bounded integer range even for multi-gigabyte streams.
			// RFC 1951 permits references to the preceding 32 KiB across blocks.
			const qsizetype start = history.size();
			QByteArray window = history;
			window.append(pending);
			const auto* data = reinterpret_cast<const quint8*>(window.constData());
			const MatchConfig config = matchConfigFor(level);
			MatchFinder finder(data, window.size(), config);
			for (qsizetype pos = 0; pos < start; ++pos) {
				if (pos % 256 == 0 && checkCancelled()) return false;
				finder.insert(pos);
			}
			BlockTokens block;
			if (!tokenizeChunk(&finder, data, start, window.size(), config, &block,
				[this]() { return checkCancelled(); })) return false;
			if (checkCancelled()) return false;
			const auto& fixed = fixedEncoderTables();
			const qint64 padding = (8 - ((writer.bitPosition() + 3) % 8)) % 8;
			const qint64 storedBits = 3 + padding + 32 + 8 * static_cast<qint64>(pending.size());
			const qint64 fixedBits = 3 + tokenBits(block, fixed.literal, fixed.distance);
			const DynamicTrees trees = buildDynamicTrees(block.literalFrequencies, block.distanceFrequencies);
			const qint64 dynamicBits = trees.valid
				? trees.headerBits + tokenBits(block, trees.literal, trees.distance)
				: std::numeric_limits<qint64>::max();
			if (storedBits <= fixedBits && storedBits <= dynamicBits)
				writeStoredBlock(&writer, pending.constData(), pending.size(), finalBlock);
			else if (dynamicBits < fixedBits)
				writeDynamicBlock(&writer, block, trees, finalBlock);
			else
				writeFixedBlock(&writer, block, fixed, finalBlock);
			history = window.right(kWindowSize);
		}
		wroteBlock = true;
		pending.clear();
		return drain();
	}
};

DeflateStreamEncoder::DeflateStreamEncoder(qint64 inputBytes, DeflateLevel level,
	std::function<bool(QByteArrayView)> sink, std::function<bool()> isCancelled)
	: m_state(std::make_unique<State>())
{
	m_state->expected = inputBytes;
	m_state->level = level;
	m_state->sink = std::move(sink);
	m_state->isCancelled = std::move(isCancelled);
	if (inputBytes < 0 || !m_state->sink)
		m_state->fail(QCoreApplication::translate("VibeStudioDeflate", "Invalid streaming compression request."));
}

DeflateStreamEncoder::~DeflateStreamEncoder() = default;

bool DeflateStreamEncoder::append(QByteArrayView bytes)
{
	auto& state = *m_state;
	if (state.checkCancelled()) return false;
	if (state.finished)
		return state.fail(QCoreApplication::translate("VibeStudioDeflate", "The compressed stream is already finished."));
	if (bytes.size() > state.expected - state.status.bytesConsumed)
		return state.fail(QCoreApplication::translate("VibeStudioDeflate", "Compression input exceeds its declared size."));
	while (!bytes.isEmpty()) {
		if (state.checkCancelled()) return false;
		const qsizetype size = qMin(bytes.size(), kMaxStoredBlock - state.pending.size());
		state.pending.append(bytes.data(), size);
		state.status.bytesConsumed += size;
		bytes = bytes.sliced(size);
		if (state.pending.size() == kMaxStoredBlock && !state.writeBlock()) return false;
	}
	return true;
}

DeflateStreamResult DeflateStreamEncoder::finish()
{
	auto& state = *m_state;
	if (state.finished || state.checkCancelled()) return state.status;
	if (state.status.bytesConsumed != state.expected) {
		state.fail(QCoreApplication::translate("VibeStudioDeflate", "Compression input ended before its declared size."));
		return state.status;
	}
	if ((!state.pending.isEmpty() || !state.wroteBlock) && !state.writeBlock()) return state.status;
	state.writer.alignToByte();
	if (!state.drain()) return state.status;
	state.finished = true;
	state.status.ok = true;
	return state.status;
}

DeflateStreamResult DeflateStreamEncoder::result() const { return m_state->status; }

quint32 crc32Bytes(const QByteArray& bytes, quint32 seed)
{
	return crc32View(bytes, seed);
}

quint32 crc32View(QByteArrayView bytes, quint32 seed)
{
	const std::array<quint32, 256>& table = crcTable();
	quint32 crc = seed ^ 0xffffffffu;
	const auto* data = reinterpret_cast<const quint8*>(bytes.data());
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
	case DeflateLevel::Best:
		return QStringLiteral("best");
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
	} else if (normalized == QStringLiteral("best")) {
		level = DeflateLevel::Best;
	} else {
		return false;
	}
	if (out != nullptr) {
		*out = level;
	}
	return true;
}

} // namespace vibestudio
