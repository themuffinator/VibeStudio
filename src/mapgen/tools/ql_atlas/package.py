"""Package verified atlas diagrams only: python -B -m tools.ql_atlas.package --output out/ql-atlas."""
import argparse
import hashlib
import json
from pathlib import Path
import stat
import zipfile


def package(output):
    output=Path(output).resolve(strict=True)
    for p in (output,*output.parents):
        if p.is_symlink() or getattr(p.lstat(),'st_file_attributes',0)&getattr(stat,'FILE_ATTRIBUTE_REPARSE_POINT',0):
            raise ValueError('package output traverses link')
    qa=json.loads((output/'preview-checks.json').read_text())
    if qa['errors'] or len(qa['states'])!=696:
        raise ValueError('expected completed six-map atlas QA')
    for relative,expected in qa['inputHashes'].items():
        if hashlib.sha256((output/relative).read_bytes()).hexdigest()!=expected:
            raise ValueError(f'stale QA input: {relative}')
    for capture in qa['screenshots']:
        path=Path(capture['path']).resolve(strict=True)
        if not path.is_relative_to(output) or hashlib.sha256(path.read_bytes()).hexdigest()!=capture['sha256']:
            raise ValueError('stale/outside screenshot')
    names=['README.md','atlas.json','preview-checks.json','ql-reference-atlas.html','index.html']
    names += [str(p.relative_to(output).as_posix()) for p in sorted((output/'maps').glob('*.json'))]
    names += [str(p.relative_to(output).as_posix()) for p in sorted((output/'orthographics').glob('*/*.png'))]
    target=output/'ql-orthographic-atlas.zip'
    if target.is_symlink():raise ValueError('cannot replace linked package')
    with zipfile.ZipFile(target,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
        for name in names:
            source=output/name
            if source.is_symlink() or not source.resolve().is_relative_to(output):
                raise ValueError('source traverses link')
            info=zipfile.ZipInfo(name,date_time=(1980,1,1,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED
            archive.writestr(info,source.read_bytes())
    with zipfile.ZipFile(target) as archive:
        if archive.testzip() is not None or any(Path(name).suffix not in ('.png','.json','.html','.md') for name in archive.namelist()):
            raise ValueError('package verification failed')
    report={'archive':str(target),'sha256':hashlib.sha256(target.read_bytes()).hexdigest(),
            'bytes':target.stat().st_size,'files':len(names),'pngs':sum(name.endswith('.png') for name in names),
            'raw_bsp_or_game_assets_included':False,'scope':'Local personal-research analytical diagrams only'}
    (output/'package.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',required=True,type=Path)
    print(json.dumps(package(parser.parse_args().output),indent=2))
