// SPDX-License-Identifier: MIT


#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace shmtu::cas::ocr::tui {

// A parsed semantic version.  Only the major/minor/patch triple and
// the raw tag string are stored; pre-release / build metadata are
// captured as opaque strings for sorting.
struct SemVer {
    int major = 0;
    int minor = 0;
    int patch = 0;
    std::string prerelease;   // e.g. "rc.1" (empty when absent)
    std::string build_meta;   // e.g. "20250101" (empty when absent)
    std::string raw;          // the original tag (e.g. "v2.0.1")
};

// True when `tag` looks like a semantic version with an optional
// leading "v" (e.g. "v2.0.1", "2.0.0-rc.1").  When `strict` is true
// the entire tag must be a semver string; otherwise any leading "v"
// is allowed.
bool looksLikeSemVer(std::string_view tag, bool strict = false);

// Parse `tag` into a `SemVer`.  Returns `std::nullopt` on failure.
std::optional<SemVer> parseSemVer(std::string_view tag);

// Compare two versions following semver 2.0.0 rules.
//   <0 if a<b, 0 if a==b, >0 if a>b.
int compareSemVer(const SemVer& a, const SemVer& b);

// Convenience overload that takes raw tag strings.  Unparseable
// inputs sort last (return value >= 0 vs. parseable peer).
int compareSemVerStrings(std::string_view a, std::string_view b);

// Stable sort `tags` in descending semver order (largest first).
// Non-semver tags are placed at the end in their original order.
void sortTagsDescending(std::vector<std::string>& tags);

}  // namespace shmtu::cas::ocr::tui
