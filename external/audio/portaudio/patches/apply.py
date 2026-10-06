"""Produce reviewed private host sources from hash-checked, untouched upstream.

No downloads, device access or in-place upstream edits. Meson supplies a build
output. Each replacement is exact and fails closed when the pinned source changes.
"""
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent.parent


def replace(source, old, new, count=1):
    if source.count(old) != count:
        raise ValueError(f'Patch anchor count changed: {old[:100]!r}')
    return source.replace(old, new)


def patch(relative, text):
    text = text.replace('\r\n', '\n')
    if relative == 'src/hostapi/wasapi/pa_win_wasapi.c':
        text = replace(text, '#include "pa_win_wasapi.h"',
                       '#include "pa_win_wasapi.h"\n#include "vibestudio_wasapi_packet.h"')
        text = replace(text, '    PaUtilCpuLoadMeasurer      cpuLoadMeasurer;',
                       '    VibeWasapiPacketState vibePacket;\n    unsigned long vibeStoppedFlags;\n    int vibeDrain;\n    PaUtilCpuLoadMeasurer cpuLoadMeasurer;')
        text = replace(text, '    // validate PaStreamParameters', '''    /* VibeStudio: full duplex uses variable callbacks, preserving packet
       timestamps and allowing the buffer processor's zero-input path. */
    if (fullDuplex && framesPerBuffer != paFramesPerBufferUnspecified)
        return paInvalidFlag;
    if (fullDuplex)
    {
        LARGE_INTEGER frequency;
        if (!QueryPerformanceFrequency(&frequency))
            return paUnanticipatedHostError;
    }

    // validate PaStreamParameters''')
        text = replace(text, '            framesPerBuffer,\n            framesPerHostCallback,',
                       '            fullDuplex ? paFramesPerBufferUnspecified : framesPerBuffer,\n            framesPerHostCallback,')
        text = replace(text, '    PaUtil_ResetBufferProcessor(&stream->bufferProcessor);',
                       '    memset(&stream->vibePacket, 0, sizeof(stream->vibePacket));\n    stream->vibeDrain = 0;\n    stream->vibeStoppedFlags = 0;\n    PaUtil_ResetBufferProcessor(&stream->bufferProcessor);')
        text = replace(text, '''        timeInfo.outputBufferDacTime = timeInfo.currentTime + pending_time;''',
                       '''        if (PA_WASAPI__IS_FULLDUPLEX(stream) && hr != S_OK)
            stream->vibePacket.flags |= VIBE_PA_TIMESTAMP_ERROR;
        timeInfo.outputBufferDacTime = timeInfo.currentTime + pending_time;''')
        text = replace(text, '    PaUtil_BeginBufferProcessing( &stream->bufferProcessor, &timeInfo, flags );', '''    if (PA_WASAPI__IS_FULLDUPLEX(stream))
    {
        timeInfo.inputBufferAdcTime = stream->vibePacket.inputTime;
        flags = stream->vibePacket.flags;
        stream->vibePacket.flags = 0;
    }
    PaUtil_BeginBufferProcessing( &stream->bufferProcessor, &timeInfo, flags );''')
        text = replace(text, '''        PaUtil_SetInterleavedInputChannels( &stream->bufferProcessor,
            0, /* first channel of inputBuffer is channel 0 */
            inputBuffer,
            0 ); /* 0 - use inputChannelCount passed to init buffer processor */''', '''        if (inputBuffer == NULL)
            PaUtil_SetNoInput(&stream->bufferProcessor);
        else
            PaUtil_SetInterleavedInputChannels( &stream->bufferProcessor,
                0, inputBuffer, 0 );''')
        text = replace(text, '    if (callbackResult != paContinue)\n',
                       '    stream->vibeDrain = (callbackResult == paComplete);\n    if (callbackResult != paContinue)\n')
        text = replace(text, '            DWORD i_flags = 0;',
                       '            DWORD i_flags = 0;\n            UINT64 i_position = 0, i_qpc = 0;')
        text = replace(text, '''            while (o_frames != 0)
            {
                // get host input buffer''', '''            while (o_frames != 0)
            {
                /* A failed output acquisition must not reuse the preceding
                   packet's consumed count and discard unprocessed capture. */
                i_processed = 0;
                // get host input buffer''')
        text = replace(text, 'IAudioCaptureClient_GetBuffer(stream->captureClient, &i_data, &i_frames, &i_flags, NULL, NULL)',
                       'IAudioCaptureClient_GetBuffer(stream->captureClient, &i_data, &i_frames, &i_flags, &i_position, &i_qpc)')
        text = replace(text, '                        if (stream->in.monoMixer != NULL)',
                       '                        if (stream->in.monoMixer != NULL && !(i_flags & AUDCLNT_BUFFERFLAGS_SILENT))')
        text = replace(text, '''                        // Process
                        processor[S_FULLDUPLEX].processor''', '''                        VibeWasapiPacket(&stream->vibePacket, i_position, i_qpc, i_processed,
                            (i_flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0,
                            (i_flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) != 0);
                        if (i_flags & AUDCLNT_BUFFERFLAGS_SILENT)
                            i_data = NULL; /* Never read or modify a silent driver buffer. */
                        // Process
                        processor[S_FULLDUPLEX].processor''')
        text = replace(text, '''                        if ((hr = IAudioRenderClient_ReleaseBuffer(stream->renderClient, o_processed, 0)) != S_OK)
                            LogHostError(hr);''', '''                        if ((hr = IAudioRenderClient_ReleaseBuffer(stream->renderClient, o_processed, 0)) != S_OK)
                        {
                            IAudioCaptureClient_ReleaseBuffer(stream->captureClient, i_processed);
                            LogHostError(hr);
                            goto thread_error;
                        }
                        stream->vibePacket.outputWritten = 1;''')
        text = replace(text, '    // Set available frame count\n    (*available) = frames - padding;', '''    if (padding > frames)
        return E_UNEXPECTED;
    if (PA_WASAPI__IS_FULLDUPLEX(stream))
        VibeWasapiPadding(&stream->vibePacket, padding);
    // Set available frame count
    (*available) = frames - padding;''')
        text = replace(text, '''        // Start
        if ((hr = IAudioClient_Start(stream->out.clientProc)) != S_OK)''', '''        /* VibeStudio: prime duplex output with host silence, never with a
           fabricated input callback. This creates room for capture polling. */
        if (PA_WASAPI__IS_FULLDUPLEX(stream))
        {
            BYTE *silence;
            UINT32 frames;
            if ((hr = _PollGetOutputFramesAvailable(stream, &frames)) != S_OK || frames == 0)
                goto thread_error;
            if ((hr = IAudioRenderClient_GetBuffer(stream->renderClient, frames, &silence)) != S_OK)
                goto thread_error;
            if ((hr = IAudioRenderClient_ReleaseBuffer(stream->renderClient, frames, AUDCLNT_BUFFERFLAGS_SILENT)) != S_OK)
                goto thread_error;
        }
        // Start
        if ((hr = IAudioClient_Start(stream->out.clientProc)) != S_OK)''', 2)
        text = replace(text, '''    // Stop INPUT/OUTPUT clients
    if (!stream->isBlocking)''', '''    /* paComplete must play queued output. This is the host worker, after
       the application callback, with a bounded wait if the driver stalls. */
    if (stream->vibeDrain && stream->out.clientProc != NULL)
    {
        int attempts = 2000;
        HRESULT drainResult;
        UINT32 padding = 0;
        while (attempts-- > 0 &&
               (drainResult = IAudioClient_GetCurrentPadding(stream->out.clientProc, &padding)) == S_OK && padding != 0)
            Sleep(1);
        if (attempts < 0 || drainResult != S_OK)
            stream->vibeStoppedFlags |= VIBE_PA_HOST_ERROR;
        stream->vibeDrain = 0;
    }
    // Stop INPUT/OUTPUT clients
    if (!stream->isBlocking)''')
        text = replace(text, 'thread_error:\n',
                       'thread_error:\n    stream->vibeStoppedFlags |= VIBE_PA_HOST_ERROR;\n', 2)
        text = replace(text, 'thread_end:\n',
                       'thread_end:\n    if (FAILED(hr))\n        stream->vibeStoppedFlags |= VIBE_PA_HOST_ERROR;\n', 2)
        text = replace(text, '                if (i_processed == 0)\n                    break;',
                       '                if (i_processed == 0 || CheckForStop(stream))\n                    break;')
        text += '''
/* VibeStudio private extension. The owner checks inactive before calling. */
unsigned long VibePaWasapi_GetStoppedFlags(PaStream *s)
{
    PaWasapiStream *stream = (PaWasapiStream *)s;
    return stream->vibeStoppedFlags;
}
'''
        # Explicit channel counts must not silently activate the mono remapper.
        text = replace(text, '                // Force our Mono to Stereo mixer mechanism',
                       '                if (isExplicitFormat)\n                    return paInvalidChannelCount;\n                // Force our Mono to Stereo mixer mechanism')
        text = replace(text, '''            // If mono, then driver does not support 1 channel, we use internal workaround''',
                       '''            if (isExplicitFormat)
                return paInvalidChannelCount;
            // If mono, then driver does not support 1 channel, we use internal workaround''')
    elif relative == 'src/hostapi/coreaudio/pa_mac_core_internal.h':
        text = replace(text, '    volatile uint32_t xrunFlags; /*PaStreamCallbackFlags*/',
                       '    _Atomic(uint32_t) xrunFlags; /* VibeStudio: atomically consume concurrent notifications. */')
        text = '#include <stdatomic.h>\n' + text
    elif relative == 'src/hostapi/coreaudio/pa_mac_core_utilities.c':
        for flag in ('paInputOverflow', 'paOutputUnderflow'):
            text = replace(text, f'OSAtomicOr32( {flag}, &stream->xrunFlags );',
                           f'atomic_fetch_or_explicit(&stream->xrunFlags, {flag}, memory_order_relaxed);')
    elif relative == 'src/hostapi/coreaudio/pa_mac_core.c':
        text = replace(text, 'stream->xrunFlags );',
                       'atomic_exchange_explicit(&stream->xrunFlags, 0, memory_order_relaxed) );', 3)
        text = replace(text, 'int xrunFlags = stream->xrunFlags;',
                       'int xrunFlags = atomic_exchange_explicit(&stream->xrunFlags, 0, memory_order_relaxed);')
        lines = text.splitlines(keepends=True)
        # The reset after each callback handoff must not erase a concurrent xrun.
        for i, line in enumerate(lines):
            if 'stream->xrunFlags = 0;' in line and any(
                    token in ''.join(lines[max(0, i-3):i])
                    for token in ('atomic_exchange_explicit', 'xrunFlags );')):
                lines[i] = ''
        text = ''.join(lines)
    return text


def main():
    source, output = map(Path, sys.argv[1:])
    relative = source.resolve().relative_to(ROOT).as_posix()
    pin = json.loads((ROOT / 'UPSTREAM.json').read_text(encoding='utf-8'))
    data = source.read_bytes()
    if hashlib.sha256(data).hexdigest() != pin['files'][relative]:
        raise ValueError('Pinned PortAudio source hash mismatch: ' + relative)
    if output.resolve().is_relative_to(ROOT):
        raise ValueError('Patched sources belong in the build directory.')
    output.write_text(patch(relative, data.decode('utf-8')), encoding='utf-8', newline='\n')


if __name__ == '__main__':
    main()
