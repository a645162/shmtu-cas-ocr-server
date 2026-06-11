// SPDX-License-Identifier: MIT
#include "http/http_client.h"

#include <curl/curl.h>

#include <mutex>
#include <string>
#include <utility>

namespace shmtu::cas::ocr::tui {

// One-shot curl global init, safely called from any thread.
static void ensureCurlInit() {
    static std::once_flag flag;
    std::call_once(flag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

HttpClient::HttpClient() {
    ensureCurlInit();
}

HttpClient::~HttpClient() {
    // We deliberately do NOT call curl_global_cleanup() because the
    // process may have other curl users (notably the lib-level
    // utilities); calling cleanup here can corrupt global state.
}

std::string HttpClient::classifyHttpError(long http_status) {
    using namespace std::string_literals;
    std::string base = "HTTP " + std::to_string(http_status) + ": ";
    if (http_status == 0) return base + "host unreachable or TLS handshake failed";
    if (http_status == 401) return base + "authentication required (check GH_TOKEN if set)";
    if (http_status == 403) return base + "rate limited or forbidden (anonymous limit: 60 req/h)";
    if (http_status == 404) return base + "resource not found (tag or asset may not exist)";
    if (http_status >= 400 && http_status < 500) return base + "client error";
    if (http_status >= 500 && http_status < 600) return base + "server error";
    if (http_status >= 300 && http_status < 400) return base + "redirect not followed";
    return base + "unexpected status";
}

std::string HttpClient::getText(const std::string& url,
                                long& http_status,
                                std::string& error_message) {
    http_status = 0;
    error_message.clear();
    std::vector<uint8_t> buf;
    if (!shmtu::cas::ocr::curlutil::downloadUrlToMemory(
            url, buf, http_status, error_message)) {
        // downloadUrlToMemory already set error_message for CURL errors
        if (error_message.empty()) error_message = classifyHttpError(http_status);
        return {};
    }
    if (http_status != 200) {
        error_message = classifyHttpError(http_status);
        return {};
    }
    return std::string(buf.begin(), buf.end());
}

std::vector<uint8_t> HttpClient::getBinary(const std::string& url,
                                           long& http_status,
                                           std::string& error_message) {
    http_status = 0;
    error_message.clear();
    std::vector<uint8_t> buf;
    shmtu::cas::ocr::curlutil::downloadUrlToMemory(
        url, buf, http_status, error_message);
    return buf;
}

bool HttpClient::downloadToFile(const std::string& url,
                                const std::string& dest_path,
                                long& http_status,
                                std::string& error_message) {
    return shmtu::cas::ocr::curlutil::downloadUrlToFile(
        url, dest_path, http_status, error_message);
}

bool HttpClient::downloadToFileWithProgress(
    const std::string& url,
    const std::string& dest_path,
    shmtu::cas::ocr::curlutil::BytesProgressCallback progress_cb,
    long& http_status,
    std::string& error_message) {
    return shmtu::cas::ocr::curlutil::downloadUrlToFileWithProgress(
        url, dest_path, std::move(progress_cb),
        http_status, error_message);
}

}  // namespace shmtu::cas::ocr::tui
