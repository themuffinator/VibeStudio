# Route diversity study — 13 September 2026

The next useful step is to give routes different jobs, rather than add more connections around one dominant room. This study extends the [Switchback work](Q2_SWITCHBACK_STUDY.md) with four existing reference-partition maps: The Chastity Belt Duel, Reclamation, The Triple Towers and Paranoia. It develops design hypotheses and a repeatable review method; it does not establish that an original generated level plays well.

The [research record](../knowledge/q2/route-diversity-study.json) binds each observation to the exact local BSP, evidence files and analysis record with SHA-256 hashes. BSP upward surfaces and embedded item locations were inspected in fresh analytical floor diagrams, retained locally in `out/route-diversity-study/`. No geometry, textures or external code were incorporated. No engine match, input automation or model-weight training was performed.

## What the references add

| Map and author | Static measurements | Design question raised |
| --- | --- | --- |
| The Chastity Belt Duel (`q2rdm2`), German “JaLisK0” Garcia | Combat-height spread 318.8 units; three occupied 128-unit bands; LOS median/p90 189.4/724.6; 1/45 visible spawn-eye pairs | Can two separated lower armor destinations make players rotate laterally while Mega and the deep rocket pickup create different vertical commitments? |
| Reclamation (`q2rdm7`), Corey “Kamarov” Peters | Spread 224; two coarse bands; LOS 162.9/456.5; 0/6 visible spawn-eye pairs | Can a compact lower weapon circuit contest an upper armor circuit without making every fight happen at the same junction? |
| The Triple Towers (`mktriple`), Martin “Killer” Kilcoyne | Spread 205.6; four coarse bands; LOS 206.4/556.1; 9/45 visible spawn-eye pairs | When does repeated room vocabulary improve learning, and when does an overlooked lower junction become too easy to control? |
| Paranoia (`grdm1`), John “Yogi” Hughes | Spread 255.2; three coarse bands; LOS 189.9/558.2; 1/6 visible spawn-eye pairs | Can a sequence of unequal spaces and local height changes remain recognizable without exposing every exit from one central point? |

Height spread is p90–p10 of authored combat-anchor origins. Coarse bands are not floor counts: Triple Towers concentrates most upward surface area at z=0 and z=192 despite its four occupied anchor bins. LOS samples are world-brush point traces from upward-surface samples, including some inaccessible trim. They do not measure real engagement distances or spawn safety. The reference sample is deliberately varied; these values are not targets to maximize or minimize.

**The Chastity Belt Duel.** The inspected plan has uneven extensions, curved transitions and partial routes at different heights. Its two Combat Armors share origin z=-176 but are approximately 1,580 units apart in straight-line space. Mega is at z=48; the rocket launcher is deeper at z=-272. This separates lateral destination choice from the decision to gain height. It suggests assigning resources across multiple useful circuits instead of placing every valuable pickup along one upper lap. The actual route cost and exposure between these items remain unknown. The author's readme explicitly describes a duel map and prohibits using the level as a base for additional levels; this study extracts principles only.

**Reclamation.** Its upper and lower plans have different footprints. Rockets and rail are at z=-256; Combat Armor, super shotgun and hyperblaster are at z=-32; Jacket Armor and grenades occupy an intermediate z=-176. The smaller intermediate route has an equipment purpose, even though the coarse descriptor counts only two bands. Embedded entities include two teleporter links and two rotating entities, so a static floor diagram cannot establish the complete movement experience. The archive's `q2rdm7.txt` contains historical player reports for its recorded alias `kamq2dm3`. They praise weapon/armor contests and fast play, but disagree over whether the map is too small and whether health scarcity is enjoyable. These are attributed historical opinions, with unknown settings and no exact tested-BSP hash, not new validation of this archive revision.

**The Triple Towers.** The author describes three octagonal rooms, lower connecting corridors and upper access by three elevators and one stair route. The BSP contains three `func_plat` entities. The plan and author account agree on a lower chaingun junction below an upper grenade position. This is a concrete vertical interaction with a resource at each end, not merely a balcony added for appearance. Its repeated room vocabulary also provides a counterexample to treating all repetition as poor design. The new generator should vary room proportions, approach positions and tactical roles; it need not prohibit shared architectural motifs. Mover timing and the lower player's escape options require engine testing.

**Paranoia.** The plan progresses through unequal spaces and changes in width and elevation; rockets are below the main sequence, rail and chaingun occupy another height, and Combat Armor is higher. The original pack readme declares a shared theme with custom textures and sounds. This establishes author intent and the existence of themed assets, not that players perceive clear landmarks. Six embedded teleporter links further limit conclusions from an apparently disconnected height slice. A useful original design can give each junction a distinctive silhouette and sound/material cue while retaining a common industrial theme. Rendering and first-time navigation must confirm that those cues work.

## Reusable design procedure

1. **Draw destination circuits before room decorations.** Name two resource contests and one recovery destination. For each pair, sketch a direct route and an alternative with a different approach or exposure. Check the physical approach shared by the routes; two graph edges through one choke are not two independent choices.
2. **Give an added floor a written purpose.** It must add a useful destination, a different approach, an overlook with counterplay, or a return connection. Mark both ascent and descent. Remove floors that only repeat the entire plan above itself.
3. **Separate control benefits.** A player on high ground should have a reason to leave it or expose themselves for some weapon, armor or recovery resource. Do not assume that placing a reward low automatically balances it: measure pursuit, interception and escape.
4. **Use spatial identity at decisions.** Vary opening direction, ceiling profile, width and the view of a local landmark before adding texture variation. Test whether the first-time player can identify the destination at the junction and recognize the return journey. Theme labels and texture entropy cannot answer that question.
5. **Keep contrasting encounter spaces.** Retain one space that supports range and lateral movement alongside tighter routes and brief recovery areas. Compactness should shorten uninteresting travel while preserving differences in encounter range and enough room for the intended player load.

These are provisional additions to the existing rule cards, recorded separately so that four observations do not silently change global reference bands or promotion status. A strong alternative route needs a distinct tactical use; a larger cycle count alone is insufficient.

## Controlled checks for the next original layout

| Hypothesis | Change only | Evidence to collect |
| --- | --- | --- |
| An outer route reduces forced central crossings | Close one bypass, preserving items and all remaining geometry | Route use between the two main contests, forced backtracks, interceptions and deaths at the shared junction |
| A local third level creates useful choices | Close its return doorway while retaining the platform and pickup | Return journeys, time spent there, control reversals, retreat failures; note that the change intentionally creates more commitment |
| Lower weapons contest upper resource control | Move one strong weapon between lower and upper validated sockets | Recovery time, upper hold duration, weapon variety and whether one player cycles both rewards uncontested |
| Landmark cues improve learning | Change only one junction's silhouette accent or material/light cue | First-time wrong turns, destination recognition and ability to retrace the route without a map |
| Compact flow still supports capacity | Keep the map fixed; test separate low/high player-load sessions | Encounter intervals, congestion, empty travel and deaths soon after respawn; do not pool duel and FFA results |

Compile/leak, player-hull clearance, directed reachability and pickup contact remain hard gates. Then walk the routes in a windowed engine and use the engine screenshot command for render evidence. Record game module, movement settings, item timers, tested artifact hash and player familiarity. Use matched starts or swap players/sides, report all failures, and do not promote a rule from a single match. The current core now includes same-node XY distance and cross-node endpoint approaches. Its Manhattan/node-center cost remains a heuristic, not elapsed engine traversal time; historical artifacts can still contain the earlier timing estimates.

## Evidence issues found

Two capacity labels conflict: `grdm1.ref` says 2–5 players while its author's pack readme says 1–3; `q2rdm2.ref` says 2–4 while the author specifies duel. Preserve both sources and resolve revision, mode and intended load before using either as a capacity label for training. Existing profiles were not rewritten. The Reclamation historical reports also require exact-version and settings verification before becoming benchmark labels.

All four AAS sidecars still have zero reachability records and unverified BSP association. Their presence is not proof that the maps are connected or disconnected. No additional unrelated code defect was found in this research pass.
