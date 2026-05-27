from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path
from typing import Sequence


SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
DEFAULT_BUILD_DIR = PROJECT_ROOT / "build" / "linux-vcpkg-vulkan"
FALLBACK_BUILD_DIR = Path("/tmp/shmtu-drogon-vulkan-config")


def env_flag(name: str, default: bool = True) -> bool:
    value = os.environ.get(name)
    if value is None:
        return default
    return value not in {"0", "false", "False", "no", "NO"}


def resolve_build_dir(binary_relpath: str) -> Path:
    build_dir_env = os.environ.get("SHMTU_BUILD_DIR")
    candidates = []
    if build_dir_env:
        candidates.append(Path(build_dir_env))
    candidates.extend([DEFAULT_BUILD_DIR, FALLBACK_BUILD_DIR])

    for candidate in candidates:
        binary = candidate / binary_relpath
        if binary.is_file() and os.access(binary, os.X_OK):
            return candidate

    searched = "\n".join(str(candidate / binary_relpath) for candidate in candidates)
    raise SystemExit(
        f"Binary not found for {binary_relpath}\n"
        f"Searched:\n{searched}\n"
        "Set SHMTU_BUILD_DIR or build the project first."
    )


def find_build_dirs() -> list[Path]:
    """Find all existing build directories under the project root."""
    dirs = []
    for d in PROJECT_ROOT.iterdir():
        if d.is_dir() and d.name.startswith("build") and (d / "CMakeCache.txt").is_file():
            dirs.append(d)
    dirs.sort(key=lambda d: (0 if d.name == "build-gui" else 1 if d.name == "build" else 2))
    return dirs


def cmake_configure(
    build_dir: Path,
    build_server: bool = True,
    build_cli: bool = True,
    build_gui: bool = False,
    use_vulkan: bool = False,
) -> int:
    """Run cmake configure. Returns 0 on success."""
    vcpkg_root = os.environ.get("VCPKG_ROOT", str(Path.home() / "vcpkg"))
    toolchain = Path(vcpkg_root) / "scripts" / "buildsystems" / "vcpkg.cmake"

    if not toolchain.is_file():
        print(f"Error: vcpkg toolchain not found at {toolchain}")
        print("Set VCPKG_ROOT environment variable or install vcpkg.")
        return 1

    cmd = [
        "cmake",
        "-B", str(build_dir),
        f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
        f"-DBUILD_SERVER={'ON' if build_server else 'OFF'}",
        f"-DBUILD_CLI={'ON' if build_cli else 'OFF'}",
        f"-DBUILD_GUI={'ON' if build_gui else 'OFF'}",
        f"-DUSE_VULKAN={'ON' if use_vulkan else 'OFF'}",
        str(PROJECT_ROOT),
    ]

    print(f"[cmake configure] {' '.join(cmd)}")
    result = subprocess.run(cmd, check=False)
    return result.returncode


def cmake_build(build_dir: Path, target: str | None = None) -> int:
    """Run cmake build. Returns 0 on success."""
    cmd = ["cmake", "--build", str(build_dir)]
    if target:
        cmd.extend(["--target", target])

    print(f"[cmake build] {' '.join(cmd)}")
    result = subprocess.run(cmd, check=False)
    return result.returncode


def build_target(
    target: str,
    binary_relpath: str,
    use_vulkan: bool = False,
    build_gui: bool = False,
    skip_build: bool = False,
) -> Path:
    """Build a target then return the binary path.

    By default always builds. Set skip_build=True to skip if binary exists.
    """
    build_dir_env = os.environ.get("SHMTU_BUILD_DIR")
    candidates = []
    if build_dir_env:
        candidates.append(Path(build_dir_env))
    candidates.extend(find_build_dirs())

    # Check existing binary (only skip build if skip_build=True)
    if skip_build:
        for candidate in candidates:
            binary = candidate / binary_relpath
            if binary.is_file() and os.access(binary, os.X_OK):
                print(f"[found] {binary} (skipped build)")
                return binary

    # Need to build
    print(f"[build] Building {target}...")

    build_dir = Path(build_dir_env) if build_dir_env else (
        PROJECT_ROOT / "build-gui" if build_gui else PROJECT_ROOT / "build"
    )

    # Configure
    rc = cmake_configure(
        build_dir,
        build_server=(target == "shmtu_cas_ocr_server"),
        build_cli=(target == "shmtu_cas_ocr_cli"),
        build_gui=build_gui or (target == "shmtu_cas_ocr_gui"),
        use_vulkan=use_vulkan,
    )
    if rc != 0:
        raise SystemExit(f"CMake configure failed with code {rc}")

    # Build
    rc = cmake_build(build_dir, target)
    if rc != 0:
        raise SystemExit(f"CMake build failed with code {rc}")

    # Verify
    binary = build_dir / binary_relpath
    if not binary.is_file():
        raise SystemExit(f"Build succeeded but binary not found at {binary}")

    print(f"[built] {binary}")
    return binary


def parse_build_args() -> dict:
    """Parse common --skip-build/--vulkan flags from sys.argv."""
    skip_build = False
    use_vulkan = env_flag("SHMTU_USE_VULKAN", default=False)
    extra_args = []

    for arg in sys.argv[1:]:
        if arg in ("--skip-build", "--no-build"):
            skip_build = True
        elif arg == "--vulkan":
            use_vulkan = True
        elif arg == "--no-vulkan":
            use_vulkan = False
        else:
            extra_args.append(arg)

    return {
        "skip_build": skip_build,
        "use_vulkan": use_vulkan,
        "extra_args": extra_args,
    }


def run_command(args: Sequence[str]) -> int:
    completed = subprocess.run(list(args), check=False)
    return completed.returncode


def main_exit(args: Sequence[str]) -> None:
    sys.exit(run_command(args))
