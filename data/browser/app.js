// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

const elements = Object.fromEntries(
  ['status', 'rms', 'peak', 'bass', 'mid', 'high', 'spectrum', 'beat-indicator', 'transient-indicator', 'bpm-value', 'tempo-confidence', 'rms-value', 'peak-value', 'bass-value', 'mid-value', 'high-value']
    .map(id => [id, document.getElementById(id)])
);

const scale = value => Math.min(100, Math.max(0, value * 300));
let socket;
let reconnectTimer;
const configuredPort = Number(new URLSearchParams(window.location.search).get('port')) || 8765;
const spectrumBars = Array.from({ length: 32 }, () => {
  const bar = document.createElement('div');
  bar.className = 'spectrum-bar';
  elements.spectrum?.appendChild(bar);
  return bar;
});
let beatTimer;
let transientTimer;

function triggerEvent(name, strength) {
  const className = name === 'beat' ? 'beat' : 'transient';
  const indicator = elements[`${name}-indicator`];
  const duration = Math.round(70 + Math.min(1, Math.max(0, strength)) * 90);
  document.body.classList.add(className);
  indicator?.classList.add('active');
  if (name === 'beat') {
    clearTimeout(beatTimer);
    beatTimer = setTimeout(() => {
      document.body.classList.remove(className);
      indicator?.classList.remove('active');
    }, duration);
  } else {
    clearTimeout(transientTimer);
    transientTimer = setTimeout(() => {
      document.body.classList.remove(className);
      indicator?.classList.remove('active');
    }, duration);
  }
}

function setStatus(connected) {
  elements.status.textContent = connected ? 'Connected' : 'Disconnected';
  elements.status.className = `status ${connected ? 'connected' : 'disconnected'}`;
}

function update(data) {
  if (data.version !== 1 || !data.bands) return;
  for (const name of ['rms', 'peak']) {
    elements[name].style.width = `${scale(data[name])}%`;
    elements[`${name}-value`].value = Number(data[name]).toFixed(3);
  }
  for (const name of ['bass', 'mid', 'high']) {
    elements[name].style.height = `${scale(data.bands[name])}%`;
    elements[`${name}-value`].value = Number(data.bands[name]).toFixed(3);
  }
  if (Array.isArray(data.fft32)) {
    data.fft32.slice(0, spectrumBars.length).forEach((value, index) => {
      spectrumBars[index].style.height = `${Math.max(1, scaleSpectrum(value, 9) * 100)}%`;
    });
  }
  if (data.beat?.detected) triggerEvent('beat', Number(data.beat.strength));
  if (data.transient?.detected) triggerEvent('transient', Number(data.transient.strength));
  if (data.tempo) {
    const bpm = Number(data.tempo.bpm);
    elements['bpm-value'].textContent = bpm > 0 ? bpm.toFixed(1) : '—';
    elements['tempo-confidence'].textContent = data.tempo.locked
      ? `${Math.round(Number(data.tempo.confidence) * 100)}% locked`
      : 'learning';
  }
}

function connect() {
  clearTimeout(reconnectTimer);
  socket = new WebSocket(`ws://127.0.0.1:${configuredPort}`);
  socket.addEventListener('open', () => setStatus(true));
  socket.addEventListener('message', event => {
    try { update(JSON.parse(event.data)); } catch { /* Ignore incomplete or future messages. */ }
  });
  socket.addEventListener('close', () => {
    setStatus(false);
    reconnectTimer = setTimeout(connect, 1500);
  });
  socket.addEventListener('error', () => socket.close());
}

connect();
