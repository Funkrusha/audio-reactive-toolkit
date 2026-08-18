// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

(() => {
  const parameters = new URLSearchParams(window.location.search);
  const requestedMode = (parameters.get('scale') || 'linear').toLowerCase();
  const mode = requestedMode === 'log' ? 'db' : requestedMode;
  const requestedFloor = Number(parameters.get('floor'));
  const floorDb = Number.isFinite(requestedFloor)
    ? Math.min(-20, Math.max(-100, requestedFloor))
    : -60;

  window.spectrumScaleMode = ['linear', 'perceptual', 'db'].includes(mode) ? mode : 'linear';
  window.scaleSpectrum = (value, gain = 1) => {
    const linear = Math.min(1, Math.max(0, Number(value) * gain));
    if (window.spectrumScaleMode === 'perceptual')
      return Math.pow(linear, 0.55);
    if (window.spectrumScaleMode === 'db') {
      if (linear <= 0) return 0;
      const decibels = 20 * Math.log10(linear);
      return Math.min(1, Math.max(0, (decibels - floorDb) / -floorDb));
    }
    return linear;
  };
})();
