#pragma once

#include "core/model_mdl.h"
#include "core/package_archive.h"
#include "tests/model_mdl_test_helpers.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QThread>
#include <atomic>

namespace vibestudio::tests
{
inline QByteArray skinLump(int value = 1, int width = 8, int height = 4)
{
	QByteArray bytes;
	mdlInteger(bytes, width);
	mdlInteger(bytes, height);
	for (int i = 0; i < width * height; ++i)
	{
		bytes += char(i % 3 == 0 ? 0 : i % 3 == 1 ? value : 255);
	}
	return bytes;
}
inline QImage skinImage(const IdTechPalette &palette, int width = 16, int height = 8)
{
	QImage image(width, height, QImage::Format_Indexed8);
	image.setColorTable(palette.colors);
	const auto pixels = skinLump(1, width, height).mid(8);
	for (int y = 0; y < height; ++y)
	{
		std::memcpy(image.scanLine(y), pixels.constData() + y * width, width);
	}
	return image;
}
inline QByteArray skinPng(const QImage &image)
{
	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	image.save(&buffer, "PNG");
	return bytes;
}
// Synthetic WAD directory, deliberately preserving repeated physical names.
inline QByteArray repeatedSkinWad(const QByteArray &first, const QByteArray &second)
{
	QByteArray bytes("WAD2");
	mdlInteger(bytes, 2);
	mdlInteger(bytes, 12 + int(first.size() + second.size()));
	bytes += first + second;
	int offset = 12;
	for (const auto &payload : {first, second})
	{
		mdlInteger(bytes, offset);
		mdlInteger(bytes, int(payload.size()));
		mdlInteger(bytes, int(payload.size()));
		bytes += QByteArray::fromHex("44000000");
		bytes += QByteArray("skin.lmp").leftJustified(16, '\0');
		offset += int(payload.size());
	}
	return bytes;
}
class SkinReader final : public PackageArchiveReader
{
  public:
	QVector<PackageEntry> metadata;
	QVector<QByteArray> payloads;
	bool lateFailure = false, delay = false;
	mutable std::atomic_bool readOnGui = false, entered = false;
	void add(const QString &path, const QByteArray &bytes)
	{
		PackageEntry entry;
		entry.virtualPath = path;
		entry.sizeBytes = quint64(bytes.size());
		metadata << entry;
		payloads << bytes;
	}
	PackageArchiveFormat format() const override { return PackageArchiveFormat::Folder; }
	QString sourcePath() const override { return QStringLiteral("synthetic-skin-snapshot"); }
	bool isOpen() const override { return true; }
	QVector<PackageEntry> entries() const override { return metadata; }
	bool readEntryBytes(const QString &, QByteArray *, QString *, qint64 = -1) const override { return false; }
	bool readEntryAt(qsizetype index, QByteArray *out, QString *error, qint64 maxBytes = -1) const override
	{
		if (!out || index < 0 || index >= payloads.size() || (maxBytes >= 0 && payloads[index].size() > maxBytes))
		{
			return false;
		}
		QByteArray bytes;
		if (!streamEntryAt(
				index,
				[&bytes](QByteArrayView part) {
					bytes.append(part.data(), part.size());
					return true;
				},
				error))
		{
			return false;
		}
		*out = std::move(bytes);
		return true;
	}
	bool streamEntryAt(qsizetype index, const std::function<bool(QByteArrayView)> &sink, QString *error,
					   const std::function<bool()> &cancelled = {}) const override
	{
		readOnGui = QThread::currentThread() == QCoreApplication::instance()->thread();
		entered = true;
		for (int wait = 0; delay && wait < 5000 && !(cancelled && cancelled()); ++wait)
		{
			QThread::msleep(1);
		}
		if (cancelled && cancelled())
		{
			return false;
		}
		if (index < 0 || index >= payloads.size())
		{
			return false;
		}
		const auto &bytes = payloads[index];
		for (qsizetype i = 0; i < bytes.size(); i += 11)
		{
			if ((cancelled && cancelled()) || !sink(QByteArrayView(bytes).sliced(i, qMin<qsizetype>(11, bytes.size() - i))))
			{
				return false;
			}
		}
		if (lateFailure && error)
		{
			*error = QStringLiteral("Synthetic payload verification failure");
		}
		return !lateFailure;
	}
};
} // namespace vibestudio::tests
