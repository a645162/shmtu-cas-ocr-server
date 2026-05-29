#include "server_internal.h"
#include "logging.h"

#include <cstdio>
#include <utility>
#include <vector>

#include <trantor/net/EventLoop.h>
#include <trantor/net/InetAddress.h>
#include <trantor/utils/MsgBuffer.h>

namespace shmtu::cas::ocr {

namespace {

constexpr std::string_view kEndMarker = "<END>";

struct TcpPayloadResult {
    std::string payload;
    size_t trailing_bytes = 0;
};

std::optional<TcpPayloadResult> take_complete_payload(
    OcrServer::Impl& impl,
    const trantor::TcpConnectionPtr& conn) {
    std::lock_guard<std::mutex> lock(impl.tcp_buffer_mutex);
    auto& accumulated = impl.tcp_buffers[conn];
    const auto pos = accumulated.find(kEndMarker);
    if (pos == std::string::npos) {
        return std::nullopt;
    }

    auto payload = accumulated.substr(0, pos);
    const size_t trailing_bytes = accumulated.size() - pos - kEndMarker.size();
    impl.tcp_buffers.erase(conn);
    return TcpPayloadResult{std::move(payload), trailing_bytes};
}

} // namespace

void start_tcp_server(OcrServer::Impl& impl) {
    impl.tcp_loop_thread = std::make_unique<trantor::EventLoopThread>("shmtu-cas-ocr-tcp");
    impl.tcp_loop_thread->run();
    auto* tcp_loop = impl.tcp_loop_thread->getLoop();

    impl.tcp_server = std::make_unique<trantor::TcpServer>(
        tcp_loop,
        trantor::InetAddress(
            impl.config.tcp_host,
            static_cast<uint16_t>(impl.config.tcp_port),
            false),
        "shmtu-cas-ocr-tcp");

    impl.tcp_server->setConnectionCallback([&impl](const trantor::TcpConnectionPtr& conn) {
        if (conn->connected()) {
            std::printf("[TCP] Connection from: %s\n", conn->peerAddr().toIpPort().c_str());
            LOG(INFO) << "TCP connection opened"
                      << " client=" << conn->peerAddr().toIpPort()
                      << " local=" << conn->localAddr().toIpPort();
            std::lock_guard<std::mutex> lock(impl.tcp_buffer_mutex);
            impl.tcp_buffers[conn].clear();
            return;
        }

        size_t buffered_bytes = 0;
        {
            std::lock_guard<std::mutex> lock(impl.tcp_buffer_mutex);
            if (const auto it = impl.tcp_buffers.find(conn); it != impl.tcp_buffers.end()) {
                buffered_bytes = it->second.size();
                impl.tcp_buffers.erase(it);
            }
        }
        LOG(INFO) << "TCP connection closed"
                  << " client=" << conn->peerAddr().toIpPort()
                  << " buffered_bytes_discarded=" << buffered_bytes;
    });

    impl.tcp_server->setRecvMessageCallback(
        [&impl](const trantor::TcpConnectionPtr& conn, trantor::MsgBuffer* buffer) {
            const std::string chunk(buffer->peek(), buffer->readableBytes());
            buffer->retrieveAll();

            {
                std::lock_guard<std::mutex> lock(impl.tcp_buffer_mutex);
                impl.tcp_buffers[conn].append(chunk);
            }

            if (auto payload = take_complete_payload(impl, conn); !payload.has_value()) {
                size_t accumulated_bytes = 0;
                {
                    std::lock_guard<std::mutex> lock(impl.tcp_buffer_mutex);
                    accumulated_bytes = impl.tcp_buffers[conn].size();
                }
                LOG(INFO) << "TCP partial payload received from " << conn->peerAddr().toIpPort()
                          << " chunk_bytes=" << chunk.size()
                          << " accumulated_bytes=" << accumulated_bytes;
                return;
            } else {
                impl.total_requests.fetch_add(1);
                const auto request_id = impl.request_sequence.fetch_add(1) + 1;
                const auto remote = conn->peerAddr().toIpPort();
                std::printf("[TCP][%s] Received %zu bytes\n", remote.c_str(), payload->payload.size());
                LOG(INFO) << "TCP request begin"
                          << " request_id=" << request_id
                          << " client=" << remote
                          << " bytes=" << payload->payload.size()
                          << " trailing_bytes=" << payload->trailing_bytes;

                std::vector<uint8_t> image_bytes(payload->payload.begin(), payload->payload.end());
                if (!impl.submit_predict(
                        std::move(image_bytes),
                        [&impl, conn, remote, request_id](PredictResult result, PredictExecutionInfo info) {
                            if (result.success) {
                                impl.successful_requests.fetch_add(1);
                                std::printf("[TCP][%s] Prediction succeeded: %s => %d\n",
                                            remote.c_str(),
                                            result.expression.c_str(),
                                            result.result);
                            } else {
                                impl.failed_requests.fetch_add(1);
                                std::printf("[TCP][%s] Prediction failed: %s\n",
                                            remote.c_str(),
                                            result.error.c_str());
                            }
                            LOG(INFO) << "TCP request completed"
                                      << " request_id=" << request_id
                                      << " client=" << remote
                                      << " success=" << result.success
                                      << " expression=\"" << result.expression << "\""
                                      << " result=" << result.result
                                      << " error=\"" << result.error << "\""
                                      << " input_bytes=" << info.input_bytes
                                      << " worker_index=" << info.worker_index
                                      << " queue_wait_ms=" << to_millis(info.queue_wait)
                                      << " inference_ms=" << to_millis(info.inference_time)
                                      << " total_ms=" << to_millis(info.total_time);

                            const auto response = result.success ? result.expression : std::string{};
                            conn->getLoop()->queueInLoop(
                                [conn, response, request_id, remote]() mutable {
                                    if (!conn->connected()) {
                                        LOG(WARNING) << "TCP response dropped because connection is already closed"
                                                     << " request_id=" << request_id
                                                     << " client=" << remote;
                                        return;
                                    }
                                    LOG(INFO) << "TCP sending response"
                                              << " request_id=" << request_id
                                              << " client=" << remote
                                              << " response_bytes=" << response.size();
                                    conn->send(response);
                                    conn->shutdown();
                                    LOG(INFO) << "TCP response sent and connection shutdown requested"
                                              << " request_id=" << request_id
                                              << " client=" << remote;
                                });
                        })) {
                    impl.failed_requests.fetch_add(1);
                    std::printf("[TCP][%s] Queue is full, request rejected\n", remote.c_str());
                    LOG(WARNING) << "TCP request rejected because queue is full"
                                 << " request_id=" << request_id
                                 << " client=" << remote
                                 << " pending_requests=" << (impl.pool ? impl.pool->pending_tasks.load() : 0)
                                 << " queue_capacity=" << impl.config.queue_capacity;
                    conn->send("");
                    conn->shutdown();
                    return;
                } else {
                    LOG(INFO) << "TCP request queued"
                              << " request_id=" << request_id
                              << " client=" << remote
                              << " pending_requests=" << (impl.pool ? impl.pool->pending_tasks.load() : 0)
                              << " queue_capacity=" << impl.config.queue_capacity;
                }
            }
        });

    impl.tcp_server->start();
    std::printf("TCP server starting on %s:%d\n",
                impl.config.tcp_host.c_str(),
                impl.config.tcp_port);
    LOG(INFO) << "TCP server started on "
              << impl.config.tcp_host << ":" << impl.config.tcp_port;
}

void stop_tcp_server(OcrServer::Impl& impl) {
    LOG(INFO) << "Stopping TCP server";
    if (impl.tcp_server) {
        impl.tcp_server->stop();
        impl.tcp_server.reset();
    }

    if (impl.tcp_loop_thread) {
        if (auto* loop = impl.tcp_loop_thread->getLoop()) {
            loop->quit();
        }
        impl.tcp_loop_thread->wait();
        impl.tcp_loop_thread.reset();
    }

    std::lock_guard<std::mutex> lock(impl.tcp_buffer_mutex);
    impl.tcp_buffers.clear();
    LOG(INFO) << "TCP server stopped";
}

} // namespace shmtu::cas::ocr
