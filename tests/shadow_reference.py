"""Independent numeric check: face-shadow membership vs segment/OBB intersection.

This checks the geometry concept. It does not execute Unity or the C# mesh code.
Run: python tests/shadow_reference.py
"""

import numpy as np


FACES = ((0, 2, 6, 4), (1, 5, 7, 3), (0, 4, 5, 1),
         (2, 3, 7, 6), (0, 1, 3, 2), (4, 6, 7, 5))


def face_shadows(origin, center, half, rotation):
    corners = np.array([center + rotation @ (half * np.array([
        1 if i & 1 else -1, 1 if i & 2 else -1, 1 if i & 4 else -1
    ])) for i in range(8)])
    shadows = []
    for ids in FACES:
        face = corners[list(ids)]
        middle = face.mean(axis=0)
        normal = np.cross(face[1] - face[0], face[2] - face[1])
        normal /= np.linalg.norm(normal)
        if normal @ (middle - center) < 0:
            normal = -normal
        if normal @ (origin - middle) <= 0.001:
            continue
        planes = [(-normal, middle)]
        for j in range(4):
            side = np.cross(face[j] - origin, face[(j + 1) % 4] - origin)
            side /= np.linalg.norm(side)
            if side @ (middle - origin) < 0:
                side = -side
            planes.append((side, origin))
        shadows.append(planes)
    return shadows


def segment_intersects(origin, point, center, half, rotation):
    local_start = rotation.T @ (origin - center)
    local_step = rotation.T @ (point - origin)
    low, high = 0.0, 1.0
    for axis in range(3):
        if abs(local_step[axis]) < 1e-10:
            if abs(local_start[axis]) > half[axis]:
                return False
            continue
        a = (-half[axis] - local_start[axis]) / local_step[axis]
        b = (half[axis] - local_start[axis]) / local_step[axis]
        low, high = max(low, min(a, b)), min(high, max(a, b))
    return low <= high


def main():
    rng = np.random.default_rng(441015)
    cases = 0
    for _ in range(100):
        angle = rng.uniform(-np.pi, np.pi)
        c, s = np.cos(angle), np.sin(angle)
        rotation = np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
        center = rng.uniform(-5, 5, 3)
        half = rng.uniform(0.3, 2.0, 3)
        origin = center + rotation @ np.array([half[0] + rng.uniform(1, 5), 0.3, 0.2])
        shadows = face_shadows(origin, center, half, rotation)
        for _ in range(1000):
            point = center + rng.uniform(-12, 12, 3)
            in_shadow = any(all(normal @ (point - anchor) >= -1e-6 for normal, anchor in volume)
                            for volume in shadows)
            intersects = segment_intersects(origin, point, center, half, rotation)
            if in_shadow != intersects:
                raise AssertionError((origin, center, half, point, in_shadow, intersects))
            cases += 1
    print(f"Verified {cases} random segment/rotated-box cases")


if __name__ == "__main__":
    main()
