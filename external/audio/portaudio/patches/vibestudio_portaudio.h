/* Private VibeStudio extension to the pinned backend, GPL-3.0-or-later. */
#ifndef VIBESTUDIO_PORTAUDIO_H
#define VIBESTUDIO_PORTAUDIO_H
#include "portaudio.h"
#define VIBE_PA_TIMESTAMP_ERROR (1UL << 16)
#define VIBE_PA_HOST_ERROR (1UL << 17)
#ifdef __cplusplus
extern "C" {
#endif
#ifdef _WIN32
/* Call only after Pa_IsStreamActive returns zero, before closing the stream. */
unsigned long VibePaWasapi_GetStoppedFlags(PaStream *stream);
#endif
#ifdef __cplusplus
}
#endif
#endif
