# Quake Live local orthographic atlas

Open `index.html` to compare maps and height bands. The `orthographics/` folders
contain 64 ordinary PNG diagrams: one fitted overview, one common-scale overview
and each nonempty height band per map. Keep this archive local for personal
research; it contains derived diagrams, not raw BSPs or game textures.

`python -B -m tools.ql_atlas --root E:/_SOURCE/_ASSETS/QL-preSteam --output out/ql-atlas`

This independent reader projects actual IBSP46/47 model-zero render surfaces.
It reuses the project's bounded lump and entity reader, and uses installed
Shapely for XY unions. It does not extract textures, decompile maps, copy BSPs,
generate navigation or execute map data. The atlas remains local personal
research; no geometry is incorporated into generated levels, no training corpus
or weights are produced, and redistribution rights are not inferred.

The fragment is wrapped with the available visualization preview script. Run
`node tools/ql_atlas/preview_check.cjs out/ql-atlas/index.html out/ql-atlas`
with an installed Playwright exposed through normal module lookup or `NODE_PATH`.
Set TEMP/TMP to the project's new task scratch directory before launching the
headless analytical browser. QA changes only the diagram's own DOM selections;
it never injects keyboard/mouse input or launches a game. Its PNGs are diagrams,
not OS screenshots or engine render-target evidence.
`python -B -m tools.ql_atlas.package --output out/ql-atlas` then verifies current
QA input/image hashes and packages the diagrams plus their provenance. Add
`--format-reference-root E:/_SOURCE/_CODE` to atlas generation to pin the local
format-reference files; no code from those sources is copied.

The output manifest records BSP identities, authored entity counts and mode
filters, surface/patch methods and limits. `orthographics/<map>/` contains an
all-height fitted view, an all-height common-scale view and each 128-unit height
slab. Slabs are coordinate intervals, not certified floors. Common-scale images
share one world span, orientation and pixel scale. Overlapping projections,
inaccessible trim, exterior tops and static visual surfaces are retained; their
area is not a walkable-density or room-volume metric.

Liquid labels come from compiled contents bits. Entity origins and direct target
relations are authored values across all modes, not settled runtime state or
movement trajectories. Model-zero excludes entity submodel surfaces and movers.
Marker display bands use the highest upward rendered triangle under the same XY
within 64 units below the authored origin; unresolved points use their origin
band. This keeps a pickup 15 units above a pedestal in that pedestal's view,
without moving its XYZ. The association is point-based visual context, not a
player-support, headroom, collision or native item-drop certificate.
Curved patches use bounded 6×6 quadratic samples per block, independently of the
engine's adaptive tessellation. Texture/lightmap UV bytes are skipped entirely;
the Campgrounds snapshot contains invalid values in unused lightmap UV fields.
XYZ and normals are validated. Full BSP hashes preserve those exact source bytes.

Format facts were inspected in local primary sources:

- [NetRadiant Custom IBSP reader](https://github.com/Garux/netradiant-custom/blob/master/tools/quake3/q3map2/bspfile_ibsp.cpp), including the 18th advertisement lump in QL's IBSP47.
- [Quake3e BSP structure definitions](https://github.com/ec-/Quake3e/blob/master/code/qcommon/qfiles.h) and [compiled surface/content flags](https://github.com/ec-/Quake3e/blob/master/code/qcommon/surfaceflags.h).

Those sources are GPL-licensed; only format facts were used. No external source
implementation was incorporated into this repository's MIT code. The Bernstein
polynomial evaluation and clipping/annotation logic are independently authored.
