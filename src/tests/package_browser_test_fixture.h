#pragma once

#include "core/package_archive.h"
#include <memory>

namespace vibestudio::tests {

class BrowserFixture final : public PackageArchiveReader {
public:
	QVector<PackageEntry> rows;
	mutable int payloadReads = 0;
	PackageArchiveFormat format() const override { return PackageArchiveFormat::Zip; }
	QString sourcePath() const override { return QStringLiteral("browser-fixture"); }
	bool isOpen() const override { return true; }
	QVector<PackageEntry> entries() const override { return rows; }
	bool readEntryBytes(const QString&, QByteArray*, QString*, qint64) const override { ++payloadReads; return false; }
	bool readEntryAt(qsizetype at, QByteArray* output, QString*, qint64) const override { ++payloadReads; if (output) { *output = QByteArray::number(at); } return at >= 0 && at < rows.size(); }
};

inline std::shared_ptr<BrowserFixture> browserFixture(int files)
{
	auto source = std::make_shared<BrowserFixture>();
	const auto add = [&](const QString& path, bool directory = false) {
		PackageEntry entry; entry.virtualPath = path; entry.kind = directory ? PackageEntryKind::Directory : PackageEntryKind::File;
		entry.sourceOrdinal = source->rows.size(); entry.sizeBytes = directory ? 0 : QByteArray::number(entry.sourceOrdinal).size();
		entry.typeHint = directory ? QStringLiteral("directory") : QStringLiteral("text/plain"); entry.storageMethod = QStringLiteral("stored");
		source->rows.append(entry);
	};
	add(QStringLiteral("empty"), true);
	add(QStringLiteral("THINGS")); add(QStringLiteral("THINGS"));
	for (int row = 0; row < files; ++row) { add(QStringLiteral("bulk/%1.txt").arg(row, 6, 10, QLatin1Char('0'))); }
	add(QStringLiteral("unsupported.bin")); source->rows.last().readable = false; source->rows.last().note = QStringLiteral("Unsupported fixture method");
	// Reader adapters preserve listed positions; admission charges implied
	// folders but does not append them to a generic reader's physical rows.
	add(QStringLiteral("bulk"), true);
	return source;
}

} // namespace vibestudio::tests
