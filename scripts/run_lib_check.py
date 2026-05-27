#!/usr/bin/env python3
from __future__ import annotations

import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from _common import main_exit


def main() -> None:
    preset = os.environ.get("SHMTU_BUILD_PRESET", "build-linux-vcpkg-vulkan")
    print("shmtu-cas-ocr-lib is a library target and has no standalone executable.")
    print(f"Running a focused build check for the library through preset: {preset}")

    args = [
        "cmake",
        "--build",
        "--preset",
        preset,
        "--target",
        "shmtu_cas_ocr_lib",
        *sys.argv[1:],
    ]
    main_exit(args)


if __name__ == "__main__":
    main()
