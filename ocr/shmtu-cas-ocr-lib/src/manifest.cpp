#include <shmtu/cas_ocr/manifest.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace shmtu::cas::ocr {

namespace {

// --------------------------------------------------------------------------
// Hand-rolled, narrow JSON walker.
//
// The manifest format is well-defined JSON, but we only need a small
// subset: objects, arrays, strings, numbers, booleans, null.  We tokenise
// on demand and walk the document recursively, materialising only the
// fields we care about.  This avoids pulling in nlohmann/json and keeps
// the lib's dependency surface unchanged.
// --------------------------------------------------------------------------

struct JsonValue;

using JsonObject = std::vector<std::pair<std::string, JsonValue>>;
using JsonArray  = std::vector<JsonValue>;

enum class JsonType {
    Null,
    Bool,
    Number,    // stored as double
    String,
    Array,
    Object
};

struct JsonValue {
    JsonType type = JsonType::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    JsonArray array;
    JsonObject object;

    bool is_null() const { return type == JsonType::Null; }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view text) : text_(text) {}

    bool parse(JsonValue& root) {
        skip_whitespace();
        if (!parse_value(root)) {
            return false;
        }
        skip_whitespace();
        return pos_ == text_.size();
    }

private:
    std::string_view text_;
    std::size_t pos_ = 0;

    void skip_whitespace() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool consume(char expected) {
        skip_whitespace();
        if (pos_ >= text_.size() || text_[pos_] != expected) {
            return false;
        }
        ++pos_;
        return true;
    }

    bool parse_value(JsonValue& v) {
        skip_whitespace();
        if (pos_ >= text_.size()) return false;
        const char c = text_[pos_];
        if (c == '{') return parse_object(v);
        if (c == '[') return parse_array(v);
        if (c == '"') return parse_string(v);
        if (c == 't' || c == 'f') return parse_bool(v);
        if (c == 'n') return parse_null(v);
        return parse_number(v);
    }

    bool parse_object(JsonValue& v) {
        v.type = JsonType::Object;
        if (!consume('{')) return false;
        skip_whitespace();
        if (consume('}')) return true;
        while (true) {
            skip_whitespace();
            JsonValue key;
            if (!parse_string(key)) return false;
            skip_whitespace();
            if (!consume(':')) return false;
            JsonValue value;
            if (!parse_value(value)) return false;
            v.object.emplace_back(std::move(key.string), std::move(value));
            skip_whitespace();
            if (consume(',')) continue;
            if (consume('}')) return true;
            return false;
        }
    }

    bool parse_array(JsonValue& v) {
        v.type = JsonType::Array;
        if (!consume('[')) return false;
        skip_whitespace();
        if (consume(']')) return true;
        while (true) {
            JsonValue item;
            if (!parse_value(item)) return false;
            v.array.push_back(std::move(item));
            skip_whitespace();
            if (consume(',')) continue;
            if (consume(']')) return true;
            return false;
        }
    }

    bool parse_string(JsonValue& v) {
        v.type = JsonType::String;
        if (!consume('"')) return false;
        std::string out;
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') {
                v.string = std::move(out);
                return true;
            }
            if (c == '\\') {
                if (pos_ >= text_.size()) return false;
                const char esc = text_[pos_++];
                switch (esc) {
                    case '"':  out.push_back('"');  break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/');  break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u': {
                        if (pos_ + 4 > text_.size()) return false;
                        // Lightweight \uXXXX -> UTF-8 byte passthrough.  The
                        // manifest doesn't use anything beyond ASCII so we
                        // don't need full surrogate handling.
                        unsigned int cp = 0;
                        for (int i = 0; i < 4; ++i) {
                            const char hc = text_[pos_++];
                            cp <<= 4;
                            if (hc >= '0' && hc <= '9') cp |= (hc - '0');
                            else if (hc >= 'a' && hc <= 'f') cp |= (hc - 'a' + 10);
                            else if (hc >= 'A' && hc <= 'F') cp |= (hc - 'A' + 10);
                            else return false;
                        }
                        if (cp < 0x80) {
                            out.push_back(static_cast<char>(cp));
                        } else if (cp < 0x800) {
                            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        } else {
                            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        }
                        break;
                    }
                    default:
                        // Unknown escape — preserve the literal char.
                        out.push_back(esc);
                        break;
                }
            } else {
                out.push_back(c);
            }
        }
        return false;  // unterminated string
    }

    bool parse_bool(JsonValue& v) {
        if (text_.substr(pos_, 4) == "true") {
            pos_ += 4;
            v.type = JsonType::Bool;
            v.boolean = true;
            return true;
        }
        if (text_.substr(pos_, 5) == "false") {
            pos_ += 5;
            v.type = JsonType::Bool;
            v.boolean = false;
            return true;
        }
        return false;
    }

    bool parse_null(JsonValue& v) {
        if (text_.substr(pos_, 4) == "null") {
            pos_ += 4;
            v.type = JsonType::Null;
            return true;
        }
        return false;
    }

    bool parse_number(JsonValue& v) {
        const std::size_t start = pos_;
        if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) ++pos_;
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
                ++pos_;
            } else {
                break;
            }
        }
        if (start == pos_) return false;
        v.type = JsonType::Number;
        v.number = std::stod(std::string(text_.substr(start, pos_ - start)));
        return true;
    }
};

// --------------------------------------------------------------------------
// Lookup helpers over the parsed JSON tree.
// --------------------------------------------------------------------------

const JsonValue* find_member(const JsonValue& obj, std::string_view key) {
    if (obj.type != JsonType::Object) return nullptr;
    for (const auto& kv : obj.object) {
        if (kv.first == key) return &kv.second;
    }
    return nullptr;
}

std::optional<double> as_number(const JsonValue* v) {
    if (!v || v->type != JsonType::Number) return std::nullopt;
    return v->number;
}

// Parse one `AssetFile` JSON object.
AssetFile parse_asset_file(const JsonValue& obj) {
    AssetFile out;
    if (const JsonValue* p = find_member(obj, "path"); p && p->type == JsonType::String) {
        out.path = p->string;
    }
    if (const JsonValue* p = find_member(obj, "release_asset_name"); p && p->type == JsonType::String) {
        out.release_asset_name = p->string;
    }
    if (const JsonValue* p = find_member(obj, "sha256"); p && p->type == JsonType::String) {
        out.sha256 = p->string;
    }
    return out;
}

// Parse one `ArtifactInfo` JSON object.
ArtifactInfo parse_artifact(const JsonValue& obj) {
    ArtifactInfo out;
    if (const JsonValue* p = find_member(obj, "engine"); p && p->type == JsonType::String) {
        out.engine = p->string;
    }
    if (const JsonValue* p = find_member(obj, "precision"); p && p->type == JsonType::String) {
        out.precision = p->string;
    }
    if (const JsonValue* p = find_member(obj, "format"); p && p->type == JsonType::String) {
        out.format = p->string;
    }
    if (const JsonValue* p = find_member(obj, "files"); p && p->type == JsonType::Array) {
        out.files.reserve(p->array.size());
        for (const auto& f : p->array) {
            if (f.type == JsonType::Object) {
                out.files.push_back(parse_asset_file(f));
            }
        }
    }
    return out;
}

// Parse one `ModelMetrics` JSON object.
std::optional<ModelMetrics> parse_metrics(const JsonValue* obj) {
    if (!obj || obj->type != JsonType::Object) return std::nullopt;
    ModelMetrics m;
    m.val_acc_expression  = as_number(find_member(*obj, "val_acc_expression"));
    m.val_loss            = as_number(find_member(*obj, "val_loss"));
    m.test_acc_expression = as_number(find_member(*obj, "test_acc_expression"));
    m.test_loss           = as_number(find_member(*obj, "test_loss"));
    // Treat an entirely-empty object as "no metrics".
    if (!m.val_acc_expression && !m.val_loss &&
        !m.test_acc_expression && !m.test_loss) {
        return std::nullopt;
    }
    return m;
}

// Parse one `ModelInfo` JSON object.
ModelInfo parse_model(const JsonValue& obj) {
    ModelInfo out;
    if (const JsonValue* p = find_member(obj, "asset_stem"); p && p->type == JsonType::String) {
        out.asset_stem = p->string;
    }
    if (const JsonValue* p = find_member(obj, "display_name"); p && p->type == JsonType::String) {
        out.display_name = p->string;
    }
    if (const JsonValue* p = find_member(obj, "backbone"); p && p->type == JsonType::String) {
        out.backbone = p->string;
    }
    if (const JsonValue* p = find_member(obj, "version"); p && p->type == JsonType::String) {
        out.version = p->string;
    }
    if (const JsonValue* p = find_member(obj, "family"); p && p->type == JsonType::String) {
        out.family = p->string;
    }
    if (const auto n = as_number(find_member(obj, "model_size_m")); n) {
        out.model_size_m = n;
    }
    if (const JsonValue* p = find_member(obj, "metrics"); p && p->type == JsonType::Object) {
        out.metrics = parse_metrics(p);
    }
    if (const JsonValue* p = find_member(obj, "supported_backbones"); p && p->type == JsonType::Array) {
        out.supported_backbones.reserve(p->array.size());
        for (const auto& v : p->array) {
            if (v.type == JsonType::String) {
                out.supported_backbones.push_back(v.string);
            }
        }
    }
    if (const JsonValue* p = find_member(obj, "artifacts"); p && p->type == JsonType::Object) {
        // engine -> { precision -> artifact }
        for (const auto& engine_kv : p->object) {
            const std::string& engine = engine_kv.first;
            const JsonValue& precisions = engine_kv.second;
            if (precisions.type != JsonType::Object) continue;
            for (const auto& prec_kv : precisions.object) {
                const std::string& precision = prec_kv.first;
                const JsonValue& artifact = prec_kv.second;
                if (artifact.type != JsonType::Object) continue;
                out.artifacts[engine][precision] = parse_artifact(artifact);
            }
        }
    }
    return out;
}

}  // namespace

ReleaseManifest parse_release_manifest(std::string_view json_text) {
    ReleaseManifest out;

    JsonValue root;
    JsonParser parser(json_text);
    if (!parser.parse(root) || root.type != JsonType::Object) {
        return out;  // schema_version defaults to 0
    }

    if (const auto n = as_number(find_member(root, "schema_version")); n) {
        out.schema_version = static_cast<int>(*n);
    }
    if (const auto n = as_number(find_member(root, "model_count")); n) {
        out.model_count = static_cast<int>(*n);
    }

    if (const JsonValue* p = find_member(root, "modellist"); p && p->type == JsonType::Array) {
        out.modellist.reserve(p->array.size());
        for (const auto& v : p->array) {
            if (v.type == JsonType::String) {
                out.modellist.push_back(v.string);
            }
        }
    }

    // Parse structured `models` array (schema_version 2).
    if (const JsonValue* p = find_member(root, "models"); p && p->type == JsonType::Array) {
        out.models.reserve(p->array.size());
        for (const auto& v : p->array) {
            if (v.type == JsonType::Object) {
                out.models.push_back(parse_model(v));
            }
        }
    }

    return out;
}

const ArtifactInfo* find_artifact(const ModelInfo& model,
                                  std::string_view engine,
                                  std::string_view precision) {
    auto engine_it = model.artifacts.find(std::string(engine));
    if (engine_it == model.artifacts.end()) return nullptr;
    const auto& prec_map = engine_it->second;
    auto prec_it = prec_map.find(std::string(precision));
    if (prec_it == prec_map.end()) return nullptr;
    return &prec_it->second;
}

std::vector<const ModelInfo*> list_models(const ReleaseManifest& manifest) {
    std::vector<const ModelInfo*> out;
    out.reserve(manifest.models.size());
    for (const auto& m : manifest.models) {
        out.push_back(&m);
    }
    return out;
}

std::string infer_asset_stem_from_dir(const std::string& model_dir) {
    // Known v2 asset stems.  Add new entries here as new backbones ship.
    static const std::vector<std::string> kKnownStems = {
        "mobilenet_v3_small.trislot_decoder.v2_0",
        "mobilenetv4_conv_small.trislot_decoder.v2_0",
    };

    namespace fs = std::filesystem;
    std::error_code ec;
    if (model_dir.empty() || !fs::is_directory(model_dir, ec)) {
        return {};
    }

    for (const auto& stem : kKnownStems) {
        const std::string base = (fs::path(model_dir) / stem).string();
        const std::vector<std::string> precisions = {"fp16", "fp32"};
        for (const auto& prec : precisions) {
            const std::string param = base + "." + prec + ".param";
            const std::string bin   = base + "." + prec + ".bin";
            if (fs::exists(param, ec) && fs::exists(bin, ec)) {
                return stem;
            }
        }
    }
    return {};
}

ReleaseManifestSummary parse_release_manifest_summary(
    std::string_view tag, std::string_view json_text) {
    ReleaseManifestSummary summary;
    summary.tag = std::string(tag);
    summary.model_count = 0;

    // Walk the JSON looking for "model_count": <int>
    auto findCount = [](std::string_view json) -> int {
        const std::string needle = "\"model_count\"";
        auto pos = json.find(needle);
        if (pos == std::string_view::npos) return 0;
        pos = json.find(':', pos + needle.size());
        if (pos == std::string_view::npos) return 0;
        ++pos;
        while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])))
            ++pos;
        if (pos >= json.size()) return 0;
        int count = 0;
        while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos]))) {
            count = count * 10 + (json[pos] - '0');
            ++pos;
        }
        return count;
    };

    summary.model_count = findCount(json_text);
    return summary;
}

}  // namespace shmtu::cas::ocr
