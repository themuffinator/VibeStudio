"""Compare exported synthetic clips with unmodified original Quake III functions.

Requires a user-supplied GPL-2.0-or-later Quake-III-Arena source checkout, Meson,
Ninja and a C++ compiler. Builds a small parser/pose-selector harness, not a game.
No game assets, input events, window or screen captures are used. The generated
fragments retain their upstream licence header and source hashes. See CREDITS.md.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from source_companion_common import reject_links


def function(source, name):
    match = re.search(r'(?m)^[^\n;{}]*\b' + re.escape(name) + r'\s*\([^;{}]*\)\s*\{', source)
    if not match:
        raise ValueError(f'Missing source function: {name}')
    # These specific upstream functions have no unmatched braces in comments.
    start = source.index('{', match.start())
    depth = 1
    end = start + 1
    while depth and end < len(source):
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    if depth:
        raise ValueError(f'Unbalanced function: {name}')
    return source[match.start():end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--expected', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.relative_to(ROOT / '.agents' / 'tmp')
    reject_links(output)
    output.mkdir(parents=True, exist_ok=False)
    source = output / 'source'
    source.mkdir()
    paths = {name: args.source / name for name in ('code/cgame/cg_players.c', 'code/game/bg_public.h', 'code/game/q_shared.c')}
    texts = {name: path.read_text(encoding='utf-8') for name, path in paths.items()}
    for text in texts.values():
        if 'either version 2 of the License' not in text or 'any later version' not in text:
            raise ValueError('Expected GPL-2.0-or-later upstream licence header.')
    players = texts['code/cgame/cg_players.c']
    shared = texts['code/game/q_shared.c']
    public = texts['code/game/bg_public.h']
    roster = re.search(r'typedef enum\s*\{\s*BOTH_DEATH1,.*?\}\s*animNumber_t;', public, re.S).group()
    animation = re.search(r'typedef struct animation_s\s*\{.*?\}\s*animation_t;', public, re.S).group()
    header = players[:players.index('*/') + 2]
    shim = r'''
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cctype>
using qboolean = int;
constexpr int qtrue=1, qfalse=0, MAX_TOKEN_CHARS=1024;
static char com_token[MAX_TOKEN_CHARS];
static int com_lines;
'''
    shim += roster + '\n' + animation + r'''
enum { FOOTSTEP_NORMAL, FOOTSTEP_BOOT, FOOTSTEP_FLESH, FOOTSTEP_MECH, FOOTSTEP_ENERGY };
enum { GENDER_MALE, GENDER_FEMALE, GENDER_NEUTER };
struct clientInfo_t { animation_t animations[MAX_TOTALANIMATIONS]; int footsteps,gender; float headOffset[3]; qboolean fixedlegs,fixedtorso; };
using fileHandle_t = FILE*;
constexpr int FS_READ = 0;
int trap_FS_FOpenFile(const char* path, fileHandle_t* file, int) {
  *file=fopen(path,"rb"); if(!*file) return -1;
  fseek(*file,0,SEEK_END); int n=int(ftell(*file)); fseek(*file,0,SEEK_SET); return n;
}
void trap_FS_Read(void* p,int n,fileHandle_t f) { if(fread(p,1,n,f)!=size_t(n)) exit(4); }
void trap_FS_FCloseFile(fileHandle_t f) { fclose(f); }
void CG_Printf(const char* f, ...) { va_list a; va_start(a,f); vfprintf(stderr,f,a); va_end(a); }
#define Com_Printf CG_Printf
#define VectorClear(v) ((v)[0]=(v)[1]=(v)[2]=0)
int Q_stricmp(const char* a,const char* b) {
  while(*a && tolower(static_cast<unsigned char>(*a))==tolower(static_cast<unsigned char>(*b))) { ++a; ++b; }
  return tolower(static_cast<unsigned char>(*a))-tolower(static_cast<unsigned char>(*b));
}
'''
    shim += '\n'.join(function(shared, name) for name in ('SkipWhitespace', 'COM_ParseExt', 'COM_Parse'))
    shim += '\n' + function(players, 'CG_ParseAnimationFile') + r'''
struct { int time; } cg;
struct { int integer; } cg_animSpeed{1}, cg_debugAnim{0};
struct lerpFrame_t { int oldFrame,frame,oldFrameTime,frameTime,animationTime,animationNumber; float backlerp; animation_t* animation; };
void CG_SetLerpFrameAnimation(clientInfo_t*,lerpFrame_t*,int) { exit(5); } // Unused: each case starts on its selected clip.
'''
    shim += '\n' + function(players, 'CG_RunLerpFrame') + r'''
int main(int argc,char** argv) {
  if(argc!=2) return 2;
  clientInfo_t ci{};
  if(!CG_ParseAnimationFile(argv[1],&ci)) return 3;
  printf("header %d %d %g %g %g %d %d\n",ci.footsteps,ci.gender,ci.headOffset[0],ci.headOffset[1],ci.headOffset[2],ci.fixedlegs,ci.fixedtorso);
  for(int i=0;i<MAX_ANIMATIONS;++i) {
    auto& a=ci.animations[i];
    printf("slot %d %d %d %d %d %d\n",i,a.firstFrame,a.numFrames,a.loopFrames,a.frameLerp,a.reversed);
    for(int tick=0;tick<33;++tick) {
      lerpFrame_t lf{}; lf.animation=&a; lf.animationNumber=i;
      // Ask the original function to select playback step tick. This isolates
      // native frame selection from transition/catch-up clock policy.
      lf.frameTime=(tick-1)*a.frameLerp; lf.animationTime=-a.frameLerp;
      cg.time=lf.frameTime;
      // animationTime is reset to zero after bypassing the initial-lerp branch:
      // for tick zero, old time zero with animationTime=period produces f=0.
      if(tick==0) { lf.frameTime=0; cg.time=0; lf.animationTime=a.frameLerp; }
      else lf.animationTime=0;
      CG_RunLerpFrame(&ci,&lf,i,1);
      printf("frame %d %d %d\n",i,tick,lf.frame);
    }
  }
}
'''
    (source / 'oracle.cpp').write_text(header + '\n' + shim, encoding='utf-8')
    (source / 'meson.build').write_text("project('q3-animation-oracle', 'cpp', default_options: ['cpp_std=c++20'])\nexecutable('oracle', 'oracle.cpp')\n", encoding='utf-8')
    commands = [['meson', 'setup', str(output / 'build'), str(source), '--buildtype=release']]
    if os.name == 'nt': commands[0] += ['--vsenv', '-Db_vscrt=md']
    commands += [['meson', 'compile', '-C', str(output / 'build'), '-j', '2']]
    for i, command in enumerate(commands):
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        (output / f'build-{i}.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        if result.returncode: raise RuntimeError(f'Oracle build failed; see {output / f"build-{i}.log"}')
    binary = output / 'build' / ('oracle.exe' if os.name == 'nt' else 'oracle')
    result = subprocess.run([str(binary), str(args.fixture.resolve())], cwd=output, capture_output=True, text=True, timeout=30)
    (output / 'oracle-output.txt').write_text(result.stdout + result.stderr, encoding='utf-8')
    if result.returncode: raise RuntimeError('Original engine parser failed.')
    slots = json.loads(args.expected.read_text(encoding='utf-8'))['slots']
    rows = result.stdout.splitlines()
    assert rows.pop(0) == 'header 1 1 1 -2 3 1 0'
    comparisons = 7
    for row in rows:
        kind, *values = row.split()
        numbers = list(map(int, values))
        i = numbers[0]
        expected = slots[i]
        if kind == 'slot':
            assert numbers[1:] == [expected[k] for k in ('first', 'count', 'loop', 'period', 'reverse')], row
            comparisons += 5
        else:
            assert kind == 'frame' and numbers[2] == expected['frames'][numbers[1]], row
            comparisons += 1
    assert len(rows) == 31 * 34
    record = {'passed': True, 'comparisons': comparisons, 'scope': 'Original parser and steady playback-step selection; not gameplay or transition timing.',
              'inputs': {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in [*paths.values(), args.fixture, args.expected, Path(__file__)]},
              'binarySha256': hashlib.sha256(binary.read_bytes()).hexdigest()}
    (output / 'verified.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'passed': True, 'comparisons': comparisons}))


if __name__ == '__main__':
    main()
