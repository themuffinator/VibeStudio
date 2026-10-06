# VibeStudio loudness metering integration

[libebur128 1.2.6](https://github.com/jiixyj/libebur128/tree/67b33abe1558160ed76ada1322329b0e9e058b02)
is pinned at `67b33abe1558160ed76ada1322329b0e9e058b02`, reviewed 2026-10-04.
Jan Kokemüller's library and Chris Moeller's R128Scan-derived filter code use
MIT terms. The internal University of California queue uses BSD-3-Clause.
These permissive licences are compatible with VibeStudio's GPLv3 licence.
All imported bytes and notices are unchanged and recorded in `UPSTREAM.json`.

Meson builds a private static C dependency. The application adapter remains
C++20 and shares read-only metering between the editor and CLI; no device,
network access, optional Qt Multimedia module or external executable is needed.
The adapter bounds formats, input, processing chunks and cancellation, and
serializes library calls because this release rewrites shared constants during
initialization. Only integrated loudness uses this library. True peak uses the
already pinned r8brain-free-src converter with double precision and explicit
silence on both sides, without adding padding to the loudness programme.
Explicit channel roles govern integrated loudness; channel count alone never
guesses a surround layout. The upstream short true-peak interpolator was
evaluated but is not used; independent broadband reconstruction checks informed
that choice. Imported upstream source remains unmodified.

Preserve `COPYING`, both `doc/license` notices and the source headers on update.
Regenerate source hashes and run numerical, channel-map, cancellation, concurrent
analysis, GUI, CLI and portable-license tests on each supported build platform.
The library version or a passing fixture does not by itself certify a meter.
