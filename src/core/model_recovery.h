#pragma once

#include "core/model_document.h"

#include <QDateTime>
#include <functional>

namespace vibestudio
{

struct ModelRecoverySnapshot
{
	ModelMesh mesh;
	ModelSelection selection;
	int frame = 0;
	QString title;
	QString sourcePath;
	QByteArray sourceSha256;
};

struct ModelRecoveryRecord
{
	QString path, id, title, sourcePath;
	QByteArray sourceSha256, payloadSha256;
	QDateTime writtenUtc;
	qint64 ownerProcessId = 0;
	qint64 payloadBytes = 0;
	int frame = 0;
	QString error;
	// Header validity only. Restoring additionally verifies the payload hash,
	// complete mesh validation and saved component selection.
	bool isValid() const { return !id.isEmpty() && error.isEmpty(); }
};

struct ModelRecoveryScan
{
	QVector<ModelRecoveryRecord> records;
	bool limited = false;
	QString error;
};

QString modelRecoveryDirectory();
QString modelRecoveryPath(const QString &directory, const QString &id);

// Versioned binary envelope: a small JSON header followed by the complete
// editable mesh JSON. Listing reads headers only, without loading mesh payloads.
// Sources are provenance, never write destinations. A restored document must
// become an unsaved draft and pass the normal Save As/overwrite checks.
QString writeModelRecovery(const ModelRecoverySnapshot &snapshot, const QString &directory, const QString &id, QString *error = nullptr,
						   const std::function<bool()> &cancelled = {});
ModelRecoveryRecord inspectModelRecovery(const QString &path, ModelRecoverySnapshot *restored = nullptr,
										 const std::function<bool()> &cancelled = {});
ModelRecoveryScan listModelRecoveries(const QString &directory, const std::function<bool()> &cancelled = {});
bool removeModelRecovery(const QString &directory, const QString &id, QString *error = nullptr);

} // namespace vibestudio
