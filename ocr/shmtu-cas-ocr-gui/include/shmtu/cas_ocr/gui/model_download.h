#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace shmtu::cas::ocr::gui {

using DownloadProgressCallback =
    std::function<bool(int completed_files, int total_files, const std::string& filename)>;

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

}  // namespace shmtu::cas::ocr::gui
