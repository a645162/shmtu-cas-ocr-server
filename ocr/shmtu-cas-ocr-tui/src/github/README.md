## src/github/

GitHub/Gitee API integration and SemVer 2.0 parsing.

- `github_client.{h,cpp}` — anonymous API client: list releases, download
  `model-assets.json`, construct asset URLs.
- `semver.{h,cpp}` — lightweight SemVer 2.0 parser, comparator and sorter.
