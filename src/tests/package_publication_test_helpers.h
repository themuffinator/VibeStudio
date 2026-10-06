#pragma once

#include "core/package_publication.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>

namespace vibestudio::publication_test {
inline bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
inline QByteArray read(const QString& path)
{
	QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
inline PackagePublicationJournalInfo interrupt(const QString& folder, const QByteArray& before = "original",
	const QByteArray& after = "replacement", const QString& backup = {}, PackagePublicationStep stop = PackagePublicationStep::OutputCommitted)
{
	QDir().mkpath(folder); const QString output = QDir(folder).filePath(QStringLiteral("output.pak"));
	if (!before.isNull() && !write(output, before)) { return {}; }
	PackagePublicationOptions options; options.destinationPath = output; options.allowOverwrite = !before.isNull(); options.backupPath = backup;
	{
		PackagePublication publication(options, [stop](auto step, QString* error) {
			if (step != stop) { return true; } *error = QStringLiteral("Injected interruption"); return false;
		});
		QString error;
		if (!publication.begin(&error) || publication.device()->write(after) != after.size()) { return {}; }
		publication.commit(after.size(), QString::fromLatin1(QCryptographicHash::hash(after, QCryptographicHash::Sha256).toHex()));
	}
	const auto files = QDir(folder).entryList({QStringLiteral(".vibestudio-package-*.payload.json")}, QDir::Files | QDir::Hidden);
	return files.size() == 1 ? inspectPackagePublicationJournal(QDir(folder).filePath(files.first())) : PackagePublicationJournalInfo();
}
} // namespace vibestudio::publication_test
