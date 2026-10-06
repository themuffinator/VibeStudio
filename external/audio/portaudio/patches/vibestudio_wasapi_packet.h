/* VibeStudio-owned WASAPI packet policy. GPL-3.0-or-later.
 * API semantics: Microsoft IAudioCaptureClient::GetBuffer and
 * _AUDCLNT_BUFFERFLAGS, reviewed 2026-10-06; see VIBESTUDIO.md.
 * This header has no Windows dependency so fixtures exercise the same policy.
 */
#ifndef VIBESTUDIO_WASAPI_PACKET_H
#define VIBESTUDIO_WASAPI_PACKET_H
#include <stdint.h>
#include "vibestudio_portaudio.h"

/* Private to this pinned build, outside PortAudio's public callback bits. */
typedef struct VibeWasapiPacketState {
    uint64_t nextInput;
    int haveInput, outputWritten;
    PaStreamCallbackFlags flags;
    double inputTime;
} VibeWasapiPacketState;

static void VibeWasapiPacket(VibeWasapiPacketState *s, uint64_t position,
                             uint64_t qpc100ns, unsigned long frames,
                             int discontinuity, int badTimestamp)
{
    /* The initial packet establishes the origin. A transition before that
     * origin is not a hole in an already-running pass. All later holes fail.
     */
    if (s->haveInput && (discontinuity || position != s->nextInput))
        s->flags |= paInputOverflow;
    if (badTimestamp || qpc100ns == 0 || UINT64_MAX - position < frames)
        s->flags |= VIBE_PA_TIMESTAMP_ERROR;
    s->inputTime = (double)qpc100ns * 0.0000001;
    s->nextInput = position + frames;
    s->haveInput = 1;
}

static void VibeWasapiPadding(VibeWasapiPacketState *s, unsigned long padding)
{
    /* Conservative: once real audio has been submitted, an empty output
     * queue may have lost samples. Never silently heal the pass clock.
     */
    if (s->outputWritten && padding == 0)
        s->flags |= paOutputUnderflow;
}
#endif
