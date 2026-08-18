# Audio Reactive Toolkit

[![Mac/Linux/Windows CI](https://github.com/Funkrusha/audio-reactive-toolkit/actions/workflows/push.yaml/badge.svg?branch=main&event=push)](https://github.com/Funkrusha/audio-reactive-toolkit/actions/workflows/push.yaml?query=branch%3Amain)

Audio Reactive Toolkit (ART) is a native OBS Studio plugin that turns audio into real-time analysis data for browser-based visuals. It captures a selected OBS audio source, detects levels, frequency bands, beats, transients, and BPM, then publishes the results over a local WebSocket connection.

ART supports Windows, macOS, and Linux.

## Features

- Selectable OBS audio source with persistent settings
- RMS, peak, bass, mid, and high levels
- 32-band FFT spectrum
- Adaptive beat and transient detection
- BPM estimation with confidence and lock state
- Local WebSocket output at `ws://127.0.0.1:8765`
- Included transparent, tunnel, landscape, and diagnostic visualizers
- Optional native OBS text-source output for BPM

## Installation

Download the package for your operating system from the [latest release](https://github.com/Funkrusha/audio-reactive-toolkit/releases/latest):

- Windows: run the `windows-x64-setup.exe` installer. A portable ZIP is also available.
- macOS: open the Universal `.pkg` package.
- Ubuntu: install the x86_64 `.deb` package.

Restart OBS Studio after installation. Detailed setup instructions and troubleshooting are available in the [user guide](HELP.md).

## Using a visualizer

Add a **Browser** source in OBS, enable **Local file**, and select one of the installed HTML files:

```text
C:\Program Files\obs-studio\data\obs-plugins\audio-reactive-toolkit\browser\visualizer-three.html
C:\Program Files\obs-studio\data\obs-plugins\audio-reactive-toolkit\browser\visualizer-tunnel.html
C:\Program Files\obs-studio\data\obs-plugins\audio-reactive-toolkit\browser\visualizer-landscape.html
C:\Program Files\obs-studio\data\obs-plugins\audio-reactive-toolkit\browser\index.html
```

The first three files provide transparent visualizations. `index.html` is a diagnostic view of all analysis values. A size of `1280 x 720` works well for the visualizers; use `1280 x 1280` for the diagnostic view.

The pages connect to the plugin automatically and reconnect after OBS or the plugin restarts.

### Display scaling

The visualizers support optional URL parameters for spectrum scaling:

```text
?scale=linear
?scale=perceptual
?scale=db&floor=-70
```

- `linear` displays the unmodified FFT values.
- `perceptual` provides moderate compression and works well for music visuals.
- `db` maps the spectrum logarithmically from a configurable floor to `0 dB`.

Parameters can be combined with a custom WebSocket port, for example `visualizer-landscape.html?port=8765&scale=perceptual`.

## Native BPM text source

Create an OBS **Text (GDI+)** source and select it under **BPM-Textquelle** in the plugin settings. ART updates only the text; its font, color, outline, position, and other styling remain controlled by OBS.

**BPM-Textformat** accepts the `{bpm}` placeholder, for example `Tempo: {bpm}`. **BPM-Rundung** controls the number of decimal places. Selecting **Keine** disables native BPM text output.

## Building from source

Building ART requires Git, CMake 3.30.5 or newer, and a supported compiler toolchain:

- Visual Studio 2022 on Windows
- Xcode on macOS
- GCC or Clang on Linux

On Windows, run the following commands from a Visual Studio Developer PowerShell:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64
```

List the presets available on the current platform with:

```powershell
cmake --list-presets
cmake --build --list-presets
```

The project uses OBS Studio 31.1.1 as its current development baseline. GitHub Actions builds and packages Windows x64, macOS Universal, and Ubuntu x86_64 releases.

## License

Audio Reactive Toolkit is free and open-source software licensed under the [GNU General Public License v3.0 or later](LICENSE) (`GPL-3.0-or-later`). You may use, study, modify, and redistribute it under those terms, including for commercial purposes. Distributed modified versions must remain under the GPL and provide the corresponding source code.

Third-party components and their notices are documented in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Acknowledgements

This project was initially based on the official [OBS Plugin Template](https://github.com/obsproject/obs-plugintemplate). Thanks to the OBS Project contributors for providing and maintaining the template and OBS Studio plugin APIs.
