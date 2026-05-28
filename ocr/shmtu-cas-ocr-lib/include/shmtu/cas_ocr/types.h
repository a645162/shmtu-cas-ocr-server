#pragma once

#include <cstdint>
#include <string>

namespace shmtu::cas::ocr {

// Operator type recognized from the captcha image.
// CHS variants indicate Chinese-character style operators.
enum class Operator {
    Add = 0,
    AddCHS = 1,
    Sub = 2,
    SubCHS = 3,
    Mul = 4,
    MulCHS = 5
};

// Equal symbol style in the captcha.
enum class EqualSymbol {
    CHS = 0,    // Chinese style equal sign
    Symbol = 1  // Standard '=' symbol
};

// Model loading status.
enum class ModelStatus {
    NotLoaded = 0,
    LoadedCPU = 1,
    LoadedGPU = 2
};

// Vulkan device type (only meaningful when NCNN_SUPPORT_VULKAN is defined).
enum class VulkanDeviceType {
    DiscreteGPU = 0,
    IntegratedGPU = 1,
    VirtualGPU = 2,
    CPU = 3
};

// Result of a CAPTCHA prediction.
struct PredictResult {
    int result = 0;             // Calculated answer
    std::string expression;     // Full expression string, e.g. "3 + 5 = 8"
    int equal_symbol = 0;       // EqualSymbol enum value
    int op = 0;                 // Operator enum value
    int digit1 = 0;             // First operand
    int digit2 = 0;             // Second operand
    bool success = false;       // Whether prediction succeeded
    std::string error;          // Error message if prediction failed
};

// GPU device information (only available with Vulkan support).
struct GpuDeviceInfo {
    int device_index = 0;
    std::string device_name;
    uint32_t api_version = 0;
    uint32_t device_memory = 0;     // MB
    VulkanDeviceType device_type = VulkanDeviceType::DiscreteGPU;
};

} // namespace shmtu::cas::ocr
