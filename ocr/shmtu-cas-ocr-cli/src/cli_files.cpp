#include "cli_files.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <ranges>

namespace shmtu::cas::ocr::cli {

std::expected<ByteBuffer, std::string> read_binary_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return std::unexpected("Cannot open file: " + path.string());
    }

    const auto file_size = input.tellg();
    if (file_size < 0) {
        return std::unexpected("Cannot determine file size: " + path.string());
    }

    ByteBuffer buffer(static_cast<size_t>(file_size));
    input.seekg(0, std::ios::beg);
    if (!buffer.empty()) {
        input.read(reinterpret_cast<char*>(buffer.data()),
                   static_cast<std::streamsize>(buffer.size()));
    }

    if (!input.good() && !input.eof()) {
        return std::unexpected("Failed to read file: " + path.string());
    }

    return buffer;
}

bool is_image_file(const fs::path& path) {
    auto extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
           extension == ".bmp" || extension == ".tif" || extension == ".tiff";
}

std::expected<std::vector<std::string>, std::string> collect_image_paths(const fs::path& input) {
    std::vector<std::string> image_paths;

    if (fs::is_directory(input)) {
        for (const auto& entry : fs::directory_iterator(input)) {
            if (entry.is_regular_file() && is_image_file(entry.path())) {
                image_paths.push_back(entry.path().string());
            }
        }
        std::ranges::sort(image_paths);
    } else if (fs::exists(input)) {
        image_paths.push_back(input.string());
    } else {
        return std::unexpected("Path does not exist: " + input.string());
    }

    if (image_paths.empty()) {
        return std::unexpected("No image files found");
    }

    return image_paths;
}

}  // namespace shmtu::cas::ocr::cli
