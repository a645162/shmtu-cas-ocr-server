#include <shmtu/cas_ocr/gui/logging.h>

#include <csignal>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>

#include <fcntl.h>
#include <unistd.h>

namespace shmtu::cas_ocr::gui {
namespace {

constexpr auto LOG_PATH = "/tmp/shmtu_cas_ocr_gui.log";

std::mutex g_log_mutex;
int g_log_fd = -1;

void writeRawLogLine(const std::string& line) {
    if (g_log_fd >= 0) {
        (void)::write(g_log_fd, line.data(), line.size());
    }
    (void)::write(STDERR_FILENO, line.data(), line.size());
}

void initLogging() {
    if (g_log_fd >= 0) {
        return;
    }
    g_log_fd = ::open(LOG_PATH, O_CREAT | O_WRONLY | O_APPEND, 0644);
}

void signalLogHandler(int sig) {
    const char* name = "UNKNOWN";
    switch (sig) {
        case SIGABRT: name = "SIGABRT"; break;
        case SIGSEGV: name = "SIGSEGV"; break;
        case SIGILL: name = "SIGILL"; break;
        case SIGFPE: name = "SIGFPE"; break;
#ifdef SIGBUS
        case SIGBUS: name = "SIGBUS"; break;
#endif
        case SIGTERM: name = "SIGTERM"; break;
        default: break;
    }

    char buf[128];
    const int len = std::snprintf(buf, sizeof(buf),
                                  "[fatal] received signal %s (%d)\n", name, sig);
    if (len > 0) {
        if (g_log_fd >= 0) {
            (void)::write(g_log_fd, buf, static_cast<size_t>(len));
        }
        (void)::write(STDERR_FILENO, buf, static_cast<size_t>(len));
    }

    std::_Exit(128 + sig);
}

}  // namespace

void installCrashHandlers() {
    initLogging();
    std::signal(SIGABRT, signalLogHandler);
    std::signal(SIGSEGV, signalLogHandler);
    std::signal(SIGILL, signalLogHandler);
    std::signal(SIGFPE, signalLogHandler);
#ifdef SIGBUS
    std::signal(SIGBUS, signalLogHandler);
#endif
    std::signal(SIGTERM, signalLogHandler);
}

void logMessage(const std::string& message) {
    initLogging();

    std::lock_guard<std::mutex> lock(g_log_mutex);

    std::time_t now = std::time(nullptr);
    std::tm tm_now {};
    localtime_r(&now, &tm_now);

    char prefix[64];
    std::snprintf(prefix, sizeof(prefix), "[%04d-%02d-%02d %02d:%02d:%02d] ",
                  tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
                  tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);

    std::string line(prefix);
    line += message;
    line += '\n';
    writeRawLogLine(line);
}

}  // namespace shmtu::cas_ocr::gui
