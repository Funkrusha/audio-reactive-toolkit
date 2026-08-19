# Experimental native Browser Source events

ART can publish analyzer data to OBS Browser Sources without an ART WebSocket connection. This proof of concept uses
the public in-process `obs-websocket` API to call the `obs-browser` vendor request `emit_event`. The existing ART
WebSocket server remains enabled and is still the interface for external applications.

## Requirements and behavior

- OBS must load both `obs-websocket` and `obs-browser`. Official OBS packages normally include both.
- ART discovers `obs-websocket` in `obs_module_post_load`, after all modules have loaded. Missing dependencies disable
  only native events; the WebSocket server continues normally.
- `obs-browser` broadcasts vendor events to every Browser Source. Its public vendor API does not provide per-source
  targeting. Every loaded Browser Source therefore incurs CEF IPC and event-dispatch work even if its page has no ART
  listener. Event payload fields could identify an intended consumer, but filtering in JavaScript would not avoid that
  fan-out cost.
- ART emits from the OBS tick callback. Serialization, the vendor request, CEF task queuing, and IPC therefore begin on
  the main OBS thread. This PoC deliberately measures that overhead before introducing another queue or thread.
- The event stream is best-effort. `sequence` exposes gaps; `timestamp` is Unix time in milliseconds for latency
  measurements on the same host.

## Event

The continuous event uses the configured WebSocket publication rate (1-60 Hz):

- `artFrame`: the same structure and field names as WebSocket schema version 1: `version`, `timestamp`, `sentAt`,
  `sequence`, `active`, `rms`, `peak`, `bands`, `beat`, `transient`, `tempo`, and `fft32`. The PoC currently exposes
  ART's 32 analyzed bands.

Early PoC revisions emitted continuous `artAudio` and `artSpectrum` events separately. `artFrame` replaces both so a
snapshot requires only one vendor request and one CEF IPC dispatch. The ART JavaScript client derives its public
`frame`, `beat`, and `bpm` events from this frame for identical Native and WebSocket behavior.

The only structural value difference from WebSocket is `fft32`: because `obs_data` arrays contain objects, each native
spectrum item has the form `{ "v": 0.1 }` instead of being a bare number. This adds serialization overhead and is
an explicit subject of the PoC. All other `artFrame` fields match the WebSocket schema.

## Test procedure

Add `native-events-test.html` as a local Browser Source. The page displays the received audio-event rate, last observed
dispatch latency, sequence gaps, BPM, and spectrum. Use the ART setting for 30 Hz and 60 Hz runs. Record OBS total CPU
and the Browser Source CPU from OBS Stats or the operating-system profiler while comparing against `index.html` using
the WebSocket transport.

The current analyzer produces 32 logarithmic bands. The requested 64/128-bin benchmarks require an analyzer output
change and are not represented by duplicating or interpolating these bands, since that would measure serialization
rather than FFT resolution. Cross-platform CPU, latency, dropped-event, and long-runtime results must be collected in
real OBS sessions on Windows, macOS, and Linux before selecting a preferred transport.

## A/B benchmark page

`transport-benchmark.html` runs the same receiver and optional rendering workload for either transport:

```text
transport-benchmark.html?transport=native
transport-benchmark.html?transport=websocket
transport-benchmark.html?transport=native&render=0
transport-benchmark.html?transport=websocket&render=0
```

It reports message rate, sequence gaps, mean/p95/p99 latency, arrival jitter, and average serialized payload size. The
summary can be copied as JSON or selected in a visible text field. Run only one benchmark Browser Source at a time, restart OBS between runs, and use
the same looping media source, ART rate, canvas size, OBS frame rate, and browser hardware-acceleration setting.

For CPU measurements, select **Native only** or **WebSocket only** under **Transport** in ART settings. **WebSocket
only** is the default; **Both** enables both transports for compatibility or direct comparison. Native-only stops the
local WebSocket server; WebSocket-only skips native payload
serialization and CEF dispatch entirely.

## Preliminary Windows results

The following 60 Hz measurements were collected with rendering enabled, **Both** transport mode selected, and both
benchmark Browser Sources active concurrently. This paired setup compares both transports under the same OBS and audio
conditions, but does not isolate their individual CPU costs. These are development-machine results rather than a
cross-platform benchmark.

| Metric | Native `artFrame` | WebSocket |
| --- | ---: | ---: |
| Effective receive rate | 60.00 Hz | 60.00 Hz |
| Dropped messages | 0 | 0 |
| Mean latency | 0.22 ms | 0.65 ms |
| p95 latency | 0.60 ms | 1.10 ms |
| p99 latency | 0.80 ms | 1.20 ms |
| Mean arrival jitter | 0.24 ms | 0.22 ms |
| p95 arrival interval | 17.10 ms | 17.10 ms |
| Mean serialized payload | 1,297 bytes | 627 bytes |

The effective rates use steady-state mean arrival intervals. WebSocket page runtime begins before its connection
handshake and therefore includes a short initial period without messages.

At 60 Hz, native delivery had about three times lower mean latency. Wake-driven WebSocket delivery reduced its jitter
to the same practical range as Native, with WebSocket slightly lower in this run. Neither transport dropped messages.
The final unified native schema with the shortened `v` spectrum key measured about 1,297 bytes per frame, compared
with 627 bytes for WebSocket. Separate earlier runs in **Native only** and **WebSocket only** modes both stayed within
an observed 0.5-0.9% OBS CPU range. The paired run with both transports and benchmark pages active showed 0.85% total
OBS CPU; that combined value cannot be attributed to either transport individually.

Native remains useful when minimum Browser Source latency is the primary concern. WebSocket is the default because its
latency remains far below one 60 Hz render interval while avoiding native broadcast fan-out, using smaller payloads,
and supporting both Browser Sources and external clients. Longer runs, 30 Hz confirmation, 64/128-bin analyzer output,
and macOS/Linux measurements remain outstanding.

Native events are most attractive for controlled scenes with a modest number of Browser Sources. Productions with
many unrelated Browser Sources should compare total CEF CPU before enabling Native globally. The supported
`obs-browser/emit_event` path cannot target one source; using its internal `BrowserSource` dispatch function would add
an unsupported private API dependency. In contrast, ART now skips WebSocket JSON serialization entirely when the
server has no connected clients, including while **Both** mode is selected.
