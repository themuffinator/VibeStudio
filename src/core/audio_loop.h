#pragma once
#include "core/audio_automation.h"
#include <limits>

namespace vibestudio
{
// Repeated playback has an independent physical clock. Keep arithmetic headroom
// for processing latency while retaining the authored timeline's existing bound.
inline constexpr qint64 AudioLoopClockLimit = std::numeric_limits<qint64>::max() / 2;
// The device/DSP clock always advances. Only authored source and automation
// positions wrap; preroll before the first loop end is played once.
struct AudioTimelineLoop {
	bool enabled = false;
	qint64 first = 0, end = 0;
	[[nodiscard]] bool valid() const
	{
		return (!enabled && first == 0 && end == 0) || (first >= 0 && end > first && end <= AudioAutomationFrameLimit);
	}
};
// Caller validates the immutable mapping before entering its processing loop.
inline qint64 audioLoopFrame(qint64 physicalFrame, const AudioTimelineLoop &loop)
{
	return loop.enabled && physicalFrame >= loop.end
	           ? loop.first + (physicalFrame - loop.first) % (loop.end - loop.first)
	           : physicalFrame;
}
} // namespace vibestudio
