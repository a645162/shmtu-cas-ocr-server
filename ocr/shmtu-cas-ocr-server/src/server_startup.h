#pragma once

#include <shmtu/cas_ocr/server.h>

namespace shmtu::cas::ocr {

void print_banner();
void install_signal_handlers(OcrServer& server);
void clear_signal_handler_target();

}  // namespace shmtu::cas::ocr
