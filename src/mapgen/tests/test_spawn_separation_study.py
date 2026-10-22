import unittest

from tools.map_study.spawn_separation import spawn_metrics


def points(*origins):
    return [{'entity': i, 'origin': list(origin)} for i, origin in enumerate(origins)]


class SpawnGeometryControls(unittest.TestCase):
    def test_warning_boundary_and_directed_count(self):
        data = spawn_metrics(points((0, 0, 24), (256, 0, 24), (0, 192, 24)), lambda a, b: 1)
        self.assertEqual(data['unordered_pair_count'], 3)
        self.assertEqual(data['close_unordered_pairs'], 1)
        self.assertEqual(data['equivalent_directed_distance_warnings'], 2)

    def test_vertical_distance_is_distinct_from_occlusion(self):
        data = spawn_metrics(points((0, 0, 24), (64, 0, 216)), lambda a, b: 1 if a == b else 0)
        self.assertEqual(data['close_unordered_pairs'], 1)
        self.assertEqual(data['world_eye_visible_pairs'], 0)
        self.assertEqual(data['pairs'][0]['height_difference'], 192)

    def test_invalid_eye_is_unknown_not_occluded(self):
        data = spawn_metrics(points((0, 0, 24), (512, 0, 24)), lambda a, b: 0 if a == b else 1)
        self.assertEqual(data['invalid_eye_entities'], [0, 1])
        self.assertIsNone(data['world_eye_visible_fraction'])
        self.assertIsNone(data['pairs'][0]['world_eye_visible'])

    def test_authored_eye_offset(self):
        queries = []
        spawn_metrics(points((0, 0, 14), (512, 0, 206)), lambda a, b: queries.append((a, b)) or 1)
        self.assertEqual(queries[0], ([0, 0, 36], [0, 0, 36]))
        self.assertEqual(queries[1], ([512, 0, 228], [512, 0, 228]))

    def test_duplicate_spawn_identity_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            spawn_metrics([{'entity': 1, 'origin': [0, 0, 24]}] * 2, lambda a, b: 1)

    def test_nonfinite_origin_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Invalid authored'):
            spawn_metrics(points((0, 0, 24), (float('nan'), 0, 24)), lambda a, b: 1)


if __name__ == '__main__':
    unittest.main()
