#include "server_internal.h"

#include <algorithm>
#include <vector>

#include <drogon/HttpAppFramework.h>
#include <drogon/MultiPart.h>

namespace shmtu::cas_ocr {

namespace {

void handle_predict_result(
    OcrServer::Impl& impl,
    const PredictResult& result,
    bool queued_ok,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    if (!queued_ok) {
        Json::Value body(Json::objectValue);
        body["success"] = false;
        body["error"] = "Server overloaded";
        impl.failed_requests.fetch_add(1);
        callback(make_json_response(body, drogon::k503ServiceUnavailable));
        return;
    }

    if (!result.success && result.error == "Prediction timeout") {
        Json::Value body(Json::objectValue);
        body["success"] = false;
        body["error"] = result.error;
        impl.failed_requests.fetch_add(1);
        callback(make_json_response(body, drogon::k504GatewayTimeout));
        return;
    }

    if (result.success) {
        impl.successful_requests.fetch_add(1);
    } else {
        impl.failed_requests.fetch_add(1);
    }

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
    if (!result.error.empty()) {
        json["error"] = result.error;
    }
    return json;
}

Json::Value health_to_json(const HealthResult& health) {
    Json::Value json(Json::objectValue);
    json["status"] = health.status;
    json["availabilityLevel"] = health.availability_level;
    json["reason"] = health.reason;
    json["modelsLoaded"] = health.models_loaded;
    json["poolSize"] = health.pool_size;
    json["queueCapacity"] = health.queue_capacity;
    json["pendingRequests"] = health.pending_requests;
    return json;
}

void register_http_handlers(OcrServer::Impl& impl, OcrServer& server) {
    auto& app = drogon::app();
    app.setThreadNum(std::max(1, impl.config.worker_count));
    app.addListener(impl.config.http_host, static_cast<uint16_t>(impl.config.http_port));

    app.registerHandler(
        "/api/health",
        [&server](const drogon::HttpRequestPtr&,
                  std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            callback(make_json_response(health_to_json(server.health())));
        },
        {drogon::Get});

    app.registerHandler(
        "/api/ocr",
        [&impl](const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            impl.total_requests.fetch_add(1);

            auto json = req->getJsonObject();
            if (!json || !json->isMember("imageBase64") || !(*json)["imageBase64"].isString()) {
                Json::Value body(Json::objectValue);
                body["success"] = false;
                body["error"] = "Missing imageBase64 field";
                impl.failed_requests.fetch_add(1);
                callback(make_json_response(body, drogon::k400BadRequest));
                return;
            }

            const auto image_base64 = (*json)["imageBase64"].asString();
            auto image_bytes = base64_decode(image_base64);
            if (image_bytes.empty()) {
                Json::Value body(Json::objectValue);
                body["success"] = false;
                body["error"] = "Empty base64 data";
                impl.failed_requests.fetch_add(1);
                callback(make_json_response(body, drogon::k400BadRequest));
                return;
            }

            bool queued_ok = false;
            auto result = impl.predict_sync(image_bytes, queued_ok);
            handle_predict_result(impl, result, queued_ok, std::move(callback));
        },
        {drogon::Post});

    app.registerHandler(
        "/api/ocr/upload",
        [&impl](const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            impl.total_requests.fetch_add(1);

            drogon::MultiPartParser parser;
            if (parser.parse(req) != 0) {
                Json::Value body(Json::objectValue);
                body["success"] = false;
                body["error"] = "Invalid multipart form data";
                impl.failed_requests.fetch_add(1);
                callback(make_json_response(body, drogon::k400BadRequest));
                return;
            }

            const auto& files = parser.getFiles();
            if (files.empty()) {
                Json::Value body(Json::objectValue);
                body["success"] = false;
                body["error"] = "No file uploaded";
                impl.failed_requests.fetch_add(1);
                callback(make_json_response(body, drogon::k400BadRequest));
                return;
            }

            const auto& file = files.front();
            std::vector<uint8_t> image_bytes(
                file.fileData(),
                file.fileData() + file.fileLength());

            bool queued_ok = false;
            auto result = impl.predict_sync(image_bytes, queued_ok);
            handle_predict_result(impl, result, queued_ok, std::move(callback));
        },
        {drogon::Post});
}

} // namespace shmtu::cas_ocr
