#!/usr/bin/env python3
"""Download the default NCNN model weights used by this project."""

from __future__ import annotations

import argparse
import hashlib
import shutil
import sys
import tempfile
import urllib.error
import urllib.request
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_DEST = PROJECT_ROOT / "models"
DEFAULT_BASE_URL = (
    "https://github.com/a645162/shmtu-cas-ocr-model/releases/download/v1.0-NCNN"
)
DEFAULT_FILES = (
    "resnet18_operator_latest.fp16.bin",
    "resnet18_operator_latest.fp16.param",
    "resnet18_equal_symbol_latest.fp16.bin",
    "resnet18_equal_symbol_latest.fp16.param",
    "resnet34_digit_latest.fp16.bin",
    "resnet34_digit_latest.fp16.param",
)

MAX_RETRIES = 3
CHECKSUM_FILENAME = "SHA256SUMS.txt"
CHUNK = 64 * 1024


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Download SHMTU CAS OCR model weights."
    )
    parser.add_argument(
        "--dest",
        type=Path,
        default=DEFAULT_DEST,
        help=f"Target directory for downloaded weights (default: {DEFAULT_DEST})",
    )
    parser.add_argument(
        "--base-url",
        default=DEFAULT_BASE_URL,
        help=f"Release base URL (default: {DEFAULT_BASE_URL})",
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


def fetch_checksums(base_url: str) -> dict[str, str]:
    """Download SHA256SUMS.txt from the release and parse it."""
    url = f"{base_url.rstrip('/')}/{CHECKSUM_FILENAME}"
    try:
        with urllib.request.urlopen(url, timeout=30) as resp:
            text = resp.read().decode("utf-8")
    except urllib.error.HTTPError:
        return {}
    except urllib.error.URLError:
        return {}

    result: dict[str, str] = {}
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(None, 1)
        if len(parts) != 2:
            continue
        hex_digest, filename = parts
        filename = filename.lstrip("*")
        result[filename] = hex_digest.lower()
    return result


def download_file(url: str, destination: Path) -> None:
    def report_progress(blocks: int, block_size: int, total_size: int) -> None:
        if total_size <= 0:
            return
        downloaded = min(blocks * block_size, total_size)
        percent = downloaded * 100 // total_size
        print(
            f"\r  {destination.name}: {percent:3d}% ({downloaded}/{total_size} bytes)",
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
    url: str, destination: Path, expected_hash: str | None, retries: int = MAX_RETRIES
) -> None:
    """Download a file and verify its SHA256. Retry up to *retries* times on mismatch."""
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


def main() -> int:
    args = parse_args()
    destination_dir = args.dest.resolve()
    destination_dir.mkdir(parents=True, exist_ok=True)

    print(f"Destination: {destination_dir}")
    print(f"Base URL: {args.base_url}")

    checksums = fetch_checksums(args.base_url)
    if checksums:
        print(f"Loaded {len(checksums)} expected checksums from {CHECKSUM_FILENAME}")
    else:
        print(f"Warning: could not fetch {CHECKSUM_FILENAME}, skipping verification")

    downloaded_count = 0
    skipped_count = 0

    for filename in DEFAULT_FILES:
        destination = destination_dir / filename
        if destination.exists() and not args.force:
            expected = checksums.get(filename)
            if expected is not None:
                actual = sha256_file(destination)
                if actual != expected:
                    print(f"  Existing {filename} has wrong SHA256, redownloading")
                    destination.unlink()
                else:
                    print(f"Skip existing (verified): {destination.name}")
                    skipped_count += 1
                    continue
            else:
                print(f"Skip existing: {destination.name}")
                skipped_count += 1
                continue

        url = f"{args.base_url.rstrip('/')}/{filename}"
        expected_hash = checksums.get(filename)
        print(f"Downloading: {filename}")
        download_file_with_verify(url, destination, expected_hash)
        downloaded_count += 1

    print(
        f"Done. downloaded={downloaded_count} skipped={skipped_count} dir={destination_dir}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\nCancelled by user.", file=sys.stderr)
        raise SystemExit(130)
    except RuntimeError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        raise SystemExit(1)
