# r8brain integration

Sample rate converter designed by Aleksey Vaneev of Voxengo.

The headers and `fft/fft4g.h` are unchanged from
[r8brain-free-src 7.5](https://github.com/avaneev/r8brain-free-src/tree/cb2abb9977efe2471979b380ed95daa56ab4fdb9),
revision `cb2abb9977efe2471979b380ed95daa56ab4fdb9`, reviewed 2026-10-04.
`UPSTREAM.json` records SHA-256 hashes; Git blob hashes were checked at import.
`.gitattributes` preserves original bytes across platform checkouts, and the
credits validator checks every pinned file against the manifest.

The MIT licence in `LICENSE` and Ooura's permission in `LICENSE-OOURA.txt` were
reviewed before incorporation and are compatible with VibeStudio's GPL-3.0
licence. Copyright headers remain intact. The optional PFFFT, Intel IPP, test,
benchmark, and external tool sources are not included or enabled.

`src/core/audio_resample.cpp` includes the library, using the double
precision Ooura backend, `CDSPResampler24`, a 2% transition band, and linear
phase. Bounded streaming, cancellation, channel interleaving, float validation,
and duration/selection mapping belong to VibeStudio. Source boundaries are zero
extended; output ends at the nearest duration frame, with a minimum of one for
nonempty audio. Filter ringing is retained as floating-point headroom.

`src/core/audio_analysis.cpp` also uses the same double-precision converter and
cache limits for read-only true peak. It streams 8×/4×/2× output without storing
an enlarged document, explicitly adds enough silence on both sides to retain
filter ringing, and keeps the result at least as large as sample peak. Both
wrappers poll cancellation between bounded chunks. Loudness uses a separate
libebur128 state with no padding, so metering does not alter programme gates.

The wrapper limits cached FIR filters to eight and fractional banks to two.
Meson marks these headers as external to keep upstream warnings separate from
VibeStudio's warnings-as-errors build. No network fetch or extra installed
library is needed to build on Windows, macOS, or Linux. Portable packages and
Meson installs carry both licences and this attribution.

To update, review upstream changes and licences, replace the unchanged header
set at one pinned revision, regenerate the manifest, update repository credits,
then run the audio numerical, CLI, editor, and packaging checks. Do not format
or edit upstream files in place; document any required patch separately.
