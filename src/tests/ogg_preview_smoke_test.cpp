#include "core/asset_tools.h"
#include "core/package_preview.h"
#include "core/package_staging.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; } return value;
}
void put(QByteArray& bytes, qsizetype offset, quint64 value, int count)
{
	for (int i = 0; i < count; ++i) { bytes[offset + i] = static_cast<char>(value >> (8 * i)); }
}
// Independent header fixtures from RFC 3533 and the Vorbis I identification
// layout. These deliberately omit codec packets/CRC: preview metadata is not
// an assertion that the sound can be decoded or imported.
QByteArray page(quint64 granule, quint32 sequence, quint8 flags, const QByteArray& payload = {}, quint32 serial = 17)
{
	QByteArray bytes(28, '\0'); bytes.replace(0, 4, "OggS");
	bytes[5] = static_cast<char>(flags); put(bytes, 6, granule, 8);
	put(bytes, 14, serial, 4); put(bytes, 18, sequence, 4);
	bytes[26] = 1; bytes[27] = static_cast<char>(payload.size()); bytes += payload;
	return bytes;
}
QByteArray identification(quint32 rate = 44100)
{
	QByteArray packet(30, '\0'); packet.replace(0, 7, QByteArray("\x01vorbis", 7));
	packet[11] = 2; put(packet, 12, rate, 4); packet[28] = char(0xb8); packet[29] = 1;
	return page(0, 0, 2, packet);
}
AssetAnalysis analyze(const QByteArray& bytes, bool partial = false)
{
	return analyzeAssetSample(QStringLiteral("sound.ogg"), bytes, static_cast<quint64>(bytes.size()) + (partial ? 1024 : 0));
}
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); bool ok = true;
	constexpr quint64 signedMaximum = static_cast<quint64>(std::numeric_limits<qint64>::max());
	const auto head = identification();
	const auto valid = head + page(44100, 1, 4);
	const auto ordinary = analyze(valid);
	ok &= expect(ordinary.audioDurationMs == 1000 && ordinary.audioFrameCount == 44100, "ordinary complete Vorbis header duration remains available");
	for (const quint64 granule : {signedMaximum / 1000 + 1, signedMaximum / 2, signedMaximum}) {
		const auto report = analyze(identification(48000) + page(granule, 1, 4));
		ok &= expect(report.audioDurationMs == static_cast<qint64>(granule / 48) && report.audioFrameCount == static_cast<qint64>(granule),
			"representable Ogg metadata survives multiplication-overflow boundaries exactly");
	}
	const QVector<QByteArray> outOfRange {
		head + page(signedMaximum + 1, 1, 4), head + page(std::numeric_limits<quint64>::max() - 1, 1, 4),
		identification(1) + page(signedMaximum, 1, 4),
	};
	for (const auto& bytes : outOfRange) {
		for (const bool partial : {false, true}) {
			const auto report = analyze(bytes, partial);
			ok &= expect(report.audioDurationMs == 0 && report.audioFrameCount == 0
				&& report.detailLines.join('\n').contains(QStringLiteral("supported range")),
				"unrepresentable Ogg positions/durations stay unknown with a range diagnosis, including prefixes");
		}
	}
	const auto continued = analyze(head + page(std::numeric_limits<quint64>::max(), 1, 0, QByteArray(255, 'x')) + page(44100, 2, 5, "tail"));
	ok &= expect(continued.audioDurationMs == 1000, "a packet continued across complete sequential pages retains its timing estimate");
	ok &= expect(ordinary.detailLines.join('\n').contains(QStringLiteral("Duration estimate"))
		&& ordinary.detailLines.join('\n').contains(QStringLiteral("playable length")), "header timing carries its estimate provenance");
	const auto noPosition = analyze(head + page(std::numeric_limits<quint64>::max(), 1, 4));
	ok &= expect(noPosition.audioDurationMs == 0 && noPosition.audioFrameCount == 0, "the all-ones Ogg unknown-position sentinel is not a negative duration");
	const auto embedded = analyze(head + page(44100, 1, 4, page(88200, 2, 4)));
	ok &= expect(embedded.audioDurationMs == 1000 && embedded.audioFrameCount == 44100,
		"an OggS marker inside packet bytes cannot impersonate a final page");
	QByteArray shortBody = head + page(44100, 1, 4, "1234"); shortBody[head.size() + 27] = 8;
	QByteArray version = valid; version[head.size() + 4] = 1;
	QByteArray reserved = valid; reserved[head.size() + 5] = char(0x84);
	QByteArray continuation = valid; continuation[head.size() + 5] = 5;
	const QVector<QByteArray> inconsistent {
		shortBody, version, reserved, continuation, valid + "tail", valid + valid,
		head + page(44100, 1, 4, {}, 18), head + page(44100, 2, 4),
		head + page(88200, 1, 0) + page(44100, 2, 4),
	};
	for (const auto& bytes : inconsistent) {
		const auto report = analyze(bytes);
		ok &= expect(report.audioDurationMs == 0 && report.audioFrameCount == 0,
			"short, inconsistent, chained, cross-stream or trailing Ogg pages cannot establish a total duration");
	}
	QVector<QByteArray> invalidHeaders { identification(0), identification(std::numeric_limits<quint32>::max()) };
	for (const auto& [offset, value] : QVector<QPair<qsizetype, char>>{{4, 1}, {5, 0}, {28 + 7, 1}, {28 + 11, 0}, {28 + 28, char(0x85)}, {28 + 29, 0}}) {
		auto invalid = head; invalid[offset] = value; invalidHeaders << invalid;
	}
	for (const auto& bytes : invalidHeaders) {
		const auto report = analyze(bytes + page(44100, 1, 4));
		ok &= expect(report.audioDurationMs == 0 && report.audioFrameCount == 0 && report.audioSampleRate >= 0 && !report.audioQtPlaybackCandidate,
			"invalid identification fields do not expose negative rates or usable audio headers");
	}
	const auto prefix = head + page(220500, 1, 0);
	const auto sampled = analyze(prefix, true);
	ok &= expect(sampled.audioDurationMs == 0 && sampled.audioFrameCount == 0
		&& sampled.detailLines.join('\n').contains(QStringLiteral("lower bound")), "a valid complete page in a prefix remains only a lower bound");
	const auto partialPage = analyze(prefix + page(441000, 2, 4, "payload").first(29), true);
	ok &= expect(partialPage.audioDurationMs == 0 && partialPage.detailLines.join('\n').contains(QStringLiteral("lower bound")),
		"a partially sampled next page preserves the last complete stream-position bound");
	for (qsizetype size = 0; size < valid.size(); ++size) {
		const auto report = analyze(valid.first(size), true);
		ok &= expect(report.audioDurationMs == 0 && report.audioFrameCount == 0 && report.audioSampleRate >= 0,
			"every truncated header/body boundary remains safe and cannot claim a total duration");
	}
	// Exercise byte mutations through the real parser, not just the arithmetic helper.
	for (qsizetype index = 0; index < valid.size(); ++index) {
		for (const unsigned value : {0u, 127u, 128u, 255u}) {
			auto bytes = valid; bytes[index] = static_cast<char>(value);
			const auto report = analyze(bytes);
			ok &= expect(report.audioDurationMs >= 0 && report.audioFrameCount >= 0 && report.audioSampleRate >= 0,
				"mutated Ogg metadata never exposes signed arithmetic wraparound");
		}
	}
	PackageStagingModel plan; plan.createEmpty(PackageArchiveFormat::Pk3);
	plan.addBytes(outOfRange.first(), QStringLiteral("bad.ogg"));
	PackageStagingArchive archive(plan);
	const auto preview = buildPackageEntryPreviewAt(archive, 0);
	ok &= expect(preview.kind == PackagePreviewKind::Audio && preview.audioDurationMs == 0
		&& preview.audioWaveformLines.join('\n').contains(QStringLiteral("audio editor"))
		&& preview.assetDetailLines.join('\n').contains(QStringLiteral("supported range")),
		"package preview publishes the same bounded audio diagnosis without losing its authoring handoff");
	if (argc > 1) {
		QTemporaryDir temporary; if (!temporary.isValid()) { return 1; }
		for (const auto& [name, bytes] : QVector<QPair<QString, QByteArray>>{{"valid.ogg", valid}, {"range.ogg", outOfRange.first()}, {"short.ogg", shortBody}}) {
			QFile file(temporary.filePath(name));
			if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) { return 1; } file.close();
			QProcess process; process.setWorkingDirectory(temporary.path());
			process.start(QString::fromLocal8Bit(argv[1]), {"--cli", "--settings-file", temporary.filePath("settings.ini"), "package", "preview", temporary.path(), name, "--json"});
			ok &= expect(process.waitForFinished(15000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
				"CLI reports bounded header metadata for readable package entries");
			const auto report = QJsonDocument::fromJson(process.readAllStandardOutput()).object().value("preview").toObject();
			ok &= expect(report.value("audioDurationMs").toDouble(-1) == (name == QStringLiteral("valid.ogg") ? 1000 : 0)
				&& report.value("audioDurationMsExact").toString() == (name == QStringLiteral("valid.ogg") ? QStringLiteral("1000") : QStringLiteral("0"))
				&& report.value("audioCodec").toString() == QStringLiteral("Vorbis") && report.value("audioChannels").toInt() == 2
				&& report.value("audioSampleRate").toInt() == 44100 && report.value("audioBitsPerSample").toInt(-1) == 0
				&& report.value("audioPlaybackCandidate").isBool() && report.value("totalBytesExact").toString() == QString::number(bytes.size()),
				"CLI JSON shares safe duration semantics and retains exact package byte metadata");
		}
	}
	return ok ? 0 : 1;
}
