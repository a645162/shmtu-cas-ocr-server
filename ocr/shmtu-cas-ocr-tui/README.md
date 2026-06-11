# shmtu-cas-ocr-tui

Interactive terminal UI for browsing, inspecting, and downloading the
`a645162/shmtu-cas-ocr-model` releases from GitHub.

## What it does

* Lists every v2 release tag (semver `v{major}.{minor}.{patch}` with
  `major <= 2`).
* For the selected tag, fetches the release's `model-assets.json`
  manifest and renders one row per logical `ModelInfo` (backbone,
  display name, parameter count, validation/test accuracy, file
  count, supported engines).
* Downloads the chosen (engine, precision) artifact into
  `$XDG_CACHE_HOME/shmtu-cas-ocr/<tag>/<asset_stem>/` (falling back
  to `~/.cache/...`).
* Honors `SHMTU_USE_GITEE=1` to switch the primary download mirror
  to Gitee.

The network side reuses the lib-level `shmtu::cas::ocr::curlutil`
helpers — the same ones the GUI uses — so error handling, follow
redirects, and timeouts are consistent across binaries.

## Build

TUI is enabled by default in vcpkg mode if `ftxui` is available.
Toggle it explicitly with:

```bash
cmake -B build -S . -DBUILD_TUI=ON \
    -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build --target shmtu_cas_ocr_tui
```

The resulting binary is `build/ocr/shmtu-cas-ocr-tui/shmtu_cas_ocr_tui`.

## Key bindings

| Key  | Action                                |
| ---- | ------------------------------------- |
| `q`  | Quit                                  |
| `r`  | Refresh the release list              |
| `d`  | Download the currently selected model |

## Anonymous API access

Listing releases and downloading assets hit the GitHub/Gitee public
APIs anonymously (60 requests/hour per IP per the GitHub docs). No
token is required.
