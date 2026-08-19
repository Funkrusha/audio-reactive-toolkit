// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

const art = ARTVisualizer.create({ fov: 48, cameraPosition: [0, 1, 12.5] });
const { renderer, scene, camera, level, spectrum } = art;
const UP = new THREE.Vector3(0, 1, 0);
const emerald = new THREE.Color(0x1de9b6);
const violet = new THREE.Color(0xa78bfa);
const white = new THREE.Color(0xffffff);

// --- Faceted gem core -------------------------------------------------
const coreGeometry = new THREE.OctahedronGeometry(1.9, 1).toNonIndexed();
coreGeometry.computeVertexNormals();
const coreUniforms = {
  time: { value: 0 },
  bass: { value: 0 },
  mid: { value: 0 },
  high: { value: 0 },
  beat: { value: 0 }
};
const core = new THREE.Mesh(
  coreGeometry,
  new THREE.ShaderMaterial({
    uniforms: coreUniforms,
    transparent: true,
    depthWrite: false,
    side: THREE.FrontSide,
    blending: THREE.AdditiveBlending,
    vertexShader: `
      uniform float time;
      uniform float bass;
      uniform float mid;
      uniform float beat;
      varying vec3 vNormal;
      varying vec3 vViewPos;
      varying float vFacet;

      float rand(vec3 seed) {
        return fract(sin(dot(seed, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
      }

      void main() {
        vFacet = rand(normal);
        float growth = 0.05 + bass * 0.42 + beat * 0.3;
        float shimmer = sin(time * 1.6 + vFacet * 20.0) * mid * 0.09;
        vec3 p = position + normal * (growth * (0.55 + vFacet * 0.85) + shimmer);
        vec4 viewPos = modelViewMatrix * vec4(p, 1.0);
        vNormal = normalize(normalMatrix * normal);
        vViewPos = viewPos.xyz;
        gl_Position = projectionMatrix * viewPos;
      }
    `,
    fragmentShader: `
      uniform float high;
      uniform float beat;
      varying vec3 vNormal;
      varying vec3 vViewPos;
      varying float vFacet;

      void main() {
        vec3 viewDir = normalize(-vViewPos);
        float fresnel = pow(1.0 - clamp(dot(normalize(vNormal), viewDir), 0.0, 1.0), 2.4);
        vec3 emerald = vec3(0.11, 0.91, 0.71);
        vec3 violet = vec3(0.65, 0.47, 0.98);
        vec3 base = mix(emerald, violet, vFacet);
        float sparkle = pow(fresnel, 5.0) * (0.5 + high * 2.4);
        vec3 color = base * (0.3 + fresnel * 0.85) + vec3(1.0) * sparkle + beat * 0.22;
        float alpha = clamp(0.4 + fresnel * 0.5 + beat * 0.22, 0.0, 0.95);
        gl_FragColor = vec4(color, alpha);
      }
    `
  })
);
scene.add(core);

const coreEdges = new THREE.LineSegments(
  new THREE.EdgesGeometry(coreGeometry, 1),
  new THREE.LineBasicMaterial({ color: 0xd8b4fe, transparent: true, opacity: 0.4, blending: THREE.AdditiveBlending })
);
coreEdges.scale.setScalar(1.015);
scene.add(coreEdges);

// --- Growing crystal spikes, arranged on a Fibonacci sphere -----------
function fibonacciSphere(count) {
  const points = [];
  const golden = Math.PI * (3 - Math.sqrt(5));
  for (let index = 0; index < count; index += 1) {
    const y = 1 - (index / (count - 1)) * 2;
    const radius = Math.sqrt(Math.max(0, 1 - y * y));
    const theta = golden * index;
    points.push(new THREE.Vector3(Math.cos(theta) * radius, y, Math.sin(theta) * radius));
  }
  return points;
}

const spikeCount = 40;
const spikeGeometry = new THREE.ConeGeometry(0.13, 1, 5, 1);
spikeGeometry.translate(0, 0.5, 0);
const spikeMaterial = new THREE.MeshBasicMaterial({
  color: 0xffffff,
  vertexColors: true,
  transparent: true,
  opacity: 0.88,
  blending: THREE.AdditiveBlending,
  depthWrite: false
});
const spikes = new THREE.InstancedMesh(spikeGeometry, spikeMaterial, spikeCount);
spikes.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
scene.add(spikes);

const dummy = new THREE.Object3D();
const spikeSeeds = fibonacciSphere(spikeCount).map((direction, index) => ({
  direction,
  band: index % 32,
  phase: Math.random() * Math.PI * 2,
  speed: 0.6 + Math.random() * 0.8
}));

// --- Beat shockwave rings ----------------------------------------------
const shockwaves = Array.from({ length: 3 }, (_, index) => {
  const wave = new THREE.Mesh(
    new THREE.RingGeometry(0.95, 1, 6, 1),
    new THREE.MeshBasicMaterial({
      color: index % 2 ? 0xa78bfa : 0x2dd4bf,
      transparent: true,
      opacity: 0,
      side: THREE.DoubleSide,
      blending: THREE.AdditiveBlending,
      depthWrite: false
    })
  );
  wave.userData.life = 0;
  scene.add(wave);
  return wave;
});
let previousBeat = 0;

// --- Drifting mineral dust ----------------------------------------------
const dustCount = 500;
const dustPositions = new Float32Array(dustCount * 3);
const dustColors = new Float32Array(dustCount * 3);
for (let index = 0; index < dustCount; index += 1) {
  const radius = 3.4 + Math.random() * 6.5;
  const theta = Math.random() * Math.PI * 2;
  const phi = Math.acos(2 * Math.random() - 1);
  dustPositions[index * 3] = radius * Math.sin(phi) * Math.cos(theta);
  dustPositions[index * 3 + 1] = radius * Math.cos(phi);
  dustPositions[index * 3 + 2] = radius * Math.sin(phi) * Math.sin(theta);
  const tint = Math.random() > 0.5 ? emerald : violet;
  dustColors.set([tint.r, tint.g, tint.b], index * 3);
}
const dustGeometry = new THREE.BufferGeometry();
dustGeometry.setAttribute('position', new THREE.BufferAttribute(dustPositions, 3));
dustGeometry.setAttribute('color', new THREE.BufferAttribute(dustColors, 3));
const dust = new THREE.Points(
  dustGeometry,
  new THREE.PointsMaterial({ size: 0.03, vertexColors: true, transparent: true, opacity: 0.5, blending: THREE.AdditiveBlending, depthWrite: false })
);
scene.add(dust);

function animate() {
  const { delta, elapsed } = art.tick();

  coreUniforms.time.value = elapsed;
  coreUniforms.bass.value = level.bass;
  coreUniforms.mid.value = level.mid;
  coreUniforms.high.value = level.high;
  coreUniforms.beat.value = art.beat;
  core.rotation.y = elapsed * (0.09 + level.mid * 0.18);
  core.rotation.x = Math.sin(elapsed * 0.22) * 0.16;
  const coreScale = 1 + level.rms * 0.1 + art.beat * 0.08;
  core.scale.setScalar(coreScale);
  coreEdges.rotation.copy(core.rotation);
  coreEdges.scale.setScalar(coreScale * 1.015);
  coreEdges.material.opacity = 0.28 + level.mid * 0.4 + art.transient * 0.2;

  if (art.beat > previousBeat + 0.25) {
    const wave = shockwaves.find(item => item.userData.life <= 0) || shockwaves[0];
    wave.userData.life = 1;
  }
  previousBeat = art.beat;

  const fracture = art.beat * 0.55;
  spikeSeeds.forEach((seed, index) => {
    const fft = spectrum[seed.band];
    const twinkle = 0.08 * Math.sin(elapsed * seed.speed + seed.phase);
    const length = 0.3 + fft * 2.1 + level.bass * 0.25 + twinkle;
    const push = fracture * (0.4 + seed.direction.y * 0.2);
    dummy.position.copy(seed.direction).multiplyScalar(1.05 + push);
    dummy.quaternion.setFromUnitVectors(UP, seed.direction);
    const girth = 0.7 + fft * 0.6;
    dummy.scale.set(girth, Math.max(0.05, length), girth);
    dummy.updateMatrix();
    spikes.setMatrixAt(index, dummy.matrix);
    const tint = emerald.clone().lerp(violet, (seed.direction.y + 1) * 0.5).lerp(white, Math.min(0.6, fft * 0.8));
    spikes.setColorAt(index, tint);
  });
  spikes.instanceMatrix.needsUpdate = true;
  if (spikes.instanceColor) spikes.instanceColor.needsUpdate = true;
  spikes.rotation.y = core.rotation.y * 0.6;

  shockwaves.forEach(wave => {
    wave.userData.life = Math.max(0, wave.userData.life - delta * 1.4);
    const progress = 1 - wave.userData.life;
    wave.scale.setScalar(1.1 + progress * 6.5);
    wave.material.opacity = wave.userData.life * 0.6;
    wave.lookAt(camera.position);
  });

  dust.rotation.y = elapsed * (0.015 + level.mid * 0.03);
  dust.rotation.x = Math.sin(elapsed * 0.09) * 0.06;
  dust.material.opacity = 0.32 + level.high * 0.4;
  dust.material.size = 0.024 + level.high * 0.05 + art.transient * 0.03;

  camera.position.x = Math.sin(elapsed * 0.11) * 1.1;
  camera.position.y = 1 + Math.cos(elapsed * 0.14) * 0.4;
  camera.lookAt(0, 0, 0);

  renderer.render(scene, camera);
  requestAnimationFrame(animate);
}

animate();
