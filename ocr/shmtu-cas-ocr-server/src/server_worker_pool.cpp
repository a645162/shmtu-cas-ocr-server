#include "server_internal.h"
#include "logging.h"

#include <exception>
#include <ranges>
#include <utility>

namespace shmtu::cas::ocr {

void OcrWorkerPool::add_worker(std::unique_ptr<CasOcr> ocr) {
    workers.push_back(std::move(ocr));
    LOG(INFO) << "Worker added, total_workers=" << workers.size();
}

void OcrWorkerPool::start() {
    accepting.store(true);
    LOG(INFO) << "Starting worker pool threads, count=" << workers.size();
    for (const auto i : std::views::iota(size_t{0}, workers.size())) {
        threads.emplace_back([this, idx = i](std::stop_token stop_token) {
            LOG(INFO) << "Worker thread " << idx << " started";
            while (!stop_token.stop_requested()) {
                if (!semaphore.try_acquire_for(std::chrono::milliseconds(100))) {
                    continue;
                }

                std::function<void(CasOcr&, size_t)> task;
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    if (task_queue.empty()) {
                        continue;
                    }
                    task = std::move(task_queue.front());
                    task_queue.pop();
                    pending_tasks.fetch_sub(1);
                    LOG(INFO) << "Worker " << idx << " dequeued task"
                              << " pending_tasks=" << pending_tasks.load();
                }

                active_workers.fetch_add(1);
                LOG(INFO) << "Worker " << idx << " executing task"
                          << " active_workers=" << active_workers.load();
                try {
                    task(*workers[idx], idx);
                } catch (...) {
                    // Keep the worker alive even if a completion callback throws.
                    LOG(ERROR) << "Worker " << idx << " caught exception from task";
                }
                active_workers.fetch_sub(1);
                LOG(INFO) << "Worker " << idx << " finished task"
                          << " active_workers=" << active_workers.load();
            }
            LOG(INFO) << "Worker thread " << idx << " stopping";
        });
    }
}

bool OcrWorkerPool::submit(std::function<void(CasOcr&, size_t)> task, int max_queue) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        if (!accepting.load()) {
            LOG(WARNING) << "Task submission rejected because pool is not accepting";
            return false;
        }
        if (static_cast<int>(task_queue.size()) >= max_queue) {
            LOG(WARNING) << "Task submission rejected because queue is full"
                         << " queue_size=" << task_queue.size()
                         << " max_queue=" << max_queue;
            return false;
        }
        task_queue.push(std::move(task));
        pending_tasks.fetch_add(1);
        LOG(INFO) << "Task queued"
                  << " queue_size=" << task_queue.size()
                  << " pending_tasks=" << pending_tasks.load();
    }

    semaphore.release();
    return true;
}

void OcrWorkerPool::stop_all() {
    accepting.store(false);
    LOG(INFO) << "Stopping all worker threads";
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
    LOG(INFO) << "All worker threads stopped";
}

OcrServer::Impl::Impl(const ServerConfig& cfg)
    : config(cfg) {
}

bool OcrServer::Impl::submit_predict(
    std::span<const uint8_t> image_bytes,
    std::function<void(PredictResult, PredictExecutionInfo)> on_complete) {
    return submit_predict(
        std::vector<uint8_t>(image_bytes.begin(), image_bytes.end()),
        std::move(on_complete));
}

bool OcrServer::Impl::submit_predict(
    std::vector<uint8_t> image_bytes,
    std::function<void(PredictResult, PredictExecutionInfo)> on_complete) {
    const auto queued_at = std::chrono::steady_clock::now();
    const auto input_bytes = image_bytes.size();
    LOG(INFO) << "Predict task submit requested"
              << " input_bytes=" << input_bytes
              << " pending_requests_before=" << (pool ? pool->pending_tasks.load() : 0)
              << " queue_capacity=" << config.queue_capacity;
    const bool accepted = pool->submit(
        [queued_at, input_bytes, image_bytes = std::move(image_bytes), on_complete = std::move(on_complete)](
            CasOcr& ocr, size_t worker_index) mutable {
            const auto started_at = std::chrono::steady_clock::now();
            const auto queue_wait = std::chrono::duration_cast<std::chrono::microseconds>(started_at - queued_at);
            const auto infer_started = std::chrono::steady_clock::now();
            auto result = ocr.predict(std::span<const uint8_t>(image_bytes));
            const auto infer_finished = std::chrono::steady_clock::now();
            PredictExecutionInfo info;
            info.input_bytes = input_bytes;
            info.worker_index = worker_index;
            info.queue_wait = queue_wait;
            info.inference_time = std::chrono::duration_cast<std::chrono::microseconds>(infer_finished - infer_started);
            info.total_time = std::chrono::duration_cast<std::chrono::microseconds>(infer_finished - queued_at);
            on_complete(std::move(result), info);
        },
        config.queue_capacity);
    LOG(INFO) << "Predict task submit result"
              << " accepted=" << accepted
              << " input_bytes=" << input_bytes
              << " pending_requests_after=" << (pool ? pool->pending_tasks.load() : 0)
              << " queue_capacity=" << config.queue_capacity;
    return accepted;
}

} // namespace shmtu::cas::ocr
