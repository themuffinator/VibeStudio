import unittest

from tools.map_study.ranking_features import map_box
from tools.mp_portfolio.upper_link_controls import variants, world_brushes


def fixture():
    slab = map_box([400,600,104],[448,760,128])
    return '{\n"classname" "worldspawn"\n"mapversion" "220"\n'+slab+map_box([0,0,-32],[1000,1000,0])+'}\n'+\
        '{\n"classname" "info_player_deathmatch"\n"origin" "32 32 24"\n"message" "quoted { brace } stays"\n}\n'


class UpperLinkControls(unittest.TestCase):
    def test_brush_only_controls_preserve_nonworld_source_bytes(self):
        source = fixture()
        values,record = variants(source,[[400,600,128],[448,760,320]])
        before,_,tail = world_brushes(source)
        for kind,count in [('closed',3),('missing-floor',1)]:
            after,_,other_tail = world_brushes(values[kind])
            self.assertEqual(len(after),count)
            self.assertEqual(source[tail:],values[kind][other_tail:])
        self.assertEqual(record['blocker'],[[416,600,128],[432,760,320]])
        self.assertEqual(len(before),2)

    def test_missing_or_duplicate_support_slab_rejected(self):
        source = fixture()
        slab = map_box([400,600,104],[448,760,128])
        for value in [source.replace(slab,''),source.replace(slab,slab+slab)]:
            with self.assertRaisesRegex(ValueError,'one exact explicit'):
                variants(value,[[400,600,128],[448,760,320]])

    def test_unsupported_geometry_or_malformed_world_rejected(self):
        with self.assertRaises(ValueError):
            variants(fixture(),[[400,600,0],[448,760,192]])
        with self.assertRaises(ValueError):
            variants(fixture(),[[400,600,128],[449,760,320]])
        with self.assertRaisesRegex(ValueError,'Unclosed'):
            world_brushes(fixture().split('}\n{\n"classname" "info_player')[0])


if __name__ == '__main__':
    unittest.main()
