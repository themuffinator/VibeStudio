"""Compiled Fold feature contracts; no native movement or encounter claims."""

import math
from itertools import product

from tools.map_recipes.drop_audit import _world, _flat_support, _sweep, _vector
from tools.map_recipes.geometry import convex_polyhedron, intersects_aabb, model_brushes


def audit_fold(bsp, layout, texture_physics, features=None):
    world = _world(bsp)
    liquids = [s for s in layout['solids'] if s['purpose'] == 6]
    assert len(liquids) == 1 and liquids[0]['contents'] == 'water'
    expected = tuple(tuple(v) for v in liquids[0]['bounds'])
    assert expected == ((512,-64,0),(752,160,24))
    water = []
    water_contents = []
    for index in model_brushes(bsp):
        first,count,contents = bsp.brushes[index]
        # ericw marks transparent liquid DETAIL|TRANSLUCENT in addition to its
        # native WATER bit. Preserve those real compiler semantics explicitly.
        base_contents = contents & ~((1 << 27) | (1 << 28))
        assert contents == 1 or base_contents == 32, f'Unexpected Fold architectural contents {contents}'
        if base_contents == 32:
            shape = convex_polyhedron(bsp.planes[i] for i,_ in bsp.brushsides[first:first+count])
            assert shape.bounds == expected, 'Liquid BSP bounds differ from authored volume'
            expected_corners = set(product(*zip(*expected)))
            assert set(shape.vertices) == expected_corners, 'Liquid BSP must retain the complete authored box, not only its bounds'
            for _,texindex in bsp.brushsides[first:first+count]:
                tex = bsp.texinfo[texindex]
                assert tex[10].split(b'\0',1)[0] == b'e3u2/water2'
                assert tex[8] == 24 and tex[9] == 0, 'Liquid needs deliberate WARP|TRANS33 without emissive value'
            water.append(index)
            water_contents.append(contents)
    assert len(water) == 1
    for material,physics in texture_physics.items():
        if material == 'e3u2/water2':
            assert physics == {'flags':25,'contents':32,'value':100}
        else:
            assert physics['contents'] == 0 and not physics['flags'] & (8|16|32)

    # Native waterlevel samples for a standing player on floor 0. This does not
    # execute Pmove; currents, speed, drowning, damage and exit behavior remain open.
    probes = [(632,48,z) for z in (1,23,46)]
    inside = [bsp.trace(p,p,mask=32) == 0 for p in probes]
    assert inside == [True,True,False]
    source = [e for e in bsp.entities if e.get('classname') == 'misc_teleporter']
    target = [e for e in bsp.entities if e.get('classname') == 'misc_teleporter_dest']
    assert len(source) == len(target) == 1
    source,target = source[0],target[0]
    assert source['target'] == target['targetname'] and 'angle' in target
    named_targets = [e for e in bsp.entities if e.get('targetname') == source['target']]
    assert len(named_targets) == 1 and named_targets[0] is target, 'Native teleporter target must be unique across all entities'
    yaw = float(target['angle'])
    assert math.isfinite(yaw), 'Native destination yaw must be finite'
    if features is not None:
        specification = features['teleporter']
        assert list(map(float,source['origin'].split())) == specification['source_origin']
        assert list(map(float,target['origin'].split())) == specification['destination_origin']
        assert yaw == specification['arrival_yaw'], 'Native destination yaw differs from the authored contract'
    assert layout['navigation_edge_counts']['teleporter'] == 1
    assert layout['ordinary_path_checks'] == 264
    assert layout['ordinary_route_edge_kinds'] == ['walk','step','door']
    poses = []
    supports = []
    for entity,offset in ((source,8),(target,10)):
        origin = tuple(float(x) for x in entity['origin'].split())
        floor = origin[2]-24
        # The native disc is wider than a standing player, so explicitly check
        # its complete support/clearance, followed by the raised standing pose.
        corners = [(origin[0]+dx,origin[1]+dy,floor) for dx in (-32,32) for dy in (-32,32)]
        support = [index for index,_,shape in world if
                   any(p[0] == p[1] == 0 and p[2] > 0 and p[3]/p[2] == floor for p in shape.planes)
                   and all(all(sum(p[i]*point[i] for i in range(3)) <= p[3]
                               for p in shape.planes) for point in corners)]
        assert support, 'Teleporter disc lacks full 64x64 compiled support'
        for _,_,shape in world:
            assert not intersects_aabb(shape,(origin[0]-32,origin[1]-32,floor),
                                       (origin[0]+32,origin[1]+32,floor+8))
            assert not intersects_aabb(shape,(origin[0]-16,origin[1]-16,origin[2]+offset-24),
                                       (origin[0]+16,origin[1]+16,origin[2]+offset+32))
        poses.append([origin[0],origin[1],origin[2]+offset])
        supports.append(support)
    mechanism = next(m for m in layout['mechanisms'] if m['kind'] == 'teleporter')
    assert list(map(float,source['origin'].split())) == [*mechanism['from'][:2],mechanism['from'][2]+24]
    assert list(map(float,target['origin'].split())) == [*mechanism['to'][:2],mechanism['to'][2]+24]

    routes = []
    counts = {'world_pair_checks':0,'exact_swept_checks':0}
    if features is not None:
        assert features['map'] == 'qmg_fold' and features['layout_sha256'] == layout['layout_sha256']
        assert 2 <= len(features['flat_segments']) <= 16
        for route in features['flat_segments']:
            a,b = _vector(route['from']),_vector(route['to'])
            assert a[2] == b[2] and sum(a[i] != b[i] for i in range(2)) == 1
            first = _flat_support(world,a,route['label']+' start')
            last = _flat_support(world,b,route['label']+' finish')
            common = sorted(set(first) & set(last))
            assert common, 'Flat segment needs the same convex support plane along its complete path'
            _sweep(world,a,b,route['label'],counts)
            routes.append({**route,'full_segment_flat_support_brushes':common,'continuous_hull_clear':True})
    return {'kind':'compiled_liquid_teleporter_and_selected_stacked_routes','ok':True,
            'world_player_blocking_brushes':len(world),'water_brush_indices':water,
            'water_bounds':liquids[0]['bounds'],'water_base_contents':32,'water_compiled_contents':water_contents,'water_surface_flags':24,
            'water_surface_value':0,'water_sample_points':probes,'water_sample_inside':inside,
            'interpreted_standing_waterlevel_at_probe':2,'teleporter_native_clearance_poses':poses,
            'teleporter_disc_full_support_brushes':supports,'destination_yaw':yaw,
            'ordinary_anchor_pairs':264,'selected_flat_segments':routes,**counts,
            'runtime_verified':False,'limits':[
                'Selected flat segments have continuous compiled hull clearance and common convex support; they do not establish every route through the map.',
                'Liquid samples interpret three stationary native sample heights, not Pmove. Water friction, speed, sound, air, damage and wet exits remain untested.',
                'Teleporter support and raised arrival geometry are checked. Native trigger contact, velocity reset, exit threats and telefrags are not exercised.',
                'Required graph routes exclude water, jumps, drops, lifts and teleporters. The optional wet route has no swimming navigation edges.',
            ]}
