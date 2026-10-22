import unittest

from tools.map_recipes.packing_audit import packing, analyze_layout


class PackingTests(unittest.TestCase):
    def test_overlap_is_not_double_counted(self):
        result = packing([(0,0,10,10),(5,0,15,10)])
        self.assertEqual(result['projected_volume_union_area'],150)
        self.assertEqual(result['envelope_coverage_fraction'],1)

    def test_ring_distinguishes_internal_hole(self):
        result = packing([(0,0,10,2),(0,8,10,10),(0,2,2,8),(8,2,10,8)])
        self.assertEqual(result['enclosed_uncovered_area'],36)
        self.assertEqual(result['edge_connected_uncovered_area'],0)
        self.assertEqual(result['projected_volume_union_area'],64)

    def test_horseshoe_has_exterior_opening(self):
        result = packing([(0,0,10,2),(0,8,10,10),(8,2,10,8)])
        self.assertEqual(result['enclosed_uncovered_area'],0)
        self.assertEqual(result['edge_connected_uncovered_area'],48)

    def test_gap_connection_needs_edge(self):
        # Interior hole touches the southwest exterior gap only at a corner.
        result = packing([(1,0,3,1),(0,1,1,3),(2,1,3,3),(1,2,2,3)])
        self.assertEqual(result['enclosed_uncovered_area'],1)
        self.assertEqual(result['edge_connected_uncovered_area'],1)

    def test_translation_and_duplication_do_not_change_area(self):
        boxes = [(-5,-4,7,2),(0,-2,9,5)]
        result = packing(boxes)
        moved = packing([(a+200,b-100,c+200,d-100) for a,b,c,d in boxes*2])
        for key in ['envelope_area','projected_volume_union_area','uncovered_area']:
            self.assertEqual(result[key],moved[key])

    def test_internal_solids_do_not_pretend_to_be_playable_coverage(self):
        layout = {'rooms':[{'bounds':[[0,0,0],[10,10,100]]}], 'connections':[],
                  'solids':[{'bounds':[[0,0,0],[9,10,100]]}]}
        self.assertEqual(analyze_layout(layout)['projected_volume_union_area'],100)

    def test_invalid_inputs_rejected(self):
        for boxes in [[],[(0,0,0,1)],[(0,0,1,float('nan'))],[(False,0,1,1)]]:
            with self.assertRaises(ValueError):
                packing(boxes)


if __name__ == '__main__':
    unittest.main()
