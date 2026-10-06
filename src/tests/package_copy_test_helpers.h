#pragma once

#include "core/package_copy.h"
#include <QCoreApplication>
#include <QFile>
#include <QThread>
#include <algorithm>
#include <atomic>
#include <iostream>

namespace package_copy_test {
using namespace vibestudio;
inline bool expect(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; } return value; }
inline QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
class Reader final : public PackageArchiveReader {
public:
	QVector<PackageEntry> directory;
	QVector<QByteArray> bytes;
	qsizetype failAtEnd = -1;
	mutable std::atomic_int streamed = 0, buffered = 0;
	mutable std::atomic_bool readOnUi = false;
	void add(const QString& path, const QByteArray& value) {
		PackageEntry entry; entry.virtualPath = path; entry.sizeBytes = value.size();
		directory << entry; bytes << value;
	}
	void folder(const QString& path) { add(path, {}); directory.last().kind = PackageEntryKind::Directory; }
	PackageArchiveFormat format() const override { return PackageArchiveFormat::Pak; }
	QString sourcePath() const override { return QStringLiteral("synthetic-copy-source"); }
	bool isOpen() const override { return true; }
	QVector<PackageEntry> entries() const override { return directory; }
	bool readEntryBytes(const QString&, QByteArray*, QString*, qint64) const override { ++buffered; return false; }
	bool streamEntryAt(qsizetype index, const std::function<bool(QByteArrayView)>& sink, QString* error,
		const std::function<bool()>& cancelled) const override {
		++streamed; readOnUi = QThread::currentThread() == QCoreApplication::instance()->thread();
		if (index < 0 || index >= bytes.size()) { return false; }
		const auto& value = bytes.at(index);
		for (qsizetype offset = 0; offset < value.size(); offset += 65536) {
			if (cancelled && cancelled()) { return false; }
			if (!sink(QByteArrayView(value).sliced(offset, std::min<qsizetype>(65536, value.size() - offset)))) { return false; }
		}
		if (index == failAtEnd && error) { *error = QStringLiteral("injected final integrity failure"); }
		return index != failAtEnd;
	}
};
} // namespace package_copy_test
