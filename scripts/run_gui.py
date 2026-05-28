#!/usr/bin/env python3
from __future__ import annotations

import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from _common import PROJECT_ROOT, build_target, env_flag, parse_build_args, run_command


def main() -> None:
    opts = parse_build_args()
    binary = build_target(
        target="shmtu_cas_ocr_gui",
        binary_relpath="ocr/shmtu-cas-ocr-gui/shmtu_cas_ocr_gui",
        use_vulkan=opts["use_vulkan"],
        build_gui=True,
        skip_build=opts["skip_build"],
    )

    args = [
        str(binary),
        "--model-dir",
        os.environ.get("SHMTU_MODEL_DIR", str(PROJECT_ROOT / "models")),
        "--precision",
        os.environ.get("SHMTU_PRECISION", "fp16"),
        *opts["extra_args"],
    ]
    if env_flag("SHMTU_USE_GPU", default=False):
        args.append("--use-gpu")
    sys.exit(run_command(args))


if __name__ == "__main__":
    main()
