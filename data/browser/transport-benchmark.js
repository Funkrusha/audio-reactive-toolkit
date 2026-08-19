(() => {
  const params = new URLSearchParams(location.search);
  const transport = params.get("transport") === "websocket" ? "websocket" : "native";
  const render = params.get("render") !== "0";
  const port = Number(params.get("port")) || 8765;
  const samples = {latency: [], interval: [], payload: []};
  const maximumSamples = 100000;
  let startedAt = performance.now();
  let previousArrival = 0;
  let previousSequence = 0;
  let received = 0;
  let receivedThisSecond = 0;
  let dropped = 0;
  let socket;

  const element = id => document.querySelector(`#${id}`);
  element("transport").textContent = transport === "native" ? "Native" : "WebSocket";
  element("rendering").textContent = render ? "on" : "off";
  const context = element("spectrum").getContext("2d");

  function addSample(name, value) {
    if (!Number.isFinite(value)) return;
    const list = samples[name];
    if (list.length < maximumSamples) list.push(value);
  }

  function percentile(values, fraction) {
    if (!values.length) return NaN;
    const sorted = [...values].sort((a, b) => a - b);
    return sorted[Math.min(sorted.length - 1, Math.floor(fraction * sorted.length))];
  }

  function mean(values) {
    return values.length ? values.reduce((sum, value) => sum + value, 0) / values.length : NaN;
  }

  function draw(bins) {
    if (!render || !bins?.length) return;
    const values = bins.map(bin => Number(typeof bin === "object" ? bin.v : bin) || 0);
    const width = context.canvas.width / values.length;
    context.clearRect(0, 0, context.canvas.width, context.canvas.height);
    context.fillStyle = "#72e6b1";
    values.forEach((value, index) => {
      const height = Math.min(context.canvas.height, Math.pow(value, 0.55) * context.canvas.height);
      context.fillRect(index * width, context.canvas.height - height, Math.max(1, width - 2), height);
    });
  }

  function receive(data, payloadBytes, bins) {
    const now = performance.now();
    const wallNow = performance.timeOrigin + performance.now();
    received += 1;
    receivedThisSecond += 1;
    if (previousSequence && data.sequence > previousSequence + 1) dropped += data.sequence - previousSequence - 1;
    previousSequence = data.sequence;
    if (previousArrival) addSample("interval", now - previousArrival);
    previousArrival = now;
    addSample("latency", wallNow - (data.sentAt ?? data.timestamp));
    addSample("payload", payloadBytes);
    draw(bins);
  }

  function connectWebSocket() {
    socket = new WebSocket(`ws://127.0.0.1:${port}`);
    socket.addEventListener("open", () => { element("status").textContent = "Connected"; });
    socket.addEventListener("message", event => {
      const data = JSON.parse(event.data);
      receive(data, new Blob([event.data]).size, data.fft32);
    });
    socket.addEventListener("close", () => {
      element("status").textContent = "Reconnecting…";
      setTimeout(connectWebSocket, 1000);
    });
    socket.addEventListener("error", () => socket.close());
  }

  if (transport === "native") {
    element("status").textContent = "Waiting for native events…";
    window.addEventListener("artFrame", event => {
      element("status").textContent = "Receiving";
      receive(event.detail, JSON.stringify(event.detail).length, event.detail.fft32);
    });
  } else {
    connectWebSocket();
  }

  setInterval(() => {
    const expectedInterval = mean(samples.interval);
    element("rate").textContent = `${receivedThisSecond} Hz`;
    element("drops").textContent = String(dropped);
    element("mean").textContent = `${mean(samples.latency).toFixed(2)} ms`;
    element("percentiles").textContent = `${percentile(samples.latency, 0.95).toFixed(2)} / ${percentile(samples.latency, 0.99).toFixed(2)} ms`;
    element("jitter").textContent = `${mean(samples.interval.map(value => Math.abs(value - expectedInterval))).toFixed(2)} ms`;
    element("interval").textContent = `${percentile(samples.interval, 0.95).toFixed(2)} ms`;
    element("messages").textContent = String(received);
    element("payload").textContent = `${mean(samples.payload).toFixed(0)} B`;
    element("runtime").textContent = `${((performance.now() - startedAt) / 1000).toFixed(0)} s`;
    receivedThisSecond = 0;
  }, 1000);

  function results() {
    const intervalMean = mean(samples.interval);
    return {
      transport, render, port, runtimeSeconds: (performance.now() - startedAt) / 1000, received, dropped,
      latencyMeanMs: mean(samples.latency), latencyP95Ms: percentile(samples.latency, 0.95),
      latencyP99Ms: percentile(samples.latency, 0.99), intervalMeanMs: intervalMean,
      jitterMeanMs: mean(samples.interval.map(value => Math.abs(value - intervalMean))),
      intervalP95Ms: percentile(samples.interval, 0.95), payloadMeanBytes: mean(samples.payload)
    };
  }

  function showResults() {
    const output = element("result");
    output.value = JSON.stringify(results(), null, 2);
    output.hidden = false;
    output.focus();
    output.select();
    return output;
  }

  element("show").addEventListener("click", showResults);
  element("copy").addEventListener("click", async () => {
    const output = showResults();
    try {
      await navigator.clipboard.writeText(output.value);
      element("status").textContent = "JSON copied";
    } catch {
      document.execCommand("copy");
      element("status").textContent = "JSON selected – press Ctrl+C";
    }
  });
  element("reset").addEventListener("click", () => location.reload());
})();
