#pragma once

#include "core/package_archive.h"
#include "core/deflate.h"
#include <QDir>
#include <QFile>
#include <QPair>

namespace vibestudio::subset_test {
using Lump = QPair<QByteArray, QByteArray>;
inline void u32(QByteArray& bytes, quint32 value)
{
	for (int shift = 0; shift < 32; shift += 8) { bytes.append(static_cast<char>(value >> shift)); }
}
inline void u16(QByteArray& bytes, quint16 value) { bytes.append(static_cast<char>(value)); bytes.append(static_cast<char>(value >> 8)); }
inline bool put(const QString& path, const QByteArray& bytes)
{
	QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
inline QByteArray get(const QString& path)
{
	QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
inline bool wad(const QString& path, const QVector<Lump>& lumps, const QByteArray& magic = "PWAD")
{
	const bool texture = magic == "WAD2" || magic == "WAD3"; QByteArray payload, directory;
	for (const auto& [name, bytes] : lumps) {
		u32(directory, 12 + payload.size()); u32(directory, bytes.size());
		if (texture) { u32(directory, bytes.size()); directory.append(char(0x43)); directory.append(QByteArray(3, '\0')); }
		auto padded = name; padded.resize(texture ? 16 : 8, '\0'); directory.append(padded); payload.append(bytes);
	}
	QByteArray header = magic; u32(header, lumps.size()); u32(header, 12 + payload.size()); return put(path, header + payload + directory);
}
inline bool pak(const QString& path, const QVector<Lump>& files)
{
	QByteArray payload, directory;
	for (const auto& [name, bytes] : files) {
		auto padded = name; padded.resize(56, '\0'); directory.append(padded);
		u32(directory, 12 + payload.size()); u32(directory, bytes.size()); payload.append(bytes);
	}
	QByteArray header("PACK"); u32(header, 12 + payload.size()); u32(header, directory.size()); return put(path, header + payload + directory);
}
inline bool zip(const QString& path, const QVector<Lump>& files)
{
	// Small stored-entry fixture, using the same APPNOTE layout as the existing
	// package archive/staging tests. Deliberately retains duplicate file names.
	QByteArray payload, directory;
	for (const auto& [name, bytes] : files) {
		const auto offset = payload.size(), size = bytes.size(); const auto crc = crc32Bytes(bytes);
		u32(payload, 0x04034b50); u16(payload, 20); u16(payload, 0); u16(payload, 0); u16(payload, 0); u16(payload, 0);
		u32(payload, crc); u32(payload, size); u32(payload, size); u16(payload, name.size()); u16(payload, 0); payload += name + bytes;
		u32(directory, 0x02014b50); u16(directory, 20); u16(directory, 20); u16(directory, 0); u16(directory, 0); u16(directory, 0); u16(directory, 0);
		u32(directory, crc); u32(directory, size); u32(directory, size); u16(directory, name.size()); u16(directory, 0); u16(directory, 0);
		u16(directory, 0); u16(directory, 0); u32(directory, 0); u32(directory, offset); directory += name;
	}
	QByteArray end; u32(end, 0x06054b50); u16(end, 0); u16(end, 0); u16(end, files.size()); u16(end, files.size());
	u32(end, directory.size()); u32(end, payload.size()); u16(end, 0); return put(path, payload + directory + end);
}
inline QVector<Lump> map(const QByteArray& label, const QByteArray& suffix)
{
	QVector<Lump> result{{label, {}}};
	for (const auto* name : {"THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES", "SEGS", "SSECTORS", "NODES", "SECTORS", "REJECT", "BLOCKMAP"}) {
		result.append({name, QByteArray(name) + suffix});
	}
	return result;
}
inline QVector<Lump> fixture()
{
	auto result = map("MAP01", "-first"); result += map("MAP02", "-second");
	result += QVector<Lump>{{"BEHAVIOR", "bytecode"}, {"SCRIPTS", "source"}, {"TITLEMAP", {}}, {"TEXTMAP", "namespace=\"zdoom\";"},
		{"CUSTOM", "sidecar"}, {"ZNODES", "nodes"}, {"ENDMAP", {}}, {"F_START", {}}, {"FLOORA", "floor-a"}, {"FLOORB", "floor-b"}, {"F_END", {}},
		{"P_START", {}}, {"P1_START", {}}, {"PATCHA", "patch-a"}, {"P1_END", {}}, {"P_END", {}},
		{"PLAYPAL", "palette"}, {"PNAMES", "names"}, {"TEXTURE1", "textures"}, {"TEXTURE2", "extra"}};
	return result;
}
inline QVector<Lump> newWadFixture()
{
	// Unique names permit ordinary source-free additions. Named binary/UDMF
	// maps, GL nodes, namespaces and a leading resource expose any save-time
	// global sorting that would disagree with the reviewed positional plan.
	QVector<Lump> result{{"PLAYPAL", "palette"}}; result += map("INTRO", "-new");
	result += QVector<Lump>{{"GL_INTRO", {}}, {"GL_VERT", "vertices"}, {"GL_SEGS", "segs"}, {"GL_SSECT", "subsectors"}, {"GL_NODES", "nodes"},
		{"TITLEMAP", {}}, {"TEXTMAP", "namespace=\"zdoom\";"}, {"CUSTOM", "sidecar"}, {"ZNODES", "nodes"}, {"ENDMAP", {}},
		{"F_START", {}}, {"FLOORA", "flat"}, {"F_END", {}}, {"P_START", {}}, {"P1_START", {}}, {"PATCHA", "patch"}, {"P1_END", {}}, {"P_END", {}},
		{"PNAMES", "names"}, {"TEXTURE1", "textures"}, {"TEXTURE2", "extra"}};
	return result;
}
inline qsizetype index(const PackageArchiveReader& archive, const QString& name, int occurrence = 0)
{
	const auto entries = archive.entries();
	for (qsizetype at = 0; at < entries.size(); ++at) {
		if (entries.at(at).virtualPath == name && occurrence-- == 0) { return at; }
	}
	return -1;
}
} // namespace vibestudio::subset_test
