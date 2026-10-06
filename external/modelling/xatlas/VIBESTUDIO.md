# xatlas integration

[xatlas](https://github.com/jpcy/xatlas/tree/f700c7790aaa030e794b52ba7791a05c085faf0c),
revision `f700c7790aaa030e794b52ba7791a05c085faf0c`, reviewed 2026-10-04.
Only the original `source/xatlas/xatlas.cpp`, `xatlas.h` and root licence are
imported. No upstream models, images, viewer or build system are included.

The MIT terms, including thekla_atlas/NVIDIA and Fast-BVH ancestry, and the
embedded OpenNL BSD-3-Clause terms are compatible with VibeStudio's GPLv3.
Original notices remain intact; `LICENSE-THIRD-PARTY.txt` reproduces the OpenNL
notice for binary distributions. Both licences, this file and the pin manifest
ship in Meson installs and portable licence bundles.

`../prepare_xatlas.py` verifies the original source and header hashes and generates
build copies with three adaptations: `Mesh::createColocals` retains vertex identity instead
of welding coincident positions. VibeStudio splits corner fans along marked
seams before calling xatlas. Reconnecting them would lose the author's cuts.
The adaptation applies to intermediate chart meshes as well as input meshes.
The second adaptation disables independent-axis texel rounding unless block
alignment is enabled. This preserves texture chart shape and relative scale;
VibeStudio does not request block alignment.

The third adaptation, reviewed 2026-10-06, adds optional `resolutionHeight` to
the generated `PackOptions` header and carries separate axis limits through
area estimation, chart fitting, mask allocation, placement and output dimensions.
Zero retains the original square API. Individual chart fitting uses one uniform
scale; random placement skips a rotated chart when its axis range is negative.
The studio disables quarter-turn chart placement, normalizes packed output by
its target width and height and bounds its density search before publication.
Original vendored bytes remain unchanged. The standard C++ library is compiled
with upstream multithreading/debug exports disabled and optimization enabled,
including debug studio builds. VibeStudio already supplies the document worker.

The wrapper installs allocator and logging callbacks once. A thread-local
allocation region caps an operation at 256 MiB, supports early cancellation,
and frees outstanding raw allocations after an upstream allocation exception.
The single-threaded upstream implementation owns no external resources in
these allocations. Failure never adopts partial geometry. Packed output is
validated before copying UVs and corner splits across every animation pose.

To update: review all source notices and API changes, replace the three original
files and their hashes, review the build adaptation and allocation assumptions,
then run atlas geometry, cancellation, memory-limit, determinism, CLI, editor,
recovery and export tests on Windows, macOS and Linux. Update repository credits
and dependency records together. No network access occurs during a build.
