// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace shmtu::cas::ocr::tui {

// Lightweight description of a single release returned by the
// GitHub `/releases` endpoint.  We avoid pulling in a JSON
// dependency by doing a hand-rolled walker here too (it only has to
// recognise the few fields we care about).
struct ReleaseSummary {
    std::string tag;          // e.g. "v2.0.1"
    std::string name;         // release display name
    std::string published_at; // ISO 8601 timestamp
    bool prerelease = false;
    bool draft = false;
    std::string html_url;     // optional
};

// GitHub/Gitee API client used by the TUI.
//
// We treat GitHub as the canonical source.  Gitee is supported
// through the same `?per_page=...` query but the URL prefix is
// configurable.  All requests are anonymous (the public release
// endpoint allows 60 req/h/IP).
class GitHubClient {
public:
    // Default GitHub API base for the model repo.  Hard-coded because
    // the project has only ever published to this single repository.
    static constexpr auto kGitHubApiBase =
        "https://api.github.com/repos/a645162/shmtu-cas-ocr-model";
    static constexpr auto kGiteeApiBase =
        "https://gitee.com/api/v5/repos/a645162/shmtu-cas-ocr-model";
    static constexpr auto kGitHubReleasesBase =
        "https://github.com/a645162/shmtu-cas-ocr-model/releases/download";
    static constexpr auto kGiteeReleasesBase =
        "https://gitee.com/a645162/shmtu-cas-ocr-model/releases/download";

    GitHubClient();

    // Fetch a list of releases.  `max_major` filters out releases
    // whose semver major version exceeds it (defaults to 2 to match
    // the project's v1/v2 manifest layout).
    //
    // On success returns the (newest-first) summary list and leaves
    // `error_message` empty.  On failure returns an empty vector and
    // sets `error_message` / `http_status`.
    std::vector<ReleaseSummary> listReleases(
        int max_major,
        long& http_status,
        std::string& error_message);

    // Fetch the `model-assets.json` manifest for a single release
    // tag.  Returns an empty string on failure.
    std::string fetchManifestJson(const std::string& tag,
                                  long& http_status,
                                  std::string& error_message);

    // Build a release-asset download URL for a given tag and asset.
    // `base` is case-insensitive: "github" (default) or "gitee".
    std::string buildAssetUrl(const std::string& base,
                              const std::string& tag,
                              const std::string& asset_name) const;

    // Where downloaded v2 model assets should be stored.
    // Returns "$XDG_CACHE_HOME/shmtu-cas-ocr/<tag>/<asset_stem>" or
    // "$HOME/.cache/shmtu-cas-ocr/<tag>/<asset_stem>" when XDG is
    // not set.
    static std::string defaultAssetCacheDir(const std::string& tag,
                                            const std::string& asset_stem);

    // Strip surrounding whitespace and the optional "v" prefix.
    static std::string normaliseTag(std::string_view tag);
};

}  // namespace shmtu::cas::ocr::tui
