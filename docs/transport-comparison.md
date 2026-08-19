# Native Browser Events vs. WebSocket

This document summarizes the preliminary comparison between ART's native OBS Browser Source transport and its local
WebSocket transport. The native transport emits one combined `artFrame` event through the supported
`obs-websocket`/`obs-browser` vendor API. WebSocket publishes one combined JSON message over IPv4 loopback.

## Test setup

| Item | Test configuration |
| --- | --- |
| Platform | Microsoft Windows 11 Pro, version 10.0.26200 |
| CPU | 13th Gen Intel Core i9-13900K |
| RAM | 32 GB |
| GPU | NVIDIA GeForce RTX 4090 |
| OBS | 31.1.1 (development baseline) |
| Benchmark page | `data/browser/transport-benchmark.html` |
| Native benchmark URL | `transport-benchmark.html?transport=native` |
| WebSocket benchmark URL | `transport-benchmark.html?transport=websocket` |
| ART update rate | 60 Hz |
| Browser rendering | Enabled |
| Spectrum | 32 logarithmic bands |
| ART transport mode | **Both** |
| Browser Sources | Native and WebSocket benchmark pages active concurrently |
| Timing methodology | Paired concurrent comparison from the same ART session |
| Observed paired OBS CPU | 0.85% with both transports and benchmark pages active |
| Separately observed isolated CPU range | 0.5-0.9% in both **Native only** and **WebSocket only** modes |

Both benchmark pages ran at the same time and used the same rendering workload. Their counters were exported
separately after approximately 64 seconds for WebSocket and 71 seconds for Native. This paired test gives both
transports the same OBS and audio conditions, but it does not isolate their individual CPU costs. The results are
useful for the proof of concept but are not a substitute for longer, repeated, cross-platform measurements.

## Results

| Metric | Native `artFrame` | WebSocket | Result |
| --- | ---: | ---: | --- |
| Effective receive rate | 60.00 Hz | 60.00 Hz | Equal |
| Dropped messages | 0 | 0 | Equal |
| Mean latency | **0.22 ms** | 0.65 ms | Native about 3x lower |
| p95 latency | **0.60 ms** | 1.10 ms | Native about 2x lower |
| p99 latency | **0.80 ms** | 1.20 ms | Native about 1.5x lower |
| Mean arrival jitter | 0.24 ms | **0.22 ms** | Effectively equal; WebSocket slightly lower |
| Mean arrival interval | 16.66 ms | 16.67 ms | Equal |
| p95 arrival interval | 17.10 ms | 17.10 ms | Equal |
| Mean serialized payload | 1,297 bytes | **627 bytes** | WebSocket about 52% smaller |

The effective rates are derived from the steady-state mean arrival intervals. The WebSocket page runtime starts before
its connection handshake, so dividing its total received count by the full page runtime would incorrectly include the
initial disconnected period.

## Interpretation

Native `artFrame` delivery retained the lowest latency in this Windows test. After replacing WebSocket's 10 ms sender
polling with wake-driven delivery, both transports had effectively equivalent arrival consistency at 60 Hz. Neither
transport produced sequence gaps. Separate earlier runs in isolated transport modes showed the same 0.5-0.9% OBS CPU
range for both variants. The paired run showed 0.85% total OBS CPU with both transports and benchmark pages active;
that combined value cannot be attributed to either transport individually.

The native payload is larger because the public `obs_data` array API represents array entries as objects rather than
primitive numeric values. Consequently, native spectrum values have this representation:

```json
"fft32": [{"v": 0.1}, {"v": 0.2}]
```

WebSocket can use a compact numeric array:

```json
"fft32": [0.1, 0.2]
```

The earlier native run measured 1,388 bytes per frame with the `value` key and the pre-alignment frame layout.
Shortening the key to `v` saves exactly four bytes per band, or 128 bytes for 32 bands. After aligning all other native
fields with the nested WebSocket schema, the final measured mean is approximately 1,297 bytes.

All other `artFrame` fields use the same names and nesting as WebSocket schema version 1. The shared ART JavaScript
client normalizes the spectrum representation for consumers.

## Operational differences

| Topic | Native `artFrame` | WebSocket |
| --- | --- | --- |
| Intended consumers | OBS Browser Sources | Local or external applications and Browser Sources |
| Connection required | No ART-specific connection | WebSocket connection to `127.0.0.1` |
| Delivery scope | Broadcast to every loaded Browser Source | Connected ART clients only |
| Idle behavior | Continues dispatching while Native is enabled | Skips JSON serialization when no clients are connected |
| Source targeting | Not supported by the public `obs-browser` vendor API | Each client chooses whether to connect |
| Dependency | `obs-browser` and `obs-websocket` | Local socket support |
| Payload shape | Spectrum entries are `{ "v": number }` objects | Spectrum entries are numbers |

The native broadcast fan-out is its main limitation. Every loaded Browser Source incurs CEF IPC and event-dispatch
work even if the page has no ART listener. Productions containing many unrelated Browser Sources should measure total
CEF/OBS CPU before choosing Native globally.

## Current recommendation

- Prefer WebSocket for bundled visualizers, general Browser Source use, and external clients. Its measured latency is
  well below one render interval, its payload is smaller, and delivery targets connected clients only.
- Use native `artFrame` explicitly for controlled OBS Browser Source scenes where minimum latency matters more than
  broadcast fan-out.
- Use ART's **Both** mode for compatibility or direct comparisons. In this mode WebSocket serialization remains
  dormant until a client connects.
- Use **Native only** and **WebSocket only** for isolated performance tests.

## Outstanding validation

- Repeat the measurements across multiple runs and record hardware details.
- Test 30 Hz as well as 60 Hz.
- Add genuine 64- and 128-bin analyzer output before benchmarking those sizes.
- Test macOS and Linux.
- Measure behavior with increasing numbers of unrelated Browser Sources.
