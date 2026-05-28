#include "server_internal.h"

#include <utility>

namespace shmtu::cas::ocr {

namespace {

static const char kBase64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

} // namespace

std::vector<uint8_t> base64_decode(std::string_view input) {
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
    for (unsigned char c : input) {
        if (decode_table[c] == -1) {
            if (c == '=') {
                break;
            }
            continue;
        }
        value = (value << 6) | decode_table[c];
        bits += 6;
        if (bits >= 0) {
            result.push_back(static_cast<uint8_t>((value >> bits) & 0xFF));
            bits -= 8;
        }
    }

    return result;
}

void OcrWorkerPool::add_worker(std::unique_ptr<CasOcr> ocr) {
    workers.push_back(std::move(ocr));
}

void OcrWorkerPool::start() {
    for (size_t i = 0; i < workers.size(); ++i) {
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
                task(*workers[idx]);
                active_workers.fetch_sub(1);
            }
        });
    }
}

bool OcrWorkerPool::submit(std::function<void(CasOcr&)> task, int max_queue) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
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

PredictResult OcrServer::Impl::predict_sync(
    const std::vector<uint8_t>& image_bytes,
    bool& queued_ok) {
    PredictResult result;
    std::mutex result_mutex;
    std::condition_variable result_cv;
    bool done = false;

    queued_ok = pool->submit([&](CasOcr& ocr) {
        result = ocr.predict(image_bytes);
        {
            std::lock_guard<std::mutex> lock(result_mutex);
            done = true;
        }
        result_cv.notify_one();
    }, config.queue_capacity);

    if (!queued_ok) {
        result.success = false;
        result.error = "Server overloaded";
        return result;
    }

    std::unique_lock<std::mutex> lock(result_mutex);
    if (!result_cv.wait_for(lock, std::chrono::seconds(30), [&] { return done; })) {
        result.success = false;
        result.error = "Prediction timeout";
    }

    return result;
}

} // namespace shmtu::cas::ocr
