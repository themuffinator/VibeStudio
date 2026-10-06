#pragma once

#include "core/model_work.h"

#include <QByteArray>

namespace vibestudio
{
inline constexpr qint64 modelFileByteLimit = 64 * 1024 * 1024;

struct ModelWriteTarget
{
	QString path, resolvedPath;
	QByteArray sha256;
	bool existed = false;
	QString error;
	bool isValid() const { return !path.isEmpty() && !resolvedPath.isEmpty() && error.isEmpty(); }
};

bool modelPathsReferToSameFile(const QString &first, const QString &second);
bool readModelFile(const QString &path, QByteArray *bytes, QString *error = nullptr, const ModelWorkControl &control = {});
ModelWriteTarget inspectModelWriteTarget(const QString &path, const ModelWorkControl &control = {});
// The reviewed identity is rechecked before publication. New destinations use
// an atomic no-replace rename; existing destinations use QSaveFile, a writer
// lock, and a final content/path check. A noncooperating writer can still race
// an existing-file replacement after that final check.
bool writeModelFile(const ModelWriteTarget &target, const QByteArray &bytes, QString *error = nullptr,
					const ModelWorkControl &control = {});
} // namespace vibestudio
