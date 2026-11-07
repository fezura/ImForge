# Releasing ImForge

Releases are built by `.github/workflows/release.yml` from a `v<major>.<minor>.<patch>` tag.
The workflow calls `tools/package_release.py`, so you can run the same steps locally.

## Cutting a release

1. Bump the version in both places (CI fails if they differ):
   - `CMakeLists.txt`: `project(ImForge VERSION X.Y.Z LANGUAGES C CXX)`
   - `include/imforge/imforge_version.h`: `IMFORGE_VERSION "X.Y.Z"`, `IMFORGE_VERSION_NUM`
     (`major*10000 + minor*100 + patch`), `IMFORGE_VERSION_MAJOR/MINOR/PATCH`
2. In `CHANGELOG.md`, rename the `## X.Y.Z (unreleased)` heading to `## X.Y.Z`. The release
   notes are the text between that heading and the next `## `. Add a new `(unreleased)` section
   on top if work continues.
3. Do a dry run on at least one platform (see below) and look over the archive listing once.
4. Commit and tag:
   ```
   git commit -am "chore: release vX.Y.Z"
   git tag -a vX.Y.Z -m "ImForge X.Y.Z"
   git push origin main --follow-tags
   ```
   Pre-releases get a suffix (`v0.2.0-rc1`). The workflow marks them as GitHub pre-releases and
   only compares the part before the `-` with the project version.
5. Wait for the `Release` workflow (Actions tab). It creates the GitHub Release
   `ImForge vX.Y.Z` with:
   - `ImForge-X.Y.Z-windows-x64.zip`
   - `ImForge-X.Y.Z-linux-x64.zip` and `ImForge-X.Y.Z-linux-x64.tar.gz`
   - `imforge_client_single.h`, `imb_runtime_single.h`, `imb_essentials_single.h`
   - `SHA256SUMS` for all of the above (`sha256sum -c SHA256SUMS` next to the downloads)

   Download one archive, check its hash, run `bin/imforge_builder` and open
   `share/imforge/examples/projects/CheatMenu`. Re-running the workflow on the same tag replaces
   the assets (`gh release upload --clobber`) and keeps the release text.

### Workflow jobs

| Job | Runner | What it does |
|-----|--------|--------------|
| `check-version` | ubuntu | `CMakeLists.txt` == `imforge_version.h` == tag |
| `package` (matrix) | windows-latest (`windows-msvc` preset), ubuntu-latest (`wsl-gcc-release` preset, apt deps as in `ci.yml`) | `tools/package_release.py --preset <p> --config Release --out dist`: configure, build everything (builder, `imforge-reflect`, `imforge_amalgamate`), ctest, `cmake --install`, deterministic archive, `SHA256SUMS` |
| `headers-only` | ubuntu | `tools/amalgamate.py --check` against Dear ImGui v1.85 and v1.91.9b, uploads `dist/*.h` |
| `release` | ubuntu, tags only | verifies each job's checksums, collects archives and single headers, regenerates `SHA256SUMS`, creates or updates the GitHub Release with the CHANGELOG section as notes |

`workflow_dispatch` runs everything except `release` and leaves the packages as workflow
artifacts (`package-Windows`, `package-Linux`, `headers-only`).

## Building a package locally

```
# Linux / WSL (full package: needs SDL3 build deps, see docs/USER_GUIDE.md)
python3 tools/package_release.py --preset wsl-gcc-release --config Release --out dist

# Windows (Visual Studio 17 2022, x64)
python tools/package_release.py --preset windows-msvc --config Release --out dist

# Headers + imforge-reflect only, reuse an existing build tree, no tests
cmake -S . -B build/pkg -G Ninja -DIMFORGE_BUILD_BUILDER=OFF -DIMFORGE_BUILD_TESTS=OFF -DIMFORGE_BUILD_EXAMPLES=OFF
python3 tools/package_release.py --build-dir build/pkg --skip-tests --out dist
```

Other flags: `--cmake-arg=-DNAME=VALUE` (extra cache entries, forces a reconfigure), `--target`
(build a subset), `--skip-build`, `--formats zip,tgz`, `--keep-stage` (leave the installed tree
in `dist/_stage/<package>`) and `--name` (override the archive name). Archives are
reproducible: entries are sorted, timestamps fixed (`SOURCE_DATE_EPOCH` or 1980-01-01), modes
normalized to 0644/0755 and no owner information stored, so two runs over the same build tree
give byte-identical files.

Plain CMake works too. `cmake --install <build> --prefix <dir>` produces the tree below, and
`cpack -G ZIP` / `cpack -G TGZ` in the build directory produces `ImForge-<version>-<os>-<arch>.*`,
though with timestamps, so use the script for anything you publish. `-DIMFORGE_PACKAGING=OFF`
removes the install rules, the `imforge_amalgamate` target and the CPack configuration.

## What's in the package

```
ImForge-X.Y.Z-<os>-<arch>/
  README.md  LICENSE  CHANGELOG.md  THIRD_PARTY.txt (font and library licenses, builder packages)
  bin/
    imforge_builder(.exe)        the editor (only when built with IMFORGE_BUILD_BUILDER=ON)
    fonts/                       Inter, JetBrains Mono, Font Awesome + licenses, loaded by the builder
    imforge-reflect(.exe)        reflection CLI (+ libclang.dll on Windows when built with libclang)
  include/imforge/**/*.h         every header: imforge_client.h, imb_runtime.h, fx/, ...
  packs/essentials/**            imb::ess widget pack (headers, .imw.json manifests, essentials.imblib)
  share/imforge/
    dist/*.h                     amalgamated single headers (same files as the standalone assets)
    examples/projects/**         CheatMenu and ToolDock sample projects
    templates/**                 wizard templates + templates.json
    schemas/**                   JSON Schemas for project, panel, node, widget, theme, ... files
```

`<os>` is `windows`, `linux` or `macos`; `<arch>` is `x64`, `arm64` or `x86`. The version comes
from `project()` in `CMakeLists.txt`.

## How people use a release

The builder zip is for whoever designs the UI: unpack it anywhere and run
`bin/imforge_builder`. The builder finds its fonts in `bin/fonts`, the templates, schemas and
example projects under `share/imforge`, and `imforge-reflect` next to itself. Add `include/`
(and `packs/essentials` if the project uses `imb::ess` widgets) to the include path of the app
you want to edit live or that compiles the exported code.

App developers can grab just the single headers. `imforge_client_single.h` is live attach:
define `IMFORGE_LIVE` in development builds and leave it out for release, where everything
compiles away. `imb_runtime_single.h` has the animations and DrawList effects generated code
uses, and `imb_essentials_single.h` the essentials pack plus the runtime. Each one only needs
Dear ImGui, works with 1.85 through the current release, and is generated from the sources by
`tools/amalgamate.py`, so it matches the headers in the archive exactly.

If you vendor ImForge in CMake, `add_subdirectory` the source tree with
`-DIMFORGE_BUILD_BUILDER=OFF -DIMFORGE_BUILD_TESTS=OFF` and link `imforge::headers`.
