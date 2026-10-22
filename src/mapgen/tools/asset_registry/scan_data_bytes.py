"""CTest data_no_asset_bytes: data/ must hold no game asset bytes (image, WAL, BSP, WAD, archive or model data).

usage:
  python -B tools/asset_registry/scan_data_bytes.py [--root DIR] [--self-test]

Every file under --root (default: the repository's data/) is read in full and checked for:
  start-of-file signatures  PNG, JPEG, GIF, BMP, TGA (header plus size or footer), PCX, Quake II WAL (mip offsets),
                            Quake miptex lumps, BSP (IBSP, RBSP, FBSP, VBSP, BSP2, 2PSB, BSP29), WAD (WAD2, WAD3,
                            IWAD, PWAD), PAK, ZIP/PK3, AAS, MDL/MD2/MD3, SPR/SP2, DDS, KTX, WebP
  embedded signatures       the PNG signature, JPEG SOI+marker, ZIP local header, TGA 2.0 footer, data:image/ URIs
  binary content            NUL bytes, or a run of 1024+ base64 or hex characters (bytes smuggled into text)
  registry entries          a qmg.asset_registry file must give every entry source, origin and licence_status
ASCII signatures (IBSP, WAD2, PACK, ...) count only at the start of a file: prose such as MISSIONPACK is not data.
--self-test first proves that a synthetic sample of each kind is detected and that a clean JSON file is not.
Exit 0 when nothing is found, 1 otherwise.
"""
from __future__ import annotations

import argparse
import json
import re
import struct
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]

START_MAGIC = [
    (b"\x89PNG\r\n\x1a\n", "PNG image"), (b"GIF87a", "GIF image"), (b"GIF89a", "GIF image"),
    (b"IBSP", "IBSP (Quake II/III BSP)"), (b"RBSP", "RBSP BSP"), (b"FBSP", "FBSP BSP"), (b"VBSP", "VBSP BSP"),
    (b"BSP2", "BSP2 BSP"), (b"2PSB", "2PSB BSP"),
    (b"WAD2", "WAD2"), (b"WAD3", "WAD3"), (b"IWAD", "IWAD"), (b"PWAD", "PWAD"), (b"PACK", "PAK archive"),
    (b"PK\x03\x04", "ZIP/PK3 archive"), (b"PK\x05\x06", "ZIP/PK3 archive"), (b"EAAS", "AAS file"),
    (b"IDPO", "MDL model"), (b"IDP2", "MD2 model"), (b"IDP3", "MD3 model"), (b"IDSP", "SPR sprite"),
    (b"IDS2", "SP2 sprite"), (b"DDS ", "DDS image"), (b"\xabKTX", "KTX image"),
]
EMBEDDED_MAGIC = [
    (b"\x89PNG\r\n\x1a\n", "embedded PNG signature"),
    (b"PK\x03\x04", "embedded ZIP local header"),
    (b"TRUEVISION-XFILE.\x00", "embedded TGA footer"),
    (b"data:image/", "data:image URI"),
]
JPEG_EMBEDDED = re.compile(rb"\xff\xd8\xff[\xc0-\xfe]")
LONG_BASE64 = re.compile(rb"[A-Za-z0-9+/]{1024,}={0,2}")
LONG_HEX = re.compile(rb"[0-9A-Fa-f]{1024,}")
ENTRY_LISTS = ("textures", "skies", "shaders", "images")
ENTRY_FIELDS = ("source", "origin", "licence_status")


def mip_chain_ok(data: bytes, header: int, offset_pos: int, size: int) -> bool:
    if len(data) < offset_pos + 16:
        return False
    width, height = struct.unpack_from("<II", data, offset_pos - 8)
    if not (0 < width <= 4096 and 0 < height <= 4096):
        return False
    offsets = struct.unpack_from("<4I", data, offset_pos)
    expected, offset = [], header
    for level in range(4):
        expected.append(offset)
        offset += (width >> level) * (height >> level)
    return list(offsets) == expected and offset <= size


def start_findings(data: bytes) -> list[str]:
    found = []
    size = len(data)
    for magic, label in START_MAGIC:
        if data.startswith(magic):
            found.append(label)
    if data[:3] == b"\xff\xd8\xff":
        found.append("JPEG image")
    if data[:4] == b"RIFF" and data[8:12] == b"WEBP":
        found.append("WebP image")
    if size >= 8 and struct.unpack_from("<i", data, 0)[0] in (29, 30):
        version_lumps = 15
        if size >= 4 + version_lumps * 8:
            lumps = struct.unpack_from(f"<{version_lumps * 2}i", data, 4)
            if all(0 <= lumps[i] and 0 <= lumps[i + 1] and lumps[i] + lumps[i + 1] <= size for i in range(0, len(lumps), 2)):
                found.append("BSP29/HL BSP (version header with an in-range lump table)")
    if size >= 26 and data[:2] == b"BM" and struct.unpack_from("<I", data, 2)[0] == size:
        found.append("BMP image")
    if size >= 128 and data[0] == 0x0A and data[1] in (0, 2, 3, 4, 5) and data[2] == 1 and data[3] in (1, 2, 4, 8):
        found.append("PCX image")
    if size >= 18:
        id_length, cmap_type, image_type = data[0], data[1], data[2]
        if cmap_type in (0, 1) and image_type in (1, 2, 3, 9, 10, 11):
            cmap_length, cmap_depth = struct.unpack_from("<H", data, 5)[0], data[7]
            width, height = struct.unpack_from("<HH", data, 12)
            depth = data[16]
            if width and height and depth in (8, 15, 16, 24, 32):
                raw = 18 + id_length + cmap_length * ((cmap_depth + 7) // 8) + width * height * ((depth + 7) // 8)
                footer = data.endswith(b"TRUEVISION-XFILE.\x00")
                if (image_type in (1, 2, 3) and raw <= size <= raw + 4096) or footer:
                    found.append("TGA image")
    if size >= 100 and mip_chain_ok(data, 100, 40, size):
        found.append("Quake II WAL texture")
    if size >= 40 and mip_chain_ok(data, 40, 24, size):
        found.append("Quake miptex lump")
    return found


def scan_bytes(data: bytes) -> list[str]:
    found = start_findings(data)
    for magic, label in EMBEDDED_MAGIC:
        if data.find(magic, 1) >= 0:
            found.append(label)
    if JPEG_EMBEDDED.search(data, 1):
        found.append("embedded JPEG SOI marker")
    if b"\x00" in data:
        found.append("binary content (NUL bytes)")
    if LONG_BASE64.search(data):
        found.append("1024+ character base64 run")
    if LONG_HEX.search(data):
        found.append("1024+ character hex run")
    return sorted(set(found))


def registry_findings(data: bytes) -> list[str]:
    try:
        document = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, ValueError):
        return []
    if not isinstance(document, dict) or document.get("schema") != "qmg.asset_registry":
        return []
    problems = []
    for list_name in ENTRY_LISTS:
        for index, entry in enumerate(document.get(list_name) or []):
            missing = [field for field in ENTRY_FIELDS if not (isinstance(entry, dict) and entry.get(field))]
            if missing:
                problems.append(f"registry {list_name}[{index}] lacks {', '.join(missing)}")
    return problems


def scan(root: Path) -> tuple[int, int, dict[str, list[str]]]:
    files = sorted(path for path in root.rglob("*") if path.is_file())
    findings: dict[str, list[str]] = {}
    total = 0
    for path in files:
        data = path.read_bytes()
        total += len(data)
        found = scan_bytes(data) + registry_findings(data)
        if found:
            findings[path.relative_to(root).as_posix()] = found
    return len(files), total, findings


def self_test() -> list[str]:
    """Each synthetic sample must be flagged; the clean sample must not."""
    wal = b"qmgtest/x".ljust(32, b"\0") + struct.pack("<II", 16, 16) + struct.pack("<4I", 100, 356, 420, 436) + bytes(44)
    wal += bytes(436 + 4 - len(wal))
    miptex = b"QMGX".ljust(16, b"\0") + struct.pack("<II4I", 16, 16, 40, 296, 360, 376) + bytes(340)
    tga = bytes([0, 0, 2]) + bytes(5) + struct.pack("<HHHH", 0, 0, 4, 4) + bytes([24, 0]) + bytes(48)
    bsp29 = struct.pack("<i", 29) + struct.pack("<30i", *([124, 0] * 15)) + bytes(4)
    samples = {
        "png.json": b"\x89PNG\r\n\x1a\n" + bytes(16),
        "jpeg.bin": b"\xff\xd8\xff\xe0" + bytes(16),
        "gif.bin": b"GIF89a" + bytes(16),
        "bmp.bin": b"BM" + struct.pack("<I", 30) + bytes(24),
        "tga.bin": tga,
        "pcx.bin": bytes([0x0A, 5, 1, 8]) + bytes(124),
        "wal.json": wal,
        "miptex.bin": miptex,
        "ibsp.bin": b"IBSP" + struct.pack("<i", 38),
        "bsp29.bin": bsp29,
        "bsp2.bin": b"BSP2" + bytes(8),
        "wad2.json": b"WAD2" + struct.pack("<ii", 0, 12),
        "wad3.bin": b"WAD3" + bytes(8),
        "pak.bin": b"PACK" + bytes(8),
        "pk3.bin": b"PK\x03\x04" + bytes(26),
        "md3.bin": b"IDP3" + bytes(8),
        "aas.bin": b"EAAS" + bytes(8),
        "embedded-png.json": b'{"x": "' + b"\x89PNG\r\n\x1a\n" + b'"}',
        "embedded-jpeg.json": b'{"x": "\xff\xd8\xff\xdb"}',
        "data-uri.json": b'{"icon": "data:image/png;base64,AAAA"}',
        "base64.json": b'{"blob": "' + b"QUJD" * 300 + b'"}',
        "hex.json": b'{"blob": "' + b"0f" * 600 + b'"}',
        "nul.json": b'{"x": 1}\x00',
        "assets/registry.json": json.dumps({"schema": "qmg.asset_registry", "textures": [
            {"name": "a", "source": "textures/a.wal", "origin": "x"}]}).encode("ascii"),
    }
    clean = json.dumps({"schema": "qmg.game_profile", "note": "MISSIONPACK, IBSP and WAD2 named in prose",
                        "sha256": "0" * 64}).encode("ascii")
    errors = []
    with tempfile.TemporaryDirectory(prefix="qmg-data-scan-") as tmp:
        root = Path(tmp)
        for name, data in samples.items():
            (root / name).parent.mkdir(parents=True, exist_ok=True)
            (root / name).write_bytes(data)
        (root / "clean.json").write_bytes(clean)
        _, _, findings = scan(root)
        for name in samples:
            if name not in findings:
                errors.append(f"self-test: {name} was not detected")
        if "clean.json" in findings:
            errors.append(f"self-test: clean.json was flagged: {findings['clean.json']}")
    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=REPO / "data")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)
    status = 0
    if args.self_test:
        errors = self_test()
        for error in errors:
            print(f"FAIL {error}")
        print(f"self-test: {'passed' if not errors else f'{len(errors)} failures'}")
        status |= 1 if errors else 0
    if not args.root.is_dir():
        print(f"FAIL {args.root} is not a directory")
        return 1
    count, total, findings = scan(args.root)
    for name in sorted(findings):
        print(f"FAIL {args.root.name}/{name}: {', '.join(findings[name])}")
    print(f"data_no_asset_bytes: {count} files, {total} bytes scanned under {args.root}; {len(findings)} with asset bytes")
    return status | (1 if findings else 0)


if __name__ == "__main__":
    sys.exit(main())
