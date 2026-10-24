#include "core/package_path_identity_p.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(Q_OS_UNIX)
#include <sys/stat.h>
#endif

namespace vibestudio
{
std::filesystem::path packageNativeFilesystemPath(const QString &path)
{
#ifdef Q_OS_WIN
	return std::filesystem::path(path.toStdWString());
#else
	return std::filesystem::path(QFile::encodeName(path).constData());
#endif
}
QString packageQtFilesystemPath(const std::filesystem::path &path)
{
#ifdef Q_OS_WIN
	return QDir::fromNativeSeparators(QString::fromStdWString(path.native()));
#else
	return QFile::decodeName(path.native().c_str());
#endif
}
PackagePathIdentity inspectPackagePathIdentity(const QString &path, bool portable)
{
	PackagePathIdentity result;
	const auto native = packageNativeFilesystemPath(path);
	std::error_code error;
	const auto status = std::filesystem::status(native, error);
	if (error == std::errc::no_such_file_or_directory || error == std::errc::not_a_directory ||
		(!error && status.type() == std::filesystem::file_type::not_found))
		return result;
	const auto failure = [&]
	{
		result.error =
			QCoreApplication::translate("PackageInputProtection", "Cannot inspect filesystem identity for source protection: %1.")
				.arg(path);
		return result;
	};
	if (error)
		return failure();
	result.exists = true;
	result.directory = std::filesystem::is_directory(status);
	result.regular = std::filesystem::is_regular_file(status);
	if (!result.regular)
		return result;
	result.links = std::filesystem::hard_link_count(native, error);
	if (error || result.links == 0)
		return failure();
	if (portable)
		return result;
#if defined(Q_OS_WIN)
	// ReFS needs the full 128-bit identifier; a 64-bit file index is insufficient.
	// Unsupported native identity queries retain the portable equivalent() path.
	auto windowsPath = QDir::toNativeSeparators(path);
	if (!windowsPath.startsWith(QStringLiteral("\\\\?\\")))
		windowsPath = windowsPath.startsWith(QStringLiteral("\\\\")) ? QStringLiteral("\\\\?\\UNC\\") + windowsPath.mid(2)
																	 : QStringLiteral("\\\\?\\") + windowsPath;
	const HANDLE handle =
		CreateFileW(reinterpret_cast<LPCWSTR>(windowsPath.utf16()), FILE_READ_ATTRIBUTES,
					FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (handle != INVALID_HANDLE_VALUE)
	{
		FILE_ID_INFO identity{};
		if (GetFileInformationByHandleEx(handle, FileIdInfo, &identity, sizeof(identity)))
		{
			result.key = QByteArray::number(identity.VolumeSerialNumber) + ':' +
						 QByteArray(reinterpret_cast<const char *>(identity.FileId.Identifier), sizeof(identity.FileId.Identifier)).toHex();
		}
		CloseHandle(handle);
	}
#elif defined(Q_OS_UNIX)
	struct stat identity{};
	if (::stat(native.c_str(), &identity) == 0 && S_ISREG(identity.st_mode))
		result.key = QByteArray::number(quint64(identity.st_dev)) + ':' + QByteArray::number(quint64(identity.st_ino));
#endif
	return result;
}
} // namespace vibestudio
