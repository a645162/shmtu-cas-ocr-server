#!/usr/bin/env python3
"""Download the V2 (TriSlot decoder) SHMTU CAS OCR weights from the GitHub release.

V2 ships a single mobilenet_v3_small backbone with a trislot decoder head that
emits three logits blobs (digit_left, operator, digit_right) in one forward
pass.  This script downloads the model from
``https://github.com/a645162/shmtu-cas-ocr-model/releases/download/<tag>/``
using the ``model-assets.json`` manifest that the release pipeline produces
alongside the weights.

Usage::

    python3 scripts/download_models_v2.py
    python3 scripts/download_models_v2.py --include-fp32
    python3 scripts/download_models_v2.py --tag v2.0.2 --backbone mobilenet_v3_small
    python3 scripts/download_models_v2.py --dest /tmp/v2-models
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import sys
import tempfile
import urllib.error
import urllib.request
from pathlib import Path
from typing import Iterable

PROJECT_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_DEST = PROJECT_ROOT / "models" / "v2"

DEFAULT_BASE_URL = (
    "https://github.com/a645162/shmtu-cas-ocr-model/releases/download"
)
# Fallback tag when GitHub API is unreachable / rate-limited.
# 程序声明支持到 v{MAX_SUPPORTED_MAJOR}.{MAX_SUPPORTED_MINOR}.x,
# 启动时通过 GitHub releases API 自动选范围内最新 patch,失败回退到此 tag。
DEFAULT_TAG = "v2.0.2"
MAX_SUPPORTED_MAJOR = 2
MAX_SUPPORTED_MINOR = 0
GITHUB_REPO = "a645162/shmtu-cas-ocr-model"
GITHUB_RELEASES_API = f"https://api.github.com/repos/{GITHUB_REPO}/releases"
TAG_PATTERN = re.compile(r"^v(\d+)\.(\d+)\.(\d+)$")
DEFAULT_BACKBONE = "mobilenet_v3_small"
DEFAULT_ENGINE = "ncnn"
DEFAULT_PRECISION = "fp16"
MANIFEST_FILENAME = "model-assets.json"
MAX_RETRIES = 3
CHUNK = 64 * 1024


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Download SHMTU CAS OCR V2 (TriSlot) NCNN weights."
    )
    parser.add_argument(
        "--tag",
        default=None,
        help=(
            "Release tag. By default auto-resolves the newest tag whose "
            f"major <= {MAX_SUPPORTED_MAJOR} and minor <= {MAX_SUPPORTED_MINOR} "
            f"(i.e. v{MAX_SUPPORTED_MAJOR}.{MAX_SUPPORTED_MINOR}.x). "
            "Set MAX_SUPPORTED_MINOR to a negative value in the source to "
            "lock only the major version. Falls back to v2.0.2 on failure."
        ),
    )
    parser.add_argument(
        "--backbone",
        default=DEFAULT_BACKBONE,
        help=f"Backbone name (default: {DEFAULT_BACKBONE})",
    )
    parser.add_argument(
        "--engine",
        default=DEFAULT_ENGINE,
        help=f"Inference engine (default: {DEFAULT_ENGINE})",
    )
    parser.add_argument(
        "--precision",
        default=DEFAULT_PRECISION,
        help=f"Primary precision (default: {DEFAULT_PRECISION})",
    )
    parser.add_argument(
        "--include-fp32",
        action="store_true",
        help="Also download fp32 weights alongside the primary precision.",
    )
    parser.add_argument(
        "--dest",
        type=Path,
        default=DEFAULT_DEST,
        help=f"Target directory (default: {DEFAULT_DEST})",
    )
    parser.add_argument(
        "--base-url",
        default=DEFAULT_BASE_URL,
        help="Release base URL (default: GitHub releases)",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Redownload and replace existing files.",
    )
    return parser.parse_args()


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fp:
        for chunk in iter(lambda: fp.read(CHUNK), b""):
            h.update(chunk)
    return h.hexdigest()


def download_bytes(url: str, timeout: int = 60) -> bytes:
    req = urllib.request.Request(
        url,
        headers={
            "User-Agent": "shmtu-cas-ocr-server-downloader",
            "Accept": "application/vnd.github+json",
        },
    )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.read()


def resolve_latest_tag(
    *,
    max_major: int = MAX_SUPPORTED_MAJOR,
    max_minor: int = MAX_SUPPORTED_MINOR,
    fallback: str = DEFAULT_TAG,
) -> str:
    """Pick the newest release tag matching v{max_major}.{<=max_minor}.x.

    Falls back to ``fallback`` when the GitHub API is unreachable or no
    matching tag is found. v1 tags (and any tag not matching the pattern)
    are ignored.
    """
    try:
        # GitHub returns up to 100 releases per page; that's enough for our
        # release cadence and avoids paging complexity.
        raw = download_bytes(f"{GITHUB_RELEASES_API}?per_page=100", timeout=30)
        releases = json.loads(raw.decode("utf-8"))
    except (urllib.error.HTTPError, urllib.error.URLError, TimeoutError) as exc:
        print(
            f"[v2] warning: cannot list GitHub releases ({exc}); "
            f"falling back to {fallback}"
        )
        return fallback
    except (json.JSONDecodeError, ValueError) as exc:
        print(
            f"[v2] warning: GitHub releases response is not valid JSON ({exc}); "
            f"falling back to {fallback}"
        )
        return fallback

    candidates: list[tuple[int, int, int, str]] = []
    for release in releases:
        if not isinstance(release, dict):
            continue
        if release.get("draft") or release.get("prerelease"):
            continue
        tag = release.get("tag_name") or ""
        m = TAG_PATTERN.match(tag)
        if not m:
            continue
        major, minor, patch = (int(g) for g in m.groups())
        if major != max_major:
            continue
        # max_minor < 0 means "no minor bound" (only major is locked).
        # max_minor == 0 means "lock major+minor" (e.g. v2.0.x).
        # max_minor > 0 means "lock major, allow minor up to N" (e.g. v2.x.x for 0<=x<=N).
        if max_minor >= 0 and minor > max_minor:
            continue
        candidates.append((major, minor, patch, tag))

    if not candidates:
        if max_minor < 0:
            filter_desc = f"v{max_major}.x.x"
        else:
            filter_desc = f"v{max_major}.{max_minor}.x"
        print(
            f"[v2] warning: no release matched {filter_desc}; "
            f"falling back to {fallback}"
        )
        return fallback

    candidates.sort(reverse=True)
    chosen = candidates[0][3]
    if max_minor < 0:
        filter_desc = f"v{max_major}.x.x"
    else:
        filter_desc = f"v{max_major}.{max_minor}.x"
    print(
        f"[v2] resolved latest tag: {chosen} "
        f"(filter: {filter_desc}, {len(candidates)} candidates)"
    )
    return chosen


def fetch_manifest(base_url: str, tag: str) -> dict | None:
    url = f"{base_url.rstrip('/')}/{tag}/{MANIFEST_FILENAME}"
    print(f"[v2] fetching manifest: {url}")
    try:
        return json.loads(download_bytes(url).decode("utf-8"))
    except (urllib.error.HTTPError, urllib.error.URLError) as exc:
        print(f"[v2] warning: could not download manifest ({exc}); aborting")
        return None
    except json.JSONDecodeError as exc:
        print(f"[v2] warning: manifest is not valid JSON ({exc})")
        return None


def select_artifacts(
    manifest: dict,
    backbone: str,
    engine: str,
    precisions: Iterable[str],
) -> list[dict]:
    """Return the artifact entries that match (backbone, engine, precision)."""
    selected: list[dict] = []
    for artifact in manifest.get("artifacts", []):
        if artifact.get("engine") != engine:
            continue
        if artifact.get("backbone") != backbone:
            continue
        if artifact.get("precision") not in precisions:
            continue
        selected.append(artifact)
    return selected


def download_file(url: str, destination: Path) -> None:
    def report_progress(blocks: int, block_size: int, total_size: int) -> None:
        if total_size <= 0:
            return
        downloaded = min(blocks * block_size, total_size)
        percent = downloaded * 100 // total_size
        print(
            f"\r  {destination.name}: {percent:3d}% "
            f"({downloaded}/{total_size} bytes)",
            end="",
        )

    with tempfile.NamedTemporaryFile(
        prefix=f"{destination.name}.", suffix=".tmp", delete=False
    ) as tmp:
        tmp_path = Path(tmp.name)

    try:
        urllib.request.urlretrieve(url, tmp_path, reporthook=report_progress)
        print()
        shutil.move(tmp_path, destination)
    except urllib.error.HTTPError as exc:
        print()
        raise RuntimeError(f"HTTP error {exc.code} for {url}") from exc
    except urllib.error.URLError as exc:
        print()
        raise RuntimeError(f"Network error for {url}: {exc.reason}") from exc
    except Exception:
        print()
        raise
    finally:
        if tmp_path.exists():
            tmp_path.unlink()


def download_file_with_verify(
    url: str,
    destination: Path,
    expected_hash: str | None,
    retries: int = MAX_RETRIES,
) -> None:
    for attempt in range(1, retries + 1):
        download_file(url, destination)
        if expected_hash is None:
            return
        actual = sha256_file(destination)
        if actual == expected_hash:
            return
        print(
            f"  SHA256 mismatch for {destination.name} "
            f"(attempt {attempt}/{retries}): "
            f"expected {expected_hash[:16]}..., got {actual[:16]}..."
        )
        if attempt < retries:
            destination.unlink(missing_ok=True)
    raise RuntimeError(
        f"SHA256 verification failed for {destination.name} after {retries} attempts"
    )


def run() -> int:
    args = parse_args()
    destination_dir = args.dest.resolve()
    destination_dir.mkdir(parents=True, exist_ok=True)

    # Resolve tag automatically when the user didn't pin one.
    tag = args.tag or resolve_latest_tag()

    print(f"[v2] destination:  {destination_dir}")
    print(f"[v2] tag:          {tag}")
    print(f"[v2] backbone:     {args.backbone}")
    print(f"[v2] engine:       {args.engine}")
    print(f"[v2] primary:      {args.precision}")
    print(f"[v2] include-fp32: {args.include_fp32}")

    manifest = fetch_manifest(args.base_url, tag)
    if manifest is None:
        return 1

    precisions = [args.precision]
    if args.include_fp32 and "fp32" not in precisions:
        precisions.append("fp32")

    artifacts = select_artifacts(manifest, args.backbone, args.engine, precisions)
    if not artifacts:
        print(
            f"[v2] no artifacts matched "
            f"backbone={args.backbone} engine={args.engine} precisions={precisions}"
        )
        return 1

    downloaded_count = 0
    skipped_count = 0

    for artifact in artifacts:
        precision = artifact["precision"]
        artifact_stem = artifact.get("asset_stem") or f"{args.backbone}.trislot_decoder"
        for entry in artifact.get("files", []):
            release_asset_name = entry["release_asset_name"]
            expected_hash = entry.get("sha256")
            destination = destination_dir / release_asset_name

            if destination.exists() and not args.force:
                if expected_hash is not None:
                    actual = sha256_file(destination)
                    if actual != expected_hash:
                        print(
                            f"[v2]   existing {release_asset_name} has wrong SHA256, "
                            f"redownloading"
                        )
                        destination.unlink()
                    else:
                        print(f"[v2]   skip existing (verified): {destination.name}")
                        skipped_count += 1
                        continue
                else:
                    print(f"[v2]   skip existing: {destination.name}")
                    skipped_count += 1
                    continue

            url = f"{args.base_url.rstrip('/')}/{tag}/{release_asset_name}"
            print(
                f"[v2]   downloading [{precision}]: {release_asset_name} "
                f"(stem={artifact_stem})"
            )
            download_file_with_verify(url, destination, expected_hash)
            downloaded_count += 1

    print(
        f"[v2] done. downloaded={downloaded_count} skipped={skipped_count} "
        f"dir={destination_dir}"
    )
    return 0


def main() -> int:
    try:
        return run()
    except KeyboardInterrupt:
        print("\nCancelled by user.", file=sys.stderr)
        return 130
    except RuntimeError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
