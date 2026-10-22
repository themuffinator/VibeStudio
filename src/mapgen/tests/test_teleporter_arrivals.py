"""Original controls for the bounded teleporter reference certificate."""
import unittest
from types import SimpleNamespace

from tools.map_study.teleporter_arrivals import (
    arrival_feet, box, box_ray_fraction, facing_end, leg,
    linked_destination, mover_envelopes, support,
)


class TeleporterArrivalControls(unittest.TestCase):
    def setUp(self):
        self.floor = {'world:0': box((-128,-128,-16),(128,128,0))}

    def test_stock_arrival_offset_is_not_authored_floor(self):
        self.assertEqual(arrival_feet({'origin':'12 34 210'}), [12,34,196])

    def test_target_ambiguity_rejected(self):
        entities = [{'classname':'misc_teleporter','target':'a'},
                    {'classname':'misc_teleporter_dest','targetname':'a'}]
        self.assertEqual(linked_destination(entities,0,1),entities[1])
        with self.assertRaises(ValueError):
            linked_destination(entities+[entities[1]],0,1)

    def test_full_footprint_not_centerline(self):
        narrow = {'world:0':box((-128,-8,-16),(128,8,0))}
        self.assertFalse(leg(narrow,{}, {},[],[-64,0,0],[64,0,0])['pass'])

    def test_shallow_settle_and_floor_contact(self):
        self.assertTrue(leg(self.floor,{}, {},[],[0,0,2],[0,0,0])['pass'])
        self.assertFalse(leg(self.floor,{}, {},[],[0,0,2],[0,0,2])['pass'])

    def test_four_unit_departure_is_separate_band(self):
        cap = {'pad:1':box((-32,-32,-4),(32,32,4))}
        self.assertTrue(leg(self.floor,cap,{},[],[0,0,4],[64,0,4],4)['pass'])
        self.assertFalse(leg(self.floor,cap,{},[],[0,0,4],[64,0,4])['pass'])
        self.assertTrue(leg(self.floor,cap,{},[],[64,0,4],[64,0,0])['pass'])

    def test_band_does_not_bridge_missing_ground(self):
        separated = {'world:0':box((-128,-32,-16),(-16,32,0)),
                     'world:1':box((16,-32,-16),(128,32,0))}
        self.assertFalse(support(separated,[-64,0,4],[64,0,4],4)['pass'])

    def test_pad_and_ceiling_are_real_blockers(self):
        pad = {'pad:1':box((-16,-16,0),(16,16,8))}
        self.assertFalse(leg(self.floor,pad,{},[],[-64,0,0],[64,0,0])['pass'])
        ceiling = {**self.floor,'world:1':box((-32,-32,55),(32,32,64))}
        self.assertFalse(leg(ceiling,{}, {},[],[-64,0,0],[64,0,0])['pass'])

    def test_mover_and_liquid_exclusions(self):
        env = [{'entity':2,'bounds':[(-32,-32,0),(32,32,64)]}]
        self.assertFalse(leg(self.floor,{}, {},env,[-64,0,0],[64,0,0])['pass'])
        wet = {'liquid:1':box((-32,-32,-8),(32,32,2))}
        self.assertFalse(leg(self.floor,{},wet,[],[-64,0,0],[64,0,0])['pass'])

    def test_diagonal_and_undeclared_settle_rejected(self):
        with self.assertRaises(ValueError):
            support(self.floor,[-64,0,0],[64,64,0])
        with self.assertRaises(ValueError):
            leg(self.floor,{}, {},[],[0,0,64],[0,0,0])

    def test_rotating_envelope_uses_local_origin(self):
        bsp = SimpleNamespace(entities=[{'classname':'func_door_rotating','model':'*1','origin':'100 200 300'}],
            models=[None,(-4,-32,-32,4,32,32)])
        self.assertEqual(mover_envelopes(bsp)[0]['bounds'],[[32,132,232],[168,268,368]])

    def test_real_yaw_not_cardinal_rounding(self):
        end = facing_end([0,0,0],273,256)
        self.assertGreater(end[0],13)
        self.assertLess(end[1],-255)
        self.assertAlmostEqual(facing_end([0,0,0],180,256)[0],-256)

    def test_ray_blocker_and_clear_ray(self):
        bounds = ((32,-16,-16),(64,16,16))
        self.assertEqual(box_ray_fraction([0,0,0],[128,0,0],bounds),0.25)
        self.assertEqual(box_ray_fraction([0,32,0],[128,32,0],bounds),1)


if __name__ == '__main__':
    unittest.main()
