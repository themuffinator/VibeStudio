"""Compare fixed requests and structural variety without treating hashes as quality."""
from collections import Counter, defaultdict
import hashlib
from itertools import permutations, product
import json
from pathlib import Path

from tools.map_study.binary import Bsp
from tools.map_recipes.drop_audit import _world

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT/'out/mp-portfolio'


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def motif(room):
    return next((tag for tag in room.get('tags', []) if tag in
                 ('raised_crosswalk', 'horseshoe_gallery', 'opposed_stair_bridge', 'offset_barrier')), 'unspecified')


def graph_signature(layout, *, colored=False):
    """Canonical undirected room multigraph for bounded small-map comparisons.

    Passage IDs, room IDs, ordering, coordinates, naming and global rotations
    cannot create a new signature. Colored mode also preserves motif identity.
    This graph omits within-room routes and therefore is not a navigation score.
    """
    rooms = layout['rooms']
    if not 2 <= len(rooms) <= 8:
        raise ValueError('Canonical comparison is bounded to2–8 rooms')
    ids = [r['id'] for r in rooms]
    if len(set(ids)) != len(ids):
        raise ValueError('Repeated room ID')
    edges = Counter()
    degrees = Counter()
    for connection in layout['connections']:
        a,b = connection['from'], connection['to']
        if a == b or a not in ids or b not in ids:
            raise ValueError('Invalid room edge')
        edges[tuple(sorted((a,b)))] += 1
        degrees.update((a,b))
    groups = defaultdict(list)
    for room in rooms:
        groups[(degrees[room['id']],motif(room) if colored else '')].append(room['id'])
    labels = sorted(groups)
    orderings = [list(permutations(groups[label])) for label in labels]
    best = None
    for selected in product(*orderings):
        order = sum((list(group) for group in selected),[])
        encoded = tuple(edges[tuple(sorted((a,b)))] for index,a in enumerate(order) for b in order[index+1:])
        if best is None or encoded < best:
            best = encoded
    value = {'partitions':[(label,len(groups[label])) for label in labels], 'upper_triangle':best}
    return hashlib.sha256(json.dumps(value,separators=(',',':')).encode()).hexdigest()


def world_signature(bsp):
    return sorted((contents,tuple(sorted(tuple(plane) for plane in shape.planes)))
                  for _,contents,shape in _world(bsp))


def compare():
    rows = []
    for case in read(OUT/'cases.json'):
        ident = case['id']
        folder = OUT/'generated'/ident
        name = 'qmg_'+ident
        old = read(OUT/'baseline'/(ident+'-verification.json'))
        initial = read(OUT/'revisions/initial'/ident/'verification.json')
        current = read(folder/'verification.json')
        layout = read(folder/'layout.json')
        report = read(folder/(name+'.qmg.json'))
        prior_report = read(OUT/'revisions/initial'/ident/(name+'.qmg.json'))
        initial_bsp = Bsp((OUT/'revisions/initial'/ident/(name+'.bsp')).read_bytes())
        current_bsp = Bsp((folder/(name+'.bsp')).read_bytes())
        other_entities = lambda bsp: [e for e in bsp.entities if e.get('classname') != 'info_player_deathmatch']
        row = {'id':ident, 'prompt':case['prompt'], 'source_sha256':current['source_sha256'],
               'bsp_sha256':current['bsp_sha256'], 'candidate_seed':report['candidate_seed'],
               'rooms':len(layout['rooms']), 'passages':len(layout['connections']),
               'cycle_rank':len(layout['connections'])-len(layout['rooms'])+1,
               'room_graph_signature':graph_signature(layout),
               'motif_colored_room_graph_signature':graph_signature(layout,colored=True),
               'motifs':dict(Counter(motif(room) for room in layout['rooms'])), 'levels':layout['levels'],
               'old_compiled_xy_envelope_area':old['compiled_world_xy_envelope_area'],
               'new_compiled_xy_envelope_area':current['compiled_world_xy_envelope_area'],
               'envelope_reduction_fraction':1-current['compiled_world_xy_envelope_area']/old['compiled_world_xy_envelope_area'],
               'old_anchor_center_failures':len(old['anchor_failures']), 'new_anchor_center_failures':len(current['anchor_failures']),
               'ordinary_pairs':current['ordinary_gate_checks']['baseline_pairs'],
               'single_passage_closures_losing_pairs':sum(r['unreachable_pairs']>0 for r in current['ordinary_gate_checks']['passages']),
               'old_spawn_pair_los_fraction':old['metrics']['spawn_pair_world_los_fraction'],
               'initial_spawn_pair_los_fraction':initial['metrics']['spawn_pair_world_los_fraction'],
               'new_spawn_pair_los_fraction':current['metrics']['spawn_pair_world_los_fraction'],
               'final_score':current['quality_score'],
               'final_findings':current['analysis_findings'],
               'spawn_refinement_control':{
                   'same_candidate_seed':report['candidate_seed']==prior_report['candidate_seed'],
                   'compiled_solid_signatures_equal':world_signature(initial_bsp)==world_signature(current_bsp),
                   'nonspawn_entities_equal':other_entities(initial_bsp)==other_entities(current_bsp)}}
        rows.append(row)
    variety = [{'rooms':count,
                'distinct_unlabelled_graphs':len({r['room_graph_signature'] for r in rows if r['rooms']==count}),
                'distinct_motif_colored_graphs':len({r['motif_colored_room_graph_signature'] for r in rows if r['rooms']==count})}
               for count in sorted({r['rooms'] for r in rows})]
    record = {'schema_version':1, 'cases':rows, 'structural_variety':variety,
              'limits':['Nine fixed requests use the default six-candidate selector; no hand-selected candidate replacement.',
                        'Before/after generation changes geometry and placement policy together; smaller envelopes are descriptive, not proven better play.',
                        'A changed seed/fingerprint is not a new topology; room graph signatures ignore within-room circulation.',
                        'Colored signatures preserve motif identity, not exact usable routes or tactics.',
                        'The two additional verticality controls are separate from this matched nine-case comparison.',
                        'Spawn LOS is static and position-specific; human respawn selection and match safety remain untested.']}
    (OUT/'comparison.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'cases':len(rows),'structural_variety':variety,
                      'envelope_reduction_percent_range':[round(100*min(r['envelope_reduction_fraction'] for r in rows),1),
                                                         round(100*max(r['envelope_reduction_fraction'] for r in rows),1)]}))


if __name__ == '__main__':
    compare()
