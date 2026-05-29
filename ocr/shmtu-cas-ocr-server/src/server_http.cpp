#include "server_internal.h"
#include "logging.h"

#include <algorithm>
#include <expected>
#include <memory>
#include <string_view>
#include <vector>

#include <drogon/HttpAppFramework.h>
#include <drogon/MultiPart.h>

namespace shmtu::cas::ocr {

namespace {

using ResponseCallback = std::function<void(const drogon::HttpResponsePtr&)>;

void log_http_request_snapshot(OcrServer::Impl& impl,
                               unsigned long long request_id,
                               const std::string& client,
                               std::string_view route) {
    const auto pending = impl.pool ? impl.pool->pending_tasks.load() : 0;
    const auto active = impl.pool ? impl.pool->active_workers.load() : 0;
    LOG(INFO) << "HTTP request snapshot"
              << " request_id=" << request_id
              << " route=" << route
              << " client=" << client
              << " pending_requests=" << pending
              << " active_workers=" << active
              << " queue_capacity=" << impl.config.queue_capacity;
}

void respond_bad_request(OcrServer::Impl& impl,
                         const unsigned long long request_id,
                         const std::string& client,
                         const std::string_view route,
                         const std::string_view error,
                         ResponseCallback&& callback) {
    LOG(WARNING) << "HTTP bad request"
                 << " request_id=" << request_id
                 << " route=" << route
                 << " client=" << client
                 << " error=\"" << error << "\"";
    Json::Value body(Json::objectValue);
    body["success"] = false;
    body["error"] = std::string(error);
    impl.failed_requests.fetch_add(1);
    callback(make_json_response(body, drogon::k400BadRequest));
}

std::expected<std::vector<uint8_t>, std::string> decode_image_base64(
    const Json::Value& json_body) {
    if (!json_body.isMember("imageBase64") || !json_body["imageBase64"].isString()) {
        return std::unexpected("ImageBase64 is required");
    }

    auto decoded = base64_decode(json_body["imageBase64"].asString());
    if (!decoded) {
        return std::unexpected("Invalid base64 string");
    }
    return decoded;
}

void handle_predict_result(
    OcrServer::Impl& impl,
    const PredictResult& result,
    const PredictExecutionInfo& info,
    unsigned long long request_id,
    const std::string& client,
    ResponseCallback callback) {
    if (result.success) {
        impl.successful_requests.fetch_add(1);
    } else {
        impl.failed_requests.fetch_add(1);
    }

    LOG(INFO) << "HTTP request completed"
              << " request_id=" << request_id
              << " client=" << client
              << " status=200"
              << " success=" << result.success
              << " expression=\"" << result.expression << "\""
              << " result=" << result.result
              << " error=\"" << result.error << "\""
              << " input_bytes=" << info.input_bytes
              << " worker_index=" << info.worker_index
              << " queue_wait_ms=" << to_millis(info.queue_wait)
              << " inference_ms=" << to_millis(info.inference_time)
              << " total_ms=" << to_millis(info.total_time);
    callback(make_json_response(predict_result_to_json(result)));
}

} // namespace

drogon::HttpResponsePtr make_json_response(
    const Json::Value& body,
    drogon::HttpStatusCode status) {
    auto response = drogon::HttpResponse::newHttpJsonResponse(body);
    response->setStatusCode(status);
    return response;
}

Json::Value predict_result_to_json(const PredictResult& result) {
    Json::Value json(Json::objectValue);
    json["success"] = result.success;
    json["expression"] = result.expression;
    json["result"] = result.result;
    json["equalSymbol"] = result.equal_symbol;
    json["operator"] = result.op;
    json["digit1"] = result.digit1;
    json["digit2"] = result.digit2;
    json["error"] = result.error.empty() ? Json::Value() : Json::Value(result.error);
    return json;
}

Json::Value health_to_json(const HealthResult& health) {
    Json::Value json(Json::objectValue);
    json["status"] = health.status;
    json["modelsLoaded"] = health.models_loaded;
    json["poolSize"] = health.pool_size;
    if (!health.server_name.empty()) {
        json["serverName"] = health.server_name;
    }
    return json;
}

Json::Value stats_to_json(const ServerStats& stats) {
    auto now = std::chrono::steady_clock::now();
    auto uptime_secs = std::chrono::duration_cast<std::chrono::seconds>(
        now - stats.start_time).count();

    Json::Value json(Json::objectValue);
    json["status"] = stats.models_loaded ? "healthy" : "unavailable";
    json["availabilityLevel"] = stats.models_loaded
        ? (stats.pending_requests > stats.queue_capacity / 2 ? "busy" : "available")
        : "unavailable";
    json["reason"] = stats.models_loaded ? "" : "Models not loaded";
    json["modelsLoaded"] = stats.models_loaded;
    json["poolSize"] = stats.pool_size;
    json["queueCapacity"] = stats.queue_capacity;
    json["pendingRequests"] = stats.pending_requests;
    json["activeWorkers"] = stats.active_workers;
    json["totalRequests"] = stats.total_requests;
    json["successCount"] = stats.successful_requests;
    json["failureCount"] = stats.failed_requests;
    json["uptimeSeconds"] = static_cast<Json::Int64>(uptime_secs);
    if (!stats.server_name.empty()) {
        json["serverName"] = stats.server_name;
    }
    return json;
}

void register_http_handlers(OcrServer::Impl& impl, OcrServer& server) {
    auto& app = drogon::app();
    app.setThreadNum(std::max(1, impl.config.worker_count));
    app.addListener(impl.config.http_host, static_cast<uint16_t>(impl.config.http_port));

    app.registerHandler(
        "/api/health",
        [&impl, &server](const drogon::HttpRequestPtr& req,
                  std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            const auto request_id = impl.request_sequence.fetch_add(1) + 1;
            const auto client = describe_http_client(req);
            const auto health = server.health();
            LOG(INFO) << "HTTP GET /api/health begin"
                      << " request_id=" << request_id
                      << " client=" << client;
            log_http_request_snapshot(impl, request_id, client, "/api/health");
            callback(make_json_response(health_to_json(health)));
            LOG(INFO) << "HTTP GET /api/health completed"
                      << " request_id=" << request_id
                      << " client=" << client
                      << " status=200"
                      << " health_status=" << health.status
                      << " models_loaded=" << health.models_loaded
                      << " pending_requests=" << health.pending_requests;
        },
        {drogon::Get});

    app.registerHandler(
        "/api/status",
        [&impl, &server](const drogon::HttpRequestPtr& req,
                  std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            const auto request_id = impl.request_sequence.fetch_add(1) + 1;
            const auto client = describe_http_client(req);
            const auto stats = server.stats();
            LOG(INFO) << "HTTP GET /api/status begin"
                      << " request_id=" << request_id
                      << " client=" << client;
            log_http_request_snapshot(impl, request_id, client, "/api/status");
            callback(make_json_response(stats_to_json(stats)));
            LOG(INFO) << "HTTP GET /api/status completed"
                      << " request_id=" << request_id
                      << " client=" << client
                      << " status=200"
                      << " total_requests=" << stats.total_requests
                      << " success_count=" << stats.successful_requests
                      << " failure_count=" << stats.failed_requests
                      << " pending_requests=" << stats.pending_requests;
        },
        {drogon::Get});

    app.registerHandler(
        "/api/ocr",
        [&impl](const drogon::HttpRequestPtr& req,
                ResponseCallback&& callback) {
            impl.total_requests.fetch_add(1);
            const auto request_id = impl.request_sequence.fetch_add(1) + 1;
            const auto client = describe_http_client(req);
            LOG(INFO) << "HTTP POST /api/ocr begin"
                      << " request_id=" << request_id
                      << " client=" << client
                      << " content_type=" << req->contentType()
                      << " content_length=" << req->body().size();
            log_http_request_snapshot(impl, request_id, client, "/api/ocr");

            auto json = req->getJsonObject();
            if (!json) {
                respond_bad_request(impl, request_id, client, "/api/ocr",
                                    "Invalid JSON body", std::move(callback));
                return;
            }

            auto image_bytes = decode_image_base64(*json);
            if (!image_bytes) {
                respond_bad_request(impl, request_id, client, "/api/ocr",
                                    image_bytes.error(), std::move(callback));
                return;
            }
            LOG(INFO) << "HTTP POST /api/ocr decoded image"
                      << " request_id=" << request_id
                      << " bytes=" << image_bytes->size();

            auto callback_holder = std::make_shared<ResponseCallback>(std::move(callback));
            if (!impl.submit_predict(
                    std::move(*image_bytes),
                    [&impl, callback_holder, request_id, client](PredictResult result, PredictExecutionInfo info) {
                        handle_predict_result(impl, result, info, request_id, client, *callback_holder);
                    })) {
                Json::Value body(Json::objectValue);
                body["success"] = false;
                body["error"] = "Server overloaded";
                impl.failed_requests.fetch_add(1);
                LOG(WARNING) << "HTTP POST /api/ocr overloaded"
                             << " request_id=" << request_id
                             << " client=" << client
                             << " pending_requests=" << (impl.pool ? impl.pool->pending_tasks.load() : 0)
                             << " queue_capacity=" << impl.config.queue_capacity;
                (*callback_holder)(make_json_response(body, drogon::k503ServiceUnavailable));
            } else {
                LOG(INFO) << "HTTP POST /api/ocr queued"
                          << " request_id=" << request_id
                          << " client=" << client
                          << " pending_requests=" << impl.pool->pending_tasks.load()
                          << " queue_capacity=" << impl.config.queue_capacity;
            }
        },
        {drogon::Post});

    app.registerHandler(
        "/api/ocr/upload",
        [&impl](const drogon::HttpRequestPtr& req,
                ResponseCallback&& callback) {
            impl.total_requests.fetch_add(1);
            const auto request_id = impl.request_sequence.fetch_add(1) + 1;
            const auto client = describe_http_client(req);
            const auto content_type = req->getHeader("content-type");
            LOG(INFO) << "HTTP POST /api/ocr/upload begin"
                      << " request_id=" << request_id
                      << " client=" << client
                      << " content_type=" << content_type
                      << " content_length=" << req->body().size();
            log_http_request_snapshot(impl, request_id, client, "/api/ocr/upload");

            drogon::MultiPartParser parser;
            if (parser.parse(req) != 0) {
                respond_bad_request(impl, request_id, client, "/api/ocr/upload",
                                    "Invalid multipart form data", std::move(callback));
                return;
            }

            const auto& files = parser.getFiles();
            LOG(INFO) << "HTTP POST /api/ocr/upload multipart parsed"
                      << " request_id=" << request_id
                      << " file_count=" << files.size();
            if (files.empty()) {
                respond_bad_request(impl, request_id, client, "/api/ocr/upload",
                                    "Image file is required", std::move(callback));
                return;
            }

            const auto& file = files.front();
            std::vector<uint8_t> image_bytes(
                file.fileData(),
                file.fileData() + file.fileLength());
            LOG(INFO) << "HTTP POST /api/ocr/upload image accepted"
                      << " request_id=" << request_id
                      << " filename=" << file.getFileName()
                      << " bytes=" << image_bytes.size();

            auto callback_holder = std::make_shared<ResponseCallback>(std::move(callback));
            if (!impl.submit_predict(
                    std::move(image_bytes),
                    [&impl, callback_holder, request_id, client](PredictResult result, PredictExecutionInfo info) {
                        handle_predict_result(impl, result, info, request_id, client, *callback_holder);
                    })) {
                Json::Value body(Json::objectValue);
                body["success"] = false;
                body["error"] = "Server overloaded";
                impl.failed_requests.fetch_add(1);
                LOG(WARNING) << "HTTP POST /api/ocr/upload overloaded"
                             << " request_id=" << request_id
                             << " client=" << client
                             << " pending_requests=" << (impl.pool ? impl.pool->pending_tasks.load() : 0)
                             << " queue_capacity=" << impl.config.queue_capacity;
                (*callback_holder)(make_json_response(body, drogon::k503ServiceUnavailable));
            } else {
                LOG(INFO) << "HTTP POST /api/ocr/upload queued"
                          << " request_id=" << request_id
                          << " client=" << client
                          << " pending_requests=" << impl.pool->pending_tasks.load()
                          << " queue_capacity=" << impl.config.queue_capacity;
            }
        },
        {drogon::Post});
}

} // namespace shmtu::cas::ocr
