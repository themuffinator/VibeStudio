#pragma once

// DEFLATE codec.
//
// Implemented from the public IETF specifications rather than adapted from an
// existing library, so VibeStudio keeps a dependency-free, deterministic,
// fixture-testable path for ZIP/PK3 content:
// - RFC 1951, DEFLATE Compressed Data Format Specification version 1.3
// - RFC 1950, ZLIB Compressed Data Format Specification version 3.3
// - CRC-32 as specified by ITU-T V.42 / used by RFC 1952 and the ZIP appnote
//   (PKWARE .ZIP File Format Specification).
//
// The decoder is bounds-checked throughout and never trusts a length or a
// distance taken from the stream. It is written to be safe on hostile input:
// every read is range-checked, output growth is capped, and malformed streams
// fail with an error instead of aborting.

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>

class QIODevice;

namespace vibestudio {

struct InflateResult {
	bool ok = false;
	QByteArray data;
	qint64 bytesConsumed = 0;
	QString error;
};

// Raw DEFLATE stream (ZIP method 8). `expectedSize` is a hint used to reserve
// output and, when non-negative, as a hard cap that a hostile stream cannot
// exceed.
InflateResult inflateRaw(const QByteArray& input, qint64 expectedSize = -1);

struct InflateStreamResult {
	bool ok = false;
	bool cancelled = false;
	qint64 bytesConsumed = 0;
	qint64 bytesWritten = 0;
	QString error;
};

// Reads at most inputBytes from the device's current position. The decoder
// keeps only its history window and bounded input/output buffers. A sink must
// consume the view before returning; false aborts the operation. expectedSize
// is a required nonnegative output cap, not an allocation request.
InflateStreamResult inflateRawToSink(QIODevice& input, qint64 inputBytes, qint64 expectedSize,
	const std::function<bool(QByteArrayView)>& sink, const std::function<bool()>& isCancelled = {});

// zlib-wrapped DEFLATE (RFC 1950): 2-byte header, raw deflate, Adler-32 tail.
InflateResult inflateZlib(const QByteArray& input, qint64 expectedSize = -1, const std::function<bool()>& isCancelled = {});

enum class DeflateLevel {
	Store,        // stored (uncompressed) blocks only
	Fast,         // greedy matching over a short hash chain
	Default,      // lazy matching over a longer hash chain
	Best,         // lazy matching over the longest hash chain
};

// Produces a raw DEFLATE stream. Output is deterministic for a given input and
// level, which package writers rely on for reproducible archives.
//
// Every level except Store chooses per block between a stored block, a fixed
// Huffman block and a dynamic Huffman block (RFC 1951 3.2.7) by measuring the
// encoded size of all three and keeping the smallest, so a block is never
// larger than simply storing its bytes would be. The levels differ only in how
// hard the LZ77 match search works, which is what makes their cost/ratio
// trade-off; they all emit dynamic blocks when that is the cheapest option.
QByteArray deflateRaw(const QByteArray& input, DeflateLevel level = DeflateLevel::Default);

struct DeflateStreamResult {
	bool ok = false;
	bool cancelled = false;
	qint64 bytesConsumed = 0;
	qint64 bytesWritten = 0;
	QString error;
};

// Push encoder with one 65535-byte block and a 32 KiB history window. Input
// partitioning does not affect the output. The sink consumes each view before
// returning; false aborts permanently. finish() requires exactly inputBytes,
// flushes the last partial byte and is idempotent. A failed encoder cannot resume.
class DeflateStreamEncoder {
public:
	DeflateStreamEncoder(qint64 inputBytes, DeflateLevel level,
		std::function<bool(QByteArrayView)> sink, std::function<bool()> isCancelled = {});
	~DeflateStreamEncoder();
	DeflateStreamEncoder(const DeflateStreamEncoder&) = delete;
	DeflateStreamEncoder& operator=(const DeflateStreamEncoder&) = delete;
	bool append(QByteArrayView bytes);
	DeflateStreamResult finish();
	DeflateStreamResult result() const;

private:
	struct State;
	std::unique_ptr<State> m_state;
};

quint32 crc32View(QByteArrayView bytes, quint32 seed = 0);
quint32 crc32Bytes(const QByteArray& bytes, quint32 seed = 0);
quint32 adler32Bytes(const QByteArray& bytes, quint32 seed = 1);

QString deflateLevelId(DeflateLevel level);
bool deflateLevelFromId(const QString& id, DeflateLevel* out = nullptr);

} // namespace vibestudio
