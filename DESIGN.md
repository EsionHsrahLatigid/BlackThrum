# BlackThrum Design

BlackThrum is a focused three-carrier thrum/drone instrument. MIDI notes retrigger a monophonic envelope and root the carrier frequencies; deterministic seeded drift, formant-skewed nonlinear coupling, unstable stereo width, grind, and bounded output define the sound.

## Product Shape

- Plugin formats: Standalone and VST3 on macOS and Windows; AUv2 additionally on macOS.
- Synth behavior: accepts MIDI, produces stereo audio, one active voice.
- Stable identity: `jp.ehl.blackthrum`, `jp.ehl.blackthrum`, AU subtype `BlTh`.
- Host parameters: `Pitch offset`, `Thrum`, `Drift`, `Formant`, `Width`, `Grind`, `Output`.
- Runtime-only UI state: `Trigger`.
- State format: parameter ID/value pairs with a `BLT1` magic header and version `1`.

## DSP Contract

- The engine is allocation-free during audio rendering.
- All public parameters are sanitized before use.
- Output samples remain finite and hard bounded by the final safety stage.
- Rendering is deterministic for identical seed, note, velocity, parameter, and sample-rate inputs.
- Before a note trigger and after release decay, the engine renders silence.
- Default triggered output has measurable RMS, note changes move rooted carrier rate, width decorrelates stereo channels, and formant/grind/thrum changes produce measurable response.
- The audio callback uses pre-owned engine, parameter handles, atomics, stack values, and output buffers only; it performs no logging, UI work, file I/O, locks, or heap allocation.

## UI Contract

The current editor is a YUP parameter grid with direct host-bound controls plus a small Standalone-oriented performance strip.

- The momentary `Trigger` button and Space-key gate are UI commands only; they publish a combined desired gate plus monotonic edge counter through processor-owned atomics. The audio thread consumes pending edges before rendering, including rapid press/release pairs that happen between callbacks.
- Mouse and Space holds are tracked separately by the editor. Closing the editor or losing focus fail-safe releases the UI gate without interfering with external MIDI.
- External MIDI remains supported and is not converted through the UI trigger path.
- The output activity meter is display-only. The audio thread stores a per-block peak as a scaled atomic integer, and the UI timer polls/decays it for drawing.
- Visual direction is a blackened industrial string lattice over the native YUP parameter grid: dense, readable, asset-free, and dependency-free.

## OMX Design Checklist

- [x] Product identity is unique: app ID `jp.ehl.blackthrum`, plugin ID `jp.ehl.blackthrum`, AU subtype `BlTh`.
- [x] Version is `0.1.0`.
- [x] State magic/version is product-specific: `BLT1` / `1`.
- [x] Host parameter list is stable and excludes runtime `Trigger`.
- [x] Standalone Trigger button and Space gate share the same atomic handoff.
- [x] External MIDI has priority over UI trigger and hands back to held UI gate after MIDI note-off.
- [x] Focus loss/editor close fail-safe releases the UI gate.
- [x] Output meter is display-only and fed from an atomic peak value.
- [x] DSP tests cover finite samples, `<= 0.98` peak, default RMS floor, pre-trigger silence, deterministic max diff, release tail, denormal flushing, note-rooted carrier identity, formant/grind/thrum response, drift/width stereo identity.
- [x] Plugin bridge tests cover parameter surface, state round-trip/rejection, trigger runtime ownership, MIDI ownership/handoff, meter, and release.
- [x] CI/release workflows use unique names, targets, artifacts, full action SHA pins, 14-day checksummed artifacts, and exact-SHA release promotion.

## CI and Release Contract

- `CI Summary` is the stable required check. A Linux classifier always runs; it skips macOS and Windows only for the documented docs-only allowlist and otherwise chooses the conservative heavy path.
- macOS and Windows each build, test, package, and upload one `latest` ZIP plus a strict single-line `SHA256SUMS.txt`. Actions artifacts expire after 14 days.
- Tag pushes never compile. The Release workflow resolves the tag to its commit, requires the tag and CMake project versions to match, locates the unique successful canonical `CI` push run on `main` with the same `head_sha`, requires exactly the two named platform artifacts, verifies SHA-256 and ZIP integrity, sanitizes the draft asset list, and only then publishes exactly the two versioned release assets.
- Release provenance failures are terminal. Missing, expired, duplicate, or mismatched artifacts must not trigger an automatic rebuild or partial release.
- GitHub actions are pinned to immutable commit SHAs. The release runner requires GitHub CLI 2.x or newer and the minimal `actions: read` / `contents: write` permissions.
