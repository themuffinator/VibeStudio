import unittest
from tools.mp_portfolio.topology_compare import full_span_room_gaps, room_graph, validate_trace


def graph(n, edges):
    return {'rooms': [{'id': i} for i in range(n)], 'connections': [{'from': a, 'to': b} for a, b in edges]}


class TopologyControls(unittest.TestCase):
    def test_same_seed_and_family_cannot_hide_changed_trace_geometry(self):
        current = {'candidate_seed': 7, 'family': 'pinwheel', 'layout_sha256': 'new'}
        trace = {'pipeline_selected_seed': 7, 'pipeline_layout_sha256': 'old', 'candidate_count': 6,
                 'candidates': [{'selected': i == 0, 'seed': 7+i, 'family': 'pinwheel'} for i in range(6)]}
        with self.assertRaisesRegex(ValueError, 'Candidate trace'):
            validate_trace(trace, current)
        trace['pipeline_layout_sha256'] = 'new'
        validate_trace(trace, current)

    def test_star_hub_is_mandatory_and_has_unit_centrality(self):
        result = room_graph(graph(5, [(0, i) for i in range(1, 5)]))
        self.assertEqual(result['room_removal_unreachable_remaining_pairs'][0], 6)
        self.assertEqual(result['shortest_room_path_betweenness'][0], 1)
        self.assertEqual(result['maximum_room_hops'], 2)

    def test_outer_loop_survives_hub_removal(self):
        result = room_graph(graph(5, [(0, i) for i in range(1, 5)]+[(1,2),(2,3),(3,4),(4,1)]))
        self.assertEqual(result['room_removal_unreachable_remaining_pairs'][0], 0)
        self.assertAlmostEqual(result['shortest_room_path_betweenness'][0], 1/9)

    def test_parallel_door_does_not_double_shortest_room_path_credit(self):
        first = room_graph(graph(3, [(0,1),(1,2)]))
        second = room_graph(graph(3, [(0,1),(0,1),(1,2)]))
        self.assertEqual(first['shortest_room_path_betweenness'], second['shortest_room_path_betweenness'])
        self.assertNotEqual(first['canonical_multigraph'], second['canonical_multigraph'])
        self.assertEqual(second['cycle_rank'], 1)

    def test_tied_ring_paths_share_credit(self):
        result = room_graph(graph(4, [(0,1),(1,2),(2,3),(3,0)]))
        for value in result['shortest_room_path_betweenness'].values():
            self.assertAlmostEqual(value, 1/6)

    def test_disconnected_graph_is_not_silently_scored(self):
        with self.assertRaisesRegex(ValueError, 'Disconnected'):
            room_graph(graph(3, [(0,1)]))

    def test_room_only_slicing_gap_excludes_local_seams(self):
        rectangles = [(0,0,20,10),(21,0,31,20),(11,21,31,31),(0,11,10,31),(11,11,20,20)]
        rooms = [{'bounds': [[a,b,0],[c,d,100]]} for a,b,c,d in rectangles]
        self.assertEqual(full_span_room_gaps({'rooms': rooms}), {'x': [], 'y': []})
        rows = {'rooms': [{'bounds': [[0,0,0],[20,10,100]]}, {'bounds': [[0,11,0],[20,21,100]]}]}
        self.assertEqual(full_span_room_gaps(rows), {'x': [], 'y': [[10,11]]})

    def test_split_pinwheel_arm_can_reintroduce_global_slice(self):
        # Reconstructed minimum-band counterexample, not an observed random seed.
        rectangles = [(688,688,1328,1328), (0,0,640,640), (688,0,1328,640),
                      (1376,0,2016,1328), (688,1376,2016,2016), (0,688,640,2016)]
        rooms = [{'bounds': [[a,b,0],[c,d,512]]} for a,b,c,d in rectangles]
        self.assertEqual(full_span_room_gaps({'rooms': rooms}), {'x': [[640,688]], 'y': []})


if __name__ == '__main__':
    unittest.main()
