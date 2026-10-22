"""Exact XY envelope coverage of authored room/passage boxes.

This deliberately does not call projected volume coverage walkable density.
Room interiors may contain solid cores, galleries, hazards or inaccessible areas.
"""

import argparse
from collections import deque
import hashlib
import json
import math
from pathlib import Path


def packing(rectangles):
    """Measure a bounded union and partition uncovered cells by edge connection."""
    rectangles = list(rectangles)
    if not rectangles or len(rectangles) > 1024:
        raise ValueError('Expected 1..1024 rectangles')
    for rect in rectangles:
        if (len(rect) != 4 or any(isinstance(v, bool) or not isinstance(v, (int, float))
                                 or not math.isfinite(v) for v in rect)
                or rect[0] >= rect[2] or rect[1] >= rect[3]):
            raise ValueError('Rectangle must have finite positive XY area')
    xs = sorted({r[i] for r in rectangles for i in (0, 2)})
    ys = sorted({r[i] for r in rectangles for i in (1, 3)})
    nx, ny = len(xs)-1, len(ys)-1
    if nx*ny > 500_000 or nx*ny*len(rectangles) > 20_000_000:
        raise ValueError('Packing grid budget exceeded')
    # Every boundary comes from the input: no sampling/grid-size approximation.
    empty = set()
    covered = 0
    for x in range(nx):
        for y in range(ny):
            cell_area = (xs[x+1]-xs[x])*(ys[y+1]-ys[y])
            if any(r[0] <= xs[x] and xs[x+1] <= r[2]
                   and r[1] <= ys[y] and ys[y+1] <= r[3] for r in rectangles):
                covered += cell_area
            else:
                empty.add((x,y))
    reached = {cell for cell in empty if cell[0] in (0,nx-1) or cell[1] in (0,ny-1)}
    pending = deque(sorted(reached))
    while pending:
        x,y = pending.popleft()
        for cell in ((x-1,y),(x+1,y),(x,y-1),(x,y+1)):
            if cell in empty and cell not in reached:
                reached.add(cell)
                pending.append(cell)
    def area(cells):
        return sum((xs[x+1]-xs[x])*(ys[y+1]-ys[y]) for x,y in cells)
    envelope = (xs[-1]-xs[0])*(ys[-1]-ys[0])
    return {
        'bounds_xy': [[xs[0],ys[0]],[xs[-1],ys[-1]]],
        'width': xs[-1]-xs[0], 'depth': ys[-1]-ys[0],
        'envelope_area': envelope, 'projected_volume_union_area': covered,
        'envelope_coverage_fraction': covered/envelope,
        'edge_connected_uncovered_area': area(reached),
        'enclosed_uncovered_area': area(empty-reached),
        'uncovered_area': area(empty), 'rectangles':len(rectangles),
        'exact_partition_cells':nx*ny,
    }


def analyze_layout(layout):
    bounds = [room['bounds'] for room in layout['rooms']]
    bounds += [b for connection in layout['connections'] for b in connection['volumes']]
    for b in bounds:
        if (len(b) != 2 or any(len(p) != 3 for p in b)
                or any(isinstance(v, bool) or not isinstance(v, (int,float))
                       or not math.isfinite(v) for p in b for v in p)
                or any(b[0][axis] >= b[1][axis] for axis in range(3))):
            raise ValueError('Expected valid room/passage volume bounds')
    return packing([(b[0][0],b[0][1],b[1][0],b[1][1]) for b in bounds])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--layouts', type=Path, nargs='+', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = {'schema_version':1, 'method':'exact_axis_aligned_room_and_passage_xy_union',
              'units':'map units; areas in square map units', 'maps':[],
              'limits':[
                  'All-height authored room/passage boxes are projected without subtracting internal solids.',
                  'This is gross volume packing, not usable floor area, hull clearance, travel time or map quality.',
                  'Uncovered areas may be intentional structure, courtyards or sightline separation. No quality threshold is assigned.',
                  'Connectivity of uncovered cells uses shared edges; corner contact is not a connection.',
                  'Do not compare these coverage fractions with QL rendered-surface coverage as if their denominators and geometry were identical.',
              ]}
    for path in args.layouts:
        raw = path.read_bytes()
        layout = json.loads(raw.decode('utf-8-sig'))
        result['maps'].append({'map':layout['map_name'],'layout_path':path.as_posix(),
                               'layout_sha256':hashlib.sha256(raw).hexdigest(),
                               'canonical_layout_sha256':layout['layout_sha256'],
                               **analyze_layout(layout)})
    args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2))


if __name__ == '__main__':
    main()
