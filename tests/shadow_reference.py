"""Independent numeric check: horizontal wall shadow vs footprint intersection.

This checks the geometry concept. It does not execute Unity or the C# mesh code.
Run: python tests/shadow_reference.py
"""

import numpy as np


def face_shadows(origin, center, half, rotation, clearance=0.0):
    corners = np.array([center + rotation @ (half * np.array([
        1 if i & 1 else -1, 1 if i & 2 else -1, 1 if i & 4 else -1
    ])) for i in range(8)])
    footprint = np.unique(corners[:, [0, 2]], axis=0)
    footprint = footprint[np.argsort(np.arctan2(footprint[:, 1] - footprint[:, 1].mean(),
                                            footprint[:, 0] - footprint[:, 0].mean()))]
    trunk = origin[[0, 2]]
    shadows = []
    for a, b in zip(footprint, np.roll(footprint, -1, axis=0)):
        edge = b - a
        outward = np.array([edge[1], -edge[0]]) / np.linalg.norm(edge)
        middle = (a + b) / 2
        if outward @ (trunk - middle) <= 0.001:
            continue
        entry = a + outward * clearance
        planes = [(np.array([-outward[0], 0.0, -outward[1]]), np.array([entry[0], 0.0, entry[1]]))]
        for endpoint in (a, b):
            ray = endpoint - trunk
            side = np.array([-ray[1], ray[0]]) / np.linalg.norm(ray)
            if side @ (middle - trunk) < 0:
                side = -side
            planes.append((np.array([side[0], 0.0, side[1]]), origin))
        planes.extend(((np.array([0.0, 1.0, 0.0]), np.array([0.0, corners[:, 1].min(), 0.0])),
                       (np.array([0.0, -1.0, 0.0]), np.array([0.0, corners[:, 1].max(), 0.0]))))
        shadows.append(planes)
    return shadows


def segment_intersects(origin, point, center, half, rotation):
    local_start = rotation.T @ (origin - center)
    local_step = rotation.T @ (point - origin)
    low, high = 0.0, 1.0
    for axis in (0, 2):
        if abs(local_step[axis]) < 1e-10:
            if abs(local_start[axis]) > half[axis]:
                return False
            continue
        a = (-half[axis] - local_start[axis]) / local_step[axis]
        b = (half[axis] - local_start[axis]) / local_step[axis]
        low, high = max(low, min(a, b)), min(high, max(a, b))
    return low <= high


def aabb_may_intersect(planes, center, extents):
    return all(normal @ (center - anchor) + np.abs(normal) @ extents >= -1e-4
               for normal, anchor in planes)


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
            corners_y = np.array([(center + rotation @ (half * np.array([
                1 if i & 1 else -1, 1 if i & 2 else -1, 1 if i & 4 else -1
            ])))[1] for i in range(8)])
            intersects = (segment_intersects(origin, point, center, half, rotation)
                          and corners_y.min() <= point[1] <= corners_y.max())
            if in_shadow != intersects:
                raise AssertionError((origin, center, half, point, in_shadow, intersects))
            if in_shadow:
                offset = rng.uniform(-1.0, 1.0, 3)
                bounds_center = point + offset
                bounds_extents = np.abs(offset) + 0.1
                assert any(aabb_may_intersect(volume, bounds_center, bounds_extents)
                           for volume in shadows)
            cases += 1
    # A low wall's side/top silhouette must not grow when clearance is added.
    origin = np.array([3.0, 0.0, 0.0])
    center = np.zeros(3)
    half = np.ones(3)
    rotation = np.eye(3)
    high_leaf = np.array([-6.0, 7.0, 0.0])
    for clearance in (0.0, 0.25, 0.5):
        assert not any(all(normal @ (high_leaf - anchor) >= -1e-6 for normal, anchor in volume)
                       for volume in face_shadows(origin, center, half, rotation, clearance))
    # Its trunk-to-leaf segment crosses the wall, but the leaf is above it.
    above_wall = np.array([-6.0, 2.0, 0.0])
    assert segment_intersects(origin, above_wall, center, half, rotation)
    for clearance in (0.0, 0.25, 0.5):
        assert not any(all(normal @ (above_wall - anchor) >= -1e-6 for normal, anchor in volume)
                       for volume in face_shadows(origin, center, half, rotation, clearance))
    behind_at_wall_height = np.array([-6.0, 0.0, 0.0])
    for clearance in (0.0, 0.05, 0.25):
        assert any(all(normal @ (behind_at_wall_height - anchor) >= -1e-6 for normal, anchor in volume)
                   for volume in face_shadows(origin, center, half, rotation, clearance))
    # A high fir branch passes through an upper wall. The ground-level trunk ray
    # would miss that wall; the horizontal branch-height shadow must retain it.
    elevated_center = np.array([0.0, 10.0, 0.0])
    elevated_leaf = np.array([-6.0, 10.0, 0.0])
    assert any(all(normal @ (elevated_leaf - anchor) >= -1e-6 for normal, anchor in volume)
               for volume in face_shadows(origin, elevated_center, half, rotation, 0.05))
    print(f"Verified {cases} random horizontal-footprint cases")


if __name__ == "__main__":
    main()
