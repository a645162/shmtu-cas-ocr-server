#include "cli_json.h"

#include <string>
#include <string_view>

namespace shmtu::cas::ocr::cli {

std::string json_escape(std::string_view input) {
    std::string escaped;
    escaped.reserve(input.size());
    for (const auto ch : input) {
        switch (ch) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped.push_back(ch); break;
        }
    }
    return escaped;
}

std::string predict_result_to_json(const shmtu::cas::ocr::PredictResult& result) {
    std::string json = "{";
    json += "\"success\":" + std::string(result.success ? "true" : "false") + ",";
    json += "\"expression\":\"" + json_escape(result.expression) + "\",";
    json += "\"result\":" + std::to_string(result.result) + ",";
    json += "\"equalSymbol\":" + std::to_string(result.equal_symbol) + ",";
    json += "\"operator\":" + std::to_string(result.op) + ",";
    json += "\"digit1\":" + std::to_string(result.digit1) + ",";
    json += "\"digit2\":" + std::to_string(result.digit2);
    if (!result.error.empty()) {
        json += ",\"error\":\"" + json_escape(result.error) + "\"";
    }
    json += "}";
    return json;
}

std::string remote_result_to_json(const RemoteOcrResult& result) {
    std::string json = "{";
    json += "\"success\":" + std::string(result.success ? "true" : "false") + ",";
    json += "\"expression\":\"" + json_escape(result.expression) + "\",";
    json += "\"result\":" + std::to_string(result.result) + ",";
    json += "\"equalSymbol\":" + std::to_string(result.equal_symbol) + ",";
    json += "\"operator\":" + std::to_string(result.op) + ",";
    json += "\"digit1\":" + std::to_string(result.digit1) + ",";
    json += "\"digit2\":" + std::to_string(result.digit2);
    if (!result.error.empty()) {
        json += ",\"error\":\"" + json_escape(result.error) + "\"";
    }
    if (!result.request_ok) {
        json += ",\"httpError\":\"request failed\"";
    } else if (result.http_status != 200) {
        json += ",\"httpStatus\":" + std::to_string(result.http_status);
    }
    json += "}";
    return json;
}

}  // namespace shmtu::cas::ocr::cli
