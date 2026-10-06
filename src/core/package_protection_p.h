#pragma once

#include "core/package_content.h"
#include <QSet>
#include <QStringList>

namespace vibestudio {
// Separate bounded set of absolute input roots. Document accounting also charges
// every retained root and UTF-16 byte. Roots protect their descendants even if
// the original directory has disappeared. No file is opened for writing here.
class PackageInputProtectionSet final {
public:
	static constexpr qsizetype pathCeiling = 250000;
	static constexpr qint64 byteCeiling = 64ll * 1024 * 1024;
	static constexpr qsizetype pathCharacterCeiling = 32768;
	PackageInputProtectionSet(qsizetype maximumPaths, qint64 maximumBytes, QString* error,
		PackageReadControl control = {}, QString implicitDirectory = {});
	bool add(const QString& path, bool captureAliases = true);
	bool finish(bool enumerationComplete = true);
	[[nodiscard]] QStringList paths() const { return m_paths; }
	[[nodiscard]] qint64 metadataBytes() const { return m_bytes; }
	// Queries the captured lexical/resolved roots without rescanning them.
	// False means cancellation/refusal; never treat that as an unprotected path.
	bool check(const QString& path, bool* protectedPath);
	[[nodiscard]] bool protects(const QString& path);
	[[nodiscard]] QString errorString() const { return m_errorText; }
	static bool validStoredPath(const QString& path);
private:
	bool checkpoint();
	bool retain(const QString& path);
	bool resolve(const QString& absolute, QString* resolved);
	bool containsAncestor(QString path, bool* found);
	bool refuse(const QString& message);
	qsizetype m_maximumPaths, m_steps = 0;
	qint64 m_maximumBytes, m_bytes = 0;
	QString* m_error;
	PackageReadControl m_control;
	QString m_implicitDirectory;
	QString m_errorText;
	QStringList m_paths;
	QSet<QString> m_keys;
	bool m_cancelled = false, m_failed = false, m_matching = false;
};
} // namespace vibestudio
