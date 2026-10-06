#include "core/audio_session_io.h"
#include "core/audio_tempo.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QtEndian>
#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace
{
bool expect(bool condition, const char *message)
{
	if (!condition)
		std::cerr << message << '\n';
	return condition;
}
QByteArray wire(const QJsonObject &root, quint32 version)
{
	const auto json = QJsonDocument(root).toJson(QJsonDocument::Compact);
	QByteArray bytes("VSMIX\r\n\x1a", 8);
	bytes.resize(16);
	qToLittleEndian(version, bytes.data() + 8);
	qToLittleEndian(quint32(json.size()), bytes.data() + 12);
	bytes += json;
	bytes += QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
	return bytes;
}
bool nativeAndEdits()
{
	bool ok = true;
	AudioSession session;
	session.musicalTime = {90, 7, 8, {{3360, 150}}, {{5, 3, 4}}};
	const auto encoded = encodeAudioSession(session);
	AudioSession decoded;
	QString error;
	ok &= expect(!encoded.isEmpty() && qFromLittleEndian<quint32>(encoded.constData() + 8) == 7 &&
	                 decodeAudioSession(encoded, &decoded, &error) && decoded.musicalTime == session.musicalTime &&
	                 encodeAudioSession(decoded) == encoded,
	             "native version seven preserves exact map state");
	for (quint32 version = 1; version < 5; ++version) {
		auto legacy = audioSessionSummary(session);
		legacy.remove("timing");
		legacy.remove("groups");
		if (version < 3)
			legacy.remove("effects");
		if (version == 3) {
			auto fx = legacy["effects"].toObject();
			fx.remove("automation");
			legacy["effects"] = fx;
		}
		ok &= expect(decodeAudioSession(wire(legacy, version), &decoded, &error) && decoded.musicalTime.tempo == 90 &&
		                 decoded.musicalTime.beatsPerBar == 7 && decoded.musicalTime.beatUnit == 4 &&
		                 decoded.musicalTime.tempoChanges.isEmpty(),
		             "versions one through four retain their constant quarter-note tempo and meter");
	}
	const auto keep = encodeAudioSession(decoded);
	for (int mutation = 0; mutation < 8; ++mutation) {
		auto root = audioSessionSummary(session), map = root["timing"].toObject();
		if (mutation == 0)
			map["beatUnit"] = 3;
		if (mutation == 1)
			map["unknown"] = true;
		if (mutation == 2)
			map["tempoChanges"] = QJsonArray{QJsonObject{{"tick", 4.5}, {"bpm", 120}}};
		if (mutation == 3)
			map["tempoChanges"] = QJsonArray{QJsonObject{{"tick", 1}, {"bpm", 120}, {"ramp", true}}};
		if (mutation == 4)
			map["meterChanges"] = QJsonArray{QJsonObject{{"bar", 1}, {"beatsPerBar", 4}, {"beatUnit", 4}}};
		if (mutation == 5)
			map["tempoChanges"] = QJsonArray{QJsonObject{{"tick", AudioMusicalTickLimit}, {"bpm", 120}}};
		if (mutation == 6) {
			map.remove("beatUnit");
			map["tempo"] = 90;
		}
		root["timing"] = map;
		if (mutation == 7)
			root.remove("timing");
		ok &= expect(!decodeAudioSession(wire(root, 6), &decoded, &error) && encodeAudioSession(decoded) == keep,
		             "checksummed hostile timing metadata cannot overwrite caller state");
	}
	AudioProject media;
	media.clip = {1, 48000, {0.f, .1f, .2f, -.1f}};
	media.endFrame = 4;
	const auto imported = importAudioSessionSource({}, media, {}, 24000);
	ok &= expect(imported.succeeded(), "create sample-anchored fixture");
	AudioSessionEdit edit;
	edit.operation = "tempo-map";
	edit.musicalTime = session.musicalTime;
	const auto changed = editAudioSession(imported.session, edit);
	ok &= expect(changed.succeeded() && changed.session.tracks[0].regions[0].position == 24000 &&
	                 changed.session.sources[0].audio.clip.samples.constData() ==
	                     imported.session.sources[0].audio.clip.samples.constData() &&
	                 renderAudioSession(changed.session, 23999, 24005).clip.samples ==
	                     renderAudioSession(imported.session, 23999, 24005).clip.samples,
	             "tempo edits preserve authored audio, automation clock and shared source ownership");
	return ok;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const bool ok = nativeAndEdits();
	std::cout << (ok ? "Audio tempo verification passed\n" : "Audio tempo verification failed\n");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
