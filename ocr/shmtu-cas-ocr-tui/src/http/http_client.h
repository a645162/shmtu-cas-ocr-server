// SPDX-License-Identifier: MIT
#pragma once

#include <shmtu/cas_ocr/curl_util.h>
#include <shmtu/cas_ocr/types.h>

#include <string>
#include <vector>

namespace shmtu::cas::ocr::tui {

// HTTP client used by the TUI.  Wraps the lib-level curl utility
// with a thread-safe request surface that the UI can call from
// background workers (e.g. while the FTXUI event loop is running).
//
// All methods are blocking; the UI is expected to invoke them from
// `std::thread` workers and post results back via the supplied
// callbacks.
//
// Error handling: when `http_status` is not 200, the returned value
// is empty/false AND `error_message` is populated with a human-
// readable string classifying the failure (e.g. "API returned HTTP
// 404", "Authorization required (401)", etc.).  Callers do not need
// to inspect `http_status` separately; an empty return signals an
// error regardless of the specific status code.
class HttpClient {
public:
    // Default request timeout for GitHub API calls (seconds).
    static constexpr long kGithubApiTimeoutSec = 30;
    // Default request timeout for release asset downloads (seconds).
    static constexpr long kAssetDownloadTimeoutSec = 600;

    HttpClient();
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    // GET `url` and return the response body as a UTF-8 string.
    // Returns an empty string on failure (check `http_status` and
    // `error_message`, or just check empty()).
    std::string getText(const std::string& url,
                       long& http_status,
                       std::string& error_message);

    // GET `url` and return the response body as a byte vector.
    std::vector<uint8_t> getBinary(const std::string& url,
                                   long& http_status,
                                   std::string& error_message);

    // Download `url` to `dest_path`.  Returns false on failure.
    bool downloadToFile(const std::string& url,
                        const std::string& dest_path,
                        long& http_status,
                        std::string& error_message);

    // Download `url` to `dest_path` with progress callback.
    bool downloadToFileWithProgress(
        const std::string& url,
        const std::string& dest_path,
        shmtu::cas::ocr::curlutil::BytesProgressCallback progress_cb,
        long& http_status,
        std::string& error_message);

private:
    // Classifies `http_status` into a human-readable error message
    // (only called when http_status != 200).
    static std::string classifyHttpError(long http_status);
};

}  // namespace shmtu::cas::ocr::tui
