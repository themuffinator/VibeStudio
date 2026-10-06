#include "core/audio_recovery_store.h"
#include "core/studio_settings.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char* message)
{
	if (!value) {
		std::cerr << message << '\n';
	}
	return value;
}
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QByteArray read(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
} // namespace
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	const QString root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT", QDir::currentPath());
	QDir().mkpath(root);
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("audio-recovery-XXXXXX")));
	if (!temporary.isValid()) {
		return 1;
	}
	const QString directory = temporary.filePath(QStringLiteral("recovery"));
	const QByteArray rootOverride = qgetenv("VIBESTUDIO_AUDIO_RECOVERY_ROOT");
	const bool hadRootOverride = qEnvironmentVariableIsSet("VIBESTUDIO_AUDIO_RECOVERY_ROOT");
	qunsetenv("VIBESTUDIO_AUDIO_RECOVERY_ROOT");
	StudioSettings::setOverrideFilePath(temporary.filePath(QStringLiteral("profile/settings.ini")));
	bool ok = expect(audioRecoveryDirectory() == temporary.filePath(QStringLiteral("profile/audio-recovery")),
	                 "audio recovery follows an explicit settings profile without creating the folder");
	StudioSettings::setOverrideFilePath({});
	if (hadRootOverride) {
		qputenv("VIBESTUDIO_AUDIO_RECOVERY_ROOT", rootOverride);
	}
	AudioProject project{
	    {2, 48000, {0.25f, -0.5f, 2.0f, -0.0f}},          1, 2, QStringLiteral("Sound <b>雪</b>"),
	    temporary.filePath(QStringLiteral("source.wav")), {}};
	const QString id = uuid();
	QString error;
	ok &= expect(listAudioRecoveries(directory).records.isEmpty() && !QFileInfo::exists(directory),
	             "listing missing folders is read-only");
	const QString path = writeAudioRecovery(project, directory, id, &error);
	const auto original = read(path);
	const auto discovered = discoverAudioRecoveries(directory);
	ok &=
	    expect(discovered.records.size() == 1 && discovered.records[0].path == path &&
	               discovered.records[0].bytes == original.size() && discovered.records[0].sha256.isEmpty() &&
	               discovered.records[0].sourceName.isEmpty() && !discovered.records[0].verified(),
	           "startup discovery reports file metadata without decoding or claiming verification");
	auto inventory = listAudioRecoveries(directory);
	ok &= expect(inventory.records.size() == 1 && inventory.records[0].verified() &&
	                 inventory.records[0].sourceName == project.sourceName &&
	                 inventory.records[0].channels == 2 && inventory.records[0].frames == 2 &&
	                 inventory.records[0].writtenUtc.isValid(),
	             "inventory verifies samples, metadata, format, timestamp and checksum");
	if (inventory.records.size() != 1) {
		return 1;
	}
	const auto digest = inventory.records[0].sha256;
	ok &= expect(digest == QCryptographicHash::hash(original, QCryptographicHash::Sha256),
	             "inventory uses a digest of every record byte");
	ok &= expect(discardAudioRecovery(directory, id, digest, true, &error) && read(path) == original &&
	                 !QFileInfo::exists(path + QStringLiteral(".active")),
	             "discard dry run preserves content and creates no session lock");
	auto lease = acquireAudioRecoverySession(directory, id, &error);
	ok &= expect(bool(lease) && !acquireAudioRecoverySession(directory, id, &error) &&
	                 !discardAudioRecovery(directory, id, digest, false, &error) && read(path) == original,
	             "a live editor lease prevents discard and duplicate ownership");
	ok &= expect(listAudioRecoveries(directory).records[0].sessionFilePresent,
	             "inventory exposes presence without claiming the recorded PID is alive");
	lease.reset();
	project.clip.samples[0] = 0.125f;
	ok &= expect(!writeAudioRecovery(project, directory, id, &error).isEmpty() &&
	                 !discardAudioRecovery(directory, id, digest, false, &error),
	             "stale reviewed digest cannot discard a newer snapshot");
	inventory = listAudioRecoveries(directory);
	ok &= expect(discardAudioRecovery(directory, id, inventory.records[0].sha256, false, &error) &&
	                 !QFileInfo::exists(path),
	             "reviewed discard removes exactly its own record");
	const QString brokenId = uuid(), broken = audioRecoveryPath(directory, brokenId);
	ok &= expect(write(broken, QByteArray("broken")), "create corrupt fixture");
	const auto brokenDiscovery = discoverAudioRecoveries(directory);
	ok &= expect(brokenDiscovery.records.size() == 1 && brokenDiscovery.records[0].sha256.isEmpty() &&
	                 brokenDiscovery.records[0].error.isEmpty() && read(broken) == QByteArray("broken"),
	             "discovery retains corrupt candidates for subsequent full review without modifying them");
	ok &= expect(
	    write(QDir(directory).filePath(QStringLiteral("user-project.vsaudio")), QByteArray("user content")),
	    "create non-managed fixture");
	const QString nested = QDir(directory).filePath(QStringLiteral("nested"));
	QDir().mkpath(nested);
	ok &= expect(write(QDir(nested).filePath(uuid() + QStringLiteral(".vsaudio")), original),
	             "create nested fixture");
	inventory = listAudioRecoveries(directory);
	ok &= expect(inventory.records.size() == 1 && !inventory.records[0].verified() &&
	                 inventory.records[0].sha256.size() == 32 && !inventory.records[0].error.isEmpty(),
	             "corrupt records remain reviewable while non-managed and nested files are ignored");
	ok &= expect(listAudioRecoveries(directory, {[]() { return true; }}).cancelled,
	             "inventory cancellation is explicit");
	ok &= expect(!discardAudioRecovery(directory, QStringLiteral("../escape"), digest, false, &error) &&
	                 !discardAudioRecovery(directory, uuid(),
	                                       QCryptographicHash::hash({}, QCryptographicHash::Sha256), true),
	             "invalid paths and missing empty-hash records fail even without an error pointer");
	const QString countRoot = temporary.filePath(QStringLiteral("count"));
	QStringList ids;
	for (int index = 0; index < AudioRecoveryCountLimit; ++index) {
		ids.append(uuid());
		ok &= expect(!writeAudioRecovery(project, countRoot, ids.last(), &error).isEmpty(),
		             "within-budget copies are accepted");
	}
	const auto before = read(audioRecoveryPath(countRoot, ids.first()));
	ok &= expect(writeAudioRecovery(project, countRoot, uuid(), &error).isEmpty() &&
	                 read(audioRecoveryPath(countRoot, ids.first())) == before,
	             "count budget refuses new records without evicting any copy");
	ok &= expect(!writeAudioRecovery(project, countRoot, ids.first(), &error).isEmpty(),
	             "an existing copy can be updated at the count limit");
	// Oversized sparse metadata test: inventory never allocates or reads this payload.
	const QString storageRoot = temporary.filePath(QStringLiteral("storage"));
	QDir().mkpath(storageRoot);
	QFile oversized(audioRecoveryPath(storageRoot, uuid()));
	ok &= expect(oversized.open(QIODevice::WriteOnly) && oversized.resize(AudioRecoveryStorageLimit + 1),
	             "create storage-bound fixture");
	oversized.close();
	ok &= expect(
	    writeAudioRecovery(project, storageRoot, uuid(), &error).isEmpty() &&
	        listAudioRecoveries(storageRoot).records.size() == 1 &&
	        !listAudioRecoveries(storageRoot).records[0].verified(),
	    "storage budget preserves oversized records and blocks additional writes without reading them");
	const QString scanRoot = temporary.filePath(QStringLiteral("scan"));
	QDir().mkpath(scanRoot);
	for (int i = 0; i <= AudioRecoveryScanLimit; ++i) {
		ok &= write(audioRecoveryPath(scanRoot, uuid()), {});
	}
	ok &= expect(listAudioRecoveries(scanRoot).truncated &&
	                 writeAudioRecovery(project, scanRoot, uuid(), &error).isEmpty(),
	             "bounded directory scans fail closed for writes");
	if (argc > 1) {
		QJsonObject last;
		const auto cli = [&](QStringList args, int expected) {
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			process.start(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath(),
			              QStringList{QStringLiteral("--cli"), QStringLiteral("--settings-file"), temporary.filePath(QStringLiteral("cli.ini")), QStringLiteral("asset"),
			                          QStringLiteral("audio-recoveries"), QStringLiteral("--directory"),
			                          directory, QStringLiteral("--json")} +
			                  args);
			if (!process.waitForFinished(30000)) {
				process.kill();
				process.waitForFinished();
				return false;
			}
			const auto output = process.readAllStandardOutput();
			last = QJsonDocument::fromJson(output).object();
			if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != expected) {
				std::cerr << output.constData() << process.readAllStandardError().constData();
				return false;
			}
			return true;
		};
		ok &= expect(cli({}, 4) && last.value(QStringLiteral("audioRecoveries"))
		                                   .toObject()
		                                   .value(QStringLiteral("records"))
		                                   .toArray()
		                                   .size() == 1,
		             "CLI returns structured corrupt-record diagnostics and validation exit code");
		const QStringList discard{QStringLiteral("--discard"), brokenId, QStringLiteral("--expected-sha256"),
		                          QString::fromLatin1(inventory.records[0].sha256.toHex())};
		ok &= expect(cli(discard, 4) && QFileInfo::exists(broken),
		             "CLI discard defaults to dry run and retains the corrupt record");
		ok &= expect(cli(discard + QStringList{QStringLiteral("--write")}, 0) && !QFileInfo::exists(broken),
		             "CLI explicit reviewed discard removes its target");
		ok &= expect(cli({QStringLiteral("--write")}, 2) && cli({QStringLiteral("--discard"), id}, 2) &&
		                 cli({QStringLiteral("--output"), path}, 2) &&
		                 cli({QStringLiteral("--directory"), directory}, 2),
		             "CLI rejects incomplete, duplicate and unrelated mutation flags");
		ok &= expect(read(QDir(directory).filePath(QStringLiteral("user-project.vsaudio"))) ==
		                 QByteArray("user content"),
		             "discard preserves unrelated user documents");
	}
	return ok ? 0 : 1;
}
