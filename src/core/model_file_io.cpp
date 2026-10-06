#include "core/model_file_io.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QTemporaryFile>

#include <algorithm>

namespace vibestudio
{
namespace
{
constexpr qint64 blockSize = 256 * 1024;
bool fail(QString *error, const QString &message)
{
	if (error)
	{
		*error = message;
	}
	return false;
}
bool sameResolved(const QString &first, const QString &second)
{
#ifdef Q_OS_WIN
	constexpr auto sensitivity = Qt::CaseInsensitive;
#else
	constexpr auto sensitivity = Qt::CaseSensitive;
#endif
	return !first.isEmpty() && !second.isEmpty() && first.compare(second, sensitivity) == 0;
}
QString resolvedDestination(const QString &path)
{
	const QFileInfo target(path);
	if (target.exists())
	{
		return target.canonicalFilePath();
	}
	// Resolve through the closest existing directory without creating anything.
	// Later checks detect a newly introduced link or a moved parent directory.
	QString parent = target.absolutePath();
	while (!QFileInfo::exists(parent))
	{
		const auto next = QFileInfo(parent).absolutePath();
		if (next == parent)
		{
			return {};
		}
		parent = next;
	}
	const QFileInfo directory(parent);
	if (!directory.isDir())
	{
		return {};
	}
	const auto canonical = directory.canonicalFilePath();
	return canonical.isEmpty()
			   ? QString()
			   : QDir::cleanPath(QDir(canonical).absoluteFilePath(QDir(parent).relativeFilePath(target.absoluteFilePath())));
}
bool writeBlocks(QFileDevice &file, const QByteArray &bytes, QString *error, const ModelWorkControl &control)
{
	for (qint64 offset = 0; offset < bytes.size();)
	{
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Writing, offset, bytes.size(), error))
		{
			return false;
		}
		const auto count = std::min(blockSize, bytes.size() - offset);
		if (file.write(bytes.constData() + offset, count) != count)
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "The model output could not be written: %1")
								   .arg(file.errorString()));
		}
		offset += count;
	}
	return modelWorkCheckpoint(control, ModelWorkPhase::Writing, bytes.size(), bytes.size(), error);
}
} // namespace

bool modelPathsReferToSameFile(const QString &first, const QString &second)
{
	if (first.isEmpty() || second.isEmpty())
	{
		return false;
	}
	return sameResolved(QDir::cleanPath(QFileInfo(first).absoluteFilePath()), QDir::cleanPath(QFileInfo(second).absoluteFilePath())) ||
		   sameResolved(resolvedDestination(first), resolvedDestination(second));
}

bool readModelFile(const QString &path, QByteArray *bytes, QString *error, const ModelWorkControl &control)
{
	if (error)
	{
		error->clear();
	}
	if (!bytes || !modelWorkCheckpoint(control, ModelWorkPhase::Reading, 0, 0, error))
	{
		return false;
	}
	QFile file(path);
	if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly) || file.size() > modelFileByteLimit)
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "The model source cannot be read or exceeds 64 MiB."));
	}
	QByteArray result;
	const auto expected = file.size();
	while (!file.atEnd())
	{
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, result.size(), expected, error))
		{
			return false;
		}
		const auto part = file.read(std::min(blockSize, modelFileByteLimit + 1 - result.size()));
		if (part.isEmpty() || part.size() + result.size() > modelFileByteLimit || file.error() != QFileDevice::NoError)
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "The model source changed or failed while reading."));
		}
		result += part;
	}
	if (result.size() != expected || !modelWorkCheckpoint(control, ModelWorkPhase::Reading, result.size(), expected, error))
	{
		if (result.size() != expected)
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "The model source changed or failed while reading."));
		}
		return false;
	}
	*bytes = std::move(result);
	return true;
}

ModelWriteTarget inspectModelWriteTarget(const QString &path, const ModelWorkControl &control)
{
	ModelWriteTarget target;
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, 0, 0, &target.error))
	{
		return target;
	}
	if (path.trimmed().isEmpty())
	{
		target.error = QCoreApplication::translate("VibeStudioModelDocument", "Choose a model output path.");
		return target;
	}
	const QFileInfo info(path);
	target.path = QDir::cleanPath(info.absoluteFilePath());
	target.existed = info.exists();
	if (info.isSymLink() || info.isJunction() || (target.existed && !info.isFile()))
	{
		target.error =
			QCoreApplication::translate("VibeStudioModelDocument", "Model output must be a regular file, not a link or directory.");
		return target;
	}
	target.resolvedPath = resolvedDestination(target.path);
	if (target.resolvedPath.isEmpty())
	{
		target.error = QCoreApplication::translate("VibeStudioModelDocument", "The model output folder cannot be resolved.");
		return target;
	}
	if (target.existed)
	{
		QByteArray bytes;
		if (!readModelFile(target.path, &bytes, &target.error, control))
		{
			return target;
		}
		target.sha256 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
	}
	return target;
}

bool writeModelFile(const ModelWriteTarget &target, const QByteArray &bytes, QString *error, const ModelWorkControl &control)
{
	if (error)
	{
		error->clear();
	}
	if (!target.isValid() || bytes.isEmpty() || bytes.size() > modelFileByteLimit)
	{
		return fail(
			error, target.error.isEmpty()
					   ? QCoreApplication::translate("VibeStudioModelDocument",
													 "A reviewed destination and nonempty model output no larger than 64 MiB are required.")
					   : target.error);
	}
	const auto unchanged = [&]
	{
		const auto current = inspectModelWriteTarget(target.path, control);
		if (!current.isValid())
		{
			return fail(error, current.error);
		}
		if (current.existed != target.existed || current.sha256 != target.sha256 ||
			!sameResolved(current.resolvedPath, target.resolvedPath))
		{
			return fail(error,
						QCoreApplication::translate("VibeStudioModelDocument",
													"The model destination changed after it was reviewed. No output was committed."));
		}
		return true;
	};
	if (!unchanged())
	{
		return false;
	}
	const auto directory = QFileInfo(target.resolvedPath).absolutePath();
	if (!QDir().mkpath(directory))
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "The model output folder could not be created."));
	}
	QLockFile lock(target.resolvedPath + QStringLiteral(".vibestudio-model.lock"));
	if (!lock.tryLock(0))
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelDocument",
													   "Another model writer is using this destination. Try again after it finishes."));
	}
	if (!unchanged())
	{
		return false;
	}
	if (target.existed)
	{
		QSaveFile file(target.resolvedPath);
		file.setDirectWriteFallback(false);
		if (!file.open(QIODevice::WriteOnly))
		{
			return fail(error, file.errorString());
		}
		if (!writeBlocks(file, bytes, error, control) || !modelWorkCheckpoint(control, ModelWorkPhase::Committing, 0, 1, error) ||
			!unchanged())
		{
			file.cancelWriting();
			return false;
		}
		if (!file.commit())
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "The model output could not be committed: %1")
								   .arg(file.errorString()));
		}
	}
	else
	{
		QTemporaryFile file(QDir(directory).filePath(QStringLiteral(".vibestudio-model-XXXXXX.tmp")));
		if (!file.open())
		{
			return fail(error, file.errorString());
		}
		if (!writeBlocks(file, bytes, error, control) || !file.flush())
		{
			if (error && error->isEmpty())
			{
				*error = file.errorString();
			}
			return false;
		}
		file.close();
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Committing, 0, 1, error) || !unchanged())
		{
			return false;
		}
		// QTemporaryFile::rename only publishes atomically and refuses an existing
		// destination; unlike QFile::rename it never falls back to copy/delete.
		// https://doc.qt.io/qt-6/qtemporaryfile.html#rename
		if (!file.rename(target.resolvedPath))
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelDocument",
														   "The new model output could not be published without replacement: %1")
								   .arg(file.errorString()));
		}
		file.setAutoRemove(false);
	}
	// Publication has succeeded. Late cancellation cannot turn a committed save
	// into a reported failure or leave the editor believing its source is dirty.
	if (control.progress)
	{
		control.progress(ModelWorkPhase::Committing, 1, 1);
	}
	return true;
}
} // namespace vibestudio
