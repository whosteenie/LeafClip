"""Check the topology rule against locally extracted Valheim assets.

The OBJ files are proprietary and ignored by git. Run artifacts/extract_pine.py
first on a matching local installation, then run this script.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "artifacts" / "pine"
EXPECTED = {"Pinetree_01": (2071, 104), "Pinetree_01_lod_02": (461, 23), "Pine_tree": (596, 56)}


def classify(path):
    vertices, triangles = [], []
    for line in path.read_text().splitlines():
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "v": vertices.append(tuple(float(value) for value in parts[1:4]))
        elif parts[0] == "f": triangles.append(tuple(int(value.split("/")[0]) - 1 for value in parts[1:4]))
    parent = list(range(len(vertices)))

    def find(index):
        while parent[index] != index:
            parent[index] = parent[parent[index]]
            index = parent[index]
        return index

    for a, b, c in triangles:
        parent[find(b)] = find(a)
        parent[find(c)] = find(a)
    components = {}
    for i, point in enumerate(vertices):
        root = find(i)
        if root not in components: components[root] = [float("inf"), float("-inf"), 0]
        components[root][0] = min(components[root][0], point[1])
        components[root][1] = max(components[root][1], point[1])
    for a, _, _ in triangles: components[find(a)][2] += 1
    trunk = max((root for root in components if components[root][2] >= 12),
                key=lambda root: components[root][1] - components[root][0])
    height = max(point[1] for point in vertices) - min(point[1] for point in vertices)
    assert components[trunk][1] - components[trunk][0] >= 0.5 * height
    assert components[trunk][0] <= min(point[1] for point in vertices) + 0.1 * height
    return len(triangles), components[trunk][2]


for name, expected in EXPECTED.items():
    result = classify(ROOT / f"{name}.obj")
    assert result == expected, (name, result, expected)
    print(name, "total triangles", result[0], "preserved trunk triangles", result[1],
          "canopy candidates", result[0] - result[1])
