# Audio Reactive Toolkit documentation

## Users

- [User guide](user-guide.md) – installation, setup, visualizers, and troubleshooting

## Integrators

- [ART JavaScript client](javascript-client.md) - shared `frame`, `beat`, and `bpm` API for WebSocket and Native
- [WebSocket protocol](websocket.md) – connection details, schema, event semantics, and client example

## Native Browser event experiment

- [Native Browser events](native-browser-events.md) – architecture, dependencies, events, test procedure, and
  limitations
- [Transport comparison](transport-comparison.md) – Windows benchmark setup, results, operational differences, and
  current recommendation

WebSocket is ART's default transport and remains the supported interface for applications outside OBS. The native
Browser event transport is an optional experimental low-latency path for OBS Browser Sources.
