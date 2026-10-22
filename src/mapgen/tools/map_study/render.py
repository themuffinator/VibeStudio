"""Self-contained, local-only analysis atlas. No remote assets or engine screenshots."""

from __future__ import annotations

import html
import math


STYLE = """
:root{color-scheme:dark;font:15px/1.6 system-ui,sans-serif;background:#0c131b;color:#dce6ec}
body{max-width:1320px;margin:auto;padding:36px}a{color:#70d9c7;text-decoration:none}a:hover{text-decoration:underline}
h1{font-size:36px;letter-spacing:-1px;line-height:1.15;margin:8px 0 18px}h2{font-size:21px;margin-top:32px}
p{max-width:1000px;color:#afc0cd}.eyebrow{color:#70d9c7;letter-spacing:2px;font-size:12px;text-transform:uppercase}
.cards{display:flex;gap:14px;flex-wrap:wrap;margin:26px 0}.card{background:#16222f;padding:16px 22px;border-radius:8px;min-width:140px}
.card strong{display:block;font-size:27px;color:#f5f8fa}.card span{color:#afc0cd;font-size:13px}
table{width:100%;border-collapse:collapse;font-size:14px}th{text-align:left;color:#afc0cd;font-weight:500}
td,th{padding:11px 12px;border-bottom:1px solid #293745}tr:hover{background:#14202b}
input,select,button{background:#16222f;color:#e2ecf4;border:1px solid #3c5064;border-radius:5px;padding:9px 12px;font:inherit;margin-right:8px}
input[type=search]{width:340px}code{background:#1b2937;padding:2px 5px;border-radius:3px;font-size:12px}
.muted{color:#94a9ba}.notice{border-left:3px solid #e1b15a;padding:8px 18px;background:#1c242c}
.map{width:100%;height:690px;background:#091018;border:1px solid #2a3948;border-radius:8px;margin:20px 0}
svg text{paint-order:stroke;stroke:#091018;stroke-width:3px;stroke-linejoin:round;fill:white;font:11px system-ui}
.tag{font-size:12px;padding:3px 7px;background:#25364a;border-radius:4px;white-space:nowrap}
details{background:#14202b;margin:14px 0;padding:14px}summary{cursor:pointer}li{margin:6px 0}
@media(max-width:800px){body{padding:18px}.map{height:480px}table{font-size:12px}td,th{padding:8px 5px}}
"""

COLORS = {"spawn": "#ffffff", "weapon": "#f4b452", "major_item": "#ef6482", "armor": "#62d697",
          "health": "#67b9ef", "ammo": "#b8a0e8", "objective": "#e870d5"}


def esc(value):
    return html.escape(str(value), quote=True)


def number(value):
    return "unknown" if value is None else f"{value:,.2f}".rstrip("0").rstrip(".")


def page(title, body, script=""):
    return (f'<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width">'
            f'<title>{esc(title)}</title><style>{STYLE}</style><body>{body}'
            f'<script>{script}</script></body></html>')


def dossier(record, floors):
    analysis = record["analysis"]
    minimum, maximum = analysis["world_bounds"]
    width, height = max(maximum[0] - minimum[0], 1), max(maximum[1] - minimum[1], 1)
    scale = min(1120 / width, 600 / height)
    def xy(point):
        return 40 + (point[0] - minimum[0]) * scale, 40 + (maximum[1] - point[1]) * scale
    zmin = min((f["center"][2] for f in floors), default=0)
    zmax = max((f["center"][2] for f in floors), default=1)
    polygons = []
    bands = set()
    for floor in sorted(floors, key=lambda f: f["center"][2]):
        points = " ".join(f"{xy(p)[0]:.1f},{xy(p)[1]:.1f}" for p in floor["points"])
        ratio = (floor["center"][2] - zmin) / max(zmax - zmin, 1)
        band = math.floor((floor["center"][2] - zmin) / 128)
        bands.add(band)
        polygons.append(f'<polygon data-band="{band}" points="{points}" fill="hsl({220 - ratio * 70:.0f} 35% {22 + ratio * 25:.0f}%)" '
                        f'stroke="#64859a" stroke-width=".4"><title>Face {floor["id"]}; z {floor["center"][2]:.1f}; {esc(floor["texture"])}</title></polygon>')
    markers = []
    for anchor in analysis["anchors"]:
        x, y = xy(anchor["origin"])
        label = f'{anchor["entity"]}: {anchor["class"]} @ {anchor["origin"]}'
        markers.append(f'<g class="anchor" data-role="{anchor["role"]}"><circle cx="{x:.1f}" cy="{y:.1f}" r="4" '
                       f'fill="{COLORS[anchor["role"]]}" stroke="#0c131b"><title>{esc(label)}</title></circle>'
                       f'<text class="label" x="{x + 6:.1f}" y="{y - 5:.1f}">{anchor["entity"]}</text></g>')
    metrics = analysis["metrics"]
    body = (f'<a href="../index.html">← Reference atlas</a><p class="eyebrow">Quake II / {esc(record["partition"])} / static study</p>'
            f'<h1>{esc(record["title"])}</h1><p>{esc(", ".join(record["authors"]) or "Author unresolved")} · '
            f'<code>{esc(record["map"])}</code> · {esc(" / ".join(record["themes"]) or "Theme unlabelled")}</p>'
            '<p class="notice">Analytical plan view of upward world surfaces. Upper surfaces can hide lower routes. '
            'Includes inaccessible trim; this view does not establish walkability, balance, or visual quality.</p>')
    body += '<div class="cards">' + "".join(f'<div class="card"><strong>{number(metrics[key])}</strong><span>{label}</span></div>'
                                           for key, label in (("spawn_count", "DM spawns"), ("weapon_types", "weapon types"),
                                                              ("combat_height_p90_p10", "combat height p90−p10 (units)"),
                                                              ("sampled_world_sightline_p90", "sampled LOS p90 (units)"))) + '</div>'
    options = ''.join(f'<option value="{b}">Surface z {zmin + b * 128:.0f}–{zmin + (b + 1) * 128:.0f}</option>' for b in sorted(bands))
    body += f'<select id="band"><option value="all">All height bands</option>{options}</select>'
    body += '<label><input id="labels" type="checkbox" checked>Entity IDs</label><p>'
    body += ' · '.join(f'<span style="color:{color}">{name.replace("_", " ")}</span>' for name, color in COLORS.items()) + '</p>'
    body += f'<svg class="map" viewBox="0 0 {width * scale + 80:.1f} {height * scale + 80:.1f}" role="img" aria-label="Analytical plan view">' + ''.join(polygons + markers) + '</svg>'
    body += '<h2>Measured descriptors</h2><table><tr><th>Metric</th><th>Value</th></tr>'
    body += ''.join(f'<tr><td>{esc(key)}</td><td>{number(value)}</td></tr>' for key, value in metrics.items()) + '</table>'
    body += '<h2>Navigation evidence</h2><p>'
    nav = analysis.get("navigation", {})
    if nav.get("status") == "unverified_sidecar":
        body += f'{nav["areas"]} AAS areas; {nav["reachabilities"]} reachability links; {nav["bound_anchors"]}/{nav["anchors"]} anchors bind. '
        body += 'BSP association and compiler settings are unverified. These observations are excluded from descriptive reference bands.'
    else:
        body += esc(nav.get("error", "Navigation has not been evaluated."))
    body += '</p><h2>Evidence and limits</h2><p>BSP SHA-256: <code>' + record["bsp_sha256"] + '</code></p>'
    body += '<p>Runtime load name: <code>map ' + esc(record["map"]) + '</code>. Required assets/mod settings need verification before a windowed engine session.</p>'
    body += '<ul>' + ''.join(f'<li>{esc(s)}</li>' for s in analysis["limitations"] + record["issues"]) + '</ul>'
    body += '<p>Local reference only. Training and redistribution permissions have not been established. '
    body += f'<a href="../records/{esc(record["artifact_id"])}.json">Complete evidence record</a></p>'
    script = """
document.querySelector('#band').addEventListener('change',event=>{
 document.querySelectorAll('polygon[data-band]').forEach(p=>p.style.display=event.target.value==='all'||p.dataset.band===event.target.value?'':'none');
});
document.querySelector('#labels').addEventListener('change',event=>{
 document.querySelectorAll('.label').forEach(p=>p.style.display=event.target.checked?'':'none');
});
"""
    return page(record["title"], body, script)


def atlas(records, summary, curriculum):
    body = '<p class="eyebrow">Quake-MapGen / reference learning lab / v1</p><h1>Study the map. Test the rule.</h1>'
    body += '<p>A local evidence atlas of the curated archive. Compare spaces, elevation, sightlines and item economies across authors. '
    body += 'Descriptors support hypotheses; playtest observations determine whether a design decision works.</p>'
    body += '<div class="cards">' + ''.join(f'<div class="card"><strong>{value}</strong><span>{label}</span></div>' for label, value in
                                          (("BSPs analyzed", len(records)), ("study maps", summary["curriculum_maps"]),
                                           ("author/family groups", summary["split_groups"]), ("training eligible", 0))) + '</div>'
    body += '<p class="notice">All map-derived artifacts stay local. AAS files are unverified sidecars; missing links are unknown navigation evidence. '
    body += 'Reference bands use reference partitions only and weight each author/family group equally. No model weights have been trained.</p>'
    body += '<p><a href="summary.json">Corpus audit</a> · <a href="profile.json">Reference profile</a> · <a href="study-plan.template.json">Study session template</a></p>'
    body += '<h2>Curriculum</h2>'
    lookup = {r["map"]: r for r in records}
    for lesson in curriculum.get("lessons", []):
        links = []
        for name in lesson["maps"]:
            row = lookup[name]
            links.append(f'<a href="maps/{row["artifact_id"]}.html">{esc(row["title"])} ({esc(name)})</a>')
        body += f'<details><summary>{esc(lesson["title"])} <span class="tag">{esc(lesson.get("partition", "reference"))}</span></summary>'
        body += '<p>' + ' · '.join(links) + '</p><p>' + esc(lesson["question"]) + '</p></details>'
    body += '<h2>Full inventory</h2><input type="search" id="query" placeholder="Filter by map, author, theme, partition…" aria-label="Filter inventory">'
    body += '<p class="muted">Detailed plan views are prepared for curriculum maps. Every map has a machine-readable evidence record.</p>'
    body += '<table><thead><tr><th>Map / author</th><th>Partition</th><th>Theme</th><th>Spawns</th><th>Height span</th><th>LOS p90</th></tr></thead><tbody>'
    selected = {name for lesson in curriculum.get("lessons", []) for name in lesson["maps"]}
    for row in records:
        link = f'maps/{row["artifact_id"]}.html' if row["map"] in selected else f'records/{row["artifact_id"]}.json'
        metrics = row["analysis"]["metrics"]
        body += f'<tr><td><a href="{link}">{esc(row["title"])}</a> <code>{esc(row["map"])}</code><br><span class="muted">{esc(", ".join(row["authors"]))}</span></td>'
        body += f'<td>{esc(row["partition"])}</td><td>{esc(" ".join(row["themes"]))}</td>'
        body += ''.join(f'<td>{number(metrics[key])}</td>' for key in ("spawn_count", "combat_height_p90_p10", "sampled_world_sightline_p90")) + '</tr>'
    body += '</tbody></table>'
    script = "document.querySelector('#query').addEventListener('input',event=>{const q=event.target.value.toLowerCase();document.querySelectorAll('tbody tr').forEach(row=>row.hidden=!row.textContent.toLowerCase().includes(q));});"
    return page("Quake-MapGen reference atlas", body, script)
