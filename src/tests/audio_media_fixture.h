#pragma once
#include "tests/audio_range_fixture.h"
#include <QFile>

namespace vibestudio::test
{
inline AudioSession mediaFixture()
{
	auto session = rangeFixture(false);
	session.sources[0].audio.metadata = {{"fixture", "original"}};
	session.sources[0].audio.clip.markers.cues = {{1, 17, "Cue"}};
	session.sources[0].audio.clip.markers.loop = AudioLoop{8, 64};
	auto unused = session.sources[0];
	unused.id = "unused";
	unused.audio.sourceName = "Unused source";
	session.sources.append(unused);
	return session;
}
inline bool writeMediaFixture(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
inline QByteArray readMediaFixture(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
} // namespace vibestudio::test
