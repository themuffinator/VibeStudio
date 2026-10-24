#pragma once

#include <QByteArray>
#include <QString>
#include <filesystem>

namespace vibestudio
{
// Transient filesystem identity, never persisted as portable draft provenance.
// Empty keys use the standard-library equivalent() fallback. No payload reads.
struct PackagePathIdentity
{
	QByteArray key;
	QString error;
	quint64 links = 0;
	bool exists = false, directory = false, regular = false;
};

std::filesystem::path packageNativeFilesystemPath(const QString &path);
QString packageQtFilesystemPath(const std::filesystem::path &path);
PackagePathIdentity inspectPackagePathIdentity(const QString &path, bool portable = false);
} // namespace vibestudio
