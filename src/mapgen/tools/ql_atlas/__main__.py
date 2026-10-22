"""Build a local atlas: python -B -m tools.ql_atlas --root <QL-preSteam> --output out/ql-atlas."""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import json
import math
from pathlib import Path
import platform
import stat

import shapely
from shapely.geometry import Polygon
from shapely.ops import unary_union

from tools.map_study.binary import read_bytes
from .bsp import QlBsp, clip_z, PATCH_STEPS

MAPS = ('campgrounds','bloodrun','aerowalk','furiousheights','toxicity','lostworld')
BAND_WIDTH = 128


def liquid_kind(contents):
    return 'lava' if contents & 8 else 'slime' if contents & 16 else 'water' if contents & 32 else 'surface'


def polygon_rings(geometry):
    """Dissolved projection retains holes and disconnected components."""
    polygons = [geometry] if geometry.geom_type == 'Polygon' else list(getattr(geometry,'geoms',[]))
    result=[]
    for polygon in polygons:
        if polygon.geom_type != 'Polygon' or polygon.area <= .001:
            continue
        result.append([[[round(x,3),round(y,3)] for x,y in ring.coords]
                       for ring in [polygon.exterior,*polygon.interiors]])
    return result


def surface_context(origin, triangles, maximum_gap=64):
    """Visual band association only: highest rendered triangle below one XY point.

    This is not collision/support: no player hull, solid contents, headroom or
    native item-drop behavior is considered. Keep the authored origin unchanged.
    """
    x,y,z=origin
    candidates=[]
    for triangle in triangles:
        a,b,c=triangle['points']
        if not min(a[0],b[0],c[0])<=x<=max(a[0],b[0],c[0]) or not min(a[1],b[1],c[1])<=y<=max(a[1],b[1],c[1]):
            continue
        turns=[(q[0]-p[0])*(y-p[1])-(q[1]-p[1])*(x-p[0]) for p,q in ((a,b),(b,c),(c,a))]
        if not (all(t>=-1e-7 for t in turns) or all(t<=1e-7 for t in turns)):
            continue
        n=triangle['normal']
        floor=a[2]-(n[0]*(x-a[0])+n[1]*(y-a[1]))/n[2]
        if 0<=z-floor<=maximum_gap:
            candidates.append((floor,triangle['surface']))
    if candidates:
        floor,face=max(candidates)
        return {'band':math.floor(floor/BAND_WIDTH),'basis':'nearby upward render surface, not hull support',
                'surface_z':floor,'surface_id':face,'vertical_gap':z-floor}
    return {'band':math.floor(z/BAND_WIDTH),'basis':'authored origin; no upward surface within64 below'}


def analyze(path: Path, name: str):
    data=read_bytes(path)
    bsp=QlBsp(data)
    triangles=list(bsp.triangles())
    annotations,links=bsp.annotations()
    for annotation in annotations:
        annotation['band_context']=surface_context(annotation['origin'],triangles)
    contexts={annotation['id']:annotation['band_context'] for annotation in annotations}
    for link in links:
        link['from_context']=contexts[link['source']]
        link['to_context']=surface_context(link['to'],triangles)
    groups=defaultdict(list)
    liquid_shaders=Counter()
    patch_count=0
    for triangle in triangles:
        xyz=triangle['points']
        patch_count+=triangle['patch']
        kind=liquid_kind(triangle['contents'])
        if kind!='surface': liquid_shaders[triangle['shader']]+=1
        low,high=min(p[2] for p in xyz),max(p[2] for p in xyz)
        for band in range(math.floor(low/BAND_WIDTH), math.floor(high/BAND_WIDTH)+1):
            points=clip_z(xyz,band*BAND_WIDTH,(band+1)*BAND_WIDTH)
            if len(points)<3:continue
            shape=Polygon([p[:2] for p in points])
            if shape.is_valid and shape.area>.001:
                groups[(band,kind)].append(shape)
    projected=[]
    merged=[]
    band_shapes=defaultdict(list)
    for (band,kind), shapes in sorted(groups.items()):
        dissolved=unary_union(shapes)
        merged.append(dissolved)
        band_shapes[band].append(dissolved)
        projected.append({'band':band,'kind':kind,'polygons':polygon_rings(dissolved),
                          'projected_union_area':round(dissolved.area,3)})
    band_areas={band:unary_union(shapes).area for band,shapes in band_shapes.items()}
    whole=unary_union(merged)
    bounds=list(whole.bounds)
    # Annotation endpoints can extend beyond the surfaces (e.g. push targets).
    all_points=[point['origin'][:2] for point in annotations]+[link['to'][:2] for link in links]
    if all_points:
        bounds=[min(bounds[0],*(p[0] for p in all_points)),min(bounds[1],*(p[1] for p in all_points)),
                max(bounds[2],*(p[0] for p in all_points)),max(bounds[3],*(p[1] for p in all_points))]
    world=bsp.entities[0]
    counts=Counter(e.get('classname','') for e in bsp.entities)
    gameplay_heights=[p['origin'][2] for p in annotations if p['kind'] in ('weapon','item','spawn')]
    metrics={'world_render_surfaces':bsp.models[0][7], 'world_brushes':bsp.models[0][9],
             'upward_triangles_including_patch_samples':len(triangles),'patch_sample_triangles':patch_count,
             'authored_entity_classes':dict(sorted(counts.items())),
             'authored_teleport_triggers':counts['trigger_teleport'],
             'resolved_direct_teleport_targets':sum(link['kind']=='teleport' for link in links),
             'authored_push_triggers':counts['trigger_push'],
             'liquid_surface_shaders':dict(liquid_shaders),
             'surface_projection_bounds':list(whole.bounds),
             'upward_surface_xy_union_area':whole.area,
             'upward_surface_projection_box_coverage':whole.area/((whole.bounds[2]-whole.bounds[0])*(whole.bounds[3]-whole.bounds[1])),
             'authored_gameplay_origin_z_range':[min(gameplay_heights),max(gameplay_heights)],
             'height_band_projected_areas':dict(sorted(band_areas.items()))}
    return {'schema_version':1,'map':name,'title':world.get('message',name),
            'authors_from_worldspawn':[world[key] for key in ('author','author2') if world.get(key)],
            'source':{'path':str(path.resolve()),'sha256':bsp.sha256,'bytes':len(data),'bsp_version':bsp.version,
                      'revision_context':'Local QL-preSteam directory snapshot; date/current Steam equivalence unverified'},
            'metrics':metrics,'bounds':bounds,'band_width':BAND_WIDTH,'bands':sorted(band_areas),
            'surfaces':projected,'annotations':annotations,'links':links,'issues':bsp.issues}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--format-reference-root',type=Path,help='Optional local source-code root for format-reference SHA pins')
    args=parser.parse_args()
    output=args.output.absolute()
    for p in (output,*output.parents):
        if p.exists() and (p.is_symlink() or getattr(p.lstat(),'st_file_attributes',0)&getattr(stat,'FILE_ATTRIBUTE_REPARSE_POINT',0)):
            raise ValueError('output cannot traverse links')
    output.mkdir(parents=True,exist_ok=True)
    (output/'maps').mkdir(exist_ok=True)
    records=[]
    for name in MAPS:
        record=analyze(args.root/f'{name}.pk3/maps/{name}.bsp',name)
        records.append(record)
        (output/'maps'/f'{name}.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
        print(name,record['source']['sha256'],len(record['bands']),'bands',flush=True)
    method={'purpose':'Local personal research; orthographic analytical reference atlas',
            'projection':'Orthographic +Z top-down: x right, +Y up; no perspective',
            'surface_selection':'Model-zero rendered triangles with upward normal Z >= 0.7; sky/nodraw excluded; no walkability inference',
            'patches':f'Actual quadratic Bezier surfaces sampled on {PATCH_STEPS}x{PATCH_STEPS} cells per 3x3 patch block; approximation, not engine tessellation',
            'height_bands':f'World-aligned {BAND_WIDTH}-unit slabs; triangles clipped at slab boundaries before XY union; bands are not certified floors',
            'union':'Shapely XY union by slab and surface/liquid category; holes retained; output coordinates rounded to 0.001 unit',
            'items':'All authored item/weapon/spawn variants retained; mode filters recorded but not evaluated; origins are not settled pickup centers. Display band uses highest upward rendered triangle under the same XY within64 units below; unresolved points retain origin band. This visual context is not hull support, movement or native item-drop evidence.',
            'teleports':'Source submodel AABB centers and direct target-key destinations; dashed relation is not a physical journey or runtime activation proof',
            'liquids':'Only compiled shader contents bits lava=8, slime=16, water=32; projected rendered surface, not depth/swimmability/damage',
            'coverage':'Union of upward rendered XY surfaces / their bounding rectangle, includes inaccessible trim and exterior tops; neither exterior envelope occupancy nor walkable density',
            'common_scale':'Identical square world span and units-per-pixel across all six maps at a given viewport; centered per map',
            'limits':['Overlapping heights can obscure each other in all-height projection. Inspect individual bands.',
                      'Model-zero only: func_static, movers, trigger geometry and other entity submodels are not floor surfaces. Trigger AABBs are annotation locators only.',
                      'Texture/lightmap data, sky, vertical walls, downward surfaces and flares are not displayed.',
                      'No native navigation, collision hull, line-of-sight, runtime, damage, rendering or balance test.',
                      'No geometry copied into generated levels, no training dataset or weights, no raw BSP or game assets included in atlas package.',
                      'PreSteam directory identity is retained; exact release date and current Steam revision equivalence are unverified.']}
    data={'maps':[{k:v for k,v in r.items() if k not in ('source','metrics','issues','authors_from_worldspawn','schema_version')} for r in records],
          'common_span':math.ceil(max(max(r['bounds'][2]-r['bounds'][0],r['bounds'][3]-r['bounds'][1]) for r in records)/128)*128}
    template=Path(__file__).with_name('atlas-view.html').read_text(encoding='utf-8')
    if template.count('__ATLAS_DATA__')!=1:raise ValueError('one data placeholder required')
    fragment=template.replace('__ATLAS_DATA__',json.dumps(data,separators=(',',':')).replace('<','\\u003c'))
    if len(fragment.encode())>1_000_000:raise ValueError(f'fragment too large: {len(fragment.encode())}')
    (output/'ql-reference-atlas.html').write_text(fragment,encoding='utf-8',newline='\n')
    references=[]
    if args.format_reference_root:
        for relative in ('netradiant-custom-current/tools/quake3/q3map2/bspfile_ibsp.cpp',
                         'Quake3e-master/code/qcommon/qfiles.h','Quake3e-master/code/qcommon/surfaceflags.h'):
            path=args.format_reference_root/relative
            references.append({'path':str(path.resolve()),'sha256':hashlib.sha256(read_bytes(path)).hexdigest(),
                               'use':'format facts only; no implementation incorporated'})
    source_files=[p for p in sorted(Path(__file__).parent.glob('*')) if p.is_file()]
    source_files.extend((Path('tools/map_study/binary.py'),Path('tests/test_ql_atlas.py')))
    manifest={'schema_version':1,'method':method,'runtime':{'python':platform.python_version(),'shapely':shapely.__version__},
              'common_span':data['common_span'],
              'tools':{str(p.as_posix()):hashlib.sha256(p.read_bytes()).hexdigest() for p in source_files},
              'format_references':references,
              'maps':[{k:v for k,v in r.items() if k not in ('surfaces','annotations','links')} for r in records],
              'files':{f'maps/{r["map"]}.json':hashlib.sha256((output/'maps'/f'{r["map"]}.json').read_bytes()).hexdigest() for r in records}}
    (output/'atlas.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    (output/'README.md').write_text(Path(__file__).with_name('README.md').read_text(encoding='utf-8'),encoding='utf-8')


if __name__=='__main__':main()
