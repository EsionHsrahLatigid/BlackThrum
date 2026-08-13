# BlackThrum

BlackThrum is a YUP-based audio plugin and standalone synth that renders a note-rooted, three-carrier detuned thrum/drone voice. It builds from this project directory, using the adjacent `../yup` checkout when present.

## Identity

- App ID: `jp.ehl.blackthrum`
- Plugin ID: `jp.ehl.blackthrum`
- AU subtype: `BlTh`
- Plugin vendor: `ehl_`
- AU manufacturer: `EHL1`
- Type: stereo-output synth accepting MIDI input
- macOS formats: Standalone, VST3, AUv2
- Windows formats: Standalone, VST3

## Standalone Controls

The built-in editor includes a momentary `Trigger` control for the Standalone app. Press and hold the button, press and hold Space while the editor has keyboard focus, or hold both; the editor publishes the combined gate state to the processor. External MIDI input is still accepted and uses the same monophonic engine path.

The editor also shows a lightweight output activity meter. Trigger edges and meter values move through processor-owned atomics, so realtime rendering stays lock-free and allocation-free.

## Parameters

`Pitch offset`, `Thrum`, `Drift`, `Formant`, `Width`, `Grind`, and `Output` are the stable host parameters. `Trigger` is intentionally runtime UI state and is not saved as host/plugin state.

## Build

Clone with `--recurse-submodules`, or initialize the shared [yup-ehl-design-module](https://github.com/EsionHsrahLatigid/yup-ehl-design-module) before configuring:

```sh
git submodule update --init
```

```sh
cmake --preset engine-debug
cmake --build --preset engine-debug
ctest --preset engine-debug
```

```sh
cmake --preset plugin-release
cmake --build --preset plugin-release
ctest --preset plugin-release
```

Release bundles are staged in a stable, human-facing tree. `build/` is only CMake's working area:

- `blackthrum_release_bundles`
- `blackthrum_standalone_plugin`
- `blackthrum_vst3_plugin`
- `blackthrum_au_plugin` on Apple platforms

On macOS, the local bundle paths are:

- `artifacts/plugin-release/macos-arm64/standalone/blackthrum_standalone_plugin.app`
- `artifacts/plugin-release/macos-arm64/vst3/blackthrum_vst3_plugin.vst3`
- `artifacts/plugin-release/macos-arm64/au/blackthrum_au_plugin.component`

Windows uses the same layout under `artifacts/plugin-release/windows-x64/` with `standalone/` and `vst3/` directories. On local macOS non-CI `plugin-release` builds, the staged VST3 and AU bundles are also physically copied to `~/Library/Audio/Plug-Ins/VST3` and `~/Library/Audio/Plug-Ins/Components`; Standalone stays in `artifacts/`. Configure with `-DEHL_COPY_PLUGIN_AFTER_BUILD=OFF` to disable the local plugin copy. `ARTIFACTS.txt` records the staged product, profile, platform, formats, and local plugin copy paths when enabled.

## CI

`.github/workflows/ci.yml` is the required CI entrypoint for pushes to `main`, pull requests, and manual runs. A lightweight Linux classifier always runs. Changes limited to `README.md`, `DESIGN.md`, `LICENSE`, `docs/**`, or `.github/ISSUE_TEMPLATE/**` skip the heavy jobs; every other change runs Debug tests and Release bundle builds on macOS arm64 and Windows x64. Manual dispatches default to forcing both heavy jobs.

Successful heavy runs upload two immutable, 14-day artifacts:

- `BlackThrum-latest-macos-arm64`, containing `BlackThrum-latest-macos-arm64.zip` and `SHA256SUMS.txt`
- `BlackThrum-latest-windows-x64`, containing `BlackThrum-latest-windows-x64.zip` and `SHA256SUMS.txt`

`.github/workflows/release.yml` is the only `v*` tag workflow. It performs no compilation. The Ubuntu release job resolves lightweight or annotated tags to a commit, requires the tag version to match the CMake project version, requires one successful `CI` push run on `main` for that exact SHA, downloads exactly the two expected artifacts, verifies their SHA-256 manifests and ZIP integrity, then publishes versioned assets such as `BlackThrum-0.1.0-macos-arm64.zip` and `BlackThrum-0.1.0-windows-x64.zip`. Publication uses a draft release whose asset list is sanitized and rechecked to contain exactly those two ZIPs. A missing, expired, ambiguous, or mismatched provenance chain fails closed.

Release operator sequence: merge or push the version commit to `main`, wait for both platform jobs and `CI Summary` to pass, then create and push the version tag. GitHub CLI 2.x or newer is required by the release runner. Never move or reuse a published tag; correct the source and use the next patch version instead.

## Layout

- `include/blackthrum/` contains the realtime-safe DSP engine API and local DSP primitives.
- `source/` contains the engine implementation and YUP plugin/editor/state wrapper.
- `tests/` contains deterministic engine regression tests and a plugin-wrapper bridge test for the built-in synthetic trigger.
- `cmake/` contains the project-local macOS icon conversion workaround used by the standalone target.
