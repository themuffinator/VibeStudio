"""Portable full-audit controls for physical arrival and endpoint binding.

The synthetic BSP tables describe actual box brushes consumed by validated_world
and the exact sweep/support code. Its trace callback is deliberately a stub:
these controls assert geometry and identity, not sampled visibility outcomes.
No published Relay files or optional engine executables are required.
"""
import copy
from types import SimpleNamespace
import unittest

from tools.map_recipes.arrival_audit import audit, bind_departures


def fixture():
    boxes = [
        ((-928, -288, -24), (928, 288, 0)),
        ((-96, -32, 0), (96, 0, 192)),
        ((-96, -32, 192), (96, 0, 224)),
    ]
    planes, sides, brushes = [], [], []
    for low, high in boxes:
        first = len(sides)
        for plane in ((1, 0, 0, high[0]), (-1, 0, 0, -low[0]),
                      (0, 1, 0, high[1]), (0, -1, 0, -low[1]),
                      (0, 0, 1, high[2]), (0, 0, -1, -low[2])):
            sides.append((len(planes), 0))
            planes.append(plane)
        brushes.append((first, 6, 1))
    bsp = SimpleNamespace(
        planes=planes, brushsides=sides, brushes=brushes,
        models=[(0,)*9+(-1,)], nodes=[], leaves=[(0,)*11+(0, len(brushes))],
        leafbrushes=list(range(len(brushes))), sha256='synthetic-arrival-binding-fixture',
        entities=[{'classname': 'worldspawn'},
                  {'classname': 'misc_teleporter', 'origin': '-640 0 24', 'target': 'arrival'},
                  {'classname': 'misc_teleporter_dest', 'origin': '0 128 24',
                   'targetname': 'arrival', 'angle': '0'},
                  {'classname': 'weapon_chaingun', 'origin': '0 -128 24'}],
        trace=lambda start, end, mask: 1.0)
    rooms = [
        {'id': 1, 'bounds': [[-384, -256, 0], [384, 256, 192]]},
        {'id': 2, 'bounds': [[-896, -256, 0], [-432, 256, 192]]},
        {'id': 3, 'bounds': [[432, -256, 0], [896, 256, 192]]},
    ]
    layout = {
        'map_name': 'qmg_relay', 'layout_sha256': 'synthetic-layout', 'rooms': rooms,
        'connections': [
            {'id': 1, 'from': 1, 'to': 2, 'volumes': [[[-432, -80, 0], [-384, 80, 192]]]},
            {'id': 2, 'from': 1, 'to': 3, 'volumes': [[[384, -80, 0], [432, 80, 192]]]}],
        'solids': [{'id': 1, 'bounds': [[-96, -32, 0], [96, 0, 192]]}],
        'placements': [{'id': 1, 'role': 4, 'origin': [0, -128, 24]}],
        'navigation_edge_counts': {'teleporter': 1},
        'mechanisms': [{'kind': 'teleporter', 'from': [-640, 0, 0], 'to': [0, 128, 0]}],
    }
    report = {'validation': {'ok': True}, 'layout_sha256': 'synthetic-layout',
              'analysis': {'navigation': {'player': {'available': True, 'ok': True,
                                                    'layout_sha256': 'synthetic-layout'}}}}
    features = {
        'schema_version': 1, 'map': 'qmg_relay', 'layout_sha256': 'synthetic-layout',
        'teleporter': {'source_origin': [-640, 0, 24], 'destination_origin': [0, 128, 24], 'arrival_yaw': 0},
        'partition': {'solid_id': 1, 'bounds': [[-96, -32, 0], [96, 0, 192]]},
        'resource': {'placement_id': 1, 'classname': 'weapon_chaingun', 'origin': [0, -128, 24]},
        'source_approach': [-768, -128, 0],
        'paths': [
            {'id': 'west', 'kind': 'departure', 'exit_connection': 1,
             'points': [[-128, 128, 0], [-256, 128, 0], [-256, 0, 0], [-512, 0, 0]]},
            {'id': 'east', 'kind': 'departure', 'exit_connection': 2,
             'points': [[128, 128, 0], [256, 128, 0], [256, 0, 0], [512, 0, 0]]},
            {'id': 'ordinary', 'kind': 'ordinary_bypass',
             'points': [[-768, -128, 0], [-512, -128, 0], [-512, 0, 0], [-256, 0, 0],
                        [-256, -128, 0], [0, -128, 0]]},
            {'id': 'resource', 'kind': 'resource_approach',
             'points': [[128, 128, 0], [192, 128, 0], [192, -128, 0], [0, -128, 0]]}],
    }
    return bsp, layout, report, features


class ArrivalBindingTests(unittest.TestCase):
    def test_full_audit_accepts_distinct_room_departures_and_same_resource(self):
        result = audit(*fixture())
        self.assertTrue(result['ok'])
        self.assertEqual(len(result['paths']), 4)
        self.assertTrue(all(path['pass'] for path in result['paths']))
        bindings = result['departure_room_bindings']
        self.assertEqual([row['arrival_room'] for row in bindings], [1, 1])
        self.assertEqual([row['destination_room'] for row in bindings], [2, 3])
        self.assertEqual([row['connection'] for row in bindings], [1, 2])
        self.assertEqual(bindings[0]['full_footprint_passage_stations'], [[-408, 0, 0]])

    def test_contact_inside_passage_does_not_complete_departure(self):
        bsp, layout, report, features = fixture()
        features['paths'][0]['points'][-1] = [-408, 0, 0]
        with self.assertRaisesRegex(ValueError, 'finish.*opposite room'):
            audit(bsp, layout, report, features)

    def test_supported_route_beginning_in_other_room_is_not_arrival_departure(self):
        bsp, layout, report, features = fixture()
        features['paths'][0]['points'] = [[-512, 128, 0], [-256, 128, 0], [-256, 0, 0], [-512, 0, 0]]
        with self.assertRaisesRegex(ValueError, 'begin in the arrival room'):
            audit(bsp, layout, report, features)

    def test_geometric_passage_contact_does_not_hide_nonincident_connection(self):
        bsp, layout, report, features = fixture()
        layout['connections'][0].update({'from': 2, 'to': 3})
        with self.assertRaisesRegex(ValueError, 'incident'):
            audit(bsp, layout, report, features)

    def test_two_different_passages_to_same_room_are_not_two_destination_rooms(self):
        bsp, layout, report, features = fixture()
        layout['connections'].append({'id': 3, 'from': 1, 'to': 2,
                                      'volumes': [[[-432, 96, 0], [-384, 224, 192]]]})
        features['paths'][1].update({'exit_connection': 3,
                                    'points': [[128, 128, 0], [128, 192, 0], [-512, 192, 0], [-512, 0, 0]]})
        with self.assertRaisesRegex(ValueError, 'distinct passages and opposite rooms'):
            audit(bsp, layout, report, features)

    def test_actual_world_guard_rejects_non_solid_roof_before_mask_filter(self):
        for contents in (32, 2, 3):
            bsp, layout, report, features = fixture()
            first, count, _ = bsp.brushes[2]
            bsp.brushes[2] = (first, count, contents)
            with self.subTest(contents=contents), self.assertRaisesRegex(ValueError, 'model-zero.*SOLID'):
                audit(bsp, layout, report, features)

    def test_departure_start_requires_full_footprint_in_arrival_room(self):
        _, layout, _, features = fixture()
        features['paths'][0]['points'][0] = [376, 128, 0]
        with self.assertRaisesRegex(ValueError, 'begin in the arrival room'):
            bind_departures(layout, features['paths'], [0, 128, 10])

    def test_destination_center_inside_room_does_not_hide_footprint_overhang(self):
        _, layout, _, features = fixture()
        features['paths'][0]['points'][-1] = [-440, 0, 0]
        with self.assertRaisesRegex(ValueError, 'finish.*opposite room'):
            bind_departures(layout, features['paths'], [0, 128, 10])

    def test_departure_starts_on_arrival_room_ground_floor(self):
        _, layout, _, features = fixture()
        features['paths'][0]['points'][0][2] = 128
        with self.assertRaisesRegex(ValueError, 'begin in the arrival room'):
            bind_departures(layout, features['paths'], [0, 128, 10])

    def test_nearby_resource_endpoint_is_not_the_named_resource(self):
        bsp, layout, report, features = fixture()
        features['paths'][3]['points'][-1] = [16, -128, 0]
        with self.assertRaisesRegex(ValueError, 'same resource'):
            audit(bsp, layout, report, features)

    def test_typed_partition_binding_does_not_replace_compiled_box_identity(self):
        bsp, layout, report, features = fixture()
        changed = [[-96, -32, 0], [80, 0, 192]]
        layout['solids'][0]['bounds'] = copy.deepcopy(changed)
        features['partition']['bounds'] = changed
        with self.assertRaisesRegex(ValueError, 'Compiled partition'):
            audit(bsp, layout, report, features)

    def test_matching_origin_does_not_replace_native_resource_class(self):
        bsp, layout, report, features = fixture()
        features['resource']['classname'] = 'weapon_shotgun'
        with self.assertRaisesRegex(ValueError, 'compiled entity binding'):
            audit(bsp, layout, report, features)


if __name__ == '__main__':
    unittest.main()
