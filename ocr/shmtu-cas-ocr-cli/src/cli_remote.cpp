#include <shmtu/cas_ocr/base64.h>

#include "cli_remote.h"

#include "cli_files.h"
#include "cli_json.h"

#include <httplib.h>

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>

namespace shmtu::cas::ocr::cli {

namespace {

namespace json_util {

std::string extract_string(std::string_view json, std::string_view key) {
    const auto key_token = "\"" + std::string(key) + "\"";
    auto pos = json.find(key_token);
    if (pos == std::string_view::npos) {
        return {};
    }

    pos = json.find(':', pos + key_token.size());
    if (pos == std::string_view::npos) {
        return {};
    }

    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    if (pos >= json.size() || json[pos] != '"') {
        return {};
    }

    std::string result;
    for (++pos; pos < json.size() && json[pos] != '"'; ++pos) {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            ++pos;
            switch (json[pos]) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                default: result.push_back(json[pos]); break;
            }
        } else {
            result.push_back(json[pos]);
        }
    }

    return result;
}

int extract_int(std::string_view json, std::string_view key) {
    const auto key_token = "\"" + std::string(key) + "\"";
    auto pos = json.find(key_token);
    if (pos == std::string_view::npos) {
        return 0;
    }

    pos = json.find(':', pos + key_token.size());
    if (pos == std::string_view::npos) {
        return 0;
    }

    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }

    std::string value;
    while (pos < json.size() &&
           (json[pos] == '-' || std::isdigit(static_cast<unsigned char>(json[pos])))) {
        value.push_back(json[pos++]);
    }

    if (value.empty()) {
        return 0;
    }

    return std::atoi(value.c_str());
}

bool extract_bool(std::string_view json, std::string_view key) {
    const auto key_token = "\"" + std::string(key) + "\"";
    auto pos = json.find(key_token);
    if (pos == std::string_view::npos) {
        return false;
    }

    pos = json.find(':', pos + key_token.size());
    if (pos == std::string_view::npos) {
        return false;
    }

    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }

    return json.substr(pos, 4) == "true";
}

}  // namespace json_util

}  // namespace

RemoteOcrResult call_remote_ocr(const std::string& host,
                                const int port,
                                std::span<const uint8_t> image_bytes,
                                const int timeout_sec) {
    RemoteOcrResult result;

    try {
        httplib::Client client(host, port);
        client.set_connection_timeout(timeout_sec);
        client.set_read_timeout(timeout_sec);

        const std::string request_body =
            "{\"imageBase64\":\"" + base64_encode(image_bytes) + "\"}";

        auto response = client.Post("/api/ocr", request_body, "application/json");
        if (!response) {
            result.request_ok = false;
            result.error =
                "HTTP request failed: " + std::to_string(static_cast<int>(response.error()));
            return result;
        }

        result.request_ok = true;
        result.http_status = response->status;
        if (response->status != 200) {
            result.error = "HTTP " + std::to_string(response->status);
            if (const auto error_message = json_util::extract_string(response->body, "error");
                !error_message.empty()) {
                result.error += ": " + error_message;
            }
            return result;
        }

        const std::string_view body = response->body;
        result.success = json_util::extract_bool(body, "success");
        result.expression = json_util::extract_string(body, "expression");
        result.result = json_util::extract_int(body, "result");
        result.equal_symbol = json_util::extract_int(body, "equalSymbol");
        result.op = json_util::extract_int(body, "operator");
        result.digit1 = json_util::extract_int(body, "digit1");
        result.digit2 = json_util::extract_int(body, "digit2");
        result.error = json_util::extract_string(body, "error");
    } catch (const std::exception& exception) {
        result.request_ok = false;
        result.error = std::string("Exception: ") + exception.what();
    }

    return result;
}

RemoteOcrResult call_remote_ocr_file(const std::string& host,
                                     const int port,
                                     const std::filesystem::path& path,
                                     const int timeout_sec) {
    const auto file_data = read_binary_file(path);
    if (!file_data) {
        RemoteOcrResult result;
        result.request_ok = false;
        result.error = file_data.error();
        return result;
    }

    return call_remote_ocr(host, port, *file_data, timeout_sec);
}

std::expected<void, std::string> check_remote_server(const CliConfig& config) {
    try {
        httplib::Client client(config.server_host, config.server_port);
        client.set_connection_timeout(5);
        client.set_read_timeout(5);
        auto response = client.Get("/api/health");
        if (response && response->status == 200) {
            return {};
        }
        if (response) {
            return std::unexpected("Health check returned HTTP " + std::to_string(response->status));
        }
        return std::unexpected("Health check connection error " +
                               std::to_string(static_cast<int>(response.error())));
    } catch (const std::exception& exception) {
        return std::unexpected(exception.what());
    }
}

}  // namespace shmtu::cas::ocr::cli
