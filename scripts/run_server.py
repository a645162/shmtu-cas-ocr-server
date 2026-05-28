#!/usr/bin/env python3
from __future__ import annotations

import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from _common import build_target, env_flag, parse_build_args, run_command, PROJECT_ROOT


def main() -> None:
    opts = parse_build_args()
    binary = build_target(
        target="shmtu_cas_ocr_server",
        binary_relpath="ocr/shmtu-cas-ocr-server/shmtu_cas_ocr_server",
        use_vulkan=opts["use_vulkan"],
        build_gui=False,
        skip_build=opts["skip_build"],
    )

    args = [
        str(binary),
        "--model-dir",
        os.environ.get("SHMTU_MODEL_DIR", str(PROJECT_ROOT / "models")),
        "--http-port",
        os.environ.get("SHMTU_HTTP_PORT", "21600"),
        "--tcp-port",
        os.environ.get("SHMTU_TCP_PORT", "21601"),
        "--precision",
        os.environ.get("SHMTU_PRECISION", "fp16"),
    ]

    if env_flag("SHMTU_USE_GPU", default=True):
        args.append("--use-gpu")

    args.extend(opts["extra_args"])
    sys.exit(run_command(args))


if __name__ == "__main__":
    main()
