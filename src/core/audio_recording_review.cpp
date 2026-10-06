#include "core/audio_recording_review.h"
#include "core/audio_publication.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

namespace vibestudio
{
namespace
{
QString text(const char *source) { return QCoreApplication::translate("AudioRecordingReview", source); }
bool cancelled(const AudioWorkControl &control, QString *error)
{
	if (!control.cancelled || !control.cancelled())
		return false;
	if (error)
		*error = text(QT_TRANSLATE_NOOP("AudioRecordingReview", "Review operation cancelled."));
	return true;
}
} // namespace

bool readAudioRecordingReview(const QString &path, AudioRecordingImportRequest *review, AudioProjectIdentity *identity,
                              QString *error, const AudioWorkControl &control)
{
	if (cancelled(control, error))
		return false;
	QFile file(path);
	if (!review || !QFileInfo(file).isFile() || !file.open(QIODevice::ReadOnly) ||
	    file.size() > AudioRecordingReviewByteLimit) {
		if (error)
			*error = text(
			    QT_TRANSLATE_NOOP("AudioRecordingReview", "Choose a readable review JSON file of at most 128 KiB."));
		return false;
	}
	const auto bytes = file.read(AudioRecordingReviewByteLimit + 1);
	AudioRecordingImportRequest next;
	QJsonParseError parse;
	const auto document = QJsonDocument::fromJson(bytes, &parse);
	if (file.error() != QFileDevice::NoError || bytes.size() != file.size() ||
	    bytes.size() > AudioRecordingReviewByteLimit || parse.error != QJsonParseError::NoError ||
	    !document.isObject()) {
		if (error)
			*error = text(
			    QT_TRANSLATE_NOOP("AudioRecordingReview", "The recording review is incomplete or is not valid JSON."));
		return false;
	}
	if (!parseAudioRecordingImportRequest(document.object(), &next, error) || cancelled(control, error))
		return false;
	const QFileInfo source(path);
	if (QDir::isRelativePath(next.directory))
		next.directory = source.dir().absoluteFilePath(next.directory);
	next.directory = QDir::cleanPath(next.directory);
	*review = std::move(next);
	if (identity)
		*identity = {source.absoluteFilePath(), source.canonicalFilePath(),
		             QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)};
	if (error)
		error->clear();
	return true;
}

AudioRecordingReviewResult openAudioRecordingReview(const AudioSession &session, const QString &path,
                                                    const AudioWorkControl &control)
{
	AudioRecordingReviewResult result;
	AudioRecordingImportRequest review;
	AudioProjectIdentity identity;
	if (readAudioRecordingReview(path, &review, &identity, &result.error, control)) {
		const auto recording = inspectAudioRecording(review.directory, control);
		const auto prepared = prepareAudioRecordingImport(session, recording, review, control);
		result.error = prepared.error;
		result.cancelled = prepared.cancelled;
		if (prepared.succeeded() && !cancelled(control, &result.error)) {
			result.review = std::move(review);
			result.recording = recording;
			result.identity = identity;
		}
	}
	if (cancelled(control, &result.error)) {
		result.cancelled = true;
		result.review = {};
		result.recording = {};
		result.identity = {};
	}
	return result;
}

AudioProjectSaveReport writeAudioRecordingReview(const AudioSession &session, const AudioRecordingImportRequest &review,
                                                 const AudioProjectSaveRequest &output, const AudioWorkControl &control,
                                                 const QStringList &protectedPaths)
{
	AudioProjectSaveReport report;
	if (cancelled(control, &report.error))
		return report;
	const auto recording = inspectAudioRecording(review.directory, control);
	const auto prepared = prepareAudioRecordingImport(session, recording, review, control);
	if (!prepared.succeeded()) {
		report.error = prepared.cancelled
		                   ? text(QT_TRANSLATE_NOOP("AudioRecordingReview", "Review operation cancelled."))
		                   : prepared.error;
		cancelled(control, &report.error);
		return report;
	}
	auto stored = review;
	stored.directory = QFileInfo(output.path).dir().relativeFilePath(QFileInfo(review.directory).absoluteFilePath());
	AudioRecordingImportRequest checked;
	const auto json = audioRecordingImportRequestJson(stored);
	if (!parseAudioRecordingImportRequest(json, &checked, &report.error))
		return report;
	const auto bytes = QJsonDocument(json).toJson(QJsonDocument::Indented);
	if (bytes.size() > AudioRecordingReviewByteLimit) {
		report.error = text(QT_TRANSLATE_NOOP("AudioRecordingReview", "The review exceeds the 128 KiB file limit."));
		return report;
	}
	auto protectedFiles = protectedPaths + audioRecordingProtectedPaths(review.directory);
	protectedFiles << recording.plan.sourceSessionPath;
	for (const auto &source : session.sources)
		protectedFiles << source.audio.sourcePath;
	return commitAudioOutput(output, QStringLiteral("json"), protectedFiles, AudioRecordingReviewByteLimit, control,
	                         [&](const AudioOutputEmit &write, QString *) { return write(bytes); });
}
} // namespace vibestudio
