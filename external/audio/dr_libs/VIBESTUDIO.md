# VibeStudio decoder integration

Unmodified dr_mp3 0.7.4 and dr_flac 0.13.4 headers from [dr_libs](https://github.com/mackron/dr_libs), pinned in UPSTREAM.json. Reviewed 2026-10-04.

VibeStudio selects the MIT No Attribution (MIT-0) alternative in LICENSE; it is compatible with VibeStudio's GPLv3 licence. Headers preserve their full notices, including dr_mp3's [minimp3](https://github.com/lieff/minimp3) public-domain ancestry. Only memory-backed decoding is compiled; no audio device, file, network, encoder or implicit sample-rate conversion is used. Application wrappers impose allocation, input, output and cancellation limits.

Source hashes are checked by the repository credits validator. The full licence and this notice are included in portable packages.
