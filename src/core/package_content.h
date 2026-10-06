#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QIODevice>
#include <QString>

#include <functional>
#include <memory>

namespace vibestudio {

struct PackageImportOptions;
struct PackageDraftSaveLimits;

struct PackageReadControl {
	std::function<bool()> isCancelled;
	// Called synchronously on the thread performing the operation. GUI callers
	// must run source reads on a worker and marshal progress to their UI.
	// Payload callbacks carry a path and byte counts. Metadata preparation and
	// source-protection checks carry a localized phase and work counts, with a
	// zero total when unknown. Consumers must not label those counts as bytes.
	std::function<void(const QString& pathOrPhase, qint64 completed, qint64 total)> progress;
	// Used only by live file imports. Readers and no-write plans ignore it.
	std::shared_ptr<const PackageImportOptions> importOptions;
	std::shared_ptr<const PackageDraftSaveLimits> draftLimits; // Used only by ordinary draft saves.
};

class PackageContentStorage;
class PackageDraftAccess;

// Content identity is immutable and shareable between independent readers.
// Concatenated SHA-256 hashes cost 32 bytes per 64 KiB of source content. No
// payload bytes or open data-file handles are retained by an identity. An owned
// import keeps its independent temporary file alive until the last reader,
// document or history reference releases it.
struct PackageFileIdentity {
	static constexpr qint64 chunkBytes = 65536;
	QString path;
	QString resolvedPath;
	QString storageDirectory; // Owned backing store, protected from package output.
	qint64 size = 0;
	QDateTime modifiedUtc;
	QByteArray sha256;
	QByteArray chunkHashes;
	std::shared_ptr<const PackageContentStorage> storage;
	std::shared_ptr<const PackageDraftAccess> draftAccess; // No-write directory lease shared by a draft snapshot.
	[[nodiscard]] bool matchesMetadata() const;
};

using PackageFileIdentityPtr = std::shared_ptr<const PackageFileIdentity>;
// A nonnegative limit bounds retained chunk hashes before any source read.
// limitExceeded distinguishes admission refusal from an unreadable/changed
// source, so staging cannot turn a late limit failure into an accepted edit.
PackageFileIdentityPtr capturePackageFileIdentity(const QString& path, QString* error = nullptr, const PackageReadControl& control = {},
	qint64 maximumHashBytes = -1, bool* limitExceeded = nullptr);
bool verifyPackageFileIdentity(const PackageFileIdentityPtr& identity, QString* error = nullptr, const PackageReadControl& control = {});
// Stream a verified independent copy into a unique temporary file. The original
// is never modified, and cancellation/failure never returns a partial snapshot.
// An empty directory uses the managed store under Qt's temporary directory.
// Session leases and payload reservations cover live readers and in-flight copies.
PackageFileIdentityPtr retainPackageFileContent(const PackageFileIdentityPtr& identity, QString* error = nullptr,
	const PackageReadControl& control = {}, const QString& directory = {});

// A random-access, read-only device. Each source chunk is checked before any
// bytes from it reach a consumer, including capped previews and stored PAK/WAD
// entries that have no format checksum. A device caches only its current chunk;
// independent consumers have independent handles and buffers.
class PackageContentDevice final : public QIODevice {
public:
	explicit PackageContentDevice(PackageFileIdentityPtr identity, PackageReadControl control = {});
	bool open(OpenMode mode = QIODevice::ReadOnly) override;
	void close() override;
	bool seek(qint64 position) override;
	[[nodiscard]] qint64 size() const override;
	[[nodiscard]] qint64 bytesAvailable() const override;
	[[nodiscard]] bool failed() const;
	[[nodiscard]] QFileDevice::FileError error() const;

protected:
	qint64 readData(char* data, qint64 maximum) override;
	qint64 writeData(const char*, qint64) override;

private:
	bool loadChunk(qint64 index);
	bool fail(const QString& message);
	PackageFileIdentityPtr m_identity;
	PackageReadControl m_control;
	QFile m_file;
	QByteArray m_chunk;
	qint64 m_chunkIndex = -1;
	qint64 m_offset = 0;
	bool m_failed = false;
};

} // namespace vibestudio
