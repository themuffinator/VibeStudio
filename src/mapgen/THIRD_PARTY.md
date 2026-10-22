# Third-party software and references

## Pinned inference and tensor libraries

Quake-MapGen pins [llama.cpp v0.3.0](https://github.com/ggml-org/llama.cpp/releases/tag/v0.3.0) at `c1d0e7a004015f23bc0233470b747b596f29b264` together with its synchronized ggml v0.22.0 implementation. The executable links llama.cpp for explicit local GGUF planning and bundled ggml for inference plus deterministic layout ranking. Both projects are MIT-licensed, which is compatible with this repository's MIT licence, and distributions must retain their copyright and permission notices. Copies are installed from `licenses/llama.cpp-LICENSE.txt` (in the Quake-MapGen repository; not imported) and `licenses/ggml-THIRD-PARTY-NOTICES.txt` (in the Quake-MapGen repository; not imported).

The original adapter was written against llama.cpp's public headers, [simple inference example](https://github.com/ggml-org/llama.cpp/blob/c1d0e7a004015f23bc0233470b747b596f29b264/examples/simple/simple.cpp), [simple chat example](https://github.com/ggml-org/llama.cpp/blob/c1d0e7a004015f23bc0233470b747b596f29b264/examples/simple-chat/simple-chat.cpp), and [GBNF guide](https://github.com/ggml-org/llama.cpp/blob/c1d0e7a004015f23bc0233470b747b596f29b264/grammars/README.md). The layout ranker's graph lifecycle follows ggml's [simple examples](https://github.com/ggml-org/ggml/tree/v0.22.0/examples/simple). No example implementation was copied into this repository.

Optional GPU and third-party BLAS backends, as well as model files and their training data, can have additional terms. They are not enabled or distributed by the M1 CPU build. Users must audit the exact model licence before use or redistribution. Reports identify the selected whole GGUF by SHA-256; because the tokenizer is embedded, M1 records the same whole-file digest as its tokenizer-source fingerprint rather than claiming a tokenizer-only digest.

The unbundled real-inference validation fixture was the Apache-2.0 [SmolLM2-135M-Instruct Q8_0 GGUF](https://huggingface.co/unsloth/SmolLM2-135M-Instruct-GGUF/blob/9e6855bc4be717fca1ef21360a1db4b29d5c559a/SmolLM2-135M-Instruct-Q8_0.gguf), derived from [HuggingFaceTB/SmolLM2-135M-Instruct](https://huggingface.co/HuggingFaceTB/SmolLM2-135M-Instruct). It is used only as a separately downloaded, hash-pinned test input and is not a project dependency or distribution artifact.

## Separately installed tools

[ericw-tools](https://github.com/ericwa/ericw-tools) is used only as an optional external compiler process. M2/M3 validation was checked against clean source revision [`8b70fce25558f2662ebee0855c12d35fe21e7beb`](https://github.com/ericwa/ericw-tools/commit/8b70fce25558f2662ebee0855c12d35fe21e7beb) and these separately installed 2.0.0-alpha11 artifacts:

| Tool | SHA-256 |
| --- | --- |
| `qbsp` | `fad5a4dc4daf42773ad9a812349b2667221bec47a920cad10cb821ae442c8eb8` |
| `vis` | `01be572845e05c87b01a0b667e590ac49bdaba23369af8529fe82fcfbc4fe6cf` |
| `light` | `8e4fe9821f73c2d5c8a13655be20d3a71f357e76171de37c02413f4da3131a2f` |

Every enabled gate requires the corresponding path and expected hash from the caller; no local path is compiled into the project. ericw-tools is GPL-licensed and is not linked, copied, or distributed here. Embree-enabled builds carry additional GPLv3 implications; consult its own notices.

Q3 validation invokes [NetRadiant Custom](https://github.com/Garux/netradiant-custom) `q3map2` and `mbspc` as external GPL processes built from clean source revision [`449778b3840f8f2062daab15c87cb012f2ac945c`](https://github.com/Garux/netradiant-custom/commit/449778b3840f8f2062daab15c87cb012f2ac945c). The exact locally tested artifacts are q3map2 2.5.17n-git-449778b with SHA-256 `7cc3963b21fa937880935e818624eb28c5424f7ff312148b13468e7afb1a71e7` and mbspc/BSPC 2.2 with SHA-256 `77c9b9cac946e7d770bce9d934404b421e51ebe01963cd864c9d0e245243b003`; CMake requires callers to supply and match both hashes. The tested Windows build resolved its runtime DLLs from `C:\msys64\mingw64\bin`, but callers supply their own runtime directory rather than relying on that machine-specific path. The executables, runtime libraries, build objects, private `bspc.log` files, game data, and generated compiler artifacts remain outside the installed project and are neither linked nor redistributed.

## Behavioral and format references

The following GPL source releases are authoritative behavioral/format references.
No external implementation is copied or translated into this MIT repository.
The optional private movement executable described below separately links a
pinned external Quake II source subset; it is not part of `qmg_core`:

- [id Software Quake](https://github.com/id-Software/Quake)
- [id Software Quake II](https://github.com/id-Software/Quake-2)
- [id Software Quake III Arena](https://github.com/id-Software/Quake-III-Arena)
- Quake [standing and large BSP hulls](https://github.com/id-Software/Quake/blob/master/QW/server/model.c), [18-unit step behavior](https://github.com/id-Software/Quake/blob/master/QW/server/sv_move.c), and [default gravity](https://github.com/id-Software/Quake/blob/master/QW/server/sv_phys.c)
- Quake II [player hull, jump, gravity, and 18-unit step movement](https://github.com/id-Software/Quake-2/blob/master/qcommon/pmove.c), [ground-monster step behavior](https://github.com/id-Software/Quake-2/blob/master/game/m_move.c), and a representative [large ground-monster hull](https://github.com/id-Software/Quake-2/blob/master/game/m_gladiator.c)
- Quake III Arena [player hull movement](https://github.com/id-Software/Quake-III-Arena/blob/master/code/game/bg_pmove.c), [18-unit step and 270-unit jump constants](https://github.com/id-Software/Quake-III-Arena/blob/master/code/game/bg_local.h), and [800-unit default gravity](https://github.com/id-Software/Quake-III-Arena/blob/master/code/game/bg_public.h)
- [ericw-tools map parser and Valve 220 handling](https://github.com/ericwa/ericw-tools/blob/8b70fce25558f2662ebee0855c12d35fe21e7beb/common/mapfile.cc)
- ericw-tools [outside fill and `.pts` leak-trail behavior](https://github.com/ericwa/ericw-tools/blob/8b70fce25558f2662ebee0855c12d35fe21e7beb/qbsp/outside.cc), [VIS entry point](https://github.com/ericwa/ericw-tools/blob/8b70fce25558f2662ebee0855c12d35fe21e7beb/vis/main.cc), and [light entry point](https://github.com/ericwa/ericw-tools/blob/8b70fce25558f2662ebee0855c12d35fe21e7beb/light/main.cc)
- [NetRadiant Custom q3map2 map parser](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/tools/quake3/q3map2/map.cpp) and [Radiant brush-token model](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/brushtokens.h)
- NetRadiant Custom q3map2 [`.lin` leak-trail writer](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/tools/quake3/q3map2/leakfile.cpp), Radiant [`.lin`/`.pts` pointfile reader behavior](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/radiant/points.cpp), and mbspc [EAAS reader/`aasinfo` metrics](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/tools/mbspc/mbspc/aas_file.c)

The core uses these references to establish behavioral constants, file envelopes,
diagnostic names and external-tool contracts; no GPL implementation was translated
or copied. Their source licences do not grant rights to commercial game data.
Quake-MapGen does not redistribute game data or treat installed commercial maps
as licensed training material.

## Optional private native movement harness

The [native movement harness](tools/native_movement/README.md) links unmodified,
externally located Quake II 3.19 [Pmove](https://github.com/id-Software/Quake-2/blob/master/qcommon/pmove.c),
[collision](https://github.com/id-Software/Quake-2/blob/master/qcommon/cmodel.c)
and shared utilities. The [official release readme](https://github.com/id-Software/Quake-2/blob/master/readme.txt)
identifies version 3.19; the engine files carry GPL-2.0-or-later notices. The
repository's original harness glue and orchestration remain MIT, a permissive
licence compatible with use in the combined GPL work. The combined executable
must be treated as GPL rather than as an MIT library artifact.

Native BSP checksumming also links the external
[RSA Data Security, Inc. MD4 Message-Digest Algorithm](https://github.com/id-Software/Quake-2/blob/master/qcommon/md4.c),
whose original attribution and notice-retention terms remain applicable. Its
source and notices stay unchanged in the read-only external checkout. The
[reference manifest](tools/native_movement/reference.json) pins source, header
and readme hashes; build provenance additionally binds the local glue, compiler,
licence evidence and executable.

This optional combined executable is a private local test dependency under
`.agents/tmp/native-movement/`. It has no install or packaging target, is not
linked into `qmg_core`, and is excluded from map ZIPs and project library
distribution. Only original map controls, provenance and numerical evidence are retained
under `out/native-movement/`; external source, objects and binaries are not
included there. This separate experiment does not change commercial asset or
reference-map rights.

## Local reference-map studies

The independently written Python study readers use the public [Quake II IBSP38 record contract](https://github.com/id-Software/Quake-2/blob/master/qcommon/qfiles.h), [entity load semantics](https://github.com/id-Software/Quake-2/blob/master/game/g_spawn.c), and NetRadiant Custom's [EAAS4/5 record contract](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/tools/mbspc/mbspc/aasfile.h) and [header encoding](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/tools/mbspc/mbspc/aas_file.c) as format references. No engine, compiler, botlib or third-party parser implementation is copied, translated or linked. Synthetic binary test fixtures are authored by this project.

The user's `q2maparchive-wip/maps` collection is an external, read-only local reference input. Maps, entities, source/decompiled geometry, textures, navigation sidecars, readmes and other assets are not incorporated into this repository's distribution. Local atlas artifacts retain each map's title, author attribution, relative evidence paths and content hashes. Their underlying rights are not changed by the project's MIT licence. The initial manifest grants no training, redistribution or raw-retrieval eligibility automatically; see [the study's provenance policy](docs/MAP_STUDY.md#rights-and-provenance).

The [courtyard/liquid study](docs/Q2_COURTYARD_LIQUID_STUDY.md) credits Manhunt to
Steve “Scary-one” Lyne and Violent & Funky (`gladdm1`) to Todd “Mr.CleaN” Rose.
Only general spatial relationships inform Lockyard; no reference geometry or
assets are copied into the original recipe. Exact local provenance and source
restrictions remain in the [study record](knowledge/q2/courtyard-liquid-study.json).

The [interleaved-route study](docs/Q2_INTERLEAVED_ROUTE_STUDY.md) credits
Reclamation (`q2rdm7`, Q2R Edition) to Corey “Kamarov” Peters. The tested BSP is
pinned separately from historical `kamq2dm3` reviews and the original readme;
byte or gameplay equivalence is not assumed. That readme prohibits using the
level as a base for additional levels and restricts distribution. The study
transfers only abstract design observations to original Ravel geometry, with
exact local evidence and rights limits in its
[record](knowledge/q2/interleaved-route-study.json). Its auxiliary q2rdm2 plan
retains German “JaLisK0” Garcia's attribution and the existing source restrictions.
No reference brushes, assets or third-party implementation enter the recipe.

The [native Talus ascent comparison](docs/Q2_NATIVE_ASCENT_STUDY.md) and
Ravel route probes (`out/ravel/native-routes/README.md`, upstream study evidence, not imported) use the same private,
externally linked Quake II 3.19 movement dependency described above.
Their orchestration, original map controls and numerical results add no engine
source or binary redistribution. The GPL and RSA MD4 notice boundaries remain
unchanged; the repository's MIT licence does not relicense the combined native
test executable or any commercial game data.

The [traversal study](docs/Q2_TRAVERSAL_STUDY.md) attributes Painkiller to Sten “ztn” Uusvali, Torn Glory to Dan “CardO” Shannon and Bio Rust to kold. Only general design observations transfer to the original Counterweight recipe. Native lift and teleport contracts were checked against the [official Quake II game source](https://github.com/id-Software/Quake-2/tree/master/game), with exact local file hashes recorded in [the mechanism notes](docs/Q2_MECHANISMS.md). The optional dedicated smoke harness invokes an independently installed [Q2REPRO](https://github.com/themuffinator/q2repro/tree/aeeb335f9a4de296c73d66b39e2a1efcfe7ade0d) executable and legacy module as separate processes; no engine code or binaries are distributed. Its [source audit and binary pins](tools/map_recipes/ENGINE_SMOKE.md) define the tested version and evidence limits.

The separate [native teleport host](tools/native_teleport/README.md) extends
that private optional boundary to the complete unmodified stock 3.19 game
module and [server entity-world queries](https://github.com/id-Software/Quake-2/blob/master/server/sv_world.c).
Its original MIT bridge links external GPL-2.0-or-later inputs into a private
GPL executable, with the RSA MD4 notice boundary unchanged. All external
sources remain in the read-only local checkout; no source, object, executable
or game asset enters the core library or Relay map ZIP. Exact source and
licence pins are in [its reference manifest](tools/native_teleport/reference.json).

The [arrival study](docs/Q2_TELEPORTER_ARRIVAL_STUDY.md) retains Painkiller's
and Torn Glory's author attribution and local-reference rights. Relay uses
original geometry; the study transfers only measured spatial relationships,
including failed departure candidates, without training or redistribution of
archive map data.

The [atrium route study](docs/Q2_ATRIUM_ROUTE_STUDY.md) attributes The Killing
Machine to Sten “ztn” Uusvali and the inspected Cunning Plan to Paul “PeeJ” James.
Only local reference measurements and derived analytical figures are recorded;
archive source/BSP assets are neither redistributed nor admitted to training.
Span's opposed-stair constructor, audits and map geometry are original project
work. Its native trials reuse the separately linked private stock Quake II
CM/Pmove harness described above; no external implementation, native executable,
compiler or stock asset is included in either Span map package.
