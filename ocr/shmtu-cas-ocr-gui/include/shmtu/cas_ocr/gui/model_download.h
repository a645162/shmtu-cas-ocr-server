#pragma once

#include <shmtu/cas_ocr/manifest.h>
#include <shmtu/cas_ocr/types.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace shmtu::cas::ocr::gui {

using DownloadProgressCallback =
    std::function<bool(int completed_files, int total_files, const std::string& filename)>;

using DownloadBytesProgressCallback =
    std::function<bool(std::int64_t bytes_now, std::int64_t bytes_total)>;

std::vector<std::string> missingModelFiles(const std::string& model_dir,
                                           const std::string& precision);

bool downloadModelFiles(const std::string& model_dir,
                        const std::vector<std::string>& missing_files,
                        bool use_gitee,
                        const DownloadProgressCallback& progress_callback,
                        std::string& error_message);

bool downloadUrlToMemory(const std::string& url,
                         std::vector<uint8_t>& output,
                         long& http_status,
                         std::string& error_message);

// --------------------------------------------------------------------------
// v2 manifest-driven downloads
// --------------------------------------------------------------------------

// Release tag (e.g. "v2.0") appended to the GitHub/Gitee base URL.
constexpr auto DEFAULT_RELEASE_TAG = "v2.0";

constexpr auto GITHUB_RELEASES_BASE_URL =
    "https://github.com/a645162/shmtu-cas-ocr-model/releases/download";
constexpr auto GITEE_RELEASES_BASE_URL =
    "https://gitee.com/a645162/shmtu-cas-ocr-model/releases/download";

// Build a release-asset URL for a given base (github or gitee).
//   base: "github" or "gitee" (case-insensitive).
//   tag:  release tag, e.g. "v2.0".
//   asset_name: bare file name within the release, e.g.
//               "mobilenet_v3_small.trislot_decoder.v2_0.fp16.param".
std::string buildReleaseAssetUrl(const std::string& base,
                                 const std::string& tag,
                                 const std::string& asset_name);

// Download a manifest JSON for a given tag.
// Returns the raw JSON text; an empty string indicates failure
// (consult `error_message` and `http_status`).
std::string downloadReleaseManifest(const std::string& base,
                                    const std::string& tag,
                                    long& http_status,
                                    std::string& error_message);

// Download one (engine, precision) artifact for a specific model from a
// parsed manifest into `dest_dir`.  SHA256 digests in the manifest are
// verified when present; files are downloaded from GitHub first, with a
// Gitee fallback.  `use_gitee_first` swaps the primary source.  Returns
// false if any file fails after all retries.
// `bytes_progress_cb` (optional) is invoked during each individual
// download to drive a UI progress bar.
bool downloadV2Artifact(const shmtu::cas::ocr::ModelInfo& model,
                        const std::string& engine,
                        const std::string& precision,
                        const std::string& dest_dir,
                        bool use_gitee_first,
                        const DownloadBytesProgressCallback& bytes_progress_cb,
                        std::string& error_message);

// Overload that accepts an explicit release tag (e.g. "v2.0.5") instead of
// using DEFAULT_RELEASE_TAG.  This is the preferred entry point for callers
// that already know which release they are downloading from.
bool downloadV2Artifact(const shmtu::cas::ocr::ModelInfo& model,
                        const std::string& engine,
                        const std::string& precision,
                        const std::string& dest_dir,
                        const std::string& tag,
                        bool use_gitee_first,
                        const DownloadBytesProgressCallback& bytes_progress_cb,
                        std::string& error_message);

// Fetch the list of v2 release tags from GitHub, sorted newest-first.
// Filters to tags matching "v2.*".  Returns empty vector on failure.
std::vector<std::string> fetchV2ReleaseTags(long& http_status,
                                             std::string& error_message);

// Fetch the latest v2 release tag from GitHub.
// Returns empty string on failure.
std::string fetchLatestV2Tag(long& http_status, std::string& error_message);

}  // namespace shmtu::cas::ocr::gui
