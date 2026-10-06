#pragma once
#include "core/level_build_workspace.h"
#include "core/level_document.h"
#include "core/package_staging.h"
#include "tests/package_subset_test_helpers.h"
#include <QtEndian>
namespace vibestudio::tests {
inline QByteArray buildMiptex(const QByteArray& name) {
	QByteArray bytes(40, '\0');
	bytes.replace(0, name.size(), name);
	qToLittleEndian<quint32>(16, bytes.data() + 16);
	qToLittleEndian<quint32>(16, bytes.data() + 20);
	for (int i = 0; i < 4; ++i) {
		qToLittleEndian<quint32>(quint32(bytes.size()), bytes.data() + 24 + i * 4);
		bytes += QByteArray((16 >> i) * (16 >> i), char(70 + i));
	}
	return bytes;
}
inline bool buildEngineFixture(const QString& root, const QString& target, LevelMapDocument* map, PackageArchive* archive, QString* error) {
	LevelMapCreateRequest create;
	create.game = target;
	create.wallTexture = create.floorTexture = create.ceilingTexture = QStringLiteral("authored");
	if (!createLevelMap(create, map, error)) {
		return false;
	}
	const auto assets = QDir(root).filePath("assets");
	QDir().mkpath(assets);
	if (target == QStringLiteral("quake")) {
		PackageStagingModel staging;
		if (!staging.createEmpty(PackageArchiveFormat::Wad, "WAD2", error)) {
			return false;
		}
		for (const auto& name : {QByteArray("authored"), QByteArray("+0anim"), QByteArray("+1anim"), QByteArray("*water")}) {
			if (!staging.addWadBytes(buildMiptex(name), QString::fromLatin1(name), {}, 0x44, false, error)) {
				return false;
			}
		}
		PackageWriteRequest write;
		write.format = PackageArchiveFormat::Wad;
		write.destinationPath = QDir(assets).filePath("textures.wad");
		if (!staging.writeArchive(write).succeeded()) {
			return false;
		}
		setLevelMapEntityProperty(map, 0, "wad", "Z:/unavailable/foreign.wad", error);
	} else {
		QByteArray wal(100, '\0');
		wal.replace(0, 8, "authored");
		qToLittleEndian<quint32>(16, wal.data() + 32);
		qToLittleEndian<quint32>(16, wal.data() + 36);
		for (int i = 0; i < 4; ++i) {
			qToLittleEndian<quint32>(quint32(wal.size()), wal.data() + 40 + 4 * i);
			wal += QByteArray((16 >> i) * (16 >> i), char(80 + i));
		}
		QDir().mkpath(QDir(assets).filePath("textures"));
		if (!subset_test::put(QDir(assets).filePath("textures/authored.wal"), wal)) {
			return false;
		}
	}
	return archive->load(assets, error);
}
} // namespace vibestudio::tests
