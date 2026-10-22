"""Bounded compiled Lockyard sky, wet profile and separate dry-route contracts.

Wet stair translations are explicit geometric candidates with full standing
hull clearance. They do not implement gravity, friction, buoyancy or Pmove.
"""

from itertools import product

from tools.map_recipes.drop_audit import (
    _world, _flat_support, _sweep, _vector, _require, _record_vector,
    _graph, _ordinary_return, _box,
)
from tools.map_recipes.geometry import convex_polyhedron, intersects_swept_aabb, model_brushes

MAX_STEPS = 32
MAX_SEGMENTS = 32
MAX_FLOOR_CELLS = 4096


def _bounds(value):
    _require(isinstance(value, (list, tuple)) and len(value) == 2, 'feature needs two bounds vectors')
    low, high = map(_vector, value)
    _require(all(low[i] < high[i] for i in range(3)), 'feature bounds are degenerate')
    return low, high


def _box_matches(world, bounds):
    corners = set(product(*zip(*bounds)))
    return [index for index, _, shape in world if shape.bounds == bounds and set(shape.vertices) == corners]


def _flat_union_support(world, a, b):
    """Cover a swept footprint by actual coplanar rectangular top faces.

This conservative contract rejects nonrectangular support candidates instead
of inferring support from their bounds. Axis partition cells are each wholly
covered by at least one real convex top face; touching seams are allowed.
"""
    _require(a[2] == b[2] and a[1] == b[1], 'wet flat floor proof requires an axial equal-height interval')
    low=(min(a[0],b[0])-16,a[1]-16); high=(max(a[0],b[0])+16,a[1]+16)
    rectangles=[]; xs={low[0],high[0]}; ys={low[1],high[1]}
    for index,_,shape in world:
        if not any(not p[0] and not p[1] and p[2]>0 and p[3]/p[2]==a[2] for p in shape.planes):continue
        lo=tuple(max(low[i],shape.bounds[0][i]) for i in range(2))
        hi=tuple(min(high[i],shape.bounds[1][i]) for i in range(2))
        if any(lo[i]>=hi[i] for i in range(2)):continue
        corners=[(x,y,a[2]) for x in (lo[0],hi[0]) for y in (lo[1],hi[1])]
        if not all(all(sum(p[i]*v[i] for i in range(3))<=p[3] for p in shape.planes) for v in corners):continue
        rectangles.append((index,lo,hi));xs.update((lo[0],hi[0]));ys.update((lo[1],hi[1]))
    xs,ys=sorted(xs),sorted(ys)
    cells=(len(xs)-1)*(len(ys)-1)
    _require(cells<=MAX_FLOOR_CELLS,'wet flat floor cell budget exceeded')
    used=set()
    for x0,x1 in zip(xs,xs[1:]):
        for y0,y1 in zip(ys,ys[1:]):
            matches=[i for i,lo,hi in rectangles if lo[0]<=x0 and x1<=hi[0] and lo[1]<=y0 and y1<=hi[1]]
            _require(matches,'wet flat interval lacks continuous full-footprint floor support')
            used.update(matches)
    return {'support_brushes':sorted(used),'covered_partition_cells':cells,
            'continuous_full_footprint_support':True}


def _profile_points(features, world):
    """Validate actual 36x16 treads and return supported center poses in +X order."""
    trial = features['wet_trial']
    start, goal = _vector(trial['start_feet']), _vector(trial['goal_feet'])
    _require(start[1] == goal[1] and start[0] < goal[0] and start[2] == goal[2] == 0,
             'wet trial requires the declared +X lane and dry floor-zero endpoints')
    down, up = trial['descent_steps'], trial['ascent_steps']
    _require(isinstance(down, list) and isinstance(up, list) and 1 <= len(down) == len(up) <= MAX_STEPS,
             'wet stair count outside budget')
    groups, records = [], []
    for name, rows, direction in (('descent', down, -1), ('ascent', up, 1)):
        boxes = [_bounds(row) for row in rows]
        points = []
        for i, bounds in enumerate(boxes):
            low, high = bounds
            _require(high[0] - low[0] == 36 and high[1] - low[1] >= 32
                     and low[1] + 16 <= start[1] <= high[1] - 16,
                     'wet tread lacks declared width or 36-unit run')
            _require(low[2] == features['local_wet_bed'], 'wet tread base differs from bed')
            matches = _box_matches(world, bounds)
            _require(matches, 'wet tread does not match a complete compiled solid box')
            if i:
                previous = boxes[i-1]
                _require(previous[1][0] == low[0] and previous[0][1] == low[1]
                         and previous[1][1] == high[1] and high[2] - previous[1][2] == direction * 16,
                         'wet tread sequence must be contiguous and change by sixteen units')
            feet = ((low[0] + high[0]) / 2, start[1], high[2])
            support = _flat_support(world, feet, name + ' tread')
            points.append(feet)
            records.append({'kind':name,'index':i,'bounds':[list(map(int,v)) for v in bounds],
                            'compiled_brushes':matches,'support_brushes':support,'feet':_record_vector(feet)})
        groups.append((boxes, points))
    down_boxes, down_points = groups[0]
    up_boxes, up_points = groups[1]
    bed = features['local_wet_bed']
    _require(list(map(_vector,trial['bottom_segment'])) ==
             [(down_boxes[-1][1][0],start[1],bed),(up_boxes[0][0][0],start[1],bed)],
             'declared bottom segment differs from the real stair edges')
    _require(down_boxes[0][1][2] == up_boxes[-1][1][2] == 0
             and down_boxes[-1][1][2] == up_boxes[0][1][2] == bed + 16,
             'wet profile must connect dry floor to the exact bed in sixteen-unit increments')
    _require(start[0] <= down_boxes[0][0][0] - 64 and goal[0] >= up_boxes[-1][1][0] + 64,
             'wet trial needs at least sixty-four dry units before and after its treads')
    _require(down_boxes[-1][1][0] + 32 <= up_boxes[0][0][0], 'wet profile lacks a full-footprint bottom landing')
    bottom_a = (down_boxes[-1][1][0] + 16, start[1], bed)
    bottom_b = (up_boxes[0][0][0] - 16, start[1], bed)
    points = [start] + down_points + [bottom_a, bottom_b] + up_points + [goal]
    for feet in (start, bottom_a, bottom_b, goal):
        _flat_support(world, feet, 'wet endpoint or bottom landing')
    return points, records, down_boxes, up_boxes


def _sweep_profile(world, points, counts):
    """Split each riser at the real edge plus/minus the sixteen-unit hull radius."""
    records = []
    for i, (a, b) in enumerate(zip(points, points[1:])):
        _require(a[1] == b[1] and a[0] < b[0] and abs(b[2]-a[2]) <= 16,
                 'wet profile contains an invalid station interval')
        if a[2] == b[2]:
            knots = [a, b]
            floor_support = _flat_union_support(world,a,b)
        elif b[2] < a[2]:
            # The next tread center is eighteen from its edge; fully clear the
            # old riser at edge+16, descend, then move the final two units.
            x = b[0] - 2 if b[0]-a[0] == 36 else b[0]
            knots = [a, (x,a[1],a[2]), (x,a[1],b[2]), b]
            floor_support = None
        else:
            x = b[0] - 34
            knots = [a, (x,a[1],a[2]), (x,a[1],b[2]), b]
            floor_support = None
        unique = [knots[0]]
        for p in knots[1:]:
            if p != unique[-1]: unique.append(p)
        for j, (first,last) in enumerate(zip(unique, unique[1:])):
            _sweep(world, first, last, f'wet geometric interval {i} leg {j}', counts)
        # The same occupied volumes give exact reverse clearance. Dynamic
        # reverse traversal is still a separate native movement question.
        records.append({'from':_record_vector(a),'to':_record_vector(b),
                        'knots':[_record_vector(p) for p in unique],
                        'flat_floor_support':floor_support,
                        'continuous_forward_and_reverse_clearance':True,
                        'transition_support':'full footprint at stations; step translations may leave the support plane'})
    return records


def audit_lockyard(bsp, layout, texture_physics, features, report=None):
    _require(features['map'] == 'qmg_lockyard' and features['layout_sha256'] == layout['layout_sha256'],
             'Lockyard features and layout identities differ')
    _require(features['main_floors'] == [0,128] and features['local_wet_bed'] == -128,
             'Lockyard combat floors and local wet bed differ')
    world = _world(bsp)
    expected = _bounds(features['water'])
    _require(expected == ((-224,-736,-128),(736,-304,-16)), 'Lockyard liquid contract changed')
    liquids = [s for s in layout['solids'] if s['purpose'] == 6]
    _require(len(liquids) == 1 and liquids[0]['contents'] == 'water'
             and _bounds(liquids[0]['bounds']) == expected, 'typed liquid differs from feature contract')
    roof = _bounds(features['sky_roof'])
    _require(features['sky_rooms'] == [1] and features['sky'] == 'unit1_', 'unexpected sky room or environment')
    court = next(room for room in layout['rooms'] if room['id'] == 1)
    _require(roof[0] == tuple(court['bounds'][0][:2]+[court['bounds'][1][2]])
             and roof[1][:2] == tuple(court['bounds'][1][:2]), 'sky roof does not belong to the Court bounds')
    water, sky, contents_rows = [], [], []
    for index in model_brushes(bsp):
        first,count,contents = bsp.brushes[index]
        base = contents & ~((1 << 27) | (1 << 28))
        _require(contents == 1 or base == 32, 'unexpected architectural collision contents')
        shape = convex_polyhedron(bsp.planes[p] for p,_ in bsp.brushsides[first:first+count])
        if base == 32:
            _require(shape.bounds == expected and set(shape.vertices) == set(product(*zip(*expected))),
                     'liquid must retain the complete compiled authored box')
            water.append(index); contents_rows.append(contents)
        for plane_index, texindex in bsp.brushsides[first:first+count]:
            tex = bsp.texinfo[texindex]
            material = tex[10].split(b'\0',1)[0]
            if base == 32:
                _require(material == b'e3u2/water2' and tex[8] == 24 and tex[9] == 0,
                         'compiled water needs explicit WATER/WARP/TRANS33 and no emission')
            if tex[8] & 4:
                p = bsp.planes[plane_index]
                _require(contents == 1 and shape.bounds == roof and set(shape.vertices) == set(product(*zip(*roof)))
                         and p[0] == p[1] == 0 and p[2] < 0 and p[3]/p[2] == roof[0][2]
                         and material == b'e1u1/sky1' and tex[8] == 4 and tex[9] == 0,
                         'sky must occur only on the chosen solid Court roof inner face')
                sky.append({'brush':index,'plane':plane_index})
    _require(len(water) == 1 and len(sky) == 1, 'expected one liquid and one Court sky side')
    _require({'e3u2/water2','e1u1/sky1'} <= texture_physics.keys(), 'required physical material metadata is missing')
    for material, physics in texture_physics.items():
        if material == 'e3u2/water2':
            _require(physics == {'flags':25,'contents':32,'value':100}, 'unexpected installed water metadata')
        elif material == 'e1u1/sky1':
            _require(physics == {'flags':132,'contents':0,'value':0}, 'unexpected installed sky metadata')
        else:
            _require(physics['contents'] == 0 and not physics['flags'] & (8|16|32),
                     'ordinary material carries unexpected liquid physics')
    worldspawn = [e for e in bsp.entities if e.get('classname') == 'worldspawn']
    _require(len(worldspawn) == 1 and worldspawn[0].get('sky') == 'unit1_', 'compiled worldspawn sky differs')
    sky_rays = []
    for a in ((0,0,46),(160,288,46),(0,400,174)):
        b = (*a[:2], roof[0][2]-1)
        _require(bsp.trace(a,b,mask=1) == 1, 'selected Court sky ray is occluded before the roof')
        _require(bsp.trace(b,(*b[:2],roof[0][2]+8),mask=1) < 1, 'Court sky ray lacks a sealed roof')
        sky_rays.append({'from':list(a),'to':_record_vector(b),'clear_to_sky':True,'roof_sealed':True})

    counts = {'world_pair_checks':0,'exact_swept_checks':0}
    segments = features['flat_segments']
    _require(1 <= len(segments) <= MAX_SEGMENTS, 'flat segment count outside budget')
    flats = []
    for row in segments:
        a,b = _vector(row['from']),_vector(row['to'])
        _require(a[2] == b[2] and sum(a[i] != b[i] for i in range(2)) == 1, 'selected flat segment must be axial')
        common = sorted(set(_flat_support(world,a,row['label'])) & set(_flat_support(world,b,row['label'])))
        _require(common, 'flat segment needs continuous common convex support')
        _sweep(world,a,b,row['label'],counts)
        flats.append({**row,'common_compiled_support':common,'continuous_hull_clear':True})
    points, treads, _, _ = _profile_points(features, world)
    profile = _sweep_profile(world,points,counts)
    ceiling = _bounds(features['submerged_ceiling'])
    _require(_box_matches(world,ceiling) and ceiling[0][2] == -40, 'submerged ceiling is not the compiled box')
    emergence = []
    for x in (160,416):
        a,b = _vector((x,-560,-128)),_vector((x,-560,-61))
        _flat_support(world,a,'emergence bottom')
        _sweep(world,a,b,'emergence column',counts)
        emergence.append({'from':_record_vector(a),'to':_record_vector(b),'standing_eye_at_top':-15,'clear':True})
    a,b = _vector((304,-560,-128)),_vector((304,-560,-61))
    blocked = [i for i,_,shape in world if intersects_swept_aabb(shape,*_box(a),tuple(b[j]-a[j] for j in range(3)))]
    _require(blocked, 'submerged commitment must block straight-up surfacing under the lintel')
    probes = [(160,-560,z) for z in (-127,-105,-82)]
    inside = [bsp.trace(p,p,mask=32) == 0 for p in probes]
    _require(inside == [True,True,True], 'deep-bed stationary waterlevel samples differ')
    nodes, edges, adjacency, excluded = _graph(layout,report)
    dry = _ordinary_return(nodes,edges,adjacency,points[0],points[-1])
    reverse = _ordinary_return(nodes,edges,adjacency,points[-1],points[0])
    _require(layout['ordinary_path_checks'] == 264 and layout['ordinary_route_edge_kinds'] == ['walk','step','door'],
             'required routes must stay ordinary and dry')
    return {'kind':'compiled_sky_deep_water_profile_and_separate_dry_routes','ok':True,
            'world_player_blocking_brushes':len(world),'water_brush_indices':water,
            'water_bounds':features['water'],'water_compiled_contents':contents_rows,
            'water_surface_flags':24,'water_surface_value':0,'water_depth':112,
            'sky_roof':features['sky_roof'],'sky_sides':sky,'selected_sky_rays':sky_rays,
            'selected_flat_segments':flats,'wet_supported_treads':treads,'wet_geometric_intervals':profile,
            'wet_start_feet':_record_vector(points[0]),'wet_goal_feet':_record_vector(points[-1]),
            'emergence_columns':emergence,'submerged_ceiling':features['submerged_ceiling'],
            'blocked_surfacing_brushes':blocked,'water_sample_points':probes,'water_sample_inside':inside,
            'interpreted_standing_waterlevel_at_bottom':3,'dry_station_forward':dry,'dry_station_reverse':reverse,
            'excluded_nonordinary_edges':excluded,**counts,'runtime_verified':False,
            'limits':[
                'Exact standing-hull box translations prove selected geometric candidates, not native swimming, stepping, speed, breath or damage.',
                'Full footprint support is checked at every tread center and dry/bottom station; artificial step translations can temporarily leave support.',
                'Dry station return uses the directed serialized ordinary graph with exact local endpoint binding; its entire route is not compiled-swept here.',
                'Three clear vertical rays verify selected sky openings; they do not prove all Court points are exposed to sky or establish rendered sunlight.',
                'Waterlevel is interpreted from three stationary sample heights. Native Pmove evidence, if present, is a separate bounded experiment.',
            ]}
