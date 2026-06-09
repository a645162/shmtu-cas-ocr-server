#!/usr/bin/env python3
"""Unified SHMTU CAS OCR model downloader.

Dispatches to ``download_models_v1.py`` (3-model pipeline) or
``download_models_v2.py`` (TriSlot decoder) depending on ``--version``.

Defaults to V2 — the v2 release is the recommended default for new deployments.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

THIS_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = THIS_DIR.parent

sys.path.insert(0, str(THIS_DIR))

import download_models_v1  # noqa: E402
import download_models_v2  # noqa: E402


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Download SHMTU CAS OCR model weights. "
            "Default version: v2 (TriSlot decoder)."
        )
    )
    parser.add_argument(
        "--version",
        choices=("v1", "v2", "both"),
        default="v2",
        help="Model version: v1 (3-model), v2 (TriSlot, default), or both.",
    )

    # v2-specific options (used when --version v2 or both)
    parser.add_argument(
        "--tag",
        default=download_models_v2.DEFAULT_TAG,
        help="(v2) Release tag (default: v2.0.2)",
    )
    parser.add_argument(
        "--backbone",
        default=download_models_v2.DEFAULT_BACKBONE,
        help="(v2) Backbone name (default: mobilenet_v3_small)",
    )
    parser.add_argument(
        "--precision",
        default=download_models_v2.DEFAULT_PRECISION,
        help="(v2) Primary precision (default: fp16)",
    )
    parser.add_argument(
        "--include-fp32",
        action="store_true",
        help="(v2) Also download fp32 weights alongside the primary precision.",
    )
    parser.add_argument(
        "--dest",
        type=Path,
        default=None,
        help=(
            "Override destination directory. By default v1 -> models/v1, "
            "v2 -> models/v2."
        ),
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Redownload and replace existing files.",
    )
    return parser.parse_args()


def run_v1(args: argparse.Namespace) -> int:
    # Re-invoke the v1 module with its own argv.
    saved = sys.argv
    sys.argv = [str(THIS_DIR / "download_models_v1.py")]
    if args.dest is not None:
        sys.argv += ["--dest", str(args.dest)]
    if args.force:
        sys.argv += ["--force"]
    try:
        return download_models_v1.run()
    finally:
        sys.argv = saved


def run_v2(args: argparse.Namespace) -> int:
    saved = sys.argv
    sys.argv = [str(THIS_DIR / "download_models_v2.py")]
    sys.argv += ["--tag", args.tag]
    sys.argv += ["--backbone", args.backbone]
    sys.argv += ["--precision", args.precision]
    if args.include_fp32:
        sys.argv += ["--include-fp32"]
    if args.dest is not None:
        sys.argv += ["--dest", str(args.dest)]
    if args.force:
        sys.argv += ["--force"]
    try:
        return download_models_v2.run()
    finally:
        sys.argv = saved


def main() -> int:
    args = parse_args()
    rc = 0
    if args.version in ("v1", "both"):
        print("=== Downloading V1 (3-model) ===")
        rc_v1 = run_v1(args)
        rc = rc or rc_v1
    if args.version in ("v2", "both"):
        print("=== Downloading V2 (TriSlot decoder) ===")
        rc_v2 = run_v2(args)
        rc = rc or rc_v2
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
