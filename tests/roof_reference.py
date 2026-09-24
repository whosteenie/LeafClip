"""Check sloped-box height slices against exact horizontal ray/box hits."""

import numpy as np


def hull(points):
    ordered = sorted(set(tuple(point) for point in points))
    if len(ordered) < 3:
        return ordered

    def cross(a, b, c):
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])

    lower, upper = [], []
    for point in ordered:
        while len(lower) > 1 and cross(lower[-2], lower[-1], point) <= 1e-8:
            lower.pop()
        lower.append(point)
    for point in reversed(ordered):
        while len(upper) > 1 and cross(upper[-2], upper[-1], point) <= 1e-8:
            upper.pop()
        upper.append(point)
    return lower[:-1] + upper[:-1]


def projected_slice(corners, low, high):
    points = []
    for i, corner in enumerate(corners):
        if low - 1e-5 <= corner[1] <= high + 1e-5:
            points.append(corner[[0, 2]])
        for bit in (1, 2, 4):
            if i & bit:
                continue
            other = corners[i | bit]
            if abs(other[1] - corner[1]) < 1e-6:
                continue
            for boundary in (low, high):
                t = (boundary - corner[1]) / (other[1] - corner[1])
                if 0 <= t <= 1:
                    points.append((corner + t * (other - corner))[[0, 2]])
    return hull(points)


def shadows(corners, origin, slices):
    low, high = corners[:, 1].min(), corners[:, 1].max()
    trunk = origin[[0, 2]]
    result = []
    for section in range(slices):
        bottom = low + (high - low) * section / slices
        top = low + (high - low) * (section + 1) / slices
        footprint = projected_slice(corners, bottom, top)
        for a, b in zip(footprint, footprint[1:] + footprint[:1]):
            a, b = np.array(a), np.array(b)
            edge = b - a
            outward = np.array([edge[1], -edge[0]]) / np.linalg.norm(edge)
            middle = (a + b) / 2
            if outward @ (trunk - middle) <= 0.001:
                continue
            planes = [(-outward, a)]
            for end in (a, b):
                ray = end - trunk
                side = np.array([-ray[1], ray[0]]) / np.linalg.norm(ray)
                if side @ (middle - trunk) < 0:
                    side = -side
                planes.append((side, trunk))
            result.append((bottom, top, planes))
    return result


def covered(point, volumes):
    planar = point[[0, 2]]
    return any(bottom - 1e-6 <= point[1] <= top + 1e-6 and
               all(normal @ (planar - anchor) >= -1e-6 for normal, anchor in planes)
               for bottom, top, planes in volumes)


def intersects_box(origin, point, center, half, rotation):
    start = np.array([origin[0], point[1], origin[2]])
    local_start = rotation.T @ (start - center)
    local_step = rotation.T @ (point - start)
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
    rng = np.random.default_rng(241015)
    positives = 0
    false_positives = 0
    for _ in range(12):
        tilt, yaw = rng.uniform(0.3, 0.8), rng.uniform(-0.5, 0.5)
        cx, sx, cy, sy = np.cos(tilt), np.sin(tilt), np.cos(yaw), np.sin(yaw)
        rotation = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]]) @ np.array(
            [[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
        center, half = np.zeros(3), np.array([1.1, 0.16, 1.5])
        corners = np.array([center + rotation @ (half * np.array([
            1 if i & 1 else -1, 1 if i & 2 else -1, 1 if i & 4 else -1
        ])) for i in range(8)])
        origin = np.array([4.0, 0.0, 0.0])
        count = min(4, max(1, int(np.ceil((corners[:, 1].max() - corners[:, 1].min()) / 0.5))))
        volumes = shadows(corners, origin, count)
        for _ in range(500):
            point = rng.uniform([-5, -2, -3], [4, 2, 3])
            exact = intersects_box(origin, point, center, half, rotation)
            approximate = covered(point, volumes)
            if exact:
                positives += 1
                assert approximate, (point, tilt, yaw)
            elif approximate:
                false_positives += 1
    print(f"Verified {positives} sloped-roof ray hits; {false_positives} conservative extra hits")


if __name__ == "__main__":
    main()
