#include "core/package_content.h"
#include "core/package_draft_access.h"
#include "core/package_import_store.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFileInfo>

#include <algorithm>
#include <cstring>
#include <utility>

namespace vibestudio {
namespace {
bool stopped(const PackageReadControl& control, QString* error)
{
	if (!control.isCancelled || !control.isCancelled()) { return false; }
	if (error) { *error = QCoreApplication::translate("VibeStudioPackageContent", "Package source verification cancelled."); }
	return true;
}
QString changedMessage(const QString& path)
{
	return QCoreApplication::translate("VibeStudioPackageContent", "Package source content changed: %1. Reopen or restage it before continuing.").arg(path);
}
} // namespace

bool PackageFileIdentity::matchesMetadata() const
{
	const QFileInfo current(path);
	return current.isFile() && current.canonicalFilePath() == resolvedPath && current.size() == size
		&& current.lastModified().toUTC() == modifiedUtc;
}

PackageFileIdentityPtr capturePackageFileIdentity(const QString& path, QString* error, const PackageReadControl& control, qint64 maximumHashBytes, bool* limitExceeded)
{
	if (limitExceeded) { *limitExceeded = false; }
	if (error) { error->clear(); }
	if (stopped(control, error)) { return {}; }
	const QFileInfo info(path);
	if (!info.isFile() || info.size() < 0 || info.canonicalFilePath().isEmpty()) {
		if (error) { *error = QCoreApplication::translate("VibeStudioPackageContent", "Package source is not a readable regular file: %1").arg(path); }
		return {};
	}
	const qint64 chunks = info.size() / PackageFileIdentity::chunkBytes + (info.size() % PackageFileIdentity::chunkBytes != 0);
	if (maximumHashBytes >= 0 && chunks > maximumHashBytes / 32) {
		if (limitExceeded) { *limitExceeded = true; }
		if (error) { *error = QCoreApplication::translate("VibeStudioPackageContent", "Source fingerprint metadata exceeds the remaining %1-byte indexing limit: %2").arg(maximumHashBytes).arg(path); }
		return {};
	}
	auto identity = std::make_shared<PackageFileIdentity>();
	identity->path = info.absoluteFilePath();
	identity->resolvedPath = info.canonicalFilePath();
	identity->size = info.size();
	identity->modifiedUtc = info.lastModified().toUTC();
	QFile input(identity->path);
	if (!input.open(QIODevice::ReadOnly)) { if (error) { *error = input.errorString(); } return {}; }
	QCryptographicHash hash(QCryptographicHash::Sha256);
	qint64 completed = 0;
	if (control.progress) { control.progress(identity->path, completed, identity->size); }
	while (completed < identity->size) {
		if (stopped(control, error)) { return {}; }
		const qint64 count = std::min(PackageFileIdentity::chunkBytes, identity->size - completed);
		const QByteArray chunk = input.read(count);
		if (chunk.size() != count || input.error() != QFileDevice::NoError) {
			if (error) { *error = QCoreApplication::translate("VibeStudioPackageContent", "Unable to fingerprint the complete source file: %1").arg(identity->path); }
			return {};
		}
		hash.addData(chunk);
		identity->chunkHashes.append(QCryptographicHash::hash(chunk, QCryptographicHash::Sha256));
		completed += count;
		if (control.progress) { control.progress(identity->path, completed, identity->size); }
	}
	if (stopped(control, error)) { return {}; }
	if (!identity->matchesMetadata()) { if (error) { *error = changedMessage(identity->path); } return {}; }
	identity->sha256 = hash.result();
	return identity;
}

PackageFileIdentityPtr retainPackageFileContent(const PackageFileIdentityPtr& identity, QString* error,
	const PackageReadControl& control, const QString& directory)
{
	if (error) { error->clear(); }
	if (!identity || stopped(control, error)) { return {}; }
	auto reservation = reservePackageImport(identity, error, control, directory);
	if (!reservation) { return {}; }
	PackageContentDevice input(identity, control);
	if (!input.open()) { if (error) { *error = input.errorString(); } return {}; }
	QFile output(reservation->path());
	if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) { if (error) { *error = output.errorString(); } return {}; }
	qint64 copied = 0;
	if (control.progress) { control.progress(identity->path, 0, identity->size); }
	while (copied < identity->size) {
		if (stopped(control, error)) { return {}; }
		const qint64 count = std::min(PackageFileIdentity::chunkBytes, identity->size - copied);
		const QByteArray bytes = input.read(count);
		if (bytes.size() != count || input.failed()) { if (error) { *error = input.errorString(); } return {}; }
		if (output.write(bytes) != bytes.size()) {
			if (error) { *error = QCoreApplication::translate("VibeStudioPackageContent", "Unable to retain the imported file: %1").arg(output.errorString()); } return {};
		}
		copied += count;
		if (control.progress) { control.progress(identity->path, copied, identity->size); }
	}
	if (stopped(control, error)) { return {}; }
	if (!identity->matchesMetadata()) { if (error) { *error = changedMessage(identity->path); } return {}; }
	if (!output.flush()) { if (error) { *error = output.errorString(); } return {}; }
	output.close();
	PackageReadControl verify = control;
	verify.progress = [control, identity](const QString&, qint64 done, qint64 total) {
		if (control.progress) { control.progress(identity->path, done, total); }
	};
	const auto captured = capturePackageFileIdentity(output.fileName(), error, verify, identity->chunkHashes.size());
	if (!captured || stopped(control, error)) { return {}; }
	if (captured->sha256 != identity->sha256 || captured->size != identity->size) {
		if (error) { *error = QCoreApplication::translate("VibeStudioPackageContent", "The retained import failed its content check."); } return {};
	}
	auto retained = std::make_shared<PackageFileIdentity>(*captured);
	retained->storageDirectory = reservation->directory();
	retained->storage = reservation->retain(*captured);
	if (!retained->storage) { if (error) { *error = QCoreApplication::translate("VibeStudioPackageContent", "The working import reservation did not match its verified content."); } return {}; }
	return retained;
}

PackageContentDevice::PackageContentDevice(PackageFileIdentityPtr identity, PackageReadControl control)
	: m_identity(std::move(identity)), m_control(std::move(control))
{
	if (m_identity) { m_file.setFileName(m_identity->path); }
}

bool PackageContentDevice::fail(const QString& message)
{
	m_failed = true;
	setErrorString(message);
	return false;
}

bool PackageContentDevice::open(OpenMode mode)
{
	close();
	m_failed = false;
	if (mode != QIODevice::ReadOnly || !m_identity) {
		return fail(QCoreApplication::translate("VibeStudioPackageContent", "Verified package reads require a captured source identity and read-only mode."));
	}
	// A directory replacement can preserve every payload statistic. An older
	// snapshot must keep using the directory whose native reader lease it owns.
	if ((m_identity->draftAccess && !m_identity->draftAccess->matchesDirectory()) || !m_identity->matchesMetadata()) {
		return fail(changedMessage(m_identity->path));
	}
	QString cancelError;
	if (stopped(m_control, &cancelError)) { return fail(cancelError); }
	if (!m_file.open(QIODevice::ReadOnly)) { return fail(m_file.errorString()); }
	return QIODevice::open(QIODevice::ReadOnly | QIODevice::Unbuffered);
}

void PackageContentDevice::close()
{
	m_file.close();
	m_chunk.clear();
	m_chunkIndex = -1;
	m_offset = 0;
	QIODevice::close();
}

qint64 PackageContentDevice::size() const { return m_identity ? m_identity->size : 0; }
qint64 PackageContentDevice::bytesAvailable() const { return isOpen() ? size() - pos() : 0; }
bool PackageContentDevice::failed() const { return m_failed; }
QFileDevice::FileError PackageContentDevice::error() const { return m_failed ? QFileDevice::ReadError : m_file.error(); }

bool PackageContentDevice::seek(qint64 position)
{
	if (!isOpen() || position < 0 || position > size() || m_failed) { return false; }
	if (!QIODevice::seek(position)) { return false; }
	m_offset = position;
	return true;
}

bool PackageContentDevice::loadChunk(qint64 index)
{
	QString error;
	if (stopped(m_control, &error)) { return fail(error); }
	if (index == m_chunkIndex) { return true; }
	if (!m_identity->matchesMetadata()) { return fail(changedMessage(m_identity->path)); }
	const qint64 offset = index * PackageFileIdentity::chunkBytes;
	const qint64 count = std::min(PackageFileIdentity::chunkBytes, size() - offset);
	if (count <= 0 || !m_file.seek(offset)) { return fail(m_file.errorString()); }
	QByteArray chunk = m_file.read(count);
	if (chunk.size() != count || m_file.error() != QFileDevice::NoError) { return fail(m_file.errorString()); }
	const QByteArray expected = m_identity->chunkHashes.mid(index * 32, 32);
	if (expected.size() != 32 || QCryptographicHash::hash(chunk, QCryptographicHash::Sha256) != expected) {
		return fail(changedMessage(m_identity->path));
	}
	m_chunk = std::move(chunk);
	m_chunkIndex = index;
	return true;
}

qint64 PackageContentDevice::readData(char* data, qint64 maximum)
{
	if (m_failed) { return -1; }
	qint64 copied = 0;
	while (copied < maximum && m_offset < size()) {
		const qint64 index = m_offset / PackageFileIdentity::chunkBytes;
		if (!loadChunk(index)) { return copied > 0 ? copied : -1; }
		const qint64 start = m_offset % PackageFileIdentity::chunkBytes;
		const qint64 count = std::min(maximum - copied, m_chunk.size() - start);
		std::memcpy(data + copied, m_chunk.constData() + start, static_cast<size_t>(count));
		m_offset += count;
		copied += count;
	}
	return copied;
}

qint64 PackageContentDevice::writeData(const char*, qint64) { return -1; }

bool verifyPackageFileIdentity(const PackageFileIdentityPtr& identity, QString* error, const PackageReadControl& control)
{
	if (error) { error->clear(); }
	PackageContentDevice input(identity, control);
	if (!input.open()) { if (error) { *error = input.errorString(); } return false; }
	qint64 completed = 0;
	if (control.progress) { control.progress(identity->path, completed, identity->size); }
	while (completed < identity->size) {
		const qint64 count = std::min(PackageFileIdentity::chunkBytes, identity->size - completed);
		if (input.read(count).size() != count || input.failed()) { if (error) { *error = input.errorString(); } return false; }
		completed += count;
		if (control.progress) { control.progress(identity->path, completed, identity->size); }
	}
	if (stopped(control, error)) { return false; }
	if ((identity->draftAccess && !identity->draftAccess->matchesDirectory()) || !identity->matchesMetadata()) {
		if (error) { *error = changedMessage(identity->path); } return false;
	}
	return true;
}

} // namespace vibestudio
