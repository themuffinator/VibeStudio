#include "core/package_draft_access.h"
#include "core/package_storage.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(Q_OS_UNIX)
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace vibestudio {

#if defined(Q_OS_WIN)
namespace {
QString nativeDirectoryPath(const QString& value)
{
	const auto path = QDir::toNativeSeparators(value);
	if (path.startsWith(QStringLiteral("\\\\?\\"))) { return path; }
	return path.startsWith(QStringLiteral("\\\\")) ? QStringLiteral("\\\\?\\UNC\\") + path.mid(2) : QStringLiteral("\\\\?\\") + path;
}
}
#endif

struct PackageDraftAccess::State {
	QString path;
	bool protectedReaders = false;
#if defined(Q_OS_WIN)
	HANDLE handle = INVALID_HANDLE_VALUE;
	BY_HANDLE_FILE_INFORMATION identity{};
	~State() { if (handle != INVALID_HANDLE_VALUE) { CloseHandle(handle); } }
#elif defined(Q_OS_UNIX)
	int descriptor = -1;
	struct stat identity{};
	~State() { if (descriptor >= 0) { close(descriptor); } }
#endif
};

PackageDraftAccess::PackageDraftAccess(std::unique_ptr<State> state) : m_state(std::move(state)) {}
PackageDraftAccess::~PackageDraftAccess() = default;
bool PackageDraftAccess::protectsReaders() const { return m_state->protectedReaders; }

bool PackageDraftAccess::matchesDirectory() const
{
	if (!safePackageStoragePath(m_state->path)) { return false; }
#if defined(Q_OS_WIN)
	const auto path = nativeDirectoryPath(m_state->path);
	const HANDLE check = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), FILE_READ_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
	if (check == INVALID_HANDLE_VALUE) { return false; }
	BY_HANDLE_FILE_INFORMATION current{};
	const bool read = GetFileInformationByHandle(check, &current); CloseHandle(check);
	return read && !(current.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
		&& (current.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		&& current.dwVolumeSerialNumber == m_state->identity.dwVolumeSerialNumber
		&& current.nFileIndexHigh == m_state->identity.nFileIndexHigh && current.nFileIndexLow == m_state->identity.nFileIndexLow;
#elif defined(Q_OS_UNIX)
	struct stat current{}; const auto path = QFile::encodeName(m_state->path);
	return lstat(path.constData(), &current) == 0 && S_ISDIR(current.st_mode)
		&& current.st_dev == m_state->identity.st_dev && current.st_ino == m_state->identity.st_ino;
#else
	return QFileInfo(m_state->path).isDir();
#endif
}

std::shared_ptr<const PackageDraftAccess> PackageDraftAccess::acquire(const QString& directory, Mode mode, QString* error)
{
	if (error) { error->clear(); }
	const auto fail = [&](const QString& message) -> std::shared_ptr<const PackageDraftAccess> { if (error) { *error = message; } return {}; };
	if (!safePackageStoragePath(directory, error) || !QFileInfo(directory).isDir()) {
		return fail(QCoreApplication::translate("PackageDraftAccess", "Package draft storage is unavailable or unsafe."));
	}
	auto state = std::make_unique<State>(); state->path = QFileInfo(directory).absoluteFilePath();
#if defined(Q_OS_WIN)
	// Original adapter using the documented CreateFile sharing contract:
	// https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-createfilew
	// Shared handles allow atomic child-file renames. Maintenance denies read
	// sharing, excluding every snapshot handle (including this process's).
	// Windows directory enumeration needs read sharing: maintenance scans and
	// takes the writer lock first, then rechecks directory state after exclusion.
	const auto path = nativeDirectoryPath(state->path);
	state->handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ,
		FILE_SHARE_WRITE | FILE_SHARE_DELETE | (mode == Mode::Read ? FILE_SHARE_READ : 0), nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
	if (state->handle == INVALID_HANDLE_VALUE) {
		const auto code = GetLastError();
		if (code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION) {
			return fail(QCoreApplication::translate("PackageDraftAccess", "The draft is in use. Close its documents and wait for background readers before maintenance, or retry after maintenance finishes."));
		}
		return fail(QCoreApplication::translate("PackageDraftAccess", "Unable to protect package draft storage (system error %1).").arg(code));
	}
	if (!GetFileInformationByHandle(state->handle, &state->identity) || !(state->identity.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		|| (state->identity.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
		return fail(QCoreApplication::translate("PackageDraftAccess", "Package draft storage changed while it was opened."));
	}
	state->protectedReaders = true;
#elif defined(Q_OS_UNIX)
	// Original adapter to BSD flock, also implemented by Linux. Advisory locks
	// are held by the open description, including copied snapshot references:
	// https://man7.org/linux/man-pages/man2/flock.2.html
	// https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/flock.2.html
	const auto path = QFile::encodeName(state->path);
	state->descriptor = open(path.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (state->descriptor < 0 || fstat(state->descriptor, &state->identity) != 0 || !S_ISDIR(state->identity.st_mode)) {
		return fail(QCoreApplication::translate("PackageDraftAccess", "Unable to open package draft storage for reader protection."));
	}
	if (flock(state->descriptor, (mode == Mode::Read ? LOCK_SH : LOCK_EX) | LOCK_NB) == 0) { state->protectedReaders = true; }
	else {
		const int code = errno;
		if (code == EWOULDBLOCK || code == EAGAIN) {
			return fail(QCoreApplication::translate("PackageDraftAccess", "The draft is in use. Close its documents and wait for background readers before maintenance, or retry after maintenance finishes."));
		}
		if (code != ENOSYS && code != EOPNOTSUPP && code != EINVAL) {
			return fail(QCoreApplication::translate("PackageDraftAccess", "Unable to protect package draft storage (system error %1).").arg(code));
		}
	}
#endif
	// Portable fallback preserves ordinary reads/saves on unsupported systems.
	// Destructive maintenance never proceeds without native reader exclusion.
	if (mode == Mode::Maintain && !state->protectedReaders) {
		return fail(QCoreApplication::translate("PackageDraftAccess", "This filesystem does not support safe draft maintenance. Reading and saving remain available."));
	}
	auto access = std::shared_ptr<const PackageDraftAccess>(new PackageDraftAccess(std::move(state)));
	if (!access->matchesDirectory()) { return fail(QCoreApplication::translate("PackageDraftAccess", "Package draft storage changed while it was opened.")); }
	return access;
}

} // namespace vibestudio
