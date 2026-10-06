"""Check a synthetic player PK3 with original Quake III loader functions.

Requires a user-supplied GPL-2.0-or-later Quake-III-Arena checkout, a VibeStudio
CLI binary, the retained PlayerBundleFixture, Meson, Ninja and a C compiler.
Extracted functions keep their original licence header and recorded hashes.
Small file/renderer shims exercise player discovery, native skin tokenization,
animation parsing and TGA pixels. This is not a game or a renderer certification.
No installed game files, input devices, windows or screen captures are used.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess
import sys
import zipfile

from model_q3_animation_engine_oracle import function

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from source_companion_common import reject_links


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_md3(data, role):
    """Independent byte-level fixture assertions; no VibeStudio parser calls."""
    assert data[:4] == b'IDP3'
    version, = struct.unpack_from('<i', data, 4)
    flags, frames, tags, surfaces, skins, frame_at, tag_at, surface_at, end = struct.unpack_from('<9i', data, 72)
    assert version == 15 and frames == (1 if role == 'head' else 12) and surfaces == 1 and end == len(data)
    assert tags == (0 if role == 'head' else 1) and frame_at == 108
    assert frame_at + frames * 56 == tag_at and tag_at + frames * tags * 112 == surface_at
    for i in range(frames):
        values = struct.unpack_from('<10f', data, frame_at + i * 56)
        assert all(math.isfinite(v) for v in values)
        assert all(values[k] <= values[k + 3] for k in range(3)) and values[9] >= 0
    for i in range(frames * tags):
        at = tag_at + i * 112
        name = data[at:at + 64].split(b'\0')[0].decode()
        assert name == ('tag_torso' if role == 'lower' else 'tag_head')
        values = struct.unpack_from('<12f', data, at + 64)
        assert all(math.isfinite(v) for v in values)
        for k in range(3):
            assert abs(sum(v * v for v in values[3 + k * 3:6 + k * 3]) - 1) < 1e-4
    assert data[surface_at:surface_at + 4] == b'IDP3'
    flags, count, shaders, vertices, triangles, tri_at, shader_at, uv_at, xyz_at, size = struct.unpack_from('<10i', data, surface_at + 68)
    assert count == frames and shaders == 2 and vertices == 3 and triangles == 1 and surface_at + size == end
    # The editable document uses counter-clockwise faces; native MD3 is clockwise.
    assert struct.unpack_from('<3i', data, surface_at + tri_at) == (0, 2, 1)
    expected = ['models/player/body' if role == 'lower' else 'textures/player/a.tga', 'textures/player/b.tga']
    assert [data[surface_at + shader_at + i * 68:surface_at + shader_at + i * 68 + 64].split(b'\0')[0].decode() for i in range(2)] == expected
    for i in range(vertices * frames):
        xyz = struct.unpack_from('<3h', data, surface_at + xyz_at + i * 8)
        assert any(xyz)
    return frames + frames * tags + frames * vertices + shaders + 6


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.relative_to(ROOT / '.agents' / 'tmp')
    reject_links(output)
    output.mkdir(parents=True, exist_ok=False)
    source = output / 'source'; source.mkdir()
    paths = {name: args.source / name for name in ('code/cgame/cg_players.c', 'code/game/bg_public.h',
             'code/game/q_shared.c', 'code/qcommon/qfiles.h', 'code/renderer/tr_image.c')}
    texts = {name: path.read_text(encoding='utf-8') for name, path in paths.items()}
    for text in texts.values():
        if 'either version 2 of the License' not in text or 'any later version' not in text:
            raise ValueError('Expected GPL-2.0-or-later upstream licence header.')
    players, public, shared, formats, images = texts.values()
    roster = re.search(r'typedef enum\s*\{\s*BOTH_DEATH1,.*?\}\s*animNumber_t;', public, re.S).group()
    animation = re.search(r'typedef struct animation_s\s*\{.*?\}\s*animation_t;', public, re.S).group()
    targa = re.search(r'typedef struct _TargaHeader\s*\{.*?\}\s*TargaHeader;', formats, re.S).group()
    header = '\n'.join(dict.fromkeys(text[:text.index('*/') + 2] for text in texts.values()))
    shim = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
typedef int qboolean;
typedef int qhandle_t;
typedef unsigned char byte;
enum { qtrue=1, qfalse=0, MAX_TOKEN_CHARS=1024, MAX_QPATH=64, FS_READ=0,
       GT_TEAM=3, TEAM_BLUE=2, ERR_DROP=1, PRINT_WARNING=1, MAX_SKINS=16, LIGHTMAP_NONE=-1, h_low=0 };
#define DEFAULT_BLUETEAM_NAME "Pagans"
#define DEFAULT_REDTEAM_NAME "Stroggs"
static char com_token[MAX_TOKEN_CHARS];
static int com_lines;
static const char* blocked;
'''
    shim += roster + '\n' + animation + '\n' + targa + r'''
enum { FOOTSTEP_NORMAL, FOOTSTEP_BOOT, FOOTSTEP_FLESH, FOOTSTEP_MECH, FOOTSTEP_ENERGY };
enum { GENDER_MALE, GENDER_FEMALE, GENDER_NEUTER };
typedef struct { animation_t animations[MAX_TOTALANIMATIONS]; int footsteps,gender; float headOffset[3];
  qboolean fixedlegs,fixedtorso; int team, legsModel,torsoModel,headModel,legsSkin,torsoSkin,headSkin,modelIcon; } clientInfo_t;
static struct { int gametype; } cgs;
typedef FILE* fileHandle_t;
int trap_FS_FOpenFile(const char* path,fileHandle_t* file,int mode) {
  FILE* f; int n;
  if(file) *file=NULL;
  if(blocked && !strcmp(path,blocked)) return -1;
  f=fopen(path,"rb"); if(!f) return -1;
  fseek(f,0,SEEK_END); n=(int)ftell(f); fseek(f,0,SEEK_SET);
  if(file) *file=f; else fclose(f);
  return n;
}
void trap_FS_Read(void* p,int n,fileHandle_t f) { if(fread(p,1,n,f)!=(size_t)n) exit(4); }
void trap_FS_FCloseFile(fileHandle_t f) { fclose(f); }
void CG_Printf(const char* f, ...) { va_list a; va_start(a,f); vfprintf(stderr,f,a); va_end(a); }
#define Com_Printf CG_Printf
#define VectorClear(v) ((v)[0]=(v)[1]=(v)[2]=0)
int Q_stricmp(const char* a,const char* b) {
  while(*a && tolower((unsigned char)*a)==tolower((unsigned char)*b)) { ++a; ++b; }
  return tolower((unsigned char)*a)-tolower((unsigned char)*b);
}
void Com_sprintf(char* out,int n,const char* f,...) { va_list a; va_start(a,f); vsnprintf(out,n,f,a); va_end(a); }
void Q_strncpyz(char* out,const char* in,int n) { snprintf(out,n,"%s",in); }
char* Q_strlwr(char* p) { char* start=p; while(*p) { *p=(char)tolower((unsigned char)*p); ++p; } return start; }
int readFile(const char* path,void** bytes) {
  FILE* f; int n=trap_FS_FOpenFile(path,&f,FS_READ); *bytes=NULL;
  if(n<=0) { if(f) fclose(f); return -1; }
  *bytes=calloc(1,(size_t)n+1); if(!*bytes) exit(4); trap_FS_Read(*bytes,n,f); fclose(f); return n;
}
void error(int code,const char* f,...) { va_list a; va_start(a,f); vfprintf(stderr,f,a); va_end(a); exit(10); }
void print(int code,const char* f,...) { va_list a; va_start(a,f); vfprintf(stderr,f,a); va_end(a); }
void* alloc(int n,int h) { return calloc(1,n); }
typedef struct { char name[MAX_QPATH]; } shader_t;
typedef struct { char name[MAX_QPATH]; shader_t* shader; } skinSurface_t;
typedef struct { char name[MAX_QPATH]; int numSurfaces; skinSurface_t* surfaces[32]; } skin_t;
static struct { int numSkins; skin_t* skins[MAX_SKINS]; } tr={1};
static struct { int (*FS_ReadFile)(const char*,void**); void (*FS_FreeFile)(void*);
 void* (*Malloc)(size_t); void (*Error)(int,const char*,...); void (*Printf)(int,const char*,...); void* (*Hunk_Alloc)(int,int);
} ri={readFile,free,malloc,error,print,alloc};
short LittleShort(short value) { const unsigned short check=1; return *(const byte*)&check ? value : (short)(((unsigned short)value>>8)|((unsigned short)value<<8)); }
void R_SyncRenderThread(void) {}
shader_t* R_FindShader(const char* name,int lightmap,qboolean mip) {
  shader_t* result=calloc(1,sizeof(shader_t)); snprintf(result->name,sizeof(result->name),"%s",name); return result;
}
'''
    shim += '\n'.join(function(shared, name) for name in ('SkipWhitespace', 'COM_ParseExt', 'COM_Parse'))
    shim += '\n' + function(images, 'CommaParse') + '\n' + function(images, 'RE_RegisterSkin')
    shim += '\n' + function(images, 'LoadTGA') + '\n' + function(players, 'CG_ParseAnimationFile')
    shim += r'''
int trap_R_RegisterModel(const char* name) {
  void* bytes; int n=readFile(name,&bytes); int result=0;
  if(n>=108 && !memcmp(bytes,"IDP3",4) && ((byte*)bytes)[4]==15) { result=1; printf("model %s\n",name); }
  free(bytes); return result;
}
int trap_R_RegisterSkin(const char* name) { int n=RE_RegisterSkin(name); if(n) printf("skin %s\n",name); return n; }
int pixels(const char* name) {
  byte* data; int w=0,h=0,x,y; LoadTGA(name,&data,&w,&h); if(!data) return 0;
  if(w!=32 || h!=32) { free(data); return 0; }
  for(y=0;y<32;++y) for(x=0;x<32;++x) {
    byte* p=data+(y*32+x)*4;
    if(p[0]!=(x<16?240:30) || p[1]!=(y<16?220:40) || p[2]!=80 || p[3]!=255) { free(data); return 0; }
  }
  free(data); printf("pixels %s 4096\n",name); return 1;
}
int trap_R_RegisterShaderNoMip(const char* name) { return pixels(name); }
'''
    shim += '\n'.join(function(players, name) for name in ('CG_FileExists', 'CG_FindClientModelFile',
                    'CG_FindClientHeadFile', 'CG_RegisterClientSkin', 'CG_RegisterClientModelname'))
    shim += r'''
int main(int argc,char** argv) {
  clientInfo_t ci={0}; int handles[3],i;
  const char* expected[3]={"models/player/body","textures/player/a.tga","textures/player/a.tga"};
  if(argc!=3) return 2;
  blocked=argv[2];
  if(!CG_RegisterClientModelname(&ci,"synthetic",argv[1],"",argv[1],"")) return 3;
  handles[0]=ci.legsSkin; handles[1]=ci.torsoSkin; handles[2]=ci.headSkin;
  for(i=0;i<3;++i) {
    skin_t* s=tr.skins[handles[i]];
    if(s->numSurfaces!=1 || strcmp(s->surfaces[0]->name,"body") || strcmp(s->surfaces[0]->shader->name,expected[i])) return 5;
  }
  if(ci.footsteps!=1 || ci.gender!=1 || ci.headOffset[0]!=1 || ci.headOffset[1]!=-2 || ci.headOffset[2]!=3 || !ci.fixedlegs || ci.fixedtorso) return 6;
  for(i=0;i<MAX_ANIMATIONS;++i) if(ci.animations[i].firstFrame<0 || ci.animations[i].firstFrame+ci.animations[i].numFrames>12) return 7;
  if(!pixels("textures/player/a.tga") || !pixels("textures/player/b.tga")) return 8;
  puts("native-player-ok"); return 0;
}
'''
    (source / 'oracle.c').write_text(header + '\n' + shim, encoding='utf-8')
    (source / 'meson.build').write_text("project('q3-player-oracle', 'c', default_options: ['c_std=c11'])\nexecutable('oracle', 'oracle.c')\n", encoding='utf-8')
    commands = [['meson', 'setup', str(output / 'build'), str(source), '--buildtype=release']]
    if os.name == 'nt': commands[0] += ['--vsenv', '-Db_vscrt=md']
    commands += [['meson', 'compile', '-C', str(output / 'build'), '-j', '2']]
    for i, command in enumerate(commands):
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        (output / f'build-{i}.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        if result.returncode: raise RuntimeError(f'Oracle build failed; see build-{i}.log')
    binary = output / 'build' / ('oracle.exe' if os.name == 'nt' else 'oracle')
    fixture = args.fixture.resolve()
    original_hashes = {str(p.relative_to(fixture)): digest(p) for p in fixture.rglob('*') if p.is_file()}
    cases = []; comparisons = 0
    for skin in ('default', 'variant'):
        package = output / f'{skin}.pk3'
        command = [str(args.binary.resolve()), '--cli', 'model', 'assembly', str(fixture / 'player.assembly.json'),
                   '--operation', 'player-export', '--player-name', 'synthetic', '--head-part', 'head', '--skin-name', skin,
                   '--icon', str(fixture / 'icon.png'), '--package', str(fixture / 'assets'), '--output', str(package),
                   '--settings-file', str(output / 'settings.ini'), '--json']
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=60)
        (output / f'{skin}-cli.json').write_text(result.stdout, encoding='utf-8')
        assert result.returncode == 0, result.stdout + result.stderr
        payload = json.loads(result.stdout)
        assert payload['write']['determinismVerified'] and payload['write']['outputCommitted']
        game = output / skin; game.mkdir()
        with zipfile.ZipFile(package) as archive:
            assert len(archive.infolist()) == 12 and archive.testzip() is None
            receipt = json.loads(archive.read('vibestudio/player-bundle.json'))
            assert receipt['model'] == 'synthetic' and receipt['skin'] == skin
            for item in receipt['files']:
                data = archive.read(item['path'])
                assert hashlib.sha256(data).hexdigest() == item['sha256'] and len(data) == item['bytes']
                comparisons += 2
            for entry in archive.infolist():
                path = PurePosixPath(entry.filename)
                assert not path.is_absolute() and '..' not in path.parts and '\\' not in entry.filename
                target = game / path; target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(archive.read(entry))
            for role in ('lower', 'upper', 'head'):
                comparisons += validate_md3(archive.read(f'models/players/synthetic/{role}.md3'), role)
        blocked = ['', 'models/players/synthetic/lower.md3', f'models/players/synthetic/head_{skin}.skin',
                   'models/players/synthetic/animation.cfg', f'models/players/synthetic/icon_{skin}.tga']
        for index, missing in enumerate(blocked):
            result = subprocess.run([str(binary), skin, missing], cwd=game, capture_output=True, text=True, timeout=20)
            passed = result.returncode == (3 if missing else 0)
            if not missing:
                passed &= result.stdout.count('model ') == 3 and result.stdout.count('skin ') == 3 and result.stdout.count('pixels ') == 3
                comparisons += 3 * 4096 + 31 + 3
            cases.append({'skin': skin, 'blockedPath': missing, 'returnCode': result.returncode, 'passed': bool(passed),
                          'stdout': result.stdout, 'stderr': result.stderr})
            assert passed, cases[-1]
    assert original_hashes == {str(p.relative_to(fixture)): digest(p) for p in fixture.rglob('*') if p.is_file()}
    record = {'passed': True, 'comparisons': comparisons, 'cases': cases,
              'scope': 'Original player discovery, skin parsing, animation parser and TGA pixels; independent MD3 layout checks. Model registration and shader lookup are shims; no gameplay, renderer or shader-language certification.',
              'inputs': {str(p): digest(p) for p in [*paths.values(), args.binary, Path(__file__), Path(__file__).with_name('model_q3_animation_engine_oracle.py')]},
              'fixtureHashes': original_hashes, 'binarySha256': digest(binary),
              'generatedSourceSha256': digest(source / 'oracle.c')}
    (output / 'verified.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'passed': True, 'cases': len(cases), 'comparisons': comparisons}))


if __name__ == '__main__':
    main()
