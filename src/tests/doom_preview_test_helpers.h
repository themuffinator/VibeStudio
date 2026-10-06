#pragma once

#include "core/level_document.h"
#include "core/level_materials.h"
#include <QFile>
#include <QtEndian>
#include <algorithm>

namespace vibestudio::tests::doom {

inline void put16(QByteArray& b, int at, int value) { qToLittleEndian<qint16>(qint16(value), b.data() + at); }
inline void put32(QByteArray& b, int at, int value) { qToLittleEndian<qint32>(value, b.data() + at); }
inline void name(QByteArray& b, int at, const QByteArray& value) { for (int i = 0; i < std::min(8, int(value.size())); ++i) { b[at+i] = value[i]; } }
struct Lump { QByteArray name, bytes; };
inline QByteArray wad(const QVector<Lump>& lumps)
{
	QByteArray bytes(12, '\0'); bytes.replace(0, 4, "PWAD");
	QByteArray directory;
	for (const auto& lump : lumps) {
		QByteArray record(16, '\0'); put32(record, 0, int(bytes.size())); put32(record, 4, int(lump.bytes.size())); name(record, 8, lump.name);
		directory += record; bytes += lump.bytes;
	}
	put32(bytes, 4, int(lumps.size())); put32(bytes, 8, int(bytes.size())); bytes += directory; return bytes;
}
inline QVector<Lump> lumps(const QByteArray& bytes)
{
	QVector<Lump> result;
	const int count = qFromLittleEndian<qint32>(bytes.constData() + 4), directory = qFromLittleEndian<qint32>(bytes.constData() + 8);
	for (int i = 0; i < count; ++i) {
		const auto* at = bytes.constData() + directory + 16 * i;
		result << Lump {QByteArray(at + 8, 8).split('\0').first(), bytes.mid(qFromLittleEndian<qint32>(at), qFromLittleEndian<qint32>(at + 4))};
	}
	return result;
}
inline QByteArray patch(int width, int height, int color, bool gaps = false)
{
	QByteArray bytes(8 + width * 4, '\0'); put16(bytes, 0, width); put16(bytes, 2, height);
	for (int x = 0; x < width; ++x) {
		put32(bytes, 8 + 4 * x, int(bytes.size()));
		if (!gaps || x % 8 < 4) {
			bytes += char(0); bytes += char(height); bytes += char(0);
			for (int y = 0; y < height; ++y) { bytes += char(color + ((x / 8 + y / 8) % 2)); }
			bytes += char(0);
		}
		bytes += char(255);
	}
	return bytes;
}
inline QVector<Lump> assets()
{
	QByteArray palette(768, '\0');
	for (int i = 0; i < 256; ++i) { palette[3*i] = char(i); palette[3*i+1] = char(255-i); palette[3*i+2] = char(i / 2); }
	QByteArray pnames(20, '\0'); put32(pnames, 0, 2); name(pnames, 4, "STONE"); name(pnames, 12, "DECOR");
	QByteArray textures(4 + 8, '\0'); put32(textures, 0, 2);
	for (int i = 0; i < 2; ++i) {
		put32(textures, 4 + i * 4, int(textures.size()));
		QByteArray definition(22 + (i ? 1 : 2) * 10, '\0'); name(definition, 0, i ? "FENCE" : "STONE");
		put16(definition, 12, 64); put16(definition, 14, 64); put16(definition, 20, i ? 1 : 2);
		put16(definition, 26, i ? 1 : 0);
		if (!i) { put16(definition, 32, 8); put16(definition, 34, 8); put16(definition, 36, 1); }
		textures += definition;
	}
	QByteArray floor(4096, '\0'), ceiling(4096, '\0');
	for (int y = 0; y < 64; ++y) { for (int x = 0; x < 64; ++x) { floor[y*64+x] = char((x/8 + y/8) % 2 ? 80 : 120); ceiling[y*64+x] = char((x/16 + y/16) % 2 ? 150 : 170); } }
	return {{"PLAYPAL", palette}, {"F_START", {}}, {"STONE", floor}, {"CEIL", ceiling}, {"ALTFLAT", QByteArray(4096, char(200))}, {"F_END", {}},
		{"P_START", {}}, {"STONE", patch(64, 64, 20)}, {"DECOR", patch(32, 32, 220, true)}, {"P_END", {}}, {"PNAMES", pnames}, {"TEXTURE1", textures}};
}
inline QByteArray fixture(const QString& game = QStringLiteral("doom"))
{
	LevelMapCreateRequest request; request.game = game; request.wallTexture = QStringLiteral("STONE"); request.floorTexture = QStringLiteral("STONE"); request.ceilingTexture = QStringLiteral("CEIL");
	LevelMapDocument map; QString error;
	if (!createLevelMap(request, &map, &error)) { return {}; }
	return wad(lumps(serializeLevelMap(map).bytes) + assets());
}
inline bool write(const QString& path, const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
inline const LevelPreviewMaterial* material(const LevelPreviewAssets& assets, const QString& key)
{
	for (const auto& m : assets.materials) { if (m.key == key) { return &m; } } return nullptr;
}
inline LevelMapDocument topology()
{
	LevelMapDocument map; map.format = LevelMapFormat::DoomWad;
	LevelMapDoomSector sector; sector.id = 0; sector.floorHeight = 0; sector.ceilingHeight = 128; sector.floorTexture = QStringLiteral("STONE"); sector.ceilingTexture = QStringLiteral("CEIL");
	map.doomSectors << sector; return map;
}
inline void loop(LevelMapDocument& map, const QVector<QPointF>& points, int sector = 0)
{
	const int base = int(map.doomVertices.size());
	for (int i = 0; i < points.size(); ++i) {
		LevelMapDoomVertex vertex; vertex.id = base+i; vertex.x = points[i].x(); vertex.y = points[i].y(); map.doomVertices << vertex;
		LevelMapDoomSidedef side; side.id = int(map.doomSidedefs.size()); side.sector = sector; side.middleTexture = QStringLiteral("STONE"); side.upperTexture = side.lowerTexture = QStringLiteral("-"); map.doomSidedefs << side;
		LevelMapDoomLinedef line; line.id = int(map.doomLinedefs.size()); line.startVertex = base+i; line.endVertex = base+(i+1)%int(points.size()); line.frontSidedef = side.id; map.doomLinedefs << line;
	}
}
}
