# WebSocket Protocol Reference

Audio Reactive Toolkit (ART) publishes real-time audio analysis data for custom OBS Browser Sources and other local
visuals. This document describes message format version `1`.

The protocol is part of early access and may evolve before ART 1.0. Clients should check the `version` field and
ignore unknown fields so that compatible additions do not break them.

## Connection

- URL: `ws://127.0.0.1:8765`
- Transport: standard WebSocket
- Messages: UTF-8 JSON text frames
- Direction: server to client only
- Default rate: 30 messages per second

The port and publication rate are configurable in the ART settings. The server listens only on IPv4 loopback and is
not accessible from other computers. Multiple local clients can connect at the same time.

ART sends the most recent analysis snapshot whenever a new snapshot is available. Clients should reconnect after OBS
or the plugin restarts.

## Message structure

```json
{
  "version": 1,
  "timestamp": 1264875234,
  "active": true,
  "rms": 0.084271,
  "peak": 0.312408,
  "bands": {
    "bass": 0.102553,
    "mid": 0.061284,
    "high": 0.028419
  },
  "beat": {
    "detected": true,
    "strength": 0.734215
  },
  "transient": {
    "detected": false,
    "strength": 0.421806
  },
  "tempo": {
    "bpm": 124.82,
    "confidence": 0.781245,
    "locked": true
  },
  "fft32": [
    0.016243, 0.021458, 0.028912, 0.036114,
    0.044583, 0.052109, 0.061827, 0.073415,
    0.081942, 0.077314, 0.069821, 0.061273,
    0.054118, 0.048531, 0.043216, 0.038905,
    0.034721, 0.031485, 0.028214, 0.025691,
    0.022843, 0.020415, 0.018107, 0.015932,
    0.013874, 0.012051, 0.010423, 0.008912,
    0.007584, 0.006391, 0.005327, 0.004418
  ]
}
```

## Fields

| Field | Type | Description |
| --- | --- | --- |
| `version` | integer | Message schema version. Version documented here is `1`. |
| `timestamp` | integer | Monotonic time in milliseconds. Use it for relative timing, not as a wall-clock or Unix timestamp. |
| `active` | boolean | `true` after audio data has been received from the selected OBS source. |
| `rms` | number | Root-mean-square level of the current audio buffer. |
| `peak` | number | Highest absolute sample level in the current audio buffer. |
| `bands.bass` | number | Low-frequency energy derived from frequencies below approximately 250 Hz. |
| `bands.mid` | number | Mid-frequency energy derived from approximately 250 Hz to 4 kHz. |
| `bands.high` | number | High-frequency energy derived from frequencies above approximately 4 kHz. |
| `beat.detected` | boolean | `true` once when one or more beat events occurred since the previous published message. |
| `beat.strength` | number | Normalized strength of the latest beat event, clamped to `0.0`–`1.0`. |
| `transient.detected` | boolean | `true` once when one or more transient events occurred since the previous published message. |
| `transient.strength` | number | Normalized strength of the latest transient event, clamped to `0.0`–`1.0`. |
| `tempo.bpm` | number | Current tempo estimate. `0.0` means that no estimate is available. |
| `tempo.confidence` | number | Confidence in the tempo estimate, normally between `0.0` and `1.0`. |
| `tempo.locked` | boolean | Whether the estimator considers the tempo stable. |
| `fft32` | number[32] | Smoothed linear spectrum magnitudes in 32 logarithmically spaced bands from about 30 Hz to 18 kHz. |

Audio level and FFT values are linear floating-point magnitudes. They are not percentages or decibels and should not
be assumed to stop at `1.0`. Apply a display transform and clamp only at the final rendering stage.

## Event semantics

Use `beat.detected` and `transient.detected` as edge-triggered events. Do not trigger an animation merely because the
corresponding `strength` is non-zero: the strength field retains the latest event's value between events.

If several events occur between two published messages, `detected` is still a single boolean notification. The data is
designed to drive visuals rather than provide a lossless event log.

Tempo is an estimate rather than an event. A client may display `tempo.bpm` while the estimator is learning, but
`tempo.locked` and `tempo.confidence` should be used when a stable value is required.

## Spectrum bands

`fft32[0]` is the lowest-frequency band and `fft32[31]` is the highest. The bands are logarithmically spaced, which
gives lower frequencies more visual resolution than linearly spaced FFT bins.

The bundled visualizers demonstrate three useful display mappings:

```js
function scaleSpectrum(value, mode = "perceptual", floorDb = -60) {
  const linear = Math.max(0, Number(value));
  if (mode === "perceptual") return Math.pow(Math.min(1, linear), 0.55);
  if (mode === "db") {
    if (linear <= 0) return 0;
    const db = 20 * Math.log10(linear);
    return Math.min(1, Math.max(0, (db - floorDb) / -floorDb));
  }
  return Math.min(1, linear);
}
```

## Minimal browser client

```js
let socket;
let reconnectTimer;

function connect() {
  clearTimeout(reconnectTimer);
  socket = new WebSocket("ws://127.0.0.1:8765");

  socket.addEventListener("message", event => {
    const data = JSON.parse(event.data);
    if (data.version !== 1) return;

    renderLevels(data.rms, data.peak, data.bands);
    renderSpectrum(data.fft32);

    if (data.beat.detected) triggerBeat(data.beat.strength);
    if (data.transient.detected) triggerTransient(data.transient.strength);
  });

  socket.addEventListener("close", () => {
    reconnectTimer = setTimeout(connect, 1500);
  });

  socket.addEventListener("error", () => socket.close());
}

connect();
```

The `renderLevels`, `renderSpectrum`, `triggerBeat`, and `triggerTransient` functions are application-specific. See the
files under `data/browser` for complete working clients.

## Client recommendations

- Validate `version` before processing a message.
- Ignore fields that the client does not understand.
- Treat missing or invalid fields as unavailable rather than terminating the visualizer.
- Animate between messages instead of tying rendering directly to the WebSocket rate.
- Reconnect with a short delay after the connection closes.
- Keep the WebSocket connection local; ART does not provide authentication or TLS.
