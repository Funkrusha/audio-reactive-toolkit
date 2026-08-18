// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

const art = ARTVisualizer.create({ fov: 46, cameraPosition: [0, 0.5, 12] });
const { renderer, scene, camera, level, spectrum } = art;
const uniforms = {
  time: { value: 0 }, bass: { value: 0 }, mid: { value: 0 }, high: { value: 0 },
  beat: { value: 0 }, spectrum: { value: Array.from({ length: 8 }, () => new THREE.Vector4()) }
};
const material = new THREE.ShaderMaterial({
  uniforms, transparent: true, depthWrite: false, side: THREE.DoubleSide, blending: THREE.AdditiveBlending,
  vertexShader: `
    uniform float time; uniform float bass; uniform float mid; uniform float high; uniform float beat;
    varying vec2 vUv; varying float vWave;
    void main() {
      vUv = uv;
      vec3 p = position;
      float wave = sin(p.x * 0.75 + time * 1.2) * (0.45 + bass * 1.4);
      wave += sin(p.x * 1.8 - time * 1.7 + p.y * 0.5) * (0.18 + mid * 0.8);
      wave += cos(p.y * 1.1 + time * 2.4) * high * 0.42;
      p.z += wave + beat * sin(uv.x * 3.14159) * 0.55;
      p.y += sin(p.x * 0.35 + time * 0.5) * (0.25 + mid * 0.35);
      vWave = wave;
      gl_Position = projectionMatrix * modelViewMatrix * vec4(p, 1.0);
    }
  `,
  fragmentShader: `
    uniform float time; uniform float bass; uniform float mid; uniform float high;
    varying vec2 vUv; varying float vWave;
    void main() {
      float curtain = pow(sin(vUv.x * 3.14159), 0.45);
      float filaments = 0.42 + 0.58 * sin(vUv.x * 46.0 + time * 2.0 + vWave * 3.0);
      filaments = smoothstep(0.05, 1.0, filaments);
      vec3 cyan = vec3(0.0, 0.9, 1.0);
      vec3 violet = vec3(0.48, 0.14, 1.0);
      vec3 pink = vec3(1.0, 0.04, 0.62);
      vec3 color = mix(cyan, violet, vUv.x + mid * 0.18);
      color = mix(color, pink, smoothstep(0.58, 1.0, vUv.x + high * 0.2));
      float edge = smoothstep(0.0, 0.12, vUv.y) * smoothstep(1.0, 0.72, vUv.y);
      float alpha = curtain * edge * (0.13 + filaments * 0.3 + bass * 0.2 + high * 0.14);
      gl_FragColor = vec4(color * (0.7 + filaments + high), alpha);
    }
  `
});
const aurora = new THREE.Group();
scene.add(aurora);
for (let index = 0; index < 4; index += 1) {
  const ribbon = new THREE.Mesh(new THREE.PlaneGeometry(15, 6.2, 128, 48), material.clone());
  ribbon.material.uniforms = THREE.UniformsUtils.clone(uniforms);
  ribbon.position.set(0, 0.5 - index * 0.55, -index * 1.3);
  ribbon.rotation.z = (index - 1.5) * 0.035;
  ribbon.userData.offset = index * 1.7;
  aurora.add(ribbon);
}

const starCount = 650;
const starPositions = new Float32Array(starCount * 3);
for (let index = 0; index < starCount; index += 1) {
  starPositions[index * 3] = (Math.random() - 0.5) * 22;
  starPositions[index * 3 + 1] = (Math.random() - 0.5) * 13;
  starPositions[index * 3 + 2] = -2 - Math.random() * 12;
}
const starsGeometry = new THREE.BufferGeometry();
starsGeometry.setAttribute('position', new THREE.BufferAttribute(starPositions, 3));
const stars = new THREE.Points(starsGeometry, new THREE.PointsMaterial({ color: 0xb8f7ff, size: 0.035, transparent: true, opacity: 0.55, blending: THREE.AdditiveBlending, depthWrite: false }));
scene.add(stars);

function animate() {
  const { elapsed } = art.tick();
  aurora.children.forEach((ribbon, index) => {
    const values = ribbon.material.uniforms;
    values.time.value = elapsed + ribbon.userData.offset;
    values.bass.value = level.bass + spectrum[index * 3] * 0.4;
    values.mid.value = level.mid + spectrum[10 + index * 2] * 0.35;
    values.high.value = level.high + spectrum[22 + index] * 0.3;
    values.beat.value = art.beat;
    ribbon.position.x = Math.sin(elapsed * 0.13 + index) * 0.35;
  });
  stars.rotation.z = elapsed * 0.008;
  stars.material.size = 0.025 + level.high * 0.055 + art.transient * 0.04;
  stars.material.opacity = 0.28 + level.high * 0.5;
  camera.position.x = Math.sin(elapsed * 0.09) * 0.45;
  camera.position.y = 0.6 + Math.cos(elapsed * 0.11) * 0.35;
  camera.lookAt(0, 0, -1.5);
  renderer.render(scene, camera);
  requestAnimationFrame(animate);
}

animate();

