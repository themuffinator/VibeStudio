#include "core/audio_publication.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>

namespace vibestudio
{
namespace
{
QString message(const char *text) { return QCoreApplication::translate("AudioSession", text); }
bool cancelled(const AudioWorkControl &control, QString *error)
{
	if (!control.cancelled || !control.cancelled()) {
		return false;
	}
	if (error)
		*error = message(QT_TRANSLATE_NOOP("AudioSession", "Session operation cancelled."));
	return true;
}
QString resolved(const QString &path)
{
	const QFileInfo info(path);
	if (info.exists()) {
		return info.canonicalFilePath();
	}
	const QString parent = info.dir().canonicalPath();
	return parent.isEmpty() ? info.absoluteFilePath() : QDir(parent).filePath(info.fileName());
}
bool same(const QString &left, const QString &right)
{
#ifdef Q_OS_WIN
	return left.compare(right, Qt::CaseInsensitive) == 0;
#else
	return left == right;
#endif
}
} // namespace
AudioProjectSaveReport commitAudioOutput(const AudioProjectSaveRequest &request, const QString &suffix,
                                         QStringList protectedPaths, qint64 expectedByteLimit,
                                         const AudioWorkControl &control, const AudioOutputProducer &produce)
{
	AudioProjectSaveReport report;
	protectedPaths << request.protectedPath;
	const QFileInfo destination(request.path);
	const QString target = resolved(request.path);
	if (request.path.trimmed().isEmpty() || destination.suffix().compare(suffix, Qt::CaseInsensitive) != 0 ||
	    !destination.dir().exists() || destination.isSymLink() || destination.isJunction() || destination.isDir()) {
		report.error = message(QT_TRANSLATE_NOOP(
		    "AudioSession", "Choose a regular file with the required extension in an existing folder."));
		return report;
	}
	for (const auto &path : protectedPaths) {
		if (!path.isEmpty() && audioPathsReferToSameFile(target, path)) {
			report.error = message(QT_TRANSLATE_NOOP(
			    "AudioSession", "Choose another output path to preserve imported media and the source session."));
			return report;
		}
	}
	const bool guarded = !request.expected.sha256.isEmpty();
	if (guarded && (request.expected.sha256.size() != 32 || !same(target, request.expected.canonicalPath))) {
		report.conflict = true;
		report.error =
		    message(QT_TRANSLATE_NOOP("AudioSession", "The session destination changed. Save a separate copy."));
		return report;
	}
	QLockFile lock(target + QStringLiteral(".lock"));
	if (!request.dryRun && !lock.tryLock(0)) {
		report.conflict = true;
		report.error = message(
		    QT_TRANSLATE_NOOP("AudioSession", "Another writer owns this output. Try again or save a separate copy."));
		return report;
	}
	const auto check = [&]() {
		const QFileInfo now(request.path);
		if (now.isSymLink() || now.isJunction() || now.isDir() || !same(target, resolved(request.path))) {
			return false;
		}
		if (!guarded) {
			return request.overwrite || !now.exists();
		}
		QFile existing(target);
		if (!existing.open(QIODevice::ReadOnly) || existing.size() > expectedByteLimit) {
			return false;
		}
		QCryptographicHash hash(QCryptographicHash::Sha256);
		while (!existing.atEnd()) {
			if (cancelled(control, &report.error)) {
				return false;
			}
			const auto bytes = existing.read(1024 * 1024);
			if (existing.error() != QFileDevice::NoError) {
				return false;
			}
			hash.addData(bytes);
		}
		return hash.result() == request.expected.sha256;
	};
	if (!check()) {
		report.conflict = true;
		report.error = message(
		    QT_TRANSLATE_NOOP("AudioSession", "The output exists or changed on disk. Review it before overwriting."));
		return report;
	}
	if (cancelled(control, &report.error)) {
		return report;
	}
	QSaveFile output(target);
	output.setDirectWriteFallback(false);
	if (!request.dryRun && !output.open(QIODevice::WriteOnly)) {
		report.error = output.errorString();
		return report;
	}
	QCryptographicHash digest(QCryptographicHash::Sha256);
	const AudioOutputEmit emitBytes = [&](const QByteArray &bytes) {
		if (cancelled(control, &report.error)) {
			return false;
		}
		if (!request.dryRun && output.write(bytes) != bytes.size()) {
			report.error = output.errorString();
			return false;
		}
		digest.addData(bytes);
		return true;
	};
	if (!produce(emitBytes, &report.error) || cancelled(control, &report.error)) {
		if (!request.dryRun) {
			output.cancelWriting();
		}
		return report;
	}
	if (!check()) {
		if (!request.dryRun) {
			output.cancelWriting();
		}
		report.conflict = true;
		report.error = message(QT_TRANSLATE_NOOP(
		    "AudioSession", "The output changed during processing. Its external contents are preserved."));
		return report;
	}
	if (!request.dryRun && !output.commit()) {
		report.error = output.errorString();
		return report;
	}
	report.succeeded = true;
	report.written = !request.dryRun;
	if (report.written) {
		report.identity = {destination.absoluteFilePath(), target, digest.result()};
	}
	return report;
}
} // namespace vibestudio
