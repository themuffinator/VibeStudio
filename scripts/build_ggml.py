#!/usr/bin/env python3
"""Build the pinned llama.cpp and ggml that VibeStudio's optional ggml support links.

    python scripts/build_ggml.py [--source <llama.cpp checkout>] [--prefix <install folder>]
                                 [--config Debug|Release] [--jobs N]

VibeStudio builds without them by default. With them (meson setup ... -Dggml=enabled), the Local
GGUF connector runs GGUF language models inside the studio, grammar-constrained, for every text
pipeline (the Assistant, both level generators' planners, map AI edits), and Quake-MapGen can rank
its candidate layouts through a ggml graph as it did upstream.

The source must be llama.cpp at the pinned revision below with no local changes; without --source a
clean clone is made under external/ai/ggml-work. The build is CPU only, static, and with the same
options Quake-MapGen used (no OpenMP, no llamafile SGEMM, no native instructions, no GPU backends),
so results do not depend on the machine it was built on. Match --config to the Meson build type:
Debug for buildtype=debug (the debug C runtime), Release otherwise. On Windows the compilers in
CC/CXX are used (VibeStudio builds with clang-cl).

Prints the meson setup options to use when it finishes.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

PINNED_REVISION = "c1d0e7a004015f23bc0233470b747b596f29b264"
REPOSITORY = "https://github.com/ggml-org/llama.cpp.git"
ROOT = Path(__file__).resolve().parents[1]
DEFAULT_WORK = ROOT / "external" / "ai" / "ggml-work"
DEFAULT_PREFIX = ROOT / "external" / "ai" / "ggml"

CMAKE_OPTIONS = {
    "BUILD_SHARED_LIBS": "OFF",
    "GGML_BUILD_TESTS": "OFF",
    "GGML_BUILD_EXAMPLES": "OFF",
    "GGML_CCACHE": "OFF",
    "GGML_LLAMAFILE": "OFF",
    "GGML_OPENMP": "OFF",
    "GGML_NATIVE": "OFF",
    "GGML_METAL": "OFF",
    "GGML_BLAS": "OFF",
    "GGML_ACCELERATE": "OFF",
    "GGML_CUDA": "OFF",
    "GGML_VULKAN": "OFF",
    "LLAMA_BUILD_IS_DEV": "OFF",
    "LLAMA_ALL_WARNINGS": "OFF",
    "LLAMA_BUILD_COMMON": "OFF",
    "LLAMA_BUILD_TESTS": "OFF",
    "LLAMA_BUILD_TOOLS": "OFF",
    "LLAMA_BUILD_EXAMPLES": "OFF",
    "LLAMA_BUILD_SERVER": "OFF",
    "LLAMA_BUILD_APP": "OFF",
    "LLAMA_BUILD_UI": "OFF",
    "LLAMA_TOOLS_INSTALL": "OFF",
    "LLAMA_TESTS_INSTALL": "OFF",
    "LLAMA_OPENSSL": "OFF",
}


def run(command: list[str], cwd: Path | None = None) -> str:
    print("+", " ".join(command), flush=True)
    result = subprocess.run(command, cwd=cwd, text=True, capture_output=True)
    if result.returncode != 0:
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise SystemExit(f"build_ggml: {command[0]} failed with exit code {result.returncode}")
    return result.stdout


def checked_source(source: Path) -> Path:
    if not (source / "CMakeLists.txt").is_file() or not (source / "ggml").is_dir():
        raise SystemExit(f"build_ggml: {source} is not a llama.cpp checkout")
    revision = run(["git", "-C", str(source), "rev-parse", "HEAD"]).strip()
    if revision != PINNED_REVISION:
        raise SystemExit(f"build_ggml: {source} is at {revision}, not the pinned {PINNED_REVISION}")
    if run(["git", "-C", str(source), "status", "--porcelain", "--untracked-files=normal"]).strip():
        raise SystemExit(f"build_ggml: {source} has local changes or untracked files")
    return source


def cloned_source(work: Path) -> Path:
    source = work / "llama.cpp"
    if not source.exists():
        work.mkdir(parents=True, exist_ok=True)
        run(["git", "clone", "--filter=blob:none", "--no-checkout", REPOSITORY, str(source)])
        run(["git", "-C", str(source), "checkout", "--detach", PINNED_REVISION])
    return checked_source(source)


def main(arguments: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Build the pinned llama.cpp and ggml for VibeStudio's -Dggml=enabled.")
    parser.add_argument("--source", type=Path, help="An existing clean llama.cpp checkout at the pinned revision.")
    parser.add_argument("--prefix", type=Path, default=DEFAULT_PREFIX, help="Install folder (default: external/ai/ggml).")
    parser.add_argument("--work", type=Path, default=DEFAULT_WORK, help="Clone and build folder (default: external/ai/ggml-work).")
    parser.add_argument("--config", choices=["Debug", "Release"], default="Debug", help="Debug for buildtype=debug, Release otherwise.")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    options = parser.parse_args(arguments)
    if not shutil.which("cmake"):
        raise SystemExit("build_ggml: CMake 3.28 or newer is required on PATH")

    source = checked_source(options.source.resolve()) if options.source else cloned_source(options.work.resolve())
    build = options.work.resolve() / f"build-{options.config.lower()}"
    prefix = options.prefix.resolve()
    configure = ["cmake", "-S", str(source), "-B", str(build), f"-DCMAKE_BUILD_TYPE={options.config}", f"-DCMAKE_INSTALL_PREFIX={prefix}",
                 "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"]
    if shutil.which("ninja"):
        configure += ["-G", "Ninja"]
    for compiler, variable in (("CMAKE_C_COMPILER", "CC"), ("CMAKE_CXX_COMPILER", "CXX")):
        if os.environ.get(variable):
            configure.append(f"-D{compiler}={os.environ[variable]}")
    configure += [f"-D{key}={value}" for key, value in CMAKE_OPTIONS.items()]
    run(configure)
    run(["cmake", "--build", str(build), "--config", options.config, "--parallel", str(options.jobs)])
    run(["cmake", "--install", str(build), "--config", options.config])
    (prefix / "VIBESTUDIO_GGML.txt").write_text(
        f"llama.cpp {PINNED_REVISION}\nconfig {options.config}\nsource {source}\n", encoding="utf-8")
    print()
    print(f"Installed llama.cpp {PINNED_REVISION[:7]} ({options.config}) into {prefix}.")
    print(f"Configure VibeStudio with: meson setup <builddir> -Dggml=enabled -Dggml_prefix={prefix.as_posix()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
