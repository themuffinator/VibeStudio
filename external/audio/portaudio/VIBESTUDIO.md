# PortAudio private device backend

Unmodified selected public headers, common code and WASAPI/CoreAudio/ALSA sources
from [PortAudio](https://github.com/PortAudio/portaudio/tree/873e3c83fbe2f57ebcf59083e627a3f8fa051ffe),
revision `873e3c83fbe2f57ebcf59083e627a3f8fa051ffe`, reviewed 2026-10-06.
`UPSTREAM.json` records SHA-256 hashes of every imported upstream file. This is a
pinned development revision, not a released PortAudio 19.8 package.

PortAudio's MIT-style permissive license is compatible with VibeStudio's GPLv3.
Copyright, permission/disclaimer notices and the non-binding requests to upstream
modifications remain intact. No ASIO SDK, other host drivers, MinGW compatibility
headers, upstream build system, example or test programs are imported.

Meson builds a private C11 library. Production VibeStudio adapters are C++20.
`audio_duplex=disabled` removes the native backend; editing, offline rendering,
CLI operations and synthetic recording fixtures remain available.

## Reviewed patches

`patches/apply.py` verifies original hashes and produces host files only in the
build directory. Every replacement checks its expected occurrence count.
`patches/vibestudio_wasapi_packet.h` is original VibeStudio policy code, shared
with device-free packet fixtures. See the repository's `docs/AUDIO_DUPLEX.md`.

- WASAPI duplex callbacks preserve capture packet QPC timestamps, propagate
  discontinuity/device-position gaps and timestamp errors, and treat silent
  packets as zero input without reading or modifying their buffers. The first
  packet establishes the capture origin; subsequent gaps terminate a pass.
- Variable callback sizes preserve packet boundaries. This private build rejects
  fixed-size full-duplex WASAPI requests; the application splits work itself.
- Explicit channel counts reject implicit mono remapping. The adapter does not
  enable automatic sample-rate conversion or host-processor redirection.
- Startup primes output with host silence without a fabricated input callback.
  Empty output after real data was submitted is conservatively flagged as a
  possible underrun. Output timestamping still estimates DAC time from padding.
- `paComplete` allows a bounded host-worker drain; errors must remain visible
  to the device owner. No disk, GUI or logging is added to application callbacks.
- CoreAudio xrun flags use C11 atomic exchange/OR so notification callbacks cannot
  race with a read-then-clear. The adapter requires one duplex device and the
  requested device rate, avoiding its separate-device resampling path.

Packet semantics follow Microsoft's
[GetBuffer contract](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-getbuffer)
and [buffer flags](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/ne-audioclient-_audclnt_bufferflags),
reviewed 2026-10-06. Capture QPC values are already in 100 ns units; padding-based
output times and device-reported latency are estimates requiring loopback
calibration and physical platform acceptance. Source-level fixtures do not prove
hardware clock quality, drift tolerance or real-time scheduling guarantees.
