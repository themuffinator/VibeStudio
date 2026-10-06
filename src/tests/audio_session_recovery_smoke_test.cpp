#include "core/audio_recovery_store.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <QtEndian>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value) {
		std::cerr << message << '\n';
	}
	return value;
}
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QByteArray read(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool write(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray hash(const QByteArray &bytes) { return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256); }
// Independent envelope fixture: no recovery encoder is used to specify the wire contract.
QByteArray envelope(const QByteArray &native, const QJsonObject &metadata)
{
	const auto json = QJsonDocument(metadata).toJson(QJsonDocument::Compact);
	QByteArray bytes = QByteArrayLiteral("VSRMX\r\n\x1a");
	bytes.resize(16);
	qToLittleEndian<quint32>(1, bytes.data() + 8);
	qToLittleEndian<quint32>(quint32(json.size()), bytes.data() + 12);
	bytes += json;
	bytes += native;
	bytes += hash(bytes);
	return bytes;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication application(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root)) {
		return 1;
	}
	QTemporaryDir temporary(QDir(root).filePath("session-recovery-XXXXXX"));
	if (!temporary.isValid()) {
		return 1;
	}
	const auto directory = temporary.filePath("copies");
	const auto originalPath = temporary.filePath("original.vssession");
	AudioProject media{{2, 48000, {0.25f, -0.5f, 0.75f, -0.0f}}, 0, 2, QStringLiteral("Original snow 雪.wav"), {}, {}};
	auto imported = importAudioSessionSource({}, media, {}, 96000);
	AudioSession session = imported.session;
	session.musicalTime = {95.5, 7, 8, {{3360, 127.25}}, {{5, 3, 4}}};
	session.name = QStringLiteral("Draft <b>session</b> 雪");
	session.tracks[0].gainAutomation = {{0, -6}, {96000, 3}};
	session.tracks[0].panAutomation = {{0, -1}, {96000, 0.5}};
	session.tracks[0].effects = {makeAudioEffect("delay", session.sampleRate),
	                             makeAudioEffect("peak-eq", session.sampleRate)};
	session.tracks[0].effects[1].parameters["gainDb"] = 4;
	session.tracks[0].effects[1].enabled = false;
	for (const auto &type : {"reverb", "chorus", "flanger", "tremolo", "phaser"})
		session.tracks[0].effects.append(makeAudioEffect(type, session.sampleRate));
	session.masterEffects = {makeAudioEffect("lookahead-limiter", session.sampleRate)};
	session.tracks[0].effectAutomation = {
	    {session.tracks[0].effects[0].id,
	     "mix",
	     false,
	     {{0, .1, AudioAutomationCurve::Smooth}, {48000, .8, AudioAutomationCurve::Step}}}};
	session.masterEffectAutomation = {{session.masterEffects[0].id, "ceilingDb", true, {{0, -2}, {96000, -6}}}};
	session.tracks[0].gainAutomation[0].curve = AudioAutomationCurve::Smooth;
	session.effectTailSeconds = .5;
	const auto native = encodeAudioSession(session);
	bool ok = expect(!native.isEmpty() && write(originalPath, native), "independent original session exists");
	QString error;
	const auto id = uuid();
	const auto wave = writeAudioRecovery(media, directory, id, &error);
	const auto path = writeAudioSessionRecovery(session, originalPath, directory, id, &error);
	const auto bytes = read(path);
	auto inventory = listAudioRecoveries(directory);
	const auto summary = audioSessionSummary(session);
	ok &= expect(!wave.isEmpty() && !path.isEmpty() && inventory.records.size() == 2,
	             "waveform and session copies can share a UUID without aliasing");
	int sessionRecords = 0, waveformRecords = 0;
	for (const auto &record : inventory.records) {
		if (record.kind == AudioRecoveryKind::Session) {
			++sessionRecords;
			ok &= expect(record.verified() && record.sourceName == session.name && record.sourcePath == originalPath &&
			                 record.frames == 120002 && record.tracks == 1 && record.clips == 1 &&
			                 record.sha256 == hash(bytes),
			             "session inventory verifies dimensions, provenance and full-file digest");
		} else {
			++waveformRecords;
		}
	}
	ok &=
	    expect(sessionRecords == 1 && waveformRecords == 1 && inventory.totalBytes == bytes.size() + read(wave).size(),
	           "shared inventory accounts both kinds once");
	const auto discovered = discoverAudioRecoveries(directory);
	for (const auto &record : discovered.records) {
		ok &= expect(!record.verified() && record.sha256.isEmpty(), "startup never decodes or implies verification");
	}
	AudioSessionRecovery restored;
	ok &= expect(readAudioSessionRecovery(path, hash(bytes), &restored, &error) &&
	                 audioSessionSummary(restored.session) == summary &&
	                 restored.session.sources[0].audio.clip.samples == media.clip.samples &&
	                 restored.sourcePath == originalPath && restored.writtenUtc.isValid(),
	             "recovery preserves samples, automation, effect chains/tail and provenance");
	auto lease = acquireAudioRecoverySession(directory, id, &error, AudioRecoveryKind::Session);
	auto waveformLease = acquireAudioRecoverySession(directory, id, &error);
	ok &= expect(lease && waveformLease &&
	                 !discardAudioRecovery(directory, id, hash(bytes), false, &error, AudioRecoveryKind::Session),
	             "distinct editor leases coexist and protect the live session");
	waveformLease.reset();
	lease.reset();
	ok &= expect(discardAudioRecovery(directory, id, hash(bytes), true, &error, AudioRecoveryKind::Session) &&
	                 read(path) == bytes && !QFileInfo::exists(path + ".active"),
	             "reviewed discard dry run is read-only");
	ok &= expect(!readAudioSessionRecovery(path, QByteArray(32, 'x'), &restored, &error) &&
	                 audioSessionSummary(restored.session) == summary,
	             "stale review digest leaves caller untouched");
	ok &= expect(!readAudioSessionRecovery(path, hash(bytes), &restored, &error, {[] { return true; }}) &&
	                 audioSessionSummary(restored.session) == summary,
	             "cancelled restoration preserves caller");
	const auto fixture = temporary.filePath("fixture.vssession-recovery");
	QJsonObject metadata{{"sourcePath", originalPath}, {"writtenUtc", "2026-10-05T12:00:00.000Z"}};
	const auto valid = envelope(native, metadata);
	ok &= expect(write(fixture, valid) && readAudioSessionRecovery(fixture, hash(valid), &restored, &error),
	             "hand-authored version-one envelope reads");
	QVector<QByteArray> invalid;
	auto unknown = metadata;
	unknown.insert("future", true);
	invalid.append(envelope(native, unknown));
	auto badDate = metadata;
	badDate.insert("writtenUtc", "nonsense");
	invalid.append(envelope(native, badDate));
	auto noZone = metadata;
	noZone.insert("writtenUtc", "2026-10-05T12:00:00");
	invalid.append(envelope(native, noZone));
	invalid.append(envelope(native + "junk", metadata));
	auto badHeader = valid;
	badHeader[8] = 2;
	badHeader.chop(32);
	badHeader += hash(badHeader);
	invalid.append(badHeader);
	auto badLength = valid;
	qToLittleEndian<quint32>(65537, badLength.data() + 12);
	badLength.chop(32);
	badLength += hash(badLength);
	invalid.append(badLength);
	auto badHash = valid;
	badHash[badHash.size() - 1] ^= 1;
	invalid.append(badHash);
	for (const auto &damaged : invalid) {
		ok &= expect(write(fixture, damaged) && !readAudioSessionRecovery(fixture, hash(damaged), &restored, &error) &&
		                 !error.isEmpty() && audioSessionSummary(restored.session) == summary,
		             "malformed envelope fails without partial restore");
	}
	ok &= expect(writeAudioSessionRecovery(session, path, directory, id, &error).isEmpty() && read(path) == bytes,
	             "recovery never overwrites recorded source path");
	const auto countRoot = temporary.filePath("mixed-count");
	for (int i = 0; i < AudioRecoveryCountLimit / 2; ++i) {
		const auto key = uuid();
		ok &= !writeAudioRecovery(media, countRoot, key).isEmpty();
		ok &= !writeAudioSessionRecovery(session, {}, countRoot, key).isEmpty();
	}
	ok &= expect(writeAudioSessionRecovery(session, {}, countRoot, uuid(), &error).isEmpty() &&
	                 writeAudioRecovery(media, countRoot, uuid(), &error).isEmpty(),
	             "combined count limit applies across editor kinds without eviction");
	const auto storageRoot = temporary.filePath("mixed-size");
	QDir().mkpath(storageRoot);
	QFile oversized(audioSessionRecoveryPath(storageRoot, uuid()));
	ok &= expect(oversized.open(QIODevice::WriteOnly) && oversized.resize(AudioRecoveryStorageLimit + 1),
	             "create bounded sparse storage fixture");
	oversized.close();
	ok &= expect(writeAudioRecovery(media, storageRoot, uuid(), &error).isEmpty() &&
	                 !listAudioRecoveries(storageRoot).records[0].verified(),
	             "oversized session blocks waveform writes without payload allocation");
	if (argc == 2) {
		QJsonObject last;
		const auto run = [&](const QStringList &args, int code) {
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			process.start(QString::fromLocal8Bit(argv[1]),
			              QStringList{"--cli", "--settings-file", temporary.filePath("cli.ini"), "--json", "asset"} +
			                  args);
			if (!process.waitForFinished(30000)) {
				process.kill();
				process.waitForFinished();
				return false;
			}
			const auto output = process.readAllStandardOutput();
			last = QJsonDocument::fromJson(output).object();
			if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != code) {
				std::cerr << output.constData() << process.readAllStandardError().constData();
				return false;
			}
			return true;
		};
		const auto output = temporary.filePath("restored.vssession");
		const QStringList recover{
		    "audio-session", "recover", path, "--expected-sha256", QString::fromLatin1(hash(bytes).toHex()),
		    "--output",      output};
		ok &= expect(run(recover + QStringList{"--dry-run"}, 0) && !QFileInfo::exists(output),
		             "CLI recovery dry run creates no output");
		ok &= expect(run(recover, 0) && read(output) == native && read(path) == bytes,
		             "CLI restores exact native session and preserves copy");
		ok &= expect(run({"audio-session", "recover", path, "--output", output}, 2),
		             "CLI restore requires review digest");
		ok &= expect(run({"audio-session", "recover", path, "--expected-sha256",
		                  QString::fromLatin1(hash(bytes).toHex()), "--output", originalPath, "--overwrite"},
		                 4) &&
		                 read(originalPath) == native,
		             "CLI recovery protects original session");
		ok &= expect(run({"audio-recoveries", "--directory", directory}, 0) &&
		                 last["audioRecoveries"].toObject()["records"].toArray().size() == 2,
		             "CLI reports mixed recovery inventory");
		const QStringList discard{"audio-recoveries",
		                          "--directory",
		                          directory,
		                          "--kind",
		                          "session",
		                          "--discard",
		                          id,
		                          "--expected-sha256",
		                          QString::fromLatin1(hash(bytes).toHex())};
		ok &= expect(run(discard, 0) && QFileInfo::exists(path), "CLI session discard defaults to dry run");
		ok &= expect(run(discard + QStringList{"--write"}, 0) && !QFileInfo::exists(path) && QFileInfo::exists(wave),
		             "CLI discards exact kind and keeps matching waveform UUID");
		ok &= expect(run({"audio-recoveries", "--directory", directory, "--kind", "session"}, 2),
		             "CLI rejects unused kind filter");
	}
	return ok ? 0 : 1;
}
