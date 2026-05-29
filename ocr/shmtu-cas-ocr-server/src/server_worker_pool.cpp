#include "server_internal.h"

#include <ranges>
#include <utility>

namespace shmtu::cas::ocr {

namespace {

static const char kBase64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

} // namespace

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

void OcrWorkerPool::add_worker(std::unique_ptr<CasOcr> ocr) {
    workers.push_back(std::move(ocr));
}

void OcrWorkerPool::start() {
    accepting.store(true);
    for (const auto i : std::views::iota(size_t{0}, workers.size())) {
        threads.emplace_back([this, idx = i](std::stop_token stop_token) {
            while (!stop_token.stop_requested()) {
                if (!semaphore.try_acquire_for(std::chrono::milliseconds(100))) {
                    continue;
                }

                std::function<void(CasOcr&)> task;
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    if (task_queue.empty()) {
                        continue;
                    }
                    task = std::move(task_queue.front());
                    task_queue.pop();
                    pending_tasks.fetch_sub(1);
                }

                active_workers.fetch_add(1);
                try {
                    task(*workers[idx]);
                } catch (...) {
                    // Keep the worker alive even if a completion callback throws.
                }
                active_workers.fetch_sub(1);
            }
        });
    }
}

bool OcrWorkerPool::submit(std::function<void(CasOcr&)> task, int max_queue) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        if (!accepting.load()) {
            return false;
        }
        if (static_cast<int>(task_queue.size()) >= max_queue) {
            return false;
        }
        task_queue.push(std::move(task));
        pending_tasks.fetch_add(1);
    }

    semaphore.release();
    return true;
}

void OcrWorkerPool::stop_all() {
    accepting.store(false);
    for (auto& thread : threads) {
        thread.request_stop();
    }
    semaphore.release(static_cast<std::ptrdiff_t>(workers.size()));
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    threads.clear();
}

OcrServer::Impl::Impl(const ServerConfig& cfg)
    : config(cfg) {
}

bool OcrServer::Impl::submit_predict(
    std::span<const uint8_t> image_bytes,
    std::function<void(PredictResult)> on_complete) {
    return submit_predict(
        std::vector<uint8_t>(image_bytes.begin(), image_bytes.end()),
        std::move(on_complete));
}

bool OcrServer::Impl::submit_predict(
    std::vector<uint8_t> image_bytes,
    std::function<void(PredictResult)> on_complete) {
    return pool->submit(
        [image_bytes = std::move(image_bytes), on_complete = std::move(on_complete)](CasOcr& ocr) mutable {
            on_complete(ocr.predict(std::span<const uint8_t>(image_bytes)));
        },
        config.queue_capacity);
}

} // namespace shmtu::cas::ocr
