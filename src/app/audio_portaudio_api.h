#pragma once
// Private injectable host seam. Fixtures call the production device policy with
// synthetic PortAudio functions; they never initialize or open native devices.
#include "app/audio_duplex_device.h"
#include <portaudio.h>

namespace vibestudio
{
struct AudioPortAudioApi {
	std::function<PaError()> initialize, terminate;
	std::function<PaDeviceIndex()> deviceCount;
	std::function<const PaDeviceInfo *(PaDeviceIndex)> deviceInfo;
	std::function<const PaHostApiInfo *(PaHostApiIndex)> hostInfo;
	std::function<PaError(PaStream **, const PaStreamParameters *, const PaStreamParameters *, double, unsigned long,
	                      PaStreamFlags, PaStreamCallback *, void *)>
	    open;
	std::function<PaError(PaStream *)> start, abort, close, active;
	std::function<const PaStreamInfo *(PaStream *)> streamInfo;
	std::function<unsigned long(PaStream *)> stoppedFlags;
	std::function<const char *(PaError)> errorText;
};
std::unique_ptr<AudioDuplexDevice> createAudioDuplexDeviceForTest(std::shared_ptr<AudioPortAudioApi> api);
} // namespace vibestudio
