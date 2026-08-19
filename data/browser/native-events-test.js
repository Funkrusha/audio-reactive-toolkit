(() => {
  const rate = document.querySelector("#rate");
  const latency = document.querySelector("#latency");
  const gaps = document.querySelector("#gaps");
  const bpm = document.querySelector("#bpm");
  const transport = document.querySelector("#transport");
  const log = document.querySelector("#log");
  const canvas = document.querySelector("#spectrum");
  const context = canvas.getContext("2d");
  let audioCount = 0;
  let gapCount = 0;
  let lastSequence = 0;

  function observe(detail) {
    if (lastSequence && detail.sequence > lastSequence + 1) gapCount += detail.sequence - lastSequence - 1;
    lastSequence = detail.sequence;
    gaps.textContent = String(gapCount);
    latency.textContent = `${Math.max(0, performance.timeOrigin + performance.now() - detail.sentAt).toFixed(2)} ms`;
  }

  function record(name, detail) {
    observe(detail);
    log.textContent = `${name} ${JSON.stringify(detail)}\n${log.textContent}`.slice(0, 8000);
  }

  ART.on("frame", detail => {
    audioCount += 1;
    observe(detail);
    const bins = detail.fft32;
    const width = canvas.width / bins.length;
    context.clearRect(0, 0, canvas.width, canvas.height);
    context.fillStyle = "#72e6b1";
    bins.forEach((value, index) => {
      const height = Math.min(canvas.height, Math.pow(value, 0.55) * canvas.height);
      context.fillRect(index * width, canvas.height - height, Math.max(1, width - 2), height);
    });
  });
  ART.on("beat", detail => record("beat", detail));
  ART.on("bpm", detail => {
    bpm.textContent = detail.bpm > 0 ? detail.bpm.toFixed(1) : "--";
    record("bpm", detail);
  });
  ART.connect({transport: "native"});
  transport.textContent = ART.transportLabel;
  setInterval(() => {
    rate.textContent = `${audioCount} Hz`;
    audioCount = 0;
  }, 1000);
})();
