#!/usr/bin/env python3
"""Download and extract a prebuilt ncnn release for Ubuntu."""

from __future__ import annotations

import argparse
import json
import shutil
import sys
import tempfile
import urllib.error
import urllib.request
import zipfile
from pathlib import Path

DEFAULT_UBUNTU_RELEASE = "2404"
LATEST_RELEASE_API = "https://api.github.com/repos/Tencent/ncnn/releases/latest"
LATEST_RELEASE_REDIRECT = "https://github.com/Tencent/ncnn/releases/latest"
UBUNTU_OPTIONS = {
    "1": "2204",
    "2": "2404",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Download a prebuilt ncnn package from GitHub Releases."
    )
    parser.add_argument(
        "--tag",
        help="ncnn release tag to download; if omitted, resolve the latest GitHub release",
    )
    parser.add_argument(
        "--ubuntu",
        choices=sorted(set(UBUNTU_OPTIONS.values())),
        default=DEFAULT_UBUNTU_RELEASE,
        help=f"Ubuntu release suffix to download (default: {DEFAULT_UBUNTU_RELEASE})",
    )
    parser.add_argument(
        "--dest",
        type=Path,
        default=Path(__file__).resolve().parent,
        help="Directory where the archive will be extracted",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Replace the extracted directory if it already exists",
    )
    parser.add_argument(
        "--interactive",
        action="store_true",
        help="Prompt for the Ubuntu release instead of using --ubuntu/default",
    )
    return parser.parse_args()


def prompt_ubuntu_release() -> str:
    print("Select the Ubuntu package to download:")
    print("  1. Ubuntu 22.04")
    print("  2. Ubuntu 24.04")
    while True:
        choice = input("Enter choice [1/2]: ").strip()
        value = UBUNTU_OPTIONS.get(choice)
        if value:
            return value
        print("Invalid selection. Please enter 1 or 2.")


def build_download_url(tag: str, ubuntu_release: str) -> tuple[str, str]:
    archive_name = f"ncnn-{tag}-ubuntu-{ubuntu_release}.zip"
    url = f"https://github.com/Tencent/ncnn/releases/download/{tag}/{archive_name}"
    return archive_name, url


def resolve_latest_release_tag() -> str:
    request = urllib.request.Request(
        LATEST_RELEASE_API,
        headers={
            "Accept": "application/vnd.github+json",
            "User-Agent": "shmtu-cas-ocr-server-ncnn-downloader",
        },
    )
    try:
        with urllib.request.urlopen(request) as response:
            payload = json.load(response)
        tag = payload.get("tag_name", "").strip()
        if tag:
            return tag
    except (urllib.error.HTTPError, urllib.error.URLError, json.JSONDecodeError):
        pass

    redirect_request = urllib.request.Request(
        LATEST_RELEASE_REDIRECT,
        headers={"User-Agent": "shmtu-cas-ocr-server-ncnn-downloader"},
        method="HEAD",
    )
    try:
        with urllib.request.urlopen(redirect_request) as response:
            final_url = response.geturl().rstrip("/")
    except urllib.error.HTTPError as exc:
        raise RuntimeError(
            f"Failed to resolve latest ncnn release tag: HTTP {exc.code}"
        ) from exc
    except urllib.error.URLError as exc:
        raise RuntimeError(f"Failed to resolve latest ncnn release tag: {exc.reason}") from exc

    tag = final_url.rsplit("/", 1)[-1].strip()
    if not tag or tag == "latest":
        raise RuntimeError("Failed to resolve latest ncnn release tag from GitHub redirect")
    return tag


def download_file(url: str, output_path: Path) -> None:
    def report_progress(blocks: int, block_size: int, total_size: int) -> None:
        if total_size <= 0:
            return
        downloaded = min(blocks * block_size, total_size)
        percent = downloaded * 100 // total_size
        print(f"\rDownloading... {percent:3d}% ({downloaded}/{total_size} bytes)", end="")

    try:
        urllib.request.urlretrieve(url, output_path, reporthook=report_progress)
    except urllib.error.HTTPError as exc:
        print("\nDownload failed.")
        raise RuntimeError(f"HTTP error {exc.code} for {url}") from exc
    except urllib.error.URLError as exc:
        print("\nDownload failed.")
        raise RuntimeError(f"Network error for {url}: {exc.reason}") from exc
    else:
        print()


def extract_archive(archive_path: Path, destination_dir: Path, force: bool) -> Path:
    with zipfile.ZipFile(archive_path) as archive:
        root_entries = [Path(name).parts[0] for name in archive.namelist() if name.strip()]
        if not root_entries:
            raise RuntimeError("Archive is empty.")
        root_dir_name = root_entries[0]
        extract_target = destination_dir / root_dir_name

        if extract_target.exists():
            if not force:
                raise RuntimeError(
                    f"Destination already exists: {extract_target}. Use --force to replace it."
                )
            shutil.rmtree(extract_target)

        archive.extractall(destination_dir)
        return extract_target


def infer_cmake_package_dir(extracted_dir: Path) -> Path:
    candidates = [
        extracted_dir / "lib" / "cmake" / "ncnn",
        extracted_dir / "lib64" / "cmake" / "ncnn",
        extracted_dir / "share" / "ncnn",
        extracted_dir,
    ]
    for candidate in candidates:
        if (candidate / "ncnnConfig.cmake").is_file() or (candidate / "ncnn-config.cmake").is_file():
            return candidate
    return extracted_dir


def main() -> int:
    args = parse_args()
    ubuntu_release = prompt_ubuntu_release() if args.interactive else args.ubuntu
    release_tag = args.tag or resolve_latest_release_tag()

    archive_name, url = build_download_url(release_tag, ubuntu_release)
    destination_dir = args.dest.resolve()
    destination_dir.mkdir(parents=True, exist_ok=True)
    extracted_dir_hint = destination_dir / Path(archive_name).stem

    print(f"Release tag: {release_tag}")
    print(f"Ubuntu package: {ubuntu_release}")
    print(f"Destination: {destination_dir}")
    print(f"URL: {url}")

    if extracted_dir_hint.exists() and not args.force:
        cmake_package_dir = infer_cmake_package_dir(extracted_dir_hint)
        print(f"Reusing existing extraction: {extracted_dir_hint}")
        print(f"CMake package dir: {cmake_package_dir}")
        print("This project can auto-detect the extracted package from 3rdparty/NCNN/.")
        print("Optional explicit hint:")
        print(f"  export NCNN_ROOT={extracted_dir_hint}")
        print("Recommended system-package build flow:")
        print("  cmake --preset linux-system-vulkan")
        print("  cmake --build --preset build-linux-system-vulkan")
        print("Done.")
        return 0

    with tempfile.TemporaryDirectory(prefix="ncnn-download-") as temp_dir:
        archive_path = Path(temp_dir) / archive_name
        download_file(url, archive_path)
        extracted_dir = extract_archive(archive_path, destination_dir, args.force)
        cmake_package_dir = infer_cmake_package_dir(extracted_dir)

    print(f"Extracted to: {extracted_dir}")
    print(f"CMake package dir: {cmake_package_dir}")
    print("This project can auto-detect the extracted package from 3rdparty/NCNN/.")
    print("Optional explicit hint:")
    print(f"  export NCNN_ROOT={extracted_dir}")
    print("Recommended system-package build flow:")
    print("  cmake --preset linux-system-vulkan")
    print("  cmake --build --preset build-linux-system-vulkan")
    print("Done.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\nCancelled by user.")
        raise SystemExit(130)
    except RuntimeError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        raise SystemExit(1)
