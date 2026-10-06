#pragma once
#include "core/audio_arrangement.h"

namespace vibestudio::test
{
inline AudioSession arrangementFixture(bool grouped = true)
{
	AudioSession session;
	session.sampleRate = 1000;
	session.effectTailSeconds = 0;
	session.name = QStringLiteral("Arrangement fixture");
	AudioProject audio;
	audio.clip.channels = 2;
	audio.clip.sampleRate = 1000;
	audio.clip.samples.resize(256);
	for (qsizetype i = 0; i < audio.clip.samples.size(); ++i)
		audio.clip.samples[i] = float((i % 37) - 18) / 64;
	audio.endFrame = 128;
	audio.sourceName = QStringLiteral("Independent samples");
	session.sources.append({QStringLiteral("media"), audio});
	for (int i = 0; i < 3; ++i) {
		AudioSessionTrack track;
		track.id = QStringLiteral("t") + QChar('a' + i);
		track.name = QStringLiteral("Track ") + QChar('A' + i);
		AudioSessionRegion region;
		region.id = QString(QChar('a' + i));
		region.name = QStringLiteral("Clip ") + QChar('A' + i);
		region.sourceId = QStringLiteral("media");
		region.position = i == 0 ? 20 : i == 1 ? 40 : 140;
		region.sourceOffset = i * 4;
		region.length = i == 0 ? 100 : i == 1 ? 80 : 16;
		region.fadeIn = region.fadeOut = i == 0 ? 80 : i == 1 ? 60 : 0;
		if (i < 2 && grouped)
			region.groupId = QStringLiteral("group-ab");
		track.regions.append(region);
		session.tracks.append(track);
	}
	if (grouped)
		session.groups.append({QStringLiteral("group-ab"), QStringLiteral("Linked A and B")});
	return session;
}
} // namespace vibestudio::test
