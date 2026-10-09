#!/usr/bin/env python3
"""Compile VibeStudio's GPU shaders into src/core/render_shader_data.inc.

Each stage in src/core/shaders is written once. This script resolves its
#include lines and produces two forms of it:

- SPIR-V for the Vulkan backend, compiled by glslangValidator with
  "#version 450" and VIBE_VULKAN defined (Vulkan 1.0 environment);
- GLSL text for the OpenGL backend, which prefixes "#version 330 core" or
  "#version 300 es" at run time. Both prefixes are compiled here too, so a
  shader that only one OpenGL flavour accepts fails the build early.

The generated file is committed, so building VibeStudio needs no shader
compiler. It records a hash of the sources and the glslang version used.

    python scripts/build_render_shaders.py           # regenerate the include
    python scripts/build_render_shaders.py --check   # fail if it is stale

--check always verifies the recorded source hash and the embedded OpenGL
text. When glslangValidator is available it also validates every variant
and, with the same glslang version as the recorded one, compares SPIR-V
word for word. glslangValidator ships with the Vulkan SDK and with the
glslang-tools (Debian, Ubuntu) and glslang (Homebrew) packages; set
GLSLANG_VALIDATOR to point at a specific one.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHADERS = ROOT / "src" / "core" / "shaders"
OUTPUT = ROOT / "src" / "core" / "render_shader_data.inc"

# (identifier, file, glslang stage). The identifier names the C++ arrays.
STAGES = [
    ("model_surface_vert", "model_surface.vert", "vert"),
    ("model_surface_frag", "model_surface.frag", "frag"),
    ("model_wire_vert", "model_wire.vert", "vert"),
    ("model_wire_frag", "model_wire.frag", "frag"),
    ("fullscreen_vert", "fullscreen.vert", "vert"),
    ("composite_frag", "composite.frag", "frag"),
    ("material_vert", "material.vert", "vert"),
    ("material_quake3_frag", "material_quake3.frag", "frag"),
    ("material_doom_frag", "material_doom.frag", "frag"),
    ("material_quake_frag", "material_quake.frag", "frag"),
    ("material_doom3_frag", "material_doom3.frag", "frag"),
    ("material_screen_frag", "material_screen.frag", "frag"),
]

VULKAN_PREFIX = "#version 450\n#define VIBE_VULKAN 1\n"
GL_PREFIXES = {
    "330 core": "#version 330 core\n",
    "300 es": "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n",
}
INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"\s*$')
# MSVC refuses string literals longer than about 16 KB; adjacent literals are
# concatenated by the compiler, so long sources are split at line ends.
CHUNK_BYTES = 8000


def resolve(path: Path, seen: tuple[Path, ...] = ()) -> str:
    if path in seen:
        raise SystemExit(f"Circular #include: {path}")
    lines = []
    for line in path.read_text(encoding="utf-8").replace("\r\n", "\n").split("\n"):
        match = INCLUDE.match(line)
        if match:
            lines.append(resolve(path.parent / match.group(1), seen + (path,)).rstrip("\n"))
        else:
            lines.append(line)
    return "\n".join(lines).rstrip("\n") + "\n"


def sources_hash() -> str:
    digest = hashlib.sha256()
    names = sorted(p.name for p in SHADERS.iterdir() if p.is_file())
    for name in names:
        digest.update(name.encode("utf-8"))
        digest.update(b"\0")
        digest.update((SHADERS / name).read_bytes().replace(b"\r\n", b"\n"))
        digest.update(b"\0")
    return digest.hexdigest()


def find_glslang() -> str | None:
    candidates = []
    if os.environ.get("GLSLANG_VALIDATOR"):
        candidates.append(os.environ["GLSLANG_VALIDATOR"])
    for name in ("glslangValidator", "glslang"):
        found = shutil.which(name)
        if found:
            candidates.append(found)
    sdk = os.environ.get("VULKAN_SDK")
    if sdk:
        for folder in ("Bin", "bin"):
            for name in ("glslangValidator.exe", "glslangValidator"):
                candidates.append(str(Path(sdk) / folder / name))
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return candidate
    return None


def glslang_version(tool: str) -> str:
    result = subprocess.run([tool, "--version"], capture_output=True, text=True)
    for line in result.stdout.splitlines():
        if line.startswith("Glslang Version:"):
            return line.split(":", 1)[1].strip()
    return result.stdout.strip().splitlines()[0] if result.stdout.strip() else "unknown"


def run_glslang(tool: str, source: str, stage: str, spirv: bool) -> bytes:
    with tempfile.TemporaryDirectory() as folder:
        source_path = Path(folder) / f"shader.{stage}"
        source_path.write_text(source, encoding="utf-8", newline="\n")
        output = Path(folder) / "shader.spv"
        command = [tool, "-S", stage]
        if spirv:
            command += ["-V", "--target-env", "vulkan1.0", "-o", str(output)]
        command.append(str(source_path))
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode != 0:
            message = (result.stdout + result.stderr).strip()
            raise RuntimeError(message)
        return output.read_bytes() if spirv else b""


def c_string(text: str) -> str:
    chunks = []
    current = ""
    for line in text.splitlines(keepends=True):
        if current and len(current) + len(line) > CHUNK_BYTES:
            chunks.append(current)
            current = ""
        current += line
    if current or not chunks:
        chunks.append(current)
    for chunk in chunks:
        if ")vibeglsl\"" in chunk:
            raise SystemExit("Shader text contains the raw string terminator")
    return "\n".join(f'R"vibeglsl({chunk})vibeglsl"' for chunk in chunks)


def c_words(data: bytes) -> str:
    if len(data) % 4:
        raise RuntimeError("SPIR-V size is not a whole number of words")
    words = [int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data), 4)]
    lines = []
    for start in range(0, len(words), 8):
        lines.append("\t" + ", ".join(f"0x{word:08x}u" for word in words[start:start + 8]) + ",")
    return "\n".join(lines)


def generate(tool: str) -> str:
    version = glslang_version(tool)
    parts = [
        "// Generated by scripts/build_render_shaders.py from src/core/shaders.",
        "// Do not edit; change the shaders and run the script again.",
        f"// sources-sha256: {sources_hash()}",
        f"// glslang: {version}",
        "",
        "namespace vibestudio::render_shader_data {",
        "",
    ]
    for identifier, file_name, stage in STAGES:
        source = resolve(SHADERS / file_name)
        for flavour, prefix in GL_PREFIXES.items():
            try:
                run_glslang(tool, prefix + "#line 1\n" + source, stage, spirv=False)
            except RuntimeError as error:
                raise SystemExit(f"{file_name} does not compile as OpenGL GLSL {flavour}:\n{error}")
        try:
            spirv = run_glslang(tool, VULKAN_PREFIX + "#line 1\n" + source, stage, spirv=True)
        except RuntimeError as error:
            raise SystemExit(f"{file_name} does not compile for Vulkan:\n{error}")
        parts.append(f"// {file_name}")
        parts.append(f"inline constexpr char {identifier}_glsl[] =\n{c_string(source)};")
        parts.append(f"inline constexpr quint32 {identifier}_spirv[] = {{\n{c_words(spirv)}\n}};")
        parts.append("")
    parts.append("} // namespace vibestudio::render_shader_data")
    return "\n".join(parts) + "\n"


def recorded(text: str, key: str) -> str | None:
    match = re.search(rf"^// {re.escape(key)}: (.*)$", text, re.MULTILINE)
    return match.group(1).strip() if match else None


def check() -> int:
    if not OUTPUT.is_file():
        print(f"{OUTPUT.relative_to(ROOT)} is missing; run python scripts/build_render_shaders.py", file=sys.stderr)
        return 1
    current = OUTPUT.read_text(encoding="utf-8")
    errors = []
    if recorded(current, "sources-sha256") != sources_hash():
        errors.append("the shader sources changed since the include was generated")
    for identifier, file_name, _stage in STAGES:
        expected = f"inline constexpr char {identifier}_glsl[] =\n{c_string(resolve(SHADERS / file_name))};"
        if expected not in current:
            errors.append(f"the OpenGL text of {file_name} is stale")
    tool = find_glslang()
    if tool is None:
        print("glslangValidator not found: checked the source hash and OpenGL text only.")
    elif not errors:
        version = glslang_version(tool)
        if version == recorded(current, "glslang"):
            if generate(tool) != current:
                errors.append("the generated SPIR-V differs from a fresh build with the same glslang")
        else:
            # Another glslang emits different (equally valid) words; still
            # prove every variant compiles.
            for identifier, file_name, stage in STAGES:
                source = resolve(SHADERS / file_name)
                try:
                    for prefix in GL_PREFIXES.values():
                        run_glslang(tool, prefix + "#line 1\n" + source, stage, spirv=False)
                    run_glslang(tool, VULKAN_PREFIX + "#line 1\n" + source, stage, spirv=True)
                except RuntimeError as error:
                    errors.append(f"{file_name} no longer compiles:\n{error}")
            print(f"glslang {version} differs from the recorded {recorded(current, 'glslang')}: validated, not compared.")
    if errors:
        for error in errors:
            print(f"{OUTPUT.relative_to(ROOT)}: {error}", file=sys.stderr)
        print("Run: python scripts/build_render_shaders.py", file=sys.stderr)
        return 1
    print(f"{OUTPUT.relative_to(ROOT)} is up to date.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail if the generated include is stale")
    args = parser.parse_args()
    if args.check:
        return check()
    tool = find_glslang()
    if tool is None:
        print("glslangValidator was not found. Install the Vulkan SDK or glslang, or set GLSLANG_VALIDATOR.", file=sys.stderr)
        return 1
    text = generate(tool)
    if OUTPUT.is_file() and OUTPUT.read_text(encoding="utf-8") == text:
        print(f"{OUTPUT.relative_to(ROOT)} is up to date.")
        return 0
    with open(OUTPUT, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    print(f"Wrote {OUTPUT.relative_to(ROOT)}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
