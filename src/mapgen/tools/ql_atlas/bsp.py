"""Independent bounded IBSP46/47 draw-surface reader.

Format facts were checked against NetRadiant Custom bspfile_ibsp.cpp and
Quake3e qfiles.h/surfaceflags.h. No external implementation is incorporated.
This reads authored rendering data, not a movement or visibility certificate.
"""
from __future__ import annotations

from collections import Counter
import math
import struct

from tools.map_study.binary import (FormatError, Lumps, check_index, check_range,
                                    entities, sha256, vec, MAX_FILE_BYTES)

MAX_TRIANGLES = 300_000
PATCH_STEPS = 6
LIQUID_MASK = 8 | 16 | 32
MODE_KEYS = ("gametype", "not_gametype", "notfree", "notteam", "notsingle",
             "spawnflags", "targetname", "target", "count", "wait", "random")


def bezier(control, u, v):
    """Evaluate an actual 3x3 tensor quadratic patch; no flat control-net fill."""
    bu = ((1-u)**2, 2*u*(1-u), u*u)
    bv = ((1-v)**2, 2*v*(1-v), v*v)
    return tuple(sum(control[j][i][axis] * bu[i] * bv[j]
                     for j in range(3) for i in range(3)) for axis in range(3))


def triangle_normal(points):
    a, b, c = points
    u, v = [b[i]-a[i] for i in range(3)], [c[i]-a[i] for i in range(3)]
    cross = (u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0])
    length = math.sqrt(sum(x*x for x in cross))
    return tuple(x/length for x in cross) if length else (0., 0., 0.)


def clip_z(points, low, high):
    """Clip a real planar triangle to a closed height slab, preserving slopes."""
    result = list(points)
    for boundary, sign in ((low, 1), (high, -1)):
        output = []
        for a, b in zip(result, result[1:]+result[:1]):
            da, db = sign*(a[2]-boundary), sign*(b[2]-boundary)
            if da >= 0:
                output.append(a)
            if (da < 0) != (db < 0):
                t = da/(da-db)
                output.append(tuple(a[i]+t*(b[i]-a[i]) for i in range(3)))
        result = output
        if not result:
            break
    return result


class QlBsp(Lumps):
    def __init__(self, data):
        if len(data) < 144 or len(data) > MAX_FILE_BYTES:
            raise FormatError("invalid bounded Q3/QL BSP size")
        magic, self.version = struct.unpack_from("<4si", data)
        if magic != b"IBSP" or self.version not in (46, 47):
            raise FormatError("expected IBSP46 or IBSP47")
        count = 18 if self.version == 47 else 17
        if len(data) < 8+count*8:
            raise FormatError("truncated BSP header")
        super().__init__(data, data, 8, count, 8+count*8)
        self.sha256 = sha256(data)
        self.issues = []
        self.entities = entities(self.lumps[0], self.issues)
        self.shaders = self.records(1, "<64sii")
        self.planes = self.records(2, "<4f")
        self.models = self.records(7, "<6f4i")
        self.brushes = self.records(8, "<3i")
        self.sides = self.records(9, "<2i")
        # Some local QL vertices carry uninitialized unused lightmap UV bytes.
        # Validate XYZ/normals; do not interpret or export texture/lighting data.
        self.vertices = self.records(10, "<3f16x3f4B")
        self.indices = [r[0] for r in self.records(11, "<i")]
        self.surfaces = self.records(13, "<12i12f2i")
        if not self.models or not self.vertices or not self.surfaces:
            raise FormatError("missing render surfaces/model")
        if len(self.surfaces) > 100_000 or len(self.vertices) > 500_000:
            raise FormatError("atlas surface/vertex budget exceeded")
        for model in self.models:
            if any(model[i] > model[i+3] for i in range(3)):
                raise FormatError("inverted model bounds")
            check_range(model[6], model[7], len(self.surfaces), "model surfaces")
            check_range(model[8], model[9], len(self.brushes), "model brushes")
        for first, count, shader in self.brushes:
            check_range(first, count, len(self.sides), "brush sides")
            check_index(shader, len(self.shaders), "brush shader")
        for plane, shader in self.sides:
            check_index(plane, len(self.planes), "brush plane")
            check_index(shader, len(self.shaders), "side shader")
        for face in self.surfaces:
            check_index(face[0], len(self.shaders), "surface shader")
            check_range(face[3], face[4], len(self.vertices), "surface vertices")
            check_range(face[5], face[6], len(self.indices), "surface indices")
            if face[2] not in (1, 2, 3, 4):
                raise FormatError("unknown surface type")
            if face[2] in (1, 3):
                if face[6] % 3:
                    raise FormatError("non-triangle draw indices")
                for i in self.indices[face[5]:face[5]+face[6]]:
                    check_index(i, face[4], "relative draw vertex")
            if face[2] == 2:
                w, h = face[-2:]
                if min(w,h)<3 or max(w,h)>129 or not w%2 or not h%2 or w*h!=face[4]:
                    raise FormatError("invalid quadratic patch dimensions")

    def triangles(self, model=0):
        """Yield actual indexed triangles and bounded tessellated patch samples."""
        m = self.models[model]
        emitted = 0
        for face_id in range(m[6], m[6]+m[7]):
            face = self.surfaces[face_id]
            shader = self.shaders[face[0]]
            if shader[1] & (4 | 128):  # sky / nodraw are not architectural floor diagrams
                continue
            verts = self.vertices[face[3]:face[3]+face[4]]
            tris = []
            if face[2] in (1,3):
                refs = self.indices[face[5]:face[5]+face[6]]
                tris = [tuple(verts[i][:3] for i in refs[k:k+3]) for k in range(0,len(refs),3)]
            elif face[2] == 2:
                w,h = face[-2:]
                for row in range(0,h-2,2):
                    for col in range(0,w-2,2):
                        control = [[verts[(row+j)*w+col+i][:3] for i in range(3)] for j in range(3)]
                        grid = [[bezier(control, i/PATCH_STEPS, j/PATCH_STEPS)
                                 for i in range(PATCH_STEPS+1)] for j in range(PATCH_STEPS+1)]
                        for j in range(PATCH_STEPS):
                            for i in range(PATCH_STEPS):
                                a,b,c,d=grid[j][i],grid[j][i+1],grid[j+1][i+1],grid[j+1][i]
                                tris.extend(((a,b,c),(a,c,d)))
            normal_hint = tuple(sum(v[3+axis] for v in verts)/max(len(verts),1) for axis in range(3))
            for points in tris:
                emitted += 1
                if emitted > MAX_TRIANGLES:
                    raise FormatError("tessellated triangle budget exceeded")
                normal = triangle_normal(points)
                if sum(normal[i]*normal_hint[i] for i in range(3)) < 0:
                    points = tuple(reversed(points))
                    normal = tuple(-x for x in normal)
                if normal[2] >= .7:
                    yield {"surface":face_id,"points":points,"normal":normal,
                           "patch":face[2]==2,"shader":shader[0].split(b'\0')[0].decode('utf-8','replace'),
                           "contents":shader[2],"flags":shader[1]}

    def annotations(self):
        """Preserve authored all-mode entities and explicit one-hop target links."""
        points, links = [], []
        targets = {}
        for index,e in enumerate(self.entities):
            if e.get('targetname'):
                targets.setdefault(e['targetname'],[]).append(index)
        for index,e in enumerate(self.entities):
            cls=e.get('classname','')
            kind = ('weapon' if cls.startswith('weapon_') else
                    'item' if cls.startswith('item_') else
                    'spawn' if cls=='info_player_deathmatch' else
                    'teleport' if cls=='trigger_teleport' else
                    'push' if cls=='trigger_push' else None)
            if not kind:
                continue
            origin = vec(e['origin']) if 'origin' in e else None
            bounds = None
            if kind in ('teleport','push') and re_model(e.get('model','')) is not None:
                model = re_model(e['model'])
                check_index(model,len(self.models),'entity model')
                m=self.models[model]
                # Model-zero-origin authored bounds; any explicit entity transform is retained.
                offset=origin or (0.,0.,0.)
                bounds=[[m[i+3*end]+offset[i] for i in range(3)] for end in (0,1)]
                origin=tuple((bounds[0][i]+bounds[1][i])/2 for i in range(3))
            if origin is None:
                self.issues.append(f"entity {index} {cls}: no authored origin/model; annotation omitted")
                continue
            point={"id":index,"class":cls,"kind":kind,"origin":list(origin),
                   "properties":{key:e[key] for key in MODE_KEYS if key in e}}
            if bounds:
                point['bounds']=bounds
                point['origin_basis']='submodel AABB center, not trigger contact point'
            if 'angle' in e: point['angle']=e['angle']
            points.append(point)
            if kind in ('teleport','push'):
                for target in targets.get(e.get('target',''),[]):
                    dest=self.entities[target]
                    if 'origin' not in dest:
                        self.issues.append(f"entity {index}: linked entity {target} has no point origin")
                        continue
                    links.append({"source":index,"target_entity":target,"kind":kind,
                                  "from":list(origin),"to":list(vec(dest['origin'])),
                                  "target_class":dest.get('classname',''),
                                  "angle":dest.get('angle'),"relation":"authored target key, not trajectory"})
        if len(points)>2048 or len(links)>1024:
            raise FormatError('annotation budget exceeded')
        return points,links


def re_model(value):
    if value.startswith('*') and value[1:].isdigit():
        return int(value[1:])
    return None
