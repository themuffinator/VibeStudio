#pragma once
#include "core/deflate.h"
#include "tests/level_doom_mirror_test_helpers.h"

namespace vibestudio::tests::doomNodes {
inline QByteArray segs() {
	QByteArray result;
	const int edges[][4] = {{0, 1, 0, 0}, {1, 2, 1, 0}, {2, 3, 2, 0}, {3, 0, 3, 0}, {3, 2, 2, 1}, {2, 4, 4, 0},
							{4, 5, 5, 0}, {5, 3, 6, 0}, {6, 7, 7, 0}, {7, 8, 8, 0}, {8, 9, 9, 0}, {9, 6, 10, 0}};
	for (const auto& edge : edges) {
		QByteArray record(12, '\0');
		doom::put16(record, 0, edge[0]);
		doom::put16(record, 2, edge[1]);
		doom::put16(record, 6, edge[2]);
		doom::put16(record, 8, edge[3]);
		result += record;
	}
	return result;
}
inline QByteArray subs() {
	QByteArray result(12, '\0');
	for (int i = 0; i < 3; ++i) {
		doom::put16(result, 4 * i, 4);
		doom::put16(result, 4 * i + 2, 4 * i);
	}
	return result;
}
inline QByteArray nodes(int version = 0) {
	const int stride = version == 0 ? 28 : version == 3 ? 40 : 32;
	QByteArray result(2 * stride, '\0');
	if (!version) {
		doom::put16(result, 24, 0x8000);
		doom::put16(result, 26, 0x8001);
		doom::put16(result, 52, 0);
		doom::put16(result, 54, 0x8002);
	} else {
		doom::put32(result, stride - 8, int(0x80000000u));
		doom::put32(result, stride - 4, int(0x80000001u));
		doom::put32(result, 2 * stride - 8, 0);
		doom::put32(result, 2 * stride - 4, int(0x80000002u));
	}
	return result;
}
inline QByteArray extended(const QByteArray& magic) {
	QByteArray raw(8, '\0');
	doom::put32(raw, 0, 10);
	const auto word = [&](int value) {
		QByteArray b(4, '\0');
		doom::put32(b, 0, value);
		raw += b;
	};
	word(3);
	word(4);
	word(4);
	word(4);
	word(12);
	const bool gl = magic.mid(1, 2) == "GL";
	const int version = magic[3] == '3' ? 3 : magic[3] == '2' ? 2 : 1;
	const auto classic = segs();
	for (int i = 0; i < 12; ++i) {
		QByteArray seg(gl && version >= 2 ? 13 : 11, '\0');
		doom::put32(seg, 0, qFromLittleEndian<quint16>(classic.data() + i * 12));
		doom::put32(seg, 4, gl ? -1 : qFromLittleEndian<quint16>(classic.data() + i * 12 + 2));
		const auto line = qFromLittleEndian<quint16>(classic.data() + i * 12 + 6);
		if (seg.size() == 13) {
			doom::put32(seg, 8, line);
		} else {
			doom::put16(seg, 8, line);
		}
		seg[seg.size() - 1] = classic[i * 12 + 8];
		raw += seg;
	}
	word(2);
	raw += nodes(version);
	if (magic[0] == 'X') {
		return magic + raw;
	}
	QByteArray sum(4, '\0');
	qToBigEndian(adler32Bytes(raw), sum.data());
	return magic + QByteArray::fromHex("789c") + deflateRaw(raw) + sum;
}
inline QByteArray fixture(bool hexen = false) {
	auto lumps = doom::lumps(doomMirror::fixture(hexen));
	for (auto& lump : lumps) {
		if (lump.name == "SEGS") {
			lump.bytes = segs();
		}
		if (lump.name == "SSECTORS") {
			lump.bytes = subs();
		}
		if (lump.name == "NODES") {
			lump.bytes = nodes();
		}
		if (lump.name == "BLOCKMAP") {
			lump.bytes = QByteArray(36, '\0');
			doom::put16(lump.bytes, 4, 1);
			doom::put16(lump.bytes, 6, 1);
			doom::put16(lump.bytes, 8, 5);
			for (int i = 0; i < 11; ++i) {
				doom::put16(lump.bytes, 12 + 2 * i, i);
			}
			doom::put16(lump.bytes, 34, -1);
		}
	}
	return doom::wad(lumps);
}
} // namespace vibestudio::tests::doomNodes
