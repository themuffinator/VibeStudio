#include "core/audio_stems.h"
#include "core/audio_publication.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QSet>
#include <limits>

namespace vibestudio
{
namespace
{
QString message(const char *text) { return QCoreApplication::translate("AudioStems", text); }
constexpr qint64 WavLimit = qint64(std::numeric_limits<quint32>::max()) + 8;
constexpr qint64 ManifestLimit = 1024 * 1024;
bool stopped(const AudioWorkControl &control) { return control.cancelled && control.cancelled(); }
QString component(const QString &text, int limit)
{
	QString clean;
	for (QChar c : text.normalized(QString::NormalizationForm_C)) {
		if (c.unicode() < 32 || c.unicode() == 127 || QStringLiteral("\\/:*?\"<>|").contains(c))
			clean += '_';
		else if (c.category() != QChar::Other_Format)
			clean += c;
	}
	clean = clean.trimmed().left(limit);
	if (!clean.isEmpty() && clean.back().isHighSurrogate())
		clean.chop(1);
	while (!clean.isEmpty() && (clean.back() == '.' || clean.back().isSpace()))
		clean.chop(1);
	return clean.isEmpty() ? QStringLiteral("audio") : clean;
}
bool inspectOutput(AudioProjectSaveRequest *output, const QStringList &protectedPaths, qint64 limit,
                   const AudioWorkControl &control, QString *error)
{
	const QFileInfo info(output->path);
	if (info.exists() && (!info.isFile() || info.isSymLink() || info.isJunction() || !output->overwrite)) {
		*error = message(QT_TRANSLATE_NOOP("AudioStems", "A delivery file exists or is not a regular file: %1."))
		             .arg(output->path);
		return false;
	}
	if (info.exists()) {
		QFile file(output->path);
		if (file.size() > limit || !file.open(QIODevice::ReadOnly)) {
			*error = message(QT_TRANSLATE_NOOP("AudioStems", "Unable to inspect an existing delivery file: %1."))
			             .arg(output->path);
			return false;
		}
		QCryptographicHash digest(QCryptographicHash::Sha256);
		while (!file.atEnd()) {
			if (stopped(control)) {
				*error = message(QT_TRANSLATE_NOOP("AudioStems", "Stem export cancelled."));
				return false;
			}
			const auto bytes = file.read(1024 * 1024);
			if (file.error() != QFileDevice::NoError) {
				*error = file.errorString();
				return false;
			}
			digest.addData(bytes);
		}
		output->expected = {info.absoluteFilePath(), info.canonicalFilePath(), digest.result()};
	} else {
		// A file appearing after preflight never inherits blanket overwrite permission.
		output->overwrite = false;
	}
	auto probe = *output;
	probe.dryRun = true;
	const auto report = commitAudioOutput(probe, info.suffix(), protectedPaths, limit, control,
	                                      [](const AudioOutputEmit &, QString *) { return true; });
	*error = report.error;
	return report.succeeded;
}
} // namespace

AudioStemPlan planAudioSessionStems(const AudioSession &session, const AudioStemExportRequest &request)
{
	AudioStemPlan plan;
	plan.error = validateAudioSessionStructure(session);
	if (!plan.error.isEmpty())
		return plan;
	const int bits = audioWavBits(request.format);
	plan.first = request.first;
	plan.end = request.end == -1 ? audioSessionFrames(session) : request.end;
	if (request.directory.trimmed().isEmpty() || request.stripIds.size() > AudioSessionTrackLimit ||
	    (request.stripIds.isEmpty() && !request.includeMaster) || !bits ||
	    (request.dither && request.format == AudioWavFormat::Float32) ||
	    (request.tap != AudioSessionRenderTarget::Tap::PreFader &&
	     request.tap != AudioSessionRenderTarget::Tap::PostFader) ||
	    plan.first < 0 || plan.end <= plan.first || plan.end > AudioSessionFrameLimit ||
	    (plan.end - plan.first) * 2 * (bits / 8) + 60 > std::numeric_limits<quint32>::max()) {
		plan.error = message(QT_TRANSLATE_NOOP("AudioStems",
		                                       "Choose strips, an output folder, a valid WAV precision and a nonempty "
		                                       "range within the RIFF limit. Dither requires integer PCM."));
		return plan;
	}
	QSet<QString> wanted;
	for (const auto &id : request.stripIds) {
		if (id.isEmpty() || wanted.contains(id)) {
			plan.error =
			    message(QT_TRANSLATE_NOOP("AudioStems", "Each selected track or bus must have a distinct ID."));
			return plan;
		}
		wanted.insert(id);
	}
	const QDir directory(QDir(request.directory).absolutePath());
	const QString prefix = request.prefix.isEmpty() ? QString() : component(request.prefix, 40) + '-';
	plan.manifestPath = directory.filePath(prefix + QStringLiteral("delivery.stems.json"));
	if (request.includeMaster)
		plan.files.append({message(QT_TRANSLATE_NOOP("AudioStems", "Master mix")),
		                   directory.filePath(prefix + "000-master.wav"),
		                   {},
		                   request.ditherSeed});
	for (qsizetype i = 0; i < session.tracks.size(); ++i) {
		const auto &track = session.tracks[i];
		if (!wanted.remove(track.id))
			continue;
		const auto filename =
		    prefix + QStringLiteral("%1-%2.wav").arg(i + 1, 3, 10, QChar('0')).arg(component(track.name, 64));
		// Distinct stable streams avoid identical dither on quiet stem tails.
		// Unsigned addition intentionally wraps at 2^64; selection order and
		// including/excluding the master never change a strip's stream.
		plan.files.append(
		    {track.name, directory.filePath(filename), {track.id, request.tap}, request.ditherSeed + quint64(i + 1)});
	}
	if (!wanted.isEmpty())
		plan.error = message(QT_TRANSLATE_NOOP("AudioStems", "A selected track or bus no longer exists."));
	return plan;
}

AudioStemExportReport writeAudioSessionStems(const AudioSession &session, const AudioStemExportRequest &request,
                                             const AudioWorkControl &control)
{
	AudioStemExportReport report;
	report.plan = planAudioSessionStems(session, request);
	report.error = report.plan.error;
	if (!report.error.isEmpty())
		return report;
	const QFileInfo directory(request.directory);
	if (!directory.isDir() || directory.isSymLink() || directory.isJunction()) {
		report.error = message(QT_TRANSLATE_NOOP("AudioStems", "Choose an existing output folder that is not a link."));
		return report;
	}
	const QString canonicalDirectory = directory.canonicalFilePath();
	const auto checkDirectory = [&] {
		const QFileInfo current(request.directory);
		if (current.isDir() && !current.isSymLink() && !current.isJunction() &&
		    current.canonicalFilePath() == canonicalDirectory)
			return true;
		report.error = message(QT_TRANSLATE_NOOP("AudioStems", "The output folder changed during delivery."));
		return false;
	};
	// Serialize deliveries sharing this manifest, including disjoint WAV selections.
	QLockFile batchLock(report.plan.manifestPath + QStringLiteral(".batch.lock"));
	if (!request.dryRun && !batchLock.tryLock(0)) {
		report.error = message(QT_TRANSLATE_NOOP("AudioStems", "Another stem delivery owns this manifest."));
		return report;
	}
	report.error = validateAudioSession(session, control);
	if (!report.error.isEmpty()) {
		report.cancelled = stopped(control);
		return report;
	}
	QStringList protectedPaths = request.protectedPaths;
	for (const auto &source : session.sources)
		protectedPaths << source.audio.sourcePath;
	QVector<AudioProjectSaveRequest> outputs;
	for (const auto &file : report.plan.files) {
		AudioProjectSaveRequest output;
		output.path = file.path;
		output.overwrite = request.overwrite;
		output.dryRun = request.dryRun;
		if (!inspectOutput(&output, protectedPaths, WavLimit, control, &report.error)) {
			report.cancelled = stopped(control);
			return report;
		}
		outputs.append(output);
	}
	AudioProjectSaveRequest manifestOutput;
	manifestOutput.path = report.plan.manifestPath;
	manifestOutput.overwrite = request.overwrite;
	manifestOutput.dryRun = request.dryRun;
	if (!inspectOutput(&manifestOutput, protectedPaths, ManifestLimit, control, &report.error)) {
		report.cancelled = stopped(control);
		return report;
	}
	report.files.resize(report.plan.files.size());
	// This describes the current snapshot; it is not a digest of a saved session.
	QJsonArray routing;
	for (const auto &track : session.tracks)
		routing.append(QJsonObject{{"id", track.id},
		                           {"name", track.name},
		                           {"muted", track.muted},
		                           {"solo", track.solo},
		                           {"routing", audioRoutingToJson(track.routing)}});
	report.manifest = {{"kind", "vibestudio-audio-stems"},
	                   {"version", 1},
	                   {"sessionName", session.name},
	                   {"sampleRate", session.sampleRate},
	                   {"channels", 2},
	                   {"firstFrame", report.plan.first},
	                   {"endFrame", report.plan.end},
	                   {"framesPerFile", report.plan.end - report.plan.first},
	                   {"format", audioWavFormatId(request.format)},
	                   {"dither", request.dither},
	                   {"ditherSeed", QString::number(request.ditherSeed)},
	                   {"respectSolo", request.respectSolo},
	                   {"preserveMutes", true},
	                   {"prerollFrames", 0},
	                   {"routing", routing},
	                   {"dryRun", request.dryRun},
	                   {"masterIncludesMasterProcessing", true}};
	const auto manifest = [&](const QString &status) {
		report.manifest.insert("status", status);
		report.manifest.insert("completed", report.completed);
		report.manifest.insert("error", report.error);
		QJsonArray entries;
		for (qsizetype i = 0; i < report.files.size(); ++i) {
			const auto &file = report.plan.files[i];
			const auto &result = report.files[i];
			entries.append(QJsonObject{{"stripId", file.target.stripId},
			                           {"name", file.name},
			                           {"file", QFileInfo(file.path).fileName()},
			                           {"tap", file.target.stripId.isEmpty()                                ? "master"
			                                   : file.target.tap == AudioSessionRenderTarget::Tap::PreFader ? "pre"
			                                                                                                : "post"},
			                           {"state", result.saved.succeeded ? (request.dryRun ? "validated" : "written")
			                                     : result.saved.error.isEmpty() ? "pending"
			                                                                    : "failed"},
			                           {"frames", result.frames},
			                           {"processingLatencyFrames", result.processingLatencyFrames},
			                           {"ditherSeed", QString::number(file.ditherSeed)},
			                           {"peak", result.peak},
			                           {"samplesAboveFullScale", result.samplesAboveFullScale},
			                           {"sha256", QString::fromLatin1(result.saved.identity.sha256.toHex())},
			                           {"error", result.saved.error}});
		}
		report.manifest.insert("files", entries);
		if (!checkDirectory()) {
			report.manifest.insert("status", "manifest-failed");
			report.manifest.insert("error", report.error);
			return false;
		}
		const auto bytes = QJsonDocument(report.manifest).toJson(QJsonDocument::Indented);
		// Finish this bounded status write even after cancellation. An unclean
		// process exit or publication conflict retains an in-progress manifest.
		report.manifestSaved = commitAudioOutput(manifestOutput, "json", protectedPaths, ManifestLimit, {},
		                                         [&](const AudioOutputEmit &write, QString *) { return write(bytes); });
		if (report.manifestSaved.written) {
			manifestOutput.expected = report.manifestSaved.identity;
			manifestOutput.overwrite = true;
		}
		if (!report.manifestSaved.succeeded) {
			report.error = message(QT_TRANSLATE_NOOP(
			                           "AudioStems",
			                           "Delivery manifest could not be updated: %1. Completed WAVs remain available."))
			                   .arg(report.manifestSaved.error);
			report.manifest.insert("status", "manifest-failed");
			report.manifest.insert("error", report.error);
			return false;
		}
		return true;
	};
	if (stopped(control)) {
		report.cancelled = true;
		report.error = message(QT_TRANSLATE_NOOP("AudioStems", "Stem export cancelled."));
		return report;
	}
	if (!manifest("in-progress"))
		return report;
	auto renderSession = session;
	if (!request.respectSolo)
		for (auto &track : renderSession.tracks)
			track.solo = false;
	for (qsizetype i = 0; i < outputs.size(); ++i) {
		if (!checkDirectory())
			return report;
		AudioSessionMixdown mix;
		mix.output = outputs[i];
		mix.protectedPaths = protectedPaths;
		mix.first = report.plan.first;
		mix.end = report.plan.end;
		mix.format = request.format;
		mix.dither = request.dither;
		mix.ditherSeed = report.plan.files[i].ditherSeed;
		mix.target = report.plan.files[i].target;
		mix.progress = [&, i](qint64 frames, qint64 total) {
			if (request.progress)
				request.progress(int(i), int(outputs.size()), frames, total);
		};
		report.files[i] = writeAudioSessionMixdown(renderSession, mix, control);
		if (!report.files[i].saved.succeeded) {
			report.error = report.files[i].saved.error;
			report.cancelled = stopped(control);
			manifest(report.cancelled ? "cancelled" : "failed");
			return report;
		}
		++report.completed;
		if (!manifest(i + 1 == outputs.size() ? (request.dryRun ? "validated" : "complete") : "in-progress"))
			return report;
	}
	report.succeeded = true;
	return report;
}
} // namespace vibestudio
