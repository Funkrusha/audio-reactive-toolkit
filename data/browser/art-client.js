// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

(() => {
  const eventNames = new Set(['frame', 'beat', 'bpm']);
  const listeners = new Map([...eventNames].map(name => [name, new Set()]));
  let transport = null;
  let socket = null;
  let reconnectTimer = null;
  let nativeFrameHandler = null;
  let lastBpm;
  let connectionGeneration = 0;

  function dispatch(name, detail) {
    for (const listener of [...listeners.get(name)]) {
      try {
        listener(detail);
      } catch (error) {
        console.error(`ART ${name} listener failed`, error);
      }
    }
  }

  function normalizeFrame(frame) {
    if (!frame || frame.version !== 1) return null;
    const fft32 = Array.isArray(frame.fft32)
      ? frame.fft32.map(bin => Number(typeof bin === 'object' ? bin?.v : bin) || 0)
      : [];
    return {...frame, fft32};
  }

  function receiveFrame(rawFrame) {
    const frame = normalizeFrame(rawFrame);
    if (!frame) return;

    dispatch('frame', frame);

    if (frame.beat?.detected) {
      dispatch('beat', {
        strength: Number(frame.beat.strength) || 0,
        timestamp: frame.timestamp,
        sentAt: frame.sentAt,
        sequence: frame.sequence
      });
    }

    const bpm = Number(frame.tempo?.bpm);
    if (Number.isFinite(bpm) && bpm !== lastBpm) {
      lastBpm = bpm;
      dispatch('bpm', {
        bpm,
        confidence: Number(frame.tempo.confidence) || 0,
        locked: Boolean(frame.tempo.locked),
        timestamp: frame.timestamp,
        sentAt: frame.sentAt,
        sequence: frame.sequence
      });
    }
  }

  function disconnect() {
    connectionGeneration += 1;
    clearTimeout(reconnectTimer);
    reconnectTimer = null;
    if (nativeFrameHandler) window.removeEventListener('artFrame', nativeFrameHandler);
    nativeFrameHandler = null;
    if (socket) {
      const activeSocket = socket;
      socket = null;
      activeSocket.close();
    }
    transport = null;
    lastBpm = undefined;
  }

  function connectNative() {
    transport = 'native';
    nativeFrameHandler = event => receiveFrame(event.detail);
    window.addEventListener('artFrame', nativeFrameHandler);
  }

  function connectWebSocket(port, reconnectDelay) {
    transport = 'websocket';
    const generation = connectionGeneration;

    function open() {
      if (generation !== connectionGeneration) return;
      const currentSocket = new WebSocket(`ws://127.0.0.1:${port}`);
      socket = currentSocket;
      currentSocket.addEventListener('message', event => {
        try {
          receiveFrame(JSON.parse(event.data));
        } catch {
          // Ignore malformed or incompatible messages.
        }
      });
      currentSocket.addEventListener('close', () => {
        if (socket === currentSocket) socket = null;
        if (generation === connectionGeneration)
          reconnectTimer = setTimeout(open, reconnectDelay);
      });
      currentSocket.addEventListener('error', () => currentSocket.close());
    }

    open();
  }

  const ART = {
    on(name, listener) {
      if (!eventNames.has(name)) throw new Error(`Unknown ART event: ${name}`);
      if (typeof listener !== 'function') throw new TypeError('ART listener must be a function');
      listeners.get(name).add(listener);
      return () => ART.off(name, listener);
    },

    off(name, listener) {
      return eventNames.has(name) && listeners.get(name).delete(listener);
    },

    connect(options = {}) {
      const parameters = new URLSearchParams(window.location.search);
      const requestedTransport = options.transport ??
        (parameters.get('transport') === 'native' ? 'native' : 'websocket');
      const port = (options.port ?? Number(parameters.get('port'))) || 8765;
      const reconnectDelay = options.reconnectDelay ?? 1500;
      if (requestedTransport !== 'native' && requestedTransport !== 'websocket')
        throw new Error(`Unknown ART transport: ${requestedTransport}`);
      disconnect();
      if (requestedTransport === 'native') connectNative();
      else connectWebSocket(Number(port) || 8765, Math.max(0, Number(reconnectDelay) || 0));
      return ART;
    },

    disconnect,

    get transport() {
      return transport;
    },

    get transportLabel() {
      if (transport === 'native') return 'Native';
      if (transport === 'websocket') return 'WebSocket';
      return 'Disconnected';
    }
  };

  window.ART = ART;
})();
