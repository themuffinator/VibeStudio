#pragma once

#include "core/model_assembly.h"

#include <QDateTime>
#include <memory>

class QLockFile;

namespace vibestudio
{
inline constexpr qint64 modelAssemblyRecoveryByteLimit = 2 * 1024 * 1024;
inline constexpr qint64 modelAssemblyRecoveryStorageLimit = 32 * 1024 * 1024;
inline constexpr int modelAssemblyRecoveryCountLimit = 32;
inline constexpr int modelAssemblyRecoveryScanLimit = 128;

struct ModelAssemblyRecoverySnapshot
{
	ModelAssembly assembly;
	QString directory, selectedPart;
	double seconds = 0;
	QString sourcePath;
	QByteArray sourceSha256;
};
struct ModelAssemblyRecoveryRecord
{
	QString id, path, title, sourcePath, error;
	QByteArray sha256, sourceSha256;
	QDateTime writtenUtc;
	qint64 bytes = 0;
	int parts = 0;
	double seconds = 0;
	bool sessionFilePresent = false;
	bool isValid() const { return error.isEmpty() && sha256.size() == 32 && writtenUtc.isValid(); }
};
struct ModelAssemblyRecoveryScan
{
	QVector<ModelAssemblyRecoveryRecord> records;
	qint64 totalBytes = 0;
	bool limited = false;
	QString error;
};

QString modelAssemblyRecoveryDirectory();
QString modelAssemblyRecoveryPath(const QString &directory, const QString &id);
QByteArray modelAssemblyRecoveryFingerprint(const ModelAssemblyRecoverySnapshot &snapshot);
ModelAssemblyRecoveryRecord inspectModelAssemblyRecovery(const QString &path, ModelAssemblyRecoverySnapshot *snapshot = nullptr,
														 const ModelWorkControl &control = {});
ModelAssemblyRecoveryScan listModelAssemblyRecoveries(const QString &directory, const ModelWorkControl &control = {});
QJsonObject modelAssemblyRecoveryJson(const ModelAssemblyRecoveryScan &scan);
// A reviewed digest is mandatory even for invalid copies. Active editors hold
// a lease; a discard can never remove their copy. Dry runs acquire no locks.
bool discardModelAssemblyRecovery(const QString &directory, const QString &id, const QByteArray &expectedSha256, bool dryRun,
								  QString *error = nullptr, const ModelWorkControl &control = {});
bool restoreModelAssemblyRecovery(const QString &path, const QByteArray &expectedSha256, ModelAssemblyDocument *document,
								  ModelAssemblyRecoverySnapshot *snapshot = nullptr, QString *error = nullptr,
								  const ModelWorkControl &control = {});

// One owner per UUID. The lease survives for the whole dirty editing session;
// stale-process handling belongs to Qt, never a PID supplied by the document.
class ModelAssemblyRecoverySession final
{
  public:
	~ModelAssemblyRecoverySession();
	QString id() const { return m_id; }
	QString path() const;
	bool write(const ModelAssemblyRecoverySnapshot &snapshot, QString *error = nullptr, const ModelWorkControl &control = {});
	bool retire(QString *error = nullptr);
	static std::shared_ptr<ModelAssemblyRecoverySession> acquire(const QString &directory, const QString &id, QString *error = nullptr);

  private:
	ModelAssemblyRecoverySession() = default;
	QString m_directory, m_id;
	QByteArray m_writtenSha256;
	std::unique_ptr<QLockFile> m_lease;
};
} // namespace vibestudio
