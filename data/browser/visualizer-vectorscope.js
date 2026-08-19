// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

const canvas = document.getElementById('visualizer');
const context = canvas.getContext('2d', { alpha: true });
const status = document.getElementById('status');
const tempo = document.getElementById('tempo');
const bpmValue = document.getElementById('bpm-value');
const yLabel = document.getElementById('y-label');
const parameters = new URLSearchParams(window.location.search);
const axisMode = parameters.get('axes') === 'bass-high' ? 'bass-high' : 'bass-mid';
const yBand = axisMode === 'bass-high' ? 'high' : 'mid';
yLabel.textContent = yBand.toUpperCase();

const trail = [];
const maximumTrail = 260;
const target = { x: 0, y: 0 };
const value = { x: 0, y: 0 };
const ceiling = { x: 0.04, y: 0.04 };
let beatPulse = 0;
let transientPulse = 0;
let lastTime = performance.now();

function setStatus(connected) {
  status.textContent = connected ? `Connected · ${ART.transportLabel}` : 'Disconnected';
  status.className = `status ${connected ? 'connected' : 'disconnected'}`;
}

function updateAudio(data) {
  if (data.version !== 1 || !data.bands) return;
  setStatus(true);
  const bass = Math.max(0, Number(data.bands.bass) || 0);
  const secondary = Math.max(0, Number(data.bands[yBand]) || 0);
  ceiling.x = Math.max(bass, ceiling.x * 0.997);
  ceiling.y = Math.max(secondary, ceiling.y * 0.997);
  target.x = Math.min(1, bass / Math.max(0.002, ceiling.x));
  target.y = Math.min(1, secondary / Math.max(0.002, ceiling.y));
  if (data.beat?.detected) beatPulse = Math.max(beatPulse, 0.5 + Number(data.beat.strength) * 0.7);
  if (data.transient?.detected)
    transientPulse = Math.max(transientPulse, 0.35 + Number(data.transient.strength) * 0.65);
  if (data.tempo) {
    const bpm = Number(data.tempo.bpm);
    bpmValue.textContent = Number.isFinite(bpm) && bpm > 0 ? bpm.toFixed(1) : '\u2014';
    tempo.classList.toggle('locked', Boolean(data.tempo.locked));
  }
}

function resize() {
  const ratio = Math.min(window.devicePixelRatio || 1, 1.5);
  canvas.width = Math.max(1, Math.floor(window.innerWidth * ratio));
  canvas.height = Math.max(1, Math.floor(window.innerHeight * ratio));
  canvas.style.width = `${window.innerWidth}px`;
  canvas.style.height = `${window.innerHeight}px`;
  context.setTransform(ratio, 0, 0, ratio, 0, 0);
}

function drawGrid(width, height, padding) {
  const left = padding;
  const top = padding;
  const right = width - padding;
  const bottom = height - padding;
  context.save();
  context.strokeStyle = 'rgba(34, 211, 238, 0.13)';
  context.lineWidth = 1;
  for (let division = 0; division <= 4; division += 1) {
    const x = left + (right - left) * division / 4;
    const y = top + (bottom - top) * division / 4;
    context.beginPath(); context.moveTo(x, top); context.lineTo(x, bottom); context.stroke();
    context.beginPath(); context.moveTo(left, y); context.lineTo(right, y); context.stroke();
  }
  context.strokeStyle = 'rgba(244, 114, 182, 0.24)';
  context.strokeRect(left, top, right - left, bottom - top);
  context.restore();
}

function drawTrail(width, height, padding) {
  if (trail.length < 2) return;
  const plotWidth = width - padding * 2;
  const plotHeight = height - padding * 2;
  context.save();
  context.lineCap = 'round';
  context.lineJoin = 'round';
  for (let pass = 0; pass < 3; pass += 1) {
    context.beginPath();
    trail.forEach((point, index) => {
      const x = padding + point.x * plotWidth;
      const y = height - padding - point.y * plotHeight;
      if (index === 0) context.moveTo(x, y); else context.lineTo(x, y);
    });
    context.strokeStyle = pass === 2 ? 'rgba(235, 250, 255, 0.82)' : pass === 1 ? 'rgba(236, 72, 153, 0.32)' : 'rgba(34, 211, 238, 0.2)';
    context.lineWidth = pass === 2 ? 1.4 + transientPulse * 2 : pass === 1 ? 5 + beatPulse * 5 : 14 + beatPulse * 10;
    context.shadowBlur = pass === 2 ? 5 : 22;
    context.shadowColor = pass === 1 ? '#ec4899' : '#22d3ee';
    context.stroke();
  }
  const latest = trail[trail.length - 1];
  const x = padding + latest.x * plotWidth;
  const y = height - padding - latest.y * plotHeight;
  context.beginPath();
  context.arc(x, y, 4 + beatPulse * 9, 0, Math.PI * 2);
  context.fillStyle = '#ffffff';
  context.shadowBlur = 28;
  context.shadowColor = '#f472b6';
  context.fill();
  context.restore();
}

function animate(now) {
  const delta = Math.min((now - lastTime) / 1000, 0.05);
  lastTime = now;
  value.x += (target.x - value.x) * (target.x > value.x ? 0.42 : 0.09);
  value.y += (target.y - value.y) * (target.y > value.y ? 0.42 : 0.09);
  beatPulse *= Math.pow(0.045, delta);
  transientPulse *= Math.pow(0.02, delta);
  trail.push({ x: value.x, y: value.y });
  if (trail.length > maximumTrail) trail.shift();

  const width = window.innerWidth;
  const height = window.innerHeight;
  const padding = Math.max(42, Math.min(width, height) * 0.09);
  context.clearRect(0, 0, width, height);
  drawGrid(width, height, padding);
  drawTrail(width, height, padding);
  requestAnimationFrame(animate);
}

window.addEventListener('resize', resize);
resize();
ART.on('frame', updateAudio);
ART.connect();
requestAnimationFrame(animate);
