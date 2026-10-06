#pragma once
#include "core/audio_project.h"
#include <QStringList>
#include <functional>

namespace vibestudio
{
using AudioOutputEmit = std::function<bool(const QByteArray &)>;
using AudioOutputProducer = std::function<bool(const AudioOutputEmit &, QString *)>;
// Shared atomic output, hard-link/source protection, lock and digest guards.
// The producer runs for dry runs too, emitting into a digest without file I/O.
AudioProjectSaveReport commitAudioOutput(const AudioProjectSaveRequest &request, const QString &suffix,
                                         QStringList protectedPaths, qint64 expectedByteLimit,
                                         const AudioWorkControl &control, const AudioOutputProducer &produce);
} // namespace vibestudio
