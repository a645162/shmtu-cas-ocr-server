#pragma once

#include <string>

namespace shmtu::cas::ocr::gui {

void installCrashHandlers();
void logMessage(const std::string& message);

}  // namespace shmtu::cas::ocr::gui
