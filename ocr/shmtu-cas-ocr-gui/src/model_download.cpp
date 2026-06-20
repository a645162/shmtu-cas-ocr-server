#include <shmtu/cas_ocr/curl_util.h>
#include <shmtu/cas_ocr/gui/model_download.h>

#include <shmtu/cas_ocr/gui/logging.h>

#include <curl/curl.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace shmtu::cas::ocr::gui {
namespace {

constexpr auto GITHUB_BASE_URL =
    "https://github.com/a645162/shmtu-cas-ocr-model/releases/download/v1.0-NCNN";
constexpr auto GITEE_BASE_URL =
    "https://gitee.com/a645162/shmtu-cas-ocr-model/releases/download/v1.0-NCNN";

struct ModelFileInfo {
    const char* pattern;
};

constexpr ModelFileInfo NCNN_MODEL_FILES[] = {
    {"resnet18_equal_symbol_latest.%s.param"},
    {"resnet18_equal_symbol_latest.%s.bin"},
    {"resnet18_operator_latest.%s.param"},
    {"resnet18_operator_latest.%s.bin"},
    {"resnet34_digit_latest.%s.param"},
    {"resnet34_digit_latest.%s.bin"},
};

std::string boolToString(const bool value) {
    return value ? "true" : "false";
}

struct CurlProgressPayload {
    std::function<bool(curl_off_t, curl_off_t)> callback;
};

std::string modelFileName(const char* pattern, const std::string& precision) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), pattern, precision.c_str());
    return std::string(buf);
}

size_t curlWriteToFile(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* stream = static_cast<std::ofstream*>(userdata);
    const auto total = size * nmemb;
    stream->write(ptr, static_cast<std::streamsize>(total));
    return stream->good() ? total : 0;
}

size_t curlWriteToVector(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buffer = static_cast<std::vector<uint8_t>*>(userdata);
    const auto total = size * nmemb;
    const auto* begin = reinterpret_cast<uint8_t*>(ptr);
    buffer->insert(buffer->end(), begin, begin + total);
    return total;
}

int curlProgressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                         curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    auto* payload = static_cast<CurlProgressPayload*>(clientp);
    if (!payload || !payload->callback) {
        return 0;
    }
    return payload->callback(dlnow, dltotal) ? 0 : 1;
}

bool curlDownload(const std::string& url,
                  curl_write_callback write_callback,
                  void* write_userdata,
                  std::function<bool(curl_off_t, curl_off_t)> progress_callback,
                  long timeout_seconds,
                  long& http_status,
                  std::string& error_message) {
    {
        std::ostringstream oss;
        oss << "curlDownload: begin"
            << ", url=" << url
            << ", timeout_seconds=" << timeout_seconds;
        logMessage(oss.str());
    }
    CURL* curl = curl_easy_init();
    if (!curl) {
        error_message = "curl_easy_init failed";
        return false;
    }

    char errbuf[CURL_ERROR_SIZE] = {0};
    CurlProgressPayload progress_payload{std::move(progress_callback)};

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "shmtu-cas-ocr-gui/1.0");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, write_userdata);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlProgressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress_payload);

    const auto code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);

    if (code != CURLE_OK) {
        error_message = errbuf[0] ? errbuf : curl_easy_strerror(code);
        logMessage("curlDownload: failed, url=" + url +
                   ", http_status=" + std::to_string(http_status) +
                   ", error=" + error_message);
        curl_easy_cleanup(curl);
        return false;
    }

    logMessage("curlDownload: succeeded, url=" + url +
               ", http_status=" + std::to_string(http_status));
    curl_easy_cleanup(curl);
    return true;
}

bool downloadUrlToFile(const std::string& url,
                       const std::string& filepath,
                       long& http_status,
                       std::string& error_message) {
    std::ofstream ofs(filepath, std::ios::binary);
    if (!ofs.is_open()) {
        error_message = "failed to open output file: " + filepath;
        return false;
    }

    const bool ok =
        curlDownload(url, curlWriteToFile, &ofs, nullptr, 300L, http_status, error_message);
    ofs.close();
    return ok;
}

std::string computeSha256Local(const std::string& filepath) {
    std::string cmd = "sha256sum \"" + filepath + "\" 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        logMessage("computeSha256: popen failed for " + filepath);
        return "";
    }
    char buf[256] = {0};
    if (!fgets(buf, sizeof(buf), pipe)) {
        pclose(pipe);
        logMessage("computeSha256: sha256sum produced no output for " + filepath);
        return "";
    }
    pclose(pipe);
    std::string result(buf);
    // sha256sum output format: "<hash>  <filename>" — extract hash only
    auto space_pos = result.find(' ');
    if (space_pos != std::string::npos) {
        result = result.substr(0, space_pos);
    }
    // Trim trailing whitespace
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' ||
                               result.back() == ' ')) {
        result.pop_back();
    }
    return result;
}

std::unordered_map<std::string, std::string> fetchChecksums(const std::string& base_url) {
    std::unordered_map<std::string, std::string> checksums;
    const auto url = base_url + "/SHA256SUMS.txt";

    std::vector<uint8_t> data;
    long http_status = 0;
    std::string error_message;

    const bool ok = shmtu::cas::ocr::curlutil::downloadUrlToMemory(
                        url, data, http_status, error_message) &&
                    http_status == 200 && !data.empty();
    if (!ok) {
        logMessage("fetchChecksums: failed to download SHA256SUMS.txt, url=" + url);
        return checksums;
    }

    std::string content(data.begin(), data.end());
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) continue;
        // Format: "<64-char-hex>  <filename>"
        if (line.size() < 66) continue;
        auto space_pos = line.find(' ');
        if (space_pos != 64) continue;
        std::string hash = line.substr(0, 64);
        auto filename_start = line.find_first_not_of(' ', space_pos);
        if (filename_start == std::string::npos) continue;
        std::string filename = line.substr(filename_start);
        // Remove leading "./" if present
        if (filename.size() > 2 && filename[0] == '.' && filename[1] == '/') {
            filename = filename.substr(2);
        }
        checksums[filename] = hash;
    }

    logMessage("fetchChecksums: loaded " + std::to_string(checksums.size()) +
               " checksums from " + url);
    return checksums;
}

}  // namespace

std::vector<std::string> missingModelFiles(const std::string& model_dir,
                                           const std::string& precision) {
    std::vector<std::string> missing_files;
    for (const auto& file_info : NCNN_MODEL_FILES) {
        const auto filename = modelFileName(file_info.pattern, precision);
        const auto filepath = std::filesystem::path(model_dir) / filename;
        if (!std::filesystem::exists(filepath)) {
            missing_files.push_back(filename);
        }
    }
    {
        std::ostringstream oss;
        oss << "missingModelFiles: scanned"
            << ", model_dir=" << model_dir
            << ", precision=" << precision
            << ", missing_count=" << missing_files.size();
        logMessage(oss.str());
    }
    return missing_files;
}

bool downloadModelFiles(const std::string& model_dir,
                        const std::vector<std::string>& missing_files,
                        bool use_gitee,
                        const DownloadProgressCallback& progress_callback,
                        std::string& error_message) {
    error_message.clear();
    {
        std::ostringstream oss;
        oss << "downloadModelFiles: begin"
            << ", model_dir=" << model_dir
            << ", missing_count=" << missing_files.size()
            << ", use_gitee=" << (use_gitee ? "true" : "false");
        logMessage(oss.str());
    }

    try {
        std::filesystem::create_directories(model_dir);
    } catch (const std::exception& e) {
        error_message = e.what();
        logMessage("downloadModelFiles: create_directories failed, model_dir=" + model_dir +
                   ", error=" + error_message);
        return false;
    }

    // Fetch checksums for integrity verification
    const std::string primary_base_url = use_gitee ? GITEE_BASE_URL : GITHUB_BASE_URL;
    const auto checksums = fetchChecksums(primary_base_url);
    {
        std::ostringstream oss;
        oss << "downloadModelFiles: checksums"
            << ", available=" << (checksums.empty() ? "false" : "true")
            << ", count=" << checksums.size();
        logMessage(oss.str());
    }

    constexpr int MAX_ATTEMPTS = 3;
    const int total_files = static_cast<int>(missing_files.size());
    int completed_files = 0;
    bool all_ok = true;

    for (const auto& filename : missing_files) {
        logMessage("downloadModelFiles: processing file=" + filename);
        if (progress_callback && !progress_callback(completed_files, total_files, filename)) {
            error_message = "下载已取消";
            logMessage("downloadModelFiles: cancelled by progress callback");
            return false;
        }

        auto filepath = (std::filesystem::path(model_dir) / filename).string();
        bool download_ok = false;

        for (int attempt = 1; attempt <= MAX_ATTEMPTS && !download_ok; ++attempt) {
            const char* sources[] = {
                use_gitee ? GITEE_BASE_URL : GITHUB_BASE_URL,
                use_gitee ? GITHUB_BASE_URL : GITEE_BASE_URL
            };

            for (const char* base_url : sources) {
                const auto url = std::string(base_url) + "/" + filename;
                long http_status = 0;
                std::string curl_error;

                logMessage("downloadModelFiles: requesting " + url +
                           ", attempt=" + std::to_string(attempt));
                const bool ok = shmtu::cas::ocr::curlutil::downloadUrlToFile(url, filepath, http_status, curl_error);
                if (!ok || http_status != 200) {
                    std::error_code ec;
                    std::filesystem::remove(filepath, ec);
                    logMessage("downloadModelFiles: download failed, status=" +
                               std::to_string(http_status) + ", error=" + curl_error);
                    continue;
                }

                // HTTP 200 — verify checksum if available
                const auto checksum_it = checksums.find(filename);
                if (checksum_it != checksums.end()) {
                    const auto actual_hash = computeSha256Local(filepath);
                    if (actual_hash.empty()) {
                        logMessage("downloadModelFiles: sha256sum command failed for " +
                                   filename + ", skipping verification");
                    } else if (actual_hash != checksum_it->second) {
                        std::error_code ec;
                        std::filesystem::remove(filepath, ec);
                        logMessage("downloadModelFiles: checksum mismatch for " + filename +
                                   ", expected=" + checksum_it->second +
                                   ", actual=" + actual_hash +
                                   ", attempt=" + std::to_string(attempt));
                        continue;
                    } else {
                        logMessage("downloadModelFiles: checksum verified for " + filename);
                    }
                } else {
                    logMessage("downloadModelFiles: no checksum for " + filename +
                               ", skipping verification");
                }

                download_ok = true;
                break;
            }

            if (!download_ok && attempt < MAX_ATTEMPTS) {
                logMessage("downloadModelFiles: retrying file=" + filename +
                           ", next_attempt=" + std::to_string(attempt + 1));
            }
        }

        if (!download_ok) {
            all_ok = false;
            error_message += "下载失败: " + filename + "\n";
        }

        completed_files++;
    }

    if (progress_callback) {
        (void)progress_callback(total_files, total_files, "");
    }

    {
        std::ostringstream oss;
        oss << "downloadModelFiles: finished"
            << ", success=" << (all_ok ? "true" : "false")
            << ", completed_files=" << completed_files
            << ", total_files=" << total_files;
        logMessage(oss.str());
    }

    return all_ok;
}

bool downloadUrlToMemory(const std::string& url,
                         std::vector<uint8_t>& output,
                         long& http_status,
                         std::string& error_message) {
    output.clear();
    const bool ok = shmtu::cas::ocr::curlutil::downloadUrlToMemory(
                        url, output, http_status, error_message);
    {
        std::ostringstream oss;
        oss << "downloadUrlToMemory: completed"
            << ", url=" << url
            << ", success=" << (ok ? "true" : "false")
            << ", http_status=" << http_status
            << ", bytes=" << output.size();
        if (!error_message.empty()) {
            oss << ", error=" + error_message;
        }
        logMessage(oss.str());
    }
    return ok;
}

std::string buildReleaseAssetUrl(const std::string& base,
                                 const std::string& tag,
                                 const std::string& asset_name) {
    const std::string base_url =
        (base == "gitee" || base == "Gitee") ? GITEE_RELEASES_BASE_URL : GITHUB_RELEASES_BASE_URL;
    return base_url + "/" + tag + "/" + asset_name;
}

std::string downloadReleaseManifest(const std::string& base,
                                    const std::string& tag,
                                    long& http_status,
                                    std::string& error_message) {
    const auto url = buildReleaseAssetUrl(base, tag, "model-assets.json");
    logMessage("downloadReleaseManifest: fetching " + url);
    std::vector<uint8_t> data;
    const bool ok = shmtu::cas::ocr::curlutil::downloadUrlToMemory(
                        url, data, http_status, error_message) &&
                    http_status == 200 && !data.empty();
    if (!ok) {
        logMessage("downloadReleaseManifest: failed, http_status=" +
                   std::to_string(http_status) + ", error=" + error_message);
        return {};
    }
    return std::string(data.begin(), data.end());
}

bool downloadV2Artifact(const shmtu::cas::ocr::ModelInfo& model,
                        const std::string& engine,
                        const std::string& precision,
                        const std::string& dest_dir,
                        bool use_gitee_first,
                        const DownloadBytesProgressCallback& bytes_progress_cb,
                        std::string& error_message) {
    // Delegate to the tag-aware overload using the default release tag.
    return downloadV2Artifact(model, engine, precision, dest_dir,
                              DEFAULT_RELEASE_TAG,
                              use_gitee_first, bytes_progress_cb, error_message);
}

bool downloadV2Artifact(const shmtu::cas::ocr::ModelInfo& model,
                        const std::string& engine,
                        const std::string& precision,
                        const std::string& dest_dir,
                        const std::string& tag,
                        bool use_gitee_first,
                        const DownloadBytesProgressCallback& bytes_progress_cb,
                        std::string& error_message) {
    logMessage("downloadV2Artifact: begin, model=" + model.display_name +
               ", engine=" + engine + ", precision=" + precision +
               ", dest_dir=" + dest_dir +
               ", tag=" + tag +
               ", gitee_first=" + boolToString(use_gitee_first));

    const auto* artifact = shmtu::cas::ocr::find_artifact(model, engine, precision);
    if (!artifact) {
        error_message = "No artifact for engine=" + engine + ", precision=" + precision;
        logMessage("downloadV2Artifact: " + error_message);
        return false;
    }

    try {
        std::filesystem::create_directories(dest_dir);
    } catch (const std::exception& e) {
        error_message = e.what();
        return false;
    }

    const auto primary = use_gitee_first ? "gitee" : "github";
    const auto fallback = use_gitee_first ? "github" : "gitee";

    bool all_ok = true;
    for (const auto& file : artifact->files) {
        const auto filepath =
            std::filesystem::path(dest_dir) / file.release_asset_name;
        const auto dest = filepath.string();

        bool download_ok = false;
        for (const auto& source : {std::string(primary), std::string(fallback)}) {
            const auto url = buildReleaseAssetUrl(source, tag, file.release_asset_name);
            logMessage("downloadV2Artifact: downloading " + url + " -> " + dest);

            long http_status = 0;
            std::string curl_error;
            const bool ok =
                shmtu::cas::ocr::curlutil::downloadUrlToFile(url, dest, http_status, curl_error);
            if (!ok || http_status != 200) {
                std::error_code ec;
                std::filesystem::remove(dest, ec);
                logMessage("downloadV2Artifact: download failed from " +
                           source + ", status=" + std::to_string(http_status) +
                           ", error=" + curl_error);
                continue;
            }

            // Verify SHA256
            const auto actual_hash = computeSha256Local(dest);
            if (actual_hash.empty()) {
                logMessage("downloadV2Artifact: sha256sum failed for " + dest +
                           ", skipping verification");
            } else if (actual_hash != file.sha256) {
                std::error_code ec;
                std::filesystem::remove(dest, ec);
                logMessage("downloadV2Artifact: checksum mismatch for " +
                           file.release_asset_name +
                           ", expected=" + file.sha256 +
                           ", actual=" + actual_hash);
                continue;
            } else {
                logMessage("downloadV2Artifact: checksum verified for " +
                           file.release_asset_name);
            }

            download_ok = true;
            break;
        }

        if (!download_ok) {
            all_ok = false;
            error_message += "downloadV2Artifact: failed to download " +
                             file.release_asset_name + "\n";
        }

        if (bytes_progress_cb) {
            bytes_progress_cb(0, 0);
        }
    }

    logMessage("downloadV2Artifact: finished, all_ok=" + boolToString(all_ok));
    return all_ok;
}

// --------------------------------------------------------------------------
// GitHub Releases API — fetch v2 tag list
// --------------------------------------------------------------------------

namespace {

constexpr auto GITHUB_API_BASE =
    "https://api.github.com/repos/a645162/shmtu-cas-ocr-model";

// Lightweight extraction of all "tag_name" string values from a GitHub
// `/releases` JSON response.  No full parse — just scan for the pattern
// `"tag_name":"<value>"` (with optional whitespace around the colon).
std::vector<std::string> extractTagNames(std::string_view json) {
    std::vector<std::string> tags;
    const std::string needle = "\"tag_name\"";
    std::size_t pos = 0;
    while (pos < json.size()) {
        auto hit = json.find(needle, pos);
        if (hit == std::string_view::npos) break;
        // Skip to the colon
        auto p = hit + needle.size();
        while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p]))) ++p;
        if (p >= json.size() || json[p] != ':') { pos = hit + 1; continue; }
        ++p;
        while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p]))) ++p;
        // Expect opening quote
        if (p >= json.size() || json[p] != '"') { pos = hit + 1; continue; }
        ++p;
        // Read until closing quote
        std::string tag;
        while (p < json.size() && json[p] != '"') {
            if (json[p] == '\\' && p + 1 < json.size()) {
                tag.push_back(json[p + 1]);
                p += 2;
            } else {
                tag.push_back(json[p]);
                ++p;
            }
        }
        if (!tag.empty()) {
            tags.push_back(std::move(tag));
        }
        pos = (p < json.size()) ? p + 1 : p;
    }
    return tags;
}

// Simple semver comparison for "v2.x.y" tags.
// Returns <0 if a<b, 0 if equal, >0 if a>b.
int compareV2Tags(std::string_view a, std::string_view b) {
    // Strip leading 'v' or 'V'
    auto strip = [](std::string_view s) -> std::string_view {
        if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s.remove_prefix(1);
        return s;
    };
    auto sa = strip(a), sb = strip(b);
    // Parse major.minor.patch
    auto parse = [](std::string_view s) -> std::tuple<int,int,int> {
        int maj = 0, min = 0, pat = 0;
        auto p1 = s.find('.');
        if (p1 == std::string_view::npos) {
            maj = std::atoi(std::string(s).c_str());
            return {maj, min, pat};
        }
        maj = std::atoi(std::string(s.substr(0, p1)).c_str());
        auto p2 = s.find('.', p1 + 1);
        if (p2 == std::string_view::npos) {
            min = std::atoi(std::string(s.substr(p1 + 1)).c_str());
            return {maj, min, pat};
        }
        min = std::atoi(std::string(s.substr(p1 + 1, p2 - p1 - 1)).c_str());
        pat = std::atoi(std::string(s.substr(p2 + 1)).c_str());
        return {maj, min, pat};
    };
    auto [amaj, amin, apat] = parse(sa);
    auto [bmaj, bmin, bpat] = parse(sb);
    if (amaj != bmaj) return amaj < bmaj ? -1 : 1;
    if (amin != bmin) return amin < bmin ? -1 : 1;
    if (apat != bpat) return apat < bpat ? -1 : 1;
    return 0;
}

// Check if a tag looks like a v2.x release.
bool isV2Tag(std::string_view tag) {
    if (tag.size() < 2) return false;
    if (tag[0] != 'v' && tag[0] != 'V') return false;
    // Must start with v2.
    return tag.size() >= 3 && tag[1] == '2';
}

}  // namespace

std::vector<std::string> fetchV2ReleaseTags(long& http_status,
                                             std::string& error_message) {
    // Try Gitee API first, then GitHub API.
    const struct { const char* name; const char* url; } sources[] = {
        {"gitee",  "https://gitee.com/api/v5/repos/a645162/shmtu-cas-ocr-model"},
        {"github", GITHUB_API_BASE},
    };

    for (const auto& src : sources) {
        const std::string url = std::string(src.url) + "/releases?per_page=100";
        std::vector<uint8_t> data;
        long status = 0;
        std::string err;
        const bool ok = shmtu::cas::ocr::curlutil::downloadUrlToMemory(
                            url, data, status, err) &&
                        status == 200 && !data.empty();
        if (!ok) {
            logMessage("fetchV2ReleaseTags: " + std::string(src.name) +
                       " failed, http_status=" + std::to_string(status) +
                       ", error=" + err);
            http_status = status;
            error_message = err;
            continue;
        }

        const std::string body(data.begin(), data.end());
        auto all_tags = extractTagNames(body);

        // Filter to v2.x only
        std::vector<std::string> v2_tags;
        for (auto& tag : all_tags) {
            if (isV2Tag(tag)) {
                v2_tags.push_back(std::move(tag));
            }
        }

        // Sort descending (newest first)
        std::sort(v2_tags.begin(), v2_tags.end(),
                  [](const std::string& a, const std::string& b) {
                      return compareV2Tags(a, b) > 0;
                  });

        if (!v2_tags.empty()) {
            logMessage("fetchV2ReleaseTags: found " + std::to_string(v2_tags.size()) +
                       " v2 tags via " + src.name);
            http_status = 200;
            error_message.clear();
            return v2_tags;
        }
    }

    // Fallback: hard-coded list for offline / rate-limited scenarios.
    logMessage("fetchV2ReleaseTags: all API sources failed, using fallback list");
    std::vector<std::string> fallback = {
        "v2.0.5", "v2.0.4", "v2.0.3", "v2.0.2", "v2.0.1", "v2.0",
    };
    http_status = 0;
    error_message = "Gitee/GitHub API unavailable; using fallback tag list";
    return fallback;
}

std::string fetchLatestV2Tag(long& http_status, std::string& error_message) {
    auto tags = fetchV2ReleaseTags(http_status, error_message);
    if (tags.empty()) return {};
    return tags[0];
}

}  // namespace shmtu::cas::ocr::gui
