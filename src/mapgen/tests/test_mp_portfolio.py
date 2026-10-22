"""Controls for compiled anchor clearance and geometric settling candidates."""
import unittest
from types import SimpleNamespace
from unittest.mock import patch
from tools.mp_portfolio.compare import graph_signature
from tools.map_recipes.geometry import convex_polyhedron
from tools.mp_portfolio.inspect import supported_anchor, validated_world, validate_report


def box(low, high):
    return convex_polyhedron([(1,0,0,high[0]),(-1,0,0,-low[0]),
                              (0,1,0,high[1]),(0,-1,0,-low[1]),
                              (0,0,1,high[2]),(0,0,-1,-low[2])])


class PortfolioAnchorTests(unittest.TestCase):
    def test_authored_airborne_spawn_retains_geometric_scope(self):
        world = [(0,1,box((-128,-128,-64),(128,128,0)))]
        result = supported_anchor(world,[0,0,32])
        self.assertEqual(result['supported_feet'],[0,0,0])
        self.assertFalse(result['native_settlement_tested'])

    def test_clear_entity_point_does_not_hide_hull_overlap(self):
        world = [(0,1,box((-128,-128,-64),(128,128,0))),
                 (1,1,box((10,-32,0),(48,32,16)))]
        with self.assertRaisesRegex(ValueError,'Authored anchor hull blocked'):
            supported_anchor(world,[0,0,24])

    def test_center_support_does_not_hide_footprint_overhang(self):
        world = [(0,1,box((-8,-128,-64),(8,128,0)))]
        with self.assertRaisesRegex(ValueError,'No full-footprint support'):
            supported_anchor(world,[0,0,24])

    def test_distant_floor_does_not_widen_settling_contract(self):
        world = [(0,1,box((-128,-128,-160),(128,128,-96)))]
        with self.assertRaisesRegex(ValueError,'No full-footprint support'):
            supported_anchor(world,[0,0,24])


class PortfolioVarietyTests(unittest.TestCase):
    def layout(self):
        return {'rooms':[{'id':i,'tags':['raised_crosswalk' if i == 1 else 'offset_barrier']} for i in (1,2,3,4)],
                'connections':[{'from':a,'to':b} for a,b in ((1,2),(2,3),(3,4),(4,1))]}

    def test_ids_names_order_and_coordinates_do_not_create_new_topology(self):
        layout = self.layout()
        renamed = {'rooms':[dict(room,id=room['id']+10,name='different',bounds=[[1,2,3],[4,5,6]]) for room in reversed(layout['rooms'])],
                   'connections':[{'from':c['to']+10,'to':c['from']+10} for c in reversed(layout['connections'])]}
        self.assertEqual(graph_signature(layout),graph_signature(renamed))
        self.assertEqual(graph_signature(layout,colored=True),graph_signature(renamed,colored=True))

    def test_additional_physical_passage_changes_multigraph(self):
        a,b = self.layout(),self.layout()
        b['connections'].append({'from':1,'to':2})
        self.assertNotEqual(graph_signature(a),graph_signature(b))

    def test_motif_labels_are_separate_from_unlabelled_topology(self):
        a,b = self.layout(),self.layout()
        b['rooms'][0]['tags']=['horseshoe_gallery']
        self.assertEqual(graph_signature(a),graph_signature(b))
        self.assertNotEqual(graph_signature(a,colored=True),graph_signature(b,colored=True))

    def test_opposed_bridge_does_not_collapse_into_unknown_room_class(self):
        a,b = self.layout(),self.layout()
        a['rooms'][0]['tags']=['opposed_stair_bridge']
        b['rooms'][0]['tags']=['unknown_future_motif']
        self.assertEqual(graph_signature(a),graph_signature(b))
        self.assertNotEqual(graph_signature(a,colored=True),graph_signature(b,colored=True))


class PortfolioGuardTests(unittest.TestCase):
    def test_nonblocking_liquid_cannot_disappear_before_architecture_gate(self):
        bsp = SimpleNamespace(brushes=[(0,6,1),(6,6,32)])
        with patch('tools.mp_portfolio.inspect.model_brushes',return_value=[0,1]), \
             patch('tools.mp_portfolio.inspect._world',return_value=[(0,1,'filtered-solid')]) as filtered:
            with self.assertRaisesRegex(ValueError,'every model-zero brush'):
                validated_world(bsp)
            filtered.assert_not_called()

    def test_structural_success_cannot_promote_failed_or_unavailable_navigation(self):
        layout = {'layout_sha256':'a'*64}
        for available,ok in ((True,False),(False,True)):
            report = {'layout_sha256':'a'*64,'validation':{'ok':True},'analysis':{'navigation':{
                'player':{'available':available,'ok':ok,'layout_sha256':'a'*64}}}}
            with self.subTest(available=available,ok=ok),self.assertRaisesRegex(ValueError,'player navigation'):
                validate_report(layout,report)


if __name__ == '__main__':
    unittest.main()
