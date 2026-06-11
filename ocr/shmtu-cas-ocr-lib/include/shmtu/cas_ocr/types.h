#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

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

// Model version selector.
// V1 — original 3-model pipeline (resnet18 equal-symbol / resnet18 operator /
//      resnet34 digit). Supports 6 operator classes (with CHS variants).
// V2 — single trislot-decoder model that emits three heads (digit_left, operator,
//      digit_right) in a single forward pass. Supports 3 operator classes
//      (Add / Sub / Mul, no CHS variants). The equal-symbol field is N/A.
enum class ModelVersion {
    V1 = 1,
    V2 = 2
};

// String <-> ModelVersion helpers (used by CLI / server config parsing).
std::string model_version_to_string(ModelVersion version);
ModelVersion model_version_from_string(const std::string& value);

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
    int equal_symbol = 0;       // EqualSymbol enum value (V1). -1 in V2 (N/A).
    int op = 0;                 // Operator enum value
    int digit1 = 0;             // First operand
    int digit2 = 0;             // Second operand
    bool success = false;       // Whether prediction succeeded
    std::string error;          // Error message if prediction failed
    int model_version = 2;      // ModelVersion int value used to produce this result
};

// GPU device information (only available with Vulkan support).
struct GpuDeviceInfo {
    int device_index = 0;
    std::string device_name;
    uint32_t api_version = 0;
    uint32_t device_memory = 0;     // MB
    VulkanDeviceType device_type = VulkanDeviceType::DiscreteGPU;
};

// --------------------------------------------------------------------------
// Release manifest types
// --------------------------------------------------------------------------
//
// The release manifest (model-assets.json) ships at the root of every
// GitHub/Gitee release for `a645162/shmtu-cas-ocr-model`.  Schema version 2
// describes a structured, multi-model catalogue where each model carries
// metrics (val/test accuracy and loss) and per-engine, per-precision
// artifact descriptors with embedded SHA256 digests.  Earlier schemas
// exposed only a flat `artifacts` list; `ReleaseManifest::flat_artifacts`
// preserves that view for backwards compatibility.

// Optional model metrics — every field is optional because the manifest
// may carry only some of them depending on the release.
struct ModelMetrics {
    std::optional<double> val_acc_expression;
    std::optional<double> val_loss;
    std::optional<double> test_acc_expression;
    std::optional<double> test_loss;
};

// One physical file that belongs to an artifact (e.g. a `.param` or `.bin`).
struct AssetFile {
    std::string path;                  // logical path inside the artifact (e.g. "ncnn/...param")
    std::string release_asset_name;    // asset name in the GitHub/Gitee release
    std::string sha256;                // lowercase hex digest
};

// One (engine, precision) artifact descriptor.
// `format` describes the artifact family (e.g. "ncnn", "onnx", "checkpoint").
struct ArtifactInfo {
    std::string engine;     // "ncnn", "onnx", "pytorch"
    std::string precision;  // "fp32", "fp16"
    std::string format;     // "ncnn", "onnx", "checkpoint"
    std::vector<AssetFile> files;
};

// One logical model — typically a single TriSlot-decoder network.
struct ModelInfo {
    std::string asset_stem;                                  // e.g. "mobilenet_v3_small.trislot_decoder.v2_0"
    std::string display_name;                                // human-readable label
    std::string backbone;                                    // "mobilenet_v3_small", "mobilenetv4_conv_small"
    std::string version;                                     // "2.0", "2.1", ...
    std::string family;                                      // "trislot_decoder"
    std::optional<double> model_size_m;                      // size in millions of parameters
    std::optional<ModelMetrics> metrics;
    std::vector<std::string> supported_backbones;
    // engine -> precision -> artifact
    std::map<std::string, std::map<std::string, ArtifactInfo>> artifacts;
};

// The complete release manifest.
struct ReleaseManifest {
    int schema_version = 0;
    int model_count = 0;
    std::vector<std::string> modellist;
    std::vector<ModelInfo> models;
    std::vector<ArtifactInfo> flat_artifacts;  // legacy v1 manifest view
};

} // namespace shmtu::cas::ocr
