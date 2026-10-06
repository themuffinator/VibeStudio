#include "core/audio_recovery_store.h"
#include "core/audio_session_io.h"
#include "tests/audio_native_fixture.h"
#include "tests/audio_range_fixture.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUuid>
#include <QtEndian>
#include <cmath>
#include <iostream>
#include <limits>
using namespace vibestudio;
namespace
{
bool expect(bool passed, const char *message)
{
	if (!passed)
		std::cerr << message << '\n';
	return passed;
}
// Independent interval oracle, including the held automation over inserted time.
qint64 sourceFrame(const AudioRangeEdit &edit, qint64 frame, bool automation = false)
{
	const auto length = edit.end - edit.first;
	if (edit.operation == "clear")
		return automation || frame < edit.first || frame >= edit.end ? frame : -1;
	if (edit.operation == "ripple-delete")
		return frame < edit.first ? frame : frame + length;
	if (edit.operation == "insert-silence")
		return frame < edit.first ? frame : frame < edit.end ? (automation ? edit.first : -1) : frame - length;
	return frame < edit.end ? frame : frame - length;
}
double scalarCurve(const QVector<AudioAutomationPoint> &points, qint64 frame)
{
	for (qsizetype i = 0; i < points.size(); ++i) {
		if (frame == points[i].frame || (i == 0 && frame < points[0].frame))
			return points[i].value;
		if (i + 1 < points.size() && frame > points[i].frame && frame < points[i + 1].frame) {
			const auto &a = points[i], &b = points[i + 1];
			const long double t = static_cast<long double>(frame - a.frame) / (b.frame - a.frame);
			const auto curve = a.curve == AudioAutomationCurve::Step     ? 0.L
			                   : a.curve == AudioAutomationCurve::Smooth ? 3 * t * t - 2 * t * t * t
			                                                             : t;
			return double(a.value + (b.value - a.value) * curve);
		}
	}
	return points.last().value;
}
bool mapping()
{
	bool ok = true;
	for (bool routed : {false, true}) {
		const auto original = test::rangeFixture(routed);
		QString error;
		AudioSessionRenderer reference;
		if (!expect(reference.prepare(original, &error), "prepare independent source arrangement"))
			return false;
		const auto originalAudio = reference.renderBlock(0, 600);
		const auto originalBytes = encodeAudioSession(original);
		for (const auto &operation : {"clear", "ripple-delete", "insert-silence", "repeat"})
			for (auto bounds :
			     {std::pair<qint64, qint64>{60, 83}, {0, 25}, {13, 109}, {87, 88}, {151, 230}, {260, 290}}) {
				const auto edit = test::rangeEdit(QLatin1String(operation), bounds.first, bounds.second);
				const auto changed = editAudioRange(original, edit);
				if (!expect(changed.succeeded(), "valid sample range edit")) {
					std::cerr << operation << ' ' << changed.error.toStdString() << '\n';
					return false;
				}
				ok &= expect(encodeAudioSession(original) == originalBytes &&
				                 changed.session.musicalTime == original.musicalTime &&
				                 original.sources[0].audio.clip.samples.constData() ==
				                     changed.session.sources[0].audio.clip.samples.constData(),
				             "range edits retain input, shared media and musical map");
				AudioSessionRenderer renderer;
				ok &= expect(renderer.prepare(changed.session, &error), "prepare edited renderer");
				const auto audio = renderer.renderBlock(0, 340);
				bool samples = audio.succeeded(), envelopes = true;
				for (int frame = 0; frame < 340; ++frame) {
					const auto mapped = sourceFrame(edit, frame);
					for (int channel = 0; channel < 2; ++channel)
						samples &= audio.clip.samples[frame * 2 + channel] ==
						           (mapped < 0 ? 0 : originalAudio.clip.samples[mapped * 2 + channel]);
					const auto oldFrame = sourceFrame(edit, frame, true);
					for (qsizetype t = 0; t < original.tracks.size(); ++t) {
						const auto &old = original.tracks[t], &next = changed.session.tracks[t];
						envelopes &= std::abs(audioAutomationValue(next.gainAutomation, frame, 0) -
						                      scalarCurve(old.gainAutomation, oldFrame)) < 2e-14;
						envelopes &= std::abs(audioAutomationValue(next.panAutomation, frame, 0) -
						                      scalarCurve(old.panAutomation, oldFrame)) < 2e-14;
						if (routed)
							envelopes &= std::abs(audioAutomationValue(next.effectAutomation[0].points, frame, 0) -
							                      scalarCurve(old.effectAutomation[0].points, oldFrame)) < 2e-14;
					}
				}
				ok &=
				    expect(samples, "every clip/fade/automated rendered sample follows the independent range mapping");
				ok &= expect(envelopes,
				             "cut automation matches independent polynomial oracle before and after boundaries");
				AudioSessionRenderer blocks;
				ok &= expect(blocks.prepare(changed.session, &error), "prepare block-invariance renderer");
				QVector<float> partitioned;
				for (int first = 0; first < 340; first += 17)
					partitioned += blocks.renderBlock(first, std::min(17, 340 - first)).clip.samples;
				ok &= expect(partitioned == audio.clip.samples, "range processing is block-boundary invariant");
			}
	}
	return ok;
}
bool scopesAndLimits()
{
	bool ok = true;
	const auto original = test::rangeFixture(true);
	auto request = test::rangeEdit("repeat");
	request.allTracks = request.masterAutomation = false;
	request.trackIds = {"ta"};
	const auto changed = editAudioRange(original, request);
	ok &= expect(changed.succeeded() && changed.trackIds == QStringList{"ta"} &&
	                 audioSessionSummary(changed.session)["tracks"].toArray()[1] ==
	                     audioSessionSummary(original)["tracks"].toArray()[1] &&
	                 changed.session.masterEffectAutomation == original.masterEffectAutomation &&
	                 changed.session.groups.size() == 1,
	             "explicit track scope does not expand groups or shift global automation");
	if (!ok)
		return false;
	request = test::rangeEdit("repeat");
	const auto grouped = editAudioRange(original, request);
	ok &= expect(grouped.succeeded() && grouped.session.groups.size() == 2 &&
	                 grouped.session.tracks[0].regions.last().groupId ==
	                     grouped.session.tracks[1].regions.last().groupId &&
	                 grouped.session.tracks[0].regions.last().groupId != "group-ab",
	             "repeated group fragments have independent links");
	request.followAutomation = request.masterAutomation = false;
	const auto anchored = editAudioRange(original, request);
	ok &=
	    expect(anchored.succeeded() && anchored.session.tracks[0].gainAutomation == original.tracks[0].gainAutomation &&
	               anchored.session.tracks[0].effectAutomation == original.tracks[0].effectAutomation &&
	               anchored.session.masterEffectAutomation == original.masterEffectAutomation,
	           "automation following can be disabled without rewriting lanes");
	for (const auto &operation : {"clear", "ripple-delete", "insert-silence", "repeat"}) {
		const auto staticEdit = editAudioRange(test::arrangementFixture(), test::rangeEdit(QLatin1String(operation)));
		ok &= expect(staticEdit.succeeded() && staticEdit.session.tracks[0].gainAutomation.isEmpty() &&
		                 staticEdit.session.tracks[0].panAutomation.isEmpty(),
		             "range edits do not create automation in empty static lanes");
	}
	auto single = original;
	single.tracks[0].gainAutomation = {{99, -7}};
	single.tracks[0].effectAutomation[0].enabled = false;
	const auto held = editAudioRange(single, test::rangeEdit("insert-silence"));
	ok &= expect(held.succeeded() && held.session.tracks[0].gainAutomation.last().frame == 122 &&
	                 !held.session.tracks[0].effectAutomation[0].enabled,
	             "a held authored point shifts and disabled effect lanes retain their read state");
	if (held.succeeded())
		for (qint64 frame = 0; frame < 260; ++frame)
			ok &= expect(audioAutomationValue(held.session.tracks[0].gainAutomation, frame, 0) == -7,
			             "single-point lanes remain constant across inserted time");
	request = test::rangeEdit("ripple-delete");
	const auto first = editAudioRange(original, request);
	const auto second = editAudioRange(first.session, request);
	if (!expect(first.succeeded() && second.succeeded(), "repeated cuts retain valid interpolation windows"))
		return false;
	for (qint64 frame = 0; frame < 260; ++frame)
		ok &= expect(audioAutomationValue(second.session.tracks[0].gainAutomation, frame, 0) ==
		                 audioAutomationValue(original.tracks[0].gainAutomation, frame < 60 ? frame : frame + 46, 0),
		             "successive cuts preserve original samples exactly");
	const auto before = encodeAudioSession(original);
	for (int mutation = 0; mutation < 13; ++mutation) {
		auto invalid = test::rangeEdit("repeat");
		if (mutation == 0)
			invalid.first = -1;
		if (mutation == 1)
			invalid.end = invalid.first;
		if (mutation == 2)
			invalid.end = AudioSessionFrameLimit + 1;
		if (mutation == 3)
			invalid.first = std::numeric_limits<qint64>::min();
		if (mutation == 4)
			invalid.end = std::numeric_limits<qint64>::max();
		if (mutation == 5)
			invalid.trackIds = {"ta"};
		if (mutation == 6)
			invalid.allTracks = false;
		if (mutation == 7) {
			invalid.allTracks = invalid.masterAutomation = false;
			invalid.trackIds = {"missing"};
		}
		if (mutation == 8) {
			invalid.allTracks = invalid.masterAutomation = false;
			invalid.trackIds = {"ta", "ta"};
		}
		if (mutation == 9)
			invalid.operation = "unknown";
		if (mutation == 10)
			invalid.followAutomation = false;
		if (mutation == 11)
			invalid.operation = "clear";
		if (mutation == 12) {
			invalid.first = AudioSessionFrameLimit - 1;
			invalid.end = AudioSessionFrameLimit;
		}
		ok &= expect(!editAudioRange(original, invalid).succeeded() && encodeAudioSession(original) == before,
		             "invalid ranges/scopes/options leave the caller unchanged");
	}
	auto crowded = original;
	auto &regions = crowded.tracks[2].regions;
	for (int i = 3; i < AudioSessionRegionLimit; ++i) {
		auto region = regions[0];
		region.id = QString::number(i);
		regions.append(region);
	}
	ok &= expect(!editAudioRange(crowded, test::rangeEdit("repeat")).succeeded(),
	             "range fragments respect clip capacity");
	auto dense = original;
	auto &points = dense.tracks[0].gainAutomation;
	points.clear();
	for (int i = 0; i < AudioAutomationPointLimit; ++i)
		points.append({i * 10, double(i % 10)});
	ok &= expect(!editAudioRange(dense, test::rangeEdit("insert-silence", 5, 6)).succeeded(),
	             "range boundaries respect point capacity");
	auto aggregate = original;
	aggregate.masterEffects.clear();
	aggregate.masterEffectAutomation.clear();
	for (auto &track : aggregate.tracks) {
		track.effects.clear();
		track.effectAutomation.clear();
		for (int i = 0; i < 8; ++i) {
			track.effects.append(makeAudioEffect("gain", aggregate.sampleRate));
			AudioEffectAutomationLane lane{track.effects.last().id, "gainDb", true, {}};
			for (int frame = 0; frame < 2048; ++frame)
				lane.points.append({frame, double(frame % 10)});
			track.effectAutomation.append(lane);
		}
	}
	ok &= expect(validateAudioSessionStructure(aggregate).isEmpty() &&
	                 !editAudioRange(aggregate, test::rangeEdit("insert-silence", 5, 6)).succeeded(),
	             "generated boundaries respect the shared 65536 effect-point limit even when each lane fits");
	auto longSession = original;
	longSession.tracks[0].regions[0].position = AudioSessionFrameLimit - 100;
	ok &= expect(!editAudioRange(longSession, test::rangeEdit("insert-silence")).succeeded(),
	             "shifted clip tail cannot exceed timeline");
	longSession = original;
	longSession.tracks[0].gainAutomation = {{0, -12, AudioAutomationCurve::Smooth}, {AudioAutomationFrameLimit, 0}};
	ok &= expect(!editAudioRange(longSession, test::rangeEdit("insert-silence")).succeeded(),
	             "authored automation beyond shifted timeline cannot be dropped silently");
	const auto far = test::rangeEdit("ripple-delete", AudioSessionFrameLimit - 15, AudioSessionFrameLimit - 8);
	const auto farCut = editAudioRange(longSession, far);
	ok &= expect(farCut.succeeded(), "sparse automation cuts near the full timeline limit");
	if (farCut.succeeded())
		for (qint64 frame = far.first - 3; frame < far.first + 7; ++frame)
			ok &=
			    expect(audioAutomationValue(farCut.session.tracks[0].gainAutomation, frame, 0) ==
			               audioAutomationValue(longSession.tracks[0].gainAutomation, sourceFrame(far, frame, true), 0),
			           "large-frame curve cuts retain precision");
	return ok;
}
QByteArray wire(const QByteArray &original, const QJsonObject &root, quint32 version = 7)
{
	const auto oldSize = qFromLittleEndian<quint32>(original.constData() + 12);
	const auto json = QJsonDocument(root).toJson(QJsonDocument::Compact);
	auto bytes = original.first(16);
	qToLittleEndian(version, bytes.data() + 8);
	qToLittleEndian(quint32(json.size()), bytes.data() + 12);
	bytes += json;
	bytes += original.mid(16 + oldSize, original.size() - 48 - oldSize);
	bytes += QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
	return bytes;
}
bool native(const QString &directory)
{
	const auto source = test::rangeFixture(true);
	const auto changed = editAudioRange(source, test::rangeEdit("ripple-delete"));
	QString error;
	const auto bytes = encodeAudioSession(changed.session, &error);
	AudioSession decoded;
	bool ok = expect(!bytes.isEmpty() && qFromLittleEndian<quint32>(bytes.constData() + 8) == 7 &&
	                     decodeAudioSession(bytes, &decoded, &error) && encodeAudioSession(decoded) == bytes,
	                 "v7 persists exact cut curves, groups, fade windows, maps and embedded media");
	if (!ok)
		return false;
	const auto root =
	    QJsonDocument::fromJson(bytes.mid(16, qFromLittleEndian<quint32>(bytes.constData() + 12))).object();
	for (int mutation = 0; mutation < 17; ++mutation) {
		auto bad = root;
		auto tracks = bad["tracks"].toArray();
		auto track = tracks[0].toObject();
		auto points = track["gainAutomation"].toArray();
		auto point = points[1].toObject(), shape = point["segment"].toObject();
		if (mutation == 0)
			shape["frames"] = -1;
		if (mutation == 1)
			shape["offset"] = -1;
		if (mutation == 2)
			shape["offset"] = shape["frames"];
		if (mutation == 3)
			shape["frames"] = 0;
		if (mutation == 4)
			shape["frames"] = 1;
		if (mutation == 5)
			shape["first"] = 25;
		if (mutation == 6)
			shape["last"] = -97;
		if (mutation == 7)
			shape["extra"] = true;
		if (mutation == 8)
			shape.remove("offset");
		if (mutation == 9)
			shape["first"] = "-12";
		if (mutation == 10)
			point["curve"] = "step";
		if (mutation == 11)
			point["value"] = -11;
		if (mutation == 12)
			shape["frames"] = 3.5;
		point["segment"] = shape;
		if (mutation == 13)
			point.remove("segment");
		if (mutation == 14)
			point["segment"] = false;
		if (mutation == 15)
			point["segment"] = QJsonArray{};
		points[1] = point;
		if (mutation == 16) {
			point["frame"] = 300;
			points.append(point);
		}
		track["gainAutomation"] = points;
		tracks[0] = track;
		bad["tracks"] = tracks;
		ok &= expect(!decodeAudioSession(wire(bytes, bad), &decoded, &error) && encodeAudioSession(decoded) == bytes,
		             "strict shape schema rejects hostile checksummed metadata transactionally");
	}
	auto raw = source.tracks[0].gainAutomation;
	raw[0].shape = {0, 96, -12, 0};
	raw[1].frame = std::numeric_limits<qint64>::min();
	ok &=
	    expect(!validAudioAutomation(raw, -96, 24), "hostile in-memory next frame cannot overflow segment validation");
	const auto oldBytes = encodeAudioSession(source);
	const auto oldRoot =
	    QJsonDocument::fromJson(oldBytes.mid(16, qFromLittleEndian<quint32>(oldBytes.constData() + 12))).object();
	ok &= expect(
	    decodeAudioSession(wire(oldBytes, test::withoutAutomationSegments(oldRoot).toObject(), 6), &decoded, &error) &&
	        encodeAudioSession(decoded) == oldBytes,
	    "v6 gain, pan, effect and master curves migrate without changing values");
	ok &= expect(!decodeAudioSession(wire(bytes, root, 6), &decoded, &error),
	             "v7 curves cannot masquerade as legacy points");
	const auto path = writeAudioSessionRecovery(changed.session, {}, directory,
	                                            QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	QFile file(path);
	if (!expect(!path.isEmpty() && file.open(QIODevice::ReadOnly), "write range recovery"))
		return false;
	const auto digest = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
	AudioSessionRecovery recovery;
	ok &= expect(readAudioSessionRecovery(path, digest, &recovery, &error) &&
	                 encodeAudioSession(recovery.session) == bytes,
	             "verified recovery retains exact cut envelopes and arrangement state");
	return ok;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir directory(QDir(root).filePath("range-core-XXXXXX"));
	if (!directory.isValid())
		return EXIT_FAILURE;
	bool ok = mapping();
	ok &= scopesAndLimits();
	ok &= native(directory.path());
	std::cout << (ok ? "Range core verification passed\n" : "Range core verification failed\n");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
