#include "core/audio_recovery_store.h"
#include "tests/audio_arrangement_fixture.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
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
bool expect(bool value, const char *message)
{
	if (!value)
		std::cerr << message << '\n';
	return value;
}
AudioArrangementEdit edit(const char *operation, QStringList ids = {"a"})
{
	AudioArrangementEdit result;
	result.operation = QLatin1String(operation);
	result.regionIds = ids;
	return result;
}
QByteArray wire(const QByteArray &original, const QJsonObject &metadata, quint32 version = 7)
{
	const auto oldSize = qFromLittleEndian<quint32>(original.constData() + 12);
	const auto json = QJsonDocument(metadata).toJson(QJsonDocument::Compact);
	auto bytes = original.first(16);
	qToLittleEndian(version, bytes.data() + 8);
	qToLittleEndian(quint32(json.size()), bytes.data() + 12);
	bytes += json;
	bytes += original.mid(16 + oldSize, original.size() - 48 - oldSize);
	bytes += QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
	return bytes;
}
bool arrangement()
{
	const auto original = test::arrangementFixture();
	const auto unchanged = audioSessionSummary(original);
	bool ok = expect(validateAudioSession(original).isEmpty(), "valid fixture");
	QString error;
	ok &= expect(audioArrangementSelection(original, {"b", "c"}, true, &error) == QStringList({"a", "b", "c"}) &&
	                 error.isEmpty(),
	             "linked selection expands across tracks in canonical session order");
	for (const auto &ids : {QStringList{}, QStringList{"a", "a"}, QStringList{"a", "missing"}})
		ok &= expect(audioArrangementSelection(original, ids, true, &error).isEmpty() && !error.isEmpty(),
		             "invalid target sets fail explicitly");
	auto move = edit("move");
	move.offset = 15;
	const auto moved = editAudioArrangement(original, move);
	ok &= expect(moved.succeeded() && moved.session.tracks[0].regions[0].position == 35 &&
	                 moved.session.tracks[1].regions[0].position == 55 &&
	                 moved.session.tracks[2].regions[0].position == 140 &&
	                 moved.session.sources[0].audio.clip.samples.constData() ==
	                     original.sources[0].audio.clip.samples.constData(),
	             "linked move preserves offsets, tracks and immutable source storage");
	for (auto offset : {qint64(-21), AudioSessionFrameLimit, std::numeric_limits<qint64>::min(),
	                    std::numeric_limits<qint64>::max()}) {
		move.offset = offset;
		ok &= expect(!editAudioArrangement(original, move).succeeded() && unchanged == audioSessionSummary(original),
		             "invalid movement cannot partially move a group");
	}
	move.offset = -20;
	move.linkedGroups = false;
	const auto single = editAudioArrangement(original, move);
	ok &= expect(single.succeeded() && single.session.tracks[0].regions[0].position == 0 &&
	                 single.session.tracks[1].regions[0].position == 40,
	             "linking off edits just the requested clip");
	auto duplicate = edit("duplicate");
	duplicate.offset = 100;
	const auto copies = editAudioArrangement(original, duplicate);
	ok &= expect(
	    copies.succeeded() && copies.addedRegionIds.size() == 2 && copies.selectedRegionIds == copies.addedRegionIds &&
	        copies.session.groups.size() == 2 && copies.session.tracks[0].regions[1].groupId != "group-ab" &&
	        copies.session.tracks[0].regions[1].groupId == copies.session.tracks[1].regions[1].groupId &&
	        copies.session.tracks[0].regions[1].position == 120 && copies.session.tracks[1].regions[1].position == 140,
	    "duplicates get fresh clip and group identities with spacing intact");
	duplicate.linkedGroups = false;
	const auto partial = editAudioArrangement(original, duplicate);
	ok &= expect(partial.succeeded() && partial.session.groups.size() == 1 &&
	                 partial.session.tracks[0].regions[1].groupId.isEmpty(),
	             "a partial group copy never links back to the originals");
	auto group = edit("group");
	group.name = "Renamed linked clips";
	const auto renamed = editAudioArrangement(original, group);
	ok &= expect(renamed.succeeded() && renamed.groupId == "group-ab" && renamed.session.groups[0].name == group.name,
	             "renaming an exact group keeps its identity");
	group.regionIds = {"a", "c"};
	const auto merged = editAudioArrangement(original, group);
	ok &= expect(merged.succeeded() && merged.session.groups.size() == 1 && merged.groupId != "group-ab" &&
	                 merged.session.tracks[2].regions[0].groupId == merged.groupId &&
	                 merged.session.tracks[1].regions[0].groupId == merged.groupId,
	             "grouping linked selections merges every effective member");
	group.linkedGroups = false;
	const auto stolen = editAudioArrangement(original, group);
	ok &= expect(stolen.succeeded() && stolen.session.groups.size() == 1 &&
	                 stolen.session.tracks[1].regions[0].groupId.isEmpty(),
	             "regrouping a partial selection releases old singleton members");
	group.regionIds = {"c"};
	ok &= expect(!editAudioArrangement(original, group).succeeded(), "singleton groups rejected");
	group.regionIds = {"a", "c"};
	for (const auto &name : {QString(), QString(257, 'x'), QString(QChar(0))}) {
		group.name = name;
		ok &= expect(!editAudioArrangement(original, group).succeeded(), "invalid group names rejected");
	}
	auto ungroup = edit("ungroup");
	ungroup.linkedGroups = false;
	const auto ungrouped = editAudioArrangement(original, ungroup);
	ok &= expect(ungrouped.succeeded() && ungrouped.session.groups.isEmpty() &&
	                 ungrouped.session.tracks[1].regions[0].groupId.isEmpty(),
	             "ungrouping also normalizes the remaining singleton");
	const auto removed = editAudioArrangement(original, edit("remove"));
	ok &= expect(removed.succeeded() && removed.selectedRegionIds.isEmpty() && removed.session.groups.isEmpty() &&
	                 removed.session.tracks[0].regions.isEmpty() && removed.session.tracks[1].regions.isEmpty() &&
	                 removed.session.sources.size() == original.sources.size(),
	             "linked removal retains shared source snapshots for history");
	AudioSessionEdit track;
	track.operation = "remove-track";
	track.trackId = "ta";
	const auto removedTrack = editAudioSession(original, track);
	ok &= expect(removedTrack.succeeded() && removedTrack.session.groups.isEmpty() &&
	                 removedTrack.session.tracks[0].regions[0].groupId.isEmpty(),
	             "ordinary track removal cannot leave an invalid group");
	auto gain = edit("gain");
	gain.gainDb = -9;
	const auto gained = editAudioArrangement(original, gain);
	ok &= expect(gained.succeeded() && gained.session.tracks[0].regions[0].gainDb == -9 &&
	                 gained.session.tracks[1].regions[0].gainDb == -9,
	             "grouped clip gain uses absolute validated values");
	gain.gainDb = std::numeric_limits<double>::quiet_NaN();
	ok &= expect(!editAudioArrangement(original, gain).succeeded(), "nonfinite batch gain rejected");
	auto mute = edit("mute");
	mute.muted = true;
	const auto muted = editAudioArrangement(original, mute);
	ok &= expect(muted.succeeded() && muted.session.tracks[0].regions[0].muted &&
	                 muted.session.tracks[1].regions[0].muted && !muted.session.tracks[2].regions[0].muted,
	             "group mute does not affect unselected clips");
	auto crowded = test::arrangementFixture(false);
	auto &regions = crowded.tracks[2].regions;
	for (int i = 3; i < AudioSessionRegionLimit; ++i) {
		auto region = regions[0];
		region.id = QString::number(i);
		regions.append(region);
	}
	ok &= expect(validateAudioSessionStructure(crowded).isEmpty() &&
	                 !editAudioArrangement(crowded, duplicate).succeeded(),
	             "batch copies respect the existing global clip capacity");
	return ok;
}
bool fadesAndNative(const QString &directory)
{
	bool ok = true;
	AudioSession divided;
	for (bool routed : {false, true}) {
		auto original = test::arrangementFixture();
		if (routed) {
			AudioSessionTrack bus;
			bus.id = "bus";
			bus.name = "Bus";
			bus.routing.bus = true;
			original.tracks.append(bus);
			original.tracks[0].routing.outputId = "bus";
			original.tracks[1].routing.outputId = "bus";
			original.masterEffects = {makeAudioEffect("gain", 1000)};
		}
		const auto reference = renderAudioSession(original);
		if (!expect(reference.succeeded(), "render original fade fixture"))
			return false;
		// Independent numeric oracle, including overlapping fade products.
		for (qint64 frame = 0; frame < reference.clip.frameCount(); ++frame)
			for (int channel = 0; channel < 2; ++channel) {
				double value = 0;
				for (int i = 0; i < 3; ++i) {
					const auto &clip = original.tracks[i].regions[0];
					const auto local = frame - clip.position;
					if (local < 0 || local >= clip.length)
						continue;
					const double ramp = i == 0 ? 79 : 59;
					const double envelope = i == 2 ? 1
					                               : std::min(1.0, double(local) / ramp) *
					                                     std::min(1.0, double(clip.length - local - 1) / ramp);
					value +=
					    original.sources[0].audio.clip.samples[(clip.sourceOffset + local) * 2 + channel] * envelope;
				}
				ok &= expect(std::abs(reference.clip.samples[frame * 2 + channel] - value) < 1e-7,
				             "independent overlapping fade oracle");
			}
		auto split = edit("split");
		split.position = 60;
		const auto first = editAudioArrangement(original, split);
		if (!expect(first.succeeded(), "split linked clips inside overlapping fades"))
			return false;
		ok &=
		    expect(first.addedRegionIds.size() == 2 && first.session.groups.size() == 2 &&
		               first.session.tracks[0].regions[1].fadeStart == 40 &&
		               first.session.tracks[0].regions[1].fadeSpan == 100 &&
		               renderAudioSession(first.session).clip.samples == reference.clip.samples,
		           "splits preserve exact audio through both renderer paths and create independent right-hand groups");
		split.regionIds = {first.addedRegionIds[0]};
		split.position = 90;
		const auto again = editAudioArrangement(first.session, split);
		ok &= expect(again.succeeded() && again.addedRegionIds.size() == 2 &&
		                 renderAudioSession(again.session).clip.samples == reference.clip.samples,
		             "repeated splits retain the original envelope domain");
		AudioSessionEdit legacy;
		legacy.operation = "split";
		legacy.trackId = "ta";
		legacy.regionId = "a";
		legacy.position = 21;
		const auto single = editAudioSession(original, legacy);
		ok &= expect(single.succeeded() && single.session.tracks[1].regions.size() == 1 &&
		                 renderAudioSession(single.session).clip.samples == reference.clip.samples,
		             "legacy precise single-clip split also preserves fade audio");
		divided = again.session;
	}
	const auto &clip = divided.tracks[0].regions[1];
	AudioSessionEdit rename;
	rename.operation = "region";
	rename.trackId = "ta";
	rename.regionId = clip.id;
	rename.name = "Renamed split segment";
	rename.position = clip.position;
	rename.sourceOffset = clip.sourceOffset;
	rename.length = clip.length;
	rename.fadeIn = clip.fadeIn;
	rename.fadeOut = clip.fadeOut;
	const auto retained = editAudioSession(divided, rename);
	ok &= expect(retained.succeeded() && retained.session.tracks[0].regions[1].fadeStart == clip.fadeStart &&
	                 renderAudioSession(retained.session).clip.samples == renderAudioSession(divided).clip.samples,
	             "single clip property edits retain inherited fade windows");
	auto fades = edit("fades", {clip.id});
	fades.fadeIn = 5;
	fades.fadeOut = 7;
	const auto reset = editAudioArrangement(divided, fades);
	ok &= expect(reset.succeeded() && reset.session.tracks[0].regions[1].fadeStart == 0 &&
	                 reset.session.tracks[0].regions[1].fadeSpan == 0,
	             "explicit batch fades start a new visible-length envelope");
	fades.fadeIn = 1000;
	ok &= expect(!editAudioArrangement(divided, fades).succeeded(),
	             "a fade exceeding any clip length fails the entire edit");
	QString error;
	const auto bytes = encodeAudioSession(divided, &error);
	if (!expect(!bytes.isEmpty(), "encode grouped split session"))
		return false;
	AudioSession decoded;
	ok &= expect(qFromLittleEndian<quint32>(bytes.constData() + 8) == 7 &&
	                 decodeAudioSession(bytes, &decoded, &error) && encodeAudioSession(decoded) == bytes,
	             "native v7 preserves group identities and inherited envelopes exactly");
	const auto metadata =
	    QJsonDocument::fromJson(bytes.mid(16, qFromLittleEndian<quint32>(bytes.constData() + 12))).object();
	for (int mutation = 0; mutation < 12; ++mutation) {
		auto root = metadata;
		auto tracks = root["tracks"].toArray();
		auto track = tracks[0].toObject();
		auto regions = track["regions"].toArray();
		auto region = regions[1].toObject();
		auto groups = root["groups"].toArray();
		if (mutation == 0)
			region["fadeStart"] = -1;
		if (mutation == 1)
			region["fadeSpan"] = "100";
		if (mutation == 2)
			region["fadeSpan"] = 0;
		if (mutation == 3)
			region["fadeStart"] = 99;
		if (mutation == 4)
			region["fadeSpan"] = AudioSampleLimit + 1;
		if (mutation == 5)
			region["groupId"] = "missing";
		if (mutation == 6)
			groups.append(groups[0]);
		if (mutation == 7)
			groups.append(QJsonObject{{"id", "empty"}, {"name", "Unused"}});
		if (mutation == 8) {
			auto group = groups[0].toObject();
			group["name"] = "";
			groups[0] = group;
		}
		if (mutation == 9)
			region.remove("groupId");
		if (mutation == 10)
			region["unknown"] = true;
		regions[1] = region;
		track["regions"] = regions;
		tracks[0] = track;
		root["tracks"] = tracks;
		root["groups"] = groups;
		if (mutation == 11)
			root.remove("groups");
		ok &= expect(!decodeAudioSession(wire(bytes, root), &decoded, &error) && encodeAudioSession(decoded) == bytes,
		             "checksummed hostile v7 metadata cannot overwrite a valid caller session");
	}
	// A genuine v5 descriptor has no groups or fade-window fields.
	const auto ordinary = test::arrangementFixture(false);
	const auto oldBytes = encodeAudioSession(ordinary);
	auto legacy =
	    QJsonDocument::fromJson(oldBytes.mid(16, qFromLittleEndian<quint32>(oldBytes.constData() + 12))).object();
	legacy.remove("groups");
	auto tracks = legacy["tracks"].toArray();
	for (qsizetype i = 0; i < tracks.size(); ++i) {
		auto track = tracks[i].toObject();
		auto regions = track["regions"].toArray();
		for (qsizetype j = 0; j < regions.size(); ++j) {
			auto region = regions[j].toObject();
			for (const auto key : {"groupId", "fadeStart", "fadeSpan"})
				region.remove(key);
			regions[j] = region;
		}
		track["regions"] = regions;
		tracks[i] = track;
	}
	legacy["tracks"] = tracks;
	ok &= expect(decodeAudioSession(wire(oldBytes, legacy, 5), &decoded, &error) &&
	                 audioSessionSummary(decoded) == audioSessionSummary(ordinary),
	             "v5 media and timing migrate without adding groups or altering fades");
	const auto recovery =
	    writeAudioSessionRecovery(divided, {}, directory, QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	QFile file(recovery);
	if (!expect(!recovery.isEmpty() && file.open(QIODevice::ReadOnly), "write grouped recovery"))
		return false;
	const auto digest = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
	AudioSessionRecovery restored;
	ok &= expect(readAudioSessionRecovery(recovery, digest, &restored, &error) &&
	                 encodeAudioSession(restored.session) == bytes,
	             "verified recovery retains named groups and repeated split envelopes");
	return ok;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath("arrangement-core-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	bool ok = arrangement();
	ok &= fadesAndNative(temporary.path());
	std::cout << (ok ? "Arrangement core verification passed\n" : "Arrangement core verification failed\n");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
