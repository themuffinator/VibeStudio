#include "core/audio_project.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QtEndian>

#include <bit>
#include <cmath>
#include <limits>

namespace vibestudio
{
namespace
{
constexpr qsizetype metadataLimit = 256 * 1024;
const QByteArray magic = QByteArrayLiteral("VSAUD\r\n\x1a");

bool fail(QString* error, const QString& message)
{
	if (error) {
		*error = message;
	}
	return false;
}

bool cancelled(const AudioWorkControl& control, QString* error)
{
	if (!control.cancelled || !control.cancelled()) {
		return false;
	}
	fail(error, QCoreApplication::translate("VibeStudioAudioProject", "Audio project operation cancelled."));
	return true;
}

bool samePath(const QString& a, const QString& b)
{
#ifdef Q_OS_WIN
	return a.compare(b, Qt::CaseInsensitive) == 0;
#else
	return a == b;
#endif
}

QString resolvedPath(const QString& path)
{
	const QFileInfo info(path);
	if (info.exists()) {
		return info.canonicalFilePath();
	}
	const QString parent = info.dir().canonicalPath();
	return parent.isEmpty() ? info.absoluteFilePath() : QDir(parent).filePath(info.fileName());
}

bool integer(const QJsonObject& object, const QString& key, qint64 minimum, qint64 maximum, qint64* result)
{
	const auto value = object.value(key);
	const double number = value.toDouble(std::numeric_limits<double>::quiet_NaN());
	if (!value.isDouble() || !std::isfinite(number) || number != std::floor(number) || number < minimum ||
	    number > maximum) {
		return false;
	}
	*result = static_cast<qint64>(number);
	return true;
}

bool hashFile(const QString& path, QByteArray* digest, QString* error)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly) || file.size() > AudioProjectByteLimit) {
		return fail(error, QCoreApplication::translate("VibeStudioAudioProject",
		                                               "The audio project is unreadable or exceeds its size limit."));
	}
	QCryptographicHash hash(QCryptographicHash::Sha256);
	if (!hash.addData(&file)) {
		return fail(error, file.errorString());
	}
	*digest = hash.result();
	return true;
}

} // namespace

QByteArray encodeAudioProject(const AudioProject& project, QString* error, const AudioWorkControl& control)
{
	if (error) {
		error->clear();
	}
	const QString problem = validateAudioClip(project.clip, true);
	if (!problem.isEmpty()) {
		fail(error, problem);
		return {};
	}
	if (project.firstFrame < 0 || project.endFrame < project.firstFrame ||
	    project.endFrame > project.clip.frameCount() || project.sourceName.size() > 8192 ||
	    project.sourcePath.size() > 8192) {
		fail(error, QCoreApplication::translate("VibeStudioAudioProject",
		                                        "Audio project selection or source metadata is invalid."));
		return {};
	}
	QJsonObject root{
	    {QStringLiteral("channels"), project.clip.channels},   {QStringLiteral("sampleRate"), project.clip.sampleRate},
	    {QStringLiteral("frames"), project.clip.frameCount()}, {QStringLiteral("selectionStart"), project.firstFrame},
	    {QStringLiteral("selectionEnd"), project.endFrame},    {QStringLiteral("sourceName"), project.sourceName},
	    {QStringLiteral("sourcePath"), project.sourcePath},    {QStringLiteral("metadata"), project.metadata}};
	const quint32 version = project.clip.markers.empty() ? 1 : 2;
	if (version == 2) {
		root.insert(QStringLiteral("markers"), audioMarkersJson(project.clip.markers));
	}
	const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);
	if (json.size() > metadataLimit) {
		fail(error, QCoreApplication::translate("VibeStudioAudioProject", "Audio project metadata exceeds 256 KiB."));
		return {};
	}
	QByteArray bytes(16 + json.size() + project.clip.samples.size() * 4, '\0');
	bytes.replace(0, 8, magic);
	qToLittleEndian<quint32>(version, bytes.data() + 8);
	qToLittleEndian<quint32>(static_cast<quint32>(json.size()), bytes.data() + 12);
	bytes.replace(16, json.size(), json);
	char* data = bytes.data() + 16 + json.size();
	for (qsizetype i = 0; i < project.clip.samples.size(); ++i) {
		if (i % 4096 == 0 && cancelled(control, error)) {
			return {};
		}
		qToLittleEndian<quint32>(std::bit_cast<quint32>(project.clip.samples[i]), data + i * 4);
	}
	bytes.append(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256));
	return bytes;
}

bool decodeAudioProject(const QByteArray& bytes, AudioProject* project, QString* error, const AudioWorkControl& control)
{
	if (error) {
		error->clear();
	}
	if (!project || bytes.size() < 52 || bytes.size() > AudioProjectByteLimit || !bytes.startsWith(magic)) {
		return fail(error,
		            QCoreApplication::translate("VibeStudioAudioProject", "Not a bounded VibeStudio audio project."));
	}
	const quint32 version = qFromLittleEndian<quint32>(bytes.constData() + 8);
	if (version != 1 && version != 2) {
		return fail(error, QCoreApplication::translate("VibeStudioAudioProject",
		                                               "This audio project version is not supported."));
	}
	const quint32 jsonSize = qFromLittleEndian<quint32>(bytes.constData() + 12);
	const qsizetype payloadEnd = bytes.size() - 32;
	if (jsonSize > metadataLimit || jsonSize > payloadEnd - 16 ||
	    QCryptographicHash::hash(QByteArrayView(bytes.constData(), payloadEnd), QCryptographicHash::Sha256) !=
	        bytes.last(32)) {
		return fail(error, QCoreApplication::translate("VibeStudioAudioProject",
		                                               "Audio project length or checksum is invalid."));
	}
	const auto json = QJsonDocument::fromJson(bytes.mid(16, jsonSize));
	const auto root = json.object();
	qint64 channels = 0, rate = 0, frames = 0;
	AudioProject next;
	if (!json.isObject() || !integer(root, QStringLiteral("channels"), 1, 8, &channels) ||
	    !integer(root, QStringLiteral("sampleRate"), 1, 384000, &rate) ||
	    !integer(root, QStringLiteral("frames"), 0, AudioSampleLimit / channels, &frames) ||
	    !integer(root, QStringLiteral("selectionStart"), 0, frames, &next.firstFrame) ||
	    !integer(root, QStringLiteral("selectionEnd"), next.firstFrame, frames, &next.endFrame) ||
	    !root.value(QStringLiteral("sourceName")).isString() || !root.value(QStringLiteral("sourcePath")).isString() ||
	    !root.value(QStringLiteral("metadata")).isObject() || payloadEnd - 16 - jsonSize != frames * channels * 4) {
		return fail(error,
		            QCoreApplication::translate("VibeStudioAudioProject",
		                                        "Audio project dimensions, selection, or metadata are invalid."));
	}
	next.sourceName = root.value(QStringLiteral("sourceName")).toString();
	next.sourcePath = root.value(QStringLiteral("sourcePath")).toString();
	next.metadata = root.value(QStringLiteral("metadata")).toObject();
	if (version == 1 && root.contains(QStringLiteral("markers"))) {
		return fail(error, QCoreApplication::translate("VibeStudioAudioProject",
		                                               "Marker metadata requires audio project version 2."));
	}
	if (version == 2 &&
	    (!root.value(QStringLiteral("markers")).isObject() ||
	     !parseAudioMarkersJson(root.value(QStringLiteral("markers")).toObject(), frames, &next.clip.markers, error))) {
		if (error && error->isEmpty()) {
			*error = QCoreApplication::translate("VibeStudioAudioProject",
			                                     "Version 2 audio projects require valid marker metadata.");
		}
		return false;
	}
	if (next.sourceName.size() > 8192 || next.sourcePath.size() > 8192) {
		return fail(
		    error, QCoreApplication::translate("VibeStudioAudioProject", "Audio project source metadata is too long."));
	}
	next.clip.channels = static_cast<int>(channels);
	next.clip.sampleRate = static_cast<int>(rate);
	next.clip.samples.resize(frames * channels);
	const char* data = bytes.constData() + 16 + jsonSize;
	for (qsizetype i = 0; i < next.clip.samples.size(); ++i) {
		if (i % 4096 == 0 && cancelled(control, error)) {
			return false;
		}
		next.clip.samples[i] = std::bit_cast<float>(qFromLittleEndian<quint32>(data + i * 4));
	}
	const QString problem = validateAudioClip(next.clip, true);
	if (!problem.isEmpty()) {
		return fail(error, problem);
	}
	*project = std::move(next);
	return true;
}

bool readAudioProject(const QString& path, AudioProject* project, AudioProjectIdentity* identity, QString* error,
                      const AudioWorkControl& control)
{
	if (error) {
		error->clear();
	}
	QFile file(path);
	const QString canonical = QFileInfo(path).canonicalFilePath();
	if (!file.open(QIODevice::ReadOnly) || file.size() > AudioProjectByteLimit) {
		return fail(error, QCoreApplication::translate("VibeStudioAudioProject",
		                                               "The audio project is unreadable or exceeds its size limit."));
	}
	const QByteArray bytes = file.read(AudioProjectByteLimit + 1);
	if (file.error() != QFileDevice::NoError) {
		return fail(error, file.errorString());
	}
	if (!decodeAudioProject(bytes, project, error, control)) {
		return false;
	}
	if (identity) {
		*identity = {QFileInfo(path).absoluteFilePath(), canonical,
		             QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)};
	}
	return true;
}

AudioProjectSaveReport writeAudioProject(const AudioProject& project, const AudioProjectSaveRequest& request,
                                         const AudioWorkControl& control)
{
	AudioProjectSaveReport report;
	const auto failure = [&](const QString& error, bool conflict = false) {
		report.error = error;
		report.conflict = conflict;
		return report;
	};
	const QFileInfo destination(request.path);
	if (request.path.trimmed().isEmpty() ||
	    destination.suffix().compare(QStringLiteral("vsaudio"), Qt::CaseInsensitive) != 0 ||
	    !destination.dir().exists() || destination.isSymLink() || destination.isJunction() || destination.isDir()) {
		return failure(QCoreApplication::translate("VibeStudioAudioProject",
		                                           "Choose a regular .vsaudio file in an existing folder."));
	}
	const QString target = resolvedPath(request.path);
	if ((!request.protectedPath.isEmpty() && samePath(target, resolvedPath(request.protectedPath))) ||
	    (!project.sourcePath.isEmpty() && samePath(target, resolvedPath(project.sourcePath)))) {
		return failure(QCoreApplication::translate(
		    "VibeStudioAudioProject", "Save the audio project to a different path to preserve its source."));
	}
	const bool guarded = !request.expected.sha256.isEmpty();
	if (guarded && (request.expected.sha256.size() != 32 || !samePath(target, request.expected.canonicalPath))) {
		return failure(QCoreApplication::translate("VibeStudioAudioProject",
		                                           "The audio project destination changed. Save a separate copy."),
		               true);
	}
	// Serialize first; a failed or cancelled encode never touches the destination.
	const QByteArray bytes = encodeAudioProject(project, &report.error, control);
	if (bytes.isEmpty()) {
		return report;
	}
	QLockFile lock(target + QStringLiteral(".lock"));
	if (!request.dryRun && !lock.tryLock(0)) {
		return failure(QCoreApplication::translate(
		                   "VibeStudioAudioProject",
		                   "Another process is saving this audio project. Try again or save a separate copy."),
		               true);
	}
	const auto checkDestination = [&]() {
		const QFileInfo current(request.path);
		if (current.isSymLink() || current.isJunction() || current.isDir() ||
		    !samePath(resolvedPath(request.path), target)) {
			return false;
		}
		if (guarded) {
			QByteArray digest;
			return current.isFile() && hashFile(request.path, &digest, &report.error) &&
			       digest == request.expected.sha256;
		}
		return !current.exists() || request.overwrite;
	};
	if (!checkDestination()) {
		return failure(QCoreApplication::translate(
		                   "VibeStudioAudioProject",
		                   "The audio project already exists or changed on disk. Review it and save a separate copy."),
		               true);
	}
	if (cancelled(control, &report.error)) {
		return report;
	}
	if (request.dryRun) {
		report.succeeded = true;
		return report;
	}
	QSaveFile file(target);
	file.setDirectWriteFallback(false);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
		return failure(QCoreApplication::translate("VibeStudioAudioProject", "Unable to save audio project: %1")
		                   .arg(file.errorString()));
	}
	if (cancelled(control, &report.error)) {
		file.cancelWriting();
		return report;
	}
	if (!checkDestination()) {
		file.cancelWriting();
		return failure(
		    QCoreApplication::translate("VibeStudioAudioProject",
		                                "The audio project changed during saving. The external file is preserved."),
		    true);
	}
	if (!file.commit()) {
		return failure(QCoreApplication::translate("VibeStudioAudioProject", "Unable to save audio project: %1")
		                   .arg(file.errorString()));
	}
	report.succeeded = report.written = true;
	report.identity = {destination.absoluteFilePath(), target,
	                   QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)};
	return report;
}

} // namespace vibestudio
