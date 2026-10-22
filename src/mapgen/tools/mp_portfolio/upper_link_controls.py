"""Create two explicitly labelled MAP controls from a retained upper-link map.

The normal generator output stays unchanged. One control adds a blocking brush
inside the upper portal; the other removes only its explicit supporting slab.
These controls require independent compiled queries before any movement claim.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re

from tools.map_study.ranking_features import map_box

ROOT = Path(__file__).resolve().parents[2]
POINT = re.compile(r'\(\s*(-?\d+)\s+(-?\d+)\s+(-?\d+)\s*\)')


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def world_brushes(text):
    """Read the exact line-oriented AABB Valve220 dialect emitted by MapWriter."""
    lines = text.splitlines(keepends=True)
    require(lines and lines[0].strip() == '{', 'Expected first world entity')
    require('"classname" "worldspawn"' in ''.join(lines[:8]) and
            '"mapversion" "220"' in ''.join(lines[:8]), 'Expected generated Valve220 world')
    depth, offset, start, face_lines = 0, 0, None, []
    brushes = []
    for line in lines:
        token = line.strip()
        if token == '{':
            depth += 1
            require(depth <= 2, 'Unexpected nested brush syntax')
            if depth == 2:
                start, face_lines = offset, []
        elif token == '}':
            if depth == 2:
                require(len(face_lines) == 6, 'Expected six-face AABB brush')
                points = [tuple(map(int, point)) for face in face_lines for point in POINT.findall(face)]
                require(len(points) == 18, 'Malformed brush plane points')
                low = [min(p[i] for p in points) for i in range(3)]
                high = [max(p[i] for p in points) for i in range(3)]
                require(all(-32768 <= a < b <= 32768 for a,b in zip(low,high)), 'Invalid bounded brush')
                # Every face uses one of the six distinct axis boundary planes.
                planes = []
                for face in face_lines:
                    pts = [tuple(map(int,p)) for p in POINT.findall(face)]
                    axes = [(i,pts[0][i]) for i in range(3) if len({p[i] for p in pts}) == 1]
                    require(len(axes) == 1, 'Only axis-aligned faces are supported')
                    planes += axes
                require(set(planes) == {(i,v) for i in range(3) for v in (low[i],high[i])}, 'Incomplete AABB planes')
                brushes.append({'start':start,'end':offset+len(line),'bounds':[low,high]})
            depth -= 1
            require(depth >= 0, 'Unbalanced world entity')
            if depth == 0:
                return brushes, offset, offset+len(line)
        elif depth == 2:
            require(token.startswith('('), 'Unsupported brush content')
            face_lines.append(line)
        offset += len(line)
    raise ValueError('Unclosed world entity')


def variants(text, portal):
    require(len(portal) == 2 and all(len(p) == 3 for p in portal), 'Invalid portal bounds')
    lo,hi = portal
    require(all(type(v) is int for p in portal for v in p) and
            all(-32736 <= a < b <= 32736 for a,b in zip(lo,hi)) and lo[2] > 0,
            'Expected bounded elevated portal')
    axes = [i for i in range(2) if hi[i]-lo[i] == 48]
    require(len(axes) == 1 and hi[1-axes[0]]-lo[1-axes[0]] == 160,
            'Expected one48-unit seam and160-unit width')
    axis = axes[0]
    brushes,world_end,entity_tail = world_brushes(text)
    slab = [[lo[0],lo[1],lo[2]-24],[hi[0],hi[1],lo[2]]]
    matching = [b for b in brushes if b['bounds'] == slab]
    require(len(matching) == 1, 'Expected one exact explicit portal slab')
    blocker = [list(lo),list(hi)]
    middle = (lo[axis]+hi[axis])//2
    blocker[0][axis],blocker[1][axis] = middle-8,middle+8
    closed = text[:world_end]+map_box(*blocker)+text[world_end:]
    cut = matching[0]
    missing = text[:cut['start']]+text[cut['end']:]
    for name,output,delta in [('closed',closed,1),('missing-floor',missing,-1)]:
        new_brushes,_,new_tail = world_brushes(output)
        require(len(new_brushes) == len(brushes)+delta and output[new_tail:] == text[entity_tail:],
                'Control changed point entities or unexpected brush count: '+name)
    return {'closed':closed,'missing-floor':missing}, {'portal':portal,'axis':axis,'blocker':blocker,
        'removed_slab':slab,'world_brushes_before':len(brushes),
        'nonworld_entity_source_sha256':sha(text[entity_tail:].encode('utf-8'))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=ROOT/'out/hinge')
    parser.add_argument('--output',type=Path,default=ROOT/'out/hinge-portal-controls')
    args = parser.parse_args()
    source,output = args.source.resolve(strict=True),args.output.resolve()
    require(source.is_relative_to(ROOT/'out') and output.is_relative_to(ROOT/'out') and
            output != ROOT/'out' and source != output,
            'Controls must use distinct named project output directories')
    record = output/'record.json'
    require(not record.exists(), 'Control pair already frozen')
    layout_path = source/'layout.json'
    layout = json.loads(layout_path.read_text(encoding='utf-8-sig'))
    low_floor = min(r['bounds'][0][2] for r in layout['rooms'])
    portals = [v for c in layout['connections'] for v in c['volumes'] if v[0][2] > low_floor]
    require(len(portals) == 1, 'Expected exactly one selected upper portal')
    original = source/(layout['map_name']+'.map')
    data = original.read_bytes()
    text = data.decode('utf-8')
    maps,geometry = variants(text,portals[0])
    rows = []
    for kind,value in maps.items():
        folder = output/kind
        folder.mkdir(parents=True,exist_ok=True)
        path = folder/('qmg_hinge_'+kind.replace('-','_')+'.map')
        require(not path.exists(), 'Control MAP already exists')
        path.write_bytes(value.encode('utf-8'))
        rows.append({'kind':kind,'path':path.relative_to(ROOT).as_posix(),'sha256':sha(path.read_bytes())})
    record.write_text(json.dumps({'scope':'Labelled geometry controls, not ordinary generation outputs; no compiled or native claim yet.',
        'source_map':original.relative_to(ROOT).as_posix(),'source_map_sha256':sha(data),
        'source_layout_sha256':sha(layout_path.read_bytes()),'geometry':geometry,'controls':rows,
        'tool_sha256':sha(Path(__file__).read_bytes())},indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'controls':len(rows),'record':record.relative_to(ROOT).as_posix()}))


if __name__ == '__main__':
    main()
