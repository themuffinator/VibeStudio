"""Bounded authored-spawn observations, kept separate from native spawn safety.

Reads six preselected reference BSPs and the sixteen frozen Lattice selections.
Never changes layouts, launches a game, or alters the archive/model profile.
"""
from __future__ import annotations

import argparse
import hashlib
from itertools import combinations
import json
import math
from pathlib import Path

from tools.map_study.binary import Bsp
from tools.map_study.features import anchors, describe
from tools.map_recipes.drop_audit import _flat_support, _world

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'out/spawn-separation-study'
ARCHIVE = Path('E:/Development/q2maparchive-wip/maps')
REFERENCES = ('ztn2dm3', 'ptrip', 'aeroq2', 'mktriple', 'q2rdm2', 'koldduel1')
THRESHOLD = 256
EYE_OFFSET = 22
POINT_MASK = 1 | 2 | 8 | 16


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def spawn_metrics(spawns, trace):
    """Each unordered pair is counted once; invalid endpoints remain unknown."""
    ids = [s['entity'] for s in spawns]
    require(len(ids) == len(set(ids)), 'Duplicate spawn entity')
    require(2 <= len(spawns) <= 128, 'Need two to128 bounded spawn entities')
    for spawn in spawns:
        point = spawn['origin']
        require(len(point) == 3 and all(isinstance(v, (int, float)) and not isinstance(v, bool)
                                       and math.isfinite(v) for v in point), 'Invalid authored origin')
    eyes = {s['entity']: [*s['origin'][:2], s['origin'][2] + EYE_OFFSET] for s in spawns}
    invalid = [ident for ident, eye in eyes.items() if trace(eye, eye) < 1]
    pairs = []
    for a, b in combinations(spawns, 2):
        delta = [a['origin'][i] - b['origin'][i] for i in range(3)]
        distance2 = sum(v*v for v in delta)
        visible = None if a['entity'] in invalid or b['entity'] in invalid else trace(eyes[a['entity']], eyes[b['entity']]) >= 1
        pairs.append({'entities': [a['entity'], b['entity']], 'distance_squared': distance2,
                      'distance': math.sqrt(distance2), 'height_difference': abs(delta[2]),
                      'below_existing_256': distance2 < THRESHOLD**2, 'world_eye_visible': visible})
    nearest = [min(p['distance'] for p in pairs if ident in p['entities']) for ident in ids]
    tested = sum(p['world_eye_visible'] is not None for p in pairs)
    visible = sum(p['world_eye_visible'] is True for p in pairs)
    return {'spawn_count': len(spawns), 'unordered_pair_count': len(pairs),
            'authored_spawns': spawns, 'pairs': pairs, 'nearest_distance': describe(nearest),
            'close_unordered_pairs': sum(p['below_existing_256'] for p in pairs),
            'equivalent_directed_distance_warnings': 2 * sum(p['below_existing_256'] for p in pairs),
            'world_eye_tested_pairs': tested, 'world_eye_visible_pairs': visible,
            'world_eye_visible_fraction': visible / tested if tested else None,
            'invalid_eye_entities': invalid,
            'authored_height_span': max(s['origin'][2] for s in spawns) - min(s['origin'][2] for s in spawns)}


def bsp_observation(path):
    bsp = Bsp(path.read_bytes())
    require(not bsp.file_issues and not bsp.entity_issues, 'Malformed BSP input')
    points, excluded = anchors(bsp.entities)
    spawns = [p for p in points if p['role'] == 'spawn']
    data = spawn_metrics(spawns, lambda a, b: bsp.trace(a, b, POINT_MASK))
    data['excluded_not_deathmatch'] = excluded
    return bsp, data


def references():
    rows = []
    for name in REFERENCES:
        records = list((ROOT / 'out/map-study/records').glob(name + '-*.json'))
        require(len(records) == 1, 'Ambiguous reference record')
        record_path = records[0]
        previous = read(record_path)
        require(previous['partition'] == 'reference' and previous['entity_policy'] == 'embedded_stock_dm',
                'Reference partition/entity policy changed')
        path = ARCHIVE / previous['bsp_path']
        require(sha(path) == previous['bsp_sha256'], 'Reference BSP changed')
        _, observation = bsp_observation(path)
        rows.append({'map': name, 'title': previous['title'], 'authors': previous['authors'],
                     'split_group': previous['split_group'], 'partition': 'reference',
                     'bsp_path': str(path), 'bsp_sha256': sha(path),
                     'inventory_path': record_path.relative_to(ROOT).as_posix(),
                     'inventory_sha256': sha(record_path),
                     'metadata_player_range': [previous['players_min'], previous['players_max']],
                     'metadata_issues': previous['issues'], 'observation': observation,
                     'full_player_support_tested': False, 'native_spawn_settlement_tested': False})
    return rows


def baseline():
    port = ROOT / 'out/lattice-portfolio'
    comparison = read(port / 'comparison.json')
    rows = []
    for case in comparison['cases']:
        folder = ROOT / 'out/lattice' if case['group'] == 'example' else port / 'generated' / case['id']
        layout = read(folder / 'layout.json')
        report = read(folder / (layout['map_name'] + '.qmg.json'))
        path = folder / (layout['map_name'] + '.bsp')
        require(sha(path) == case['after']['bsp_sha256'], 'Baseline compiled BSP changed')
        require(layout['layout_sha256'] == case['after']['layout_sha256'], 'Baseline layout changed')
        bsp, observation = bsp_observation(path)
        source = [p for p in layout['placements'] if p['role'] == 1]
        require(sorted(tuple(p['origin']) for p in source) ==
                sorted(tuple(p['origin']) for p in observation['authored_spawns']), 'Typed/native spawn origins differ')
        world = _world(bsp)
        support = []
        for spawn in source:
            feet = [*spawn['origin'][:2], spawn['origin'][2] - 24]
            brushes = _flat_support(world, feet, 'frozen spawn station')
            rooms = [room for room in layout['rooms'] if all(
                room['bounds'][0][i] <= spawn['origin'][i] <= room['bounds'][1][i] for i in range(3))]
            require(len(rooms) == 1, 'Spawn room assignment is ambiguous')
            support.append({'placement': spawn['id'], 'origin': spawn['origin'], 'room': rooms[0]['id'],
                            'feet': feet, 'full_support_brushes': brushes})
        findings = report['analysis']['visibility']['findings']
        require(findings == case['after']['findings']['visibility'], 'Source warnings differ')
        rows.append({'id': case['id'], 'group': case['group'], 'layout_sha256': layout['layout_sha256'],
                     'layout_file': (folder / 'layout.json').relative_to(ROOT).as_posix(),
                     'layout_file_sha256': sha(folder / 'layout.json'),
                     'bsp_path': path.relative_to(ROOT).as_posix(), 'bsp_sha256': sha(path),
                     'report_path': (folder / (layout['map_name'] + '.qmg.json')).relative_to(ROOT).as_posix(),
                     'report_sha256': sha(folder / (layout['map_name'] + '.qmg.json')),
                     'source_visibility_findings': findings, 'observation': observation,
                     'compiled_station_support': support, 'native_spawn_settlement_tested': False})
    return rows


def study():
    return {'schema_version': 1, 'kind': 'bounded_spawn_geometry_observations',
            'reference_selection': list(REFERENCES),
            'selection_reason': 'Six preselected reference-partition curriculum maps from six recorded split groups; no holdout/test maps.',
            'distance_basis': 'Euclidean authored-origin distance; strict less-than256 existing generator warning, not a learned safe-distance law.',
            'eye_basis': {'offset_above_authored_origin': EYE_OFFSET, 'world_contents_mask': POINT_MASK,
                          'invalid_endpoints_excluded_and_retained': True},
            'references': references(), 'frozen_lattice_baseline': baseline(),
            'limits': ['Authored positions precede native spawn adjustment; archived spawn standing hulls were not certified.',
                       'Reference point rays use model0 only; windows/lava/slime block, water and entity movers are excluded.',
                       'Occluded pair counts and Euclidean proximity do not establish immediate deaths, recovery, weapon access or spawn safety.',
                       'Archive player recommendations are metadata, not measured capacity; authored spawn count is a separate variable.',
                       'Generated compiled support checks certify local standing poses, not complete native routes or spawn mechanics.',
                       'No geometry or entities copied into generation, no count/threshold changes, no learned weights or profile changes.']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    # BSP parsing uses tuples; compare the same JSON-domain representation that
    # is retained on disk instead of confusing tuple/list shape with a change.
    result = json.loads(json.dumps(study(), allow_nan=False))
    path = OUT / 'observations.json'
    if args.verify:
        require(result == read(path), 'Spawn study rederivation differs')
    else:
        require(not path.exists(), 'Refusing to overwrite retained observations')
        OUT.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(result, indent=2, ensure_ascii=False, allow_nan=False) + '\n', encoding='utf-8')
    print(json.dumps({'ok': True, 'reference_maps': len(result['references']),
                      'baseline_cases': len(result['frozen_lattice_baseline']), 'observations_sha256': sha(path)}))


if __name__ == '__main__':
    main()
