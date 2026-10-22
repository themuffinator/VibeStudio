"""Compare room topology and spatial seams without assigning gameplay scores."""
import argparse
import hashlib
from collections import Counter, deque
from itertools import combinations
import json
from pathlib import Path

from tools.mp_portfolio.compare import graph_signature


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def full_span_room_gaps(layout):
    """Empty strips between room volumes only; passages/walls are excluded.

    Their absence distinguishes the non-slicing arrangement geometrically. It
    does not establish navigation, net void area or an absence of local seams.
    """
    result = {}
    for axis, name in enumerate(['x', 'y']):
        spans = sorted((r['bounds'][0][axis], r['bounds'][1][axis]) for r in layout['rooms'])
        if any(low >= high for low, high in spans):
            raise ValueError('Invalid room bounds')
        gaps, end = [], spans[0][1]
        for low, high in spans[1:]:
            if low > end:
                gaps.append([end, low])
            end = max(end, high)
        result[name] = gaps
    return result


def room_graph(layout):
    ids = [room['id'] for room in layout['rooms']]
    if len(ids) != len(set(ids)) or not 2 <= len(ids) <= 8:
        raise ValueError('Need distinct2–8 room IDs')
    neighbors = {ident: set() for ident in ids}
    degree = Counter()
    for edge in layout['connections']:
        a, b = edge['from'], edge['to']
        if a == b or a not in neighbors or b not in neighbors:
            raise ValueError('Invalid room edge')
        neighbors[a].add(b)
        neighbors[b].add(a)
        degree.update((a, b))

    def distances(start, removed=None):
        found, queue = {start: 0}, deque([start])
        while queue:
            node = queue.popleft()
            for target in sorted(neighbors[node]):
                if target != removed and target not in found:
                    found[target] = found[node] + 1
                    queue.append(target)
        return found

    distance = {ident: distances(ident) for ident in ids}
    if any(len(row) != len(ids) for row in distance.values()):
        raise ValueError('Disconnected room graph')
    hops = [distance[a][b] for a, b in combinations(ids, 2)]
    centrality = Counter({ident: 0.0 for ident in ids})
    for start, goal in combinations(ids, 2):
        paths = []

        def visit(node, path):
            if node == goal:
                paths.append(path)
                return
            for target in sorted(neighbors[node]):
                if (distance[start][target] == distance[start][node] + 1 and
                        distance[start][target] + distance[target][goal] == distance[start][goal]):
                    visit(target, path + [target])

        visit(start, [start])
        for path in paths:
            for node in path[1:-1]:
                centrality[node] += 1/len(paths)
    denominator = (len(ids)-1)*(len(ids)-2)/2
    centrality = {node: value/denominator if denominator else 0 for node, value in centrality.items()}
    closures = {}
    for removed in ids:
        remaining = [ident for ident in ids if ident != removed]
        closures[removed] = sum(goal not in distances(start, removed)
                                for start, goal in combinations(remaining, 2))
    return {'canonical_multigraph': graph_signature(layout), 'room_count': len(ids),
            'passages': len(layout['connections']), 'simple_edges': sum(map(len, neighbors.values()))//2,
            'cycle_rank': len(layout['connections'])-len(ids)+1,
            'passage_degree': dict(degree), 'maximum_room_hops': max(hops),
            'mean_room_hops': sum(hops)/len(hops),
            'shortest_room_path_betweenness': centrality,
            'maximum_shortest_room_path_betweenness': max(centrality.values()),
            'room_removal_unreachable_remaining_pairs': closures,
            'centrality_scope': 'Unweighted simple-room shortest paths, splitting credit across tied paths; parallel doorways count once here. Not movement time, exposure or tactical control.'}


def describe(folder):
    layout, verification = read(folder/'layout.json'), read(folder/'verification.json')
    if not verification['ok'] or verification['layout_sha256'] != layout['layout_sha256']:
        raise ValueError('Invalid or stale layout verification')
    for suffix, key in [('.map', 'source_sha256'), ('.bsp', 'bsp_sha256')]:
        if hashlib.sha256((folder/(layout['map_name']+suffix)).read_bytes()).hexdigest() != verification[key]:
            raise ValueError('Compiled input bytes differ from verification')
    return {'family': 'pinwheel' if any('compact_family_pinwheel' in r.get('tags', []) for r in layout['rooms']) else 'rows',
            'layout_sha256': layout['layout_sha256'],
            'candidate_seed': layout['candidate_seed'], 'source_sha256': verification['source_sha256'],
            'bsp_sha256': verification['bsp_sha256'], 'room_graph': room_graph(layout),
            'full_span_room_gaps': full_span_room_gaps(layout),
            'compiled_xy_envelope_area': verification['compiled_world_xy_envelope_area'],
            'authored_room_passage_packing': verification['authored_room_passage_packing'],
            'ordinary_pairs': verification['ordinary_gate_checks']['baseline_pairs'],
            'ordinary_missing_pairs': verification['ordinary_gate_checks']['baseline_missing_pairs'],
            'passage_closures_losing_pairs': sum(row['unreachable_pairs'] > 0 for row in verification['ordinary_gate_checks']['passages']),
            'spawn_pair_world_los_fraction': verification['metrics']['spawn_pair_world_los_fraction'],
            'findings': verification['analysis_findings'], 'levels': layout['levels']}


def validate_trace(trace, current):
    chosen = [row for row in trace['candidates'] if row['selected']]
    if (len(chosen) != 1 or chosen[0]['seed'] != current['candidate_seed'] or
            chosen[0]['family'] != current['family'] or
            trace['pipeline_selected_seed'] != current['candidate_seed'] or
            trace['pipeline_layout_sha256'] != current['layout_sha256'] or
            trace['candidate_count'] != len(trace['candidates']) or trace['candidate_count'] != 6):
        raise ValueError('Candidate trace differs from final selected export')


def compare(output):
    rows = []
    for case in read(output/'cases.json'):
        ident = case['id']
        old, new = describe(output/'baseline'/ident), describe(output/'generated'/ident)
        trace = read(output/'generated'/ident/'candidate-trace.json')
        validate_trace(trace, new)
        rows.append({'id': ident, 'before': old, 'after': new,
                     'topology_changed': old['room_graph']['canonical_multigraph'] != new['room_graph']['canonical_multigraph'],
                     'envelope_change_fraction': new['compiled_xy_envelope_area']/old['compiled_xy_envelope_area']-1,
                     'candidate_families': dict(Counter(c['family'] for c in trace['candidates'])),
                     'candidate_graphs': len({graph_signature(c) for c in trace['candidates']})})
    variety = []
    for count in sorted({row['before']['room_graph']['room_count'] for row in rows}):
        subset = [row for row in rows if row['before']['room_graph']['room_count'] == count]
        variety.append({'rooms': count, **{key+'_distinct_graphs': len({row[key]['room_graph']['canonical_multigraph'] for row in subset})
                       for key in ['before', 'after']}})
    hero = read(output/'hero.json')['id']
    hero_after = describe(output.parent/'splice')
    validate_trace(read(output.parent/'splice/candidate-trace.json'), hero_after)
    record = {'schema_version': 1, 'primary_cases': rows, 'selected_graph_variety': variety,
              'separate_predeclared_hero': {'id': hero, 'before': describe(output/'baseline'/hero),
                                           'after': hero_after},
              'limits': ['Matched requests change geometry and placements together; this is not a causal quality trial.',
                         'Candidates are observed before repair; final exports remain ordinary default selections.',
                         'Room graphs omit internal platforms, turns and chokepoints. Shortest room hops are not movement times.',
                         'Closed-room metrics exclude endpoints in the removed room and operate only on room adjacency.',
                         'Neither more cycles nor higher or lower centrality is a universal quality target.',
                         'A complete empty strip between room rectangles is a spatial descriptor, not certified void or movement space.']}
    (output/'comparison.json').write_text(json.dumps(record, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({'cases': len(rows), 'topologies_changed': sum(row['topology_changed'] for row in rows),
                      'selected_graph_variety': variety, 'selected_families': dict(Counter(row['after']['family'] for row in rows))}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('out/splice-portfolio'))
    compare(parser.parse_args().output.resolve(strict=True))
