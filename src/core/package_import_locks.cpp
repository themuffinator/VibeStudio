#include "core/package_import_store.h"
#include "core/package_storage.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(Q_OS_UNIX) && !defined(Q_OS_ANDROID)
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace vibestudio {
namespace {
constexpr qint64 lockByteLimit = 65536;
QString message(const char* value) { return QCoreApplication::translate("PackageImportLocks", value); }
bool fail(QString* error, const QString& value) { if (error) *error = value; return false; }
#if defined(Q_OS_WIN)
QString nativePath(const QString& path)
{
	const auto native = QDir::toNativeSeparators(path);
	if (native.startsWith(QStringLiteral("\\\\?\\"))) return native;
	return native.startsWith(QStringLiteral("\\\\")) ? QStringLiteral("\\\\?\\UNC\\") + native.mid(2) : QStringLiteral("\\\\?\\") + native;
}
#endif

QString lockPath(const QString& directory, const QString& relative, QString* error)
{
	const auto parts = relative.split('/');
	bool valid = relative == QStringLiteral(".store.lock") || relative == QStringLiteral(".store.lock.rmlock");
	if (parts.size() == 2 && parts.first().endsWith(QStringLiteral(".working"))
		&& (parts.last() == QStringLiteral(".lease") || parts.last() == QStringLiteral(".lease.rmlock"))) {
		const auto id = parts.first().chopped(8); const QUuid uuid(id);
		valid = !uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == id;
	}
	if (directory.isEmpty() || !valid) {
		fail(error, message(QT_TRANSLATE_NOOP("PackageImportLocks", "Choose a lock listed in the working import inventory."))); return {};
	}
	const QString path = QDir(QFileInfo(directory).absoluteFilePath()).filePath(relative);
	return safePackageStoragePath(path, error) ? path : QString();
}

class LockHandle final {
public:
	explicit LockHandle(QString path) : m_path(std::move(path)) {}
	~LockHandle()
	{
#if defined(Q_OS_WIN)
		if (m_handle != INVALID_HANDLE_VALUE) CloseHandle(m_handle);
#elif defined(Q_OS_UNIX) && !defined(Q_OS_ANDROID)
		if (m_fd >= 0) close(m_fd);
#endif
	}
	bool open(bool exclusive, QString* error)
	{
		m_exclusive = exclusive;
#if defined(Q_OS_WIN)
		// Original adapter to CreateFile sharing and handle-based deletion:
		// https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-createfilew
		// https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle
		const QString native = nativePath(m_path);
		m_handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), GENERIC_READ | (exclusive ? DELETE : 0),
			exclusive ? 0 : FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
			FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
		if (m_handle == INVALID_HANDLE_VALUE) return systemError(error, GetLastError());
#elif defined(Q_OS_UNIX) && !defined(Q_OS_ANDROID)
		const auto path = QFile::encodeName(m_path);
		m_fd = ::open(path.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
		if (m_fd < 0) return systemError(error, errno);
		// flock excludes Qt's native lock holders on macOS and Linux. It is
		// tied to this open description, not a process-wide fcntl record.
		// https://man7.org/linux/man-pages/man2/flock.2.html
		if (exclusive && flock(m_fd, LOCK_EX | LOCK_NB) != 0) return systemError(error, errno);
#else
		return fail(error, message(QT_TRANSLATE_NOOP("PackageImportLocks", "This platform cannot safely inspect or release working import locks.")));
#endif
		return true;
	}
	QByteArray review(qint64* byteCount, QString* error, const PackageReadControl& control)
	{
		qint64 size = 0; const auto before = identity(&size, error); if (before.isEmpty()) return {};
		if (size < 0 || size > lockByteLimit) {
			fail(error, message(QT_TRANSLATE_NOOP("PackageImportLocks", "The lock exceeds the 64 KiB metadata limit."))); return {};
		}
		if (control.isCancelled && control.isCancelled()) { cancelled(error); return {}; }
		QByteArray bytes(size, '\0');
#if defined(Q_OS_WIN)
		DWORD read = 0;
		if (!ReadFile(m_handle, bytes.data(), static_cast<DWORD>(size), &read, nullptr) || read != static_cast<DWORD>(size)) {
			systemError(error, GetLastError()); return {};
		}
#elif defined(Q_OS_UNIX) && !defined(Q_OS_ANDROID)
		qint64 done = 0;
		while (done < size) {
			const auto count = ::read(m_fd, bytes.data() + done, static_cast<size_t>(size - done));
			if (count < 0 && errno == EINTR) continue;
			if (count <= 0) { systemError(error, count < 0 ? errno : EIO); return {}; }
			done += count;
		}
#endif
		if (control.progress) control.progress(m_path, size, size);
		if (control.isCancelled && control.isCancelled()) { cancelled(error); return {}; }
		qint64 afterSize = 0;
		if (identity(&afterSize, error) != before || afterSize != size || !matchesPath()) {
			fail(error, message(QT_TRANSLATE_NOOP("PackageImportLocks", "The lock changed during review. Refresh the inventory."))); return {};
		}
		QCryptographicHash hash(QCryptographicHash::Sha256);
		for (const auto& field : {m_path.toUtf8(), before, bytes}) {
			hash.addData(QByteArray::number(field.size())); hash.addData(":"); hash.addData(field);
		}
		*byteCount = size; return hash.result();
	}
	bool remove(QString* error)
	{
		if (!m_exclusive || !safePackageStoragePath(m_path, error) || !matchesPath())
			return fail(error, message(QT_TRANSLATE_NOOP("PackageImportLocks", "The reviewed lock is no longer at this path.")));
#if defined(Q_OS_WIN)
		FILE_DISPOSITION_INFO disposition{TRUE};
		if (!SetFileInformationByHandle(m_handle, FileDispositionInfo, &disposition, sizeof(disposition)))
			return systemError(error, GetLastError());
		return true; // The exclusively held reviewed file disappears on close.
#elif defined(Q_OS_UNIX) && !defined(Q_OS_ANDROID)
		const auto path = QFile::encodeName(m_path);
		return unlink(path.constData()) == 0 || systemError(error, errno);
#else
		return false;
#endif
	}
private:
	static bool cancelled(QString* error) { return fail(error, message(QT_TRANSLATE_NOOP("PackageImportLocks", "Working import lock recovery cancelled."))); }
	static bool systemError(QString* error, quint64 code)
	{
		return fail(error, message(QT_TRANSLATE_NOOP("PackageImportLocks", "The lock is in use, inaccessible, or cannot be safely released on this filesystem (system error %1). Retry after its owner finishes.")).arg(code));
	}
	QByteArray identity(qint64* size, QString* error)
	{
#if defined(Q_OS_WIN)
		BY_HANDLE_FILE_INFORMATION info{};
		if (!GetFileInformationByHandle(m_handle, &info)) { systemError(error, GetLastError()); return {}; }
		if (GetFileType(m_handle) != FILE_TYPE_DISK || (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) || info.nNumberOfLinks != 1) {
			fail(error, message(QT_TRANSLATE_NOOP("PackageImportLocks", "The lock must be an independent regular file without links."))); return {};
		}
		m_identity = info;
		const quint64 length = (quint64(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
		*size = length > quint64(lockByteLimit) ? lockByteLimit + 1 : static_cast<qint64>(length);
		return QByteArray::number(info.dwVolumeSerialNumber) + ':' + QByteArray::number(info.nFileIndexHigh) + ':' + QByteArray::number(info.nFileIndexLow)
			+ ':' + QByteArray::number((quint64(info.ftCreationTime.dwHighDateTime) << 32) | info.ftCreationTime.dwLowDateTime)
			+ ':' + QByteArray::number((quint64(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime) + ':' + QByteArray::number(length);
#elif defined(Q_OS_UNIX) && !defined(Q_OS_ANDROID)
		struct stat info{};
		if (fstat(m_fd, &info) != 0) { systemError(error, errno); return {}; }
		if (!S_ISREG(info.st_mode) || info.st_nlink != 1) {
			fail(error, message(QT_TRANSLATE_NOOP("PackageImportLocks", "The lock must be an independent regular file without links."))); return {};
		}
		m_identity = info; *size = info.st_size;
#if defined(Q_OS_MACOS)
		const auto modified = info.st_mtimespec, changed = info.st_ctimespec;
#else
		const auto modified = info.st_mtim, changed = info.st_ctim;
#endif
		return QByteArray::number(quint64(info.st_dev)) + ':' + QByteArray::number(quint64(info.st_ino)) + ':' + QByteArray::number(*size)
			+ ':' + QByteArray::number(qint64(modified.tv_sec)) + ':' + QByteArray::number(qint64(modified.tv_nsec))
			+ ':' + QByteArray::number(qint64(changed.tv_sec)) + ':' + QByteArray::number(qint64(changed.tv_nsec));
#else
		Q_UNUSED(size); Q_UNUSED(error); return {};
#endif
	}
	bool matchesPath() const
	{
		if (!safePackageStoragePath(m_path)) return false;
#if defined(Q_OS_WIN)
		// An exclusive handle already denies replacement; a shared inspection
		// reopens for attributes to reject a rename/replacement during its read.
		const HANDLE check = CreateFileW(reinterpret_cast<LPCWSTR>(nativePath(m_path).utf16()), FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
		if (check == INVALID_HANDLE_VALUE) return false;
		BY_HANDLE_FILE_INFORMATION current{}; const bool read = GetFileInformationByHandle(check, &current); CloseHandle(check);
		return read && !(current.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
			&& current.dwVolumeSerialNumber == m_identity.dwVolumeSerialNumber && current.nFileIndexHigh == m_identity.nFileIndexHigh && current.nFileIndexLow == m_identity.nFileIndexLow;
#elif defined(Q_OS_UNIX) && !defined(Q_OS_ANDROID)
		struct stat current{}; const auto path = QFile::encodeName(m_path);
		return lstat(path.constData(), &current) == 0 && S_ISREG(current.st_mode) && current.st_dev == m_identity.st_dev && current.st_ino == m_identity.st_ino;
#else
		return false;
#endif
	}
	QString m_path;
	bool m_exclusive = false;
#if defined(Q_OS_WIN)
	HANDLE m_handle = INVALID_HANDLE_VALUE;
	BY_HANDLE_FILE_INFORMATION m_identity{};
#elif defined(Q_OS_UNIX) && !defined(Q_OS_ANDROID)
	int m_fd = -1;
	struct stat m_identity{};
#endif
};
} // namespace

PackageImportLockInfo inspectPackageImportLock(const QString& directory, const QString& relativePath, const PackageReadControl& control)
{
	PackageImportLockInfo result; result.relativePath = relativePath;
	const auto path = lockPath(directory, relativePath, &result.error); if (path.isEmpty()) return result;
	result.exists = QFileInfo::exists(path); if (!result.exists) return result;
	LockHandle handle(path);
	if (handle.open(false, &result.error)) result.fingerprint = handle.review(&result.bytes, &result.error, control);
	// Ordinary owners can finish between directory enumeration and inspection.
	if (!result.error.isEmpty() && !QFileInfo::exists(path) && safePackageStoragePath(path)) {
		result.exists = false; result.error.clear(); result.fingerprint.clear();
	}
	return result;
}

bool releasePackageImportLock(const QString& directory, const QString& relativePath, const QByteArray& expectedFingerprint,
	bool dryRun, QString* error, const PackageReadControl& control)
{
	if (error) error->clear();
	const auto path = lockPath(directory, relativePath, error); if (path.isEmpty()) return false;
	if (expectedFingerprint.size() != 32) return fail(error, message(QT_TRANSLATE_NOOP("PackageImportLocks", "Choose the current lock review checksum.")));
	LockHandle handle(path); if (!handle.open(true, error)) return false;
	qint64 size = 0; const auto observed = handle.review(&size, error, control); if (observed.isEmpty()) return false;
	if (observed != expectedFingerprint) return fail(error, message(QT_TRANSLATE_NOOP("PackageImportLocks", "The lock changed after review. Refresh the inventory.")));
	return dryRun || handle.remove(error);
}
} // namespace vibestudio
