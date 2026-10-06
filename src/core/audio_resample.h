#pragma once

#include "core/audio_clip.h"

namespace vibestudio
{

// Nearest destination frame, ties upward. Returns -1 for invalid rates or a
// frame outside the bounded AudioClip domain. No floating-point timing drift.
qint64 audioFrameAtSampleRate(qint64 frame, int sourceRate, int targetRate);

// Whole-document, linear-phase conversion with anti-alias filtering. Keeps
// channel order and floating-point headroom. The result has the nearest frame
// count to the original duration (at least one frame for a nonempty input).
// The source is immutable; cancellation or failure returns no partial clip.
AudioClipResult resampleAudioClip(const AudioClip& clip, int targetRate, const AudioWorkControl& control = {});

} // namespace vibestudio
