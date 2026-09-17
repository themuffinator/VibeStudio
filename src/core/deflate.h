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
#include <QString>

#include <cstdint>

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

// zlib-wrapped DEFLATE (RFC 1950): 2-byte header, raw deflate, Adler-32 tail.
InflateResult inflateZlib(const QByteArray& input, qint64 expectedSize = -1);

enum class DeflateLevel {
	Store,        // stored (uncompressed) blocks only
	Fast,         // fixed Huffman with a small match window
	Default,      // fixed Huffman with a larger match window
};

// Produces a raw DEFLATE stream. Output is deterministic for a given input and
// level, which package writers rely on for reproducible archives.
QByteArray deflateRaw(const QByteArray& input, DeflateLevel level = DeflateLevel::Default);

quint32 crc32Bytes(const QByteArray& bytes, quint32 seed = 0);
quint32 adler32Bytes(const QByteArray& bytes, quint32 seed = 1);

QString deflateLevelId(DeflateLevel level);
bool deflateLevelFromId(const QString& id, DeflateLevel* out = nullptr);

} // namespace vibestudio
