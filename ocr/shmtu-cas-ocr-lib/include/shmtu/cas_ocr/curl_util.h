// SPDX-License-Identifier: MIT


#pragma once

#include <curl/curl.h>

#include <cstdint>
#include <functional>
#include <string>

namespace shmtu::cas::ocr::curlutil {

// Progress callback used by both in-memory and file downloads.
// `bytes_now` and `bytes_total` are bytes downloaded and the
// Content-Length value reported by the server.  Returning `false`
// aborts the transfer (curl returns CURLE_ABORTED_BY_CALLBACK).
using BytesProgressCallback =
    std::function<bool(curl_off_t bytes_now, curl_off_t bytes_total)>;

// Low-level libcurl wrapper.  Writes received bytes to `write_callback`
// (libcurl `CURLOPT_WRITEFUNCTION` signature) and reports progress
// through `progress_callback`.  On success returns `true` and stores
// the HTTP status code in `http_status`.  On failure `error_message`
// contains a human-readable description.
bool curlDownload(const std::string& url,
                  curl_write_callback write_callback,
                  void* write_userdata,
                  BytesProgressCallback progress_callback,
                  long timeout_seconds,
                  long& http_status,
                  std::string& error_message);

// Convenience wrapper that writes the response body directly to a
// local file at `filepath`, creating/truncating it.
bool downloadUrlToFile(const std::string& url,
                       const std::string& filepath,
                       long& http_status,
                       std::string& error_message);

// Same as `downloadUrlToFile` but with a progress callback.
bool downloadUrlToFileWithProgress(
    const std::string& url,
    const std::string& filepath,
    BytesProgressCallback bytes_progress_cb,
    long& http_status,
    std::string& error_message);

// Convenience wrapper that buffers the response body into `output`.
bool downloadUrlToMemory(const std::string& url,
                         std::vector<uint8_t>& output,
                         long& http_status,
                         std::string& error_message);

}  // namespace shmtu::cas::ocr::curlutil
