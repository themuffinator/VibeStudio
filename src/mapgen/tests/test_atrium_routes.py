"""Original exact-geometry controls for the new bounded atrium study."""
import unittest
from tools.map_study.atrium_routes import box, paired_leg, step_pair
from tools.map_study.raised_resource_circuit import static_leg, clearance_leg
from tools.map_recipes.geometry import convex_polyhedron


class AtriumRouteControls(unittest.TestCase):
    def setUp(self):
        self.lower={0:box((-96,-64,-16),(96,64,0))}
        self.step={**self.lower,1:box((0,-32,0),(96,32,16))}

    def test_bidirectional_supported_step(self):
        r=step_pair(self.step,{},[],[-16,0,0],[16,0,16])
        self.assertTrue(r['forward_pass'] and r['reverse_pass'])
        self.assertFalse(r['native_step_or_continuous_grounded_motion_certified'])

    def test_clearance_at_step_raise_matters(self):
        world={**self.step,2:box((-32,-32,70),(0,32,90))}
        self.assertFalse(step_pair(world,{},[],[-16,0,0],[16,0,16])['pass'])

    def test_missing_ground_between_clear_endpoints(self):
        world={0:box((-96,-32,-16),(-16,32,0)),1:box((16,-32,-16),(96,32,0))}
        self.assertTrue(static_leg(world,{},[],[-48,0,0],[-48,0,0])['pass'])
        self.assertTrue(static_leg(world,{},[],[48,0,0],[48,0,0])['pass'])
        self.assertFalse(paired_leg(world,{},[],[-48,0,0],[48,0,0])['pass'])

    def test_full_width_not_centerline(self):
        world={0:box((-96,-8,-16),(96,8,0))}
        self.assertFalse(paired_leg(world,{},[],[-48,0,0],[48,0,0])['pass'])

    def test_actual_convex_planes_not_bounds(self):
        solid=convex_polyhedron([(1,0,0,64),(-1,0,0,0),(0,1,0,64),(0,-1,0,0),
                                (0,0,1,0),(0,0,-1,16),(1,1,0,64)])
        self.assertFalse(static_leg({0:solid},{},[],[48,48,0],[48,48,0])['pass'])

    def test_stacked_paths_do_not_create_vertical_edge(self):
        world={**self.lower,1:box((-96,-32,112),(96,32,128))}
        self.assertTrue(paired_leg(world,{},[],[-48,0,0],[48,0,0])['pass'])
        self.assertTrue(paired_leg(world,{},[],[-48,0,128],[48,0,128])['pass'])
        self.assertFalse(clearance_leg(world,{},[],[0,0,0],[0,0,128])['clear'])

    def test_upper_floor_not_lower_support(self):
        world={1:box((-96,-32,112),(96,32,128))}
        self.assertFalse(paired_leg(world,{},[],[-48,0,0],[48,0,0])['pass'])

    def test_projection_can_hide_standing_blocker(self):
        world={**self.lower,1:box((-8,-32,0),(8,32,80))}
        self.assertFalse(paired_leg(world,{},[],[-48,0,0],[48,0,0])['pass'])

    def test_mover_and_liquid_are_not_ordinary_floor(self):
        env=[{'entity':3,'bounds':[(-8,-32,0),(8,32,80)]}]
        self.assertFalse(paired_leg(self.lower,{},env,[-48,0,0],[48,0,0])['pass'])
        wet={7:box((-8,-32,-4),(8,32,4))}
        self.assertFalse(paired_leg(self.lower,wet,[],[-48,0,0],[48,0,0])['pass'])

    def test_unfrozen_step_size_or_diagonal_rejected(self):
        for b in ([16,0,32],[16,16,16]):
            with self.assertRaises(ValueError):step_pair(self.step,{},[],[-16,0,0],b)


if __name__=='__main__':unittest.main()
