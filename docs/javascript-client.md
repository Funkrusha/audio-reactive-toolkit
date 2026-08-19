# ART JavaScript client

`art-client.js` provides the same small event API for native OBS Browser Source events and the local WebSocket
transport. Load the client before application scripts, register listeners, and then select a transport:

```html
<script src="art-client.js"></script>
<script>
  ART.on("frame", frame => {
    renderLevels(frame.rms, frame.peak, frame.bands);
    renderSpectrum(frame.fft32);
  });

  ART.on("beat", beat => triggerBeat(beat.strength));

  ART.on("bpm", tempo => updateTempo(tempo.bpm, tempo.confidence, tempo.locked));

  ART.connect();
</script>
```

`ART.connect()` uses WebSocket by default. Browser Source URLs can select the native low-latency transport without
application changes by adding `?transport=native`. The WebSocket port can be changed with `?port=8765`. Explicit
options such as `ART.connect({transport: "native"})` override the URL. The client reconnects WebSocket connections
automatically. Calling `connect` again switches transports, and `ART.disconnect()` stops delivery.
The selected transport is available as `ART.transport` (`"native"` or `"websocket"`) and as the display-ready
`ART.transportLabel` (`"Native"` or `"WebSocket"`).

## Events

| Event | Delivery | Data |
| --- | --- | --- |
| `frame` | At the configured publication rate | Complete schema-version-1 analysis frame |
| `beat` | When `frame.beat.detected` is true | `strength`, `timestamp`, `sentAt`, and `sequence` |
| `bpm` | Initially and whenever the numeric BPM changes | `bpm`, `confidence`, `locked`, `timestamp`, `sentAt`, and `sequence` |

The native `fft32` representation is normalized to the same number array used by WebSocket. `beat` and `bpm` are
derived from complete frames for both transports. This prevents duplicate callbacks and gives a Browser Source loaded
after plugin startup the same initial BPM notification as a WebSocket client.

`ART.on` returns an unsubscribe function. A listener can also be removed explicitly:

```js
const stop = ART.on("beat", onBeat);
stop();

ART.on("beat", onBeat);
ART.off("beat", onBeat);
```
