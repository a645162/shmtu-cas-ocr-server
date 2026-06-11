// SPDX-License-Identifier: MIT


#pragma once

#include <shmtu/cas_ocr/types.h>

#include <string>
#include <vector>

namespace shmtu::cas::ocr::tui {

/// Bundle of manifests for multiple release tags (used for offline
/// cache or testing).  Each entry pairs a tag with the raw JSON body
/// of the release manifest at that tag.
struct ManifestBundle {
    struct Entry {
        std::string tag;
        std::string json_body;  // raw model-assets.json text
    };
    std::vector<Entry> entries;
};

/// Utility that returns true when `tag` contains a `v2` prefix (case-
/// insensitive).  This is the same gate the TUI uses to decide
/// whether to download a manifest — it avoids fetching assets from
/// tags that existed before the manifest schema was introduced.
inline bool tagSupportsManifest(std::string_view tag) {
    if (tag.empty()) return false;
    auto s = tag;
    if (s.front() == 'v' || s.front() == 'V') s.remove_prefix(1);
    return s.starts_with("2");
}

}  // namespace shmtu::cas::ocr::tui
