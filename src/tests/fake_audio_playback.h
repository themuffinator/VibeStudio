#pragma once

#include "app/audio_playback.h"
#include <QPointer>
#include <utility>

// Deterministic event source, with no device discovery or physical audio output.
// Tests control delayed callbacks and failures separately from the controller.
class FakeAudioPlaybackBackend final : public vibestudio::AudioPlaybackBackend
{
public:
	void open(const QByteArray& wav, const QString& hint, quint64 session, float level, bool repeat) override
	{
		bytes = wav;
		fileName = hint;
		currentSession = session;
		volume = level;
		loop = repeat;
		++opens;
		if (!openError.isEmpty()) { emit failed(session, openError); }
		else if (autoReady) { emit ready(session, seekable); }
	}
	void clear() override
	{
		bytes.clear();
		state = State::Stopped;
		++clears;
		// Some real backends synchronously reset position when detaching.
		emit stateChanged(currentSession, state);
		emit positionChanged(currentSession, 0);
	}
	void clearAndWait(QObject* context, std::function<void()> complete) override
	{
		if (!delayClear) { AudioPlaybackBackend::clearAndWait(context, std::move(complete)); return; }
		clear();
		pendingClear.append({context, std::move(complete)});
	}
	void finishClear()
	{
		const auto pending = std::exchange(pendingClear, {});
		for (const auto& [context, complete] : pending) { if (context) { complete(); } }
	}
	void play() override
	{
		++plays;
		if (autoState) { state = State::Playing; emit stateChanged(currentSession, state); }
	}
	void pause() override
	{
		++pauses;
		if (autoState) { state = State::Paused; emit stateChanged(currentSession, state); }
	}
	void seek(qint64 milliseconds) override
	{
		lastSeek = milliseconds;
		++seeks;
		emit positionChanged(currentSession, milliseconds);
	}
	void setVolume(float level) override { volume = level; }
	void setLoop(bool enabled) override { loop = enabled; }
	QByteArray bytes;
	QString openError, fileName;
	quint64 currentSession = 0;
	float volume = 0.7f;
	bool loop = false, seekable = true, autoReady = true, autoState = true;
	bool delayClear = false;
	QVector<std::pair<QPointer<QObject>, std::function<void()>>> pendingClear;
	State state = State::Stopped;
	int opens = 0, clears = 0, plays = 0, pauses = 0, seeks = 0;
	qint64 lastSeek = -1;
};
