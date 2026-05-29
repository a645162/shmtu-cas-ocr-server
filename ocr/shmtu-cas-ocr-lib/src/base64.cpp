#include <shmtu/cas_ocr/base64.h>

#include <array>
#include <cctype>
#include <string_view>
#include <ranges>

namespace shmtu::cas::ocr {

namespace {

constexpr std::string_view kBase64Table =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

}  // namespace

std::string base64_encode(std::span<const uint8_t> data) {
    std::string result;
    result.reserve(((data.size() + 2) / 3) * 4);

    size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        const uint32_t n = (static_cast<uint32_t>(data[i]) << 16) |
                           (static_cast<uint32_t>(data[i + 1]) << 8) |
                           static_cast<uint32_t>(data[i + 2]);
        result += kBase64Table[(n >> 18) & 0x3F];
        result += kBase64Table[(n >> 12) & 0x3F];
        result += kBase64Table[(n >> 6) & 0x3F];
        result += kBase64Table[n & 0x3F];
    }

    if (i < data.size()) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < data.size()) {
            n |= static_cast<uint32_t>(data[i + 1]) << 8;
        }
        result += kBase64Table[(n >> 18) & 0x3F];
        result += kBase64Table[(n >> 12) & 0x3F];
        result += (i + 1 < data.size()) ? kBase64Table[(n >> 6) & 0x3F] : '=';
        result += '=';
    }

    return result;
}

std::expected<std::vector<uint8_t>, std::string> base64_decode(std::string_view input) {
    static std::array<int, 256> decode_table = [] {
        std::array<int, 256> table{};
        table.fill(-1);
        for (int i = 0; i < 64; ++i) {
            table[static_cast<unsigned char>(kBase64Table[i])] = i;
        }
        return table;
    }();

    std::vector<uint8_t> result;
    result.reserve(input.size() * 3 / 4);

    int value = 0;
    int bits = -8;
    bool saw_payload = false;
    for (const auto c : input | std::views::transform([](const char ch) {
             return static_cast<unsigned char>(ch);
         })) {
        if (decode_table[c] == -1) {
            if (c == '=') {
                break;
            }
            if (!std::isspace(c)) {
                return std::unexpected("Invalid base64 character");
            }
            continue;
        }
        saw_payload = true;
        value = (value << 6) | decode_table[c];
        bits += 6;
        if (bits >= 0) {
            result.push_back(static_cast<uint8_t>((value >> bits) & 0xFF));
            bits -= 8;
        }
    }

    if (!saw_payload && !input.empty()) {
        return std::unexpected("Empty base64 payload");
    }

    return result;
}

}  // namespace shmtu::cas::ocr
