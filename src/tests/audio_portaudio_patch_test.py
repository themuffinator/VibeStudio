"""Device-free integrity and generated-host regression checks."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
vendor = ROOT / 'external/audio/portaudio'
spec = importlib.util.spec_from_file_location('vibe_pa_patch', vendor / 'patches/apply.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)
pin = json.loads((vendor / 'UPSTREAM.json').read_text(encoding='utf-8'))
generated = {}
for relative, expected in pin['files'].items():
    path = (vendor / relative).resolve()
    assert path.is_relative_to(vendor.resolve()) and not path.is_symlink(), relative
    raw = path.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == expected, relative
    if relative.startswith('src/hostapi/wasapi/') or relative.startswith('src/hostapi/coreaudio/'):
        original = raw.decode('utf-8')
        result = patcher.patch(relative, original)
        assert result == patcher.patch(relative, original), 'non-deterministic host patch'
        assert 'Permission is hereby granted' in result and 'Copyright' in result, relative
        generated[relative] = result

windows = generated['src/hostapi/wasapi/pa_win_wasapi.c']
assert '&i_position, &i_qpc)' in windows
assert 'i_processed = 0;\n                // get host input buffer' in windows
assert 'i_processed == 0 || CheckForStop(stream)' in windows
assert 'PaUtil_SetNoInput(&stream->bufferProcessor)' in windows
assert 'fullDuplex ? paFramesPerBufferUnspecified : framesPerBuffer' in windows
assert windows.count('stream->vibeStoppedFlags |= VIBE_PA_HOST_ERROR;') == 5
assert 'VibePaWasapi_GetStoppedFlags' in windows
assert 'if (isExplicitFormat)\n                return paInvalidChannelCount;' in windows
mac = generated['src/hostapi/coreaudio/pa_mac_core.c']
assert mac.count('atomic_exchange_explicit(&stream->xrunFlags, 0, memory_order_relaxed)') == 4
assert mac.count('stream->xrunFlags = 0;') == 2  # Initialization and restart only.
assert 'volatile uint32_t xrunFlags' not in generated['src/hostapi/coreaudio/pa_mac_core_internal.h']
assert 'OSAtomicOr32(' not in generated['src/hostapi/coreaudio/pa_mac_core_utilities.c']
try:
    patcher.patch('src/hostapi/wasapi/pa_win_wasapi.c', windows)
except ValueError:
    pass
else:
    raise AssertionError('double patch/source drift must fail closed')
print(f'Checked {len(pin["files"])} pinned files, deterministic WASAPI/CoreAudio patches and drift rejection.')
