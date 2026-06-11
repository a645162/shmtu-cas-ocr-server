// SPDX-License-Identifier: MIT
//
// shmtu-cas-ocr-lib curl utility implementation.
//
// These helpers wrap libcurl with a small, callback-driven interface
// that was previously duplicated inside the GUI subproject.  Both
// memory and file downloads are supported along with a progress
// callback that returns a `bool` so the caller can abort a transfer.
//
// We intentionally keep the lib's dependency surface stable: only
// libcurl is required at link time.

#include "shmtu/cas_ocr/curl_util.h"

#include <fstream>
#include <sstream>
#include <utility>

namespace shmtu::cas::ocr::curlutil {

namespace {

constexpr const char* kUserAgent = "shmtu-cas-ocr/1.0";

size_t writeToFile(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* stream = static_cast<std::ofstream*>(userdata);
    const auto total = size * nmemb;
    stream->write(ptr, static_cast<std::streamsize>(total));
    return stream->good() ? total : 0;
}

size_t writeToVector(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buffer = static_cast<std::vector<uint8_t>*>(userdata);
    const auto total = size * nmemb;
    const auto* begin = reinterpret_cast<uint8_t*>(ptr);
    buffer->insert(buffer->end(), begin, begin + total);
    return total;
}

int progressCallback(void* clientp,
                    curl_off_t dltotal, curl_off_t dlnow,
                    curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    auto* cb = static_cast<BytesProgressCallback*>(clientp);
    if (!cb || !*cb) {
        return 0;
    }
    return (*cb)(dlnow, dltotal) ? 0 : 1;
}

}  // namespace

bool curlDownload(const std::string& url,
                  curl_write_callback write_callback,
                  void* write_userdata,
                  BytesProgressCallback progress_callback,
                  long timeout_seconds,
                  long& http_status,
                  std::string& error_message) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        error_message = "curl_easy_init failed";
        return false;
    }

    char errbuf[CURL_ERROR_SIZE] = {0};

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, write_userdata);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress_callback);

    const auto code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);

    if (code != CURLE_OK) {
        error_message = errbuf[0] ? errbuf : curl_easy_strerror(code);
        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_cleanup(curl);
    return true;
}

bool downloadUrlToFile(const std::string& url,
                       const std::string& filepath,
                       long& http_status,
                       std::string& error_message) {
    return downloadUrlToFileWithProgress(url, filepath, nullptr,
                                        http_status, error_message);
}

bool downloadUrlToFileWithProgress(
    const std::string& url,
    const std::string& filepath,
    BytesProgressCallback bytes_progress_cb,
    long& http_status,
    std::string& error_message) {
    std::ofstream ofs(filepath, std::ios::binary);
    if (!ofs.is_open()) {
        error_message = "failed to open output file: " + filepath;
        return false;
    }

    const bool ok = curlDownload(url, writeToFile, &ofs,
                                 std::move(bytes_progress_cb),
                                 300L, http_status, error_message);
    ofs.close();
    return ok;
}

bool downloadUrlToMemory(const std::string& url,
                         std::vector<uint8_t>& output,
                         long& http_status,
                         std::string& error_message) {
    output.clear();
    return curlDownload(url, writeToVector, &output, nullptr,
                        30L, http_status, error_message);
}

}  // namespace shmtu::cas::ocr::curlutil
