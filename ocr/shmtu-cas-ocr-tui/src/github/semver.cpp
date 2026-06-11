// SPDX-License-Identifier: MIT
#include "github/semver.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace shmtu::cas::ocr::tui {

namespace {

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

bool isSemverChar(char c) {
    return isDigit(c) || c == '.' || c == '-' || c == '+' || std::isalnum(static_cast<unsigned char>(c));
}

}  // namespace

bool looksLikeSemVer(std::string_view tag, bool strict) {
    if (tag.empty()) return false;
    auto s = tag;
    if (!strict && s.front() == 'v') {
        s.remove_prefix(1);
    }
    if (s.empty()) return false;
    // Must start with MAJOR.MINOR.PATCH
    auto dot1 = s.find('.');
    if (dot1 == std::string_view::npos) return false;
    auto dot2 = s.find('.', dot1 + 1);
    if (dot2 == std::string_view::npos) return false;

    auto isDigits = [](std::string_view v) {
        if (v.empty()) return false;
        for (char c : v) {
            if (!isDigit(c)) return false;
        }
        return true;
    };

    if (!isDigits(s.substr(0, dot1))) return false;
    if (!isDigits(s.substr(dot1 + 1, dot2 - dot1 - 1))) return false;

    auto rest = s.substr(dot2 + 1);
    // patch part can be digits, possibly followed by "-" or "+".
    auto split = rest.find_first_of("-+");
    std::string_view patch_part;
    if (split == std::string_view::npos) {
        patch_part = rest;
    } else {
        patch_part = rest.substr(0, split);
    }
    if (!isDigits(patch_part)) return false;

    // After the patch part, only valid semver chars are allowed.
    for (char c : rest) {
        if (!isSemverChar(c)) return false;
    }
    return true;
}

std::optional<SemVer> parseSemVer(std::string_view tag) {
    if (tag.empty()) return std::nullopt;
    auto s = tag;
    if (s.front() == 'v' || s.front() == 'V') {
        s.remove_prefix(1);
    }
    if (s.empty()) return std::nullopt;

    auto dot1 = s.find('.');
    if (dot1 == std::string_view::npos) return std::nullopt;
    auto dot2 = s.find('.', dot1 + 1);
    if (dot2 == std::string_view::npos) return std::nullopt;

    SemVer out;
    out.raw = std::string(tag);

    auto parseInt = [](std::string_view v) -> std::optional<int> {
        if (v.empty()) return std::nullopt;
        int value = 0;
        for (char c : v) {
            if (!isDigit(c)) return std::nullopt;
            value = value * 10 + (c - '0');
        }
        return value;
    };

    auto major = parseInt(s.substr(0, dot1));
    auto minor = parseInt(s.substr(dot1 + 1, dot2 - dot1 - 1));
    if (!major || !minor) return std::nullopt;
    out.major = *major;
    out.minor = *minor;

    auto rest = s.substr(dot2 + 1);
    auto pre = rest.find('-');
    auto build = rest.find('+');
    if (pre != std::string_view::npos &&
        (build == std::string_view::npos || pre < build)) {
        auto patch_part = rest.substr(0, pre);
        auto patch = parseInt(patch_part);
        if (!patch) return std::nullopt;
        out.patch = *patch;
        if (build != std::string_view::npos) {
            out.prerelease = std::string(rest.substr(pre + 1, build - pre - 1));
            out.build_meta = std::string(rest.substr(build + 1));
        } else {
            out.prerelease = std::string(rest.substr(pre + 1));
        }
    } else if (build != std::string_view::npos) {
        auto patch_part = rest.substr(0, build);
        auto patch = parseInt(patch_part);
        if (!patch) return std::nullopt;
        out.patch = *patch;
        out.build_meta = std::string(rest.substr(build + 1));
    } else {
        auto patch = parseInt(rest);
        if (!patch) return std::nullopt;
        out.patch = *patch;
    }
    return out;
}

namespace {

// Split a dot-delimited string into its segments.
std::vector<std::string> splitDots(std::string_view s) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (true) {
        auto nxt = s.find('.', pos);
        if (nxt == std::string_view::npos) {
            out.emplace_back(s.substr(pos));
            break;
        }
        out.emplace_back(s.substr(pos, nxt - pos));
        pos = nxt + 1;
    }
    return out;
}

// True if every character in `s` is an ASCII digit.
bool allDigits(std::string_view s) {
    return !s.empty() && std::all_of(s.begin(), s.end(),
        [](unsigned char c) { return std::isdigit(c); });
}

// Compare two pre-release identifiers per semver 2.0.0 sect. 11.
int comparePrerelease(std::string_view a, std::string_view b) {
    auto segsA = splitDots(a);
    auto segsB = splitDots(b);
    std::size_t maxSegs = std::max(segsA.size(), segsB.size());
    for (std::size_t i = 0; i < maxSegs; ++i) {
        if (i >= segsA.size()) return -1;
        if (i >= segsB.size()) return 1;
        bool aDigits = allDigits(segsA[i]);
        bool bDigits = allDigits(segsB[i]);
        if (aDigits && bDigits) {
            long long va, vb;
            try {
                va = std::stoll(segsA[i]);
                vb = std::stoll(segsB[i]);
            } catch (const std::exception&) {
                int cmp = segsA[i].compare(segsB[i]);
                if (cmp != 0) return cmp;
                continue;
            }
            if (va != vb) return va < vb ? -1 : 1;
        } else if (aDigits != bDigits) {
            return aDigits ? -1 : 1;
        } else {
            int cmp = segsA[i].compare(segsB[i]);
            if (cmp != 0) return cmp;
        }
    }
    return 0;
}

}  // namespace

int compareSemVer(const SemVer& a, const SemVer& b) {
    if (a.major != b.major) return a.major - b.major;
    if (a.minor != b.minor) return a.minor - b.minor;
    if (a.patch != b.patch) return a.patch - b.patch;
    // Semver 2.0.0: a version with pre-release has lower precedence
    // than the same version without.  Two pre-release strings are
    // compared lexically (numeric segments compared numerically).
    bool aHasPre = !a.prerelease.empty();
    bool bHasPre = !b.prerelease.empty();
    if (aHasPre != bHasPre) {
        return aHasPre ? -1 : 1;
    }
    if (aHasPre) {
        return comparePrerelease(a.prerelease, b.prerelease);
    }
    return 0;
}

int compareSemVerStrings(std::string_view a, std::string_view b) {
    auto pa = parseSemVer(a);
    auto pb = parseSemVer(b);
    if (!pa && !pb) {
        // Both unparseable - fall back to lexical.
        const int cmp = a.compare(b);
        return cmp == 0 ? 0 : (cmp < 0 ? 1 : -1);
    }
    if (!pa) return 1;   // unparseable sorts last
    if (!pb) return -1;
    return compareSemVer(*pa, *pb);
}

void sortTagsDescending(std::vector<std::string>& tags) {
    std::stable_sort(tags.begin(), tags.end(),
                     [](const std::string& a, const std::string& b) {
                         return compareSemVerStrings(a, b) < 0;
                     });
}

}  // namespace shmtu::cas::ocr::tui
