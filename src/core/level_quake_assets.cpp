#include "core/level_quake_assets.h"
#include <QCoreApplication>
#include <QSet>
#include <QtEndian>

namespace vibestudio {
namespace {
constexpr quint64 budget = 256ULL * 1024 * 1024;
constexpr int textureLimit = 65536;
QString text(const char* value) { return QCoreApplication::translate("LevelQuakeAssets", value); }
quint32 u32(const QByteArray& bytes, qsizetype at) { return qFromLittleEndian<quint32>(bytes.constData() + at); }
void put32(QByteArray& bytes, qsizetype at, quint32 value) { qToLittleEndian(value, bytes.data() + at); }
QString nameAt(const QByteArray& bytes, qsizetype at) {
	const auto raw = bytes.mid(at, 16);
	const auto end = raw.indexOf('\0');
	if (end < 1) {
		return {};
	}
	const auto name = raw.left(end);
	for (const auto c : name) {
		if (static_cast<unsigned char>(c) < 33 || static_cast<unsigned char>(c) > 126) {
			return {};
		}
	}
	const auto result = QString::fromLatin1(name);
	return isSafePackageVirtualPath(result) && !result.contains('/') && !result.contains('\\') ? result : QString();
}
bool validMiptex(const QByteArray& bytes, const QString& name) {
	if (bytes.size() < 40 || nameAt(bytes, 0).compare(name, Qt::CaseInsensitive) != 0) {
		return false;
	}
	const auto width = u32(bytes, 16), height = u32(bytes, 20);
	if (!width || !height || width > 8192 || height > 8192 || width % 16 || height % 16) {
		return false;
	}
	quint64 previousEnd = 40;
	for (int mip = 0; mip < 4; ++mip) {
		const quint64 offset = u32(bytes, 24 + mip * 4), size = quint64(width >> mip) * (height >> mip);
		if (offset < previousEnd || offset > quint64(bytes.size()) || size > quint64(bytes.size()) - offset) {
			return false;
		}
		previousEnd = offset + size;
	}
	return previousEnd == quint64(bytes.size()); // WAD3 palette tails are not Quake miptex.
}
bool cancelled(const PackageReadControl& control, LevelQuakeTextures* result) {
	if (!control.isCancelled || !control.isCancelled()) {
		return false;
	}
	result->cancelled = true;
	result->error = text(QT_TRANSLATE_NOOP("LevelQuakeAssets", "Texture WAD inspection cancelled."));
	return true;
}
} // namespace
LevelQuakeTextures inspectLevelQuakeTextures(const PackageArchiveReader& archive, const PackageReadControl& control, bool loose) {
	LevelQuakeTextures result;
	quint64 readBytes = 0;
	quint64 retainedBytes = 0;
	const auto entries = archive.entries();
	const auto fail = [&](const QString& path) {
		if (result.error.isEmpty()) {
			result.error = text(QT_TRANSLATE_NOOP("LevelQuakeAssets", "Cannot capture Quake texture data: %1")).arg(path);
		}
		return result;
	};
	const auto add = [&](const QString& name, const QString& path, QByteArray mip) {
		if (name.isEmpty() || !validMiptex(mip, name) || result.textures.size() >= textureLimit ||
			quint64(mip.size()) > budget - retainedBytes) {
			return false;
		}
		const auto key = name.toCaseFolded();
		if (result.textures.contains(key)) {
			result.textures[key].ambiguous = true;
		} else {
			retainedBytes += quint64(mip.size());
			result.textures.insert(key, {name, path, std::move(mip), false});
		}
		return true;
	};
	for (qsizetype at = 0; at < entries.size(); ++at) {
		if (cancelled(control, &result)) {
			return result;
		}
		const auto& entry = entries[at];
		if (entry.kind != PackageEntryKind::File) {
			continue;
		}
		const bool wad = entry.virtualPath.endsWith(QStringLiteral(".wad"), Qt::CaseInsensitive);
		const bool mip = loose && (entry.typeHint == QStringLiteral("wad-texture") ||
								   entry.virtualPath.endsWith(QStringLiteral(".mip"), Qt::CaseInsensitive));
		if (!wad && !mip) {
			continue;
		}
		if (!entry.readable || !isSafePackageVirtualPath(entry.virtualPath) || entry.sizeBytes > budget - readBytes) {
			return fail(entry.virtualPath);
		}
		readBytes += entry.sizeBytes;
		QByteArray bytes;
		if (!archive.streamEntryAt(
				at,
				[&](QByteArrayView chunk) {
					if (cancelled(control, &result) || quint64(chunk.size()) > entry.sizeBytes - quint64(bytes.size())) {
						return false;
					}
					bytes.append(chunk.data(), chunk.size());
					return true;
				},
				&result.error, control.isCancelled) ||
			quint64(bytes.size()) != entry.sizeBytes) {
			return fail(entry.virtualPath);
		}
		if (mip) {
			if (bytes.size() < 40 || !add(nameAt(bytes, 0), entry.virtualPath, bytes)) {
				return fail(entry.virtualPath);
			}
		} else {
			// Quake WAD2/miptex layout: id Software Quake wad.h / bspfile.h,
			// GPL-2.0-or-later. Interface facts only; see docs/CREDITS.md.
			if (bytes.size() < 12 || bytes.first(4) != QByteArrayLiteral("WAD2")) {
				return fail(entry.virtualPath);
			}
			const quint64 count = u32(bytes, 4), directory = u32(bytes, 8);
			if (count > textureLimit || directory < 12 || directory > quint64(bytes.size()) ||
				count * 32 > quint64(bytes.size()) - directory) {
				return fail(entry.virtualPath);
			}
			for (quint64 i = 0; i < count; ++i) {
				if (cancelled(control, &result)) {
					return result;
				}
				const auto record = qsizetype(directory + i * 32);
				if (quint8(bytes[record + 12]) != 0x44) {
					continue;
				}
				const quint64 offset = u32(bytes, record), size = u32(bytes, record + 4);
				if (bytes[record + 13] != 0 || size != u32(bytes, record + 8) || offset < 12 || offset > directory ||
					size > directory - offset ||
					!add(nameAt(bytes, record + 16), entry.virtualPath, bytes.mid(qsizetype(offset), qsizetype(size)))) {
					return fail(entry.virtualPath);
				}
			}
		}
		if (control.progress) {
			control.progress(entry.virtualPath, at + 1, entries.size());
		}
	}
	return result;
}
QByteArray encodeLevelQuakeTextureWad(const LevelQuakeTextures& textures, QString* error, const PackageReadControl& control) {
	if (!textures.error.isEmpty()) {
		*error = textures.error;
		return {};
	}
	QByteArray result(12, '\0'), directory;
	result.replace(0, 4, "WAD2");
	for (const auto& texture : textures.textures) {
		if (control.isCancelled && control.isCancelled()) {
			*error = text(QT_TRANSLATE_NOOP("LevelQuakeAssets", "Texture WAD preparation cancelled."));
			return {};
		}
		if (texture.ambiguous || !validMiptex(texture.miptex, texture.name) ||
			quint64(result.size()) + texture.miptex.size() + directory.size() + 32 > budget) {
			*error = text(QT_TRANSLATE_NOOP("LevelQuakeAssets", "A Quake texture is ambiguous, invalid or exceeds the WAD byte limit: %1"))
						 .arg(texture.name);
			return {};
		}
		QByteArray record(32, '\0');
		put32(record, 0, quint32(result.size()));
		put32(record, 4, quint32(texture.miptex.size()));
		put32(record, 8, quint32(texture.miptex.size()));
		record[12] = 0x44;
		record.replace(16, texture.name.toLatin1().size(), texture.name.toLatin1());
		directory += record;
		result += texture.miptex;
	}
	put32(result, 4, quint32(textures.textures.size()));
	put32(result, 8, quint32(result.size()));
	return result + directory;
}
} // namespace vibestudio
