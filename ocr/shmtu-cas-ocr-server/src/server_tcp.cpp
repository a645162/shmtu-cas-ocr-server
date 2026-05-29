#include "server_internal.h"

#include <cstdio>
#include <utility>
#include <vector>

#include <trantor/net/EventLoop.h>
#include <trantor/net/InetAddress.h>
#include <trantor/utils/MsgBuffer.h>

namespace shmtu::cas::ocr {

namespace {

constexpr std::string_view kEndMarker = "<END>";

std::optional<std::string> take_complete_payload(
    OcrServer::Impl& impl,
    const trantor::TcpConnectionPtr& conn) {
    std::lock_guard<std::mutex> lock(impl.tcp_buffer_mutex);
    auto& accumulated = impl.tcp_buffers[conn];
    const auto pos = accumulated.find(kEndMarker);
    if (pos == std::string::npos) {
        return std::nullopt;
    }

    auto payload = accumulated.substr(0, pos);
    impl.tcp_buffers.erase(conn);
    return payload;
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
            std::lock_guard<std::mutex> lock(impl.tcp_buffer_mutex);
            impl.tcp_buffers[conn].clear();
            return;
        }

        std::lock_guard<std::mutex> lock(impl.tcp_buffer_mutex);
        impl.tcp_buffers.erase(conn);
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
                return;
            } else {
                impl.total_requests.fetch_add(1);
                const auto remote = conn->peerAddr().toIpPort();
                std::printf("[TCP][%s] Received %zu bytes\n", remote.c_str(), payload->size());

                std::vector<uint8_t> image_bytes(payload->begin(), payload->end());
                if (!impl.submit_predict(
                        std::move(image_bytes),
                        [&impl, conn, remote](PredictResult result) {
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

                            const auto response = result.success ? result.expression : std::string{};
                            conn->getLoop()->queueInLoop(
                                [conn, response]() mutable {
                                    if (!conn->connected()) {
                                        return;
                                    }
                                    conn->send(response);
                                    conn->shutdown();
                                });
                        })) {
                    impl.failed_requests.fetch_add(1);
                    std::printf("[TCP][%s] Queue is full, request rejected\n", remote.c_str());
                    conn->send("");
                    conn->shutdown();
                    return;
                }
            }
        });

    impl.tcp_server->start();
    std::printf("TCP server starting on %s:%d\n",
                impl.config.tcp_host.c_str(),
                impl.config.tcp_port);
}

void stop_tcp_server(OcrServer::Impl& impl) {
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
}

} // namespace shmtu::cas::ocr
