"""Small constructed format/projection controls; no reference geometry embedded."""
import math
import struct
import unittest

from tools.ql_atlas.bsp import QlBsp, FormatError, bezier, clip_z
from tools.ql_atlas.__main__ import liquid_kind, polygon_rings, surface_context
from shapely.geometry import Polygon


def fixture(version=47, refs=(0,1,2), normal=(0,0,1), vertices=None, entity_text=None,
            flags=0, contents=1, patch=False, unused_uv=0):
    lumps=[b'']*(18 if version==47 else 17)
    lumps[0]=entity_text or b'{"classname" "worldspawn" "message" "Synthetic"}\0'
    lumps[1]=struct.pack('<64sii',b'textures/test',flags,contents)
    xyz=vertices or [(0,0,0),(128,0,0),(0,128,0)]
    lumps[10]=b''.join(struct.pack('<10f4B',*p,0,0,unused_uv,unused_uv,*normal,255,255,255,255) for p in xyz)
    lumps[11]=struct.pack('<'+'i'*len(refs),*refs)
    lumps[13]=struct.pack('<12i12f2i',0,-1,2 if patch else 1,0,len(xyz),0,len(refs),-1,0,0,0,0,*([0.]*12),3 if patch else 0,3 if patch else 0)
    lumps[7]=struct.pack('<6f4i',0,0,0,128,128,128,0,1,0,0)
    offset=8+8*len(lumps);header=struct.pack('<4si',b'IBSP',version);body=b''
    for lump in lumps:
        header+=struct.pack('<ii',offset,len(lump));body+=lump;offset+=len(lump)
    return header+body


class QlAtlasTests(unittest.TestCase):
    def test_versions_and_actual_indexed_coordinates(self):
        for version in (46,47):
            bsp=QlBsp(fixture(version));triangles=list(bsp.triangles())
            self.assertEqual(bsp.version,version)
            self.assertEqual(triangles[0]['points'],((0.,0.,0.),(128.,0.,0.),(0.,128.,0.)))

    def test_bad_relative_index_rejected(self):
        with self.assertRaisesRegex(FormatError,'relative draw vertex'):
            QlBsp(fixture(refs=(0,1,3)))

    def test_header_lump_overlap_rejected(self):
        data=bytearray(fixture());struct.pack_into('<ii',data,16,152,72)
        with self.assertRaisesRegex(FormatError,'overlapping'):
            QlBsp(data)

    def test_unused_lightmap_garbage_not_geometry(self):
        self.assertEqual(len(list(QlBsp(fixture(unused_uv=math.nan)).triangles())),1)
        with self.assertRaises(FormatError):
            QlBsp(fixture(vertices=[(math.nan,0,0),(128,0,0),(0,128,0)]))

    def test_downward_sky_and_nodraw_are_excluded(self):
        for kwargs in ({'normal':(0,0,-1)},{'flags':4},{'flags':128}):
            self.assertFalse(list(QlBsp(fixture(**kwargs)).triangles()))

    def test_patch_curve_is_not_control_net(self):
        control=[[(i*64,j*64,64 if i==1 and j==1 else 0) for i in range(3)] for j in range(3)]
        self.assertEqual(bezier(control,.5,.5),(64.,64.,16.))
        bsp=QlBsp(fixture(vertices=[p for row in control for p in row],patch=True,refs=()))
        triangles=list(bsp.triangles());self.assertTrue(triangles)
        self.assertTrue(all(t['patch'] for t in triangles))
        self.assertLessEqual(max(p[2] for t in triangles for p in t['points']),16)

    def test_sloped_band_clipping_preserves_crossing(self):
        clipped=clip_z([(0,0,0),(128,0,256),(0,128,0)],64,128)
        self.assertEqual({p[2] for p in clipped},{64,128})
        self.assertIn((64.,0.,128.),clipped)
        self.assertIn((32.,0.,64.),clipped)

    def test_dissolved_projection_preserves_hole(self):
        shape=Polygon([(0,0),(100,0),(100,100),(0,100)], [[(20,20),(80,20),(80,80),(20,80)]])
        rings=polygon_rings(shape)
        self.assertEqual(len(rings),1);self.assertEqual(len(rings[0]),2)

    def test_mode_filters_are_retained_not_interpreted(self):
        text=b'{"classname" "worldspawn"}{"classname" "item_quad" "origin" "10 20 30" "not_gametype" "1"}'
        points,_=QlBsp(fixture(entity_text=text)).annotations()
        self.assertEqual(points[0]['properties'],{'not_gametype':'1'})

    def test_teleport_direct_target_and_submodel_center(self):
        text=b'{"classname" "worldspawn"}{"classname" "trigger_teleport" "model" "*0" "target" "dest"}{"classname" "target_position" "origin" "256 320 128" "targetname" "dest"}'
        points,links=QlBsp(fixture(entity_text=text)).annotations()
        self.assertEqual(points[0]['origin'],[64,64,64]);self.assertEqual(links[0]['to'],[256,320,128])
        self.assertEqual(links[0]['relation'],'authored target key, not trajectory')

    def test_liquid_identity_uses_contents_not_name(self):
        self.assertEqual([liquid_kind(x) for x in [0,8,16,32]],['surface','lava','slime','water'])

    def test_visual_context_keeps_authored_origin_and_limited_scope(self):
        origin=[32,32,136]
        triangle={'surface':3,'points':[(0,0,121),(128,0,121),(0,128,121)],'normal':(0,0,1)}
        result=surface_context(origin,[triangle])
        self.assertEqual(result['band'],0);self.assertEqual(result['vertical_gap'],15)
        self.assertEqual(origin,[32,32,136])
        self.assertIn('not hull support',result['basis'])
        self.assertEqual(surface_context([32,32,300],[triangle])['band'],2)


if __name__=='__main__':unittest.main()
