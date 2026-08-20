# Changelog

All notable changes to Audio Reactive Toolkit are documented here.

## 0.4.0 - 2026-08-20

### Added

- Nine native, audio-reactive OBS video filters: **ART Mosaic**, **ART RGB Split**, **ART Shake**, **ART Zoom/Punch**,
  **ART Pixelate**, **ART Wave**, **ART Glow/Pulse**, **ART Glitch**, and **ART 3D Tile**.
- **ART Custom Shader** filter for writing your own audio-reactive HLSL pixel shader, either as a short wrapper
  function or a full raw `.effect` file, with a live-swappable four-slot generic modulation contract
  (`mod_1`..`mod_4`) and an `effect_mix` crossfade against the untouched source. Ships with a bundled
  "Audio Kaleidoscope Pulse" example effect.
- Shared `ArtModulation` binding layer (source, `[min, max]` range, return-speed smoothing, and effect-strength
  scaling) that every native filter's parameters are built on, so Source/Min/Max/Return-speed/Strength controls
  behave identically across all ten filters.
- Optional **Sync to BPM** phase-locked oscillator for the Wave, Shake, and Glitch filters, free-running at a rate
  derived from the live tempo estimate and gently correcting phase drift on each detected beat.
- **Photo Mosaic** and **Growing Crystal** bundled browser visualizers.
- Dedicated [ART Custom Shader filter documentation](docs/custom-shader-filter.md).
- Shared Windows VS Code development tasks and scripts (build, install-plugin, formatting checks).

### Changed

- Expanded the README's project description to reflect the toolkit's broader scope beyond visualizers: audio as a
  general real-time control signal for reactive visuals, effects, and animations.
- Optimized CI workflow usage and pinned local gersemi formatting checks to the CI version (0.21.0).

### Notes

- The nine built-in filters and ART Custom Shader are currently localized in English, German, and French only; the
  remaining eight bundled languages still show English strings for these filters until translated.

## 0.3.0 - 2026-08-19

### Added

- Experimental native OBS Browser Source transport through the supported `obs-websocket` and `obs-browser`
  `emit_event` vendor API.
- Combined `artFrame` event with the WebSocket schema's audio, frequency-band, beat, transient, tempo, timing, and
  32-band spectrum data.
- Configurable **Both**, **Native only**, and **WebSocket only** transport modes for compatibility and isolated
  performance testing.
- Transport-independent `ART.on("frame")`, `ART.on("beat")`, and `ART.on("bpm")` JavaScript client API with
  WebSocket reconnection and native spectrum normalization.
- Native-event diagnostic page and a shared native-versus-WebSocket benchmark page with latency, jitter, message-rate,
  sequence-gap, payload-size, and JSON result reporting.
- Native transport architecture, limitations, test procedure, and preliminary Windows benchmark documentation.
- Dedicated `docs/` structure with a documentation index.

### Changed

- Centralized the analyzer wire model, protocol field names, and native/WebSocket serializers in a shared ART protocol
  module to prevent schema drift between transports.
- Added `sentAt` and `sequence` fields to WebSocket schema version 1 for same-host latency and dropped-message
  measurements.
- Aligned `artFrame` field names and nesting with the WebSocket payload. Native `fft32` entries use `{ "v": number }`
  objects because the public OBS data API does not support primitive numeric arrays.
- WebSocket JSON serialization now remains idle until at least one client completes its handshake.
- Connected WebSocket clients are now woken immediately when a new frame is published instead of relying on a 10 ms
  sender polling interval.
- Migrated bundled visualizers to the shared ART JavaScript client and added visible transport labels.
- Made **WebSocket only** the default transport mode. Native remains available as an explicit low-latency option for
  controlled OBS Browser Source scenes.

### Performance

- In a concurrent paired Windows 60 Hz test, native `artFrame` averaged 0.22 ms delivery latency and 0.24 ms arrival
  jitter, compared with 0.65 ms and 0.22 ms for wake-driven WebSocket delivery.
- Both transports sustained 60 Hz without dropped messages. Separate isolated mode runs both remained within the
  observed 0.5-0.9% OBS CPU range on the test system.

### Notes

- Native events are broadcast to every loaded Browser Source because the public `obs-browser` vendor API does not
  support per-source targeting.
- Native transport remains experimental pending longer runs, macOS/Linux validation, and genuine 64-/128-bin analyzer
  benchmarks.

## 0.2.0 - 2026-08-18

### Added

- Native OBS audio capture with selectable and persisted audio sources.
- RMS, peak, bass, mid, high, and logarithmically distributed 32-band FFT analysis.
- Adaptive beat and transient detection plus BPM estimation.
- Local WebSocket output for OBS Browser Sources.
- Diagnostic, Three.js orb, neon tunnel, and spectrum landscape visualizers.
- Configurable linear, perceptual, and decibel spectrum scaling.
- Native BPM text-source output.
- Localized plugin settings for eleven languages.
- Windows x64, macOS Universal, and Ubuntu x86_64 builds.
- Portable ZIP and Inno Setup installer for Windows.
- Native macOS and Debian/Ubuntu packages.
- GPL-3.0-or-later licensing with bundled third-party notices.

### Notes

- This is the first public release. Settings and the WebSocket message format may still change before version 1.0.
