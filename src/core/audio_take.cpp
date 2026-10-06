#include "core/audio_take.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QSet>
#include <QtEndian>
#include <algorithm>
#include <bit>
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
constexpr int MetadataLimit = 64 * 1024;
constexpr int RecordHeaderSize = 24;
constexpr int RecordDigestSize = 32;
QString problem(const char *text) { return QCoreApplication::translate("AudioTake", text); }
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
bool cancelled(const AudioWorkControl &control) { return control.cancelled && control.cancelled(); }
bool textValid(const QString &text, int max, bool empty = true)
{
	return (empty || !text.trimmed().isEmpty()) && text.size() <= max && text.isValidUtf16() &&
	       !text.contains(QChar(0));
}
bool safeDirectory(const QString &directory)
{
	QFileInfo current(QDir(directory).absolutePath());
	if (!current.isDir())
		return false;
	for (;;) {
		if (current.isSymLink() || current.isJunction())
			return false;
		const QString parent = current.dir().absolutePath();
		if (parent == current.absoluteFilePath())
			return true;
		current.setFile(parent);
	}
}
// Original guarded adapters to CRT/POSIX file synchronization. QFile::flush
// first empties Qt's buffers, then the descriptor requests an OS storage flush.
// Device/filesystem power-loss guarantees still require platform acceptance.
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
bool syncParent(const QString &path)
{
#ifdef Q_OS_WIN
	Q_UNUSED(path);
	return true;
#else
	const auto bytes = QFile::encodeName(QFileInfo(path).absolutePath());
	const int descriptor = ::open(bytes.constData(), O_RDONLY | O_DIRECTORY);
	if (descriptor < 0)
		return false;
	const bool success = ::fsync(descriptor) == 0;
	::close(descriptor);
	return success;
#endif
}
QByteArray hashRecord(const QByteArray &previous, QByteArrayView header, QByteArrayView data)
{
	QCryptographicHash hash(QCryptographicHash::Sha256);
	hash.addData(previous);
	hash.addData(header);
	hash.addData(data);
	return hash.result();
}
bool decodeMetadata(const QByteArray &bytes, AudioTakeMetadata *metadata)
{
	const auto document = QJsonDocument::fromJson(bytes);
	if (!document.isObject())
		return false;
	const auto object = document.object();
	const QSet<QString> keys{"name",          "sampleRate", "inputChannels",     "channelMap", "position",
	                         "latencyFrames", "trackId",    "sourceSessionPath", "deviceName", "startedUtc"};
	if (object.size() != keys.size())
		return false;
	for (auto it = object.begin(); it != object.end(); ++it)
		if (!keys.contains(it.key()))
			return false;
	const auto integer = [&](const char *key, qint64 *value) {
		const auto item = object.value(QLatin1String(key));
		if (!item.isDouble() || !std::isfinite(item.toDouble()) || item.toDouble() != std::floor(item.toDouble()) ||
		    std::abs(item.toDouble()) > double(AudioTakeFrameLimit))
			return false;
		*value = qint64(item.toDouble());
		return true;
	};
	qint64 rate = 0, inputChannels = 0;
	if (!integer("sampleRate", &rate) || rate < 1 || rate > 384000 || !integer("inputChannels", &inputChannels) ||
	    inputChannels < 1 || inputChannels > 32 || !integer("position", &metadata->position) ||
	    !integer("latencyFrames", &metadata->latencyFrames))
		return false;
	metadata->sampleRate = int(rate);
	metadata->inputChannels = int(inputChannels);
	for (const auto *key : {"name", "trackId", "sourceSessionPath", "deviceName", "startedUtc"}) {
		if (!object.value(QLatin1String(key)).isString())
			return false;
	}
	metadata->name = object.value("name").toString();
	metadata->trackId = object.value("trackId").toString();
	metadata->sourceSessionPath = object.value("sourceSessionPath").toString();
	metadata->deviceName = object.value("deviceName").toString();
	const auto date = object.value("startedUtc").toString();
	if (!date.endsWith('Z'))
		return false;
	metadata->startedUtc = QDateTime::fromString(date, Qt::ISODateWithMs);
	if (!object.value("channelMap").isArray())
		return false;
	metadata->channelMap.clear();
	for (const auto &channel : object.value("channelMap").toArray()) {
		if (!channel.isDouble() || channel.toDouble() < 0 || channel.toDouble() >= 32 ||
		    channel.toDouble() != std::floor(channel.toDouble()))
			return false;
		metadata->channelMap.append(channel.toInt());
	}
	return validateAudioTakeMetadata(*metadata).isEmpty();
}
using BlockVisitor = std::function<bool(const AudioTakeMetadata &, qint64, int, QByteArrayView)>;
AudioTakeInfo scan(const QString &path, const AudioWorkControl &control, const BlockVisitor &visitor = {})
{
	AudioTakeInfo info;
	info.path = QFileInfo(path).absoluteFilePath();
	QFile file(info.path);
	const QFileInfo source(info.path);
	if (!source.isFile() || source.isSymLink() || source.isJunction() || !safeDirectory(source.absolutePath()) ||
	    source.size() < 48 || source.size() > AudioTakeByteLimit || !file.open(QIODevice::ReadOnly)) {
		info.error = problem(QT_TRANSLATE_NOOP(
		    "AudioTake", "The take is unreadable, linked or outside the supported file-size bounds."));
		return info;
	}
	info.bytes = file.size();
	const auto header = file.read(16);
	if (header.size() != 16 || !header.startsWith(QByteArrayLiteral("VSTAK\r\n\x1a")) ||
	    qFromLittleEndian<quint32>(header.constData() + 8) != 1) {
		info.error = problem(QT_TRANSLATE_NOOP("AudioTake", "Invalid take header or unsupported version."));
		return info;
	}
	const auto length = qFromLittleEndian<quint32>(header.constData() + 12);
	if (length > MetadataLimit || length > info.bytes - 48) {
		info.error = problem(QT_TRANSLATE_NOOP("AudioTake", "Invalid take metadata length."));
		return info;
	}
	const auto metadata = file.read(length), expected = file.read(32);
	if (metadata.size() != length || expected.size() != 32 ||
	    QCryptographicHash::hash(header + metadata, QCryptographicHash::Sha256) != expected ||
	    !decodeMetadata(metadata, &info.metadata)) {
		info.error = problem(QT_TRANSLATE_NOOP("AudioTake", "Take metadata failed validation or its checksum."));
		return info;
	}
	info.headerValid = true;
	info.verifiedBytes = file.pos();
	info.prefixSha256 = expected;
	quint32 sequence = 0;
	for (;;) {
		if (cancelled(control)) {
			info.cancelled = true;
			info.error = problem(QT_TRANSLATE_NOOP("AudioTake", "Take verification cancelled."));
			return info;
		}
		if (file.pos() == info.bytes) {
			info.error = problem(QT_TRANSLATE_NOOP(
			    "AudioTake", "The take has no completion record. Its verified prefix can be recovered."));
			return info;
		}
		const auto record = file.read(RecordHeaderSize);
		if (record.size() != RecordHeaderSize) {
			info.error = problem(QT_TRANSLATE_NOOP(
			    "AudioTake", "The final take record is incomplete. Its earlier verified blocks can be recovered."));
			return info;
		}
		const bool done = record.startsWith("DONE");
		const quint32 frames = qFromLittleEndian<quint32>(record.constData() + 16);
		const quint32 bytes = qFromLittleEndian<quint32>(record.constData() + 20);
		if ((!done && !record.startsWith("DATA")) || qFromLittleEndian<quint32>(record.constData() + 4) != sequence ||
		    qFromLittleEndian<quint64>(record.constData() + 8) != quint64(info.frames) ||
		    (done ? (frames != 0 || bytes != 0)
		          : (frames < 1 || frames > AudioTakeBlockFrames ||
		             bytes != frames * quint32(info.metadata.channelMap.size()) * 4)) ||
		    info.frames > AudioTakeFrameLimit - frames || qint64(bytes) + 32 > info.bytes - file.pos()) {
			info.error = problem(QT_TRANSLATE_NOOP(
			    "AudioTake",
			    "A take record is damaged or incomplete. Recovery stops at the preceding verified block."));
			return info;
		}
		const auto payload = file.read(bytes), checksum = file.read(32);
		if (payload.size() != bytes || checksum.size() != 32 ||
		    hashRecord(info.prefixSha256, record, payload) != checksum) {
			info.error = problem(
			    QT_TRANSLATE_NOOP("AudioTake", "A take block failed its checksum. Recovery stops before that block."));
			return info;
		}
		for (qsizetype i = 0; i < payload.size(); i += 4) {
			if (!std::isfinite(std::bit_cast<float>(qFromLittleEndian<quint32>(payload.constData() + i)))) {
				info.error = problem(QT_TRANSLATE_NOOP(
				    "AudioTake", "A take block contains non-finite samples. Recovery stops before that block."));
				return info;
			}
		}
		if (!done && visitor && !visitor(info.metadata, info.frames, int(frames), payload)) {
			info.error =
			    problem(QT_TRANSLATE_NOOP("AudioTake", "The requested take range or channel selection is invalid."));
			return info;
		}
		info.frames += frames;
		info.verifiedBytes = file.pos();
		info.prefixSha256 = checksum;
		++sequence;
		if (done) {
			info.complete = file.pos() == info.bytes && file.size() == info.bytes;
			if (!info.complete)
				info.error = problem(
				    QT_TRANSLATE_NOOP("AudioTake", "The take has trailing data or changed during verification."));
			return info;
		}
	}
}
} // namespace

QString validateAudioTakeMetadata(const AudioTakeMetadata &metadata)
{
	if (!textValid(metadata.name, 256, false) || !textValid(metadata.trackId, 64) ||
	    !textValid(metadata.sourceSessionPath, 8192) || !textValid(metadata.deviceName, 1024) ||
	    metadata.sampleRate < 1 || metadata.sampleRate > 384000 || metadata.inputChannels < 1 ||
	    metadata.inputChannels > 32 || metadata.channelMap.isEmpty() || metadata.channelMap.size() > 8 ||
	    metadata.position < 0 || metadata.position > AudioTakeFrameLimit ||
	    metadata.latencyFrames < -qint64(metadata.sampleRate) * 10 ||
	    metadata.latencyFrames > qint64(metadata.sampleRate) * 10 || !metadata.startedUtc.isValid() ||
	    metadata.startedUtc.toUTC().toString(Qt::ISODateWithMs).isEmpty()) {
		return problem(
		    QT_TRANSLATE_NOOP("AudioTake", "Invalid take name, rate, channels, placement, compensation or timestamp."));
	}
	QSet<int> selected;
	for (int channel : metadata.channelMap) {
		if (channel < 0 || channel >= metadata.inputChannels || selected.contains(channel)) {
			return problem(
			    QT_TRANSLATE_NOOP("AudioTake", "Select distinct input channels within the device channel count."));
		}
		selected.insert(channel);
	}
	return {};
}
QJsonObject audioTakeMetadataJson(const AudioTakeMetadata &metadata)
{
	QJsonArray channels;
	for (int channel : metadata.channelMap)
		channels.append(channel);
	return {{"name", metadata.name},
	        {"sampleRate", metadata.sampleRate},
	        {"inputChannels", metadata.inputChannels},
	        {"channelMap", channels},
	        {"position", metadata.position},
	        {"latencyFrames", metadata.latencyFrames},
	        {"trackId", metadata.trackId},
	        {"sourceSessionPath", metadata.sourceSessionPath},
	        {"deviceName", metadata.deviceName},
	        {"startedUtc", metadata.startedUtc.toUTC().toString(Qt::ISODateWithMs)}};
}
QJsonObject audioTakeInfoJson(const AudioTakeInfo &info)
{
	return {{"metadata", info.headerValid ? audioTakeMetadataJson(info.metadata) : QJsonObject{}},
	        {"path", info.path},
	        {"bytes", info.bytes},
	        {"verifiedBytes", info.verifiedBytes},
	        {"frames", info.frames},
	        {"prefixSha256", QString::fromLatin1(info.prefixSha256.toHex())},
	        {"headerValid", info.headerValid},
	        {"complete", info.complete},
	        {"recoverable", info.recoverable()},
	        {"cancelled", info.cancelled},
	        {"error", info.error}};
}
struct AudioTakeWriter::Private {
	QFile file;
	std::unique_ptr<QLockFile> lock;
	AudioTakeMetadata metadata;
	QByteArray previous;
	qint64 frames = 0;
	quint32 sequence = 0;
	bool failed = false;
	bool record(QByteArrayView payload, quint32 count, bool done, QString *error)
	{
		if (!file.isOpen() || failed || frames > AudioTakeFrameLimit - count ||
		    file.pos() > AudioTakeByteLimit - RecordHeaderSize - payload.size() - RecordDigestSize) {
			return fail(error, problem(QT_TRANSLATE_NOOP(
			                       "AudioTake", "The take is closed, failed or has reached its storage limit.")));
		}
		QByteArray header(RecordHeaderSize, '\0');
		header.replace(0, 4, done ? "DONE" : "DATA");
		qToLittleEndian(sequence, header.data() + 4);
		qToLittleEndian(quint64(frames), header.data() + 8);
		qToLittleEndian(count, header.data() + 16);
		qToLittleEndian(quint32(payload.size()), header.data() + 20);
		const auto checksum = hashRecord(previous, header, payload);
		if (file.write(header) != header.size() ||
		    (!payload.empty() && file.write(payload.data(), payload.size()) != payload.size()) ||
		    file.write(checksum) != checksum.size() || !syncFile(file)) {
			failed = true;
			return fail(
			    error,
			    problem(QT_TRANSLATE_NOOP(
			        "AudioTake",
			        "The next take block could not be stored and flushed. Earlier verified blocks are retained.")));
		}
		previous = checksum;
		frames += count;
		++sequence;
		return true;
	}
};
AudioTakeWriter::AudioTakeWriter() : d(std::make_unique<Private>()) {}
AudioTakeWriter::~AudioTakeWriter() { close(); }
void AudioTakeWriter::close()
{
	d->file.close();
	d->lock.reset();
}
qint64 AudioTakeWriter::frames() const { return d->frames; }
bool AudioTakeWriter::open(const QString &path, const AudioTakeMetadata &metadata, QString *error)
{
	close();
	d = std::make_unique<Private>();
	const auto issue = validateAudioTakeMetadata(metadata);
	if (!issue.isEmpty())
		return fail(error, issue);
	const QFileInfo output(path), lockInfo(path + QStringLiteral(".lock"));
	if (path.isEmpty() || output.suffix().compare("vstake", Qt::CaseInsensitive) != 0 || output.exists() ||
	    output.isSymLink() || output.isJunction() || !safeDirectory(output.absolutePath()) || lockInfo.isSymLink() ||
	    lockInfo.isJunction() || lockInfo.isDir()) {
		return fail(
		    error,
		    problem(QT_TRANSLATE_NOOP(
		        "AudioTake",
		        "Choose a new .vstake file in an existing, unlinked folder. Existing files are never overwritten.")));
	}
	d->lock = std::make_unique<QLockFile>(lockInfo.absoluteFilePath());
	if (!d->lock->tryLock(0))
		return fail(error, problem(QT_TRANSLATE_NOOP("AudioTake", "Another operation owns this take path.")));
	d->file.setFileName(output.absoluteFilePath());
	if (!d->file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
		close();
		return fail(error, problem(QT_TRANSLATE_NOOP("AudioTake", "The take file could not be created.")));
	}
	d->metadata = metadata;
	const auto json = QJsonDocument(audioTakeMetadataJson(metadata)).toJson(QJsonDocument::Compact);
	QByteArray header = QByteArrayLiteral("VSTAK\r\n\x1a");
	header.resize(16);
	qToLittleEndian<quint32>(1, header.data() + 8);
	qToLittleEndian<quint32>(quint32(json.size()), header.data() + 12);
	header += json;
	d->previous = QCryptographicHash::hash(header, QCryptographicHash::Sha256);
	if (json.size() > MetadataLimit || d->file.write(header) != header.size() || d->file.write(d->previous) != 32 ||
	    !syncFile(d->file) || !syncParent(d->file.fileName())) {
		close();
		return fail(error, problem(QT_TRANSLATE_NOOP("AudioTake", "The take header could not be stored and flushed.")));
	}
	if (error)
		error->clear();
	return true;
}
bool AudioTakeWriter::append(std::span<const float> samples, QString *error)
{
	const auto channels = size_t(d->metadata.channelMap.size());
	if (channels == 0 || samples.empty() || samples.size() % channels ||
	    samples.size() / channels > AudioTakeBlockFrames) {
		return fail(error, problem(QT_TRANSLATE_NOOP("AudioTake", "A take block requires 1–4096 complete frames.")));
	}
	QByteArray payload(qsizetype(samples.size() * 4), '\0');
	for (size_t i = 0; i < samples.size(); ++i) {
		if (!std::isfinite(samples[i]))
			return fail(error, problem(QT_TRANSLATE_NOOP(
			                       "AudioTake", "Input contains non-finite samples. The take block was not written.")));
		qToLittleEndian(std::bit_cast<quint32>(samples[i]), payload.data() + i * 4);
	}
	return d->record(payload, quint32(samples.size() / channels), false, error);
}
bool AudioTakeWriter::finish(QString *error)
{
	const bool ok = d->record({}, 0, true, error);
	close();
	return ok;
}
AudioTakeInfo inspectAudioTake(const QString &path, const AudioWorkControl &control) { return scan(path, control); }
AudioTakeBatchReadResult readAudioTakeRanges(const QString &path, const QByteArray &expectedPrefixSha256,
                                             const QVector<AudioTakeReadRange> &ranges, bool allowIncomplete,
                                             const AudioWorkControl &control)
{
	AudioTakeBatchReadResult result;
	const auto rangeError = [&] {
		result.error = problem(QT_TRANSLATE_NOOP(
		    "AudioTake",
		    "Review the take digest and choose nonempty ranges within the take and sample-storage limits."));
	};
	if (expectedPrefixSha256.size() != 32 || ranges.isEmpty() || ranges.size() > 128) {
		rangeError();
		return result;
	}
	qint64 total = 0, last = 0;
	for (const auto &range : ranges) {
		QSet<int> selected;
		for (int channel : range.channels) {
			if (channel < 0 || channel >= 8 || selected.contains(channel)) {
				result.error =
				    problem(QT_TRANSLATE_NOOP("AudioTake", "Choose distinct stored channels from the reviewed take."));
				return result;
			}
			selected.insert(channel);
		}
		if (range.first < 0 || range.end <= range.first || range.end > AudioTakeFrameLimit ||
		    range.channels.isEmpty() || range.channels.size() > 8 ||
		    range.end - range.first > AudioSampleLimit / range.channels.size()) {
			rangeError();
			return result;
		}
		total += (range.end - range.first) * range.channels.size();
		last = std::max(last, range.end);
		if (total > AudioSampleLimit * 4) {
			rangeError();
			return result;
		}
	}
	QVector<AudioProject> audio(ranges.size());
	for (qsizetype i = 0; i < ranges.size(); ++i) {
		audio[i].clip.channels = int(ranges[i].channels.size());
		audio[i].clip.samples.resize((ranges[i].end - ranges[i].first) * ranges[i].channels.size());
	}
	bool invalidChannel = false;
	result.info = scan(path, control, [&](const auto &metadata, qint64 blockFirst, int frames, QByteArrayView bytes) {
		for (qsizetype i = 0; i < ranges.size(); ++i) {
			const auto &range = ranges[i];
			for (int channel : range.channels)
				if (channel >= metadata.channelMap.size()) {
					invalidChannel = true;
					return false;
				}
			const auto begin = std::max(range.first, blockFirst), finish = std::min(range.end, blockFirst + frames);
			for (qint64 frame = begin; frame < finish; ++frame)
				for (qsizetype output = 0; output < range.channels.size(); ++output) {
					const auto offset =
					    ((frame - blockFirst) * metadata.channelMap.size() + range.channels[output]) * 4;
					audio[i].clip.samples[(frame - range.first) * range.channels.size() + output] =
					    std::bit_cast<float>(qFromLittleEndian<quint32>(bytes.data() + offset));
				}
		}
		return true;
	});
	if (invalidChannel) {
		result.error = problem(QT_TRANSLATE_NOOP(
		    "AudioTake",
		    "A selected stored channel is absent from this take. Choose channels shown in its reviewed channel map."));
		return result;
	}
	if (!result.info.recoverable() || last > result.info.frames || (!result.info.complete && !allowIncomplete) ||
	    result.info.prefixSha256 != expectedPrefixSha256) {
		result.error =
		    result.info.cancelled ? result.info.error
		    : result.info.prefixSha256 != expectedPrefixSha256
		        ? problem(QT_TRANSLATE_NOOP(
		              "AudioTake", "The verified take prefix changed after review. Inspect it again before importing."))
		    : !result.info.error.isEmpty()
		        ? result.info.error
		        : problem(QT_TRANSLATE_NOOP("AudioTake", "The selected range is outside the verified take."));
		return result;
	}
	for (qsizetype i = 0; i < ranges.size(); ++i) {
		const auto &range = ranges[i];
		auto &part = audio[i];
		part.clip.sampleRate = result.info.metadata.sampleRate;
		part.endFrame = range.end - range.first;
		part.sourceName = result.info.metadata.name;
		part.sourcePath = result.info.path;
		QJsonArray storedChannels, hardwareChannels;
		for (int channel : range.channels) {
			storedChannels.append(channel);
			hardwareChannels.append(result.info.metadata.channelMap[channel]);
		}
		part.metadata = {{"recordedTake", audioTakeMetadataJson(result.info.metadata)},
		                 {"takePrefixSha256", QString::fromLatin1(expectedPrefixSha256.toHex())},
		                 {"takeFirstFrame", range.first},
		                 {"takeEndFrame", range.end},
		                 {"recoveredPrefix", !result.info.complete},
		                 {"takeStoredChannels", storedChannels},
		                 {"takeHardwareChannels", hardwareChannels}};
	}
	result.audio = std::move(audio);
	return result;
}
AudioTakeReadResult readAudioTakeRange(const QString &path, const QByteArray &expectedPrefixSha256, qint64 first,
                                       qint64 end, const QVector<int> &channels, bool allowIncomplete,
                                       const AudioWorkControl &control)
{
	auto batch = readAudioTakeRanges(path, expectedPrefixSha256, {{first, end, channels}}, allowIncomplete, control);
	AudioTakeReadResult result;
	result.info = std::move(batch.info);
	result.error = std::move(batch.error);
	if (!batch.audio.isEmpty())
		result.audio = std::move(batch.audio.first());
	return result;
}
} // namespace vibestudio
