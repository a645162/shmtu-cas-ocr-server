// SPDX-License-Identifier: MIT
#include "github/github_client.h"

#include "github/semver.h"
#include "http/http_client.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>

namespace shmtu::cas::ocr::tui {

namespace {

// Light-weight JSON string unescape.  We only need to handle the
// sequences the GitHub release API actually emits, which are mostly
// printable ASCII.  Anything else is passed through as the source
// byte.
std::string unescapeJsonString(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (c == '\\' && i + 1 < in.size()) {
            char n = in[++i];
            switch (n) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'u':
                    // We don't expect unicode in tag_name; bail.
                    if (i + 4 < in.size()) {
                        i += 4;
                    }
                    out.push_back('?');
                    break;
                default:
                    out.push_back(n);
                    break;
            }
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string trim(std::string_view s) {
    auto begin = s.begin();
    auto end = s.end();
    while (begin != end && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }
    while (end != begin) {
        auto prev = std::prev(end);
        if (!std::isspace(static_cast<unsigned char>(*prev))) break;
        end = prev;
    }
    return std::string(begin, end);
}

std::string extractStringField(const std::string& json,
                               const std::string& field) {
    const std::string needle = "\"" + field + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return {};
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    if (pos >= json.size() || json[pos] != '"') return {};
    ++pos;
    std::string raw;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            raw.push_back(json[pos]);
            raw.push_back(json[pos + 1]);
            pos += 2;
        } else {
            raw.push_back(json[pos]);
            ++pos;
        }
    }
    return unescapeJsonString(raw);
}

bool extractBoolField(const std::string& json, const std::string& field) {
    const std::string needle = "\"" + field + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    if (pos + 4 <= json.size() && json.compare(pos, 4, "true") == 0) {
        return true;
    }
    return false;
}

std::vector<std::string> findTopLevelObjects(const std::string& json) {
    std::vector<std::string> out;
    auto pos = json.find('[');
    if (pos == std::string::npos) return out;
    auto end = json.rfind(']');
    if (end == std::string::npos) return out;

    bool in_string = false;
    int depth = 0;
    std::size_t obj_start = std::string::npos;
    for (std::size_t i = pos + 1; i < end; ++i) {
        char c = json[i];
        if (in_string) {
            if (c == '\\' && i + 1 < json.size()) {
                ++i;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
        } else if (c == '{') {
            if (depth == 0) obj_start = i;
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0 && obj_start != std::string::npos) {
                out.emplace_back(json.substr(obj_start, i - obj_start + 1));
                obj_start = std::string::npos;
            }
        }
    }
    return out;
}

}  // namespace

GitHubClient::GitHubClient() = default;

std::vector<ReleaseSummary> GitHubClient::listReleases(
    int max_major, long& http_status, std::string& error_message) {
    std::vector<ReleaseSummary> releases;
    HttpClient http;

    // Try Gitee first, then fall back to GitHub.
    const struct { const char* name; const char* base; } sources[] = {
        {"gitee", kGiteeApiBase},
        {"github", kGitHubApiBase},
    };

    for (const auto& src : sources) {
        const std::string url =
            std::string(src.base) + "/releases?per_page=100";
        long status = 0;
        std::string err;
        std::string body = http.getText(url, status, err);
        if (body.empty() || status != 200) {
            http_status = status;
            error_message = err;
            continue;
        }

        for (const auto& obj : findTopLevelObjects(body)) {
            ReleaseSummary r;
            r.tag = trim(extractStringField(obj, "tag_name"));
            r.name = trim(extractStringField(obj, "name"));
            r.published_at = trim(extractStringField(obj, "published_at"));
            r.html_url = trim(extractStringField(obj, "html_url"));
            r.prerelease = extractBoolField(obj, "prerelease");
            r.draft = extractBoolField(obj, "draft");
            if (r.tag.empty()) continue;

            if (max_major > 0) {
                auto sv = parseSemVer(r.tag);
                if (sv && sv->major > max_major) continue;
            }
            releases.push_back(std::move(r));
        }

        if (!releases.empty()) {
            // Sort newest first.
            std::stable_sort(releases.begin(), releases.end(),
                             [](const ReleaseSummary& a, const ReleaseSummary& b) {
                                 return compareSemVerStrings(a.tag, b.tag) < 0;
                             });
            http_status = 200;
            error_message.clear();
            return releases;
        }
    }

    return releases;
}

std::string GitHubClient::fetchManifestJson(const std::string& tag,
                                            long& http_status,
                                            std::string& error_message) {
    HttpClient http;

    // Try Gitee first, then fall back to GitHub.
    const struct { const char* name; const char* base; } sources[] = {
        {"gitee", kGiteeReleasesBase},
        {"github", kGitHubReleasesBase},
    };

    for (const auto& src : sources) {
        const std::string url =
            std::string(src.base) + "/" + tag + "/model-assets.json";
        long status = 0;
        std::string err;
        std::string body = http.getText(url, status, err);
        if (!body.empty() && status == 200) {
            http_status = status;
            error_message.clear();
            return body;
        }
        http_status = status;
        error_message = err;
    }

    return {};
}

std::string GitHubClient::buildAssetUrl(const std::string& base,
                                        const std::string& tag,
                                        const std::string& asset_name) const {
    std::string lc = base;
    std::transform(lc.begin(), lc.end(), lc.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    const std::string root = (lc == "gitee") ? kGiteeReleasesBase
                                              : kGitHubReleasesBase;
    // Reject obviously-malformed path segments.  All known release tags
    // and asset names are printable ASCII; anything else is refused.
    auto safe = [](std::string_view s) -> bool {
        if (s.empty()) return false;
        for (unsigned char c : s) {
            if (c <= 0x1F || c >= 0x7F) return false;
            if (c == '/' || c == '\\') return false;
        }
        return true;
    };
    if (!safe(tag) || !safe(asset_name)) return {};
    return root + "/" + tag + "/" + asset_name;
}

std::string GitHubClient::defaultAssetCacheDir(const std::string& tag,
                                              const std::string& asset_stem) {
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    std::filesystem::path base;
    if (xdg && *xdg) {
        base = xdg;
    } else {
        const char* home = std::getenv("HOME");
        base = (home && *home) ? std::filesystem::path(home) / ".cache"
                               : std::filesystem::temp_directory_path();
    }
    return (base / "shmtu-cas-ocr" / tag / asset_stem).string();
}

std::string GitHubClient::normaliseTag(std::string_view tag) {
    std::string s = trim(tag);
    if (!s.empty() && (s.front() == 'v' || s.front() == 'V')) {
        s.erase(0, 1);
    }
    return s;
}

}  // namespace shmtu::cas::ocr::tui
