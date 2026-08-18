// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

const art = ARTVisualizer.create({ fov: 58, cameraPosition: [0, 6.5, 13], fog: new THREE.FogExp2(0x03020d, 0.045) });
const { renderer, scene, camera, level, spectrum } = art;
const columns = 32;
const rows = 7;
const geometry = new THREE.BoxGeometry(0.34, 1, 0.65);
geometry.translate(0, 0.5, 0);
const material = new THREE.MeshBasicMaterial({ color: 0x39d9ff, transparent: true, opacity: 0.78, blending: THREE.AdditiveBlending, depthWrite: false, wireframe: true });
const buildings = new THREE.InstancedMesh(geometry, material, columns * rows);
buildings.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
scene.add(buildings);
const dummy = new THREE.Object3D();

const grid = new THREE.GridHelper(34, 48, 0xff27c8, 0x164e63);
grid.position.y = -0.02;
grid.material.transparent = true;
grid.material.opacity = 0.32;
scene.add(grid);

const horizon = new THREE.Mesh(
  new THREE.CircleGeometry(2.6, 96),
  new THREE.MeshBasicMaterial({ color: 0xff2bc2, transparent: true, opacity: 0.22, blending: THREE.AdditiveBlending, depthWrite: false })
);
horizon.position.set(0, 3.4, -14);
scene.add(horizon);

const lanes = new THREE.Group();
for (let index = -4; index <= 4; index += 1) {
  const points = [new THREE.Vector3(index * 1.4, 0.015, 9), new THREE.Vector3(index * 3.5, 0.015, -22)];
  const line = new THREE.Line(new THREE.BufferGeometry().setFromPoints(points), new THREE.LineBasicMaterial({ color: index % 2 ? 0x22d3ee : 0x8b5cf6, transparent: true, opacity: 0.24, blending: THREE.AdditiveBlending }));
  lanes.add(line);
}
scene.add(lanes);

function animate() {
  const { elapsed } = art.tick();
  let instance = 0;
  for (let row = 0; row < rows; row += 1) {
    for (let column = 0; column < columns; column += 1) {
      const centered = column - (columns - 1) / 2;
      const fft = spectrum[column];
      const depthPulse = 0.78 + 0.22 * Math.sin(elapsed * 1.4 - row * 0.72 + column * 0.18);
      const height = 0.12 + fft * (3.8 + row * 0.34) * depthPulse + level.rms * 0.22;
      dummy.position.set(centered * 0.43, 0, 4.2 - row * 1.65);
      dummy.scale.set(0.76 + level.high * 0.16, height + art.beat * 0.18, 0.82);
      dummy.updateMatrix();
      buildings.setMatrixAt(instance, dummy.matrix);
      instance += 1;
    }
  }
  buildings.instanceMatrix.needsUpdate = true;
  material.color.setHSL(0.52 + level.mid * 0.27, 0.95, 0.56 + level.high * 0.12);
  material.opacity = 0.42 + level.rms * 0.34 + art.transient * 0.18;
  grid.position.z = (elapsed * (0.7 + level.rms * 1.4)) % 0.7;
  grid.material.opacity = 0.18 + level.high * 0.3;
  horizon.scale.setScalar(1 + level.bass * 0.35 + art.beat * 0.22);
  horizon.material.opacity = 0.12 + level.bass * 0.3 + art.beat * 0.25;
  camera.position.x = Math.sin(elapsed * 0.12) * 2.1;
  camera.position.y = 5.5 + Math.cos(elapsed * 0.16) * 0.45;
  camera.position.z = 12.5 + Math.sin(elapsed * 0.09) * 0.5;
  camera.lookAt(0, 1.1, -4.5);
  renderer.render(scene, camera);
  requestAnimationFrame(animate);
}

animate();
