#pragma once
#include "tests/doom_preview_test_helpers.h"

namespace vibestudio::tests::doomMirror {
// Generated two rooms with a shared wall, plus an unrelated island. Independent
// binary encoding makes endpoint/side/argument preservation checks meaningful.
inline QByteArray fixture(bool hexen = false) {
	namespace d = doom;
	QByteArray vertices, lines, sides, sectors, things;
	const QVector<QPoint> points{{0, 0}, {0, 128}, {128, 128}, {128, 0}, {256, 128}, {256, 0}, {512, 0}, {512, 64}, {608, 64}, {608, 0}};
	for (auto p : points) {
		QByteArray v(4, '\0');
		d::put16(v, 0, p.x());
		d::put16(v, 2, p.y());
		vertices += v;
	}
	const QVector<QPair<int, int>> edges{{0, 1}, {1, 2}, {2, 3}, {3, 0}, {2, 4}, {4, 5}, {5, 3}, {6, 7}, {7, 8}, {8, 9}, {9, 6}};
	for (int i = 0; i < edges.size(); ++i) {
		QByteArray line(hexen ? 16 : 14, '\0');
		d::put16(line, 0, edges[i].first);
		d::put16(line, 2, edges[i].second);
		d::put16(line, 4, i == 2 ? 4 | 16 : 1 | 8);
		if (hexen) {
			line[6] = char(80);
			for (int a = 0; a < 5; ++a) {
				line[7 + a] = char(11 + a + i);
			}
		} else {
			d::put16(line, 6, 31);
			d::put16(line, 8, 17 + i);
		}
		d::put16(line, hexen ? 12 : 10, i);
		d::put16(line, hexen ? 14 : 12, i == 2 ? 11 : -1);
		lines += line;
	}
	for (int i = 0; i < 12; ++i) {
		QByteArray side(30, '\0');
		d::put16(side, 0, 3 + i);
		d::put16(side, 2, -5 - i);
		d::name(side, 4, "STONE");
		d::name(side, 12, "STONE");
		d::name(side, 20, i == 2 || i == 11 ? "-" : "STONE");
		d::put16(side, 28, i == 11 ? 1 : i < 4 ? 0 : i < 7 ? 1 : 2);
		sides += side;
	}
	for (int i = 0; i < 3; ++i) {
		QByteArray sector(26, '\0');
		d::put16(sector, 0, i * 16);
		d::put16(sector, 2, 128 - i * 16);
		d::name(sector, 4, "STONE");
		d::name(sector, 12, "CEIL");
		d::put16(sector, 20, 176 + i * 16);
		d::put16(sector, 22, i);
		d::put16(sector, 24, 7 + i);
		sectors += sector;
	}
	for (int i = 0; i < 2; ++i) {
		QByteArray thing(hexen ? 20 : 10, '\0');
		const int p = hexen ? 2 : 0;
		d::put16(thing, p, i ? 550 : 40);
		d::put16(thing, p + 2, i ? 32 : 24);
		d::put16(thing, hexen ? 8 : 4, i ? 90 : 30);
		d::put16(thing, hexen ? 10 : 6, i ? 2 : 1);
		d::put16(thing, hexen ? 12 : 8, hexen ? 7 | 0x700 : 7);
		if (hexen) {
			d::put16(thing, 0, 42 + i);
			d::put16(thing, 6, 8 + i);
			thing[14] = 80;
			for (int a = 0; a < 5; ++a) {
				thing[15 + a] = char(21 + a);
			}
		}
		things += thing;
	}
	QVector<d::Lump> map{{"MAP01", {}},
						 {"THINGS", things},
						 {"LINEDEFS", lines},
						 {"SIDEDEFS", sides},
						 {"VERTEXES", vertices},
						 {"SEGS", QByteArray(12, '\0')},
						 {"SSECTORS", QByteArray(4, '\0')},
						 {"NODES", QByteArray(28, '\0')},
						 {"SECTORS", sectors},
						 {"REJECT", QByteArray(2, '\1')},
						 {"BLOCKMAP", QByteArray(8, '\0')}};
	if (hexen) {
		map << d::Lump{"BEHAVIOR", QByteArray::fromHex("41435300080000000000000000000000")} << d::Lump{"SCRIPTS", "// authored fixture\n"};
	}
	auto other = map;
	other[0].name = "MAP02";
	return d::wad(map + d::assets() + QVector<d::Lump>{{"USERDATA", "first"}, {"USERDATA", "second"}} + other);
}
inline QByteArray firstLump(const QByteArray& wad, const QByteArray& name) {
	for (const auto& lump : doom::lumps(wad)) {
		if (lump.name == name) {
			return lump.bytes;
		}
	}
	return {};
}
} // namespace vibestudio::tests::doomMirror
