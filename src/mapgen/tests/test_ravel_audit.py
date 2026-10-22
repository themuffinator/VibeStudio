"""Independent synthetic support and collision counterexamples for bent routes."""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.map_recipes.geometry import GeometryError, convex_polyhedron
from tools.map_recipes.ravel_audit import _flat_route, _crossing


def box(low,high,index=0):
    planes=[(1,0,0,high[0]),(-1,0,0,-low[0]),(0,1,0,high[1]),
            (0,-1,0,-low[1]),(0,0,1,high[2]),(0,0,-1,-low[2])]
    return index,1,convex_polyhedron(planes)


def counts():return {'world_pair_checks':0,'exact_swept_checks':0}


class RavelAuditTests(unittest.TestCase):
    def test_coplanar_union_crosses_actual_floor_seam(self):
        world=[box((-64,-32,-24),(0,32,0)),box((0,-32,-24),(64,32,0),1)]
        result=_flat_route(world,[(-48,0,0),(48,0,0)],'seam',counts())
        self.assertEqual(result['legs'][0]['support_brushes'],[0,1])
        self.assertEqual(result['legs'][0]['covered_partition_cells'],2)
        self.assertTrue(result['legs'][0]['continuous_forward_and_reverse_clearance'])

    def test_endpoint_support_does_not_hide_one_unit_floor_gap(self):
        world=[box((-64,-32,-24),(0,32,0)),box((1,-32,-24),(64,32,0),1)]
        with self.assertRaisesRegex(GeometryError,'full-footprint floor support'):
            _flat_route(world,[(-48,0,0),(48,0,0)],'gap',counts())

    def test_center_support_is_not_full_width_support(self):
        world=[box((-64,-15,-24),(64,15,0))]
        with self.assertRaisesRegex(GeometryError,'full-footprint floor support'):
            _flat_route(world,[(-48,0,0),(48,0,0)],'narrow',counts())

    def test_elbow_union_includes_corner_footprint(self):
        world=[box((-32,-96,-24),(32,32,0)),box((32,-32,-24),(96,32,0),1)]
        result=_flat_route(world,[(0,-80,0),(0,0,0),(80,0,0)],'elbow',counts())
        self.assertEqual(len(result['legs']),2)
        self.assertEqual(result['axis_distance'],160)

    def test_missing_elbow_cap_is_rejected(self):
        world=[box((-32,-96,-24),(32,0,0)),box((32,-32,-24),(96,32,0),1)]
        with self.assertRaisesRegex(GeometryError,'full-footprint floor support'):
            _flat_route(world,[(0,-80,0),(0,0,0),(80,0,0)],'elbow',counts())

    def test_between_endpoint_obstacle_is_continuously_swept(self):
        world=[box((-96,-32,-24),(96,32,0)),box((-1,-8,24),(1,8,40),1)]
        with self.assertRaisesRegex(GeometryError,'continuous standing-hull sweep'):
            _flat_route(world,[(-64,0,0),(64,0,0)],'obstacle',counts())

    def test_slanted_support_cannot_be_inferred_from_aggregate_bounds(self):
        shape=convex_polyhedron([(1,0,0,64),(-1,0,0,64),(0,1,0,32),(0,-1,0,32),
                                 (0,0,1,0),(0,0,-1,24),(1,1,0,16)])
        with self.assertRaisesRegex(GeometryError,'full-footprint floor support'):
            _flat_route([(0,1,shape)],[(-48,0,0),(48,0,0)],'chamfer',counts())

    def test_route_input_and_partition_caps_are_explicit(self):
        world=[box((-96,-96,-24),(96,96,0))]
        for points in ([(0,0,0)],[(0,0,0)]*33,[(0,0,0),(16,16,0)],
                       [(0,0,0),(0,0,0)],[(0,0,0),(16,0,1)],[(0,0,0),(True,0,0)]):
            with self.subTest(points=points),self.assertRaises(GeometryError):
                _flat_route(world,points,'invalid',counts())
        with patch('tools.map_recipes.ravel_audit.MAX_FLOOR_CELLS',0),self.assertRaisesRegex(GeometryError,'cell budget'):
            _flat_route(world,[(0,0,0),(16,0,0)],'budget',counts())

    def crossing(self):
        return [box((-512,-720,-24),(512,80,0)),box((-240,-400,104),(320,-240,128),1)]

    def test_both_crossing_levels_have_independent_full_hulls(self):
        result=_crossing(self.crossing(),counts())
        self.assertEqual(result['shared_xy'],[32,-320])
        self.assertEqual(result['lower_clearance_above_hull'],48)
        self.assertEqual(result['lower']['legs'][0]['support_brushes'],[0])
        self.assertEqual(result['upper']['legs'][0]['support_brushes'],[1])

    def test_missing_lower_floor_is_not_implied_by_upper_floor(self):
        with self.assertRaisesRegex(GeometryError,'lower Crossing route lacks'):
            _crossing(self.crossing()[1:],counts())

    def test_low_blocker_under_crossing_is_rejected(self):
        world=self.crossing()+[box((24,-336,0),(40,-304,72),2)]
        with self.assertRaisesRegex(GeometryError,'lower Crossing route.*standing-hull sweep'):
            _crossing(world,counts())

    def test_actual_crosswalk_thickness_is_bound(self):
        world=[self.crossing()[0],box((-240,-400,96),(320,-240,128),1)]
        with self.assertRaisesRegex(GeometryError,'24-thick deck'):
            _crossing(world,counts())


if __name__=='__main__':unittest.main()
