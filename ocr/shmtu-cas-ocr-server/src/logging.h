#pragma once

#include "server_internal.h"

#include <glog/logging.h>

namespace shmtu::cas::ocr {

inline long long to_millis(std::chrono::microseconds value) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(value).count();
}

}  // namespace shmtu::cas::ocr
