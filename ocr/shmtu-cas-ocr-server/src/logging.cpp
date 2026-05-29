#include "logging.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>

namespace shmtu::cas::ocr {

namespace {

bool is_probably_server_log(const std::filesystem::path& path, const ServerConfig& config) {
    const auto filename = path.filename().string();
    if (filename.rfind(config.log_file_prefix + ".", 0) != 0) {
        return false;
    }
    return filename.find(".INFO.") != std::string::npos ||
           filename.find(".WARNING.") != std::string::npos ||
           filename.find(".ERROR.") != std::string::npos ||
           filename.find(".FATAL.") != std::string::npos;
}

}  // namespace

void init_server_logging(const ServerConfig& config, const char* argv0) {
    google::InitGoogleLogging(argv0);
    google::InstallFailureSignalHandler();

    FLAGS_log_dir = config.log_dir;
    FLAGS_minloglevel = config.log_min_level;
    FLAGS_logtostderr = config.log_to_stderr;
    FLAGS_alsologtostderr = config.also_log_to_stderr;
    FLAGS_colorlogtostderr = true;
    FLAGS_logbufsecs = 0;
    FLAGS_max_log_size = config.log_max_size_mb;
    FLAGS_stop_logging_if_full_disk = true;

    std::filesystem::create_directories(config.log_dir);
    LOG(INFO) << "glog initialized"
              << " log_dir=" << config.log_dir
              << " min_level=" << config.log_min_level
              << " log_to_stderr=" << config.log_to_stderr
              << " also_log_to_stderr=" << config.also_log_to_stderr
              << " max_size_mb=" << config.log_max_size_mb
              << " cleanup_interval_secs=" << config.log_cleanup_interval_secs
              << " retention_days=" << config.log_retention_days;
}

void shutdown_server_logging() {
    LOG(INFO) << "shutting down glog";
    google::ShutdownGoogleLogging();
}

std::string describe_http_client(const drogon::HttpRequestPtr& req) {
    const auto forwarded_for = req->getHeader("x-forwarded-for");
    const auto real_ip = req->getHeader("x-real-ip");
    const auto user_agent = req->getHeader("user-agent");
    std::string client = req->peerAddr().toIpPort();
    if (!forwarded_for.empty()) {
        client += " forwarded_for=" + forwarded_for;
    }
    if (!real_ip.empty()) {
        client += " real_ip=" + real_ip;
    }
    if (!user_agent.empty()) {
        client += " ua=\"" + user_agent + "\"";
    }
    return client;
}

void cleanup_old_log_files(const ServerConfig& config) {
    if (config.log_cleanup_interval_secs <= 0 || config.log_retention_days <= 0) {
        return;
    }

    std::error_code ec;
    if (!std::filesystem::exists(config.log_dir, ec)) {
        return;
    }

    const auto now = std::filesystem::file_time_type::clock::now();
    const auto retention = std::chrono::hours(24 * config.log_retention_days);
    size_t removed_count = 0;

    for (const auto& entry : std::filesystem::directory_iterator(config.log_dir, ec)) {
        if (ec) {
            LOG(WARNING) << "log cleanup: directory iteration failed, log_dir=" << config.log_dir
                         << " error=" << ec.message();
            break;
        }
        if (!entry.is_regular_file(ec) || ec) {
            continue;
        }
        if (!is_probably_server_log(entry.path(), config)) {
            continue;
        }

        const auto write_time = entry.last_write_time(ec);
        if (ec) {
            continue;
        }
        if (now - write_time < retention) {
            continue;
        }

        std::filesystem::remove(entry.path(), ec);
        if (!ec) {
            ++removed_count;
            LOG(INFO) << "log cleanup: removed old log file " << entry.path().string();
        }
    }

    if (removed_count > 0) {
        LOG(INFO) << "log cleanup completed, removed_count=" << removed_count;
    }
}

}  // namespace shmtu::cas::ocr
