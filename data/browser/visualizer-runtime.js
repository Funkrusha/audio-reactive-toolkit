// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

window.ARTVisualizer = {
  create({ fov = 50, cameraPosition = [0, 0, 14], fog = null } = {}) {
    const canvas = document.getElementById('visualizer');
    const status = document.getElementById('status');
    const tempo = document.getElementById('tempo');
    const bpmValue = document.getElementById('bpm-value');
    const configuredPort = Number(new URLSearchParams(window.location.search).get('port')) || 8765;
    const renderer = new THREE.WebGLRenderer({ canvas, alpha: true, antialias: true, powerPreference: 'high-performance' });
    renderer.setClearColor(0x000000, 0);
    renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 1.5));
    renderer.outputColorSpace = THREE.SRGBColorSpace;

    const scene = new THREE.Scene();
    if (fog) scene.fog = fog;
    const camera = new THREE.PerspectiveCamera(fov, 1, 0.1, 120);
    camera.position.set(...cameraPosition);
    const target = { rms: 0, peak: 0, bass: 0, mid: 0, high: 0 };
    const level = { ...target };
    const spectrum = new Float32Array(32);
    const clock = new THREE.Clock();
    let beat = 0;
    let transient = 0;
    let socket;
    let reconnectTimer;

    const scaleAudio = (value, gain) => Math.min(1, Math.max(0, Number(value) * gain));
    const smooth = (current, next, attack = 0.48, release = 0.1) =>
      current + (next - current) * (next > current ? attack : release);

    function setStatus(connected) {
      status.textContent = connected ? 'Connected' : 'Disconnected';
      status.className = `status ${connected ? 'connected' : 'disconnected'}`;
    }

    function updateAudio(data) {
      if (data.version !== 1 || !data.bands) return;
      target.rms = scaleAudio(data.rms, 3.5);
      target.peak = scaleAudio(data.peak, 2.4);
      target.bass = scaleAudio(data.bands.bass, 4.8);
      target.mid = scaleAudio(data.bands.mid, 6.5);
      target.high = scaleAudio(data.bands.high, 11);
      if (Array.isArray(data.fft32)) {
        data.fft32.slice(0, spectrum.length).forEach((value, index) => {
          spectrum[index] = scaleSpectrum(value, 14);
        });
      }
      if (data.beat?.detected) beat = Math.max(beat, 0.5 + Number(data.beat.strength) * 0.7);
      if (data.transient?.detected)
        transient = Math.max(transient, 0.35 + Number(data.transient.strength) * 0.65);
      if (data.tempo) {
        const bpm = Number(data.tempo.bpm);
        bpmValue.textContent = Number.isFinite(bpm) && bpm > 0 ? bpm.toFixed(1) : '\u2014';
        tempo.classList.toggle('locked', Boolean(data.tempo.locked));
      }
    }

    function connect() {
      clearTimeout(reconnectTimer);
      socket = new WebSocket(`ws://127.0.0.1:${configuredPort}`);
      socket.addEventListener('open', () => setStatus(true));
      socket.addEventListener('message', event => {
        try { updateAudio(JSON.parse(event.data)); } catch { /* Ignore malformed or future messages. */ }
      });
      socket.addEventListener('close', () => {
        setStatus(false);
        reconnectTimer = setTimeout(connect, 1500);
      });
      socket.addEventListener('error', () => socket.close());
    }

    function resize() {
      const width = Math.max(1, window.innerWidth);
      const height = Math.max(1, window.innerHeight);
      renderer.setSize(width, height, false);
      camera.aspect = width / height;
      camera.updateProjectionMatrix();
    }

    const api = {
      renderer, scene, camera, level, target, spectrum,
      get beat() { return beat; },
      get transient() { return transient; },
      tick() {
        const delta = Math.min(clock.getDelta(), 0.05);
        for (const name of Object.keys(level)) level[name] = smooth(level[name], target[name]);
        beat *= Math.pow(0.045, delta);
        transient *= Math.pow(0.02, delta);
        return { delta, elapsed: clock.elapsedTime };
      }
    };

    window.addEventListener('resize', resize);
    resize();
    connect();
    return api;
  }
};

