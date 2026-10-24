#include "core/package_protection_p.h"
#include "core/package_archive.h"
#include "core/package_path_identity_p.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <utility>

namespace vibestudio {
namespace {
QString pathKey(const QString& path)
{
#ifdef Q_OS_WIN
	return path.toCaseFolded();
#else
	return path;
#endif
}
bool driveAbsolute(const QString& path)
{
	return path.size() >= 3 && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':' && path[2] == '/';
}
bool portableAbsolute(const QString& path) { return path.startsWith('/') || driveAbsolute(path); }
bool nativeAbsolute(const QString& path)
{
#ifdef Q_OS_WIN
	return driveAbsolute(path) || path.startsWith(QStringLiteral("//"));
#else
	return path.startsWith('/');
#endif
}
}
PackageInputProtectionSet::PackageInputProtectionSet(qsizetype maximumPaths, qint64 maximumBytes, QString* error,
	PackageReadControl control, QString implicitDirectory, bool portableIdentity)
	: m_maximumPaths(std::min(maximumPaths, pathCeiling)), m_maximumBytes(std::min(maximumBytes, byteCeiling)),
	  m_error(error), m_control(std::move(control)), m_implicitDirectory(std::move(implicitDirectory)), m_portableIdentity(portableIdentity) {}
bool PackageInputProtectionSet::refuse(const QString& message)
{
	if (!m_failed) { m_errorText = message; if (m_error) { *m_error = message; } } m_failed = true; return false;
}
bool PackageInputProtectionSet::checkpoint()
{
	m_cancelled = m_cancelled || (m_control.isCancelled && m_control.isCancelled());
	if (m_cancelled) { return refuse(QCoreApplication::translate("VibeStudioPackageStaging", "Package source protection preparation cancelled.")); }
	if (m_failed) { return false; }
	if (m_control.progress && m_steps % 256 == 0) {
		m_control.progress(m_matching ? QCoreApplication::translate("VibeStudioPackageStaging", "Checking package source protections")
			: QCoreApplication::translate("VibeStudioPackageStaging", "Retaining package source protections"), m_steps, 0);
		m_cancelled = m_cancelled || (m_control.isCancelled && m_control.isCancelled());
		if (m_cancelled) { return refuse(QCoreApplication::translate("VibeStudioPackageStaging", "Package source protection preparation cancelled.")); }
	}
	++m_steps;
	return true;
}
bool PackageInputProtectionSet::validStoredPath(const QString& path)
{
	return !path.isEmpty() && path.size() <= pathCharacterCeiling && path.isValidUtf16() && !path.contains(QChar(0))
		&& portableAbsolute(path) && QDir::cleanPath(QDir::fromNativeSeparators(path)) == path;
}
bool PackageInputProtectionSet::retain(const QString& path)
{
	if (!validStoredPath(path)) { return refuse(QCoreApplication::translate("VibeStudioPackageStaging", "A protected package input path is invalid or too long.")); }
	if (!m_implicitDirectory.isEmpty() && packagePathIsInsideDirectory(m_implicitDirectory, path)) { return true; }
	const QString key = pathKey(path);
	if (m_keys.contains(key)) { return true; }
	const qint64 bytes = path.size() * qint64(sizeof(QChar));
	if (m_paths.size() >= m_maximumPaths || bytes > m_maximumBytes - m_bytes) {
		return refuse(QCoreApplication::translate("VibeStudioPackageStaging", "Package source protections exceed the limit of %1 paths or %2 metadata bytes.").arg(m_maximumPaths).arg(m_maximumBytes));
	}
	m_bytes += bytes; m_keys.insert(key); m_paths.append(QString(path.constData(), path.size()));
	m_identityReady = false; m_identityKeys.clear(); m_identityPaths.clear(); m_portablePaths.clear(); return true;
}
bool PackageInputProtectionSet::add(const QString& path, bool captureAliases)
{
	if (m_matching) { m_matching = false; m_steps = 0; }
	if (m_identityReady) { m_identityReady = false; m_identityKeys.clear(); m_identityPaths.clear(); m_portablePaths.clear(); }
	if (!checkpoint()) { return false; }
	if (path.isEmpty()) { return true; }
	if (path.size() > pathCharacterCeiling || !path.isValidUtf16() || path.contains(QChar(0))) {
		return refuse(QCoreApplication::translate("VibeStudioPackageStaging", "A protected package input path is invalid or too long."));
	}
	if (!captureAliases) { return retain(path); }
	const auto input = QDir::fromNativeSeparators(path);
	// Foreign absolute roots in a moved draft remain literal provenance. They
	// must not turn into relative paths under this machine's working directory.
	const QString absolute = QDir::cleanPath(portableAbsolute(input) ? input : QFileInfo(input).absoluteFilePath());
	const bool already = m_keys.contains(pathKey(absolute));
	if (!retain(absolute)) { return false; }
	if (already || !nativeAbsolute(absolute)) { return true; }
	QString resolved;
	return resolve(absolute, &resolved) && (resolved.isEmpty() || retain(resolved));
}
bool PackageInputProtectionSet::resolve(const QString& absolute, QString* resolved)
{
	auto control = m_control;
	control.isCancelled = [this] { return !checkpoint(); };
	control.progress = {};
	const auto result = packageResolvedAbsolutePath(absolute, control);
	if (!checkpoint()) { return false; }
	if (!validStoredPath(result)) {
		return refuse(QCoreApplication::translate("VibeStudioPackageStaging", "Unable to resolve a package path for source protection checks."));
	}
	*resolved = result; return true;
}
bool PackageInputProtectionSet::finish(bool enumerationComplete)
{
	if (!checkpoint()) { return false; }
	return enumerationComplete || refuse(m_error && !m_error->isEmpty() ? *m_error
		: QCoreApplication::translate("VibeStudioPackageStaging", "Unable to retain all package source protections."));
}
bool PackageInputProtectionSet::containsAncestor(QString path, bool* found)
{
	path = pathKey(path);
	// Truncation reuses one bounded string. Lookup work follows output depth,
	// independently of the number of captured input roots.
	while (nativeAbsolute(path)) {
		if (!checkpoint()) { return false; }
		if (m_keys.contains(path)) { *found = true; return true; }
		if (path.endsWith('/')) { break; }
		const auto slash = path.lastIndexOf('/');
		if (slash < 0) { break; }
		const auto length = slash == 0 || (slash == 2 && driveAbsolute(path)) ? slash + 1 : slash;
		path.truncate(length);
	}
	return checkpoint();
}
bool PackageInputProtectionSet::check(const QString& path, bool* protectedPath)
{
	if (!m_matching) { m_matching = true; m_steps = 0; }
	if (!checkpoint()) { return false; }
	if (!protectedPath || path.isEmpty() || path.size() > pathCharacterCeiling || !path.isValidUtf16() || path.contains(QChar(0))) {
		return refuse(QCoreApplication::translate("VibeStudioPackageStaging", "A package output path is invalid or too long for source protection checks."));
	}
	// Output paths use this host's filesystem interpretation. Foreign absolute
	// provenance in the set cannot become a root relative to this directory.
	const auto absolute = QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
	if (!validStoredPath(absolute) || !nativeAbsolute(absolute)) {
		return refuse(QCoreApplication::translate("VibeStudioPackageStaging", "A package output path is invalid or too long for source protection checks."));
	}
	bool found = false;
	if (!containsAncestor(absolute, &found)) { return false; }
	if (!found) {
		QString resolved;
		if (!resolve(absolute, &resolved) || (!resolved.isEmpty() && !containsAncestor(resolved, &found))) { return false; }
	}
	if (!found) {
		const auto identity = inspectPackagePathIdentity(absolute, m_portableIdentity);
		if (!identity.error.isEmpty()) { return refuse(identity.error); }
		// New and independent destinations cannot alias a different regular input.
		if (identity.regular && identity.links > 1 &&
			(!prepareIdentities() || !matchesIdentity(absolute, identity.key, &found))) { return false; }
	}
	if (!checkpoint()) { return false; }
	*protectedPath = found; return true;
}

bool PackageInputProtectionSet::prepareIdentities()
{
	if (m_identityReady) { return checkpoint(); }
	QStringList pending;
	QSet<QString> queued;
	qint64 bytes = 0;
	const auto enqueue = [&](const QString& path) {
		if (!checkpoint()) { return false; }
		if (!nativeAbsolute(path)) { return true; }
		QString resolved;
		if (!resolve(path, &resolved)) { return false; }
		const auto key = pathKey(resolved);
		if (queued.contains(key)) { return true; }
		// Account the retained path, key and container record before admitting it.
		const qint64 cost = resolved.size() * qint64(sizeof(QChar)) * 2 + 128;
		if (queued.size() >= m_maximumPaths || cost > m_maximumBytes - bytes) {
			return refuse(QCoreApplication::translate("PackageInputProtection",
				"Filesystem source protection exceeds the limit of %1 paths or %2 metadata bytes. Choose a new independent output.").arg(m_maximumPaths).arg(m_maximumBytes));
		}
		bytes += cost; queued.insert(key); pending.append(resolved); return true;
	};
	for (const auto& path : m_paths) { if (!enqueue(path)) { return false; } }
	for (qsizetype index = 0; index < pending.size(); ++index) {
		if (!checkpoint()) { return false; }
		const auto path = pending[index];
		const auto identity = inspectPackagePathIdentity(path, m_portableIdentity);
		if (!identity.error.isEmpty()) { return refuse(identity.error); }
		if (identity.regular) {
			m_identityPaths.append(path);
			if (identity.key.isEmpty()) { m_portablePaths.append(path); }
			else { m_identityKeys.insert(identity.key); }
		} else if (identity.directory) {
			std::error_code error;
			std::filesystem::directory_iterator entry(packageNativeFilesystemPath(path), error), end;
			while (!error && entry != end) {
				if (!enqueue(packageQtFilesystemPath(entry->path()))) { return false; }
				entry.increment(error);
			}
			if (error) {
				return refuse(QCoreApplication::translate("PackageInputProtection", "Cannot enumerate a protected source folder: %1.").arg(path));
			}
		}
	}
	m_identityReady = true;
	return checkpoint();
}

bool PackageInputProtectionSet::matchesIdentity(const QString& path, const QByteArray& key, bool* found)
{
	if (!key.isEmpty() && m_identityKeys.contains(key)) { *found = true; return checkpoint(); }
	const auto& candidates = key.isEmpty() ? m_identityPaths : m_portablePaths;
	for (const auto& input : candidates) {
		if (!checkpoint()) { return false; }
		std::error_code error;
		const bool equivalent = std::filesystem::equivalent(packageNativeFilesystemPath(path), packageNativeFilesystemPath(input), error);
		if (error) {
			return refuse(QCoreApplication::translate("PackageInputProtection", "Cannot compare protected file identities: %1.").arg(input));
		}
		if (equivalent) { *found = true; return checkpoint(); }
	}
	return checkpoint();
}
bool PackageInputProtectionSet::protects(const QString& path)
{
	bool found = false;
	return !check(path, &found) || found;
}
} // namespace vibestudio
