#pragma once

#include <shmtu/cas_ocr/types.h>

#include <string>
#include <string_view>
#include <vector>

namespace shmtu::cas::ocr {

// --------------------------------------------------------------------------
// Release-manifest JSON parsing (schema_version 2, with v1 fallback).
//
// The manifest is fetched from
//   {base_url}/{tag}/model-assets.json
// where `base_url` is one of the GitHub/Gitee release roots.  It catalogues
// one or more logical models (each potentially with multiple engines /
// precisions) along with metrics and SHA256 digests for every artifact file.
//
// We intentionally avoid pulling in nlohmann/json to keep the lib's
// dependency surface identical to what it was.  The parser below is a
// narrow, schema-aware walker that tolerates the legacy flat `artifacts`
// array so older releases still produce a populated manifest.
// --------------------------------------------------------------------------

// Lightweight summary returned by `parse_release_manifest_summary`.  Only
// extracts `model_count` from a manifest JSON blob without parsing the
// full model tree.  Used when the caller only needs to know how many
// models a release contains (e.g. a release-tag list view).
struct ReleaseManifestSummary {
    std::string tag;       // the release tag, e.g. "v2.0.1"
    int model_count = 0;   // number of `ModelInfo` entries, 0 on parse error
};

// Parse a release manifest from its raw JSON text.
// Returns a manifest whose `models` array is populated for schema_version
// >= 2 (preferred path); for older schemas the same data lands in
// `flat_artifacts` and `models` is left empty.  Errors are not thrown —
// an unparseable / missing input simply yields a manifest with
// schema_version == 0 and empty model lists.  Callers should check
// `manifest.schema_version` and `manifest.model_count` to detect failure.
ReleaseManifest parse_release_manifest(std::string_view json_text);

// Look up one artifact inside a `ModelInfo` by (engine, precision).
// Returns nullptr if either key is absent.  The pointer is valid for the
// lifetime of `model`.
const ArtifactInfo* find_artifact(const ModelInfo& model,
                                  std::string_view engine,
                                  std::string_view precision);

// Return pointers to every `ModelInfo` contained in the manifest.  The
// pointers are valid for the lifetime of `manifest`.  Order matches
// `manifest.models` (which in turn follows `manifest.modellist` when
// present).
std::vector<const ModelInfo*> list_models(const ReleaseManifest& manifest);

// Resolve the asset_stem (the manifest entry's `asset_stem`) from a
// legacy v2 directory layout.  Recognises the canonical
// `mobilenet_v3_small.trislot_decoder.v2_0` stem.  Returns an empty
// string if no candidate is present in `model_dir`.
std::string infer_asset_stem_from_dir(const std::string& model_dir);

// Parse only `model_count` from a manifest JSON blob.  This is a
// low-overhead alternative to `parse_release_manifest` when the
// caller does not need the per-model details.  The returned summary
// carries the tag that was passed in (for bookkeeping) and the
// model count extracted from the JSON.  On parse failure,
// `model_count` is 0.
ReleaseManifestSummary parse_release_manifest_summary(
    std::string_view tag, std::string_view json_text);

} // namespace shmtu::cas::ocr