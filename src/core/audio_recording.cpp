#include "core/audio_recording.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <cmath>
#include <limits>
#ifdef Q_OS_WIN
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace vibestudio
{
namespace
{
constexpr qint64 JsonLimit = 128 * 1024;
QString recordingText(const char *source) { return QCoreApplication::translate("AudioRecording", source); }
bool fail(QString *error, const QString &text)
{
	if (error)
		*error = text;
	return false;
}
bool safeFolder(const QString &path)
{
	QFileInfo folder(QDir(path).absolutePath());
	if (!folder.isDir())
		return false;
	for (;;) {
		if (folder.isSymLink() || folder.isJunction())
			return false;
		const auto parent = folder.dir().absolutePath();
		if (parent == folder.absoluteFilePath())
			return true;
		folder.setFile(parent);
	}
}
bool validText(const QString &text, int maximum)
{
	return text.size() <= maximum && text.isValidUtf16() && !text.contains(QChar(0));
}
QByteArray jsonBytes(const QJsonObject &object) { return QJsonDocument(object).toJson(QJsonDocument::Compact); }
QByteArray digest(const QByteArray &bytes) { return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256); }
QJsonObject planJson(const AudioRecordingPlan &plan)
{
	const auto &pass = plan.pass;
	QJsonArray arms;
	for (const auto &arm : pass.arms) {
		QJsonArray map;
		for (int i = 0; i < arm.channels; ++i)
			map.append(arm.channelMap[size_t(i)]);
		arms.append(QJsonObject{{"trackId", arm.trackId},
		                        {"channelMap", map},
		                        {"monitor", arm.monitor},
		                        {"replacePlayback", arm.replacePlayback},
		                        {"monitorGain", arm.monitorGain}});
	}
	QJsonObject result{{"format", "VibeStudioRecording"},
	                   {"version", pass.loopPasses == 1 ? 1 : 2},
	                   {"name", plan.name},
	                   {"sourceSessionPath", plan.sourceSessionPath},
	                   {"inputDeviceName", plan.inputDeviceName},
	                   {"sampleRate", plan.sampleRate},
	                   {"startedUtc", plan.startedUtc.toUTC().toString(Qt::ISODateWithMs)},
	                   {"playbackFirst", pass.playbackFirst},
	                   {"punchFirst", pass.punchFirst},
	                   {"punchEnd", pass.punchEnd},
	                   {"calibrationFrames", pass.calibrationFrames},
	                   {"inputChannels", pass.inputChannels},
	                   {"blockFrames", pass.blockFrames},
	                   {"clockToleranceFrames", pass.clockToleranceFrames},
	                   {"outputGain", pass.outputGain},
	                   {"arms", arms}};
	if (pass.loopPasses != 1)
		result.insert("loopPasses", pass.loopPasses);
	return result;
}
bool integer(const QJsonObject &json, const char *key, qint64 *value, qint64 bound = AudioTakeFrameLimit)
{
	const auto item = json.value(QLatin1String(key));
	const auto number = item.toDouble(std::numeric_limits<double>::quiet_NaN());
	if (!item.isDouble() || !std::isfinite(number) || std::abs(number) > double(bound) || std::floor(number) != number)
		return false;
	*value = qint64(number);
	return true;
}
bool decodePlan(const QJsonObject &json, AudioRecordingPlan *plan)
{
	plan->name = json.value("name").toString();
	plan->sourceSessionPath = json.value("sourceSessionPath").toString();
	plan->inputDeviceName = json.value("inputDeviceName").toString();
	plan->startedUtc = QDateTime::fromString(json.value("startedUtc").toString(), Qt::ISODateWithMs);
	qint64 rate, channels, block, tolerance;
	auto &pass = plan->pass;
	if (json.value("version").toInt() == 2) {
		qint64 loops;
		if (!integer(json, "loopPasses", &loops, AudioDuplexLoopPassLimit) || loops < 2)
			return false;
		pass.loopPasses = int(loops);
	}
	if (!integer(json, "sampleRate", &rate, 384000) || !integer(json, "inputChannels", &channels, 32) ||
	    !integer(json, "blockFrames", &block, 4096) || !integer(json, "clockToleranceFrames", &tolerance, 384000) ||
	    !integer(json, "playbackFirst", &pass.playbackFirst) || !integer(json, "punchFirst", &pass.punchFirst) ||
	    !integer(json, "punchEnd", &pass.punchEnd) || !integer(json, "calibrationFrames", &pass.calibrationFrames))
		return false;
	plan->sampleRate = int(rate);
	pass.inputChannels = int(channels);
	pass.blockFrames = int(block);
	pass.clockToleranceFrames = int(tolerance);
	pass.outputGain = json.value("outputGain").toDouble(-1);
	const auto arms = json.value("arms").toArray();
	if (arms.isEmpty() || arms.size() > AudioDuplexArmLimit)
		return false;
	for (const auto &value : arms) {
		const auto object = value.toObject();
		AudioDuplexArm arm;
		arm.trackId = object.value("trackId").toString();
		arm.monitor = object.value("monitor").toBool();
		arm.replacePlayback = object.value("replacePlayback").toBool();
		arm.monitorGain = object.value("monitorGain").toDouble(-1);
		const auto map = object.value("channelMap").toArray();
		arm.channels = int(map.size());
		if (arm.channels < 1 || arm.channels > 2)
			return false;
		for (int i = 0; i < arm.channels; ++i) {
			const auto n = map[i].toDouble(-1);
			if (n < 0 || n > 31 || std::floor(n) != n)
				return false;
			arm.channelMap[size_t(i)] = int(n);
		}
		pass.arms.append(arm);
	}
	return validateAudioRecordingPlan(*plan).isEmpty() && planJson(*plan) == json;
}
bool syncFile(QFile &file)
{
	if (!file.flush())
		return false;
#ifdef Q_OS_WIN
	return ::_commit(file.handle()) == 0;
#else
	return ::fsync(file.handle()) == 0;
#endif
}
bool syncDirectory(const QString &path)
{
#ifdef Q_OS_WIN
	Q_UNUSED(path);
	return true; // QFile/CRT flush; directory-entry power-loss acceptance is separate.
#else
	const auto name = QFile::encodeName(path);
	const int fd = ::open(name.constData(), O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		return false;
	const bool ok = ::fsync(fd) == 0;
	::close(fd);
	return ok;
#endif
}
bool writeNew(const QString &path, const QJsonObject &payload, QString *error)
{
	const auto data = jsonBytes(payload);
	const auto bytes = jsonBytes({{"payload", payload}, {"sha256", QString::fromLatin1(digest(data).toHex())}});
	const QFileInfo target(path);
	QFile file(path);
	if (!safeFolder(target.absolutePath()) || target.exists() || target.isSymLink() || target.isJunction() ||
	    bytes.size() > JsonLimit || !file.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
	    file.write(bytes) != bytes.size() || !syncFile(file) || !syncDirectory(target.absolutePath()))
		return fail(error,
		            recordingText(QT_TRANSLATE_NOOP(
		                "AudioRecording",
		                "The recording record could not be created and flushed. Existing files were retained.")));
	if (error)
		error->clear();
	return true;
}
bool readRecord(const QString &path, QJsonObject *payload, QByteArray *sha256)
{
	const QFileInfo source(path);
	QFile file(path);
	if (!source.isFile() || source.isSymLink() || source.isJunction() || !safeFolder(source.absolutePath()) ||
	    source.size() > JsonLimit || !file.open(QIODevice::ReadOnly))
		return false;
	const auto bytes = file.read(JsonLimit + 1);
	const auto json = QJsonDocument::fromJson(bytes).object();
	// Our canonical envelope also rejects duplicate keys, unknown fields and
	// trailing bytes instead of silently normalizing an ambiguous record.
	if (json.size() != 2 || !json.value("payload").isObject() || jsonBytes(json) != bytes)
		return false;
	*payload = json.value("payload").toObject();
	*sha256 = digest(jsonBytes(*payload));
	return json.value("sha256").toString() == QString::fromLatin1(sha256->toHex());
}
QJsonObject receiptJson(const AudioRecordingReceipt &receipt, const QByteArray &planDigest,
                        const QVector<AudioTakeInfo> &takes)
{
	const auto &p = receipt.progress;
	QJsonArray files;
	for (const auto &take : takes)
		files.append(QJsonObject{{"frames", take.frames},
		                         {"headerValid", take.headerValid},
		                         {"complete", take.complete},
		                         {"prefixSha256", QString::fromLatin1(take.prefixSha256.toHex())}});
	return {{"format", "VibeStudioRecordingResult"},
	        {"version", 1},
	        {"planSha256", QString::fromLatin1(planDigest.toHex())},
	        {"outcome", int(receipt.outcome)},
	        {"error", receipt.error},
	        {"inputName", receipt.inputName},
	        {"outputName", receipt.outputName},
	        {"host", receipt.host},
	        {"inputLatencySeconds", receipt.inputLatencySeconds},
	        {"outputLatencySeconds", receipt.outputLatencySeconds},
	        {"packetInputTimestamp", receipt.packetInputTimestamp},
	        {"state", int(p.state)},
	        {"fault", int(p.fault)},
	        {"processedFrames", p.processedFrames},
	        {"capturedFrames", p.capturedFrames},
	        {"primingFrames", p.primingFrames},
	        {"inputTimelineOrigin", p.inputTimelineOrigin},
	        {"roundTripFrames", p.roundTripFrames},
	        {"maximumClockDeviationFrames", p.maximumClockDeviationFrames},
	        {"processingLatencyFrames", p.processingLatencyFrames},
	        {"takes", files}};
}
bool decodeReceipt(const QJsonObject &json, AudioRecordingReceipt *receipt)
{
	qint64 outcome, state, fault, latency;
	auto &p = receipt->progress;
	if (!integer(json, "outcome", &outcome, 2) || outcome < 0 ||
	    !integer(json, "state", &state, int(AudioDuplexProgress::State::Error)) || state < 0 ||
	    !integer(json, "fault", &fault, int(AudioDuplexFault::CaptureQueueFull)) || fault < 0 ||
	    !integer(json, "processedFrames", &p.processedFrames, AudioTakeFrameLimit * 2) || p.processedFrames < 0 ||
	    !integer(json, "capturedFrames", &p.capturedFrames) || p.capturedFrames < 0 ||
	    !integer(json, "primingFrames", &p.primingFrames) || p.primingFrames < 0 ||
	    !integer(json, "inputTimelineOrigin", &p.inputTimelineOrigin, AudioTakeFrameLimit * 2) ||
	    !integer(json, "roundTripFrames", &p.roundTripFrames, 3840000) || p.roundTripFrames < 0 ||
	    !integer(json, "maximumClockDeviationFrames", &p.maximumClockDeviationFrames) ||
	    p.maximumClockDeviationFrames < 0 || !integer(json, "processingLatencyFrames", &latency, 3840000) ||
	    latency < 0)
		return false;
	receipt->outcome = AudioRecordingReceipt::Outcome(outcome);
	p.state = AudioDuplexProgress::State(state);
	p.fault = AudioDuplexFault(fault);
	p.processingLatencyFrames = int(latency);
	receipt->error = json.value("error").toString();
	receipt->inputName = json.value("inputName").toString();
	receipt->outputName = json.value("outputName").toString();
	receipt->host = json.value("host").toString();
	receipt->inputLatencySeconds = json.value("inputLatencySeconds").toDouble(-1);
	receipt->outputLatencySeconds = json.value("outputLatencySeconds").toDouble(-1);
	receipt->packetInputTimestamp = json.value("packetInputTimestamp").toBool();
	return validText(receipt->error, 8192) && validText(receipt->inputName, 1024) &&
	       validText(receipt->outputName, 1024) && validText(receipt->host, 256) &&
	       std::isfinite(receipt->inputLatencySeconds) && std::isfinite(receipt->outputLatencySeconds) &&
	       receipt->inputLatencySeconds >= 0 && receipt->outputLatencySeconds >= 0 &&
	       receipt->inputLatencySeconds <= 10 && receipt->outputLatencySeconds <= 10;
}
bool receiptAgrees(const AudioRecordingReceipt &receipt, const AudioRecordingInfo &info)
{
	const auto plannedFrames = audioDuplexCaptureEnd(info.plan.pass) - info.plan.pass.punchFirst;
	if (receipt.progress.capturedFrames > plannedFrames)
		return false;
	for (const auto &take : info.takes)
		if (take.headerValid && take.frames > receipt.progress.capturedFrames)
			return false;
	if (receipt.outcome == AudioRecordingReceipt::Outcome::Interrupted)
		return true;
	if (!receipt.error.isEmpty() || receipt.progress.fault != AudioDuplexFault::None)
		return false;
	if (receipt.outcome == AudioRecordingReceipt::Outcome::Stopped &&
	    receipt.progress.state != AudioDuplexProgress::State::Stopped &&
	    receipt.progress.state != AudioDuplexProgress::State::Complete)
		return false;
	for (const auto &take : info.takes)
		if (!take.headerValid || !take.complete || take.frames != receipt.progress.capturedFrames)
			return false;
	return receipt.outcome != AudioRecordingReceipt::Outcome::Complete ||
	       (receipt.progress.state == AudioDuplexProgress::State::Complete &&
	        receipt.progress.capturedFrames == plannedFrames);
}
} // namespace
AudioTakeMetadata audioRecordingArmMetadata(const AudioRecordingPlan &plan, int arm)
{
	AudioTakeMetadata metadata;
	if (arm < 0 || arm >= plan.pass.arms.size())
		return metadata;
	const auto &input = plan.pass.arms[arm];
	metadata.name = plan.name;
	metadata.sampleRate = plan.sampleRate;
	metadata.inputChannels = plan.pass.inputChannels;
	metadata.channelMap.clear();
	for (int c = 0; c < input.channels && c < 2; ++c)
		metadata.channelMap.append(input.channelMap[size_t(c)]);
	metadata.position = plan.pass.punchFirst;
	metadata.trackId = input.trackId;
	metadata.sourceSessionPath = plan.sourceSessionPath;
	metadata.deviceName = plan.inputDeviceName;
	metadata.startedUtc = plan.startedUtc;
	return metadata;
}
QString validateAudioRecordingPlan(const AudioRecordingPlan &plan)
{
	const auto &p = plan.pass;
	QSet<QString> tracks;
	bool invalid = p.arms.isEmpty() || p.arms.size() > AudioDuplexArmLimit || p.playbackFirst < 0 ||
	               p.punchFirst < p.playbackFirst || p.punchEnd <= p.punchFirst || audioDuplexCaptureEnd(p) < 0 ||
	               p.blockFrames < 1 || p.blockFrames > 4096 || p.clockToleranceFrames < 0 ||
	               p.clockToleranceFrames > plan.sampleRate || !std::isfinite(p.outputGain) || p.outputGain < 0 ||
	               p.outputGain > 1 || p.calibrationFrames < -qint64(plan.sampleRate) * 10 ||
	               p.calibrationFrames > qint64(plan.sampleRate) * 10;
	for (int i = 0; i < p.arms.size(); ++i) {
		const auto &arm = p.arms[i];
		invalid |= arm.trackId.isEmpty() || tracks.contains(arm.trackId) || arm.channels < 1 || arm.channels > 2 ||
		           !std::isfinite(arm.monitorGain) || arm.monitorGain < 0 || arm.monitorGain > 1 ||
		           (arm.replacePlayback && !arm.monitor) ||
		           !validateAudioTakeMetadata(audioRecordingArmMetadata(plan, i)).isEmpty();
		tracks.insert(arm.trackId);
	}
	return invalid ? recordingText(QT_TRANSLATE_NOOP(
	                     "AudioRecording", "The recording plan has invalid tracks, channels, timing or metadata."))
	               : QString{};
}
QString audioRecordingArmPath(const QString &directory, int arm)
{
	if (arm < 0 || arm >= AudioDuplexArmLimit)
		return {};
	return QDir(directory).filePath(QStringLiteral("arm-%1.vstake").arg(arm + 1, 2, 10, QChar('0')));
}
bool createAudioRecordingFolder(const QString &directory, const AudioRecordingPlan &plan, QByteArray *sha256,
                                QString *error)
{
	const auto issue = validateAudioRecordingPlan(plan);
	if (!issue.isEmpty())
		return fail(error, issue);
	const QFileInfo folder(directory);
	if (directory.isEmpty() || folder.suffix().compare("vsrecord", Qt::CaseInsensitive) != 0 || folder.exists() ||
	    folder.isSymLink() || folder.isJunction() || !safeFolder(folder.absolutePath()) ||
	    !QDir(folder.absolutePath()).mkdir(folder.fileName()) || !safeFolder(folder.absoluteFilePath()) ||
	    !syncDirectory(folder.absolutePath()))
		return fail(error,
		            recordingText(QT_TRANSLATE_NOOP(
		                "AudioRecording", "Choose a new .vsrecord folder inside an existing, unlinked folder.")));
	const auto json = planJson(plan);
	if (!writeNew(QDir(folder.absoluteFilePath()).filePath("plan.json"), json, error))
		return false;
	if (sha256)
		*sha256 = digest(jsonBytes(json));
	return true;
}
AudioRecordingInfo inspectAudioRecording(const QString &directory, const AudioWorkControl &control)
{
	AudioRecordingInfo info;
	info.directory = QFileInfo(directory).absoluteFilePath();
	QJsonObject plan;
	if (!safeFolder(info.directory) ||
	    !readRecord(QDir(info.directory).filePath("plan.json"), &plan, &info.planSha256) ||
	    !decodePlan(plan, &info.plan)) {
		info.error = recordingText(QT_TRANSLATE_NOOP(
		    "AudioRecording",
		    "The recording plan is missing, invalid or linked. Individual take files can still be inspected."));
		return info;
	}
	info.planValid = true;
	for (int i = 0; i < info.plan.pass.arms.size(); ++i) {
		auto take = inspectAudioTake(audioRecordingArmPath(info.directory, i), control);
		if (take.headerValid &&
		    audioTakeMetadataJson(take.metadata) != audioTakeMetadataJson(audioRecordingArmMetadata(info.plan, i))) {
			take.headerValid = false;
			take.error = recordingText(
			    QT_TRANSLATE_NOOP("AudioRecording", "The take metadata does not match this recording plan."));
		}
		info.takes.append(take);
		if (take.cancelled) {
			info.error = take.error;
			return info;
		}
	}
	QJsonObject result;
	if (!readRecord(QDir(info.directory).filePath("result.json"), &result, &info.receiptSha256) ||
	    !decodeReceipt(result, &info.receipt)) {
		info.error = recordingText(QT_TRANSLATE_NOOP(
		    "AudioRecording", "No valid final recording record is available. Review each verified take prefix."));
		return info;
	}
	// Recreate the receipt from independently verified files, not its asserted
	// lengths/digests. Unknown fields, wrong plan and substituted files fail.
	info.receiptValid = result.value("planSha256").toString() == QString::fromLatin1(info.planSha256.toHex());
	info.receiptRecord = result;
	info.receiptMatches = info.receiptValid && result == receiptJson(info.receipt, info.planSha256, info.takes) &&
	                      receiptAgrees(info.receipt, info);
	if (!info.receiptMatches)
		info.error = recordingText(QT_TRANSLATE_NOOP(
		    "AudioRecording", "The final recording record does not match the current verified take files."));
	return info;
}
bool finishAudioRecordingFolder(const QString &directory, const QByteArray &planSha256,
                                const AudioRecordingReceipt &receipt, QString *error, AudioRecordingInfo *finalInfo)
{
	auto info = inspectAudioRecording(directory);
	AudioRecordingReceipt checked;
	const auto json = receiptJson(receipt, planSha256, info.takes);
	if (!info.planValid || planSha256.size() != 32 || planSha256 != info.planSha256 || !decodeReceipt(json, &checked) ||
	    json != receiptJson(checked, planSha256, info.takes) || !receiptAgrees(receipt, info)) {
		if (finalInfo)
			*finalInfo = std::move(info);
		return fail(error,
		            recordingText(QT_TRANSLATE_NOOP(
		                "AudioRecording", "The recording result cannot be reconciled with its plan and take files.")));
	}
	const bool written = writeNew(QDir(info.directory).filePath("result.json"), json, error);
	if (written) {
		info.receipt = receipt;
		info.receiptRecord = json;
		info.receiptValid = info.receiptMatches = true;
		info.receiptSha256 = digest(jsonBytes(json));
		info.error.clear();
	}
	if (finalInfo)
		*finalInfo = std::move(info);
	return written;
}
QJsonObject audioRecordingInfoJson(const AudioRecordingInfo &info)
{
	QJsonArray takes;
	for (const auto &take : info.takes) {
		auto entry = audioTakeInfoJson(take);
		const auto length = info.plan.pass.punchEnd - info.plan.pass.punchFirst;
		if (info.planValid && length > 0) {
			entry.insert("completePasses", take.frames / length);
			entry.insert("partialPassFrames", take.frames % length);
		}
		takes.append(entry);
	}
	return {{"directory", info.directory},
	        {"planValid", info.planValid},
	        {"receiptValid", info.receiptValid},
	        {"receiptMatches", info.receiptMatches},
	        {"planSha256", QString::fromLatin1(info.planSha256.toHex())},
	        {"receiptSha256", QString::fromLatin1(info.receiptSha256.toHex())},
	        {"error", info.error},
	        {"plan", info.planValid ? planJson(info.plan) : QJsonObject{}},
	        {"result", info.receiptRecord},
	        {"takes", takes}};
}
} // namespace vibestudio
